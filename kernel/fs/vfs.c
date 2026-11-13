#include <horizon/fs.h>
#include <horizon/mm.h>
#include <horizon/spinlock.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

static fs_node_t  *g_fs_root = NULL;
static kspinlock_t g_fs_lock = KSPINLOCK_INIT;

static bool is_sep(char c) {
    return c == '/' || c == '\\';
}

static fs_node_t *alloc_node(const char *name, fs_node_type_t type, fs_node_t *parent) {
    fs_node_t *n = (fs_node_t *)kzalloc(sizeof(fs_node_t));
    if (!n) return NULL;
    strncpy(n->name, name, FS_MAX_NAME - 1);
    n->type   = type;
    n->parent = parent;
    if (parent) {
        n->next_sibling = parent->first_child;
        parent->first_child = n;
    }
    return n;
}

static fs_node_t *find_child(fs_node_t *dir, const char *name) {
    for (fs_node_t *c = dir->first_child; c; c = c->next_sibling) {
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

void fs_init(void) {
    g_fs_root = alloc_node("/", FS_NODE_DIR, NULL);
    KASSERT(g_fs_root != NULL);

    fs_node_t *dir = NULL;
    fs_create_file("/bin", FS_NODE_DIR, &dir);
    fs_create_file("/etc", FS_NODE_DIR, &dir);
    fs_create_file("/tmp", FS_NODE_DIR, &dir);

    const char *motd =
        "Welcome to horizonNT 0.1.0 (x86_64 Hybrid Object-Based Kernel)\n"
        "Combining NT Object Manager & IRP I/O with Unix-clean Syscalls & ELF64.\n";
    fs_populate_file("/etc/motd", motd, strlen(motd), true);

    const char *ver = "horizonNT kernel 0.1.0-x86_64\n";
    fs_populate_file("/etc/version", ver, strlen(ver), true);
}

fs_node_t *fs_root(void) {
    return g_fs_root;
}

fs_node_t *fs_lookup(const char *path) {
    if (!path || !g_fs_root) return NULL;
    uint64_t flags = spin_lock_irqsave(&g_fs_lock);

    const char *p = path;
    if (strncmp(p, "\\SystemRoot", 11) == 0) p += 11;
    while (is_sep(*p)) p++;

    fs_node_t *cur = g_fs_root;
    char seg[FS_MAX_NAME];
    while (*p) {
        size_t len = 0;
        while (*p && !is_sep(*p)) {
            if (len + 1 >= sizeof(seg)) {
                spin_unlock_irqrestore(&g_fs_lock, flags);
                return NULL;
            }
            seg[len++] = *p++;
        }
        seg[len] = '\0';
        while (is_sep(*p)) p++;

        if (strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        if (cur->type != FS_NODE_DIR) {
            spin_unlock_irqrestore(&g_fs_lock, flags);
            return NULL;
        }
        cur = find_child(cur, seg);
        if (!cur) {
            spin_unlock_irqrestore(&g_fs_lock, flags);
            return NULL;
        }
    }

    spin_unlock_irqrestore(&g_fs_lock, flags);
    return cur;
}

NTSTATUS fs_create_file(const char *path, fs_node_type_t type, fs_node_t **out_node) {
    if (!path || !g_fs_root) return STATUS_INVALID_PARAMETER;
    uint64_t flags = spin_lock_irqsave(&g_fs_lock);

    const char *p = path;
    if (strncmp(p, "\\SystemRoot", 11) == 0) p += 11;
    while (is_sep(*p)) p++;
    if (*p == '\0') {
        spin_unlock_irqrestore(&g_fs_lock, flags);
        return STATUS_OBJECT_NAME_INVALID;
    }

    fs_node_t *cur = g_fs_root;
    char seg[FS_MAX_NAME];
    for (;;) {
        size_t len = 0;
        while (*p && !is_sep(*p)) {
            if (len + 1 >= sizeof(seg)) {
                spin_unlock_irqrestore(&g_fs_lock, flags);
                return STATUS_OBJECT_NAME_INVALID;
            }
            seg[len++] = *p++;
        }
        seg[len] = '\0';
        while (is_sep(*p)) p++;

        if (*p == '\0') {
            fs_node_t *existing = find_child(cur, seg);
            if (existing) {
                spin_unlock_irqrestore(&g_fs_lock, flags);
                if (out_node) *out_node = existing;
                return STATUS_OBJECT_NAME_COLLISION;
            }
            fs_node_t *created = alloc_node(seg, type, cur);
            spin_unlock_irqrestore(&g_fs_lock, flags);
            if (!created) return STATUS_NO_MEMORY;
            if (out_node) *out_node = created;
            return STATUS_SUCCESS;
        }

        fs_node_t *next = find_child(cur, seg);
        if (!next) {
            next = alloc_node(seg, FS_NODE_DIR, cur);
            if (!next) {
                spin_unlock_irqrestore(&g_fs_lock, flags);
                return STATUS_NO_MEMORY;
            }
        } else if (next->type != FS_NODE_DIR) {
            spin_unlock_irqrestore(&g_fs_lock, flags);
            return STATUS_OBJECT_NAME_INVALID;
        }
        cur = next;
    }
}

NTSTATUS fs_write_node(fs_node_t *node, uint64_t offset, const void *data, size_t len, size_t *out_written) {
    if (!node || !data) return STATUS_INVALID_PARAMETER;
    if (node->type != FS_NODE_FILE) return STATUS_INVALID_DEVICE_REQUEST;
    if (node->readonly) return STATUS_ACCESS_DENIED;

    uint64_t flags = spin_lock_irqsave(&g_fs_lock);
    size_t needed = (size_t)offset + len;
    if (needed > node->capacity) {
        size_t new_cap = needed < 256 ? 256 : needed * 2;
        uint8_t *nbuf = (uint8_t *)krealloc(node->data, new_cap);
        if (!nbuf) {
            spin_unlock_irqrestore(&g_fs_lock, flags);
            return STATUS_NO_MEMORY;
        }
        if (new_cap > node->capacity) {
            memset(nbuf + node->capacity, 0, new_cap - node->capacity);
        }
        node->data     = nbuf;
        node->capacity = new_cap;
    }

    memcpy(node->data + offset, data, len);
    if (needed > node->size) node->size = needed;
    if (out_written) *out_written = len;

    spin_unlock_irqrestore(&g_fs_lock, flags);
    return STATUS_SUCCESS;
}

NTSTATUS fs_read_node(fs_node_t *node, uint64_t offset, void *buf, size_t len, size_t *out_read) {
    if (!node || !buf) return STATUS_INVALID_PARAMETER;
    if (node->type != FS_NODE_FILE) return STATUS_INVALID_DEVICE_REQUEST;

    uint64_t flags = spin_lock_irqsave(&g_fs_lock);
    if (offset >= node->size) {
        if (out_read) *out_read = 0;
        spin_unlock_irqrestore(&g_fs_lock, flags);
        return STATUS_END_OF_FILE;
    }

    size_t avail = node->size - (size_t)offset;
    size_t to_copy = len < avail ? len : avail;
    memcpy(buf, node->data + offset, to_copy);
    if (out_read) *out_read = to_copy;

    spin_unlock_irqrestore(&g_fs_lock, flags);
    return STATUS_SUCCESS;
}

NTSTATUS fs_populate_file(const char *path, const void *data, size_t len, bool readonly) {
    fs_node_t *node = NULL;
    NTSTATUS st = fs_create_file(path, FS_NODE_FILE, &node);
    if (!NT_SUCCESS(st) && st != STATUS_OBJECT_NAME_COLLISION) return st;
    if (node && data && len > 0) {
        node->readonly = false;
        st = fs_write_node(node, 0, data, len, NULL);
        node->readonly = readonly;
        return st;
    }
    if (node) node->readonly = readonly;
    return STATUS_SUCCESS;
}
