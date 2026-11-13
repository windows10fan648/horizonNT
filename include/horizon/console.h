#pragma once

#include <horizon/types.h>

/* ANSI SGR sequences. console_puts() forwards them to serial and translates
 * them into VGA text attributes. */
#define ANSI_RESET        "\033[0m"
#define ANSI_BOLD         "\033[1m"
#define ANSI_RED          "\033[31m"
#define ANSI_GREEN        "\033[32m"
#define ANSI_YELLOW       "\033[33m"
#define ANSI_BLUE         "\033[34m"
#define ANSI_MAGENTA      "\033[35m"
#define ANSI_CYAN         "\033[36m"
#define ANSI_WHITE        "\033[37m"
#define ANSI_GRAY         "\033[90m"
#define ANSI_BRIGHT_RED   "\033[91m"
#define ANSI_BRIGHT_GREEN "\033[92m"
#define ANSI_BRIGHT_YELLOW "\033[93m"
#define ANSI_BRIGHT_BLUE  "\033[94m"
#define ANSI_BRIGHT_MAGENTA "\033[95m"
#define ANSI_BRIGHT_CYAN  "\033[96m"
#define ANSI_BRIGHT_WHITE "\033[97m"

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
