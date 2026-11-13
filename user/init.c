#include <horizon/libhorizon.h>

int main(void) {
    uint64_t pid = hnt_getpid();
    uint64_t tid = hnt_gettid();

    hnt_printf("[Ring-3 /bin/init] Hello from User Mode! PID=%llu TID=%llu\n",
               pid, tid);

    hnt_sysinfo_t info;
    if (hnt_query_sysinfo(&info) == 0) {
        hnt_printf("[Ring-3 /bin/init] Kernel: %s | Procs=%llu | FreeRAM_MB=%llu\n",
                   info.kernel_name, info.process_count,
                   info.free_ram_bytes / (1024 * 1024));
    }

    /* Test dynamic virtual memory allocation from Ring-3. */
    char *dyn = (char *)0x10000000ULL;
    if (hnt_alloc_vm(dyn, 4096, HNT_VM_READ | HNT_VM_WRITE) == 0) {
        strcpy(dyn, "[Ring-3 /bin/init] Dynamic VM @ 0x10000000 verified!\n");
        hnt_puts(dyn);
    }

    /* Test opening and reading a VFS file through the handle table. */
    int64_t fh = hnt_open("/etc/version", HNT_ACCESS_READ);
    if (fh >= 0) {
        char buf[64];
        int64_t nr = hnt_read((uint64_t)fh, buf, sizeof(buf) - 1);
        if (nr > 0) {
            buf[nr] = '\0';
            hnt_printf("[Ring-3 /bin/init] Read /etc/version: %s", buf);
        }
        hnt_close((uint64_t)fh);
    }

    hnt_yield();
    hnt_puts("[Ring-3 /bin/init] Exiting cleanly with code 42.\n");
    return 42;
}
