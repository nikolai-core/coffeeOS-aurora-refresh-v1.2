#ifndef NETIF_H
#define NETIF_H

#include <stdint.h>

#define NETIF_NAME_MAX  8u
#define NETIF_MAX       2u
#define ETH_MTU         1500u
#define ETH_FRAME_MAX   1514u

typedef struct NetInterface {
    char     name[NETIF_NAME_MAX];
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
    int      up;
    int      dhcp_configured;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    int    (*send)(const void *data, uint16_t len);
} NetInterface;

/* Reset the static network-interface registry. */
void netif_init(void);

/* Register one statically-backed interface with a transmit callback. */
int netif_register(const char *name, const uint8_t *mac,
                   int (*send)(const void *data, uint16_t len));

/* Return one interface slot by index, or null when absent. */
NetInterface *netif_get(int idx);

/* Find one interface by its short name. */
NetInterface *netif_find(const char *name);

/* Return the first interface marked UP, or null when none are configured. */
NetInterface *netif_default(void);

/* Set one interface IPv4 configuration in host byte order. */
void netif_set_ip(int idx, uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns);

/* Mark one interface UP. */
void netif_up(int idx);

/* Mark one interface DOWN. */
void netif_down(int idx);

/* Log one interface's counters to serial. */
void netif_print_stats(int idx);

/* Dispatch one received Ethernet frame into the layer-3 handlers. */
void net_receive(const void *frame, uint16_t len);

/* Return the number of dropped Ethernet frames seen by the dispatcher. */
uint64_t netif_rx_dropped(void);

#endif
