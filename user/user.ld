ENTRY(_start)

SECTIONS
{
    . = 0x00400000;
    .text   : { *(.text .text.*) }
    . = ALIGN(0x1000);
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(0x1000);
    .data   : { *(.data .data.*) }
    .bss    : { *(COMMON) *(.bss .bss.*) }
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note.gnu.property) }
}
