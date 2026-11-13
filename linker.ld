ENTRY(_start)

SECTIONS
{
    . = 0x00100000;
    __kernel_phys_start = .;

    .boot ALIGN(0x1000) :
    {
        KEEP(*(.multiboot))
        *(.boot.text)
        *(.boot.rodata)
        *(.boot.data)
        *(.boot.bss)
    }

    . = ALIGN(0x1000);
    __boot_phys_end = .;

    . = 0xFFFFFFFF80000000 + __boot_phys_end;
    __kernel_virt_start = .;

    .text ALIGN(0x1000) : AT(ADDR(.text) - 0xFFFFFFFF80000000)
    {
        *(.text .text.*)
    }

    .rodata ALIGN(0x1000) : AT(ADDR(.rodata) - 0xFFFFFFFF80000000)
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(0x1000) : AT(ADDR(.data) - 0xFFFFFFFF80000000)
    {
        *(.data .data.*)
    }

    .bss ALIGN(0x1000) : AT(ADDR(.bss) - 0xFFFFFFFF80000000)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(0x1000);
    __kernel_virt_end = .;
    __kernel_phys_end = __kernel_virt_end - 0xFFFFFFFF80000000;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame*)
        *(.note.gnu.property)
    }
}
