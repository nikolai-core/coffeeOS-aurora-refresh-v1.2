#ifndef X86_CPU_H
#define X86_CPU_H

#include <stdint.h>

static inline void x86_cpuid(uint32_t leaf, uint32_t subleaf,
                             uint32_t *eax_out, uint32_t *ebx_out, uint32_t *ecx_out, uint32_t *edx_out) {
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax_out), "=b"(*ebx_out), "=c"(*ecx_out), "=d"(*edx_out)
        : "a"(leaf), "c"(subleaf)
    );
}

#endif

