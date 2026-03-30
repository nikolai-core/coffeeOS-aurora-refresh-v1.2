#include <stdint.h>

#include "app.h"
#include "ascii_util.h"

static int click_count;

static void hello_copy_string(char *dst, uint32_t dst_max_len, const char *src) {
    uint32_t i = 0u;

    if (dst_max_len == 0u) {
        return;
    }

    while (src[i] != '\0' && i + 1u < dst_max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void hello_append_string(char *dst, uint32_t dst_max_len, const char *src) {
    uint32_t i = ascii_strlen(dst);
    uint32_t j = 0u;

    if (i >= dst_max_len) {
        return;
    }

    while (src[j] != '\0' && i + 1u < dst_max_len) {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}

static void hello_append_u32(char *dst, uint32_t dst_max_len, uint32_t value) {
    char digits[11];
    uint32_t count = 0u;
    uint32_t i;

    if (value == 0u) {
        hello_append_string(dst, dst_max_len, "0");
        return;
    }

    while (value > 0u && count < 10u) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    for (i = count; i > 0u; i--) {
        char one[2];

        one[0] = digits[i - 1u];
        one[1] = '\0';
        hello_append_string(dst, dst_max_len, one);
    }
}

static void hello_on_init(void) {
    click_count = 0;
}

static void hello_on_draw(int win_x, int win_y, int win_w, int win_h) {
    char buf[64];

    (void)win_x;
    (void)win_y;
    (void)win_w;
    (void)win_h;

    app_clear(0x1A1A2Eu);
    app_draw_string(10, 10, "Hello from coffeeOS app!", 0xFFFFFFu, 0x1A1A2Eu);
    app_draw_string(10, 30, "Click anywhere in this window", 0xAAAAAAu, 0x1A1A2Eu);

    hello_copy_string(buf, (uint32_t)sizeof(buf), "Clicks: ");
    hello_append_u32(buf, (uint32_t)sizeof(buf), (uint32_t)click_count);
    app_draw_string(10, 50, buf, 0x00FF88u, 0x1A1A2Eu);
}

static void hello_on_click(int x, int y, int btn) {
    (void)x;
    (void)y;
    (void)btn;
    click_count++;
}

App hello_app = {
    .title = "Hello App",
    .x = 700,
    .y = 260,
    .w = 300,
    .h = 150,
    .bg_color = 0x1A1A2Eu,
    .on_init = hello_on_init,
    .on_draw = hello_on_draw,
    .on_key = 0,
    .on_click = hello_on_click,
    .on_close = 0
};
