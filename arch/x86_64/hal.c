#include <horizon/hal.h>
#include <horizon/ps.h>
#include <horizon/string.h>

/* ---------------- 16550 UART Serial (COM1) ---------------- */
#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00); /* Disable interrupts */
    outb(COM1 + 3, 0x80); /* Enable DLAB */
    outb(COM1 + 0, 0x01); /* 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* 8N1 */
    outb(COM1 + 2, 0xC7); /* Enable FIFO */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
}

void serial_putc(char c) {
    if (c == '\n') {
        while ((inb(COM1 + 5) & 0x20) == 0) {}
        outb(COM1, '\r');
    }
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, (uint8_t)c);
}

int serial_getc_nonblock(void) {
    if (inb(COM1 + 5) & 0x01) {
        return (int)(uint8_t)inb(COM1);
    }
    return -1;
}

/* ---------------- VGA Text Mode (80x25) ---------------- */
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   ((volatile uint16_t *)PHYS_TO_VIRT(0xB8000ULL))
#define VGA_COLOR  0x0F /* Bright White on Black */

static size_t  vga_row = 0;
static size_t  vga_col = 0;
static uint8_t vga_attr = VGA_COLOR;

void vga_set_attr(uint8_t attr) {
    vga_attr = attr;
}

uint8_t vga_get_attr(void) {
    return vga_attr;
}

static void vga_update_cursor(void) {
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    volatile uint16_t *vga = VGA_ADDR;
    for (size_t r = 1; r < VGA_HEIGHT; r++) {
        for (size_t c = 0; c < VGA_WIDTH; c++) {
            vga[(r - 1) * VGA_WIDTH + c] = vga[r * VGA_WIDTH + c];
        }
    }
    uint16_t blank = (uint16_t)(' ' | (vga_attr << 8));
    for (size_t c = 0; c < VGA_WIDTH; c++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = blank;
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_clear(void) {
    volatile uint16_t *vga = VGA_ADDR;
    vga_attr = VGA_COLOR;
    uint16_t blank = (uint16_t)(' ' | (vga_attr << 8));
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }
    vga_row = 0;
    vga_col = 0;
    vga_update_cursor();
}

void vga_init(void) {
    vga_clear();
}

void vga_putc(char c) {
    volatile uint16_t *vga = VGA_ADDR;
    if (c == '\r') {
        vga_col = 0;
    } else if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
            vga[vga_row * VGA_WIDTH + vga_col] = (uint16_t)(' ' | (vga_attr << 8));
        }
    } else if (c == '\t') {
        size_t next = (vga_col + 4) & ~3ULL;
        while (vga_col < next && vga_col < VGA_WIDTH) {
            vga[vga_row * VGA_WIDTH + vga_col] = (uint16_t)(' ' | (vga_attr << 8));
            vga_col++;
        }
    } else {
        vga[vga_row * VGA_WIDTH + vga_col] = (uint16_t)((uint8_t)c | (vga_attr << 8));
        vga_col++;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
    }

    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
    }
    vga_update_cursor();
}

/* ---------------- PIT Timer ---------------- */
static volatile uint64_t g_ticks = 0;
static uint32_t          g_pit_hz = 250;

static void pit_irq_handler(interrupt_frame_t *frame) {
    UNUSED(frame);
    g_ticks++;
    sched_tick();
}

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 250;
    g_pit_hz = hz;
    uint32_t divisor = 1193182U / hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    irq_register_handler(0, pit_irq_handler);
}

uint64_t hal_get_ticks(void) {
    return g_ticks;
}

uint64_t hal_get_uptime_ms(void) {
    return (g_ticks * 1000ULL) / g_pit_hz;
}

/* ---------------- PS/2 Keyboard ---------------- */
#define KBD_BUF_SIZE 128
static volatile char   g_kbd_buf[KBD_BUF_SIZE];
static volatile size_t g_kbd_head = 0;
static volatile size_t g_kbd_tail = 0;
static bool            g_shift = false;

static const char scancode_map[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char scancode_shift_map[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0,
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' '
};

static void kbd_irq_handler(interrupt_frame_t *frame) {
    UNUSED(frame);
    uint8_t sc = inb(0x60);
    if (sc == 0x2A || sc == 0x36) { g_shift = true; return; }
    if (sc == 0xAA || sc == 0xB6) { g_shift = false; return; }
    if (sc & 0x80) return; /* key release */

    char c = g_shift ? scancode_shift_map[sc] : scancode_map[sc];
    if (c != 0) {
        size_t next = (g_kbd_head + 1) % KBD_BUF_SIZE;
        if (next != g_kbd_tail) {
            g_kbd_buf[g_kbd_head] = c;
            g_kbd_head = next;
        }
    }
}

void kbd_init(void) {
    while (inb(0x64) & 0x01) {
        (void)inb(0x60);
    }
    irq_register_handler(1, kbd_irq_handler);
}

int kbd_getc_nonblock(void) {
    if (g_kbd_head == g_kbd_tail) return -1;
    char c = g_kbd_buf[g_kbd_tail];
    g_kbd_tail = (g_kbd_tail + 1) % KBD_BUF_SIZE;
    return (int)(uint8_t)c;
}

/* ---------------- CPUID & Power ---------------- */
static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0));
}

void hal_cpu_brand(char *out49) {
    uint32_t max_ext, b, c, d;
    cpuid(0x80000000U, &max_ext, &b, &c, &d);
    if (max_ext >= 0x80000004U) {
        uint32_t *p = (uint32_t *)out49;
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002U + i, &p[i * 4 + 0], &p[i * 4 + 1], &p[i * 4 + 2], &p[i * 4 + 3]);
        }
        out49[48] = '\0';
    } else {
        uint32_t eax;
        cpuid(0, &eax, &b, &c, &d);
        memcpy(out49 + 0, &b, 4);
        memcpy(out49 + 4, &d, 4);
        memcpy(out49 + 8, &c, 4);
        out49[12] = '\0';
    }
}

HNT_NORETURN void hal_reboot(void) {
    hal_cli();
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);
    for (;;) { hal_hlt(); }
}

HNT_NORETURN void hal_shutdown(void) {
    hal_cli();
    /* QEMU / Bochs ACPI shutdown ports */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    for (;;) { hal_hlt(); }
}
