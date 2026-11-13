#include <stdbool.h>

#include <horizon/libhorizon.h>

static int g_failed;

static void check(bool cond, const char *name) {
    if (!cond) {
        hnt_printf("[libHorizon] FAIL: %s\n", name);
        g_failed++;
    }
}

int main(void) {
    /* C string/memory runtime. */
    char text[32];
    strcpy(text, "libHorizon");
    check(strlen(text) == 10, "strlen/strcpy");
    check(strcmp(text, "libHorizon") == 0, "strcmp");
    check(strncmp(text, "lib", 3) == 0, "strncmp match");
    check(strncmp(text, "libX", 4) != 0, "strncmp mismatch");

    strncpy(text, "abc", sizeof(text));
    check(text[0] == 'a' && text[3] == '\0', "strncpy");

    char bytes[8];
    memset(bytes, 0x5A, sizeof(bytes));
    check(bytes[0] == 0x5A && bytes[7] == 0x5A, "memset");

    char copy[8];
    memcpy(copy, bytes, sizeof(copy));
    check(memcmp(copy, bytes, sizeof(copy)) == 0, "memcpy/memcmp");

    char moved[8] = "abcdefg";
    memmove(moved + 1, moved, 6);
    check(strcmp(moved, "aabcdef") == 0, "memmove overlap");

    /* Formatted console output through SYS_WRITE. */
    int n = hnt_printf("[libHorizon] printf check: %s %d %u %x %p\n",
                       "ok", -7, 42U, 0xBEEFU, (void *)(uintptr_t)0x1234);
    check(n > 0, "hnt_printf");

    /* System information. */
    hnt_sysinfo_t info;
    bool have_info = (hnt_query_sysinfo(&info) == 0);
    check(have_info, "query_sysinfo");
    if (have_info) {
        check(strncmp(info.kernel_name, "horizonNT", 9) == 0, "kernel_name");
    }

    /* Dynamic user address space. */
    char *vm = (char *)0x20000000ULL;
    if (hnt_alloc_vm(vm, 4096, HNT_VM_READ | HNT_VM_WRITE) == 0) {
        strcpy(vm, "libHorizon VM");
        check(strcmp(vm, "libHorizon VM") == 0, "dynamic VM write");
    } else {
        check(false, "hnt_alloc_vm");
    }

    /* File I/O through the object/handle layer. */
    int64_t fh = hnt_open("/etc/version", HNT_ACCESS_READ);
    check(fh >= 0, "open /etc/version");
    if (fh >= 0) {
        char file_buf[64];
        int64_t nr = hnt_read((uint64_t)fh, file_buf, sizeof(file_buf) - 1);
        if (nr >= 9) {
            check(memcmp(file_buf, "horizonNT", 9) == 0, "read /etc/version");
        } else {
            check(false, "read /etc/version");
        }
        check(hnt_close((uint64_t)fh) == 0, "close");
    }

    /* Process identity through both syscall vectors. */
    check(hnt_getpid() != 0, "getpid");
    check(hnt_gettid() != 0, "gettid");
    check(hnt_int80(HNT_SYS_GET_TID, 0, 0, 0, 0, 0) != 0, "int 0x80 gettid");

    hnt_yield();
    hnt_sleep_ms(1);

    if (g_failed != 0) {
        hnt_printf("[libHorizon] Runtime self-check FAILED (%d)\n", g_failed);
        return 20 + g_failed;
    }

    hnt_printf("[libHorizon] Runtime self-check OK (PID=%llu TID=%llu)\n",
               hnt_getpid(), hnt_gettid());
    return 0;
}
