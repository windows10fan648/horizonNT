# AGENTS.md

Instructions for AI agents and contributors working on **horizonNT**.

## TL;DR

```sh
make clean
make all      # build build/horizonnt.elf
make test     # boot in QEMU with autotest; must exit 0
```

`make test` is the source of truth. It must end with:

```text
[TEST] Summary: 9 passed, 0 failed
[AUTOTEST] Result: ALL_TESTS_PASSED
```

Do not edit generated files under `build/`.

## What this project is

horizonNT is a clean-room, freestanding `x86_64` hybrid object-based operating
system kernel. It is **not** a Linux userspace project:

- Boots via Multiboot1 and runs in the higher half.
- Contains its own PMM, 4-level paging VMM, kernel heap, object manager,
  process/thread scheduler, I/O manager, RamFS, ELF64 loader, syscalls, shell,
  embedded Ring-3 programs, and the libHorizon user runtime.
- Has no system libc, no libgcc, no host runtime, no dynamic linker, and no
  external dependencies beyond GCC/binutils and QEMU. libHorizon is the
  freestanding Ring-3 runtime statically linked into user programs.

Read `README.md` for the high-level architecture.

## Toolchain and commands

Required tools: `make`, `gcc`, `ld`, `ar`, `objcopy`, `qemu-system-x86_64`.

| Command | Purpose |
| --- | --- |
| `make all` | Build the kernel and embedded user programs (`/bin/init`, `/bin/spawnprobe`, `/bin/libtest`). |
| `make test` | Boot with `-append autotest`, run guest self-tests, shut down QEMU. |
| `make run` | Interactive boot with VGA window and serial stdio. |
| `make run-nographic` | Interactive boot over the serial console. |
| `make clean` | Remove the entire `build/` directory. |

Tests execute inside the guest. There is no host-side unit-test framework.
`make test` only passes if the kernel reaches `hal_shutdown()` after reporting
all tests passed. If it hangs at the `hntsh>` prompt, the `autotest` command
line was not detected or was corrupted.

## Repository map

| Path | Contents |
| --- | --- |
| `arch/x86_64/` | Boot, GDT/TSS, IDT/PIC/PIT, HAL, ISR stubs, syscall entry. |
| `include/horizon/` | All kernel headers, included as `<horizon/...>`. |
| `kernel/kmain.c` | Boot sequence and subsystem initialization order. |
| `kernel/mm/` | PMM bitmap allocator, VMM paging, coalescing kernel heap. |
| `kernel/ob/` | NT-style object namespace and handle tables. |
| `kernel/ps/` | Processes, kernel/user threads, scheduler. |
| `kernel/io/` | Driver/device registration and IRP dispatch. |
| `kernel/fs/` | In-memory VFS / RamFS. |
| `kernel/exec/` | ELF64 loader, syscall dispatch, embedded user image. |
| `kernel/shell/` | Guest self-test suite and interactive `hntsh`. |
| `kernel/string.c` | Kernel string/memory helpers. |
| `lib/libhorizon/` | libHorizon Ring-3 runtime: shared syscall ABI, wrappers, string/memory, printf, I/O. |
| `user/` | Ring-3 programs (`crt0.S`, `init.c`, `spawnprobe.c`, `libtest.c`) and user linker script. |
| `linker.ld` | Higher-half kernel layout. |
| `build/` | Generated objects and binaries; never commit or hand-edit. |

## Build system notes

- Kernel C sources are listed in `K_C_SRCS`; assembly in `K_S_SRCS`; user
  program objects in `U_OBJS` / `SPAWNPROBE_OBJS` / `LIBTEST_OBJS`; libHorizon
  sources in `LIBH_C_SRCS` / `LIBH_S_SRCS` (`Makefile`). The runtime is built
  as `build/libhorizon.a` and linked into every user program.
- Add any new kernel source to those lists, or it will not be compiled.
- Kernel C flags include `-std=gnu11 -ffreestanding -Wall -Wextra -Werror`,
  `-mcmodel=kernel`, `-mno-red-zone`, and `-mno-mmx -mno-sse -mno-sse2`.
  Warnings are build failures.
- Kernel assembly is compiled with only `-m64 -Iinclude`.
- User code uses `UCFLAGS` and must remain freestanding; it cannot include
  kernel headers or use a system libc. It includes libHorizon headers from
  `lib/libhorizon/include`. `UCFLAGS` also disables MMX/SSE because the kernel
  does not yet save/restore FPU/SIMD state; GCC must not emit those
  instructions in Ring 3.
- `kernel/exec/embedded_user.S` embeds `build/init.elf`,
  `build/spawnprobe.elf`, and `build/libtest.elf` with `.incbin`. Rebuild them
  via `make all`; do not commit the generated binaries.
- Final kernel linking is `-nostdlib`. Avoid introducing dependencies on libc
  or compiler runtime helpers (for example 128-bit division or float
  formatting) unless the build system is updated deliberately.

## Runtime invariants

### Address space

| Region | Address |
| --- | --- |
| Kernel virtual base | `0xFFFFFFFF80000000` |
| HHDM (direct physical map) | `0xFFFF800000000000` |
| Kernel heap | `0xFFFFC00000000000` |
| User executable base | `0x00400000` |
| User stack top | `0x00007FFFFFFFE000` |

- Physical addresses must be converted with `PHYS_TO_VIRT`, `VIRT_TO_PHYS`, or
  `KVIRT_TO_PHYS`; never dereference a raw `phys_addr_t` directly.
- `pmm_alloc_page()` / `pmm_alloc_pages()` return physical addresses.
- The HHDM currently maps only the first 1 GiB. The PMM tracks up to 1 GiB.
  Do not raise QEMU memory above 1 GiB without extending both.
- Page-table modifications must keep the shared kernel higher-half mappings
  intact. User address spaces copy PML4 entries `256..511` from the kernel
  space and share the heap PDPT at PML4 index 384.

### Multiboot data

Multiboot info pointers are physical addresses. Convert them with
`PHYS_TO_VIRT` before reading.

Any Multiboot structure used after `pmm_init()` must first be reserved in the
PMM bitmap. This includes `mb_info` itself, the memory map, the command-line
string, and modules. Missing the command-line reservation breaks `make test`
because the kernel heap can overwrite the string before `cmdline_has()` runs.

### Interrupts and exceptions

- `interrupt_frame_t` in `include/horizon/hal.h` must stay in sync with the
  push order in `arch/x86_64/isr.S`.
- Exceptions that push a CPU error code use `ISR_ERR`; the rest use
  `ISR_NOERR` so every stub has the same frame shape.
- ISR stub generation uses GAS `.altmacro`. To concatenate a label prefix with
  an evaluated numeric expression, emit through a helper macro; a bare
  `.quad isr_stub_%i` fails with `invalid operands ... for '%'`.
- Kernel mode runs with `GS_BASE = &g_percpu`; Ring 3 runs with
  `GS_BASE = 0`. `syscall_entry`, ISR entry/exit for user frames, and
  `uthread_enter_ring3` must keep that pairing intact with `swapgs`. A missing
  swap on a user-entry path can fault in `syscall_entry` at `%gs:8` (CR2=8)
  after a context switch out of a syscall.
- The PIT runs at 250 Hz and drives preemption. Use `ps_sleep_ms()` or
  `ps_yield()` instead of busy-waiting when waiting from kernel code.
- Protect shared state with `kspinlock_t`. Use `spin_lock_irqsave()` /
  `spin_unlock_irqrestore()` when the state may also be touched from interrupt
  context. Do not sleep or block while holding a spinlock.

### Syscall ABI

Kernel entry (`arch/x86_64/syscall_asm.S`):

```text
rax = syscall number
rdi, rsi, rdx, r10, r8 = arguments 1..5
```

Syscall numbers, access masks, VM flags, and the `hnt_sysinfo_t` layout live in
`lib/libhorizon/include/horizon/abi.h`. Both `include/horizon/syscall.h` and
the libHorizon user runtime include that header, so there is one ABI
definition. Dispatch lives in `kernel/exec/syscall.c`; user wrappers and
startup live in `lib/libhorizon/` and `user/crt0.S`.

Validate user pointers with the existing range checks before reading or
writing them on behalf of Ring-3.

## Coding conventions

- C11 (`gnu11`), 4-space indentation, no tabs in C/headers (Make recipes need
  tabs).
- Opening brace for functions on the next line; control blocks use
  `if (...) {` on the same line.
- `snake_case` for functions and variables, `_t` suffix for typedefs, `g_`
  prefix for file-scope globals, uppercase for macros.
- Prefix exported subsystem functions (`pmm_`, `vmm_`, `heap_`, `ob_`, `ps_`,
  `io_`, `fs_`, `elf_`, `hal_`, `syscall_`).
- Headers use `#pragma once` and include `<horizon/types.h>` first where
  appropriate.
- Use `NTSTATUS`, `STATUS_*`, and `NT_SUCCESS()` for fallible kernel
  operations. Include `<horizon/status.h>` explicitly in every file that uses
  them; do not rely on transitive includes.
- Use `bool` for predicates and `NTSTATUS` for error-reporting APIs.
- `KASSERT()` / `PANIC()` are for boot-time invariants and impossible states,
  not for validating user input or recoverable failure paths.
- Prefer kernel helpers (`kprintf`, `memset`, `memcpy`, `strlen`, etc.) over
  libc. Do not include standard C library headers beyond compiler-provided
  `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, and `<stdarg.h>`.
- Console colors use the `ANSI_*` macros from `<horizon/console.h>`.
  `console_puts()` forwards SGR sequences to serial and translates them into
  VGA text attributes; raw escape bytes must never reach the VGA buffer.
- Keep existing formatting and naming; make focused changes instead of broad
  rewrites.
- Do not remove or weaken assertions or self-tests to make a build pass.

## Adding functionality

- **New kernel subsystem/file:** add it to `K_C_SRCS` or `K_S_SRCS` in the
  `Makefile`, add its header under `include/horizon/`, and wire initialization
  into `kernel/kmain.c` in the correct order.
- **New syscall:** add the number to `lib/libhorizon/include/horizon/abi.h`,
  implement it in `kernel/exec/syscall.c`, and add the typed user wrapper to
  `lib/libhorizon/`. Consider an `int 0x80` path test as well as `syscall`.
- **New kernel test:** add a `CHECK(...)` case to
  `horizon_run_self_tests()` in `kernel/shell/shell.c`. Tests must run under
  `make test` and leave global state clean for later tests. Keep the summary
  count in this file and in `README.md`/`AGENTS.md` accurate.
- **New user behavior:** build it on libHorizon (`hnt_*` wrappers, `hnt_printf`,
  string helpers). Add the program to the `Makefile` user object list, embed
  its ELF in `kernel/exec/embedded_user.S`, populate it into RamFS in
  `kernel/kmain.c`, and add/extend a guest self-test for it.
- **New object/device/file behavior:** use the existing object manager,
  handle-table access checks, and IRP dispatch rather than bypassing them.

## Verification checklist

Before finishing a change:

1. Run `make clean && make all`. It must complete with no warnings or errors.
2. Run `make test`. It must exit `0` and print `9 passed, 0 failed` and
   `ALL_TESTS_PASSED`.
3. If the change affects boot, interrupts, paging, scheduling, syscalls, or
   user mode, check the serial log for panics or unexpected output beyond the
   normal boot banner.
4. Do not report success based only on a successful compile; the guest tests
   must actually run.
