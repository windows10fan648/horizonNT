.section .text
.code64

/* uint64_t hnt_syscall(num, a1, a2, a3, a4, a5)
 * System V ABI args: rdi=num, rsi=a1, rdx=a2, rcx=a3, r8=a4, r9=a5
 * SYSCALL ABI:       rax=num, rdi=a1, rsi=a2, rdx=a3, r10=a4, r8=a5
 */
.globl hnt_syscall
hnt_syscall:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8,  %r10
    movq %r9,  %r8
    syscall
    ret

/* Legacy int 0x80 compatibility vector with the same argument layout. */
.globl hnt_int80
hnt_int80:
    movq %rdi, %rax
    movq %rsi, %rdi
    movq %rdx, %rsi
    movq %rcx, %rdx
    movq %r8,  %r10
    movq %r9,  %r8
    int  $0x80
    ret
