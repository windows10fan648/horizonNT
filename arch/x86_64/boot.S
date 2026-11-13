#define MB_MAGIC    0x1BADB002
#define MB_FLAGS    0x00000003
#define MB_CHECKSUM -(MB_MAGIC + MB_FLAGS)

#define HHDM_BASE   0xFFFF800000000000

.section .multiboot, "a"
.align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM

.section .boot.bss, "aw", @nobits
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt_low:
    .skip 4096
boot_pdpt_high:
    .skip 4096
boot_pdpt_hhdm:
    .skip 4096
boot_pd:
    .skip 4096

.align 16
boot_stack_bottom:
    .skip 16384
boot_stack_top:

mb_magic_saved:
    .skip 4
mb_info_saved:
    .skip 4

.section .boot.rodata, "a"
.align 8
boot_gdt:
    .quad 0x0000000000000000   /* 0x00: Null */
    .quad 0x00AF9A000000FFFF   /* 0x08: 64-bit Kernel Code */
    .quad 0x00CF92000000FFFF   /* 0x10: 64-bit Kernel Data */
boot_gdt_end:

boot_gdt_ptr:
    .word boot_gdt_end - boot_gdt - 1
    .long boot_gdt

.section .boot.text, "ax"
.code32
.globl _start
_start:
    cli
    movl %eax, mb_magic_saved
    movl %ebx, mb_info_saved
    movl $boot_stack_top, %esp

    /* Clear 5 boot page tables (5 * 4096 = 20480 bytes = 5120 dwords) */
    movl $boot_pml4, %edi
    xorl %eax, %eax
    movl $5120, %ecx
    rep stosl

    /* Link PML4 entries */
    movl $boot_pdpt_low, %eax
    orl  $0x03, %eax
    movl %eax, boot_pml4 + (0 * 8)

    movl $boot_pdpt_hhdm, %eax
    orl  $0x03, %eax
    movl %eax, boot_pml4 + (256 * 8)

    movl $boot_pdpt_high, %eax
    orl  $0x03, %eax
    movl %eax, boot_pml4 + (511 * 8)

    /* Link PDPT entries to boot_pd (maps first 1 GiB) */
    movl $boot_pd, %eax
    orl  $0x03, %eax
    movl %eax, boot_pdpt_low + (0 * 8)
    movl %eax, boot_pdpt_hhdm + (0 * 8)
    movl %eax, boot_pdpt_high + (510 * 8)

    /* Fill boot_pd with 512 x 2MiB pages (1 GiB total) */
    movl $boot_pd, %edi
    movl $0x00000083, %eax  /* Present | Writable | Huge(2MiB) */
    movl $512, %ecx
1:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    /* Load CR3 */
    movl $boot_pml4, %eax
    movl %eax, %cr3

    /* Enable PAE (CR4.PAE = bit 5) */
    movl %cr4, %eax
    orl  $(1 << 5), %eax
    movl %eax, %cr4

    /* Enable Long Mode + NXE + SYSCALL/SYSRET in IA32_EFER */
    movl $0xC0000080, %ecx
    rdmsr
    orl  $((1 << 0) | (1 << 8) | (1 << 11)), %eax
    wrmsr

    /* Enable Paging (CR0.PG = bit 31) and Write Protect (CR0.WP = bit 16) */
    movl %cr0, %eax
    orl  $((1 << 31) | (1 << 16)), %eax
    movl %eax, %cr0

    /* Load 64-bit GDT and far jump into 64-bit mode */
    lgdt boot_gdt_ptr
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorw %ax, %ax
    movw %ax, %fs
    movw %ax, %gs

    movl mb_magic_saved, %edi
    movl mb_info_saved, %esi
    movabsq $HHDM_BASE, %rax
    testq %rsi, %rsi
    jz 2f
    addq %rax, %rsi
2:
    movabsq $higher_half_entry, %rax
    jmp *%rax

.section .bss
.align 16
kernel_boot_stack_bottom:
    .skip 32768
.globl kernel_boot_stack_top
kernel_boot_stack_top:

.section .text
.code64
.extern kmain
higher_half_entry:
    movabsq $kernel_boot_stack_top, %rsp
    xorq %rbp, %rbp
    call kmain
    cli
3:
    hlt
    jmp 3b
