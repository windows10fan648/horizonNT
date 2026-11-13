#include <horizon/mm.h>
#include <horizon/hal.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

extern uint8_t __kernel_virt_start[];
extern uint8_t __kernel_virt_end[];
extern uint8_t __kernel_phys_start[];
extern uint8_t __kernel_phys_end[];

static vmm_space_t g_kernel_space;

static inline uint64_t *get_table(phys_addr_t phys) {
    return (uint64_t *)PHYS_TO_VIRT(phys);
}

static uint64_t *get_or_alloc_next(uint64_t *table, size_t idx, bool user) {
    if (table[idx] & PTE_PRESENT) {
        if (user && !(table[idx] & PTE_USER)) {
            table[idx] |= PTE_USER;
        }
        return get_table(table[idx] & PTE_ADDR_MASK);
    }
    phys_addr_t next_phys = pmm_alloc_page();
    if (!next_phys) return NULL;
    table[idx] = next_phys | PTE_PRESENT | PTE_WRITABLE | (user ? PTE_USER : 0);
    return get_table(next_phys);
}

NTSTATUS vmm_map_page(vmm_space_t *space, virt_addr_t virt, phys_addr_t phys, uint32_t flags) {
    if (!space || !space->pml4_phys) return STATUS_INVALID_PARAMETER;
    virt = ALIGN_DOWN(virt, PAGE_SIZE);
    phys = ALIGN_DOWN(phys, PAGE_SIZE);

    bool is_user = (flags & VMM_USER) != 0;
    uint64_t *pml4 = get_table(space->pml4_phys);

    size_t i4 = (virt >> 39) & 0x1FF;
    size_t i3 = (virt >> 30) & 0x1FF;
    size_t i2 = (virt >> 21) & 0x1FF;
    size_t i1 = (virt >> 12) & 0x1FF;

    uint64_t *pdpt = get_or_alloc_next(pml4, i4, is_user);
    if (!pdpt) return STATUS_NO_MEMORY;
    uint64_t *pd = get_or_alloc_next(pdpt, i3, is_user);
    if (!pd) return STATUS_NO_MEMORY;
    uint64_t *pt = get_or_alloc_next(pd, i2, is_user);
    if (!pt) return STATUS_NO_MEMORY;

    uint64_t pte = phys | PTE_PRESENT;
    if (flags & VMM_WRITE) pte |= PTE_WRITABLE;
    if (is_user)           pte |= PTE_USER;
    if (!(flags & VMM_EXEC)) pte |= PTE_NX;

    pt[i1] = pte;
    invlpg(virt);
    return STATUS_SUCCESS;
}

NTSTATUS vmm_unmap_page(vmm_space_t *space, virt_addr_t virt, phys_addr_t *out_phys) {
    if (!space || !space->pml4_phys) return STATUS_INVALID_PARAMETER;
    virt = ALIGN_DOWN(virt, PAGE_SIZE);

    uint64_t *pml4 = get_table(space->pml4_phys);
    size_t i4 = (virt >> 39) & 0x1FF;
    if (!(pml4[i4] & PTE_PRESENT)) return STATUS_OBJECT_NAME_NOT_FOUND;

    uint64_t *pdpt = get_table(pml4[i4] & PTE_ADDR_MASK);
    size_t i3 = (virt >> 30) & 0x1FF;
    if (!(pdpt[i3] & PTE_PRESENT)) return STATUS_OBJECT_NAME_NOT_FOUND;

    uint64_t *pd = get_table(pdpt[i3] & PTE_ADDR_MASK);
    size_t i2 = (virt >> 21) & 0x1FF;
    if (!(pd[i2] & PTE_PRESENT) || (pd[i2] & PTE_HUGE)) return STATUS_OBJECT_NAME_NOT_FOUND;

    uint64_t *pt = get_table(pd[i2] & PTE_ADDR_MASK);
    size_t i1 = (virt >> 12) & 0x1FF;
    if (!(pt[i1] & PTE_PRESENT)) return STATUS_OBJECT_NAME_NOT_FOUND;

    if (out_phys) *out_phys = pt[i1] & PTE_ADDR_MASK;
    pt[i1] = 0;
    invlpg(virt);
    return STATUS_SUCCESS;
}

phys_addr_t vmm_virt_to_phys(vmm_space_t *space, virt_addr_t virt) {
    if (!space || !space->pml4_phys) return 0;
    uint64_t *pml4 = get_table(space->pml4_phys);
    size_t i4 = (virt >> 39) & 0x1FF;
    if (!(pml4[i4] & PTE_PRESENT)) return 0;

    uint64_t *pdpt = get_table(pml4[i4] & PTE_ADDR_MASK);
    size_t i3 = (virt >> 30) & 0x1FF;
    if (!(pdpt[i3] & PTE_PRESENT)) return 0;

    uint64_t *pd = get_table(pdpt[i3] & PTE_ADDR_MASK);
    size_t i2 = (virt >> 21) & 0x1FF;
    if (!(pd[i2] & PTE_PRESENT)) return 0;
    if (pd[i2] & PTE_HUGE) {
        return (pd[i2] & PTE_ADDR_MASK) + (virt & 0x1FFFFFULL);
    }

    uint64_t *pt = get_table(pd[i2] & PTE_ADDR_MASK);
    size_t i1 = (virt >> 12) & 0x1FF;
    if (!(pt[i1] & PTE_PRESENT)) return 0;
    return (pt[i1] & PTE_ADDR_MASK) + (virt & 0xFFFULL);
}

void vmm_init(void) {
    phys_addr_t pml4_phys = pmm_alloc_page();
    KASSERT(pml4_phys != 0);
    g_kernel_space.pml4_phys = pml4_phys;

    uint64_t *pml4 = get_table(pml4_phys);

    /* Map first 1 GiB into HHDM (0xFFFF800000000000 -> 0x0) with 2 MiB huge pages */
    phys_addr_t hhdm_pdpt_phys = pmm_alloc_page();
    phys_addr_t hhdm_pd_phys   = pmm_alloc_page();
    KASSERT(hhdm_pdpt_phys && hhdm_pd_phys);

    uint64_t *hhdm_pdpt = get_table(hhdm_pdpt_phys);
    uint64_t *hhdm_pd   = get_table(hhdm_pd_phys);
    for (uint64_t i = 0; i < 512; i++) {
        hhdm_pd[i] = (i * 0x200000ULL) | PTE_PRESENT | PTE_WRITABLE | PTE_HUGE | PTE_NX;
    }
    hhdm_pdpt[0] = hhdm_pd_phys | PTE_PRESENT | PTE_WRITABLE;
    pml4[256]    = hhdm_pdpt_phys | PTE_PRESENT | PTE_WRITABLE;

    /* Pre-allocate shared kernel heap PDPT at PML4[384] (0xFFFFC00000000000)
     * so all user address spaces automatically share heap expansions. */
    phys_addr_t heap_pdpt_phys = pmm_alloc_page();
    KASSERT(heap_pdpt_phys != 0);
    pml4[384] = heap_pdpt_phys | PTE_PRESENT | PTE_WRITABLE;

    /* Map higher-half kernel image at 0xFFFFFFFF80000000+ */
    virt_addr_t k_vstart = ALIGN_DOWN((virt_addr_t)__kernel_virt_start, PAGE_SIZE);
    virt_addr_t k_vend   = ALIGN_UP((virt_addr_t)__kernel_virt_end, PAGE_SIZE);
    for (virt_addr_t v = k_vstart; v < k_vend; v += PAGE_SIZE) {
        phys_addr_t p = KVIRT_TO_PHYS(v);
        NTSTATUS st = vmm_map_page(&g_kernel_space, v, p, VMM_READ | VMM_WRITE | VMM_EXEC);
        KASSERT(NT_SUCCESS(st));
    }

    write_cr3(g_kernel_space.pml4_phys);
}

vmm_space_t *vmm_kernel_space(void) {
    return &g_kernel_space;
}

vmm_space_t *vmm_create_user_space(void) {
    vmm_space_t *space = (vmm_space_t *)kmalloc(sizeof(vmm_space_t));
    if (!space) return NULL;

    space->pml4_phys = pmm_alloc_page();
    if (!space->pml4_phys) {
        kfree(space);
        return NULL;
    }

    uint64_t *dst_pml4 = get_table(space->pml4_phys);
    uint64_t *src_pml4 = get_table(g_kernel_space.pml4_phys);
    for (size_t i = 256; i < 512; i++) {
        dst_pml4[i] = src_pml4[i];
    }
    return space;
}

void vmm_destroy_user_space(vmm_space_t *space) {
    if (!space || space == &g_kernel_space) return;
    if (read_cr3() == space->pml4_phys) {
        write_cr3(g_kernel_space.pml4_phys);
    }

    uint64_t *pml4 = get_table(space->pml4_phys);
    for (size_t i4 = 0; i4 < 256; i4++) {
        if (!(pml4[i4] & PTE_PRESENT)) continue;
        phys_addr_t pdpt_phys = pml4[i4] & PTE_ADDR_MASK;
        uint64_t *pdpt = get_table(pdpt_phys);
        for (size_t i3 = 0; i3 < 512; i3++) {
            if (!(pdpt[i3] & PTE_PRESENT)) continue;
            phys_addr_t pd_phys = pdpt[i3] & PTE_ADDR_MASK;
            uint64_t *pd = get_table(pd_phys);
            for (size_t i2 = 0; i2 < 512; i2++) {
                if (!(pd[i2] & PTE_PRESENT) || (pd[i2] & PTE_HUGE)) continue;
                phys_addr_t pt_phys = pd[i2] & PTE_ADDR_MASK;
                uint64_t *pt = get_table(pt_phys);
                for (size_t i1 = 0; i1 < 512; i1++) {
                    if (pt[i1] & PTE_PRESENT) {
                        pmm_free_page(pt[i1] & PTE_ADDR_MASK);
                    }
                }
                pmm_free_page(pt_phys);
            }
            pmm_free_page(pd_phys);
        }
        pmm_free_page(pdpt_phys);
    }
    pmm_free_page(space->pml4_phys);
    kfree(space);
}

void vmm_switch_space(vmm_space_t *space) {
    phys_addr_t target = space ? space->pml4_phys : g_kernel_space.pml4_phys;
    if (read_cr3() != target) {
        write_cr3(target);
    }
}

NTSTATUS vmm_alloc_user_region(vmm_space_t *space, virt_addr_t virt, size_t size, uint32_t flags) {
    if (!space || size == 0) return STATUS_INVALID_PARAMETER;
    virt_addr_t start = ALIGN_DOWN(virt, PAGE_SIZE);
    virt_addr_t end   = ALIGN_UP(virt + size, PAGE_SIZE);

    for (virt_addr_t v = start; v < end; v += PAGE_SIZE) {
        if (vmm_virt_to_phys(space, v) != 0) {
            /* Update permissions if already mapped */
            phys_addr_t existing = vmm_virt_to_phys(space, v);
            vmm_map_page(space, v, existing, flags | VMM_USER);
            continue;
        }
        phys_addr_t phys = pmm_alloc_page();
        if (!phys) return STATUS_NO_MEMORY;
        NTSTATUS st = vmm_map_page(space, v, phys, flags | VMM_USER);
        if (!NT_SUCCESS(st)) {
            pmm_free_page(phys);
            return st;
        }
    }
    return STATUS_SUCCESS;
}
