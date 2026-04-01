#ifndef NET_H
#define NET_H

#include <stdint.h>

#include "dns.h"
#include "netif.h"

#define NETMON_LINES     16u
#define NETMON_LINE_LEN  96u

/* Initialize the full network stack and bring up eth0 when DHCP succeeds. */
void net_init(void);

/* Poll the NIC receive path once from the desktop loop. */
void net_poll(void);

/* Age network caches once per PIT tick. */
void net_tick(void);

/* Return non-zero when the default interface is up and configured. */
int net_is_up(void);

/* Append one timestamped message to the network monitor log. */
void netmon_log(const char *msg);

/* Return the number of stored network monitor log lines. */
uint32_t netmon_log_count(void);

/* Return one chronological network monitor log line by index. */
const char *netmon_log_line(uint32_t index);

/* Format one host-order IPv4 address into dotted decimal text. */
void net_format_ip(uint32_t ip, char *out, uint32_t out_len);

/* Parse one dotted-decimal IPv4 string into host byte order. */
int net_parse_ip(const char *text, uint32_t *out_ip);

#endif
