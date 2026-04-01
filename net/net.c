#include <stdint.h>

#include "arp.h"
#include "ascii_util.h"
#include "dhcp.h"
#include "dns.h"
#include "ethernet.h"
#include "net.h"
#include "netif.h"
#include "pit.h"
#include "rtl8139.h"
#include "serial.h"

static char netmon_lines[NETMON_LINES][NETMON_LINE_LEN];
static uint32_t netmon_count_value;
static uint32_t netmon_next;

/* Copy one small ASCII string into a fixed log line buffer. */
static void net_copy_text(char *dst, const char *src, uint32_t max_len) {
    uint32_t i = 0u;

    while (i + 1u < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Append one unsigned decimal value to a fixed log line buffer. */
static void net_append_u32(char *dst, uint32_t max_len, uint32_t value) {
    char digits[10];
    uint32_t len = ascii_strlen(dst);
    uint32_t count = 0u;

    if (len + 1u >= max_len) {
        return;
    }
    if (value == 0u) {
        dst[len] = '0';
        dst[len + 1u] = '\0';
        return;
    }
    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0u && len + 1u < max_len) {
        dst[len++] = digits[--count];
    }
    dst[len] = '\0';
}

/* Format one host-order IPv4 address into dotted decimal text. */
void net_format_ip(uint32_t ip, char *out, uint32_t out_len) {
    uint32_t pos = 0u;
    uint32_t part;

    if (out_len == 0u) {
        return;
    }
    out[0] = '\0';
    for (part = 0u; part < 4u; part++) {
        char temp[4];
        uint32_t value = (ip >> ((3u - part) * 8u)) & 0xFFu;
        uint32_t count = 0u;

        if (value == 0u) {
            temp[count++] = '0';
        } else {
            while (value != 0u) {
                temp[count++] = (char)('0' + (value % 10u));
                value /= 10u;
            }
        }
        while (count != 0u && pos + 1u < out_len) {
            out[pos++] = temp[--count];
        }
        if (part != 3u && pos + 1u < out_len) {
            out[pos++] = '.';
        }
    }
    out[pos] = '\0';
}

/* Parse one dotted-decimal IPv4 string into host byte order. */
int net_parse_ip(const char *text, uint32_t *out_ip) {
    uint32_t part = 0u;
    uint32_t value = 0u;
    uint32_t ip = 0u;

    if (text == (const char *)0 || out_ip == (uint32_t *)0) {
        return 0;
    }
    while (*text != '\0') {
        if (*text >= '0' && *text <= '9') {
            value = value * 10u + (uint32_t)(*text - '0');
            if (value > 255u) {
                return 0;
            }
        } else if (*text == '.') {
            if (part >= 3u) {
                return 0;
            }
            ip = (ip << 8) | value;
            value = 0u;
            part++;
        } else {
            return 0;
        }
        text++;
    }
    if (part != 3u) {
        return 0;
    }
    ip = (ip << 8) | value;
    *out_ip = ip;
    return 1;
}

/* Append one timestamped message to the network monitor log. */
void netmon_log(const char *msg) {
    char line[NETMON_LINE_LEN];
    uint32_t len;

    line[0] = '[';
    line[1] = '\0';
    net_append_u32(line, sizeof(line), get_ticks() / 100u);
    len = ascii_strlen(line);
    net_copy_text(line + len, "] ", sizeof(line) - len);
    len = ascii_strlen(line);
    net_copy_text(line + len, msg, sizeof(line) - len);
    net_copy_text(netmon_lines[netmon_next], line, NETMON_LINE_LEN);
    netmon_next = (netmon_next + 1u) % NETMON_LINES;
    if (netmon_count_value < NETMON_LINES) {
        netmon_count_value++;
    }
}

/* Return the number of stored network monitor log lines. */
uint32_t netmon_log_count(void) {
    return netmon_count_value;
}

/* Return one chronological network monitor log line by index. */
const char *netmon_log_line(uint32_t index) {
    uint32_t start;

    if (index >= netmon_count_value) {
        return (const char *)0;
    }
    start = (netmon_next + NETMON_LINES - netmon_count_value) % NETMON_LINES;
    return netmon_lines[(start + index) % NETMON_LINES];
}

/* Initialize the full network stack and bring up eth0 when DHCP succeeds. */
void net_init(void) {
    uint8_t mac[6];
    NetInterface *iface;
    char ip_text[16];

    netif_init();
    arp_init();
    if (rtl8139_init() == 0) {
        if (rtl8139_get_mac(mac) == 0) {
            (void)netif_register("eth0", mac, rtl8139_send);
        }
        iface = netif_find("eth0");
        if (iface != (NetInterface *)0) {
            serial_print("[net] RTL8139 registered as eth0\n");
            serial_print("[net] starting DHCP...\n");
            if (dhcp_request(iface, 500u) == 0) {
                netif_up(0);
                net_format_ip(iface->ip, ip_text, sizeof(ip_text));
                serial_print("[net] DHCP OK, IP=");
                serial_print(ip_text);
                serial_print("\n");
            } else {
                serial_print("[net] DHCP failed\n");
            }
        }
    } else {
        serial_print("[net] no RTL8139 found\n");
    }
}

/* Poll the NIC receive path once from the desktop loop. */
void net_poll(void) {
    if (rtl8139_present()) {
        rtl8139_poll();
    }
}

/* Age network caches once per PIT tick. */
void net_tick(void) {
    arp_tick();
    dns_tick();
}

/* Return non-zero when the default interface is up and configured. */
int net_is_up(void) {
    NetInterface *iface = netif_default();

    return iface != (NetInterface *)0 && iface->ip != 0u;
}
