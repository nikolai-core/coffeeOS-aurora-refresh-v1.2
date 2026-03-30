#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>

#include "syscall_numbers.h"

static inline int sys_write(const char *buf, uint32_t len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE), "b"(buf), "c"(len)
        : "memory"
    );
    return ret;
}

static inline int sys_readchar(char *out) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_READCHAR), "b"(out)
        : "memory"
    );
    return ret;
}

static inline void sys_exit(int code) {
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(SYS_EXIT), "b"(code)
        : "memory"
    );
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static inline uint32_t sys_gettime(void) {
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_GETTIME)
        : "memory"
    );
    return ret;
}

static inline int sys_audio_play(uint32_t freq, uint32_t duration_ms, uint32_t wave) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_AUDIO_PLAY), "b"(freq), "c"(duration_ms), "d"(wave)
        : "memory"
    );
    return ret;
}

#endif
