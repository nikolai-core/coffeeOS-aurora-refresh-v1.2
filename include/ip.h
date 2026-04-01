#ifndef IP_H
#define IP_H

#include <stdint.h>

struct NetInterface;

typedef struct __attribute__((packed)) IpHeader {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} IpHeader;

#define IP_PROTO_ICMP  1u
#define IP_PROTO_TCP   6u
#define IP_PROTO_UDP   17u

#define IP_FLAG_DF (1u << 14)

/* Compute the standard IPv4/transport one's-complement checksum. */
uint16_t ip_checksum(const void *data, uint16_t len);

/* Build and send one IPv4 packet to a host-order destination IPv4 address. */
int ip_send(struct NetInterface *iface, uint32_t dst_ip, uint8_t proto,
            const void *payload, uint16_t payload_len);

/* Parse one Ethernet+IPv4 frame and dispatch the layer-4 payload. */
void ip_receive(const void *frame, uint16_t len);

#endif
