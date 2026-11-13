#pragma once

#include <horizon/types.h>
#include <horizon/multiboot.h>

/* Page table flags (x86_64) */
#define PTE_PRESENT  (1ULL << 0)
#define PTE_WRITABLE (1ULL << 1)
#define PTE_USER     (1ULL << 2)
#define PTE_PWT      (1ULL << 3)
#define PTE_PCD      (1ULL << 4)
#define PTE_ACCESSED (1ULL << 5)
#define PTE_DIRTY    (1ULL << 6)
#define PTE_HUGE     (1ULL << 7)
#define PTE_GLOBAL   (1ULL << 8)
#define PTE_NX       (1ULL << 63)

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

/* VMM abstract protection flags */
#define VMM_READ   0x01
#define VMM_WRITE  0x02
#define VMM_EXEC   0x04
#define VMM_USER   0x08

typedef struct {
    phys_addr_t pml4_phys;
} vmm_space_t;

typedef struct {
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
    uint64_t total_bytes;
    uint64_t free_bytes;
} pmm_stats_t;

typedef struct {
    size_t heap_total;
    size_t heap_used;
    size_t heap_free;
    size_t alloc_count;
} heap_stats_t;

/* PMM */
void        pmm_init(const multiboot_info_t *mb_info);
phys_addr_t pmm_alloc_page(void);
phys_addr_t pmm_alloc_pages(size_t count);
void        pmm_free_page(phys_addr_t phys);
void        pmm_free_pages(phys_addr_t phys, size_t count);
void        pmm_get_stats(pmm_stats_t *out);

/* VMM */
void         vmm_init(void);
vmm_space_t *vmm_kernel_space(void);
vmm_space_t *vmm_create_user_space(void);
void         vmm_destroy_user_space(vmm_space_t *space);
void         vmm_switch_space(vmm_space_t *space);
NTSTATUS     vmm_map_page(vmm_space_t *space, virt_addr_t virt, phys_addr_t phys, uint32_t flags);
NTSTATUS     vmm_unmap_page(vmm_space_t *space, virt_addr_t virt, phys_addr_t *out_phys);
phys_addr_t  vmm_virt_to_phys(vmm_space_t *space, virt_addr_t virt);
NTSTATUS     vmm_alloc_user_region(vmm_space_t *space, virt_addr_t virt, size_t size, uint32_t flags);

/* Kernel Heap */
void  heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *krealloc(void *ptr, size_t new_size);
void  kfree(void *ptr);
void  heap_get_stats(heap_stats_t *out);
