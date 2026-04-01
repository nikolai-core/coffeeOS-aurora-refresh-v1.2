#include <stdint.h>

#include "arp.h"
#include "ascii_util.h"
#include "ethernet.h"
#include "net.h"
#include "netif.h"
#include "pit.h"
#include "rtl8139.h"
#include "serial.h"

#define ARP_TTL_TICKS 30000u

static ArpEntry arp_cache[ARP_CACHE_SIZE];
static const uint8_t arp_broadcast_mac[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
static const uint8_t arp_zero_mac[6] = {0u, 0u, 0u, 0u, 0u, 0u};

/* Copy one fixed-size MAC address. */
static void arp_copy_mac(uint8_t *dst, const uint8_t *src) {
    uint32_t i;

    for (i = 0u; i < 6u; i++) {
        dst[i] = src[i];
    }
}

/* Return non-zero when two MAC addresses match exactly. */
static int arp_mac_equal(const uint8_t *a, const uint8_t *b) {
    uint32_t i;

    for (i = 0u; i < 6u; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* Insert or refresh one ARP cache entry from host-order IP/MAC data. */
static void arp_cache_update(uint32_t ip, const uint8_t *mac) {
    uint32_t i;
    int free_slot = -1;
    int replaced = 0;

    for (i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            replaced = !arp_mac_equal(arp_cache[i].mac, mac);
            arp_copy_mac(arp_cache[i].mac, mac);
            arp_cache[i].ttl = ARP_TTL_TICKS;
            if (replaced) {
                netmon_log("ARP entry updated");
            }
            return;
        }
        if (!arp_cache[i].valid && free_slot < 0) {
            free_slot = (int)i;
        }
    }

    if (free_slot < 0) {
        free_slot = 0;
    }
    arp_cache[free_slot].valid = 1;
    arp_cache[free_slot].ip = ip;
    arp_copy_mac(arp_cache[free_slot].mac, mac);
    arp_cache[free_slot].ttl = ARP_TTL_TICKS;
    netmon_log("ARP entry cached");
}

/* Reset the static ARP cache. */
void arp_init(void) {
    uint32_t i;

    for (i = 0u; i < ARP_CACHE_SIZE; i++) {
        arp_cache[i].valid = 0;
        arp_cache[i].ttl = 0u;
    }
}

/* Look up one host-order IPv4 address in the cache. */
int arp_lookup(uint32_t ip, uint8_t *out_mac) {
    uint32_t i;

    for (i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            if (out_mac != (uint8_t *)0) {
                arp_copy_mac(out_mac, arp_cache[i].mac);
            }
            return 0;
        }
    }
    return -1;
}

/* Broadcast one ARP request for a target IPv4 address. */
void arp_request(struct NetInterface *iface, uint32_t target_ip) {
    ArpPacket pkt;

    if (iface == (struct NetInterface *)0) {
        return;
    }
    pkt.htype = htons(1u);
    pkt.ptype = htons(ETH_TYPE_IP);
    pkt.hlen = 6u;
    pkt.plen = 4u;
    pkt.oper = htons(1u);
    arp_copy_mac(pkt.sha, iface->mac);
    pkt.spa = htonl(iface->ip);
    arp_copy_mac(pkt.tha, arp_zero_mac);
    pkt.tpa = htonl(target_ip);
    netmon_log("ARP request");
    (void)eth_send(iface, arp_broadcast_mac, ETH_TYPE_ARP, &pkt, (uint16_t)sizeof(pkt));
}

/* Send one ARP reply for a target host-order IPv4 address and MAC. */
void arp_reply(struct NetInterface *iface, const uint8_t *dst_mac, uint32_t dst_ip) {
    ArpPacket pkt;

    if (iface == (struct NetInterface *)0 || dst_mac == (const uint8_t *)0) {
        return;
    }
    pkt.htype = htons(1u);
    pkt.ptype = htons(ETH_TYPE_IP);
    pkt.hlen = 6u;
    pkt.plen = 4u;
    pkt.oper = htons(2u);
    arp_copy_mac(pkt.sha, iface->mac);
    pkt.spa = htonl(iface->ip);
    arp_copy_mac(pkt.tha, dst_mac);
    pkt.tpa = htonl(dst_ip);
    (void)eth_send(iface, dst_mac, ETH_TYPE_ARP, &pkt, (uint16_t)sizeof(pkt));
}

/* Send one gratuitous ARP for the interface's configured IPv4 address. */
void arp_announce(struct NetInterface *iface) {
    if (iface == (struct NetInterface *)0 || iface->ip == 0u) {
        return;
    }
    arp_request(iface, iface->ip);
}

/* Age the ARP cache by one PIT tick. */
void arp_tick(void) {
    uint32_t i;

    for (i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            if (arp_cache[i].ttl > 0u) {
                arp_cache[i].ttl--;
            }
            if (arp_cache[i].ttl == 0u) {
                arp_cache[i].valid = 0;
            }
        }
    }
}

/* Log the current ARP cache contents to serial. */
void arp_print_cache(void) {
    uint32_t i;

    serial_print("[arp] cache\n");
    for (i = 0u; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            continue;
        }
        serial_print("[arp] ip=");
        serial_write_hex(arp_cache[i].ip);
        serial_print(" ttl=");
        serial_write_hex(arp_cache[i].ttl);
        serial_print("\n");
    }
}

/* Parse one Ethernet ARP frame and update the ARP cache. */
void arp_receive(const void *frame, uint16_t len) {
    const uint8_t *bytes = (const uint8_t *)frame;
    const EthHeader *eth;
    const ArpPacket *pkt;
    NetInterface *iface = netif_get(0);
    uint32_t target_ip;

    if (iface == (NetInterface *)0 || frame == (const void *)0
        || len < (uint16_t)(sizeof(EthHeader) + sizeof(ArpPacket))) {
        return;
    }

    eth = (const EthHeader *)frame;
    pkt = (const ArpPacket *)(const void *)(bytes + sizeof(EthHeader));
    if (ntohs(eth->ethertype) != ETH_TYPE_ARP || ntohs(pkt->htype) != 1u
        || ntohs(pkt->ptype) != ETH_TYPE_IP || pkt->hlen != 6u || pkt->plen != 4u) {
        return;
    }

    arp_cache_update(ntohl(pkt->spa), pkt->sha);
    target_ip = ntohl(pkt->tpa);
    if (ntohs(pkt->oper) == 1u && target_ip == iface->ip) {
        arp_reply(iface, pkt->sha, ntohl(pkt->spa));
    }
}

/* Resolve one host-order IPv4 address with a blocking ARP request. */
int arp_resolve(struct NetInterface *iface, uint32_t ip, uint8_t *out_mac) {
    uint32_t start_tick;

    if (iface == (struct NetInterface *)0 || out_mac == (uint8_t *)0) {
        return -1;
    }
    if (arp_lookup(ip, out_mac) == 0) {
        return 0;
    }

    arp_request(iface, ip);
    start_tick = get_ticks();
    while ((get_ticks() - start_tick) < 100u) {
        rtl8139_poll();
        if (arp_lookup(ip, out_mac) == 0) {
            return 0;
        }
    }
    return -1;
}

/* Return one ARP cache entry by index for shell/UI inspection. */
const ArpEntry *arp_get_entry(int idx) {
    if (idx < 0 || (uint32_t)idx >= ARP_CACHE_SIZE) {
        return (const ArpEntry *)0;
    }
    return &arp_cache[idx];
}
