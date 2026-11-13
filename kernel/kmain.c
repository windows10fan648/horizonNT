#include <horizon/types.h>
#include <horizon/multiboot.h>
#include <horizon/hal.h>
#include <horizon/mm.h>
#include <horizon/ob.h>
#include <horizon/ps.h>
#include <horizon/io.h>
#include <horizon/fs.h>
#include <horizon/syscall.h>
#include <horizon/shell.h>
#include <horizon/string.h>
#include <horizon/console.h>
#include <horizon/status.h>

extern const uint8_t embedded_init_elf_start[];
extern const uint8_t embedded_init_elf_end[];

static bool cmdline_has(const multiboot_info_t *mb, const char *needle) {
    if (!mb || !(mb->flags & MULTIBOOT_INFO_CMDLINE) || mb->cmdline == 0) return false;
    const char *cmd = (const char *)PHYS_TO_VIRT((phys_addr_t)mb->cmdline);
    size_t nlen = strlen(needle);
    for (size_t i = 0; cmd[i]; i++) {
        if (strncmp(cmd + i, needle, nlen) == 0) return true;
    }
    return false;
}

void kmain(uint32_t mb_magic, const multiboot_info_t *mb_info) {
    console_init();

    kprintf("============================================================\n");
    kprintf("  horizonNT Kernel v0.1.0 (x86_64 Hybrid Object Executive)  \n");
    kprintf("============================================================\n");

    if (mb_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        PANIC("Invalid Multiboot magic: 0x%08x", mb_magic);
    }

    char cpu_brand[49];
    hal_cpu_brand(cpu_brand);
    kprintf("[HAL] CPU: %s\n", cpu_brand);

    gdt_init();
    idt_init();
    pit_init(250);
    kbd_init();
    kprintf("[HAL] GDT, TSS, IDT, PIC, PIT (250Hz), PS/2 KBD initialized.\n");

    pmm_init(mb_info);
    vmm_init();
    heap_init();
    pmm_stats_t pmm;
    pmm_get_stats(&pmm);
    kprintf("[MM]  PMM & 4-level VMM online: %llu MiB RAM (%llu MiB free).\n",
            pmm.total_bytes / (1024 * 1024), pmm.free_bytes / (1024 * 1024));

    ob_init();
    io_init();
    devices_init();
    fs_init();

    size_t init_sz = (size_t)(embedded_init_elf_end - embedded_init_elf_start);
    KASSERT(NT_SUCCESS(fs_populate_file("/bin/init", embedded_init_elf_start, init_sz, true)));
    kprintf("[OB/IO/FS] Object Manager, IRP Drivers, RamFS (/bin/init = %zu B) online.\n", init_sz);

    ps_init();
    syscall_init();
    kprintf("[PS/SYS] Process Executive, Scheduler, SYSCALL/SYSRET & int 0x80 ready.\n");

    hal_sti();

    bool ok = horizon_run_self_tests();
    if (cmdline_has(mb_info, "autotest")) {
        kprintf("[AUTOTEST] Result: %s\n", ok ? "ALL_TESTS_PASSED" : "TESTS_FAILED");
        hal_shutdown();
    }

    horizon_shell_run();
}
