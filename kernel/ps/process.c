#include <horizon/ps.h>
#include <horizon/hal.h>
#include <horizon/io.h>
#include <horizon/status.h>
#include <horizon/string.h>
#include <horizon/console.h>

/* Internal scheduler hooks in sched.c */
void sched_init_boot(kthread_t *boot_thread);
void sched_add_thread(kthread_t *t);
void sched_schedule(void);
void sched_reap(void);

extern void uthread_enter_ring3(uint64_t user_rip, uint64_t user_rsp);

static kprocess_t *g_system_proc  = NULL;
static kprocess_t *g_proc_list    = NULL;
static uint64_t    g_next_pid     = 1;
static uint64_t    g_next_tid     = 1;
static kspinlock_t g_ps_lock      = KSPINLOCK_INIT;

static kthread_t   g_boot_thread;
extern kthread_t  *g_current_thread;

static void kthread_trampoline(void) {
    hal_sti();
    kthread_t *t = ps_current_thread();
    if (t && t->entry) {
        t->entry(t->arg);
    }
    ps_thread_exit(0);
}

static void uthread_trampoline(void) {
    kthread_t *t = ps_current_thread();
    tss_set_rsp0(t->kstack_top);
    vmm_switch_space(t->process->addr_space);
    uthread_enter_ring3(t->user_rip, t->user_rsp);
}

static uint64_t setup_initial_kstack(uint8_t *kstack_base, uint64_t entry_addr) {
    uint64_t sp = (uint64_t)kstack_base + KTHREAD_STACK_SIZE;
    sp &= ~0xFULL;

    /* Reserve one qword so the C entry point observes rsp % 16 == 8, matching
     * the SysV ABI after ctx_switch pops the saved registers and returns. */
    sp -= sizeof(uint64_t);

    /* Return address popped by ctx_switch `ret` */
    sp -= sizeof(uint64_t);
    *(uint64_t *)sp = entry_addr;

    /* Callee-saved regs popped by ctx_switch: rbp, rbx, r12, r13, r14, r15 */
    for (int i = 0; i < 6; i++) {
        sp -= sizeof(uint64_t);
        *(uint64_t *)sp = 0;
    }
    return sp;
}

void ps_init(void) {
    g_system_proc = (kprocess_t *)kzalloc(sizeof(kprocess_t));
    KASSERT(g_system_proc != NULL);
    g_system_proc->pid        = 0;
    strcpy(g_system_proc->name, "System");
    g_system_proc->addr_space = vmm_kernel_space();
    g_system_proc->is_kernel  = true;
    ob_handle_table_init(&g_system_proc->handles);
    g_proc_list = g_system_proc;

    memset(&g_boot_thread, 0, sizeof(g_boot_thread));
    g_boot_thread.tid      = 0;
    strcpy(g_boot_thread.name, "SystemIdle");
    g_boot_thread.process  = g_system_proc;
    g_boot_thread.state    = THREAD_RUNNING;
    g_boot_thread.priority = 0;
    g_boot_thread.quantum  = 4;
    g_system_proc->thread_count = 1;

    extern uint8_t kernel_boot_stack_top[];
    g_boot_thread.kstack_top = (uint64_t)kernel_boot_stack_top;

    sched_init_boot(&g_boot_thread);
}

kprocess_t *ps_system_process(void) {
    return g_system_proc;
}

kprocess_t *ps_current_process(void) {
    kthread_t *t = ps_current_thread();
    return t ? t->process : g_system_proc;
}

NTSTATUS ps_create_process(const char *name, bool is_kernel, kprocess_t **out_proc) {
    if (!out_proc) return STATUS_INVALID_PARAMETER;
    sched_reap();

    kprocess_t *proc = (kprocess_t *)kzalloc(sizeof(kprocess_t));
    if (!proc) return STATUS_NO_MEMORY;

    proc->is_kernel = is_kernel;
    if (is_kernel) {
        proc->addr_space = vmm_kernel_space();
    } else {
        proc->addr_space = vmm_create_user_space();
        if (!proc->addr_space) {
            kfree(proc);
            return STATUS_NO_MEMORY;
        }
    }

    ob_handle_table_init(&proc->handles);
    strncpy(proc->name, name ? name : "unnamed", sizeof(proc->name) - 1);

    /* Open standard handles 0=stdin, 1=stdout, 2=stderr backed by \Device\Console0 */
    void *con_dev = NULL;
    if (NT_SUCCESS(ob_lookup_object("\\Device\\Console0", OBJ_TYPE_DEVICE, &con_dev))) {
        for (int fd = 0; fd < 3; fd++) {
            void *fobj = NULL;
            if (NT_SUCCESS(ob_create_object(OBJ_TYPE_FILE, sizeof(file_object_t), NULL, &fobj))) {
                file_object_t *fo = (file_object_t *)fobj;
                fo->device = (device_object_t *)con_dev;
                ob_ref_object(con_dev);
                fo->access = (fd == 0) ? GENERIC_READ : GENERIC_WRITE;
                HANDLE h = 0;
                ob_open_handle(&proc->handles, fobj, fo->access, &h);
                ob_deref_object(fobj);
            }
        }
        ob_deref_object(con_dev);
    }

    uint64_t flags = spin_lock_irqsave(&g_ps_lock);
    proc->pid  = g_next_pid++;
    proc->next = g_proc_list;
    g_proc_list = proc;
    spin_unlock_irqrestore(&g_ps_lock, flags);

    *out_proc = proc;
    return STATUS_SUCCESS;
}

NTSTATUS ps_create_kthread(kprocess_t *proc, const char *name, kthread_entry_t entry,
                           void *arg, uint8_t priority, kthread_t **out_thread) {
    if (!proc || !entry) return STATUS_INVALID_PARAMETER;

    kthread_t *t = (kthread_t *)kzalloc(sizeof(kthread_t));
    if (!t) return STATUS_NO_MEMORY;

    t->kstack_base = (uint8_t *)kmalloc(KTHREAD_STACK_SIZE);
    if (!t->kstack_base) {
        kfree(t);
        return STATUS_NO_MEMORY;
    }
    memset(t->kstack_base, 0, KTHREAD_STACK_SIZE);
    t->kstack_top = (uint64_t)t->kstack_base + KTHREAD_STACK_SIZE;
    t->kstack_sp  = setup_initial_kstack(t->kstack_base, (uint64_t)kthread_trampoline);

    t->process  = proc;
    t->state    = THREAD_READY;
    t->priority = priority ? priority : 1;
    t->quantum  = 4;
    t->entry    = entry;
    t->arg      = arg;
    t->is_user  = false;
    strncpy(t->name, name ? name : "kthread", sizeof(t->name) - 1);

    uint64_t flags = spin_lock_irqsave(&g_ps_lock);
    t->tid = g_next_tid++;
    proc->thread_count++;
    spin_unlock_irqrestore(&g_ps_lock, flags);

    sched_add_thread(t);
    if (out_thread) *out_thread = t;
    return STATUS_SUCCESS;
}

NTSTATUS ps_create_uthread(kprocess_t *proc, const char *name, uint64_t user_rip,
                           uint64_t user_rsp, uint8_t priority, kthread_t **out_thread) {
    if (!proc || proc->is_kernel) return STATUS_INVALID_PARAMETER;

    kthread_t *t = (kthread_t *)kzalloc(sizeof(kthread_t));
    if (!t) return STATUS_NO_MEMORY;

    t->kstack_base = (uint8_t *)kmalloc(KTHREAD_STACK_SIZE);
    if (!t->kstack_base) {
        kfree(t);
        return STATUS_NO_MEMORY;
    }
    memset(t->kstack_base, 0, KTHREAD_STACK_SIZE);
    t->kstack_top = (uint64_t)t->kstack_base + KTHREAD_STACK_SIZE;
    t->kstack_sp  = setup_initial_kstack(t->kstack_base, (uint64_t)uthread_trampoline);

    t->process  = proc;
    t->state    = THREAD_READY;
    t->priority = priority ? priority : 1;
    t->quantum  = 4;
    t->is_user  = true;
    t->user_rip = user_rip;
    t->user_rsp = user_rsp;
    strncpy(t->name, name ? name : "uthread", sizeof(t->name) - 1);

    uint64_t flags = spin_lock_irqsave(&g_ps_lock);
    t->tid = g_next_tid++;
    proc->thread_count++;
    spin_unlock_irqrestore(&g_ps_lock, flags);

    sched_add_thread(t);
    if (out_thread) *out_thread = t;
    return STATUS_SUCCESS;
}

HNT_NORETURN void ps_thread_exit(int32_t code) {
    hal_cli();
    kthread_t *t = ps_current_thread();
    kprocess_t *p = t->process;
    t->state = THREAD_TERMINATED;

    if (p && p != g_system_proc) {
        if (__atomic_sub_fetch(&p->thread_count, 1, __ATOMIC_ACQ_REL) == 0) {
            p->exit_code = code;
            p->exited    = true;
        }
    }

    for (;;) {
        sched_schedule();
        hal_hlt();
    }
}

HNT_NORETURN void ps_process_exit(kprocess_t *proc, int32_t code) {
    hal_cli();
    if (proc && proc != g_system_proc) {
        proc->exit_code = code;
        proc->exited    = true;
    }
    ps_thread_exit(code);
}

uint64_t ps_process_count(void) {
    uint64_t cnt = 0;
    uint64_t flags = spin_lock_irqsave(&g_ps_lock);
    for (kprocess_t *p = g_proc_list; p; p = p->next) {
        if (!p->exited) cnt++;
    }
    spin_unlock_irqrestore(&g_ps_lock, flags);
    return cnt;
}
