#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>

#include "syscall_numbers.h"
#include "vfs.h"

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

static inline int sys_open(const char *path, uint32_t flags) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_OPEN), "b"(path), "c"(flags)
        : "memory"
    );
    return ret;
}

static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_CLOSE), "b"(fd)
        : "memory"
    );
    return ret;
}

static inline int sys_read(int fd, void *buf, uint32_t len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_READ), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

static inline int sys_write_fd(int fd, const void *buf, uint32_t len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_WRITE_FD), "b"(fd), "c"(buf), "d"(len)
        : "memory"
    );
    return ret;
}

static inline int sys_seek(int fd, int32_t offset, int whence) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SEEK), "b"(fd), "c"(offset), "d"(whence)
        : "memory"
    );
    return ret;
}

static inline int sys_mkdir(const char *path) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_MKDIR), "b"(path)
        : "memory"
    );
    return ret;
}

static inline int sys_delete(const char *path) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_DELETE), "b"(path)
        : "memory"
    );
    return ret;
}

static inline int sys_stat(const char *path, VfsDirEntry *out) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_STAT), "b"(path), "c"(out)
        : "memory"
    );
    return ret;
}

static inline int sys_listdir(const char *path, VfsDirEntry *entries, int max) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_LISTDIR), "b"(path), "c"(entries), "d"(max)
        : "memory"
    );
    return ret;
}

#endif
