#ifndef NET_STACK_H
#define NET_STACK_H

#include <stdint.h>

#include "net/socket_abi.h"
#include "spinlock.h"

#define NET_PACKET_BUFFER_COUNT 32u
#define NET_PACKET_BUFFER_SIZE  1536u
#define NET_SOCKET_COUNT        16u
#define NET_ARP_CACHE_SIZE      16u
#define NET_TCP_BACKLOG         4u
#define NET_TCP_RX_BUFFER       2048u
#define NET_TCP_TX_BUFFER       1024u
#define NET_TCP_MSS             536u
#define NET_UDP_RX_QUEUE        4u
#define NET_QUEUE_NONE          0xFFFFu

#define NET_ETHERTYPE_IPV4 0x0800u
#define NET_ETHERTYPE_ARP  0x0806u

#define NET_IP_PROTO_ICMP 1u
#define NET_IP_PROTO_TCP  6u
#define NET_IP_PROTO_UDP  17u

#define NET_TCP_FIN 0x01u
#define NET_TCP_SYN 0x02u
#define NET_TCP_RST 0x04u
#define NET_TCP_PSH 0x08u
#define NET_TCP_ACK 0x10u

typedef enum NetSocketState {
    NET_SOCK_CLOSED = 0,
    NET_SOCK_LISTEN,
    NET_SOCK_SYN_SENT,
    NET_SOCK_SYN_RECEIVED,
    NET_SOCK_ESTABLISHED,
    NET_SOCK_FIN_WAIT_1,
    NET_SOCK_FIN_WAIT_2,
    NET_SOCK_CLOSE_WAIT,
    NET_SOCK_LAST_ACK,
    NET_SOCK_TIME_WAIT
} NetSocketState;

typedef struct NetPacketBuffer {
    uint16_t len;
    uint16_t next;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint8_t data[NET_PACKET_BUFFER_SIZE];
    uint8_t in_use;
} NetPacketBuffer;

typedef struct NetSocket {
    uint8_t in_use;
    uint8_t type;
    uint8_t protocol;
    uint8_t flags;
    uint8_t backlog_limit;
    uint8_t backlog_head;
    uint8_t backlog_tail;
    uint8_t backlog_count;
    uint8_t backlog[NET_TCP_BACKLOG];
    uint8_t pending_close;
    uint8_t remote_closed;
    uint8_t awaiting_ack;
    uint8_t tx_flags;
    uint8_t tx_retries;
    uint8_t dup_acks;
    uint8_t pad0;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t local_addr;
    uint32_t remote_addr;
    NetSocketState state;
    uint32_t iss;
    uint32_t irs;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t cwnd;
    uint32_t ssthresh;
    uint32_t last_tx_tick;
    uint32_t rto_ticks;
    uint32_t tx_seq;
    uint16_t tx_len;
    uint16_t rx_head;
    uint16_t rx_len;
    uint16_t udp_rx_head;
    uint16_t udp_rx_tail;
    uint16_t udp_rx_count;
    uint8_t parent;
    uint8_t child_pending;
    uint8_t tx_data[NET_TCP_TX_BUFFER];
    uint8_t rx_data[NET_TCP_RX_BUFFER];
    SpinLock lock;
} NetSocket;

typedef struct NetIfConfig {
    uint8_t mac[6];
    uint32_t addr;
    uint32_t netmask;
    uint32_t gateway;
    uint8_t up;
} NetIfConfig;

static inline void net_memcpy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

static inline void net_memset(void *dst, uint8_t value, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = value;
    }
}

static inline int net_memcmp(const void *a, const void *b, uint32_t len) {
    const uint8_t *aa = (const uint8_t *)a;
    const uint8_t *bb = (const uint8_t *)b;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (aa[i] != bb[i]) {
            return (int)aa[i] - (int)bb[i];
        }
    }
    return 0;
}

static inline uint16_t net_checksum16(const void *data, uint32_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0u;
    uint32_t i;

    for (i = 0u; i + 1u < len; i += 2u) {
        sum += ((uint32_t)bytes[i] << 8) | bytes[i + 1u];
    }
    if ((len & 1u) != 0u) {
        sum += (uint32_t)bytes[len - 1u] << 8;
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static inline uint16_t net_ipv4_checksum(const void *header, uint32_t len) {
    return net_checksum16(header, len);
}

static inline int net_ipv4_is_broadcast(uint32_t addr, uint32_t netmask) {
    return (addr | netmask) == 0xFFFFFFFFu;
}

void net_init(void);
void net_tick(void);
int net_handle_irq(uint8_t irq_line);

const NetIfConfig *net_if_config(void);
uint32_t net_now_ticks(void);

int net_packet_alloc(void);
void net_packet_free(int index);
NetPacketBuffer *net_packet_get(int index);
void net_queue_push(uint16_t *head, uint16_t *tail, uint16_t packet_index);
int net_queue_pop(uint16_t *head, uint16_t *tail);

int ethernet_send_frame(const uint8_t dst_mac[6], uint16_t ethertype, const void *payload, uint16_t len);
void ethernet_receive_frame(const uint8_t *frame, uint16_t len);

void arp_init(void);
void arp_tick(void);
void arp_receive(const uint8_t *frame, uint16_t len);
int arp_resolve(uint32_t target_ip, uint8_t out_mac[6]);
void arp_learn(uint32_t addr, const uint8_t mac[6]);

void ipv4_init(void);
void ipv4_tick(void);
void ipv4_receive(const uint8_t *frame, uint16_t len);
int ipv4_send(uint32_t dst_addr, uint8_t protocol, const void *payload, uint16_t payload_len);

void icmp_receive(uint32_t src_addr, uint32_t dst_addr, const uint8_t *packet, uint16_t len);

void udp_init(void);
void udp_receive(uint32_t src_addr, uint32_t dst_addr, const uint8_t *packet, uint16_t len);
int udp_send(uint32_t src_addr, uint16_t src_port, uint32_t dst_addr, uint16_t dst_port,
             const void *payload, uint16_t len);

void tcp_init(void);
void tcp_tick(void);
void tcp_receive(uint32_t src_addr, uint32_t dst_addr, const uint8_t *packet, uint16_t len);
int tcp_send_ack(NetSocket *sock, uint8_t flags, const void *payload, uint16_t len);
void tcp_abort(NetSocket *sock);

void net_socket_init(void);
int net_socket_create(int domain, int type, int protocol);
int net_socket_bind(int fd, const NetSockAddrIn *addr);
int net_socket_listen(int fd, int backlog);
int net_socket_accept(int fd, NetSockAddrIn *addr);
int net_socket_connect(int fd, const NetSockAddrIn *addr);
int net_socket_send(int fd, const void *buf, uint32_t len, uint32_t flags);
int net_socket_recv(int fd, void *buf, uint32_t len, uint32_t flags);
int net_socket_sendto(int fd, const void *buf, uint32_t len, uint32_t flags, const NetSockAddrIn *addr);
int net_socket_recvfrom(int fd, void *buf, uint32_t len, uint32_t flags, NetSockAddrIn *addr);
int net_socket_close(int fd);
NetSocket *net_socket_get(int fd);
uint16_t net_socket_alloc_ephemeral_port(void);
void net_socket_queue_udp(NetSocket *sock, int packet_index);

#endif
