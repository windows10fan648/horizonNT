# horizonNT

**horizonNT** is a clean-room `x86_64` hybrid object-based operating system kernel inspired by the strongest architectural ideas of both Windows NT and Unix/Linux.

## Architecture Overview

- **HAL (`arch/x86_64/`)**: Multiboot1 32-to-64-bit higher-half bootstrap (`0xFFFFFFFF80000000` kernel + `0xFFFF800000000000` HHDM), GDT + 64-bit TSS, 256-vector IDT, 8259 PIC, 250 Hz PIT timer, PS/2 keyboard, 16550 UART serial (`COM1`), VGA text console, and `SYSCALL`/`SYSRET` + `int 0x80` entry paths.
- **Memory Manager (`kernel/mm/`)**:
  - **PMM**: Multiboot memory-map bitmap physical page allocator.
  - **VMM**: 4-level paging (`PML4 -> PDPT -> PD -> PT`), shared kernel higher-half + heap PDPT, and isolated per-process lower-half user address spaces.
  - **Heap**: Coalescing kernel heap allocator (`kmalloc`, `kzalloc`, `krealloc`, `kfree`).
- **Object Manager (`kernel/ob/`)**: NT-style hierarchical namespace (`\Device`, `\Driver`, `\KernelObjects`, `\SystemRoot`), reference-counted typed kernel objects, and per-process handle tables with access-mask validation (`GENERIC_READ`, `GENERIC_WRITE`).
- **Process & Thread Executive (`kernel/ps/`)**: Separate `KPROCESS` and `KTHREAD` abstractions, preemptive priority round-robin scheduler, kernel threads, and Ring-3 user-mode threads (`CS=0x23`, `SS=0x1B`).
- **I/O Manager & VFS (`kernel/io/`, `kernel/fs/`)**: Driver/Device object registration and IRP dispatch (`IRP_MJ_CREATE`, `IRP_MJ_READ`, `IRP_MJ_WRITE`, `IRP_MJ_CLOSE`) unified with an in-memory VFS (`RamFS`).
- **ELF64 Loader & Syscalls (`kernel/exec/`, `user/`)**: Loads Ring-3 ELF64 binaries (embedded `/bin/init`) into isolated address spaces with `SYS_*` system services.
- **Interactive Shell (`hntsh`)**: Built-in kernel shell accessible over serial or VGA/PS2 keyboard (`help`, `uname`, `mem`, `obj`, `ps`, `ls`, `cat`, `write`, `spawn`, `test`, `uptime`, `reboot`, `shutdown`).

## Building and Running

```sh
make          # Build build/horizonnt.elf (and Ring-3 user/init.elf)
make test     # Boot in QEMU with autotest and verify all subsystems
make run      # Boot interactively with VGA window + serial stdio
make run-nographic  # Boot interactively in terminal via serial console
```
