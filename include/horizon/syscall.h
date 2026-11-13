#pragma once

#include <horizon/types.h>
#include <horizon/hal.h>

/* HorizonNT Native System Service Numbers (Nt* / Sys*) */
#define SYS_EXIT           0
#define SYS_WRITE          1
#define SYS_READ           2
#define SYS_OPEN           3
#define SYS_CLOSE          4
#define SYS_YIELD          5
#define SYS_SLEEP_MS       6
#define SYS_GET_PID        7
#define SYS_GET_TID        8
#define SYS_ALLOC_VM       9
#define SYS_QUERY_SYSINFO  10
#define SYS_MAX            11

typedef struct {
    uint64_t uptime_ms;
    uint64_t total_ram_bytes;
    uint64_t free_ram_bytes;
    uint64_t process_count;
    uint64_t thread_count;
    char     kernel_name[32];
} hnt_sysinfo_t;

void syscall_init(void);
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
