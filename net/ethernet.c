#include <stdint.h>

#include "ethernet.h"
#include "netif.h"

static uint8_t eth_frame_buf[ETH_FRAME_MAX];

/* Copy one fixed-size MAC address. */
static void eth_copy_mac(uint8_t *dst, const uint8_t *src) {
    uint32_t i;

    for (i = 0u; i < 6u; i++) {
        dst[i] = src[i];
    }
}

/* Copy one raw payload without libc. */
static void eth_copy_bytes(uint8_t *dst, const uint8_t *src, uint16_t len) {
    uint16_t i;

    for (i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
}

/* Build and transmit one Ethernet frame with a layer-3 payload. */
int eth_send(struct NetInterface *iface, const uint8_t *dst_mac,
             uint16_t ethertype, const void *payload, uint16_t len) {
    EthHeader *eth;

    if (iface == (struct NetInterface *)0 || dst_mac == (const uint8_t *)0
        || payload == (const void *)0 || len > ETH_MTU || iface->send == (int (*)(const void *, uint16_t))0) {
        return -1;
    }

    eth = (EthHeader *)(void *)eth_frame_buf;
    eth_copy_mac(eth->dst, dst_mac);
    eth_copy_mac(eth->src, iface->mac);
    eth->ethertype = htons(ethertype);
    eth_copy_bytes(eth_frame_buf + sizeof(EthHeader), (const uint8_t *)payload, len);
    if (iface->send(eth_frame_buf, (uint16_t)(len + sizeof(EthHeader))) != 0) {
        return -1;
    }

    iface->tx_packets++;
    iface->tx_bytes += (uint64_t)(len + sizeof(EthHeader));
    return 0;
}
