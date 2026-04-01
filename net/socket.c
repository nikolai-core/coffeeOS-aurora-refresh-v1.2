#include <stdint.h>

#include "net/stack.h"

static NetSocket net_sockets[NET_SOCKET_COUNT];
static SpinLock net_socket_table_lock;
static uint16_t net_next_ephemeral_port = 49152u;

static void net_socket_reset(NetSocket *sock) {
    net_memset(sock, 0u, sizeof(*sock));
    sock->state = NET_SOCK_CLOSED;
    sock->udp_rx_head = NET_QUEUE_NONE;
    sock->udp_rx_tail = NET_QUEUE_NONE;
    sock->rto_ticks = 50u;
    sock->ssthresh = NET_TCP_TX_BUFFER;
    sock->parent = 0xFFu;
}

static int net_socket_port_in_use(uint16_t port, uint8_t type) {
    uint32_t i;

    for (i = 0u; i < NET_SOCKET_COUNT; i++) {
        if (!net_sockets[i].in_use) {
            continue;
        }
        if (net_sockets[i].type == type && net_sockets[i].local_port == port
            && net_sockets[i].state != NET_SOCK_CLOSED) {
            return 1;
        }
    }
    return 0;
}

void net_socket_init(void) {
    uint32_t i;

    for (i = 0u; i < NET_SOCKET_COUNT; i++) {
        net_socket_reset(&net_sockets[i]);
    }
    net_socket_table_lock.value = 0u;
}

NetSocket *net_socket_get(int fd) {
    if (fd < 0 || (uint32_t)fd >= NET_SOCKET_COUNT || !net_sockets[fd].in_use) {
        return (NetSocket *)0;
    }
    return &net_sockets[fd];
}

uint16_t net_socket_alloc_ephemeral_port(void) {
    uint32_t attempt;
    uint32_t flags = spin_lock_irqsave(&net_socket_table_lock);

    for (attempt = 0u; attempt < 16384u; attempt++) {
        uint16_t port = net_next_ephemeral_port++;

        if (net_next_ephemeral_port < 49152u) {
            net_next_ephemeral_port = 49152u;
        }
        if (!net_socket_port_in_use(port, SOCK_STREAM) && !net_socket_port_in_use(port, SOCK_DGRAM)) {
            spin_unlock_irqrestore(&net_socket_table_lock, flags);
            return port;
        }
    }
    spin_unlock_irqrestore(&net_socket_table_lock, flags);
    return 0u;
}

int net_socket_create(int domain, int type, int protocol) {
    uint32_t i;
    uint32_t flags;

    if (domain != AF_INET) {
        return -1;
    }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        return -1;
    }
    if (protocol != 0 && ((type == SOCK_STREAM && protocol != NET_IP_PROTO_TCP)
        || (type == SOCK_DGRAM && protocol != NET_IP_PROTO_UDP))) {
        return -1;
    }

    flags = spin_lock_irqsave(&net_socket_table_lock);
    for (i = 0u; i < NET_SOCKET_COUNT; i++) {
        if (!net_sockets[i].in_use) {
            net_socket_reset(&net_sockets[i]);
            net_sockets[i].in_use = 1u;
            net_sockets[i].type = (uint8_t)type;
            net_sockets[i].protocol = (uint8_t)(type == SOCK_STREAM ? NET_IP_PROTO_TCP : NET_IP_PROTO_UDP);
            net_sockets[i].local_addr = net_if_config()->addr;
            net_sockets[i].cwnd = NET_TCP_MSS;
            spin_unlock_irqrestore(&net_socket_table_lock, flags);
            return (int)i;
        }
    }
    spin_unlock_irqrestore(&net_socket_table_lock, flags);
    return -1;
}

int net_socket_bind(int fd, const NetSockAddrIn *addr) {
    NetSocket *sock = net_socket_get(fd);
    uint16_t port;
    uint32_t flags;

    if (sock == (NetSocket *)0 || addr == (const NetSockAddrIn *)0 || addr->family != AF_INET) {
        return -1;
    }

    port = ntohs(addr->port);
    if (port == 0u) {
        port = net_socket_alloc_ephemeral_port();
        if (port == 0u) {
            return -1;
        }
    }

    flags = spin_lock_irqsave(&net_socket_table_lock);
    if (net_socket_port_in_use(port, sock->type)) {
        spin_unlock_irqrestore(&net_socket_table_lock, flags);
        return -1;
    }
    sock->local_port = port;
    sock->local_addr = addr->addr != 0u ? ntohl(addr->addr) : net_if_config()->addr;
    spin_unlock_irqrestore(&net_socket_table_lock, flags);
    return 0;
}

int net_socket_listen(int fd, int backlog) {
    NetSocket *sock = net_socket_get(fd);
    uint32_t flags;

    if (sock == (NetSocket *)0 || sock->type != SOCK_STREAM) {
        return -1;
    }
    if (sock->local_port == 0u) {
        sock->local_port = net_socket_alloc_ephemeral_port();
        if (sock->local_port == 0u) {
            return -1;
        }
    }

    flags = spin_lock_irqsave(&sock->lock);
    sock->state = NET_SOCK_LISTEN;
    sock->backlog_limit = (uint8_t)((backlog <= 0) ? 1 : (backlog > (int)NET_TCP_BACKLOG ? NET_TCP_BACKLOG : backlog));
    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}

static int net_socket_backlog_pop(NetSocket *listener) {
    int child_fd;

    if (listener->backlog_count == 0u) {
        return -1;
    }
    child_fd = (int)listener->backlog[listener->backlog_head];
    listener->backlog_head = (uint8_t)((listener->backlog_head + 1u) % NET_TCP_BACKLOG);
    listener->backlog_count--;
    return child_fd;
}

int net_socket_accept(int fd, NetSockAddrIn *addr) {
    NetSocket *listener = net_socket_get(fd);
    NetSocket *child;
    int child_fd;
    uint32_t flags;

    if (listener == (NetSocket *)0 || listener->state != NET_SOCK_LISTEN) {
        return -1;
    }

    flags = spin_lock_irqsave(&listener->lock);
    child_fd = net_socket_backlog_pop(listener);
    spin_unlock_irqrestore(&listener->lock, flags);
    if (child_fd < 0) {
        return -1;
    }

    child = net_socket_get(child_fd);
    if (child == (NetSocket *)0) {
        return -1;
    }
    if (addr != (NetSockAddrIn *)0) {
        addr->family = AF_INET;
        addr->port = htons(child->remote_port);
        addr->addr = htonl(child->remote_addr);
    }
    return child_fd;
}

int net_socket_connect(int fd, const NetSockAddrIn *addr) {
    NetSocket *sock = net_socket_get(fd);
    uint32_t flags;

    if (sock == (NetSocket *)0 || addr == (const NetSockAddrIn *)0 || addr->family != AF_INET) {
        return -1;
    }
    if (sock->type == SOCK_DGRAM) {
        sock->remote_addr = ntohl(addr->addr);
        sock->remote_port = ntohs(addr->port);
        if (sock->local_port == 0u) {
            sock->local_port = net_socket_alloc_ephemeral_port();
        }
        return (sock->local_port != 0u) ? 0 : -1;
    }
    if (sock->type != SOCK_STREAM || sock->state != NET_SOCK_CLOSED) {
        return -1;
    }

    if (sock->local_port == 0u) {
        sock->local_port = net_socket_alloc_ephemeral_port();
        if (sock->local_port == 0u) {
            return -1;
        }
    }

    flags = spin_lock_irqsave(&sock->lock);
    sock->remote_addr = ntohl(addr->addr);
    sock->remote_port = ntohs(addr->port);
    sock->state = NET_SOCK_SYN_SENT;
    sock->iss = (net_now_ticks() << 8) ^ ((uint32_t)fd << 16) ^ sock->remote_addr;
    sock->snd_una = sock->iss;
    sock->snd_nxt = sock->iss;
    sock->rcv_nxt = 0u;
    sock->cwnd = NET_TCP_MSS;
    sock->ssthresh = NET_TCP_TX_BUFFER;
    spin_unlock_irqrestore(&sock->lock, flags);

    return tcp_send_ack(sock, NET_TCP_SYN, (const void *)0, 0u);
}

void net_socket_queue_udp(NetSocket *sock, int packet_index) {
    uint32_t flags;

    if (sock == (NetSocket *)0 || packet_index < 0) {
        return;
    }
    flags = spin_lock_irqsave(&sock->lock);
    if (sock->udp_rx_count >= NET_UDP_RX_QUEUE) {
        int old = net_queue_pop(&sock->udp_rx_head, &sock->udp_rx_tail);
        if (old >= 0) {
            sock->udp_rx_count--;
            net_packet_free(old);
        }
    }
    net_queue_push(&sock->udp_rx_head, &sock->udp_rx_tail, (uint16_t)packet_index);
    sock->udp_rx_count++;
    spin_unlock_irqrestore(&sock->lock, flags);
}

static int net_socket_recv_udp(NetSocket *sock, void *buf, uint32_t len, NetSockAddrIn *addr) {
    int packet_index;
    NetPacketBuffer *packet;
    uint32_t flags;
    uint32_t copy_len;

    flags = spin_lock_irqsave(&sock->lock);
    packet_index = net_queue_pop(&sock->udp_rx_head, &sock->udp_rx_tail);
    if (packet_index >= 0 && sock->udp_rx_count != 0u) {
        sock->udp_rx_count--;
    }
    spin_unlock_irqrestore(&sock->lock, flags);
    if (packet_index < 0) {
        return -1;
    }

    packet = net_packet_get(packet_index);
    if (packet == (NetPacketBuffer *)0) {
        return -1;
    }

    copy_len = packet->len < len ? packet->len : len;
    net_memcpy(buf, packet->data, copy_len);
    if (addr != (NetSockAddrIn *)0) {
        addr->family = AF_INET;
        addr->port = htons(packet->src_port);
        addr->addr = htonl(packet->src_addr);
    }
    net_packet_free(packet_index);
    return (int)copy_len;
}

static int net_socket_recv_tcp(NetSocket *sock, void *buf, uint32_t len) {
    uint32_t flags;
    uint32_t i;
    uint32_t copy_len;

    flags = spin_lock_irqsave(&sock->lock);
    if (sock->rx_len == 0u) {
        spin_unlock_irqrestore(&sock->lock, flags);
        return sock->remote_closed ? 0 : -1;
    }

    copy_len = sock->rx_len < len ? sock->rx_len : len;
    for (i = 0u; i < copy_len; i++) {
        ((uint8_t *)buf)[i] = sock->rx_data[(sock->rx_head + i) % NET_TCP_RX_BUFFER];
    }
    sock->rx_head = (uint16_t)((sock->rx_head + copy_len) % NET_TCP_RX_BUFFER);
    sock->rx_len = (uint16_t)(sock->rx_len - copy_len);
    spin_unlock_irqrestore(&sock->lock, flags);
    return (int)copy_len;
}

int net_socket_send(int fd, const void *buf, uint32_t len, uint32_t flags) {
    NetSocket *sock = net_socket_get(fd);

    (void)flags;
    if (sock == (NetSocket *)0 || buf == (const void *)0) {
        return -1;
    }
    if (sock->type == SOCK_DGRAM) {
        if (sock->remote_port == 0u || sock->remote_addr == 0u) {
            return -1;
        }
        if (sock->local_port == 0u) {
            sock->local_port = net_socket_alloc_ephemeral_port();
            if (sock->local_port == 0u) {
                return -1;
            }
        }
        return udp_send(sock->local_addr, sock->local_port, sock->remote_addr, sock->remote_port,
                        buf, (uint16_t)(len > 1472u ? 1472u : len));
    }
    if (sock->type != SOCK_STREAM || sock->state != NET_SOCK_ESTABLISHED) {
        return -1;
    }
    return tcp_send_ack(sock, (uint8_t)(NET_TCP_ACK | NET_TCP_PSH), buf,
                        (uint16_t)(len > NET_TCP_MSS ? NET_TCP_MSS : len));
}

int net_socket_recv(int fd, void *buf, uint32_t len, uint32_t flags) {
    NetSocket *sock = net_socket_get(fd);

    (void)flags;
    if (sock == (NetSocket *)0 || buf == (void *)0) {
        return -1;
    }
    if (sock->type == SOCK_DGRAM) {
        return net_socket_recv_udp(sock, buf, len, (NetSockAddrIn *)0);
    }
    return net_socket_recv_tcp(sock, buf, len);
}

int net_socket_sendto(int fd, const void *buf, uint32_t len, uint32_t flags, const NetSockAddrIn *addr) {
    NetSocket *sock = net_socket_get(fd);

    (void)flags;
    if (sock == (NetSocket *)0 || sock->type != SOCK_DGRAM || addr == (const NetSockAddrIn *)0 || addr->family != AF_INET) {
        return -1;
    }
    if (sock->local_port == 0u) {
        sock->local_port = net_socket_alloc_ephemeral_port();
        if (sock->local_port == 0u) {
            return -1;
        }
    }
    return udp_send(sock->local_addr, sock->local_port, ntohl(addr->addr), ntohs(addr->port),
                    buf, (uint16_t)(len > 1472u ? 1472u : len));
}

int net_socket_recvfrom(int fd, void *buf, uint32_t len, uint32_t flags, NetSockAddrIn *addr) {
    NetSocket *sock = net_socket_get(fd);

    (void)flags;
    if (sock == (NetSocket *)0 || sock->type != SOCK_DGRAM || buf == (void *)0) {
        return -1;
    }
    return net_socket_recv_udp(sock, buf, len, addr);
}

int net_socket_close(int fd) {
    NetSocket *sock = net_socket_get(fd);
    uint32_t flags;

    if (sock == (NetSocket *)0) {
        return -1;
    }

    if (sock->type == SOCK_STREAM) {
        if (sock->state == NET_SOCK_ESTABLISHED) {
            return tcp_send_ack(sock, (uint8_t)(NET_TCP_FIN | NET_TCP_ACK), (const void *)0, 0u);
        }
        if (sock->state == NET_SOCK_CLOSE_WAIT) {
            return tcp_send_ack(sock, (uint8_t)(NET_TCP_FIN | NET_TCP_ACK), (const void *)0, 0u);
        }
    }

    flags = spin_lock_irqsave(&sock->lock);
    while (sock->udp_rx_count != 0u) {
        int packet_index = net_queue_pop(&sock->udp_rx_head, &sock->udp_rx_tail);
        if (packet_index >= 0) {
            sock->udp_rx_count--;
            net_packet_free(packet_index);
        }
    }
    net_socket_reset(sock);
    spin_unlock_irqrestore(&sock->lock, flags);
    return 0;
}
