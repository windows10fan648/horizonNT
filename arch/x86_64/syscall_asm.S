.section .data
.align 8
.globl g_syscall_stack_misaligned
g_syscall_stack_misaligned:
    .quad 0

.section .text
.code64

.extern syscall_dispatch

/*
 * Fast SYSCALL entry (System V AMD64 / Linux-like register convention):
 *   RAX = syscall number
 *   RDI = arg1, RSI = arg2, RDX = arg3, R10 = arg4, R8 = arg5
 *   RCX = user RIP (saved by CPU)
 *   R11 = user RFLAGS (saved by CPU)
 *
 * GS points to percpu_t after swapgs:
 *   %gs:0 = kernel_rsp
 *   %gs:8 = user_rsp_scratch
 */
.globl syscall_entry
syscall_entry:
    swapgs
    movq %rsp, %gs:8
    movq %gs:0, %rsp

    /* Save user context on this thread's kernel stack */
    pushq %gs:8       /* user RSP */
    pushq %rcx        /* user RIP */
    pushq %r11        /* user RFLAGS */
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Rearrange arguments for:
     * uint64_t syscall_dispatch(num, a1, a2, a3, a4, a5)
     *   rdi=rax, rsi=old_rdi, rdx=old_rsi, rcx=old_rdx, r8=r10, r9=old_r8
     */
    movq %r8,  %r9    /* a5 */
    movq %r10, %r8    /* a4 */
    movq %rdx, %rcx   /* a3 */
    movq %rsi, %rdx   /* a2 */
    movq %rdi, %rsi   /* a1 */
    movq %rax, %rdi   /* num */

    /* SysV ABI requires rsp % 16 == 0 before the call. */
    subq $8, %rsp
    testq $0xF, %rsp
    jz 1f
    movq $1, g_syscall_stack_misaligned(%rip)
1:
    sti
    cld
    call syscall_dispatch
    cli
    addq $8, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r11         /* user RFLAGS */
    popq %rcx         /* user RIP */
    popq %rsp         /* user RSP */

    swapgs
    sysretq
