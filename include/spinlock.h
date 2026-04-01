#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct SpinLock {
    volatile uint32_t value;
} SpinLock;

static inline uint32_t spin_lock_irqsave(SpinLock *lock) {
    uint32_t flags;
    uint32_t state = 1u;

    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    while (state != 0u) {
        __asm__ volatile ("xchgl %0, %1" : "+r"(state), "+m"(lock->value) : : "memory");
        if (state != 0u) {
            __asm__ volatile ("pause");
            state = 1u;
        }
    }
    return flags;
}

static inline void spin_unlock_irqrestore(SpinLock *lock, uint32_t flags) {
    __asm__ volatile ("" : : : "memory");
    lock->value = 0u;
    __asm__ volatile ("" : : : "memory");
    if ((flags & 0x200u) != 0u) {
        __asm__ volatile ("sti");
    }
}

#endif
