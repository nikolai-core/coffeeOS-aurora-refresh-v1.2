#ifndef ETHERNET_H
#define ETHERNET_H

#include <stdint.h>

struct NetInterface;

typedef struct __attribute__((packed)) EthHeader {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} EthHeader;

#define ETH_TYPE_IP  0x0800u
#define ETH_TYPE_ARP 0x0806u

static inline uint16_t htons(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFF000000u) >> 24)
         | ((v & 0x00FF0000u) >> 8)
         | ((v & 0x0000FF00u) << 8)
         | ((v & 0x000000FFu) << 24);
}

#define ntohs(v) htons(v)
#define ntohl(v) htonl(v)

/* Build and transmit one Ethernet frame with a layer-3 payload. */
int eth_send(struct NetInterface *iface, const uint8_t *dst_mac,
             uint16_t ethertype, const void *payload, uint16_t len);

#endif
