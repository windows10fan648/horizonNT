.section .rodata
.align 16
.globl embedded_init_elf_start
.globl embedded_init_elf_end
embedded_init_elf_start:
    .incbin "build/init.elf"
embedded_init_elf_end:

.align 16
.globl embedded_spawnprobe_elf_start
.globl embedded_spawnprobe_elf_end
embedded_spawnprobe_elf_start:
    .incbin "build/spawnprobe.elf"
embedded_spawnprobe_elf_end:
