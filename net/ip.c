#include <stdint.h>

#include "arp.h"
#include "ethernet.h"
#include "icmp.h"
#include "ip.h"
#include "netif.h"
#include "tcp.h"
#include "udp.h"

static uint16_t ip_id_counter;
static const uint8_t ip_broadcast_mac[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
static uint8_t ip_packet_buf[ETH_MTU];

/* Compute the standard IPv4/transport one's-complement checksum. */
uint16_t ip_checksum(const void *data, uint16_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0u;
    uint16_t i;

    for (i = 0u; i + 1u < len; i += 2u) {
        sum += ((uint32_t)bytes[i] << 8) | (uint32_t)bytes[i + 1u];
    }
    if ((len & 1u) != 0u) {
        sum += (uint32_t)bytes[len - 1u] << 8;
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

/* Build and send one IPv4 packet to a host-order destination IPv4 address. */
int ip_send(struct NetInterface *iface, uint32_t dst_ip, uint8_t proto,
            const void *payload, uint16_t payload_len) {
    IpHeader *ip;
    uint8_t next_hop_mac[6];
    uint16_t total_len;
    uint32_t next_hop_ip;

    if (iface == (struct NetInterface *)0 || payload == (const void *)0) {
        return -1;
    }
    total_len = (uint16_t)(sizeof(IpHeader) + payload_len);
    if (total_len > ETH_MTU) {
        return -1;
    }

    ip = (IpHeader *)(void *)ip_packet_buf;
    ip->version_ihl = 0x45u;
    ip->dscp_ecn = 0u;
    ip->total_len = htons(total_len);
    ip->id = htons(ip_id_counter++);
    ip->flags_frag = htons(IP_FLAG_DF);
    ip->ttl = 64u;
    ip->protocol = proto;
    ip->checksum = 0u;
    ip->src_ip = htonl(iface->ip);
    ip->dst_ip = htonl(dst_ip);
    {
        uint16_t i;
        const uint8_t *src = (const uint8_t *)payload;
        uint8_t *dst = ip_packet_buf + sizeof(IpHeader);

        for (i = 0u; i < payload_len; i++) {
            dst[i] = src[i];
        }
    }
    ip->checksum = htons(ip_checksum(ip, (uint16_t)sizeof(IpHeader)));

    if (dst_ip == 0xFFFFFFFFu) {
        return eth_send(iface, ip_broadcast_mac, ETH_TYPE_IP, ip_packet_buf, total_len);
    }

    if ((dst_ip & iface->netmask) == (iface->ip & iface->netmask) || iface->gateway == 0u) {
        next_hop_ip = dst_ip;
    } else {
        next_hop_ip = iface->gateway;
    }
    if (arp_resolve(iface, next_hop_ip, next_hop_mac) != 0) {
        return -1;
    }
    return eth_send(iface, next_hop_mac, ETH_TYPE_IP, ip_packet_buf, total_len);
}

/* Parse one Ethernet+IPv4 frame and dispatch the layer-4 payload. */
void ip_receive(const void *frame, uint16_t len) {
    const uint8_t *bytes = (const uint8_t *)frame;
    const EthHeader *eth;
    const IpHeader *ip;
    NetInterface *iface = netif_get(0);
    uint16_t ip_len;
    uint16_t hdr_len;
    const uint8_t *payload;
    uint16_t payload_len;

    if (iface == (NetInterface *)0 || frame == (const void *)0
        || len < (uint16_t)(sizeof(EthHeader) + sizeof(IpHeader))) {
        return;
    }

    eth = (const EthHeader *)frame;
    if (ntohs(eth->ethertype) != ETH_TYPE_IP) {
        return;
    }

    ip = (const IpHeader *)(const void *)(bytes + sizeof(EthHeader));
    if ((ip->version_ihl >> 4) != 4u) {
        return;
    }
    hdr_len = (uint16_t)((ip->version_ihl & 0x0Fu) * 4u);
    if (hdr_len < sizeof(IpHeader)) {
        return;
    }
    ip_len = ntohs(ip->total_len);
    if (ip_len < hdr_len || len < (uint16_t)(sizeof(EthHeader) + ip_len)) {
        return;
    }
    if (ip_checksum(ip, hdr_len) != 0u) {
        return;
    }

    payload = bytes + sizeof(EthHeader) + hdr_len;
    payload_len = (uint16_t)(ip_len - hdr_len);
    if (ip->protocol == IP_PROTO_ICMP) {
        icmp_receive(ip, payload, payload_len);
    } else if (ip->protocol == IP_PROTO_UDP) {
        udp_receive(ip, payload, payload_len);
    } else if (ip->protocol == IP_PROTO_TCP) {
        tcp_receive(ip, payload, payload_len);
    }
}
