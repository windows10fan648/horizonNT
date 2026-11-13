#include <horizon/shell.h>
#include <horizon/hal.h>
#include <horizon/mm.h>
#include <horizon/ob.h>
#include <horizon/ps.h>
#include <horizon/io.h>
#include <horizon/fs.h>
#include <horizon/elf.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

static volatile uint64_t g_kthread_hits[2] = { 0, 0 };

static void test_worker_thread(void *arg) {
    uint64_t idx = (uint64_t)arg;
    for (int i = 0; i < 5; i++) {
        __atomic_add_fetch(&g_kthread_hits[idx], 1, __ATOMIC_RELAXED);
        ps_sleep_ms(8);
    }
}

bool horizon_run_self_tests(void) {
    kprintf("\n[TEST] Running horizonNT kernel verification suite...\n");
    int passed = 0, failed = 0;

    #define CHECK(name, cond) do { \
        if (cond) { kprintf("  [PASS] %s\n", name); passed++; } \
        else      { kprintf("  [FAIL] %s\n", name); failed++; } \
    } while (0)

    /* 1. PMM */
    pmm_stats_t s1, s2;
    pmm_get_stats(&s1);
    phys_addr_t p1 = pmm_alloc_page();
    phys_addr_t p4 = pmm_alloc_pages(4);
    pmm_get_stats(&s2);
    bool pmm_ok = (p1 != 0) && (p4 != 0) && (s2.free_pages == s1.free_pages - 5);
    pmm_free_page(p1);
    pmm_free_pages(p4, 4);
    pmm_get_stats(&s2);
    pmm_ok = pmm_ok && (s2.free_pages == s1.free_pages);
    CHECK("PMM page & multi-page alloc/free", pmm_ok);

    /* 2. Heap */
    void *h1 = kmalloc(128);
    void *h2 = kmalloc(4096);
    memset(h1, 0xA5, 128);
    h1 = krealloc(h1, 512);
    bool heap_ok = (h1 != NULL) && (h2 != NULL) && (((uint8_t *)h1)[0] == 0xA5);
    kfree(h1);
    kfree(h2);
    CHECK("Kernel heap kmalloc/krealloc/kfree", heap_ok);

    /* 3. Object Manager + Handle Table + Access Mask */
    void *test_obj = NULL;
    NTSTATUS st = ob_create_object(OBJ_TYPE_DIRECTORY, 0, "\\KernelObjects\\TestNode", &test_obj);
    HANDLE h_ro = INVALID_HANDLE_VALUE;
    kprocess_t *sys = ps_system_process();
    bool ob_ok = NT_SUCCESS(st);
    if (ob_ok) {
        ob_ok = NT_SUCCESS(ob_open_handle(&sys->handles, test_obj, GENERIC_READ, &h_ro));
        void *ref = NULL;
        /* Writing through a read-only handle must fail with STATUS_ACCESS_DENIED */
        NTSTATUS w_st = ob_ref_by_handle(&sys->handles, h_ro, OBJ_TYPE_DIRECTORY, GENERIC_WRITE, &ref);
        NTSTATUS r_st = ob_ref_by_handle(&sys->handles, h_ro, OBJ_TYPE_DIRECTORY, GENERIC_READ, &ref);
        ob_ok = ob_ok && (w_st == STATUS_ACCESS_DENIED) && NT_SUCCESS(r_st);
        if (NT_SUCCESS(r_st)) ob_deref_object(ref);
        ob_close_handle(&sys->handles, h_ro);
        ob_deref_object(test_obj);
    }
    CHECK("Object Manager namespace & handle access control", ob_ok);

    /* 4. I/O Manager + VFS RamFS */
    HANDLE f_w = INVALID_HANDLE_VALUE, f_r = INVALID_HANDLE_VALUE;
    const char *payload = "horizonNT-io-check";
    char rbuf[64] = {0};
    size_t nw = 0, nr = 0;
    bool io_ok = NT_SUCCESS(io_open_file("/tmp/selftest.txt", GENERIC_WRITE, &f_w)) &&
                 NT_SUCCESS(io_write_file(f_w, payload, strlen(payload), &nw)) &&
                 NT_SUCCESS(io_close_file(f_w)) &&
                 NT_SUCCESS(io_open_file("/tmp/selftest.txt", GENERIC_READ, &f_r)) &&
                 NT_SUCCESS(io_read_file(f_r, rbuf, sizeof(rbuf) - 1, &nr)) &&
                 NT_SUCCESS(io_close_file(f_r)) &&
                 (nw == strlen(payload)) && (nr == strlen(payload)) &&
                 (strcmp(rbuf, payload) == 0);
    CHECK("IO Manager IRP & RamFS file read/write", io_ok);

    /* 5. Preemptive Kernel Multithreading */
    g_kthread_hits[0] = 0;
    g_kthread_hits[1] = 0;
    ps_create_kthread(sys, "worker-0", test_worker_thread, (void *)0, 2, NULL);
    ps_create_kthread(sys, "worker-1", test_worker_thread, (void *)1, 2, NULL);
    for (int wait = 0; wait < 40; wait++) {
        if (g_kthread_hits[0] == 5 && g_kthread_hits[1] == 5) break;
        ps_sleep_ms(10);
    }
    CHECK("Preemptive kernel multithreading", g_kthread_hits[0] == 5 && g_kthread_hits[1] == 5);

    /* 6. Ring-3 ELF64 User Process + SYSCALL/SYSRET + int 0x80 */
    kprocess_t *uproc = NULL;
    st = elf_spawn_from_fs("/bin/init", &uproc);
    bool user_ok = NT_SUCCESS(st) && uproc != NULL;
    if (user_ok) {
        for (int wait = 0; wait < 100 && !uproc->exited; wait++) {
            ps_sleep_ms(10);
        }
        user_ok = uproc->exited && (uproc->exit_code == 42);
    }
    CHECK("Ring-3 ELF64 user process & syscalls (exit=42)", user_ok);

    kprintf("[TEST] Summary: %d passed, %d failed\n\n", passed, failed);
    return failed == 0;
    #undef CHECK
}

static void cmd_uname(void) {
    char brand[49];
    hal_cpu_brand(brand);
    kprintf("horizonNT 0.1.0-x86_64 (Hybrid Object-Based Executive Kernel)\n");
    kprintf("CPU: %s\n", brand);
}

static void cmd_mem(void) {
    pmm_stats_t pmm;
    heap_stats_t heap;
    pmm_get_stats(&pmm);
    heap_get_stats(&heap);
    kprintf("Physical Memory: %llu MiB total, %llu MiB free (%llu / %llu pages used)\n",
            pmm.total_bytes / (1024 * 1024),
            pmm.free_bytes / (1024 * 1024),
            pmm.used_pages, pmm.total_pages);
    kprintf("Kernel Heap:     %zu KiB total, %zu KiB used, %zu KiB free (%zu active allocs)\n",
            heap.heap_total / 1024, heap.heap_used / 1024,
            heap.heap_free / 1024, heap.alloc_count);
}

static void cmd_ls(const char *path) {
    if (!path || path[0] == '\0') path = "/";
    fs_node_t *dir = fs_lookup(path);
    if (!dir) {
        kprintf("ls: '%s' not found\n", path);
        return;
    }
    if (dir->type != FS_NODE_DIR) {
        kprintf("%-20s %8zu bytes\n", dir->name, dir->size);
        return;
    }
    for (fs_node_t *c = dir->first_child; c; c = c->next_sibling) {
        if (c->type == FS_NODE_DIR) {
            kprintf("<DIR>  %s/\n", c->name);
        } else {
            kprintf("<FILE> %-18s %8zu bytes%s\n", c->name, c->size, c->readonly ? " [ro]" : "");
        }
    }
}

static void cmd_cat(const char *path) {
    if (!path || path[0] == '\0') {
        kprintf("usage: cat <path>\n");
        return;
    }
    HANDLE h = INVALID_HANDLE_VALUE;
    NTSTATUS st = io_open_file(path, GENERIC_READ, &h);
    if (!NT_SUCCESS(st)) {
        kprintf("cat: failed to open '%s' (%s)\n", path, nt_status_str(st));
        return;
    }
    char buf[256];
    size_t nr = 0;
    while (NT_SUCCESS(io_read_file(h, buf, sizeof(buf) - 1, &nr)) && nr > 0) {
        buf[nr] = '\0';
        kprintf("%s", buf);
    }
    kprintf("\n");
    io_close_file(h);
}

static void cmd_write(const char *args) {
    while (*args == ' ') args++;
    if (*args == '\0') {
        kprintf("usage: write <path> <text>\n");
        return;
    }
    char path[128];
    size_t i = 0;
    while (*args && *args != ' ' && i + 1 < sizeof(path)) {
        path[i++] = *args++;
    }
    path[i] = '\0';
    while (*args == ' ') args++;

    HANDLE h = INVALID_HANDLE_VALUE;
    NTSTATUS st = io_open_file(path, GENERIC_WRITE, &h);
    if (!NT_SUCCESS(st)) {
        kprintf("write: cannot open '%s' (%s)\n", path, nt_status_str(st));
        return;
    }
    size_t nw = 0;
    st = io_write_file(h, args, strlen(args), &nw);
    io_close_file(h);
    if (NT_SUCCESS(st)) {
        kprintf("Wrote %zu bytes to %s\n", nw, path);
    } else {
        kprintf("write failed: %s\n", nt_status_str(st));
    }
}

static void cmd_spawn(const char *path) {
    if (!path || path[0] == '\0') path = "/bin/init";
    kprocess_t *p = NULL;
    NTSTATUS st = elf_spawn_from_fs(path, &p);
    if (!NT_SUCCESS(st)) {
        kprintf("spawn '%s' failed: %s\n", path, nt_status_str(st));
        return;
    }
    for (int i = 0; i < 100 && !p->exited; i++) {
        ps_sleep_ms(10);
    }
    kprintf("Process '%s' (PID=%llu) exited with code %d\n", p->name, p->pid, p->exit_code);
}

static void read_line(char *buf, size_t max_len) {
    size_t pos = 0;
    for (;;) {
        char c = console_getc_blocking();
        if (c == '\n') {
            console_putc('\n');
            buf[pos] = '\0';
            return;
        } else if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                console_puts("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            if (pos + 1 < max_len) {
                buf[pos++] = c;
                console_putc(c);
            }
        }
    }
}

void horizon_shell_run(void) {
    kprintf("horizonNT Interactive Kernel Shell (hntsh). Type 'help' for commands.\n");
    char line[256];

    for (;;) {
        kprintf("hntsh> ");
        read_line(line, sizeof(line));

        char *cmd = line;
        while (*cmd == ' ') cmd++;
        if (*cmd == '\0') continue;

        char *arg = cmd;
        while (*arg && *arg != ' ') arg++;
        if (*arg == ' ') {
            *arg++ = '\0';
            while (*arg == ' ') arg++;
        }

        if (strcmp(cmd, "help") == 0) {
            kprintf("Commands:\n");
            kprintf("  help                Show this help\n");
            kprintf("  uname               Show kernel & CPU info\n");
            kprintf("  mem                 Show physical memory & heap usage\n");
            kprintf("  obj                 Dump NT Object Manager namespace\n");
            kprintf("  ps                  List processes & threads\n");
            kprintf("  ls [path]           List VFS directory\n");
            kprintf("  cat <path>          Print file contents\n");
            kprintf("  write <path> <txt>  Write text to RamFS file\n");
            kprintf("  spawn [path]        Run Ring-3 ELF64 binary (default /bin/init)\n");
            kprintf("  test                Run kernel self-test suite\n");
            kprintf("  uptime              Show ticks and uptime\n");
            kprintf("  clear               Clear VGA screen\n");
            kprintf("  reboot              Reboot system\n");
            kprintf("  shutdown            Power off (QEMU ACPI)\n");
        } else if (strcmp(cmd, "uname") == 0) {
            cmd_uname();
        } else if (strcmp(cmd, "mem") == 0) {
            cmd_mem();
        } else if (strcmp(cmd, "obj") == 0) {
            ob_dump_namespace();
        } else if (strcmp(cmd, "ps") == 0) {
            sched_dump_tasks();
        } else if (strcmp(cmd, "ls") == 0) {
            cmd_ls(arg);
        } else if (strcmp(cmd, "cat") == 0) {
            cmd_cat(arg);
        } else if (strcmp(cmd, "write") == 0) {
            cmd_write(arg);
        } else if (strcmp(cmd, "spawn") == 0) {
            cmd_spawn(arg);
        } else if (strcmp(cmd, "test") == 0) {
            horizon_run_self_tests();
        } else if (strcmp(cmd, "uptime") == 0) {
            kprintf("Uptime: %llu ms (%llu ticks @ 250 Hz)\n",
                    hal_get_uptime_ms(), hal_get_ticks());
        } else if (strcmp(cmd, "clear") == 0) {
            vga_clear();
        } else if (strcmp(cmd, "reboot") == 0) {
            hal_reboot();
        } else if (strcmp(cmd, "shutdown") == 0) {
            hal_shutdown();
        } else {
            kprintf("Unknown command: '%s'. Type 'help'.\n", cmd);
        }
    }
}
