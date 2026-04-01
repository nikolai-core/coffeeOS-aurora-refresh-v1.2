#include <stdint.h>

#include "app.h"
#include "arp.h"
#include "net.h"
#include "netif.h"

#define NETMON_BG          0x0D1B2Au
#define NETMON_BORDER      0x1A3A5Cu
#define NETMON_UP          0x2ECC71u
#define NETMON_DOWN        0xE74C3Cu
#define NETMON_NONE        0x808080u
#define NETMON_IP          0x5DADE2u
#define NETMON_LABEL       0x85C1E9u
#define NETMON_VALUE       0xEBF5FBu
#define NETMON_ODD         0x0D1B2Au
#define NETMON_EVEN        0x122337u
#define NETMON_LOG_BG      0x080F18u
#define NETMON_LOG_TEXT    0x27AE60u

/* Draw one fixed label/value pair in the monitor client area. */
static void netmon_draw_pair(int x, int y, const char *label, const char *value, uint32_t value_color) {
    app_draw_string(x, y, label, NETMON_LABEL, NETMON_BG);
    app_draw_string(x + 72, y, value, value_color, NETMON_BG);
}

/* Append one unsigned decimal value to a fixed app buffer. */
static void netmon_append_u32(char *dst, uint32_t max_len, uint32_t value) {
    char digits[10];
    uint32_t len = 0u;
    uint32_t count = 0u;

    while (dst[len] != '\0') {
        len++;
    }
    if (value == 0u) {
        if (len + 1u < max_len) {
            dst[len] = '0';
            dst[len + 1u] = '\0';
        }
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

/* Render the network monitor app from the current stack state. */
static void netmon_draw(int win_x, int win_y, int win_w, int win_h) {
    NetInterface *iface = netif_get(0);
    char text[64];
    uint32_t i;

    (void)win_x;
    (void)win_y;
    (void)win_h;
    app_clear(NETMON_BG);
    app_draw_rect(0, 0, win_w, 80, NETMON_BG);
    app_draw_hline(0, 80, win_w, NETMON_BORDER);
    app_draw_hline(0, 140, win_w, NETMON_BORDER);
    app_draw_hline(0, 240, win_w, NETMON_BORDER);

    app_draw_string(12, 10, "Interface", NETMON_LABEL, NETMON_BG);
    app_draw_rect(330, 12, 10, 10,
                  iface == (NetInterface *)0 ? NETMON_NONE : (iface->up ? NETMON_UP : NETMON_DOWN));
    if (iface == (NetInterface *)0) {
        app_draw_string(12, 30, "No NIC", NETMON_VALUE, NETMON_BG);
    } else {
        net_format_ip(iface->ip, text, sizeof(text));
        app_draw_string(12, 30, iface->name, NETMON_VALUE, NETMON_BG);
        netmon_draw_pair(12, 48, "IP", text, NETMON_IP);
        text[0] = '\0';
        app_draw_string(200, 48, "MAC present", NETMON_VALUE, NETMON_BG);
    }

    if (iface != (NetInterface *)0) {
        net_format_ip(iface->ip, text, sizeof(text));
        netmon_draw_pair(12, 92, "IPv4", text, NETMON_IP);
        text[0] = '\0';
        netmon_append_u32(text, sizeof(text), (uint32_t)iface->rx_packets);
        netmon_draw_pair(12, 110, "RX packets", text, NETMON_VALUE);
        text[0] = '\0';
        netmon_append_u32(text, sizeof(text), (uint32_t)iface->tx_packets);
        netmon_draw_pair(200, 110, "TX packets", text, NETMON_VALUE);
    }

    app_draw_string(12, 150, "ARP Cache", NETMON_LABEL, NETMON_BG);
    for (i = 0u; i < 4u; i++) {
        const ArpEntry *entry = arp_get_entry((int)i);
        int row_y = 168 + (int)(i * 18u);

        app_draw_rect(8, row_y - 2, win_w - 16, 16, (i & 1u) ? NETMON_EVEN : NETMON_ODD);
        if (entry != (const ArpEntry *)0 && entry->valid) {
            net_format_ip(entry->ip, text, sizeof(text));
            app_draw_string(12, row_y, text, NETMON_VALUE, (i & 1u) ? NETMON_EVEN : NETMON_ODD);
        }
    }

    app_draw_rect(0, 240, win_w, 60, NETMON_LOG_BG);
    app_draw_string(12, 246, "Activity", NETMON_LABEL, NETMON_LOG_BG);
    for (i = 0u; i < 4u; i++) {
        const char *line;
        uint32_t count = netmon_log_count();

        if (count <= i) {
            break;
        }
        line = netmon_log_line(count - 1u - i);
        if (line != (const char *)0) {
            app_draw_string(12, 262 + (int)(i * 12u), line, NETMON_LOG_TEXT, NETMON_LOG_BG);
        }
    }
}

/* Ignore keyboard input in the passive network monitor app. */
static void netmon_key(char c) {
    (void)c;
}

/* Ignore mouse clicks in the passive network monitor app. */
static void netmon_click(int x, int y, int btn) {
    (void)x;
    (void)y;
    (void)btn;
}

/* The network monitor has no teardown state to release. */
static void netmon_close(void) {
}

App netmon_app = {
    .title = "Network",
    .x = 300,
    .y = 200,
    .w = 400,
    .h = 300,
    .bg_color = NETMON_BG,
    .on_init = (void (*)(void))0,
    .on_draw = netmon_draw,
    .on_key = netmon_key,
    .on_click = netmon_click,
    .on_close = netmon_close
};
