#include <horizon/libhorizon.h>

uint64_t hnt_getpid(void) {
    return hnt_syscall(HNT_SYS_GET_PID, 0, 0, 0, 0, 0);
}

uint64_t hnt_gettid(void) {
    return hnt_syscall(HNT_SYS_GET_TID, 0, 0, 0, 0, 0);
}

void hnt_yield(void) {
    hnt_syscall(HNT_SYS_YIELD, 0, 0, 0, 0, 0);
}

void hnt_sleep_ms(uint64_t ms) {
    hnt_syscall(HNT_SYS_SLEEP_MS, ms, 0, 0, 0, 0);
}

int64_t hnt_alloc_vm(void *addr, size_t size, uint32_t flags) {
    return (int64_t)hnt_syscall(HNT_SYS_ALLOC_VM, (uint64_t)addr, (uint64_t)size,
                                (uint64_t)flags, 0, 0);
}

int64_t hnt_query_sysinfo(hnt_sysinfo_t *out) {
    return (int64_t)hnt_syscall(HNT_SYS_QUERY_SYSINFO, (uint64_t)out, 0, 0, 0, 0);
}

HNT_NORETURN void hnt_exit(int code) {
    hnt_syscall(HNT_SYS_EXIT, (uint64_t)(int64_t)code, 0, 0, 0, 0);
    for (;;) {
    }
}
