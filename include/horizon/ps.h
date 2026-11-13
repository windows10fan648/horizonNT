#pragma once

#include <horizon/types.h>
#include <horizon/mm.h>
#include <horizon/ob.h>

#define KTHREAD_STACK_SIZE (16 * 1024)
#define UTHREAD_STACK_TOP  0x00007FFFFFFFE000ULL
#define UTHREAD_STACK_PAGES 4

typedef enum {
    THREAD_READY = 0,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

typedef struct kprocess {
    uint64_t          pid;
    char              name[32];
    vmm_space_t      *addr_space;
    ob_handle_table_t handles;
    int32_t           exit_code;
    bool              is_kernel;
    bool              exited;
    uint64_t          thread_count;
    struct kprocess  *next;
} kprocess_t;

typedef void (*kthread_entry_t)(void *arg);

typedef struct kthread {
    uint64_t        tid;
    char            name[32];
    kprocess_t     *process;
    thread_state_t  state;
    uint8_t         priority;
    uint32_t        quantum;
    uint64_t        wake_tick;
    uint64_t        total_ticks;

    /* Saved kernel stack pointer for ctx_switch */
    uint64_t        kstack_sp;
    uint8_t        *kstack_base;
    uint64_t        kstack_top;

    /* Initial entry info */
    kthread_entry_t entry;
    void           *arg;
    bool            is_user;
    uint64_t        user_rip;
    uint64_t        user_rsp;

    struct kthread *next;
    struct kthread *sched_next;
} kthread_t;

void        ps_init(void);
kprocess_t *ps_system_process(void);
kprocess_t *ps_current_process(void);
kthread_t  *ps_current_thread(void);

NTSTATUS    ps_create_process(const char *name, bool is_kernel, kprocess_t **out_proc);
NTSTATUS    ps_create_kthread(kprocess_t *proc, const char *name, kthread_entry_t entry,
                              void *arg, uint8_t priority, kthread_t **out_thread);
NTSTATUS    ps_create_uthread(kprocess_t *proc, const char *name, uint64_t user_rip,
                              uint64_t user_rsp, uint8_t priority, kthread_t **out_thread);

void        ps_yield(void);
void        ps_sleep_ms(uint64_t ms);
HNT_NORETURN void ps_thread_exit(int32_t code);
HNT_NORETURN void ps_process_exit(kprocess_t *proc, int32_t code);

void        sched_tick(void);
void        sched_dump_tasks(void);
uint64_t    ps_process_count(void);
uint64_t    ps_thread_count(void);
