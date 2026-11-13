#include <horizon/console.h>
#include <horizon/hal.h>
#include <horizon/spinlock.h>
#include <horizon/string.h>

static kspinlock_t g_console_lock = KSPINLOCK_INIT;

void console_init(void) {
    serial_init();
    vga_init();
}

void console_putc(char c) {
    serial_putc(c);
    vga_putc(c);
}

void console_puts(const char *s) {
    uint64_t flags = spin_lock_irqsave(&g_console_lock);
    while (*s) {
        console_putc(*s++);
    }
    spin_unlock_irqrestore(&g_console_lock, flags);
}

int kvprintf(const char *fmt, va_list ap) {
    char buf[1024];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    console_puts(buf);
    return n;
}

int kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvprintf(fmt, ap);
    va_end(ap);
    return n;
}

int console_getc_nonblock(void) {
    int c = serial_getc_nonblock();
    if (c >= 0) {
        if (c == '\r') c = '\n';
        return c;
    }
    return kbd_getc_nonblock();
}

char console_getc_blocking(void) {
    for (;;) {
        int c = console_getc_nonblock();
        if (c >= 0) return (char)c;
        hal_hlt();
    }
}

HNT_NORETURN void kpanic(const char *file, int line, const char *fmt, ...) {
    hal_cli();
    kprintf("\n*** HORIZONNT KERNEL PANIC at %s:%d ***\n", file, line);
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\nSystem halted.\n");
    for (;;) {
        hal_hlt();
    }
}
