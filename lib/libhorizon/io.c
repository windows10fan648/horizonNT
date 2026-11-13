#include <horizon/libhorizon.h>

int64_t hnt_write(uint64_t handle, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t done = 0;

    while (done < len) {
        int64_t n = (int64_t)hnt_syscall(HNT_SYS_WRITE, handle,
                                         (uint64_t)(p + done), len - done, 0, 0);
        if (n < 0) return n;
        if (n == 0) break;
        done += (size_t)n;
    }
    return (int64_t)done;
}

int64_t hnt_read(uint64_t handle, void *buf, size_t len) {
    return (int64_t)hnt_syscall(HNT_SYS_READ, handle, (uint64_t)buf, len, 0, 0);
}

int64_t hnt_open(const char *path, uint32_t access) {
    return (int64_t)hnt_syscall(HNT_SYS_OPEN, (uint64_t)path, access, 0, 0, 0);
}

int64_t hnt_close(uint64_t handle) {
    return (int64_t)hnt_syscall(HNT_SYS_CLOSE, handle, 0, 0, 0, 0);
}

int64_t hnt_puts(const char *s) {
    return hnt_write(HNT_STDOUT, s, strlen(s));
}
