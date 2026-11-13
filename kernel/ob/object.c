#include <horizon/ob.h>
#include <horizon/mm.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

#define OB_MAGIC 0x484E544FU /* 'HNTO' */

static ob_type_t            g_types[OBJ_TYPE_MAX];
static hnt_object_header_t *g_root_dir = NULL;
static kspinlock_t          g_ob_lock  = KSPINLOCK_INIT;

static bool is_sep(char c) {
    return c == '\\' || c == '/';
}

static hnt_object_header_t *find_child(hnt_object_header_t *dir, const char *name) {
    for (hnt_object_header_t *c = dir->first_child; c; c = c->next_sibling) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

static void detach_from_parent(hnt_object_header_t *hdr) {
    if (!hdr->parent) return;
    hnt_object_header_t *p = hdr->parent;
    if (p->first_child == hdr) {
        p->first_child = hdr->next_sibling;
    } else {
        for (hnt_object_header_t *c = p->first_child; c; c = c->next_sibling) {
            if (c->next_sibling == hdr) {
                c->next_sibling = hdr->next_sibling;
                break;
            }
        }
    }
    hdr->parent = NULL;
    hdr->next_sibling = NULL;
}

void ob_register_type(const ob_type_t *type_info) {
    if (!type_info || type_info->id <= OBJ_TYPE_NONE || type_info->id >= OBJ_TYPE_MAX) return;
    g_types[type_info->id] = *type_info;
}

void ob_init(void) {
    memset(g_types, 0, sizeof(g_types));

    ob_type_t dir_type = {
        .id = OBJ_TYPE_DIRECTORY,
        .name = "Directory",
        .body_size = 0,
        .on_close = NULL,
        .on_delete = NULL,
    };
    ob_register_type(&dir_type);

    g_root_dir = (hnt_object_header_t *)kzalloc(sizeof(hnt_object_header_t));
    KASSERT(g_root_dir != NULL);
    g_root_dir->magic     = OB_MAGIC;
    g_root_dir->type      = OBJ_TYPE_DIRECTORY;
    g_root_dir->ref_count = 1;
    strcpy(g_root_dir->name, "\\");

    void *tmp = NULL;
    KASSERT(NT_SUCCESS(ob_create_object(OBJ_TYPE_DIRECTORY, 0, "\\Device", &tmp)));
    KASSERT(NT_SUCCESS(ob_create_object(OBJ_TYPE_DIRECTORY, 0, "\\Driver", &tmp)));
    KASSERT(NT_SUCCESS(ob_create_object(OBJ_TYPE_DIRECTORY, 0, "\\KernelObjects", &tmp)));
    KASSERT(NT_SUCCESS(ob_create_object(OBJ_TYPE_DIRECTORY, 0, "\\SystemRoot", &tmp)));
}

static NTSTATUS resolve_parent_and_leaf(const char *path, hnt_object_header_t **out_parent,
                                        char *leaf_buf, size_t leaf_cap) {
    if (!path || !is_sep(path[0])) return STATUS_OBJECT_NAME_INVALID;

    hnt_object_header_t *cur = g_root_dir;
    const char *p = path;
    while (is_sep(*p)) p++;
    if (*p == '\0') return STATUS_OBJECT_NAME_INVALID;

    char seg[OB_MAX_NAME];
    for (;;) {
        size_t len = 0;
        while (*p && !is_sep(*p)) {
            if (len + 1 >= sizeof(seg)) return STATUS_OBJECT_NAME_INVALID;
            seg[len++] = *p++;
        }
        seg[len] = '\0';
        while (is_sep(*p)) p++;

        if (*p == '\0') {
            if (len == 0 || len >= leaf_cap) return STATUS_OBJECT_NAME_INVALID;
            strcpy(leaf_buf, seg);
            *out_parent = cur;
            return STATUS_SUCCESS;
        }

        if (cur->type != OBJ_TYPE_DIRECTORY) return STATUS_OBJECT_NAME_NOT_FOUND;
        hnt_object_header_t *next = find_child(cur, seg);
        if (!next) return STATUS_OBJECT_NAME_NOT_FOUND;
        cur = next;
    }
}

NTSTATUS ob_create_object(ob_type_id_t type, size_t extra_size, const char *path, void **out_body) {
    if (type <= OBJ_TYPE_NONE || type >= OBJ_TYPE_MAX || !out_body) {
        return STATUS_INVALID_PARAMETER;
    }
    size_t body_sz = g_types[type].body_size + extra_size;
    if (body_sz == 0) body_sz = 8;

    hnt_object_header_t *hdr = (hnt_object_header_t *)kzalloc(sizeof(hnt_object_header_t) + body_sz);
    if (!hdr) return STATUS_NO_MEMORY;

    hdr->magic     = OB_MAGIC;
    hdr->type      = type;
    hdr->ref_count = 1;

    if (path && path[0] != '\0') {
        uint64_t flags = spin_lock_irqsave(&g_ob_lock);
        hnt_object_header_t *parent = NULL;
        char leaf[OB_MAX_NAME];
        NTSTATUS st = resolve_parent_and_leaf(path, &parent, leaf, sizeof(leaf));
        if (!NT_SUCCESS(st)) {
            spin_unlock_irqrestore(&g_ob_lock, flags);
            kfree(hdr);
            return st;
        }
        if (find_child(parent, leaf) != NULL) {
            spin_unlock_irqrestore(&g_ob_lock, flags);
            kfree(hdr);
            return STATUS_OBJECT_NAME_COLLISION;
        }
        strcpy(hdr->name, leaf);
        hdr->parent = parent;
        hdr->next_sibling = parent->first_child;
        parent->first_child = hdr;
        spin_unlock_irqrestore(&g_ob_lock, flags);
    }

    *out_body = OB_HDR_TO_BODY(hdr);
    return STATUS_SUCCESS;
}

void ob_ref_object(void *body) {
    if (!body) return;
    hnt_object_header_t *hdr = OB_BODY_TO_HDR(body);
    KASSERT(hdr->magic == OB_MAGIC);
    __atomic_add_fetch(&hdr->ref_count, 1, __ATOMIC_RELAXED);
}

void ob_deref_object(void *body) {
    if (!body) return;
    hnt_object_header_t *hdr = OB_BODY_TO_HDR(body);
    KASSERT(hdr->magic == OB_MAGIC);
    int32_t rem = __atomic_sub_fetch(&hdr->ref_count, 1, __ATOMIC_ACQ_REL);
    if (rem <= 0) {
        uint64_t flags = spin_lock_irqsave(&g_ob_lock);
        detach_from_parent(hdr);
        spin_unlock_irqrestore(&g_ob_lock, flags);

        if (hdr->type < OBJ_TYPE_MAX && g_types[hdr->type].on_delete) {
            g_types[hdr->type].on_delete(body);
        }
        kfree(hdr);
    }
}

NTSTATUS ob_lookup_object(const char *path, ob_type_id_t expected_type, void **out_body) {
    if (!path || !out_body) return STATUS_INVALID_PARAMETER;
    uint64_t flags = spin_lock_irqsave(&g_ob_lock);

    if (is_sep(path[0]) && path[1] == '\0') {
        if (expected_type != OBJ_TYPE_NONE && expected_type != OBJ_TYPE_DIRECTORY) {
            spin_unlock_irqrestore(&g_ob_lock, flags);
            return STATUS_OBJECT_TYPE_MISMATCH;
        }
        void *body = OB_HDR_TO_BODY(g_root_dir);
        ob_ref_object(body);
        *out_body = body;
        spin_unlock_irqrestore(&g_ob_lock, flags);
        return STATUS_SUCCESS;
    }

    hnt_object_header_t *parent = NULL;
    char leaf[OB_MAX_NAME];
    NTSTATUS st = resolve_parent_and_leaf(path, &parent, leaf, sizeof(leaf));
    if (!NT_SUCCESS(st)) {
        spin_unlock_irqrestore(&g_ob_lock, flags);
        return st;
    }
    hnt_object_header_t *found = find_child(parent, leaf);
    if (!found) {
        spin_unlock_irqrestore(&g_ob_lock, flags);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (expected_type != OBJ_TYPE_NONE && found->type != expected_type) {
        spin_unlock_irqrestore(&g_ob_lock, flags);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    void *body = OB_HDR_TO_BODY(found);
    ob_ref_object(body);
    *out_body = body;
    spin_unlock_irqrestore(&g_ob_lock, flags);
    return STATUS_SUCCESS;
}

static void dump_dir_recursive(hnt_object_header_t *node, int depth) {
    for (int i = 0; i < depth; i++) kprintf("  ");
    const char *tname = (node->type < OBJ_TYPE_MAX && g_types[node->type].name)
                        ? g_types[node->type].name : "Unknown";
    kprintf("%s%s" ANSI_RESET " " ANSI_GRAY "[%s, refs=%d]" ANSI_RESET "\n",
            node->type == OBJ_TYPE_DIRECTORY ? ANSI_BRIGHT_BLUE : ANSI_WHITE,
            node->name, tname, node->ref_count);
    for (hnt_object_header_t *c = node->first_child; c; c = c->next_sibling) {
        dump_dir_recursive(c, depth + 1);
    }
}

void ob_dump_namespace(void) {
    uint64_t flags = spin_lock_irqsave(&g_ob_lock);
    dump_dir_recursive(g_root_dir, 0);
    spin_unlock_irqrestore(&g_ob_lock, flags);
}

/* ---------------- Per-process Handle Table ---------------- */
void ob_handle_table_init(ob_handle_table_t *table) {
    memset(table, 0, sizeof(*table));
}

void ob_handle_table_destroy(ob_handle_table_t *table) {
    for (size_t i = 0; i < OB_MAX_HANDLES; i++) {
        if (table->entries[i].object) {
            void *obj = table->entries[i].object;
            hnt_object_header_t *hdr = OB_BODY_TO_HDR(obj);
            __atomic_sub_fetch(&hdr->handle_count, 1, __ATOMIC_RELAXED);
            if (hdr->type < OBJ_TYPE_MAX && g_types[hdr->type].on_close) {
                g_types[hdr->type].on_close(obj);
            }
            ob_deref_object(obj);
            table->entries[i].object = NULL;
        }
    }
}

NTSTATUS ob_open_handle(ob_handle_table_t *table, void *body, ACCESS_MASK access, HANDLE *out_handle) {
    if (!table || !body || !out_handle) return STATUS_INVALID_PARAMETER;
    uint64_t flags = spin_lock_irqsave(&table->lock);
    /* Start handles at 3 (0,1,2 reserved or used for stdin/stdout/stderr) */
    for (size_t i = 0; i < OB_MAX_HANDLES; i++) {
        if (table->entries[i].object == NULL) {
            ob_ref_object(body);
            hnt_object_header_t *hdr = OB_BODY_TO_HDR(body);
            __atomic_add_fetch(&hdr->handle_count, 1, __ATOMIC_RELAXED);
            table->entries[i].object = body;
            table->entries[i].access = access;
            *out_handle = (HANDLE)i;
            spin_unlock_irqrestore(&table->lock, flags);
            return STATUS_SUCCESS;
        }
    }
    spin_unlock_irqrestore(&table->lock, flags);
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS ob_ref_by_handle(ob_handle_table_t *table, HANDLE handle, ob_type_id_t expected_type,
                          ACCESS_MASK desired_access, void **out_body) {
    if (!table || !out_body || handle >= OB_MAX_HANDLES) return STATUS_INVALID_HANDLE;
    uint64_t flags = spin_lock_irqsave(&table->lock);
    ob_handle_entry_t *e = &table->entries[handle];
    if (!e->object) {
        spin_unlock_irqrestore(&table->lock, flags);
        return STATUS_INVALID_HANDLE;
    }
    if ((e->access & desired_access) != desired_access) {
        spin_unlock_irqrestore(&table->lock, flags);
        return STATUS_ACCESS_DENIED;
    }
    hnt_object_header_t *hdr = OB_BODY_TO_HDR(e->object);
    if (expected_type != OBJ_TYPE_NONE && hdr->type != expected_type) {
        spin_unlock_irqrestore(&table->lock, flags);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    ob_ref_object(e->object);
    *out_body = e->object;
    spin_unlock_irqrestore(&table->lock, flags);
    return STATUS_SUCCESS;
}

NTSTATUS ob_close_handle(ob_handle_table_t *table, HANDLE handle) {
    if (!table || handle >= OB_MAX_HANDLES) return STATUS_INVALID_HANDLE;
    uint64_t flags = spin_lock_irqsave(&table->lock);
    void *obj = table->entries[handle].object;
    if (!obj) {
        spin_unlock_irqrestore(&table->lock, flags);
        return STATUS_INVALID_HANDLE;
    }
    table->entries[handle].object = NULL;
    table->entries[handle].access = 0;
    spin_unlock_irqrestore(&table->lock, flags);

    hnt_object_header_t *hdr = OB_BODY_TO_HDR(obj);
    __atomic_sub_fetch(&hdr->handle_count, 1, __ATOMIC_RELAXED);
    if (hdr->type < OBJ_TYPE_MAX && g_types[hdr->type].on_close) {
        g_types[hdr->type].on_close(obj);
    }
    ob_deref_object(obj);
    return STATUS_SUCCESS;
}
