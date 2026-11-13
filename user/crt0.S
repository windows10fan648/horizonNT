.section .text
.code64

.extern main
.extern hnt_exit

/* Every Ring-3 program enters here. main() returns its exit code, which is
 * passed to the libHorizon runtime. */
.globl _start
_start:
    xorq %rbp, %rbp
    call main
    movl %eax, %edi
    call hnt_exit
1:
    jmp 1b
