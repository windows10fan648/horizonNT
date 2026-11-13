#include <horizon/console.h>
#include <horizon/hal.h>
#include <horizon/spinlock.h>
#include <horizon/string.h>

static kspinlock_t g_console_lock = KSPINLOCK_INIT;

/* ANSI SGR codes map onto the VGA text attribute (high nibble = background,
 * low nibble = foreground). VGA color order differs from ANSI order. */
static const uint8_t ansi_to_vga[8] = {
    0, /* black   */
    4, /* red     */
    2, /* green   */
    6, /* yellow  */
    1, /* blue    */
    5, /* magenta */
    3, /* cyan    */
    7  /* white   */
};

static uint8_t g_sgr_attr = 0x0F; /* bright white on black */

static void console_apply_sgr(int code) {
    uint8_t fg = g_sgr_attr & 0x0F;
    uint8_t bg = (uint8_t)((g_sgr_attr >> 4) & 0x0F);

    if (code == 0) {
        fg = 0x0F;
        bg = 0x00;
    } else if (code == 1) {
        fg |= 0x08;
    } else if (code == 22) {
        fg &= 0x07;
    } else if (code >= 30 && code <= 37) {
        fg = (uint8_t)((fg & 0x08) | ansi_to_vga[code - 30]);
    } else if (code >= 90 && code <= 97) {
        fg = (uint8_t)(0x08 | ansi_to_vga[code - 90]);
    } else if (code == 39) {
        fg = (uint8_t)(0x07 | (fg & 0x08));
    } else if (code >= 40 && code <= 47) {
        bg = ansi_to_vga[code - 40];
    } else if (code >= 100 && code <= 107) {
        bg = (uint8_t)(0x08 | ansi_to_vga[code - 100]);
    } else if (code == 49) {
        bg = 0x00;
    } else {
        return;
    }

    g_sgr_attr = (uint8_t)((bg << 4) | fg);
    vga_set_attr(g_sgr_attr);
}

/* Parse "ESC [ <params> m". Returns the number of bytes consumed, or 0 when
 * the sequence is not a complete SGR command. */
static size_t console_parse_sgr(const char *s) {
    if (s[0] != '\033' || s[1] != '[') return 0;

    int params[8];
    int nparams = 0;
    int value = 0;
    bool have_value = false;

    size_t i = 2;
    for (;;) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            have_value = true;
            i++;
        } else if (c == ';') {
            if (nparams < 8) params[nparams++] = have_value ? value : 0;
            value = 0;
            have_value = false;
            i++;
        } else if (c == 'm') {
            if (nparams < 8) params[nparams++] = have_value ? value : 0;
            i++;
            break;
        } else {
            return 0;
        }
    }

    if (nparams == 0) params[nparams++] = 0;
    for (int p = 0; p < nparams; p++) {
        console_apply_sgr(params[p]);
    }
    return i;
}

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
        if (*s == '\033') {
            size_t consumed = console_parse_sgr(s);
            if (consumed > 0) {
                /* Serial keeps the raw escape sequence; VGA uses the attribute
                 * updated by console_apply_sgr(). */
                for (size_t i = 0; i < consumed; i++) serial_putc(s[i]);
                s += consumed;
                continue;
            }
        }
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
    kprintf(ANSI_BOLD ANSI_BRIGHT_RED
            "\n*** HORIZONNT KERNEL PANIC at %s:%d ***" ANSI_RESET "\n",
            file, line);
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf(ANSI_BRIGHT_RED "\nSystem halted." ANSI_RESET "\n");
    for (;;) {
        hal_hlt();
    }
}
