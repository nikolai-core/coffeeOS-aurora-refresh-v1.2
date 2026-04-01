#include <stdint.h>

#include "ethernet.h"
#include "net.h"
#include "netif.h"
#include "pit.h"
#include "rtl8139.h"
#include "tcp.h"

typedef struct __attribute__((packed)) TcpPseudoHeader {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t tcp_len;
} TcpPseudoHeader;

static TcpSocket tcp_sockets[TCP_MAX_SOCKETS];
static uint16_t tcp_next_port = 40000u;
static uint8_t tcp_tx_buf[sizeof(TcpPseudoHeader) + sizeof(TcpHeader) + TCP_MSS];

/* Return the next ephemeral TCP local port. */
static uint16_t tcp_alloc_port(void) {
    return tcp_next_port++;
}

/* Compute one TCP pseudo-header checksum over header and payload bytes. */
static uint16_t tcp_checksum(const IpHeader *ip, const void *segment, uint16_t len) {
    TcpPseudoHeader *ph = (TcpPseudoHeader *)(void *)tcp_tx_buf;
    uint8_t *dst = tcp_tx_buf + sizeof(TcpPseudoHeader);
    uint16_t i;

    ph->src_ip = ip->src_ip;
    ph->dst_ip = ip->dst_ip;
    ph->zero = 0u;
    ph->protocol = IP_PROTO_TCP;
    ph->tcp_len = htons(len);
    for (i = 0u; i < len; i++) {
        dst[i] = ((const uint8_t *)segment)[i];
    }
    return ip_checksum(tcp_tx_buf, (uint16_t)(sizeof(TcpPseudoHeader) + len));
}

/* Build one outbound TCP segment from socket state and payload bytes. */
static int tcp_send_segment(TcpSocket *sock, uint8_t flags, const void *data, uint16_t len) {
    uint8_t packet[sizeof(TcpHeader) + TCP_MSS];
    TcpHeader *tcp = (TcpHeader *)(void *)packet;
    IpHeader ip;
    NetInterface *iface = netif_default();
    uint16_t i;

    if (sock == (TcpSocket *)0 || iface == (NetInterface *)0 || len > TCP_MSS) {
        return -1;
    }

    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq = htonl(sock->seq_send);
    tcp->ack = htonl(sock->seq_recv);
    tcp->data_offset = (uint8_t)(sizeof(TcpHeader) / 4u) << 4;
    tcp->flags = flags;
    tcp->window = htons(TCP_WINDOW);
    tcp->checksum = 0u;
    tcp->urgent = 0u;
    for (i = 0u; i < len; i++) {
        packet[sizeof(TcpHeader) + i] = ((const uint8_t *)data)[i];
    }

    ip.src_ip = htonl(sock->local_ip);
    ip.dst_ip = htonl(sock->remote_ip);
    tcp->checksum = htons(tcp_checksum(&ip, packet, (uint16_t)(sizeof(TcpHeader) + len)));
    if (ip_send(iface, sock->remote_ip, IP_PROTO_TCP, packet, (uint16_t)(sizeof(TcpHeader) + len)) != 0) {
        return -1;
    }
    if ((flags & TCP_SYN) != 0u || (flags & TCP_FIN) != 0u) {
        sock->seq_send++;
    }
    sock->seq_send += len;
    return 0;
}

/* Append one incoming TCP payload into the socket receive ring. */
static void tcp_rx_push(TcpSocket *sock, const uint8_t *data, uint16_t len) {
    uint16_t i;

    for (i = 0u; i < len; i++) {
        uint32_t next = (sock->rx_head + 1u) % TCP_RX_BUF_SIZE;

        if (next == sock->rx_tail) {
            break;
        }
        sock->rx_buf[sock->rx_head] = data[i];
        sock->rx_head = next;
    }
}

/* Find one open TCP socket matching a four-tuple. */
static int tcp_find_socket(uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    uint32_t i;

    for (i = 0u; i < TCP_MAX_SOCKETS; i++) {
        if (tcp_sockets[i].open
            && tcp_sockets[i].remote_ip == src_ip
            && tcp_sockets[i].remote_port == src_port
            && tcp_sockets[i].local_port == dst_port) {
            return (int)i;
        }
    }
    return -1;
}

/* Open one blocking outbound TCP connection and wait for ESTABLISHED. */
int tcp_connect(struct NetInterface *iface, uint32_t dst_ip, uint16_t dst_port,
                uint32_t timeout_ticks) {
    uint32_t i;
    uint32_t start_tick;

    if (iface == (struct NetInterface *)0) {
        return -1;
    }

    for (i = 0u; i < TCP_MAX_SOCKETS; i++) {
        if (!tcp_sockets[i].open) {
            tcp_sockets[i].open = 1;
            tcp_sockets[i].state = TCP_SYN_SENT;
            tcp_sockets[i].local_ip = iface->ip;
            tcp_sockets[i].local_port = tcp_alloc_port();
            tcp_sockets[i].remote_ip = dst_ip;
            tcp_sockets[i].remote_port = dst_port;
            tcp_sockets[i].seq_send = get_ticks();
            tcp_sockets[i].seq_recv = 0u;
            tcp_sockets[i].rx_head = 0u;
            tcp_sockets[i].rx_tail = 0u;
            if (tcp_send_segment(&tcp_sockets[i], TCP_SYN, (const void *)0, 0u) != 0) {
                tcp_sockets[i].open = 0;
                tcp_sockets[i].state = TCP_CLOSED;
                return -1;
            }

            start_tick = get_ticks();
            while ((get_ticks() - start_tick) < timeout_ticks) {
                rtl8139_poll();
                if (tcp_sockets[i].state == TCP_ESTABLISHED) {
                    netmon_log("TCP connected");
                    return (int)i;
                }
            }

            tcp_sockets[i].open = 0;
            tcp_sockets[i].state = TCP_CLOSED;
            return -1;
        }
    }
    return -1;
}

/* Close one TCP socket by index. */
void tcp_close(int fd) {
    if (fd < 0 || (uint32_t)fd >= TCP_MAX_SOCKETS || !tcp_sockets[fd].open) {
        return;
    }
    if (tcp_sockets[fd].state == TCP_ESTABLISHED) {
        /* Free the client socket immediately after sending FIN; this stack does not keep a scheduler-driven close state machine. */
        (void)tcp_send_segment(&tcp_sockets[fd], (uint8_t)(TCP_FIN | TCP_ACK), (const void *)0, 0u);
    }
    tcp_sockets[fd].open = 0;
    tcp_sockets[fd].state = TCP_CLOSED;
    tcp_sockets[fd].local_ip = 0u;
    tcp_sockets[fd].local_port = 0u;
    tcp_sockets[fd].remote_ip = 0u;
    tcp_sockets[fd].remote_port = 0u;
    tcp_sockets[fd].seq_send = 0u;
    tcp_sockets[fd].seq_recv = 0u;
    tcp_sockets[fd].rx_head = 0u;
    tcp_sockets[fd].rx_tail = 0u;
}

/* Send one TCP PSH+ACK segment from an established socket. */
int tcp_send(int fd, const void *data, uint16_t len) {
    if (fd < 0 || (uint32_t)fd >= TCP_MAX_SOCKETS || !tcp_sockets[fd].open
        || tcp_sockets[fd].state != TCP_ESTABLISHED || data == (const void *)0) {
        return -1;
    }
    if (len > TCP_MSS) {
        len = TCP_MSS;
    }
    return tcp_send_segment(&tcp_sockets[fd], (uint8_t)(TCP_PSH | TCP_ACK), data, len) == 0 ? (int)len : -1;
}

/* Receive bytes from one TCP socket into a caller buffer with a timeout. */
int tcp_recv(int fd, void *buf, uint16_t max_len, uint32_t timeout_ticks) {
    TcpSocket *sock;
    uint32_t start_tick;
    uint16_t count = 0u;

    if (fd < 0 || (uint32_t)fd >= TCP_MAX_SOCKETS || buf == (void *)0) {
        return -1;
    }
    sock = &tcp_sockets[fd];
    if (!sock->open) {
        return -1;
    }

    start_tick = get_ticks();
    while ((get_ticks() - start_tick) < timeout_ticks) {
        while (sock->rx_tail != sock->rx_head && count < max_len) {
            ((uint8_t *)buf)[count++] = sock->rx_buf[sock->rx_tail];
            sock->rx_tail = (sock->rx_tail + 1u) % TCP_RX_BUF_SIZE;
        }
        if (count != 0u) {
            return (int)count;
        }
        if (!sock->open || sock->state == TCP_CLOSED
            || sock->state == TCP_TIME_WAIT || sock->state == TCP_CLOSE_WAIT) {
            return 0;
        }
        rtl8139_poll();
    }
    return 0;
}

/* Parse one TCP payload attached to an IPv4 packet. */
void tcp_receive(const IpHeader *ip, const void *payload, uint16_t len) {
    const TcpHeader *tcp = (const TcpHeader *)payload;
    uint16_t hdr_len;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    int fd;
    TcpSocket *sock;
    uint16_t data_len;
    const uint8_t *data;

    if (ip == (const IpHeader *)0 || payload == (const void *)0 || len < sizeof(TcpHeader)) {
        return;
    }
    hdr_len = (uint16_t)(((tcp->data_offset >> 4) & 0x0Fu) * 4u);
    if (hdr_len < sizeof(TcpHeader) || hdr_len > len) {
        return;
    }
    if (tcp_checksum(ip, payload, len) != 0u) {
        return;
    }

    src_port = ntohs(tcp->src_port);
    dst_port = ntohs(tcp->dst_port);
    seq = ntohl(tcp->seq);
    ack = ntohl(tcp->ack);
    fd = tcp_find_socket(ntohl(ip->src_ip), src_port, dst_port);
    if (fd < 0) {
        return;
    }

    sock = &tcp_sockets[fd];
    data = (const uint8_t *)payload + hdr_len;
    data_len = (uint16_t)(len - hdr_len);

    if ((tcp->flags & TCP_RST) != 0u) {
        sock->state = TCP_CLOSED;
        sock->open = 0;
        return;
    }

    if (sock->state == TCP_SYN_SENT) {
        if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && ack == sock->seq_send) {
            sock->seq_recv = seq + 1u;
            (void)tcp_send_segment(sock, TCP_ACK, (const void *)0, 0u);
            sock->state = TCP_ESTABLISHED;
        }
        return;
    }

    if (data_len != 0u && seq == sock->seq_recv) {
        tcp_rx_push(sock, data, data_len);
        sock->seq_recv += data_len;
        (void)tcp_send_segment(sock, TCP_ACK, (const void *)0, 0u);
    }

    if ((tcp->flags & TCP_FIN) != 0u) {
        sock->seq_recv++;
        if (sock->state == TCP_FIN_WAIT1) {
            sock->state = TCP_FIN_WAIT2;
        } else if (sock->state == TCP_FIN_WAIT2) {
            sock->state = TCP_TIME_WAIT;
        } else if (sock->state == TCP_ESTABLISHED) {
            sock->state = TCP_CLOSE_WAIT;
        }
        (void)tcp_send_segment(sock, TCP_ACK, (const void *)0, 0u);
        if (sock->state == TCP_CLOSE_WAIT) {
            (void)tcp_send_segment(sock, (uint8_t)(TCP_FIN | TCP_ACK), (const void *)0, 0u);
        }
    }
}

/* Return non-zero when one TCP socket is established. */
int tcp_ready(int fd) {
    if (fd < 0 || (uint32_t)fd >= TCP_MAX_SOCKETS || !tcp_sockets[fd].open) {
        return 0;
    }
    return tcp_sockets[fd].state == TCP_ESTABLISHED;
}

/* Return one TCP socket by index for shell/UI inspection. */
const TcpSocket *tcp_get_socket(int fd) {
    if (fd < 0 || (uint32_t)fd >= TCP_MAX_SOCKETS || !tcp_sockets[fd].open) {
        return (const TcpSocket *)0;
    }
    return &tcp_sockets[fd];
}

/* Return a short printable name for one TCP state value. */
const char *tcp_state_name(TcpState state) {
    if (state == TCP_CLOSED) return "CLOSED";
    if (state == TCP_SYN_SENT) return "SYN_SENT";
    if (state == TCP_ESTABLISHED) return "ESTABLISHED";
    if (state == TCP_FIN_WAIT1) return "FIN_WAIT1";
    if (state == TCP_FIN_WAIT2) return "FIN_WAIT2";
    if (state == TCP_TIME_WAIT) return "TIME_WAIT";
    if (state == TCP_CLOSE_WAIT) return "CLOSE_WAIT";
    return "UNKNOWN";
}
