#include <stdint.h>

#include "arp.h"
#include "ascii_util.h"
#include "ethernet.h"
#include "ip.h"
#include "net.h"
#include "netif.h"
#include "serial.h"

static NetInterface netif_table[NETIF_MAX];
static uint64_t rx_dropped_packets;

/* Copy one fixed-size MAC address. */
static void netif_copy_mac(uint8_t *dst, const uint8_t *src) {
    uint32_t i;

    for (i = 0u; i < 6u; i++) {
        dst[i] = src[i];
    }
}

/* Copy one short ASCII interface name. */
static void netif_copy_name(char *dst, const char *src) {
    uint32_t i = 0u;

    while (i + 1u < NETIF_NAME_MAX && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < NETIF_NAME_MAX) {
        dst[i++] = '\0';
    }
}

/* Reset the static network-interface registry. */
void netif_init(void) {
    uint32_t i;

    for (i = 0u; i < NETIF_MAX; i++) {
        netif_table[i].name[0] = '\0';
        netif_table[i].up = 0;
        netif_table[i].dhcp_configured = 0;
        netif_table[i].send = (int (*)(const void *, uint16_t))0;
    }
    rx_dropped_packets = 0u;
}

/* Register one statically-backed interface with a transmit callback. */
int netif_register(const char *name, const uint8_t *mac,
                   int (*send)(const void *data, uint16_t len)) {
    uint32_t i;

    if (name == (const char *)0 || mac == (const uint8_t *)0 || send == (int (*)(const void *, uint16_t))0) {
        return -1;
    }
    for (i = 0u; i < NETIF_MAX; i++) {
        if (netif_table[i].send == (int (*)(const void *, uint16_t))0) {
            netif_copy_name(netif_table[i].name, name);
            netif_copy_mac(netif_table[i].mac, mac);
            netif_table[i].ip = 0u;
            netif_table[i].netmask = 0u;
            netif_table[i].gateway = 0u;
            netif_table[i].dns = 0u;
            netif_table[i].rx_packets = 0u;
            netif_table[i].tx_packets = 0u;
            netif_table[i].rx_bytes = 0u;
            netif_table[i].tx_bytes = 0u;
            netif_table[i].up = 0;
            netif_table[i].dhcp_configured = 0;
            netif_table[i].send = send;
            return (int)i;
        }
    }
    return -1;
}

/* Return one interface slot by index, or null when absent. */
NetInterface *netif_get(int idx) {
    if (idx < 0 || (uint32_t)idx >= NETIF_MAX || netif_table[idx].send == (int (*)(const void *, uint16_t))0) {
        return (NetInterface *)0;
    }
    return &netif_table[idx];
}

/* Find one interface by its short name. */
NetInterface *netif_find(const char *name) {
    uint32_t i;

    if (name == (const char *)0) {
        return (NetInterface *)0;
    }
    for (i = 0u; i < NETIF_MAX; i++) {
        if (netif_table[i].send != (int (*)(const void *, uint16_t))0
            && ascii_streq(netif_table[i].name, name)) {
            return &netif_table[i];
        }
    }
    return (NetInterface *)0;
}

/* Return the first interface marked UP, or null when none are configured. */
NetInterface *netif_default(void) {
    uint32_t i;

    for (i = 0u; i < NETIF_MAX; i++) {
        if (netif_table[i].send != (int (*)(const void *, uint16_t))0 && netif_table[i].up) {
            return &netif_table[i];
        }
    }
    return (NetInterface *)0;
}

/* Set one interface IPv4 configuration in host byte order. */
void netif_set_ip(int idx, uint32_t ip, uint32_t mask, uint32_t gw, uint32_t dns) {
    NetInterface *iface = netif_get(idx);

    if (iface == (NetInterface *)0) {
        return;
    }
    iface->ip = ip;
    iface->netmask = mask;
    iface->gateway = gw;
    iface->dns = dns;
    iface->dhcp_configured = 1;
}

/* Mark one interface UP. */
void netif_up(int idx) {
    NetInterface *iface = netif_get(idx);

    if (iface != (NetInterface *)0) {
        iface->up = 1;
    }
}

/* Mark one interface DOWN. */
void netif_down(int idx) {
    NetInterface *iface = netif_get(idx);

    if (iface != (NetInterface *)0) {
        iface->up = 0;
    }
}

/* Log one interface's counters to serial. */
void netif_print_stats(int idx) {
    NetInterface *iface = netif_get(idx);

    if (iface == (NetInterface *)0) {
        return;
    }
    serial_print("[netif] ");
    serial_print(iface->name);
    serial_print(" rx=");
    serial_write_hex((uint32_t)iface->rx_packets);
    serial_print(" tx=");
    serial_write_hex((uint32_t)iface->tx_packets);
    serial_print("\n");
}

/* Dispatch one received Ethernet frame into the layer-3 handlers. */
void net_receive(const void *frame, uint16_t len) {
    const EthHeader *eth;
    uint16_t ethertype;
    NetInterface *iface = netif_get(0);

    if (iface != (NetInterface *)0) {
        iface->rx_packets++;
        iface->rx_bytes += len;
    }
    if (frame == (const void *)0 || len < sizeof(EthHeader)) {
        rx_dropped_packets++;
        return;
    }

    eth = (const EthHeader *)frame;
    ethertype = ntohs(eth->ethertype);
    if (ethertype == ETH_TYPE_IP) {
        ip_receive(frame, len);
        return;
    }
    if (ethertype == ETH_TYPE_ARP) {
        arp_receive(frame, len);
        return;
    }

    rx_dropped_packets++;
}

/* Return the number of dropped Ethernet frames seen by the dispatcher. */
uint64_t netif_rx_dropped(void) {
    return rx_dropped_packets;
}
