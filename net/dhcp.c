#include <stdint.h>

#include "arp.h"
#include "dhcp.h"
#include "ethernet.h"
#include "net.h"
#include "netif.h"
#include "pit.h"
#include "rtl8139.h"
#include "serial.h"
#include "udp.h"

static DhcpState dhcp_state = DHCP_STATE_IDLE;
static struct NetInterface *dhcp_iface;
static int dhcp_udp_fd = -1;
static uint32_t dhcp_xid;
static uint32_t dhcp_offer_ip;
static uint32_t dhcp_server_id;
static uint32_t dhcp_ack_ip;
static uint32_t dhcp_ack_mask;
static uint32_t dhcp_ack_router;
static uint32_t dhcp_ack_dns;

/* Print one host-order IPv4 DHCP address to serial in dotted-decimal form. */
static void dhcp_log_ip(const char *msg, uint32_t ip) {
    char text[16];

    net_format_ip(ip, text, sizeof(text));
    serial_print("[dhcp] ");
    serial_print(msg);
    serial_print(" ");
    serial_print(text);
    serial_print("\n");
}

/* Append one DHCP option triplet into one packet options buffer. */
static uint32_t dhcp_add_option(uint8_t *opts, uint32_t pos, uint8_t code, uint8_t len, const void *data) {
    uint32_t i;

    opts[pos++] = code;
    opts[pos++] = len;
    for (i = 0u; i < len; i++) {
        opts[pos++] = ((const uint8_t *)data)[i];
    }
    return pos;
}

/* Broadcast one DHCP DISCOVER or REQUEST packet from the active interface. */
static int dhcp_send_message(uint8_t msg_type, uint32_t req_ip, uint32_t server_id) {
    DhcpPacket pkt;
    uint32_t pos = 0u;

    if (dhcp_iface == (struct NetInterface *)0) {
        return -1;
    }

    {
        uint32_t i;

        for (i = 0u; i < sizeof(pkt); i++) {
            ((uint8_t *)&pkt)[i] = 0u;
        }
    }
    pkt.op = 1u;
    pkt.htype = 1u;
    pkt.hlen = 6u;
    pkt.xid = htonl(dhcp_xid);
    pkt.flags = htons(0x8000u);
    {
        uint32_t i;

        for (i = 0u; i < 6u; i++) {
            pkt.chaddr[i] = dhcp_iface->mac[i];
        }
    }
    pkt.magic = htonl(DHCP_MAGIC);

    pkt.options[pos++] = DHCP_OPT_MSG_TYPE;
    pkt.options[pos++] = 1u;
    pkt.options[pos++] = msg_type;
    if (msg_type == DHCP_REQUEST) {
        uint32_t be_req = htonl(req_ip);
        uint32_t be_server = htonl(server_id);

        pos = dhcp_add_option(pkt.options, pos, DHCP_OPT_REQ_IP, 4u, &be_req);
        pos = dhcp_add_option(pkt.options, pos, DHCP_OPT_SERVER_ID, 4u, &be_server);
    }
    pkt.options[pos++] = DHCP_OPT_END;
    return udp_send(dhcp_iface, 0xFFFFFFFFu, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, &pkt, (uint16_t)sizeof(pkt));
}

/* Parse one DHCP UDP payload and advance the DHCP state machine. */
void dhcp_receive(uint32_t src_ip, uint16_t src_port,
                  const void *data, uint16_t len) {
    const DhcpPacket *pkt = (const DhcpPacket *)data;
    uint32_t pos = 0u;
    uint8_t msg_type = 0u;

    (void)src_ip;
    if (src_port != DHCP_SERVER_PORT || data == (const void *)0 || len < 240u) {
        return;
    }
    if (ntohl(pkt->magic) != DHCP_MAGIC || ntohl(pkt->xid) != dhcp_xid) {
        return;
    }

    while ((uint32_t)(240u + pos) < len && pos < sizeof(pkt->options)) {
        uint8_t code = pkt->options[pos++];
        uint8_t opt_len;

        if (code == DHCP_OPT_END) {
            break;
        }
        if (code == 0u || pos >= sizeof(pkt->options)) {
            continue;
        }
        opt_len = pkt->options[pos++];
        if (pos + opt_len > sizeof(pkt->options)) {
            break;
        }
        if (code == DHCP_OPT_MSG_TYPE && opt_len >= 1u) {
            msg_type = pkt->options[pos];
        } else if (code == DHCP_OPT_SERVER_ID && opt_len == 4u) {
            dhcp_server_id = ntohl(*(const uint32_t *)(const void *)(pkt->options + pos));
        } else if (code == DHCP_OPT_SUBNET_MASK && opt_len == 4u) {
            dhcp_ack_mask = ntohl(*(const uint32_t *)(const void *)(pkt->options + pos));
        } else if (code == DHCP_OPT_ROUTER && opt_len >= 4u) {
            dhcp_ack_router = ntohl(*(const uint32_t *)(const void *)(pkt->options + pos));
        } else if (code == DHCP_OPT_DNS && opt_len >= 4u) {
            dhcp_ack_dns = ntohl(*(const uint32_t *)(const void *)(pkt->options + pos));
        }
        pos += opt_len;
    }

    if (msg_type == DHCP_OFFER) {
        dhcp_offer_ip = ntohl(pkt->yiaddr);
        dhcp_state = DHCP_STATE_OFFER_RECEIVED;
        dhcp_log_ip("offer", dhcp_offer_ip);
    } else if (msg_type == DHCP_ACK) {
        dhcp_ack_ip = ntohl(pkt->yiaddr);
        dhcp_state = DHCP_STATE_BOUND;
        dhcp_log_ip("ack", dhcp_ack_ip);
    } else if (msg_type == DHCP_NAK) {
        dhcp_state = DHCP_STATE_FAILED;
        serial_print("[dhcp] nak\n");
    }
}

/* Run one blocking DHCP DORA transaction for the given interface. */
int dhcp_request(struct NetInterface *iface, uint32_t timeout_ticks) {
    uint32_t start_tick;

    if (iface == (struct NetInterface *)0) {
        netmon_log("DHCP failed");
        return -1;
    }

    dhcp_iface = iface;
    dhcp_xid = get_ticks() ^ 0x434F4645u;
    dhcp_offer_ip = 0u;
    dhcp_server_id = 0u;
    dhcp_ack_ip = 0u;
    dhcp_ack_mask = 0u;
    dhcp_ack_router = 0u;
    dhcp_ack_dns = 0u;
    dhcp_state = DHCP_STATE_DISCOVER;
    dhcp_udp_fd = udp_open(DHCP_CLIENT_PORT, dhcp_receive);
    if (dhcp_udp_fd < 0) {
        netmon_log("DHCP failed");
        dhcp_state = DHCP_STATE_FAILED;
        return -1;
    }

    if (dhcp_send_message(DHCP_DISCOVER, 0u, 0u) != 0) {
        serial_print("[dhcp] discover send failed\n");
        udp_close(dhcp_udp_fd);
        dhcp_udp_fd = -1;
        netmon_log("DHCP failed");
        dhcp_state = DHCP_STATE_FAILED;
        return -1;
    }

    start_tick = get_ticks();
    while ((get_ticks() - start_tick) < timeout_ticks) {
        rtl8139_poll();
        if (dhcp_state == DHCP_STATE_OFFER_RECEIVED) {
            break;
        }
    }
    if (dhcp_state != DHCP_STATE_OFFER_RECEIVED) {
        serial_print("[dhcp] offer timeout\n");
        udp_close(dhcp_udp_fd);
        dhcp_udp_fd = -1;
        netmon_log("DHCP failed");
        dhcp_state = DHCP_STATE_FAILED;
        return -1;
    }

    dhcp_state = DHCP_STATE_REQUEST;
    if (dhcp_send_message(DHCP_REQUEST, dhcp_offer_ip, dhcp_server_id) != 0) {
        serial_print("[dhcp] request send failed\n");
        udp_close(dhcp_udp_fd);
        dhcp_udp_fd = -1;
        netmon_log("DHCP failed");
        dhcp_state = DHCP_STATE_FAILED;
        return -1;
    }

    start_tick = get_ticks();
    while ((get_ticks() - start_tick) < timeout_ticks) {
        rtl8139_poll();
        if (dhcp_state == DHCP_STATE_BOUND || dhcp_state == DHCP_STATE_FAILED) {
            break;
        }
    }

    udp_close(dhcp_udp_fd);
    dhcp_udp_fd = -1;
    if (dhcp_state != DHCP_STATE_BOUND) {
        serial_print("[dhcp] ack timeout/state=");
        serial_write_hex((uint32_t)dhcp_state);
        serial_print("\n");
        netmon_log("DHCP failed");
        return -1;
    }

    netif_set_ip(0, dhcp_ack_ip, dhcp_ack_mask, dhcp_ack_router, dhcp_ack_dns);
    arp_announce(iface);
    netmon_log("DHCP OK");
    return 0;
}

/* Return the current DHCP client state. */
DhcpState dhcp_get_state(void) {
    return dhcp_state;
}
