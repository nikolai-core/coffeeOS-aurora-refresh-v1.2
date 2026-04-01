#ifndef UDP_H
#define UDP_H

#include <stdint.h>

#include "ip.h"

struct NetInterface;

typedef struct __attribute__((packed)) UdpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} UdpHeader;

#define UDP_MAX_SOCKETS 8u

typedef void (*UdpRecvCallback)(uint32_t src_ip, uint16_t src_port,
                                const void *data, uint16_t len);

typedef struct UdpSocket {
    uint16_t        local_port;
    UdpRecvCallback callback;
    int             open;
} UdpSocket;

/* Open one callback-based UDP socket bound to a local port. */
int udp_open(uint16_t local_port, UdpRecvCallback cb);

/* Close one callback-based UDP socket by index. */
void udp_close(int fd);

/* Send one UDP datagram to a host-order IPv4/port tuple. */
int udp_send(struct NetInterface *iface, uint32_t dst_ip,
             uint16_t src_port, uint16_t dst_port,
             const void *data, uint16_t len);

/* Parse one UDP payload attached to an IPv4 packet. */
void udp_receive(const IpHeader *ip, const void *payload, uint16_t len);

#endif
