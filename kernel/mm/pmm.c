#include <horizon/mm.h>
#include <horizon/spinlock.h>
#include <horizon/string.h>
#include <horizon/console.h>

extern uint8_t __kernel_phys_start[];
extern uint8_t __kernel_phys_end[];

#define PMM_MAX_PAGES (262144ULL) /* 1 GiB max tracked in static bitmap */
#define BITMAP_WORDS  (PMM_MAX_PAGES / 64)

static uint64_t    g_bitmap[BITMAP_WORDS];
static uint64_t    g_total_pages = 0;
static uint64_t    g_used_pages  = 0;
static kspinlock_t g_pmm_lock    = KSPINLOCK_INIT;

static inline void bm_set(uint64_t page) {
    if (page < g_total_pages) {
        uint64_t mask = 1ULL << (page & 63);
        if ((g_bitmap[page >> 6] & mask) == 0) {
            g_bitmap[page >> 6] |= mask;
            g_used_pages++;
        }
    }
}

static inline void bm_clear(uint64_t page) {
    if (page < g_total_pages) {
        uint64_t mask = 1ULL << (page & 63);
        if ((g_bitmap[page >> 6] & mask) != 0) {
            g_bitmap[page >> 6] &= ~mask;
            g_used_pages--;
        }
    }
}

static inline bool bm_test(uint64_t page) {
    if (page >= g_total_pages) return true;
    return (g_bitmap[page >> 6] & (1ULL << (page & 63))) != 0;
}

static void mark_region_free(phys_addr_t base, uint64_t len) {
    uint64_t start = ALIGN_UP(base, PAGE_SIZE) >> PAGE_SHIFT;
    uint64_t end   = ALIGN_DOWN(base + len, PAGE_SIZE) >> PAGE_SHIFT;
    if (end > g_total_pages) end = g_total_pages;
    for (uint64_t p = start; p < end; p++) {
        bm_clear(p);
    }
}

static void mark_region_used(phys_addr_t base, uint64_t len) {
    uint64_t start = ALIGN_DOWN(base, PAGE_SIZE) >> PAGE_SHIFT;
    uint64_t end   = ALIGN_UP(base + len, PAGE_SIZE) >> PAGE_SHIFT;
    if (end > g_total_pages) end = g_total_pages;
    for (uint64_t p = start; p < end; p++) {
        bm_set(p);
    }
}

void pmm_init(const multiboot_info_t *mb_info) {
    memset(g_bitmap, 0xFF, sizeof(g_bitmap));

    uint64_t highest_addr = 128ULL * 1024 * 1024; /* fallback 128 MiB */
    if (mb_info && (mb_info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        uint64_t end = mb_info->mmap_addr + mb_info->mmap_length;
        uint64_t cur = mb_info->mmap_addr;
        highest_addr = 0;
        while (cur < end) {
            const multiboot_mmap_entry_t *e = (const multiboot_mmap_entry_t *)PHYS_TO_VIRT(cur);
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t region_end = e->addr + e->len;
                if (region_end > highest_addr) highest_addr = region_end;
            }
            cur += e->size + sizeof(e->size);
        }
    } else if (mb_info && (mb_info->flags & MULTIBOOT_INFO_MEMORY)) {
        highest_addr = ((uint64_t)mb_info->mem_upper + 1024ULL) * 1024ULL;
    }

    g_total_pages = highest_addr >> PAGE_SHIFT;
    if (g_total_pages > PMM_MAX_PAGES) {
        g_total_pages = PMM_MAX_PAGES;
    }
    g_used_pages = g_total_pages;

    if (mb_info && (mb_info->flags & MULTIBOOT_INFO_MEM_MAP)) {
        uint64_t end = mb_info->mmap_addr + mb_info->mmap_length;
        uint64_t cur = mb_info->mmap_addr;
        while (cur < end) {
            const multiboot_mmap_entry_t *e = (const multiboot_mmap_entry_t *)PHYS_TO_VIRT(cur);
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                mark_region_free(e->addr, e->len);
            }
            cur += e->size + sizeof(e->size);
        }
    } else {
        mark_region_free(0x100000ULL, highest_addr - 0x100000ULL);
    }

    /* Reserve low 1 MiB and kernel image */
    mark_region_used(0x0ULL, 0x100000ULL);
    uint64_t kstart = (uint64_t)__kernel_phys_start;
    uint64_t kend   = (uint64_t)__kernel_phys_end;
    mark_region_used(kstart, kend - kstart);

    /* Reserve Multiboot structures if any */
    if (mb_info) {
        phys_addr_t mb_phys = VIRT_TO_PHYS(mb_info);
        mark_region_used(mb_phys, sizeof(*mb_info));
        if (mb_info->flags & MULTIBOOT_INFO_MEM_MAP) {
            mark_region_used(mb_info->mmap_addr, mb_info->mmap_length);
        }
        if ((mb_info->flags & MULTIBOOT_INFO_CMDLINE) && mb_info->cmdline != 0) {
            const char *cmd = (const char *)PHYS_TO_VIRT((phys_addr_t)mb_info->cmdline);
            mark_region_used(mb_info->cmdline, strlen(cmd) + 1);
        }
        if ((mb_info->flags & MULTIBOOT_INFO_MODS) && mb_info->mods_count > 0) {
            mark_region_used(mb_info->mods_addr, mb_info->mods_count * sizeof(multiboot_module_t));
            const multiboot_module_t *mods = (const multiboot_module_t *)PHYS_TO_VIRT(mb_info->mods_addr);
            for (uint32_t i = 0; i < mb_info->mods_count; i++) {
                mark_region_used(mods[i].mod_start, mods[i].mod_end - mods[i].mod_start);
            }
        }
    }
}

phys_addr_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

phys_addr_t pmm_alloc_pages(size_t count) {
    if (count == 0) return 0;
    uint64_t flags = spin_lock_irqsave(&g_pmm_lock);

    uint64_t run = 0;
    uint64_t start_page = 0;
    for (uint64_t p = 256; p < g_total_pages; p++) {
        if (!bm_test(p)) {
            if (run == 0) start_page = p;
            run++;
            if (run == count) {
                for (uint64_t i = 0; i < count; i++) {
                    bm_set(start_page + i);
                }
                spin_unlock_irqrestore(&g_pmm_lock, flags);
                phys_addr_t phys = start_page << PAGE_SHIFT;
                memset(PHYS_TO_VIRT(phys), 0, count * PAGE_SIZE);
                return phys;
            }
        } else {
            run = 0;
        }
    }

    spin_unlock_irqrestore(&g_pmm_lock, flags);
    return 0;
}

void pmm_free_page(phys_addr_t phys) {
    pmm_free_pages(phys, 1);
}

void pmm_free_pages(phys_addr_t phys, size_t count) {
    if (phys == 0 || count == 0) return;
    uint64_t flags = spin_lock_irqsave(&g_pmm_lock);
    uint64_t start = phys >> PAGE_SHIFT;
    for (size_t i = 0; i < count; i++) {
        bm_clear(start + i);
    }
    spin_unlock_irqrestore(&g_pmm_lock, flags);
}

void pmm_get_stats(pmm_stats_t *out) {
    uint64_t flags = spin_lock_irqsave(&g_pmm_lock);
    out->total_pages = g_total_pages;
    out->used_pages  = g_used_pages;
    out->free_pages  = (g_total_pages >= g_used_pages) ? (g_total_pages - g_used_pages) : 0;
    out->total_bytes = out->total_pages * PAGE_SIZE;
    out->free_bytes  = out->free_pages * PAGE_SIZE;
    spin_unlock_irqrestore(&g_pmm_lock, flags);
}
