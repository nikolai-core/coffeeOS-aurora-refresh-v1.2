#include <stdint.h>

#include "app.h"
#include "ascii_util.h"
#include "gfx.h"
#include "pit.h"

static App *app_active;
static int app_client_x;
static int app_client_y;
static int app_client_w;
static int app_client_h;
static int app_context_valid;
static AppRedrawCallback app_redraw_callback;

void app_set_draw_context_for(App *app, int client_x, int client_y, int client_w, int client_h) {
    app_active = app;
    app_client_x = client_x;
    app_client_y = client_y;
    app_client_w = client_w;
    app_client_h = client_h;
    app_context_valid = 1;
}

void app_set_draw_context(int client_x, int client_y, int client_w, int client_h) {
    app_set_draw_context_for((App *)0, client_x, client_y, client_w, client_h);
}

void app_clear_draw_context(void) {
    app_active = (App *)0;
    app_context_valid = 0;
}

App *app_current(void) {
    return app_context_valid ? app_active : (App *)0;
}

int app_client_width(void) {
    return app_context_valid ? app_client_w : 0;
}

int app_client_height(void) {
    return app_context_valid ? app_client_h : 0;
}

void app_set_redraw_callback(AppRedrawCallback callback) {
    app_redraw_callback = callback;
}

void app_request_redraw(App *app) {
    if (app_redraw_callback != (AppRedrawCallback)0) {
        app_redraw_callback(app);
    }
}

void app_request_current_redraw(void) {
    app_request_redraw(app_current());
}

static int app_clip_rect(int *x, int *y, int *w, int *h) {
    int x0;
    int y0;
    int x1;
    int y1;

    if (!app_context_valid || *w <= 0 || *h <= 0) {
        return 0;
    }

    x0 = *x;
    y0 = *y;
    x1 = *x + *w;
    y1 = *y + *h;

    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > app_client_w) {
        x1 = app_client_w;
    }
    if (y1 > app_client_h) {
        y1 = app_client_h;
    }
    if (x1 <= x0 || y1 <= y0) {
        return 0;
    }

    *x = x0;
    *y = y0;
    *w = x1 - x0;
    *h = y1 - y0;
    return 1;
}

void app_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (!app_clip_rect(&x, &y, &w, &h)) {
        return;
    }

    gfx_draw_rect(app_client_x + x, app_client_y + y, w, h, color);
}

void app_draw_hline(int x, int y, int w, uint32_t color) {
    app_draw_rect(x, y, w, 1, color);
}

void app_draw_vline(int x, int y, int h, uint32_t color) {
    app_draw_rect(x, y, 1, h, color);
}

void app_draw_border(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }

    app_draw_hline(x, y, w, color);
    app_draw_hline(x, y + h - 1, w, color);
    app_draw_vline(x, y, h, color);
    app_draw_vline(x + w - 1, y, h, color);
}

void app_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t i;

    if (!app_context_valid || s == (const char *)0 || y + APP_FONT_HEIGHT <= 0 || y >= app_client_h) {
        return;
    }

    for (i = 0u; s[i] != '\0'; i++) {
        int char_x = x + (int)(i * APP_FONT_WIDTH);
        int char_y = y;

        if (char_x + APP_FONT_WIDTH <= 0) {
            continue;
        }
        if (char_x >= app_client_w) {
            break;
        }

        gfx_draw_char_at(app_client_x + char_x, app_client_y + char_y, s[i], fg, bg);
    }
}

void app_clear(uint32_t color) {
    if (!app_context_valid) {
        return;
    }

    gfx_draw_rect(app_client_x, app_client_y, app_client_w, app_client_h, color);
}

void app_draw_button(int x, int y, int w, int h, const char *label,
                     uint32_t bg, uint32_t border, uint32_t fg) {
    int text_x;
    int text_y;
    int text_w;

    app_draw_rect(x, y, w, h, bg);
    app_draw_border(x, y, w, h, border);

    text_w = app_text_width(label);
    text_x = x + ((w - text_w) / 2);
    text_y = y + ((h - APP_FONT_HEIGHT) / 2);
    if (text_y < y) {
        text_y = y;
    }
    app_draw_string(text_x, text_y, label, fg, bg);
}

int app_font_width(void) {
    return APP_FONT_WIDTH;
}

int app_font_height(void) {
    return APP_FONT_HEIGHT;
}

int app_text_width(const char *s) {
    if (s == (const char *)0) {
        return 0;
    }
    return (int)(ascii_strlen(s) * APP_FONT_WIDTH);
}

int app_point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

int app_clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

void app_copy_string(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i = 0u;

    if (dst == (char *)0 || dst_len == 0u) {
        return;
    }
    if (src == (const char *)0) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1u < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void app_append_string(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i;
    uint32_t j = 0u;

    if (dst == (char *)0 || dst_len == 0u || src == (const char *)0) {
        return;
    }

    i = ascii_strlen(dst);
    if (i >= dst_len) {
        return;
    }

    while (src[j] != '\0' && i + 1u < dst_len) {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}

void app_append_u32(char *dst, uint32_t dst_len, uint32_t value) {
    char digits[10];
    uint32_t count = 0u;
    uint32_t i;

    if (dst == (char *)0 || dst_len == 0u) {
        return;
    }
    if (value == 0u) {
        app_append_string(dst, dst_len, "0");
        return;
    }

    while (value > 0u && count < (uint32_t)(sizeof(digits) / sizeof(digits[0]))) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    for (i = count; i > 0u; i--) {
        char one[2];

        one[0] = digits[i - 1u];
        one[1] = '\0';
        app_append_string(dst, dst_len, one);
    }
}

uint32_t app_ticks(void) {
    return get_ticks();
}

uint32_t app_blend_color(uint32_t a, uint32_t b, uint32_t step, uint32_t steps) {
    uint32_t ar;
    uint32_t ag;
    uint32_t ab;
    uint32_t br;
    uint32_t bg;
    uint32_t bb;
    uint32_t r;
    uint32_t g;
    uint32_t blue;

    if (steps == 0u || step >= steps) {
        return b;
    }

    ar = (a >> 16) & 0xFFu;
    ag = (a >> 8) & 0xFFu;
    ab = a & 0xFFu;
    br = (b >> 16) & 0xFFu;
    bg = (b >> 8) & 0xFFu;
    bb = b & 0xFFu;

    r = ((ar * (steps - step)) + (br * step)) / steps;
    g = ((ag * (steps - step)) + (bg * step)) / steps;
    blue = ((ab * (steps - step)) + (bb * step)) / steps;
    return (r << 16) | (g << 8) | blue;
}

int app_anim_saw(uint32_t period_ticks, int max_value) {
    if (period_ticks == 0u || max_value <= 0) {
        return 0;
    }
    return (int)((get_ticks() % period_ticks) * (uint32_t)max_value / period_ticks);
}

int app_anim_pingpong(uint32_t period_ticks, int max_value) {
    uint32_t half;
    uint32_t pos;

    if (period_ticks < 2u || max_value <= 0) {
        return 0;
    }

    half = period_ticks / 2u;
    if (half == 0u) {
        return 0;
    }
    pos = get_ticks() % period_ticks;
    if (pos >= half) {
        pos = period_ticks - pos;
    }
    return (int)((pos * (uint32_t)max_value) / half);
}

void app_format_size(uint32_t size, char *out, uint32_t out_len) {
    uint32_t value;
    const char *suffix;

    if (out == (char *)0 || out_len == 0u) {
        return;
    }

    if (size >= 1024u * 1024u) {
        value = size / (1024u * 1024u);
        suffix = "MB";
    } else if (size >= 1024u) {
        value = size / 1024u;
        suffix = "KB";
    } else {
        value = size;
        suffix = "B";
    }

    out[0] = '\0';
    app_append_u32(out, out_len, value);
    app_append_string(out, out_len, suffix);
}
