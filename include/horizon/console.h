#pragma once

#include <horizon/types.h>

void console_init(void);
void console_putc(char c);
void console_puts(const char *s);
int  kprintf(const char *fmt, ...);
int  kvprintf(const char *fmt, va_list ap);

/* Interactive input from serial or PS/2 keyboard */
int  console_getc_nonblock(void);
char console_getc_blocking(void);

HNT_NORETURN void kpanic(const char *file, int line, const char *fmt, ...);

#define PANIC(fmt, ...) kpanic(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define KASSERT(cond) do { \
    if (!(cond)) { \
        kpanic(__FILE__, __LINE__, "Assertion failed: %s", #cond); \
    } \
} while (0)
