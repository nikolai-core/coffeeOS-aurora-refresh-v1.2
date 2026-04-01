#include <stdint.h>

#include "net/stack.h"

typedef struct __attribute__((packed)) EthernetHeader {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} EthernetHeader;

typedef struct __attribute__((packed)) IPv4Header {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} IPv4Header;

typedef struct IPv4FragmentSlot {
    uint8_t active;
    uint8_t protocol;
    uint16_t id;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t total_len;
    uint16_t max_seen;
    uint32_t last_update;
    uint8_t blocks[192 / 8];
    uint8_t data[NET_PACKET_BUFFER_SIZE];
} IPv4FragmentSlot;

static IPv4FragmentSlot ipv4_fragments[4];
static uint16_t ipv4_next_id = 1u;
static SpinLock ipv4_lock;

static void ipv4_mark_blocks(IPv4FragmentSlot *slot, uint16_t offset, uint16_t len) {
    uint16_t start = (uint16_t)(offset / 8u);
    uint16_t end = (uint16_t)((offset + len + 7u) / 8u);
    uint16_t i;

    for (i = start; i < end && i < 192u; i++) {
        slot->blocks[i / 8u] |= (uint8_t)(1u << (i & 7u));
    }
}

static int ipv4_all_blocks_present(IPv4FragmentSlot *slot) {
    uint16_t need = (uint16_t)((slot->total_len + 7u) / 8u);
    uint16_t i;

    for (i = 0u; i < need; i++) {
        if ((slot->blocks[i / 8u] & (uint8_t)(1u << (i & 7u))) == 0u) {
            return 0;
        }
    }
    return 1;
}

static void ipv4_dispatch_payload(uint32_t src_addr, uint32_t dst_addr, uint8_t protocol,
                                  const uint8_t *payload, uint16_t len) {
    if (protocol == NET_IP_PROTO_ICMP) {
        icmp_receive(src_addr, dst_addr, payload, len);
    } else if (protocol == NET_IP_PROTO_UDP) {
        udp_receive(src_addr, dst_addr, payload, len);
    } else if (protocol == NET_IP_PROTO_TCP) {
        tcp_receive(src_addr, dst_addr, payload, len);
    }
}

static void ipv4_reassemble(uint32_t src_addr, uint32_t dst_addr, uint16_t id, uint8_t protocol,
                            uint16_t frag_off, int more_frags, const uint8_t *payload, uint16_t len) {
    IPv4FragmentSlot *slot = (IPv4FragmentSlot *)0;
    uint32_t i;
    uint32_t flags;

    if ((uint32_t)frag_off + len > NET_PACKET_BUFFER_SIZE) {
        return;
    }

    flags = spin_lock_irqsave(&ipv4_lock);
    for (i = 0u; i < 4u; i++) {
        if (ipv4_fragments[i].active && ipv4_fragments[i].src_addr == src_addr && ipv4_fragments[i].dst_addr == dst_addr
            && ipv4_fragments[i].id == id && ipv4_fragments[i].protocol == protocol) {
            slot = &ipv4_fragments[i];
            break;
        }
    }
    if (slot == (IPv4FragmentSlot *)0) {
        for (i = 0u; i < 4u; i++) {
            if (!ipv4_fragments[i].active) {
                slot = &ipv4_fragments[i];
                net_memset(slot, 0u, sizeof(*slot));
                slot->active = 1u;
                slot->src_addr = src_addr;
                slot->dst_addr = dst_addr;
                slot->id = id;
                slot->protocol = protocol;
                break;
            }
        }
    }
    if (slot == (IPv4FragmentSlot *)0) {
        spin_unlock_irqrestore(&ipv4_lock, flags);
        return;
    }

    net_memcpy(slot->data + frag_off, payload, len);
    ipv4_mark_blocks(slot, frag_off, len);
    slot->last_update = net_now_ticks();
    if ((uint32_t)frag_off + len > slot->max_seen) {
        slot->max_seen = (uint16_t)(frag_off + len);
    }
    if (!more_frags) {
        slot->total_len = (uint16_t)(frag_off + len);
    }

    if (slot->total_len != 0u && ipv4_all_blocks_present(slot)) {
        uint8_t protocol_copy = slot->protocol;
        uint16_t total_len = slot->total_len;
        uint32_t src_copy = slot->src_addr;
        uint32_t dst_copy = slot->dst_addr;
        uint8_t data_copy[NET_PACKET_BUFFER_SIZE];

        net_memcpy(data_copy, slot->data, total_len);
        slot->active = 0u;
        spin_unlock_irqrestore(&ipv4_lock, flags);
        ipv4_dispatch_payload(src_copy, dst_copy, protocol_copy, data_copy, total_len);
        return;
    }
    spin_unlock_irqrestore(&ipv4_lock, flags);
}

void ipv4_init(void) {
    net_memset(ipv4_fragments, 0u, sizeof(ipv4_fragments));
    ipv4_lock.value = 0u;
}

void ipv4_tick(void) {
    uint32_t i;
    uint32_t now = net_now_ticks();
    uint32_t flags = spin_lock_irqsave(&ipv4_lock);

    for (i = 0u; i < 4u; i++) {
        if (ipv4_fragments[i].active && now - ipv4_fragments[i].last_update > 100u) {
            ipv4_fragments[i].active = 0u;
        }
    }
    spin_unlock_irqrestore(&ipv4_lock, flags);
}

void ipv4_receive(const uint8_t *frame, uint16_t len) {
    const EthernetHeader *eth;
    const IPv4Header *header;
    uint16_t header_len;
    uint16_t total_len;
    uint16_t frag_field;
    uint16_t frag_off;
    int more_frags;
    uint32_t src_addr;
    uint32_t dst_addr;
    const uint8_t *payload;
    uint16_t payload_len;

    if (len < sizeof(EthernetHeader) + sizeof(IPv4Header)) {
        return;
    }

    eth = (const EthernetHeader *)(const void *)frame;
    header = (const IPv4Header *)(const void *)(frame + sizeof(EthernetHeader));
    (void)eth;
    if ((header->version_ihl >> 4) != 4u) {
        return;
    }
    header_len = (uint16_t)((header->version_ihl & 0x0Fu) * 4u);
    if (header_len < sizeof(IPv4Header) || len < sizeof(EthernetHeader) + header_len) {
        return;
    }
    total_len = ntohs(header->total_length);
    if (total_len < header_len || total_len > len - sizeof(EthernetHeader)) {
        return;
    }
    if (net_ipv4_checksum(header, header_len) != 0u) {
        return;
    }

    src_addr = ntohl(header->src_addr);
    dst_addr = ntohl(header->dst_addr);
    if (dst_addr != net_if_config()->addr && dst_addr != 0xFFFFFFFFu
        && !net_ipv4_is_broadcast(dst_addr, net_if_config()->netmask)) {
        return;
    }

    frag_field = ntohs(header->flags_fragment);
    frag_off = (uint16_t)((frag_field & 0x1FFFu) * 8u);
    more_frags = (frag_field & 0x2000u) != 0u;
    payload = (const uint8_t *)header + header_len;
    payload_len = (uint16_t)(total_len - header_len);

    if (frag_off != 0u || more_frags) {
        ipv4_reassemble(src_addr, dst_addr, ntohs(header->identification), header->protocol,
                        frag_off, more_frags, payload, payload_len);
        return;
    }

    ipv4_dispatch_payload(src_addr, dst_addr, header->protocol, payload, payload_len);
}

int ipv4_send(uint32_t dst_addr, uint8_t protocol, const void *payload, uint16_t payload_len) {
    uint16_t remaining = payload_len;
    uint16_t offset = 0u;
    uint8_t frame[NET_PACKET_BUFFER_SIZE];
    uint8_t dst_mac[6];
    uint32_t next_hop = dst_addr;
    uint16_t packet_id;

    if ((dst_addr & net_if_config()->netmask) != (net_if_config()->addr & net_if_config()->netmask)) {
        next_hop = net_if_config()->gateway;
    }
    if (arp_resolve(next_hop, dst_mac) != 0) {
        return -1;
    }

    packet_id = ipv4_next_id++;
    while (remaining != 0u || (payload_len == 0u && offset == 0u)) {
        IPv4Header *header = (IPv4Header *)(void *)frame;
        uint16_t chunk = remaining;
        uint16_t flags_fragment = (uint16_t)(offset / 8u);

        if (chunk > 1480u) {
            chunk = 1480u;
        }
        if (remaining > chunk) {
            chunk = (uint16_t)(chunk & ~7u);
            flags_fragment |= 0x2000u;
        }

        header->version_ihl = 0x45u;
        header->dscp_ecn = 0u;
        header->total_length = htons((uint16_t)(sizeof(IPv4Header) + chunk));
        header->identification = htons(packet_id);
        header->flags_fragment = htons(flags_fragment);
        header->ttl = 64u;
        header->protocol = protocol;
        header->header_checksum = 0u;
        header->src_addr = htonl(net_if_config()->addr);
        header->dst_addr = htonl(dst_addr);
        if (chunk != 0u) {
            net_memcpy(frame + sizeof(IPv4Header), (const uint8_t *)payload + offset, chunk);
        }
        header->header_checksum = net_ipv4_checksum(header, sizeof(IPv4Header));
        if (ethernet_send_frame(dst_mac, NET_ETHERTYPE_IPV4, frame, (uint16_t)(sizeof(IPv4Header) + chunk)) != 0) {
            return -1;
        }
        if (remaining == 0u) {
            break;
        }
        remaining = (uint16_t)(remaining - chunk);
        offset = (uint16_t)(offset + chunk);
    }

    return (int)payload_len;
}
