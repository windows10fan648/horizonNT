#pragma once

#include <horizon/types.h>
#include <horizon/ps.h>

#define ELF_MAGIC 0x464C457FU

#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct HNT_PACKED {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct HNT_PACKED {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

NTSTATUS elf_spawn_process(const char *name, const void *image, size_t size, kprocess_t **out_proc);
NTSTATUS elf_spawn_from_fs(const char *path, kprocess_t **out_proc);
