#ifndef DNS_H
#define DNS_H

#include <stdint.h>

#define DNS_PORT        53u
#define DNS_MAX_NAME    256u
#define DNS_TIMEOUT     300u

typedef struct __attribute__((packed)) DnsHeader {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} DnsHeader;

#define DNS_FLAG_QR     (1u << 15)
#define DNS_FLAG_RD     (1u << 8)
#define DNS_FLAG_RA     (1u << 7)

#define DNS_TYPE_A      1u
#define DNS_CLASS_IN    1u

#define DNS_CACHE_SIZE  8u

typedef struct DnsEntry {
    char     name[DNS_MAX_NAME];
    uint32_t ip;
    uint32_t ttl_ticks;
    int      valid;
} DnsEntry;

/* Resolve one hostname to a host-order IPv4 address with caching. */
int dns_resolve(const char *hostname, uint32_t *out_ip);

/* Parse one DNS UDP response and complete any pending query. */
void dns_receive(uint32_t src_ip, uint16_t src_port,
                 const void *data, uint16_t len);

/* Age the DNS cache by one PIT tick. */
void dns_tick(void);

/* Log the current DNS cache to serial. */
void dns_print_cache(void);

/* Return one DNS cache entry by index for shell/UI inspection. */
const DnsEntry *dns_get_entry(int idx);

/* Return the TTL ticks from the last successful DNS response. */
uint32_t dns_last_ttl_ticks(void);

#endif
