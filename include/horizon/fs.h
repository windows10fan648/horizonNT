#pragma once

#include <horizon/types.h>

#define FS_MAX_NAME 64
#define FS_MAX_PATH 256

typedef enum {
    FS_NODE_FILE = 1,
    FS_NODE_DIR  = 2
} fs_node_type_t;

typedef struct fs_node {
    char            name[FS_MAX_NAME];
    fs_node_type_t  type;
    uint8_t        *data;
    size_t          size;
    size_t          capacity;
    bool            readonly;
    struct fs_node *parent;
    struct fs_node *first_child;
    struct fs_node *next_sibling;
} fs_node_t;

void       fs_init(void);
fs_node_t *fs_root(void);
fs_node_t *fs_lookup(const char *path);
NTSTATUS   fs_create_file(const char *path, fs_node_type_t type, fs_node_t **out_node);
NTSTATUS   fs_write_node(fs_node_t *node, uint64_t offset, const void *data, size_t len, size_t *out_written);
NTSTATUS   fs_read_node(fs_node_t *node, uint64_t offset, void *buf, size_t len, size_t *out_read);
NTSTATUS   fs_populate_file(const char *path, const void *data, size_t len, bool readonly);
