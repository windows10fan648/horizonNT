#pragma once

#include <horizon/types.h>

typedef struct HNT_PACKED {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

typedef void (*irq_handler_t)(interrupt_frame_t *frame);

/* Port I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

/* MSR & Control Registers */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    __asm__ volatile ("wrmsr" :: "c"(msr), "a"((uint32_t)val), "d"((uint32_t)(val >> 32)));
}

static inline uint64_t read_cr2(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(v));
    return v;
}

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(v) : "memory");
}

static inline void invlpg(uint64_t addr) {
    __asm__ volatile ("invlpg (%0)" :: "r"(addr) : "memory");
}

static inline void hal_sti(void) { __asm__ volatile ("sti"); }
static inline void hal_cli(void) { __asm__ volatile ("cli"); }
static inline void hal_hlt(void) { __asm__ volatile ("hlt"); }

/* GDT / TSS */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS_SEL     0x28

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);
bool gdt_gs_state_valid(void);

/* IDT / PIC / PIT / Keyboard / Serial / VGA */
void idt_init(void);
void irq_register_handler(uint8_t irq, irq_handler_t handler);
void pic_send_eoi(uint8_t irq);

void serial_init(void);
void serial_putc(char c);
int  serial_getc_nonblock(void);

void vga_init(void);
void vga_putc(char c);
void vga_clear(void);

void pit_init(uint32_t hz);
uint64_t hal_get_ticks(void);
uint64_t hal_get_uptime_ms(void);

void kbd_init(void);
int  kbd_getc_nonblock(void);

void hal_cpu_brand(char *out49);
HNT_NORETURN void hal_reboot(void);
HNT_NORETURN void hal_shutdown(void);
