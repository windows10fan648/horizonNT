#include <stdint.h>
#include <stddef.h>

extern uint64_t hnt_syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
extern uint64_t hnt_int80(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

#define SYS_EXIT          0
#define SYS_WRITE         1
#define SYS_READ          2
#define SYS_OPEN          3
#define SYS_CLOSE         4
#define SYS_YIELD         5
#define SYS_SLEEP_MS      6
#define SYS_GET_PID       7
#define SYS_GET_TID       8
#define SYS_ALLOC_VM      9
#define SYS_QUERY_SYSINFO 10

#define GENERIC_READ  (1U << 31)
#define GENERIC_WRITE (1U << 30)
#define VMM_READ      0x01
#define VMM_WRITE     0x02

typedef struct {
    uint64_t uptime_ms;
    uint64_t total_ram_bytes;
    uint64_t free_ram_bytes;
    uint64_t process_count;
    uint64_t thread_count;
    char     kernel_name[32];
} hnt_sysinfo_t;

static size_t u_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void u_puts(const char *s) {
    hnt_syscall(SYS_WRITE, 1, (uint64_t)s, u_strlen(s), 0, 0);
}

static void u_put_u64(uint64_t v) {
    char buf[24];
    int i = 0;
    if (v == 0) {
        buf[i++] = '0';
    } else {
        while (v > 0) {
            buf[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    char rev[24];
    for (int j = 0; j < i; j++) rev[j] = buf[i - 1 - j];
    hnt_syscall(SYS_WRITE, 1, (uint64_t)rev, (uint64_t)i, 0, 0);
}

int main(void) {
    uint64_t pid = hnt_syscall(SYS_GET_PID, 0, 0, 0, 0, 0);
    uint64_t tid = hnt_int80(SYS_GET_TID, 0, 0, 0, 0, 0);

    u_puts("[Ring-3 /bin/init] Hello from User Mode! PID=");
    u_put_u64(pid);
    u_puts(" TID=");
    u_put_u64(tid);
    u_puts("\n");

    hnt_sysinfo_t info;
    if ((int64_t)hnt_syscall(SYS_QUERY_SYSINFO, (uint64_t)&info, 0, 0, 0, 0) >= 0) {
        u_puts("[Ring-3 /bin/init] Kernel: ");
        u_puts(info.kernel_name);
        u_puts(" | Procs=");
        u_put_u64(info.process_count);
        u_puts(" | FreeRAM_MB=");
        u_put_u64(info.free_ram_bytes / (1024 * 1024));
        u_puts("\n");
    }

    /* Test dynamic virtual memory allocation from Ring-3 */
    volatile char *dyn = (volatile char *)0x10000000ULL;
    if ((int64_t)hnt_syscall(SYS_ALLOC_VM, (uint64_t)dyn, 4096, VMM_READ | VMM_WRITE, 0, 0) >= 0) {
        const char *msg = "[Ring-3 /bin/init] Dynamic VM @ 0x10000000 verified!\n";
        for (size_t i = 0; msg[i]; i++) dyn[i] = msg[i];
        dyn[u_strlen(msg)] = '\0';
        u_puts((const char *)dyn);
    }

    /* Test opening and reading VFS file via Handle Table */
    int64_t fh = (int64_t)hnt_syscall(SYS_OPEN, (uint64_t)"/etc/version", GENERIC_READ, 0, 0, 0);
    if (fh >= 0) {
        char buf[64];
        int64_t nr = (int64_t)hnt_syscall(SYS_READ, (uint64_t)fh, (uint64_t)buf, sizeof(buf) - 1, 0, 0);
        if (nr > 0) {
            buf[nr] = '\0';
            u_puts("[Ring-3 /bin/init] Read /etc/version: ");
            u_puts(buf);
        }
        hnt_syscall(SYS_CLOSE, (uint64_t)fh, 0, 0, 0, 0);
    }

    hnt_syscall(SYS_YIELD, 0, 0, 0, 0, 0);
    u_puts("[Ring-3 /bin/init] Exiting cleanly with code 42.\n");
    return 42;
}
