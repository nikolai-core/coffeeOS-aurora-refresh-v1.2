#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>

struct NetInterface;

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_MAGIC       0x63825363u

typedef struct __attribute__((packed)) DhcpPacket {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[308];
} DhcpPacket;

#define DHCP_OPT_SUBNET_MASK  1u
#define DHCP_OPT_ROUTER       3u
#define DHCP_OPT_DNS          6u
#define DHCP_OPT_LEASE_TIME   51u
#define DHCP_OPT_MSG_TYPE     53u
#define DHCP_OPT_SERVER_ID    54u
#define DHCP_OPT_REQ_IP       50u
#define DHCP_OPT_END          255u

#define DHCP_DISCOVER 1u
#define DHCP_OFFER    2u
#define DHCP_REQUEST  3u
#define DHCP_ACK      5u
#define DHCP_NAK      6u

typedef enum {
    DHCP_STATE_IDLE,
    DHCP_STATE_DISCOVER,
    DHCP_STATE_OFFER_RECEIVED,
    DHCP_STATE_REQUEST,
    DHCP_STATE_BOUND,
    DHCP_STATE_FAILED
} DhcpState;

/* Run one blocking DHCP DORA transaction for the given interface. */
int dhcp_request(struct NetInterface *iface, uint32_t timeout_ticks);

/* Parse one DHCP UDP payload and advance the DHCP state machine. */
void dhcp_receive(uint32_t src_ip, uint16_t src_port,
                  const void *data, uint16_t len);

/* Return the current DHCP client state. */
DhcpState dhcp_get_state(void);

#endif
