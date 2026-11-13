#include <horizon/syscall.h>
#include <horizon/hal.h>
#include <horizon/io.h>
#include <horizon/mm.h>
#include <horizon/ps.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

#define MSR_STAR   0xC0000081U
#define MSR_LSTAR  0xC0000082U
#define MSR_SFMASK 0xC0000084U

extern void syscall_entry(void);

static bool is_valid_user_buf(uint64_t ptr, size_t len) {
    if (ptr == 0) return false;
    if (ptr >= 0x00007FFFFFFFFFFFULL) return false;
    if (len > 0 && (ptr + len < ptr || ptr + len > 0x00007FFFFFFFFFFFULL)) return false;
    return true;
}

static bool is_valid_user_str(const char *s, size_t max_len) {
    if (!is_valid_user_buf((uint64_t)s, 1)) return false;
    for (size_t i = 0; i < max_len; i++) {
        if (!is_valid_user_buf((uint64_t)(s + i), 1)) return false;
        if (s[i] == '\0') return true;
    }
    return false;
}

void syscall_init(void) {
    /* STAR[47:32] = 0x08 (kernel CS), STAR[63:48] = 0x10 (user SS=0x18|3, CS=0x20|3) */
    uint64_t star = ((uint64_t)0x0010 << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    /* Mask IF (0x200), TF (0x100), DF (0x400) on SYSCALL entry */
    wrmsr(MSR_SFMASK, 0x0700ULL);
}

uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    UNUSED(a4);
    UNUSED(a5);

    switch (num) {
        case SYS_EXIT: {
            kprocess_t *p = ps_current_process();
            ps_process_exit(p, (int32_t)a1);
        }
        case SYS_WRITE: {
            HANDLE h = (HANDLE)a1;
            const void *buf = (const void *)a2;
            size_t len = (size_t)a3;
            if (len > 65536) len = 65536;
            if (!is_valid_user_buf(a2, len)) return (uint64_t)(int64_t)STATUS_ACCESS_DENIED;
            size_t written = 0;
            NTSTATUS st = io_write_file(h, buf, len, &written);
            return NT_SUCCESS(st) ? written : (uint64_t)(int64_t)st;
        }
        case SYS_READ: {
            HANDLE h = (HANDLE)a1;
            void *buf = (void *)a2;
            size_t len = (size_t)a3;
            if (len > 65536) len = 65536;
            if (!is_valid_user_buf(a2, len)) return (uint64_t)(int64_t)STATUS_ACCESS_DENIED;
            size_t nread = 0;
            NTSTATUS st = io_read_file(h, buf, len, &nread);
            return NT_SUCCESS(st) ? nread : (uint64_t)(int64_t)st;
        }
        case SYS_OPEN: {
            const char *path = (const char *)a1;
            ACCESS_MASK access = (ACCESS_MASK)a2;
            if (!is_valid_user_str(path, 255)) return (uint64_t)(int64_t)STATUS_INVALID_PARAMETER;
            HANDLE h = INVALID_HANDLE_VALUE;
            NTSTATUS st = io_open_file(path, access, &h);
            return NT_SUCCESS(st) ? h : (uint64_t)(int64_t)st;
        }
        case SYS_CLOSE: {
            NTSTATUS st = io_close_file((HANDLE)a1);
            return (uint64_t)(int64_t)st;
        }
        case SYS_YIELD: {
            ps_yield();
            return STATUS_SUCCESS;
        }
        case SYS_SLEEP_MS: {
            ps_sleep_ms(a1);
            return STATUS_SUCCESS;
        }
        case SYS_GET_PID: {
            kprocess_t *p = ps_current_process();
            return p ? p->pid : 0;
        }
        case SYS_GET_TID: {
            kthread_t *t = ps_current_thread();
            return t ? t->tid : 0;
        }
        case SYS_ALLOC_VM: {
            virt_addr_t addr = (virt_addr_t)a1;
            size_t size      = (size_t)a2;
            uint32_t flags   = (uint32_t)a3;
            if (!is_valid_user_buf(addr, size) || size == 0 || size > (16ULL * 1024 * 1024)) {
                return (uint64_t)(int64_t)STATUS_INVALID_PARAMETER;
            }
            kprocess_t *p = ps_current_process();
            NTSTATUS st = vmm_alloc_user_region(p->addr_space, addr, size, flags | VMM_USER);
            return (uint64_t)(int64_t)st;
        }
        case SYS_QUERY_SYSINFO: {
            hnt_sysinfo_t *out = (hnt_sysinfo_t *)a1;
            if (!is_valid_user_buf(a1, sizeof(hnt_sysinfo_t))) {
                return (uint64_t)(int64_t)STATUS_ACCESS_DENIED;
            }
            pmm_stats_t pmm;
            pmm_get_stats(&pmm);
            out->uptime_ms       = hal_get_uptime_ms();
            out->total_ram_bytes = pmm.total_bytes;
            out->free_ram_bytes  = pmm.free_bytes;
            out->process_count   = ps_process_count();
            out->thread_count    = ps_thread_count();
            strncpy(out->kernel_name, "horizonNT 0.1.0 x86_64", sizeof(out->kernel_name) - 1);
            out->kernel_name[sizeof(out->kernel_name) - 1] = '\0';
            return STATUS_SUCCESS;
        }
        default:
            return (uint64_t)(int64_t)STATUS_NOT_IMPLEMENTED;
    }
}
