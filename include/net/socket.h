#ifndef NET_SOCKET_H
#define NET_SOCKET_H

#include <stdint.h>

#include "net/socket_abi.h"
#include "syscall_numbers.h"

static inline int socket(int domain, int type, int protocol) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SOCKET), "b"(domain), "c"(type), "d"(protocol)
        : "memory"
    );
    return ret;
}

static inline int bind(int fd, const NetSockAddrIn *addr, uint32_t addr_len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_BIND), "b"(fd), "c"(addr), "d"(addr_len)
        : "memory"
    );
    return ret;
}

static inline int listen(int fd, int backlog) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_LISTEN), "b"(fd), "c"(backlog)
        : "memory"
    );
    return ret;
}

static inline int accept(int fd, NetSockAddrIn *addr, uint32_t *addr_len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_ACCEPT), "b"(fd), "c"(addr), "d"(addr_len)
        : "memory"
    );
    return ret;
}

static inline int connect(int fd, const NetSockAddrIn *addr, uint32_t addr_len) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_CONNECT), "b"(fd), "c"(addr), "d"(addr_len)
        : "memory"
    );
    return ret;
}

static inline int send(int fd, const void *buf, uint32_t len, uint32_t flags) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SEND), "b"(fd), "c"(buf), "d"(len), "S"(flags)
        : "memory"
    );
    return ret;
}

static inline int recv(int fd, void *buf, uint32_t len, uint32_t flags) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_RECV), "b"(fd), "c"(buf), "d"(len), "S"(flags)
        : "memory"
    );
    return ret;
}

static inline int sendto(int fd, const void *buf, uint32_t len, uint32_t flags,
                         const NetSockAddrIn *addr, uint32_t addr_len) {
    int ret;
    (void)addr_len;
    __asm__ volatile (
        "int $0x80\n"
        : "=a"(ret)
        : "a"(SYS_SENDTO), "b"(fd), "c"(buf), "d"(len), "S"(flags), "D"(addr)
        : "memory"
    );
    return ret;
}

static inline int recvfrom(int fd, void *buf, uint32_t len, uint32_t flags,
                           NetSockAddrIn *addr, uint32_t *addr_len) {
    int ret;
    (void)addr_len;
    __asm__ volatile (
        "int $0x80\n"
        : "=a"(ret)
        : "a"(SYS_RECVFROM), "b"(fd), "c"(buf), "d"(len), "S"(addr), "D"(flags)
        : "memory"
    );
    return ret;
}

static inline int closesocket(int fd) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(SYS_SOCKET_CLOSE), "b"(fd)
        : "memory"
    );
    return ret;
}

#endif
