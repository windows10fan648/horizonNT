#pragma once

#include <horizon/types.h>
#include <horizon/spinlock.h>

#define OB_MAX_NAME 64
#define OB_MAX_HANDLES 64

typedef enum {
    OBJ_TYPE_NONE = 0,
    OBJ_TYPE_DIRECTORY,
    OBJ_TYPE_PROCESS,
    OBJ_TYPE_THREAD,
    OBJ_TYPE_DEVICE,
    OBJ_TYPE_FILE,
    OBJ_TYPE_EVENT,
    OBJ_TYPE_MAX
} ob_type_id_t;

struct hnt_object_header;

typedef void (*ob_close_fn_t)(void *body);
typedef void (*ob_delete_fn_t)(void *body);

typedef struct {
    ob_type_id_t   id;
    const char    *name;
    size_t         body_size;
    ob_close_fn_t  on_close;
    ob_delete_fn_t on_delete;
} ob_type_t;

typedef struct hnt_object_header {
    uint32_t          magic;
    ob_type_id_t      type;
    volatile int32_t  ref_count;
    volatile int32_t  handle_count;
    char              name[OB_MAX_NAME];
    struct hnt_object_header *parent;
    struct hnt_object_header *first_child;
    struct hnt_object_header *next_sibling;
} hnt_object_header_t;

typedef struct {
    void        *object;
    ACCESS_MASK  access;
    uint32_t     flags;
} ob_handle_entry_t;

typedef struct {
    kspinlock_t       lock;
    ob_handle_entry_t entries[OB_MAX_HANDLES];
} ob_handle_table_t;

#define OB_BODY_TO_HDR(body) ((hnt_object_header_t *)(body) - 1)
#define OB_HDR_TO_BODY(hdr)  ((void *)((hnt_object_header_t *)(hdr) + 1))

void     ob_init(void);
void     ob_register_type(const ob_type_t *type_info);

NTSTATUS ob_create_object(ob_type_id_t type, size_t extra_size, const char *path, void **out_body);
void     ob_ref_object(void *body);
void     ob_deref_object(void *body);
NTSTATUS ob_lookup_object(const char *path, ob_type_id_t expected_type, void **out_body);
void     ob_dump_namespace(void);
size_t   ob_list_namespace(char *buf, size_t max_len);

/* Handle table management (per-process) */
void     ob_handle_table_init(ob_handle_table_t *table);
void     ob_handle_table_destroy(ob_handle_table_t *table);
NTSTATUS ob_open_handle(ob_handle_table_t *table, void *body, ACCESS_MASK access, HANDLE *out_handle);
NTSTATUS ob_ref_by_handle(ob_handle_table_t *table, HANDLE handle, ob_type_id_t expected_type,
                          ACCESS_MASK desired_access, void **out_body);
NTSTATUS ob_close_handle(ob_handle_table_t *table, HANDLE handle);
