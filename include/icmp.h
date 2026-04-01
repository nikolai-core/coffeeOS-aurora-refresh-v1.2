#ifndef ICMP_H
#define ICMP_H

#include <stdint.h>

#include "ip.h"

struct NetInterface;

typedef struct __attribute__((packed)) IcmpHeader {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t sequence;
} IcmpHeader;

#define ICMP_ECHO_REQUEST 8u
#define ICMP_ECHO_REPLY   0u

typedef struct PingState {
    uint16_t id;
    uint16_t seq;
    int      received;
    uint32_t sent_tick;
    uint32_t rtt_ticks;
} PingState;

/* Parse one ICMP payload attached to an IPv4 packet. */
void icmp_receive(const IpHeader *ip, const void *payload, uint16_t len);

/* Send one blocking ICMP echo request to a host-order IPv4 address. */
int icmp_ping(struct NetInterface *iface, uint32_t dst_ip,
              uint16_t seq, uint32_t timeout_ticks);

/* Return the last successful ping RTT in PIT ticks. */
uint32_t icmp_last_rtt_ticks(void);

#endif
