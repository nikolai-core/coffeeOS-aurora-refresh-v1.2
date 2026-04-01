#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#include "ip.h"

struct NetInterface;

typedef struct __attribute__((packed)) TcpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} TcpHeader;

#define TCP_FIN 0x01u
#define TCP_SYN 0x02u
#define TCP_RST 0x04u
#define TCP_PSH 0x08u
#define TCP_ACK 0x10u

#define TCP_MAX_SOCKETS  4u
#define TCP_RX_BUF_SIZE  4096u
#define TCP_TX_BUF_SIZE  4096u
#define TCP_MSS          1460u
#define TCP_WINDOW       4096u

typedef enum {
    TCP_CLOSED,
    TCP_SYN_SENT,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT1,
    TCP_FIN_WAIT2,
    TCP_TIME_WAIT,
    TCP_CLOSE_WAIT
} TcpState;

typedef struct TcpSocket {
    TcpState  state;
    uint32_t  local_ip;
    uint16_t  local_port;
    uint32_t  remote_ip;
    uint16_t  remote_port;
    uint32_t  seq_send;
    uint32_t  seq_recv;
    uint8_t   rx_buf[TCP_RX_BUF_SIZE];
    uint32_t  rx_head;
    uint32_t  rx_tail;
    int       open;
} TcpSocket;

/* Open one blocking outbound TCP connection and wait for ESTABLISHED. */
int tcp_connect(struct NetInterface *iface, uint32_t dst_ip, uint16_t dst_port,
                uint32_t timeout_ticks);

/* Close one TCP socket by index. */
void tcp_close(int fd);

/* Send one TCP PSH+ACK segment from an established socket. */
int tcp_send(int fd, const void *data, uint16_t len);

/* Receive bytes from one TCP socket into a caller buffer with a timeout. */
int tcp_recv(int fd, void *buf, uint16_t max_len, uint32_t timeout_ticks);

/* Parse one TCP payload attached to an IPv4 packet. */
void tcp_receive(const IpHeader *ip, const void *payload, uint16_t len);

/* Return non-zero when one TCP socket is established. */
int tcp_ready(int fd);

/* Return one TCP socket by index for shell/UI inspection. */
const TcpSocket *tcp_get_socket(int fd);

/* Return a short printable name for one TCP state value. */
const char *tcp_state_name(TcpState state);

#endif
