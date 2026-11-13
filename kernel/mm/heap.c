#include <horizon/mm.h>
#include <horizon/status.h>
#include <horizon/spinlock.h>
#include <horizon/string.h>
#include <horizon/console.h>

#define KHEAP_START 0xFFFFC00000000000ULL
#define KHEAP_INIT_PAGES 64 /* 256 KiB initial */
#define KHEAP_MAX_BYTES  (64ULL * 1024 * 1024)
#define BLOCK_MAGIC 0x484E5442U /* 'HNTB' */

typedef struct heap_block {
    uint32_t           magic;
    uint32_t           is_free;
    size_t             size; /* payload size */
    struct heap_block *next;
    struct heap_block *prev;
} heap_block_t;

static heap_block_t *g_heap_head  = NULL;
static virt_addr_t   g_heap_end   = KHEAP_START;
static size_t        g_heap_total = 0;
static size_t        g_alloc_cnt  = 0;
static kspinlock_t   g_heap_lock  = KSPINLOCK_INIT;

static bool heap_grow(size_t pages) {
    if (g_heap_total + pages * PAGE_SIZE > KHEAP_MAX_BYTES) return false;
    virt_addr_t region_start = g_heap_end;
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t p = pmm_alloc_page();
        if (!p) return false;
        if (!NT_SUCCESS(vmm_map_page(vmm_kernel_space(), g_heap_end, p, VMM_READ | VMM_WRITE))) {
            pmm_free_page(p);
            return false;
        }
        g_heap_end += PAGE_SIZE;
    }

    size_t added = pages * PAGE_SIZE;
    g_heap_total += added;

    heap_block_t *new_blk = (heap_block_t *)region_start;
    new_blk->magic   = BLOCK_MAGIC;
    new_blk->is_free = 1;
    new_blk->size    = added - sizeof(heap_block_t);
    new_blk->next    = NULL;
    new_blk->prev    = NULL;

    if (!g_heap_head) {
        g_heap_head = new_blk;
    } else {
        heap_block_t *tail = g_heap_head;
        while (tail->next) tail = tail->next;
        if (tail->is_free && (uint8_t *)tail + sizeof(heap_block_t) + tail->size == (uint8_t *)new_blk) {
            tail->size += added;
        } else {
            tail->next    = new_blk;
            new_blk->prev = tail;
        }
    }
    return true;
}

void heap_init(void) {
    KASSERT(heap_grow(KHEAP_INIT_PAGES));
}

static void split_block(heap_block_t *blk, size_t size) {
    size_t min_split = sizeof(heap_block_t) + 16;
    if (blk->size >= size + min_split) {
        heap_block_t *rem = (heap_block_t *)((uint8_t *)(blk + 1) + size);
        rem->magic   = BLOCK_MAGIC;
        rem->is_free = 1;
        rem->size    = blk->size - size - sizeof(heap_block_t);
        rem->next    = blk->next;
        rem->prev    = blk;
        if (blk->next) blk->next->prev = rem;
        blk->next = rem;
        blk->size = size;
    }
}

void *kmalloc(size_t size) {
    if (size == 0) size = 16;
    size = ALIGN_UP(size, 16);

    uint64_t flags = spin_lock_irqsave(&g_heap_lock);

    for (int pass = 0; pass < 2; pass++) {
        for (heap_block_t *cur = g_heap_head; cur; cur = cur->next) {
            if (cur->is_free && cur->size >= size) {
                split_block(cur, size);
                cur->is_free = 0;
                g_alloc_cnt++;
                spin_unlock_irqrestore(&g_heap_lock, flags);
                return (void *)(cur + 1);
            }
        }
        size_t need_pages = ALIGN_UP(size + sizeof(heap_block_t), PAGE_SIZE) >> PAGE_SHIFT;
        if (need_pages < 16) need_pages = 16;
        if (!heap_grow(need_pages)) break;
    }

    spin_unlock_irqrestore(&g_heap_lock, flags);
    return NULL;
}

void *kzalloc(size_t size) {
    void *p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uint64_t flags = spin_lock_irqsave(&g_heap_lock);

    heap_block_t *blk = ((heap_block_t *)ptr) - 1;
    KASSERT(blk->magic == BLOCK_MAGIC);
    if (blk->is_free) {
        spin_unlock_irqrestore(&g_heap_lock, flags);
        return;
    }

    blk->is_free = 1;
    if (g_alloc_cnt > 0) g_alloc_cnt--;

    /* Coalesce with next */
    if (blk->next && blk->next->is_free &&
        (uint8_t *)(blk + 1) + blk->size == (uint8_t *)blk->next) {
        heap_block_t *nxt = blk->next;
        blk->size += sizeof(heap_block_t) + nxt->size;
        blk->next = nxt->next;
        if (nxt->next) nxt->next->prev = blk;
    }

    /* Coalesce with prev */
    if (blk->prev && blk->prev->is_free &&
        (uint8_t *)(blk->prev + 1) + blk->prev->size == (uint8_t *)blk) {
        heap_block_t *prv = blk->prev;
        prv->size += sizeof(heap_block_t) + blk->size;
        prv->next = blk->next;
        if (blk->next) blk->next->prev = prv;
    }

    spin_unlock_irqrestore(&g_heap_lock, flags);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    heap_block_t *blk = ((heap_block_t *)ptr) - 1;
    KASSERT(blk->magic == BLOCK_MAGIC);
    if (blk->size >= new_size) return ptr;

    void *np = kmalloc(new_size);
    if (!np) return NULL;
    memcpy(np, ptr, blk->size);
    kfree(ptr);
    return np;
}

void heap_get_stats(heap_stats_t *out) {
    uint64_t flags = spin_lock_irqsave(&g_heap_lock);
    size_t used = 0, free_b = 0;
    for (heap_block_t *cur = g_heap_head; cur; cur = cur->next) {
        if (cur->is_free) free_b += cur->size;
        else              used += cur->size;
    }
    out->heap_total  = g_heap_total;
    out->heap_used   = used;
    out->heap_free   = free_b;
    out->alloc_count = g_alloc_cnt;
    spin_unlock_irqrestore(&g_heap_lock, flags);
}
