#pragma once

#include <horizon/types.h>

typedef struct {
    volatile uint32_t locked;
} kspinlock_t;

#define KSPINLOCK_INIT { 0 }

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    __asm__ volatile ("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
}

static inline uint64_t spin_lock_irqsave(kspinlock_t *lock) {
    uint64_t flags = irq_save();
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile ("pause" ::: "memory");
    }
    return flags;
}

static inline void spin_unlock_irqrestore(kspinlock_t *lock, uint64_t flags) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
    irq_restore(flags);
}

static inline void spin_lock(kspinlock_t *lock) {
    while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE) != 0) {
        __asm__ volatile ("pause" ::: "memory");
    }
}

static inline void spin_unlock(kspinlock_t *lock) {
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
}
