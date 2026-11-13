#include <horizon/ps.h>
#include <horizon/hal.h>
#include <horizon/console.h>

extern void ctx_switch(uint64_t *old_sp, uint64_t new_sp);

kthread_t *g_current_thread = NULL;
static kthread_t *g_thread_list = NULL;
static kspinlock_t g_sched_lock = KSPINLOCK_INIT;

void sched_init_boot(kthread_t *boot_thread) {
    g_current_thread = boot_thread;
    g_thread_list    = boot_thread;
    boot_thread->sched_next = NULL;
}

kthread_t *ps_current_thread(void) {
    return g_current_thread;
}

void sched_add_thread(kthread_t *t) {
    uint64_t flags = spin_lock_irqsave(&g_sched_lock);
    t->sched_next = g_thread_list;
    g_thread_list = t;
    spin_unlock_irqrestore(&g_sched_lock, flags);
}

void sched_reap(void) {
    uint64_t flags = irq_save();
    kthread_t *prev = NULL;
    kthread_t *cur  = g_thread_list;
    while (cur) {
        kthread_t *nxt = cur->sched_next;
        if (cur != g_current_thread && cur->state == THREAD_TERMINATED && cur->tid != 0) {
            if (prev) prev->sched_next = nxt;
            else      g_thread_list    = nxt;

            kprocess_t *p = cur->process;
            if (cur->kstack_base) kfree(cur->kstack_base);
            kfree(cur);

            if (p && p->exited && !p->is_kernel && p->addr_space) {
                ob_handle_table_destroy(&p->handles);
                vmm_destroy_user_space(p->addr_space);
                p->addr_space = NULL;
            }
        } else {
            prev = cur;
        }
        cur = nxt;
    }
    irq_restore(flags);
}

void sched_schedule(void) {
    uint64_t flags = irq_save();
    if (!g_current_thread) {
        irq_restore(flags);
        return;
    }

    uint64_t now = hal_get_ticks();
    for (kthread_t *t = g_thread_list; t; t = t->sched_next) {
        if (t->state == THREAD_BLOCKED && t->wake_tick != 0 && now >= t->wake_tick) {
            t->wake_tick = 0;
            t->state     = THREAD_READY;
        }
    }

    kthread_t *prev = g_current_thread;
    kthread_t *best = NULL;

    /* Round-robin starting after prev, preferring highest priority ready thread */
    kthread_t *start = prev->sched_next ? prev->sched_next : g_thread_list;
    kthread_t *cur   = start;
    do {
        if (cur->state == THREAD_READY || (cur == prev && cur->state == THREAD_RUNNING)) {
            if (!best || cur->priority > best->priority) {
                best = cur;
            }
        }
        cur = cur->sched_next ? cur->sched_next : g_thread_list;
    } while (cur != start);

    if (!best) best = prev;

    if (best != prev) {
        if (prev->state == THREAD_RUNNING) {
            prev->state = THREAD_READY;
        }
        best->state   = THREAD_RUNNING;
        best->quantum = 4;
        g_current_thread = best;

        tss_set_rsp0(best->kstack_top);
        vmm_switch_space(best->process ? best->process->addr_space : vmm_kernel_space());
        ctx_switch(&prev->kstack_sp, best->kstack_sp);
    }

    irq_restore(flags);
}

void sched_tick(void) {
    if (!g_current_thread) return;
    g_current_thread->total_ticks++;
    if (g_current_thread->quantum > 0) {
        g_current_thread->quantum--;
    }
    if (g_current_thread->quantum == 0) {
        sched_schedule();
    }
}

void ps_yield(void) {
    sched_schedule();
}

void ps_sleep_ms(uint64_t ms) {
    uint64_t ticks = (ms * 250ULL + 999ULL) / 1000ULL;
    if (ticks == 0) ticks = 1;

    uint64_t flags = irq_save();
    kthread_t *t = g_current_thread;
    if (t && t->tid != 0) {
        t->wake_tick = hal_get_ticks() + ticks;
        t->state     = THREAD_BLOCKED;
        sched_schedule();
        irq_restore(flags);
    } else {
        irq_restore(flags);
        uint64_t target = hal_get_ticks() + ticks;
        while (hal_get_ticks() < target) {
            hal_hlt();
        }
    }
}

uint64_t ps_thread_count(void) {
    uint64_t cnt = 0;
    uint64_t flags = irq_save();
    for (kthread_t *t = g_thread_list; t; t = t->sched_next) {
        if (t->state != THREAD_TERMINATED) cnt++;
    }
    irq_restore(flags);
    return cnt;
}

void sched_dump_tasks(void) {
    static const char *state_str[] = { "READY", "RUNNING", "BLOCKED", "TERM" };

    /* Drop terminated threads so the listing reflects live tasks only. */
    sched_reap();

    kprintf(ANSI_BRIGHT_CYAN
            "%-5s %-5s %-18s %-14s %-8s %-4s %s" ANSI_RESET "\n",
            "PID", "TID", "THREAD", "PROCESS", "STATE", "PRI", "TICKS");
    uint64_t flags = irq_save();
    for (kthread_t *t = g_thread_list; t; t = t->sched_next) {
        kprintf("%-5llu %-5llu %-18s %-14s %-8s %-4u %llu\n",
                t->process ? t->process->pid : 0,
                t->tid,
                t->name,
                t->process ? t->process->name : "?",
                state_str[t->state],
                t->priority,
                t->total_ticks);
    }
    irq_restore(flags);
}
