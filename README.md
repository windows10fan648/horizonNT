# horizonNT

**horizonNT** is a clean-room `x86_64` hybrid object-based operating system kernel inspired by the strongest architectural ideas of both Windows NT and Unix/Linux.

## Architecture Overview

- **HAL (`arch/x86_64/`)**: Multiboot1 32-to-64-bit higher-half bootstrap (`0xFFFFFFFF80000000` kernel + `0xFFFF800000000000` HHDM), GDT + 64-bit TSS, 256-vector IDT, 8259 PIC, 250 Hz PIT timer, PS/2 keyboard, 16550 UART serial (`COM1`), VGA text console with ANSI SGR color translation, and `SYSCALL`/`SYSRET` + `int 0x80` entry paths.
- **Memory Manager (`kernel/mm/`)**:
  - **PMM**: Multiboot memory-map bitmap physical page allocator.
  - **VMM**: 4-level paging (`PML4 -> PDPT -> PD -> PT`), shared kernel higher-half + heap PDPT, and isolated per-process lower-half user address spaces.
  - **Heap**: Coalescing kernel heap allocator (`kmalloc`, `kzalloc`, `krealloc`, `kfree`).
- **Object Manager (`kernel/ob/`)**: NT-style hierarchical namespace (`\Device`, `\Driver`, `\KernelObjects`, `\SystemRoot`), reference-counted typed kernel objects, and per-process handle tables with access-mask validation (`GENERIC_READ`, `GENERIC_WRITE`).
- **Process & Thread Executive (`kernel/ps/`)**: Separate `KPROCESS` and `KTHREAD` abstractions, preemptive priority round-robin scheduler, kernel threads, and Ring-3 user-mode threads (`CS=0x23`, `SS=0x1B`).
- **I/O Manager & VFS (`kernel/io/`, `kernel/fs/`)**: Driver/Device object registration and IRP dispatch (`IRP_MJ_CREATE`, `IRP_MJ_READ`, `IRP_MJ_WRITE`, `IRP_MJ_CLOSE`) unified with an in-memory VFS (`RamFS`).
- **ELF64 Loader & Syscalls (`kernel/exec/`, `user/`)**: Loads Ring-3 ELF64 binaries (embedded `/bin/init`, quiet `/bin/spawnprobe` regression probe, and `/bin/libtest` runtime self-check) into isolated address spaces with `SYS_*` system services.
- **libHorizon Runtime (`lib/libhorizon/`)**: Freestanding Ring-3 runtime with the shared syscall ABI (`<horizon/abi.h>`), syscall wrappers, process/memory services, file and console I/O, `hnt_printf`, and C string/memory primitives. Every user program links `build/libhorizon.a`.
- **Interactive Shell (`hntsh`)**: Built-in kernel shell accessible over serial or VGA/PS2 keyboard (`help`, `uname`, `mem`, `obj`, `ps`, `ls`, `cat`, `write`, `spawn`, `test`, `uptime`, `reboot`, `shutdown`).

## Building and Running

```sh
make          # Build build/horizonnt.elf (and Ring-3 init, spawnprobe, libtest ELFs)
make test     # Boot in QEMU with autotest and run the 9 guest self-tests
make run      # Boot interactively with VGA window + serial stdio
make run-nographic  # Boot interactively in terminal via serial console
```

## libHorizon Runtime

Ring-3 programs are freestanding and link the static `build/libhorizon.a`
runtime. libHorizon is the user-side half of the syscall ABI in
`lib/libhorizon/include/horizon/abi.h`; the kernel includes the same header, so
syscall numbers, access masks, VM flags, and `hnt_sysinfo_t` have one
definition.

```c
#include <horizon/libhorizon.h>

int main(void) {
    hnt_printf("Hello from PID %llu\n", hnt_getpid());

    hnt_sysinfo_t info;
    if (hnt_query_sysinfo(&info) == 0) {
        hnt_printf("Kernel: %s, %llu MiB free\n",
                   info.kernel_name,
                   info.free_ram_bytes / (1024 * 1024));
    }
    return 0;
}
```

- **Process/memory**: `hnt_exit`, `hnt_getpid`, `hnt_gettid`, `hnt_yield`,
  `hnt_sleep_ms`, `hnt_alloc_vm`, `hnt_query_sysinfo`.
- **I/O**: `hnt_open`, `hnt_read`, `hnt_write`, `hnt_close`, `hnt_puts`,
  `hnt_printf`.
- **Runtime**: `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strcmp`,
  `strncmp`, `strcpy`, `strncpy`.
- **Raw entry**: `hnt_syscall` (`SYSCALL`) and `hnt_int80` (legacy vector).

Programs enter through `user/crt0.S`, which calls `main()` and passes its
return value to `hnt_exit()`. To add an app, compile it with `UCFLAGS`, link it
against `build/libhorizon.a`, embed its ELF in
`kernel/exec/embedded_user.S`, and populate it into RamFS in `kernel/kmain.c`.
