#include <horizon/hal.h>
#include <horizon/console.h>
#include <horizon/syscall.h>
#include <horizon/ps.h>
#include <horizon/string.h>

typedef struct HNT_PACKED {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} idt_entry_t;

typedef struct HNT_PACKED {
    uint16_t limit;
    uint64_t base;
} idt_ptr_t;

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

extern uint64_t isr_stub_table[256];

static idt_entry_t   g_idt[256];
static idt_ptr_t     g_idt_ptr;
static irq_handler_t g_irq_handlers[16];

static const char *exception_names[32] = {
    "Division By Zero", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

static void idt_set_gate(uint8_t vec, uint64_t handler, uint8_t type_attr) {
    g_idt[vec].offset_low  = handler & 0xFFFF;
    g_idt[vec].selector    = GDT_KERNEL_CODE;
    g_idt[vec].ist         = 0;
    g_idt[vec].type_attr   = type_attr;
    g_idt[vec].offset_mid  = (handler >> 16) & 0xFFFF;
    g_idt[vec].offset_high = (uint32_t)(handler >> 32);
    g_idt[vec].zero        = 0;
}

static void pic_remap(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait(); /* IRQ 0..7  -> int 32..39 */
    outb(PIC2_DATA, 0x28); io_wait(); /* IRQ 8..15 -> int 40..47 */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Unmask IRQ0 (PIT), IRQ1 (KBD), IRQ2 (Cascade) */
    outb(PIC1_DATA, 0xF8);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void idt_init(void) {
    memset(g_idt, 0, sizeof(g_idt));
    memset(g_irq_handlers, 0, sizeof(g_irq_handlers));

    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x8E); /* P=1, DPL=0, Interrupt Gate */
    }
    /* Allow Ring-3 syscall via int 0x80 */
    idt_set_gate(0x80, isr_stub_table[0x80], 0xEE); /* P=1, DPL=3, Interrupt Gate */

    pic_remap();

    g_idt_ptr.limit = sizeof(g_idt) - 1;
    g_idt_ptr.base  = (uint64_t)&g_idt;
    __asm__ volatile ("lidt %0" :: "m"(g_idt_ptr) : "memory");
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        g_irq_handlers[irq] = handler;
    }
}

void isr_dispatch(interrupt_frame_t *frame) {
    if (frame->int_no < 32) {
        bool from_user = (frame->cs & 3) == 3;
        const char *name = exception_names[frame->int_no];
        uint64_t cr2 = (frame->int_no == 14) ? read_cr2() : 0;

        if (from_user) {
            kprocess_t *proc = ps_current_process();
            kprintf(ANSI_BRIGHT_YELLOW
                    "\n[USER EXCEPTION] PID=%llu (%s) #%llu %s"
                    ANSI_RESET " err=0x%llx rip=0x%llx rsp=0x%llx cr2=0x%llx\n",
                    proc ? proc->pid : 0, proc ? proc->name : "?",
                    frame->int_no, name, frame->err_code, frame->rip, frame->rsp, cr2);
            if (proc) {
                ps_process_exit(proc, -((int32_t)frame->int_no + 128));
            }
        }

        kprintf(ANSI_BOLD ANSI_BRIGHT_RED "\n=== KERNEL EXCEPTION #%llu: %s ==="
                ANSI_RESET "\n", frame->int_no, name);
        kprintf("ERR=0x%016llx RIP=0x%016llx CS=0x%04llx RFLAGS=0x%016llx\n",
                frame->err_code, frame->rip, frame->cs, frame->rflags);
        kprintf("RSP=0x%016llx SS=0x%04llx  CR2=0x%016llx CR3=0x%016llx\n",
                frame->rsp, frame->ss, cr2, read_cr3());
        kprintf("RAX=0x%016llx RBX=0x%016llx RCX=0x%016llx RDX=0x%016llx\n",
                frame->rax, frame->rbx, frame->rcx, frame->rdx);
        kprintf("RSI=0x%016llx RDI=0x%016llx RBP=0x%016llx\n",
                frame->rsi, frame->rdi, frame->rbp);
        PANIC("Unhandled kernel exception");
    } else if (frame->int_no >= 32 && frame->int_no < 48) {
        uint8_t irq = (uint8_t)(frame->int_no - 32);
        /* Send EOI before handler so timer preemption can cleanly switch threads */
        pic_send_eoi(irq);
        if (g_irq_handlers[irq]) {
            g_irq_handlers[irq](frame);
        }
    } else if (frame->int_no == 0x80) {
        hal_sti();
        frame->rax = syscall_dispatch(
            frame->rax, frame->rdi, frame->rsi,
            frame->rdx, frame->r10, frame->r8
        );
        hal_cli();
    }
}
