#include <stdint.h>

#include "ethernet.h"
#include "icmp.h"
#include "ip.h"
#include "netif.h"
#include "pit.h"
#include "rtl8139.h"

static PingState ping_state;
static uint8_t icmp_packet_buf[64];

/* Parse one ICMP payload attached to an IPv4 packet. */
void icmp_receive(const IpHeader *ip, const void *payload, uint16_t len) {
    const IcmpHeader *icmp = (const IcmpHeader *)payload;
    uint8_t *packet = icmp_packet_buf;
    IcmpHeader *reply = (IcmpHeader *)(void *)packet;
    uint32_t src_ip;

    if (ip == (const IpHeader *)0 || payload == (const void *)0 || len < sizeof(IcmpHeader)) {
        return;
    }
    if (ip_checksum(payload, len) != 0u) {
        return;
    }

    src_ip = ntohl(ip->src_ip);
    if (icmp->type == ICMP_ECHO_REQUEST) {
        struct NetInterface *iface = netif_default();
        uint16_t i;

        if (iface == (struct NetInterface *)0 || len > sizeof(icmp_packet_buf)) {
            return;
        }
        for (i = 0u; i < len; i++) {
            packet[i] = ((const uint8_t *)payload)[i];
        }
        reply->type = ICMP_ECHO_REPLY;
        reply->checksum = 0u;
        reply->checksum = htons(ip_checksum(packet, len));
        (void)ip_send(iface, src_ip, IP_PROTO_ICMP, packet, len);
        return;
    }

    if (icmp->type == ICMP_ECHO_REPLY
        && ntohs(icmp->id) == ping_state.id
        && ntohs(icmp->sequence) == ping_state.seq) {
        ping_state.received = 1;
        ping_state.rtt_ticks = get_ticks() - ping_state.sent_tick;
    }
}

/* Send one blocking ICMP echo request to a host-order IPv4 address. */
int icmp_ping(struct NetInterface *iface, uint32_t dst_ip,
              uint16_t seq, uint32_t timeout_ticks) {
    uint8_t packet[sizeof(IcmpHeader) + 32u];
    IcmpHeader *icmp = (IcmpHeader *)(void *)packet;
    uint32_t start_tick;
    uint16_t i;

    if (iface == (struct NetInterface *)0) {
        return -1;
    }

    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0u;
    icmp->checksum = 0u;
    icmp->id = htons((uint16_t)(get_ticks() & 0xFFFFu));
    icmp->sequence = htons(seq);
    for (i = 0u; i < 32u; i++) {
        packet[sizeof(IcmpHeader) + i] = 0xA5u;
    }
    icmp->checksum = htons(ip_checksum(packet, (uint16_t)sizeof(packet)));

    ping_state.id = ntohs(icmp->id);
    ping_state.seq = seq;
    ping_state.received = 0;
    ping_state.sent_tick = get_ticks();
    ping_state.rtt_ticks = 0u;

    if (ip_send(iface, dst_ip, IP_PROTO_ICMP, packet, (uint16_t)sizeof(packet)) != 0) {
        return -1;
    }

    start_tick = get_ticks();
    while ((get_ticks() - start_tick) < timeout_ticks) {
        rtl8139_poll();
        if (ping_state.received) {
            return 0;
        }
    }
    return -1;
}

/* Return the last successful ping RTT in PIT ticks. */
uint32_t icmp_last_rtt_ticks(void) {
    return ping_state.rtt_ticks;
}
