#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <horizon/abi.h>

/* libHorizon: the freestanding Ring-3 runtime for horizonNT user programs.
 *
 * It provides syscall wrappers, process/memory helpers, console and file I/O,
 * a small printf implementation, and the C string/memory primitives needed by
 * freestanding apps. The runtime shares the syscall ABI in <horizon/abi.h>
 * with the kernel. */

#ifndef HNT_NORETURN
#define HNT_NORETURN __attribute__((noreturn))
#endif

/* Raw entry points. hnt_int80 exercises the legacy int 0x80 compatibility
 * vector; hnt_syscall uses SYSCALL/SYSRET. */
uint64_t hnt_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                     uint64_t a4, uint64_t a5);
uint64_t hnt_int80(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5);

/* Process and scheduler services. */
HNT_NORETURN void hnt_exit(int code);
uint64_t hnt_getpid(void);
uint64_t hnt_gettid(void);
void hnt_yield(void);
void hnt_sleep_ms(uint64_t ms);

/* Address-space and system information services. */
int64_t hnt_alloc_vm(void *addr, size_t size, uint32_t flags);
int64_t hnt_query_sysinfo(hnt_sysinfo_t *out);

/* Console and file I/O. Negative returns are NTSTATUS error codes. */
int64_t hnt_write(uint64_t handle, const void *buf, size_t len);
int64_t hnt_read(uint64_t handle, void *buf, size_t len);
int64_t hnt_open(const char *path, uint32_t access);
int64_t hnt_close(uint64_t handle);
int64_t hnt_puts(const char *s);

/* Minimal formatted console output: %s %c %d %i %u %x %X %p %% with
 * optional '-' and '0' flags, decimal width, and l/ll/z length modifiers. */
int hnt_printf(const char *fmt, ...);
int hnt_vprintf(const char *fmt, va_list ap);

/* Freestanding C string/memory runtime. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
