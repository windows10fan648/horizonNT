#pragma once

#include <stdint.h>

/* horizonNT native system service numbers.
 *
 * This header is the single source of truth for the Ring-3 ABI shared by the
 * kernel syscall dispatcher and the libHorizon runtime. Kernel and user code
 * must include it rather than redefining syscall numbers locally. */
#define HNT_SYS_EXIT          0
#define HNT_SYS_WRITE         1
#define HNT_SYS_READ          2
#define HNT_SYS_OPEN          3
#define HNT_SYS_CLOSE         4
#define HNT_SYS_YIELD         5
#define HNT_SYS_SLEEP_MS      6
#define HNT_SYS_GET_PID       7
#define HNT_SYS_GET_TID       8
#define HNT_SYS_ALLOC_VM      9
#define HNT_SYS_QUERY_SYSINFO 10
#define HNT_SYS_MAX           11

/* Short kernel-facing aliases. */
#define SYS_EXIT          HNT_SYS_EXIT
#define SYS_WRITE         HNT_SYS_WRITE
#define SYS_READ          HNT_SYS_READ
#define SYS_OPEN          HNT_SYS_OPEN
#define SYS_CLOSE         HNT_SYS_CLOSE
#define SYS_YIELD         HNT_SYS_YIELD
#define SYS_SLEEP_MS      HNT_SYS_SLEEP_MS
#define SYS_GET_PID       HNT_SYS_GET_PID
#define SYS_GET_TID       HNT_SYS_GET_TID
#define SYS_ALLOC_VM      HNT_SYS_ALLOC_VM
#define SYS_QUERY_SYSINFO HNT_SYS_QUERY_SYSINFO
#define SYS_MAX           HNT_SYS_MAX

/* Standard console handles assigned to every process. */
#define HNT_STDIN  0
#define HNT_STDOUT 1
#define HNT_STDERR 2

/* Generic access rights (NT-inspired). */
#define HNT_ACCESS_READ    (1U << 31)
#define HNT_ACCESS_WRITE   (1U << 30)
#define HNT_ACCESS_EXECUTE (1U << 29)
#define HNT_ACCESS_ALL     (1U << 28)

/* Virtual-memory protection flags accepted by SYS_ALLOC_VM. */
#define HNT_VM_READ  0x01
#define HNT_VM_WRITE 0x02
#define HNT_VM_EXEC  0x04
#define HNT_VM_USER  0x08

/* Filled in by SYS_QUERY_SYSINFO. Layout is part of the syscall ABI. */
typedef struct {
    uint64_t uptime_ms;
    uint64_t total_ram_bytes;
    uint64_t free_ram_bytes;
    uint64_t process_count;
    uint64_t thread_count;
    char     kernel_name[32];
} hnt_sysinfo_t;
