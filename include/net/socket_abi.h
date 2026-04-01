#ifndef NET_SOCKET_ABI_H
#define NET_SOCKET_ABI_H

#include <stdint.h>

#define AF_INET      2
#define SOCK_STREAM  1
#define SOCK_DGRAM   2

#define NET_SOCK_NONBLOCK 0x01u

typedef struct NetSockAddrIn {
    uint16_t family;
    uint16_t port;
    uint32_t addr;
} NetSockAddrIn;

static inline uint16_t net_bswap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static inline uint32_t net_bswap32(uint32_t value) {
    return ((value & 0x000000FFu) << 24)
        | ((value & 0x0000FF00u) << 8)
        | ((value & 0x00FF0000u) >> 8)
        | ((value & 0xFF000000u) >> 24);
}

static inline uint16_t htons(uint16_t value) {
    return net_bswap16(value);
}

static inline uint16_t ntohs(uint16_t value) {
    return net_bswap16(value);
}

static inline uint32_t htonl(uint32_t value) {
    return net_bswap32(value);
}

static inline uint32_t ntohl(uint32_t value) {
    return net_bswap32(value);
}

#endif
