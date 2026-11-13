#include <horizon/hal.h>
#include <horizon/string.h>

typedef struct HNT_PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} gdt_entry_t;

typedef struct HNT_PACKED {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} gdt_tss_entry_t;

typedef struct HNT_PACKED {
    uint16_t limit;
    uint64_t base;
} gdt_ptr_t;

typedef struct HNT_PACKED {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} tss64_t;

typedef struct {
    uint64_t kernel_rsp;       /* %gs:0 */
    uint64_t user_rsp_scratch; /* %gs:8 */
} percpu_t;

#define MSR_GS_BASE        0xC0000101U
#define MSR_KERNEL_GS_BASE 0xC0000102U

static struct HNT_PACKED {
    gdt_entry_t     null;       /* 0x00 */
    gdt_entry_t     kcode;      /* 0x08 */
    gdt_entry_t     kdata;      /* 0x10 */
    gdt_entry_t     udata;      /* 0x18 */
    gdt_entry_t     ucode;      /* 0x20 */
    gdt_tss_entry_t tss;        /* 0x28 */
} g_gdt;

static gdt_ptr_t g_gdt_ptr;
static tss64_t   g_tss;
static percpu_t  g_percpu;

extern uint8_t kernel_boot_stack_top[];

static void set_gdt_entry(gdt_entry_t *e, uint8_t access, uint8_t gran) {
    e->limit_low = 0xFFFF;
    e->base_low  = 0;
    e->base_mid  = 0;
    e->access    = access;
    e->gran      = gran;
    e->base_high = 0;
}

void gdt_init(void) {
    memset(&g_gdt, 0, sizeof(g_gdt));
    memset(&g_tss, 0, sizeof(g_tss));

    /* 0x08: Kernel Code 64-bit (P=1, DPL=0, S=1, Type=0xA, L=1) */
    set_gdt_entry(&g_gdt.kcode, 0x9A, 0xAF);
    /* 0x10: Kernel Data (P=1, DPL=0, S=1, Type=0x2) */
    set_gdt_entry(&g_gdt.kdata, 0x92, 0xCF);
    /* 0x18: User Data (P=1, DPL=3, S=1, Type=0x2) */
    set_gdt_entry(&g_gdt.udata, 0xF2, 0xCF);
    /* 0x20: User Code 64-bit (P=1, DPL=3, S=1, Type=0xA, L=1) */
    set_gdt_entry(&g_gdt.ucode, 0xFA, 0xAF);

    uint64_t tss_base  = (uint64_t)&g_tss;
    uint32_t tss_limit = sizeof(g_tss) - 1;
    g_gdt.tss.limit_low  = tss_limit & 0xFFFF;
    g_gdt.tss.base_low   = tss_base & 0xFFFF;
    g_gdt.tss.base_mid   = (tss_base >> 16) & 0xFF;
    g_gdt.tss.access     = 0x89; /* Present, 64-bit TSS Available */
    g_gdt.tss.gran       = (tss_limit >> 16) & 0x0F;
    g_gdt.tss.base_high  = (tss_base >> 24) & 0xFF;
    g_gdt.tss.base_upper = (uint32_t)(tss_base >> 32);
    g_gdt.tss.reserved   = 0;

    g_tss.rsp0 = (uint64_t)kernel_boot_stack_top;
    g_tss.iopb_offset = sizeof(g_tss);

    g_percpu.kernel_rsp = (uint64_t)kernel_boot_stack_top;
    g_percpu.user_rsp_scratch = 0;

    g_gdt_ptr.limit = sizeof(g_gdt) - 1;
    g_gdt_ptr.base  = (uint64_t)&g_gdt;

    __asm__ volatile (
        "lgdt %0\n\t"
        "pushq $0x08\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw $0x28, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "m"(g_gdt_ptr)
        : "rax", "memory"
    );

    /* Kernel mode always runs with the per-CPU GS block active; user mode
     * runs with GS base 0. Entry/exit paths swap between the two. */
    wrmsr(MSR_GS_BASE, (uint64_t)&g_percpu);
    wrmsr(MSR_KERNEL_GS_BASE, 0);
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
    g_percpu.kernel_rsp = rsp0;
}

bool gdt_gs_state_valid(void) {
    return rdmsr(MSR_GS_BASE) == (uint64_t)&g_percpu &&
           rdmsr(MSR_KERNEL_GS_BASE) == 0;
}
