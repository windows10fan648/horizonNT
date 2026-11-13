#pragma once

#include <horizon/types.h>

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002U

#define MULTIBOOT_INFO_MEMORY      (1U << 0)
#define MULTIBOOT_INFO_CMDLINE     (1U << 2)
#define MULTIBOOT_INFO_MODS        (1U << 3)
#define MULTIBOOT_INFO_MEM_MAP     (1U << 6)

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED  2

typedef struct HNT_PACKED {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} multiboot_module_t;

typedef struct HNT_PACKED {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} multiboot_mmap_entry_t;

typedef struct HNT_PACKED {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
} multiboot_info_t;
