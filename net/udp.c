#include <stdint.h>

#include "ethernet.h"
#include "ip.h"
#include "netif.h"
#include "udp.h"

static UdpSocket udp_sockets[UDP_MAX_SOCKETS];

/* Open one callback-based UDP socket bound to a local port. */
int udp_open(uint16_t local_port, UdpRecvCallback cb) {
    uint32_t i;

    if (local_port == 0u || cb == (UdpRecvCallback)0) {
        return -1;
    }
    for (i = 0u; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].open && udp_sockets[i].local_port == local_port) {
            return -1;
        }
    }
    for (i = 0u; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].open) {
            udp_sockets[i].local_port = local_port;
            udp_sockets[i].callback = cb;
            udp_sockets[i].open = 1;
            return (int)i;
        }
    }
    return -1;
}

/* Close one callback-based UDP socket by index. */
void udp_close(int fd) {
    if (fd < 0 || (uint32_t)fd >= UDP_MAX_SOCKETS) {
        return;
    }
    udp_sockets[fd].open = 0;
    udp_sockets[fd].local_port = 0u;
    udp_sockets[fd].callback = (UdpRecvCallback)0;
}

/* Send one UDP datagram to a host-order IPv4/port tuple. */
int udp_send(struct NetInterface *iface, uint32_t dst_ip,
             uint16_t src_port, uint16_t dst_port,
             const void *data, uint16_t len) {
    uint8_t packet[sizeof(UdpHeader) + ETH_MTU];
    UdpHeader *udp = (UdpHeader *)(void *)packet;

    if (iface == (struct NetInterface *)0 || data == (const void *)0
        || (uint32_t)len + sizeof(UdpHeader) > sizeof(packet)) {
        return -1;
    }
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons((uint16_t)(sizeof(UdpHeader) + len));
    udp->checksum = 0u;
    {
        uint16_t i;
        uint8_t *dst = packet + sizeof(UdpHeader);
        const uint8_t *src = (const uint8_t *)data;

        for (i = 0u; i < len; i++) {
            dst[i] = src[i];
        }
    }
    return ip_send(iface, dst_ip, IP_PROTO_UDP, packet, (uint16_t)(sizeof(UdpHeader) + len));
}

/* Parse one UDP payload attached to an IPv4 packet. */
void udp_receive(const IpHeader *ip, const void *payload, uint16_t len) {
    const UdpHeader *udp = (const UdpHeader *)payload;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t udp_len;
    uint32_t i;

    if (ip == (const IpHeader *)0 || payload == (const void *)0 || len < sizeof(UdpHeader)) {
        return;
    }

    udp_len = ntohs(udp->length);
    if (udp_len < sizeof(UdpHeader) || udp_len > len) {
        return;
    }
    src_port = ntohs(udp->src_port);
    dst_port = ntohs(udp->dst_port);
    for (i = 0u; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].open && udp_sockets[i].local_port == dst_port) {
            udp_sockets[i].callback(ntohl(ip->src_ip), src_port,
                                    (const uint8_t *)payload + sizeof(UdpHeader),
                                    (uint16_t)(udp_len - sizeof(UdpHeader)));
            return;
        }
    }
}
