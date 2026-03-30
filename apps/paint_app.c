#include <stdint.h>

#include "app.h"
#define PAINT_CANVAS_W 300
#define PAINT_CANVAS_H 200
#define PAINT_PADDING 12
#define PAINT_TOOLBAR_H 44
#define PAINT_SWATCH_SIZE 18
#define PAINT_SWATCH_GAP 8
#define PAINT_CLEAR_W 52

#define PAINT_BG 0x182028u
#define PAINT_PANEL 0x11181Fu
#define PAINT_PANEL_BORDER 0x31404Du
#define PAINT_CANVAS_BG 0x10161Bu
#define PAINT_CANVAS_BORDER 0x4A5B69u
#define PAINT_TEXT 0xDCE7EFu
#define PAINT_TEXT_DIM 0x9AAAB7u
#define PAINT_CLEAR_BG 0x7A2E3Au
#define PAINT_CLEAR_BORDER 0xA54A59u
#define PAINT_ACTIVE_BORDER 0xF2F7FBu

struct PaintSwatch {
    int x;
    int color;
};

static uint32_t canvas[PAINT_CANVAS_W * PAINT_CANVAS_H];
static uint32_t current_color;

static const struct PaintSwatch paint_swatches[] = {
    {PAINT_PADDING + 58, 0xD95C5Cu},
    {PAINT_PADDING + 58 + (PAINT_SWATCH_SIZE + PAINT_SWATCH_GAP), 0x5FBF7Fu},
    {PAINT_PADDING + 58 + ((PAINT_SWATCH_SIZE + PAINT_SWATCH_GAP) * 2), 0x5C8FE6u},
    {PAINT_PADDING + 58 + ((PAINT_SWATCH_SIZE + PAINT_SWATCH_GAP) * 3), 0xE3C567u},
    {PAINT_PADDING + 58 + ((PAINT_SWATCH_SIZE + PAINT_SWATCH_GAP) * 4), 0xF2F7FBu}
};

static void paint_clear(void) {
    int i;

    for (i = 0; i < PAINT_CANVAS_W * PAINT_CANVAS_H; i++) {
        canvas[i] = PAINT_CANVAS_BG;
    }
}

static int paint_canvas_x(void) {
    return PAINT_PADDING;
}

static int paint_canvas_y(void) {
    return PAINT_PADDING;
}

static int paint_toolbar_y(void) {
    return PAINT_PADDING + PAINT_CANVAS_H + 10;
}

static int paint_clear_x(void) {
    return PAINT_CANVAS_W + PAINT_PADDING - PAINT_CLEAR_W;
}

static void paint_plot(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= PAINT_CANVAS_W || y >= PAINT_CANVAS_H) {
        return;
    }

    canvas[(y * PAINT_CANVAS_W) + x] = color;
}

static void paint_stamp_brush(int x, int y) {
    int dx;
    int dy;

    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            paint_plot(x + dx, y + dy, current_color);
        }
    }
}

static int paint_hit_swatch(int x, int y) {
    int i;
    int toolbar_y = paint_toolbar_y();
    int swatch_y = toolbar_y + 13;

    if (y < swatch_y || y >= swatch_y + PAINT_SWATCH_SIZE) {
        return -1;
    }

    for (i = 0; i < (int)(sizeof(paint_swatches) / sizeof(paint_swatches[0])); i++) {
        if (x >= paint_swatches[i].x && x < paint_swatches[i].x + PAINT_SWATCH_SIZE) {
            return i;
        }
    }

    return -1;
}

static int paint_hit_clear(int x, int y) {
    int toolbar_y = paint_toolbar_y();
    int clear_x = paint_clear_x();

    return x >= clear_x && x < clear_x + PAINT_CLEAR_W
        && y >= toolbar_y + 8 && y < toolbar_y + 8 + 26;
}

static void paint_init(void) {
    current_color = paint_swatches[0].color;
    paint_clear();
}

static void paint_draw_toolbar(void) {
    int toolbar_y = paint_toolbar_y();
    int swatch_y = toolbar_y + 13;
    int i;

    app_draw_rect(PAINT_PADDING, toolbar_y, PAINT_CANVAS_W, PAINT_TOOLBAR_H, PAINT_PANEL);
    app_draw_hline(PAINT_PADDING, toolbar_y, PAINT_CANVAS_W, PAINT_PANEL_BORDER);
    app_draw_hline(PAINT_PADDING, toolbar_y + PAINT_TOOLBAR_H - 1, PAINT_CANVAS_W, PAINT_PANEL_BORDER);
    app_draw_vline(PAINT_PADDING, toolbar_y, PAINT_TOOLBAR_H, PAINT_PANEL_BORDER);
    app_draw_vline(PAINT_PADDING + PAINT_CANVAS_W - 1, toolbar_y, PAINT_TOOLBAR_H, PAINT_PANEL_BORDER);

    app_draw_string(PAINT_PADDING + 8, toolbar_y + 14, "Paint", PAINT_TEXT, PAINT_PANEL);

    for (i = 0; i < (int)(sizeof(paint_swatches) / sizeof(paint_swatches[0])); i++) {
        uint32_t border = (paint_swatches[i].color == current_color) ? PAINT_ACTIVE_BORDER : PAINT_PANEL_BORDER;

        app_draw_rect(paint_swatches[i].x, swatch_y, PAINT_SWATCH_SIZE, PAINT_SWATCH_SIZE, paint_swatches[i].color);
        app_draw_hline(paint_swatches[i].x, swatch_y, PAINT_SWATCH_SIZE, border);
        app_draw_hline(paint_swatches[i].x, swatch_y + PAINT_SWATCH_SIZE - 1, PAINT_SWATCH_SIZE, border);
        app_draw_vline(paint_swatches[i].x, swatch_y, PAINT_SWATCH_SIZE, border);
        app_draw_vline(paint_swatches[i].x + PAINT_SWATCH_SIZE - 1, swatch_y, PAINT_SWATCH_SIZE, border);
    }

    app_draw_rect(paint_clear_x(), toolbar_y + 8, PAINT_CLEAR_W, 26, PAINT_CLEAR_BG);
    app_draw_hline(paint_clear_x(), toolbar_y + 8, PAINT_CLEAR_W, PAINT_CLEAR_BORDER);
    app_draw_hline(paint_clear_x(), toolbar_y + 33, PAINT_CLEAR_W, PAINT_CLEAR_BORDER);
    app_draw_vline(paint_clear_x(), toolbar_y + 8, 26, PAINT_CLEAR_BORDER);
    app_draw_vline(paint_clear_x() + PAINT_CLEAR_W - 1, toolbar_y + 8, 26, PAINT_CLEAR_BORDER);
    app_draw_string(paint_clear_x() + 10, toolbar_y + 13, "Clear", PAINT_TEXT, PAINT_CLEAR_BG);
}

static void paint_draw(int win_x, int win_y, int win_w, int win_h) {
    int x;
    int y;

    (void)win_x;
    (void)win_y;
    (void)win_w;
    (void)win_h;

    app_clear(PAINT_BG);

    for (y = 0; y < PAINT_CANVAS_H; y++) {
        for (x = 0; x < PAINT_CANVAS_W; x++) {
            app_draw_rect(paint_canvas_x() + x, paint_canvas_y() + y, 1, 1, canvas[(y * PAINT_CANVAS_W) + x]);
        }
    }

    app_draw_hline(paint_canvas_x() - 1, paint_canvas_y() - 1, PAINT_CANVAS_W + 2, PAINT_CANVAS_BORDER);
    app_draw_hline(paint_canvas_x() - 1, paint_canvas_y() + PAINT_CANVAS_H, PAINT_CANVAS_W + 2, PAINT_CANVAS_BORDER);
    app_draw_vline(paint_canvas_x() - 1, paint_canvas_y() - 1, PAINT_CANVAS_H + 2, PAINT_CANVAS_BORDER);
    app_draw_vline(paint_canvas_x() + PAINT_CANVAS_W, paint_canvas_y() - 1, PAINT_CANVAS_H + 2, PAINT_CANVAS_BORDER);

    paint_draw_toolbar();
    app_draw_string(PAINT_PADDING, paint_toolbar_y() + PAINT_TOOLBAR_H + 6,
                    "1-5 colors, C clears", PAINT_TEXT_DIM, PAINT_BG);
}

static void paint_click(int x, int y, int btn) {
    int swatch;

    if ((btn & 0x01) == 0) {
        return;
    }

    swatch = paint_hit_swatch(x, y);
    if (swatch >= 0) {
        current_color = paint_swatches[swatch].color;
        return;
    }

    if (paint_hit_clear(x, y)) {
        paint_clear();
        return;
    }

    x -= paint_canvas_x();
    y -= paint_canvas_y();
    if (x < 0 || y < 0 || x >= PAINT_CANVAS_W || y >= PAINT_CANVAS_H) {
        return;
    }

    paint_stamp_brush(x, y);
}

static void paint_key(char c) {
    if (c == '1') {
        current_color = paint_swatches[0].color;
    } else if (c == '2') {
        current_color = paint_swatches[1].color;
    } else if (c == '3') {
        current_color = paint_swatches[2].color;
    } else if (c == '4') {
        current_color = paint_swatches[3].color;
    } else if (c == '5') {
        current_color = paint_swatches[4].color;
    } else if (c == 'c' || c == 'C') {
        paint_clear();
    }
}

static void paint_close(void) {
}

App paint_app = {
    .title = "Paint",
    .x = 80,
    .y = 80,
    .w = 324,
    .h = 286,
    .bg_color = PAINT_BG,
    .on_init = paint_init,
    .on_draw = paint_draw,
    .on_key = paint_key,
    .on_click = paint_click,
    .on_close = paint_close
};
