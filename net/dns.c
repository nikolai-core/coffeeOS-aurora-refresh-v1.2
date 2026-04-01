#include <stdint.h>

#include "ascii_util.h"
#include "dns.h"
#include "ethernet.h"
#include "net.h"
#include "netif.h"
#include "pit.h"
#include "rtl8139.h"
#include "serial.h"
#include "udp.h"

static DnsEntry dns_cache[DNS_CACHE_SIZE];
static uint16_t dns_pending_id;
static uint32_t dns_received_ip;
static uint32_t dns_received_ttl;
static int dns_pending_ok;
static int dns_udp_fd = -1;

/* Copy one short hostname into a fixed DNS cache slot. */
static void dns_copy_name(char *dst, const char *src) {
    uint32_t i = 0u;

    while (i + 1u < DNS_MAX_NAME && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Return non-zero when one string is a dotted-decimal IPv4 literal. */
static int dns_parse_literal(const char *hostname, uint32_t *out_ip) {
    return net_parse_ip(hostname, out_ip);
}

/* Skip one DNS name that may use label compression. */
static int dns_skip_name(const uint8_t *data, uint16_t len, uint16_t *pos) {
    while (*pos < len) {
        uint8_t count = data[*pos];

        if (count == 0u) {
            (*pos)++;
            return 0;
        }
        if ((count & 0xC0u) == 0xC0u) {
            *pos = (uint16_t)(*pos + 2u);
            return 0;
        }
        *pos = (uint16_t)(*pos + 1u + count);
    }
    return -1;
}

/* Parse one DNS UDP response and complete any pending query. */
void dns_receive(uint32_t src_ip, uint16_t src_port,
                 const void *data, uint16_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    const DnsHeader *hdr = (const DnsHeader *)data;
    uint16_t pos;
    uint16_t ancount;
    uint16_t i;

    (void)src_ip;
    if (src_port != DNS_PORT || data == (const void *)0 || len < sizeof(DnsHeader)) {
        return;
    }
    if ((ntohs(hdr->flags) & DNS_FLAG_QR) == 0u || ntohs(hdr->id) != dns_pending_id) {
        return;
    }

    pos = (uint16_t)sizeof(DnsHeader);
    if (dns_skip_name(bytes, len, &pos) != 0 || pos + 4u > len) {
        return;
    }
    pos = (uint16_t)(pos + 4u);

    ancount = ntohs(hdr->ancount);
    for (i = 0u; i < ancount && pos < len; i++) {
        uint16_t type;
        uint16_t class_id;
        uint16_t rdlen;

        if (dns_skip_name(bytes, len, &pos) != 0 || pos + 10u > len) {
            return;
        }
        type = ntohs(*(const uint16_t *)(const void *)(bytes + pos));
        class_id = ntohs(*(const uint16_t *)(const void *)(bytes + pos + 2u));
        dns_received_ttl = ntohl(*(const uint32_t *)(const void *)(bytes + pos + 4u));
        rdlen = ntohs(*(const uint16_t *)(const void *)(bytes + pos + 8u));
        pos = (uint16_t)(pos + 10u);
        if (pos + rdlen > len) {
            return;
        }
        if (type == DNS_TYPE_A && class_id == DNS_CLASS_IN && rdlen == 4u) {
            dns_received_ip = ntohl(*(const uint32_t *)(const void *)(bytes + pos));
            dns_pending_ok = 1;
            return;
        }
        pos = (uint16_t)(pos + rdlen);
    }
}

/* Resolve one hostname to a host-order IPv4 address with caching. */
int dns_resolve(const char *hostname, uint32_t *out_ip) {
    uint8_t packet[512];
    DnsHeader *hdr = (DnsHeader *)(void *)packet;
    NetInterface *iface;
    uint16_t pos;
    uint32_t i;
    uint32_t start_tick;
    uint16_t local_port;

    if (hostname == (const char *)0 || out_ip == (uint32_t *)0) {
        return -1;
    }

    for (i = 0u; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid && ascii_streq(dns_cache[i].name, hostname)) {
            *out_ip = dns_cache[i].ip;
            return 0;
        }
    }
    if (dns_parse_literal(hostname, out_ip)) {
        return 0;
    }

    iface = netif_default();
    if (iface == (NetInterface *)0 || iface->dns == 0u) {
        return -1;
    }

    for (i = 0u; i < sizeof(packet); i++) {
        packet[i] = 0u;
    }
    dns_pending_id = (uint16_t)(get_ticks() & 0xFFFFu);
    dns_received_ip = 0u;
    dns_received_ttl = 0u;
    dns_pending_ok = 0;

    hdr->id = htons(dns_pending_id);
    hdr->flags = htons(DNS_FLAG_RD);
    hdr->qdcount = htons(1u);
    pos = (uint16_t)sizeof(DnsHeader);
    {
        uint32_t start = 0u;
        uint32_t end = 0u;

        while (1) {
            while (hostname[end] != '\0' && hostname[end] != '.') {
                end++;
            }
            packet[pos++] = (uint8_t)(end - start);
            while (start < end) {
                packet[pos++] = (uint8_t)hostname[start++];
            }
            if (hostname[end] == '\0') {
                break;
            }
            end++;
            start = end;
        }
    }
    packet[pos++] = 0u;
    *(uint16_t *)(void *)(packet + pos) = htons(DNS_TYPE_A);
    pos = (uint16_t)(pos + 2u);
    *(uint16_t *)(void *)(packet + pos) = htons(DNS_CLASS_IN);
    pos = (uint16_t)(pos + 2u);

    local_port = (uint16_t)(1024u + (get_ticks() & 0x3FFFu));
    dns_udp_fd = udp_open(local_port, dns_receive);
    if (dns_udp_fd < 0) {
        return -1;
    }
    if (udp_send(iface, iface->dns, local_port, DNS_PORT, packet, pos) != 0) {
        udp_close(dns_udp_fd);
        dns_udp_fd = -1;
        return -1;
    }

    start_tick = get_ticks();
    while ((get_ticks() - start_tick) < DNS_TIMEOUT) {
        rtl8139_poll();
        if (dns_pending_ok) {
            break;
        }
    }

    udp_close(dns_udp_fd);
    dns_udp_fd = -1;
    if (!dns_pending_ok) {
        return -1;
    }

    *out_ip = dns_received_ip;
    for (i = 0u; i < DNS_CACHE_SIZE; i++) {
        if (!dns_cache[i].valid) {
            dns_cache[i].valid = 1;
            dns_copy_name(dns_cache[i].name, hostname);
            dns_cache[i].ip = dns_received_ip;
            dns_cache[i].ttl_ticks = dns_received_ttl * 100u;
            break;
        }
    }
    netmon_log("DNS query OK");
    return 0;
}

/* Age the DNS cache by one PIT tick. */
void dns_tick(void) {
    uint32_t i;

    for (i = 0u; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid) {
            if (dns_cache[i].ttl_ticks > 0u) {
                dns_cache[i].ttl_ticks--;
            }
            if (dns_cache[i].ttl_ticks == 0u) {
                dns_cache[i].valid = 0;
            }
        }
    }
}

/* Log the current DNS cache to serial. */
void dns_print_cache(void) {
    uint32_t i;

    serial_print("[dns] cache\n");
    for (i = 0u; i < DNS_CACHE_SIZE; i++) {
        if (dns_cache[i].valid) {
            serial_print("[dns] ");
            serial_print(dns_cache[i].name);
            serial_print("\n");
        }
    }
}

/* Return one DNS cache entry by index for shell/UI inspection. */
const DnsEntry *dns_get_entry(int idx) {
    if (idx < 0 || (uint32_t)idx >= DNS_CACHE_SIZE) {
        return (const DnsEntry *)0;
    }
    return &dns_cache[idx];
}

/* Return the TTL ticks from the last successful DNS response. */
uint32_t dns_last_ttl_ticks(void) {
    return dns_received_ttl * 100u;
}
