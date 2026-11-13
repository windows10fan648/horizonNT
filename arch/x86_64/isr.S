.section .text
.code64

.extern isr_dispatch

.macro ISR_NOERR num
.globl isr_stub_\num
isr_stub_\num:
    pushq $0
    pushq $\num
    jmp isr_common
.endm

.macro ISR_ERR num
.globl isr_stub_\num
isr_stub_\num:
    pushq $\num
    jmp isr_common
.endm

.macro ISR_TABLE_ENTRY num
    .quad isr_stub_\num
.endm

/* CPU Exceptions 0..31 */
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

/* IRQs 32..47 and remaining vectors 48..255 */
.altmacro
.set i, 32
.rept 224
    ISR_NOERR %i
    .set i, i + 1
.endr

isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi
    cld
    call isr_dispatch

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq

.section .rodata
.align 8
.globl isr_stub_table
isr_stub_table:
.set i, 0
.rept 256
    ISR_TABLE_ENTRY %i
    .set i, i + 1
.endr

.section .text
/* void ctx_switch(uint64_t *old_sp, uint64_t new_sp) */
.globl ctx_switch
ctx_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

/* HNT_NORETURN void uthread_enter_ring3(uint64_t user_rip, uint64_t user_rsp) */
.globl uthread_enter_ring3
uthread_enter_ring3:
    cli
    pushq $0x1B       /* User Data SS | RPL 3 */
    pushq %rsi        /* User RSP */
    pushq $0x202      /* RFLAGS: IF=1 */
    pushq $0x23       /* User Code CS | RPL 3 */
    pushq %rdi        /* User RIP */

    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    iretq
