#ifndef ARP_H
#define ARP_H

#include <stdint.h>

struct NetInterface;

typedef struct __attribute__((packed)) ArpPacket {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} ArpPacket;

#define ARP_CACHE_SIZE 16u

typedef struct ArpEntry {
    uint32_t ip;
    uint8_t  mac[6];
    uint32_t ttl;
    int      valid;
} ArpEntry;

/* Reset the static ARP cache. */
void arp_init(void);

/* Parse one Ethernet ARP frame and update the ARP cache. */
void arp_receive(const void *frame, uint16_t len);

/* Look up one host-order IPv4 address in the cache. */
int arp_lookup(uint32_t ip, uint8_t *out_mac);

/* Broadcast one ARP request for a target IPv4 address. */
void arp_request(struct NetInterface *iface, uint32_t target_ip);

/* Send one ARP reply for a target host-order IPv4 address and MAC. */
void arp_reply(struct NetInterface *iface, const uint8_t *dst_mac, uint32_t dst_ip);

/* Send one gratuitous ARP for the interface's configured IPv4 address. */
void arp_announce(struct NetInterface *iface);

/* Age the ARP cache by one PIT tick. */
void arp_tick(void);

/* Log the current ARP cache contents to serial. */
void arp_print_cache(void);

/* Resolve one host-order IPv4 address with a blocking ARP request. */
int arp_resolve(struct NetInterface *iface, uint32_t ip, uint8_t *out_mac);

/* Return one ARP cache entry by index for shell/UI inspection. */
const ArpEntry *arp_get_entry(int idx);

#endif
