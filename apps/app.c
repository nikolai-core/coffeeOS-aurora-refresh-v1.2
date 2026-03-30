#include <stdint.h>

#include "app.h"
#include "ascii_util.h"
#include "gfx.h"

#define APP_FONT_W 8
#define APP_FONT_H 16

static int app_client_x;
static int app_client_y;
static int app_client_w;
static int app_client_h;
static int app_context_valid;

void app_set_draw_context(int client_x, int client_y, int client_w, int client_h) {
    app_client_x = client_x;
    app_client_y = client_y;
    app_client_w = client_w;
    app_client_h = client_h;
    app_context_valid = 1;
}

void app_clear_draw_context(void) {
    app_context_valid = 0;
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

void app_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    uint32_t i;

    if (!app_context_valid || y + APP_FONT_H <= 0 || y >= app_client_h) {
        return;
    }

    for (i = 0u; s[i] != '\0'; i++) {
        int char_x = x + (int)(i * APP_FONT_W);
        int char_y = y;

        if (char_x + APP_FONT_W <= 0) {
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
