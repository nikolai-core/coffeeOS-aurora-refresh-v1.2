#include <stdint.h>

#include "ascii_util.h"
#include "cursor_assets.h"
#include "desktop.h"
#include "format.h"
#include "gfx.h"
#include "multiboot.h"
#include "paging.h"
#include "pit.h"
#include "vmm.h"

#define FONT_W 8u
#define FONT_H 16u
#define MAX_COLS 160u
#define MAX_ROWS 100u
#define MAX_BACKBUFFER_WIDTH (MAX_COLS * FONT_W)
#define MAX_BACKBUFFER_HEIGHT (MAX_ROWS * FONT_H)
#define MAX_BACKBUFFER_BYTES (MAX_BACKBUFFER_WIDTH * MAX_BACKBUFFER_HEIGHT * 4u)
#define FRAMEBUFFER_VIRT_BASE 0xE0000000u

static uint8_t *framebuffer_base;
static uint32_t framebuffer_pitch_bytes;
static uint32_t framebuffer_width_px;
static uint32_t framebuffer_height_px;
static uint32_t framebuffer_bits_per_pixel;
static uint8_t backbuffer[MAX_BACKBUFFER_BYTES];
static uint32_t framebuffer_bytes_per_pixel;
static uint32_t framebuffer_backbuffer_bytes;
static uint32_t dirty_x0;
static uint32_t dirty_y0;
static uint32_t dirty_x1;
static uint32_t dirty_y1;
static uint8_t dirty_region_valid;
static uint8_t cursor_saved_pixels[64u * 64u * 4u];
static int cursor_saved_x;
static int cursor_saved_y;
static uint8_t cursor_drawn;
static int cursor_saved_w;
static int cursor_saved_h;
static const struct CursorAsset *current_cursor;
static const struct CursorFrame *current_cursor_frame;

static uint32_t text_columns;
static uint32_t text_rows;
static uint32_t cursor_column;
static uint32_t cursor_row;
static int output_target = GFX_OUTPUT_FRAMEBUFFER;
static uint8_t current_text_attr = 0x1Fu;
static uint8_t ansi_escape_active;
static uint8_t ansi_escape_bracket_seen;
static char ansi_escape_buf[8];
static uint32_t ansi_escape_len;

static uint32_t screen_cell_codepoint[MAX_COLS * MAX_ROWS];
static uint8_t screen_cell_attr[MAX_COLS * MAX_ROWS];

static const uint32_t vga_palette[16] = {
    0x000000u, 0x0000AAu, 0x00AA00u, 0x00AAAAu,
    0xAA0000u, 0xAA00AAu, 0xAA5500u, 0xAAAAAAu,
    0x555555u, 0x5555FFu, 0x55FF55u, 0x55FFFFu,
    0xFF5555u, 0xFF55FFu, 0xFFFF55u, 0xFFFFFFu
};

static const uint8_t font8x16_terminus[128][16] = {
#include "terminus16.inc"
};

struct SymbolGlyph {
    uint32_t codepoint;
    uint8_t rows[16];
};

static const struct SymbolGlyph symbol_glyphs[] = {
    {0x2713u, {0x00,0x00,0x00,0x01,0x03,0x06,0x46,0x6C,0x38,0x10,0x00,0x00,0x00,0x00,0x00,0x00}},
    {0x2605u, {0x00,0x10,0x10,0x7C,0x38,0xFE,0x38,0x7C,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00}},
    {0x25A0u, {0x00,0x00,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x7E,0x00,0x00,0x00,0x00,0x00,0x00}}
};

static inline __attribute__((always_inline)) uint32_t palette_color(uint8_t nibble) {
    return vga_palette[nibble & 0x0Fu];
}

static inline __attribute__((always_inline)) uint32_t fg_color(uint8_t attr) {
    return palette_color((uint8_t)(attr & 0x0Fu));
}

static inline __attribute__((always_inline)) uint32_t bg_color(uint8_t attr) {
    return palette_color((uint8_t)((attr >> 4) & 0x0Fu));
}

static inline __attribute__((always_inline)) uint32_t packed_pixel32(uint32_t color) {
    return color & 0x00FFFFFFu;
}

static inline __attribute__((always_inline)) uint8_t *backbuffer_pixel_ptr(uint32_t x, uint32_t y) {
    return backbuffer + (y * framebuffer_pitch_bytes) + (x * framebuffer_bytes_per_pixel);
}

static inline __attribute__((always_inline)) void store_backbuffer_pixel(uint8_t *p, uint32_t color) {
    if (framebuffer_bits_per_pixel == 32u) {
        *(uint32_t *)p = packed_pixel32(color);
    } else if (framebuffer_bits_per_pixel == 24u) {
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
    }
}

static inline __attribute__((always_inline)) void store_framebuffer_pixel(uint8_t *p, uint32_t color) {
    if (framebuffer_bits_per_pixel == 32u) {
        *(uint32_t *)p = packed_pixel32(color);
    } else if (framebuffer_bits_per_pixel == 24u) {
        p[0] = (uint8_t)(color & 0xFFu);
        p[1] = (uint8_t)((color >> 8) & 0xFFu);
        p[2] = (uint8_t)((color >> 16) & 0xFFu);
    }
}

void gfx_mark_dirty(int x, int y, int w, int h) {
    uint32_t x0;
    uint32_t y0;
    uint32_t x1;
    uint32_t y1;

    if (w <= 0 || h <= 0) {
        return;
    }
    if (framebuffer_base == 0 || framebuffer_width_px == 0u || framebuffer_height_px == 0u) {
        return;
    }

    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    x0 = (uint32_t)x;
    y0 = (uint32_t)y;
    x1 = x0 + (uint32_t)w;
    y1 = y0 + (uint32_t)h;

    if (x0 >= framebuffer_width_px || y0 >= framebuffer_height_px) {
        return;
    }
    if (x1 > framebuffer_width_px) {
        x1 = framebuffer_width_px;
    }
    if (y1 > framebuffer_height_px) {
        y1 = framebuffer_height_px;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }

    if (!dirty_region_valid) {
        dirty_x0 = x0;
        dirty_y0 = y0;
        dirty_x1 = x1;
        dirty_y1 = y1;
        dirty_region_valid = 1u;
        return;
    }

    if (x0 < dirty_x0) dirty_x0 = x0;
    if (y0 < dirty_y0) dirty_y0 = y0;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

void gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint8_t *p;

    if (x >= framebuffer_width_px || y >= framebuffer_height_px || framebuffer_base == 0) {
        return;
    }

    p = backbuffer_pixel_ptr(x, y);
    store_backbuffer_pixel(p, color);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t y_end = y + h;

    if (w == 0u || h == 0u) {
        return;
    }

    gfx_mark_dirty((int)x, (int)y, (int)w, (int)h);

    if (framebuffer_bits_per_pixel == 32u) {
        uint32_t packed = packed_pixel32(color);
        uint32_t py = y;

        while (py < y_end) {
            uint32_t *dst = (uint32_t *)(void *)(backbuffer + (py * framebuffer_pitch_bytes) + (x * 4u));
            uint32_t *end = dst + w;

            while ((uint32_t)(end - dst) >= 4u) {
                dst[0] = packed;
                dst[1] = packed;
                dst[2] = packed;
                dst[3] = packed;
                dst += 4;
            }
            while (dst < end) {
                *dst++ = packed;
            }
            py++;
        }
        return;
    }

    {
        uint32_t py = y;
        while (py < y_end) {
            uint32_t px = x;
            uint32_t x_end = x + w;
            while (px < x_end) {
                uint8_t *dst = backbuffer_pixel_ptr(px, py);
                store_backbuffer_pixel(dst, color);
                px++;
            }
            py++;
        }
    }
}

static void draw_ascii_char_at(int x, int y, const uint8_t *glyph, uint32_t fg, uint32_t bg) {
    uint32_t row = 0u;

    fill_rect((uint32_t)x, (uint32_t)y, FONT_W, FONT_H, bg);
    while (row < FONT_H) {
        uint8_t bits = glyph[row];
        uint32_t col = 0u;

        while (col < FONT_W) {
            if ((bits & (1u << (7u - col))) != 0u) {
                gfx_put_pixel((uint32_t)x + col, (uint32_t)y + row, fg);
            }
            col++;
        }
        row++;
    }
}

void gfx_draw_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0 || y < 0) {
        return;
    }
    fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, color);
}

void gfx_draw_hline(int x, int y, int w, uint32_t color) {
    gfx_draw_rect(x, y, w, 1, color);
}

void gfx_draw_vline(int x, int y, int h, uint32_t color) {
    gfx_draw_rect(x, y, 1, h, color);
}

void gfx_draw_rect_outline(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) {
        return;
    }

    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, y + h - 1, w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline(x + w - 1, y, h, color);
}

void gfx_draw_char_at(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (x < 0 || y < 0) {
        return;
    }

    draw_ascii_char_at(x, y, font8x16_terminus[(uint8_t)c], fg, bg);
}

void gfx_draw_string_at(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    const uint8_t (*glyphs)[16] = font8x16_terminus;
    int dx = x;
    uint32_t width = 0u;

    while (s[width] != '\0') {
        width++;
    }
    if (width != 0u) {
        gfx_mark_dirty(x, y, (int)(width * FONT_W), (int)FONT_H);
    }

    while (*s != '\0') {
        draw_ascii_char_at(dx, y, glyphs[(uint8_t)*s], fg, bg);
        dx += (int)FONT_W;
        s++;
    }
}

void gfx_fill_screen(uint32_t color) {
    fill_rect(0u, 0u, framebuffer_width_px, framebuffer_height_px, color);
}

int gfx_get_width(void) {
    return (int)framebuffer_width_px;
}

int gfx_get_height(void) {
    return (int)framebuffer_height_px;
}

void gfx_draw_cursor(int x, int y) {
    uint32_t row;
    uint32_t col;
    int draw_x;
    int draw_y;
    const struct CursorFrame *frame;

    if (framebuffer_base == 0 || framebuffer_backbuffer_bytes == 0u) {
        return;
    }
    if (current_cursor == (const struct CursorAsset *)0 || current_cursor->frame_count == 0u
        || current_cursor->frames == (const struct CursorFrame *)0) {
        return;
    }

    if (current_cursor->frame_count == 1u) {
        frame = &current_cursor->frames[0];
    } else {
        uint32_t total = 0u;
        uint32_t tick = get_ticks();
        uint32_t i;

        for (i = 0u; i < current_cursor->frame_count; i++) {
            uint32_t delay = current_cursor->frames[i].delay_ticks;
            total += (delay == 0u) ? 1u : delay;
        }
        if (total == 0u) {
            frame = &current_cursor->frames[0];
        } else {
            uint32_t pos = tick % total;
            frame = &current_cursor->frames[0];
            for (i = 0u; i < current_cursor->frame_count; i++) {
                uint32_t delay = current_cursor->frames[i].delay_ticks;
                if (delay == 0u) {
                    delay = 1u;
                }
                if (pos < delay) {
                    frame = &current_cursor->frames[i];
                    break;
                }
                pos -= delay;
            }
        }
    }

    if (frame->pixels == (const uint32_t *)0
        || frame->width <= 0 || frame->height <= 0
        || frame->width > 64 || frame->height > 64) {
        return;
    }

    draw_x = x - frame->hotspot_x;
    draw_y = y - frame->hotspot_y;

    for (row = 0; row < (uint32_t)frame->height; row++) {
        for (col = 0; col < (uint32_t)frame->width; col++) {
            int px_i = draw_x + (int)col;
            int py_i = draw_y + (int)row;
            uint32_t src = frame->pixels[(row * (uint32_t)frame->width) + col];
            uint32_t a = (src >> 24) & 0xFFu;
            uint32_t saved_offset = ((row * (uint32_t)frame->width) + col) * 4u;

            if (saved_offset + framebuffer_bytes_per_pixel > sizeof(cursor_saved_pixels)) {
                continue;
            }
            if (px_i >= 0 && py_i >= 0
                && (uint32_t)px_i < framebuffer_width_px && (uint32_t)py_i < framebuffer_height_px) {
                uint32_t px = (uint32_t)px_i;
                uint32_t py = (uint32_t)py_i;
                uint8_t *dst = backbuffer_pixel_ptr(px, py);
                uint32_t i;

                for (i = 0u; i < framebuffer_bytes_per_pixel; i++) {
                    cursor_saved_pixels[saved_offset + i] = dst[i];
                }
                for (; i < 4u; i++) {
                    cursor_saved_pixels[saved_offset + i] = 0u;
                }

                if (a != 0u) {
                    uint32_t dst_color = 0u;
                    if (framebuffer_bits_per_pixel == 32u) {
                        dst_color = *(uint32_t *)dst & 0x00FFFFFFu;
                    } else if (framebuffer_bits_per_pixel == 24u) {
                        dst_color = (uint32_t)dst[0] | ((uint32_t)dst[1] << 8) | ((uint32_t)dst[2] << 16);
                    }

                    if (a != 0xFFu) {
                        uint32_t sr = (src >> 16) & 0xFFu;
                        uint32_t sg = (src >> 8) & 0xFFu;
                        uint32_t sb = src & 0xFFu;
                        uint32_t dr = (dst_color >> 16) & 0xFFu;
                        uint32_t dg = (dst_color >> 8) & 0xFFu;
                        uint32_t db = dst_color & 0xFFu;
                        uint32_t r = ((sr * a) + (dr * (255u - a))) / 255u;
                        uint32_t g = ((sg * a) + (dg * (255u - a))) / 255u;
                        uint32_t b = ((sb * a) + (db * (255u - a))) / 255u;
                        store_backbuffer_pixel(dst, (r << 16) | (g << 8) | b);
                    } else {
                        store_backbuffer_pixel(dst, src & 0x00FFFFFFu);
                    }
                }
            } else {
                uint32_t i;
                for (i = 0u; i < 4u; i++) {
                    cursor_saved_pixels[saved_offset + i] = 0u;
                }
            }
        }
    }

    cursor_saved_x = draw_x;
    cursor_saved_y = draw_y;
    cursor_saved_w = frame->width;
    cursor_saved_h = frame->height;
    current_cursor_frame = frame;
    cursor_drawn = 1u;
    gfx_mark_dirty(draw_x, draw_y, frame->width, frame->height);
}

void gfx_erase_cursor(int x, int y) {
    uint32_t row;
    uint32_t col;

    (void)x;
    (void)y;

    if (!cursor_drawn || framebuffer_base == 0 || framebuffer_backbuffer_bytes == 0u) {
        return;
    }

    for (row = 0; row < (uint32_t)cursor_saved_h; row++) {
        for (col = 0; col < (uint32_t)cursor_saved_w; col++) {
            int px_i = cursor_saved_x + (int)col;
            int py_i = cursor_saved_y + (int)row;
            uint32_t saved_offset = ((row * (uint32_t)cursor_saved_w) + col) * 4u;

            if (px_i >= 0 && py_i >= 0
                && (uint32_t)px_i < framebuffer_width_px && (uint32_t)py_i < framebuffer_height_px) {
                uint32_t px = (uint32_t)px_i;
                uint32_t py = (uint32_t)py_i;
                uint8_t *dst = backbuffer_pixel_ptr(px, py);
                uint32_t i;

                for (i = 0u; i < framebuffer_bytes_per_pixel; i++) {
                    dst[i] = cursor_saved_pixels[saved_offset + i];
                }
            }
        }
    }

    gfx_mark_dirty(cursor_saved_x, cursor_saved_y, cursor_saved_w, cursor_saved_h);
    cursor_drawn = 0u;
}

void gfx_present(void) {
    if (framebuffer_base == 0 || framebuffer_backbuffer_bytes == 0u || !dirty_region_valid) {
        return;
    }

    if (framebuffer_bits_per_pixel == 32u) {
        uint32_t y = dirty_y0;
        uint32_t byte_offset = dirty_x0 * 4u;
        uint32_t width_words = dirty_x1 - dirty_x0;

        while (y < dirty_y1) {
            uint32_t *src = (uint32_t *)(void *)(backbuffer + (y * framebuffer_pitch_bytes) + byte_offset);
            uint32_t *dst = (uint32_t *)(void *)(framebuffer_base + (y * framebuffer_pitch_bytes) + byte_offset);
            uint32_t *end = src + width_words;

            while ((uint32_t)(end - src) >= 4u) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
                src += 4;
                dst += 4;
            }
            while (src < end) {
                *dst++ = *src++;
            }
            y++;
        }
    } else {
        uint32_t y = dirty_y0;
        uint32_t byte_offset = dirty_x0 * framebuffer_bytes_per_pixel;
        uint32_t width_bytes = (dirty_x1 - dirty_x0) * framebuffer_bytes_per_pixel;

        while (y < dirty_y1) {
            uint8_t *src = backbuffer + (y * framebuffer_pitch_bytes) + byte_offset;
            uint8_t *dst = framebuffer_base + (y * framebuffer_pitch_bytes) + byte_offset;
            uint8_t *end = src + width_bytes;

            while (src < end) {
                *dst++ = *src++;
            }
            y++;
        }
    }

    dirty_region_valid = 0u;
}

void gfx_set_cursor(const char *name) {
    const struct CursorAsset *cursor = (const struct CursorAsset *)0;

    if (name != (const char *)0 && name[0] != '\0') {
        cursor = cursor_assets_find(name);
    }
    if (cursor == (const struct CursorAsset *)0) {
        cursor = cursor_assets_default();
    }
    current_cursor = cursor;
}

static void draw_braille_cell(uint32_t x, uint32_t y, uint8_t dots, uint8_t attr) {
    uint32_t fg = fg_color(attr);
    uint32_t bg = bg_color(attr);
    uint32_t positions[8][2] = {
        {1, 1}, {1, 4}, {1, 7}, {1, 10},
        {5, 1}, {5, 4}, {5, 7}, {5, 10}
    };
    uint32_t i;

    fill_rect(x, y, FONT_W, FONT_H, bg);

    for (i = 0; i < 8u; i++) {
        uint32_t dx;
        uint32_t dy;

        if ((dots & (1u << i)) == 0u) {
            continue;
        }

        for (dy = 0; dy < 2u; dy++) {
            for (dx = 0; dx < 2u; dx++) {
                gfx_put_pixel(x + positions[i][0] + dx, y + positions[i][1] + dy, fg);
            }
        }
    }
}

void gfx_draw_braille(uint8_t dots) {
    uint32_t idx;

    if (cursor_column >= text_columns) {
        cursor_column = 0;
        cursor_row++;
    }
    if (cursor_row >= text_rows) {
        cursor_row = text_rows - 1u;
    }

    idx = cursor_row * text_columns + cursor_column;
    screen_cell_codepoint[idx] = 0x2800u + dots;
    screen_cell_attr[idx] = current_text_attr;

    draw_braille_cell(cursor_column * FONT_W, cursor_row * FONT_H, dots, current_text_attr);
    cursor_column++;
}

static const uint8_t *find_symbol(uint32_t codepoint) {
    uint32_t i;
    for (i = 0; i < (uint32_t)(sizeof(symbol_glyphs) / sizeof(symbol_glyphs[0])); i++) {
        if (symbol_glyphs[i].codepoint == codepoint) {
            return symbol_glyphs[i].rows;
        }
    }
    return (const uint8_t *)0;
}

static void draw_glyph_rows(uint32_t x, uint32_t y, const uint8_t *rows, uint8_t attr, uint32_t rows_count) {
    uint32_t fg = fg_color(attr);
    uint32_t bg = bg_color(attr);
    uint32_t row;

    fill_rect(x, y, FONT_W, FONT_H, bg);

    for (row = 0; row < rows_count && row < FONT_H; row++) {
        uint8_t bits = rows[row];
        uint32_t col;
        for (col = 0; col < FONT_W; col++) {
            if ((bits & (1u << (7u - col))) != 0u) {
                gfx_put_pixel(x + col, y + row, fg);
            }
        }
    }
}

static void draw_ascii(uint32_t x, uint32_t y, uint8_t c, uint8_t attr) {
    draw_glyph_rows(x, y, font8x16_terminus[c], attr, 16);
}

static void draw_codepoint_cell(uint32_t col, uint32_t row, uint32_t codepoint, uint8_t attr) {
    uint32_t x = col * FONT_W;
    uint32_t y = row * FONT_H;
    const uint8_t *symbol;

    if (codepoint >= 0x2800u && codepoint <= 0x28FFu) {
        draw_braille_cell(x, y, (uint8_t)(codepoint - 0x2800u), attr);
        return;
    }

    if (codepoint < 128u) {
        draw_ascii(x, y, (uint8_t)codepoint, attr);
        return;
    }

    symbol = find_symbol(codepoint);
    if (symbol != 0) {
        draw_glyph_rows(x, y, symbol, attr, 16);
        return;
    }

    draw_ascii(x, y, (uint8_t)'?', attr);
}

static void redraw_all(void) {
    uint32_t row;
    uint32_t col;
    for (row = 0; row < text_rows; row++) {
        for (col = 0; col < text_columns; col++) {
            uint32_t idx = row * text_columns + col;
            draw_codepoint_cell(col, row, screen_cell_codepoint[idx], screen_cell_attr[idx]);
        }
    }
}

static void clear_cells(uint8_t attr) {
    uint32_t i;
    for (i = 0; i < text_columns * text_rows; i++) {
        screen_cell_codepoint[i] = ' ';
        screen_cell_attr[i] = attr;
    }
}

void gfx_set_attr(uint8_t attr) {
    current_text_attr = attr;
}

uint8_t gfx_get_attr(void) {
    return current_text_attr;
}

void gfx_repaint_attr(uint8_t attr) {
    uint32_t i;
    current_text_attr = attr;
    for (i = 0; i < text_columns * text_rows; i++) {
        screen_cell_attr[i] = attr;
    }
    redraw_all();
}

void gfx_clear(void) {
    if (output_target == GFX_OUTPUT_GUI_TERMINAL) {
        desktop_terminal_clear();
        return;
    }

    cursor_column = 0;
    cursor_row = 0;
    current_text_attr = 0x1Fu;
    clear_cells(current_text_attr);
    redraw_all();
}

static void scroll_up(void) {
    uint32_t row;
    uint32_t col;

    for (row = 1u; row < text_rows; row++) {
        for (col = 0u; col < text_columns; col++) {
            uint32_t dst = (row - 1u) * text_columns + col;
            uint32_t src = row * text_columns + col;
            screen_cell_codepoint[dst] = screen_cell_codepoint[src];
            screen_cell_attr[dst] = screen_cell_attr[src];
        }
    }

    for (col = 0u; col < text_columns; col++) {
        uint32_t idx = (text_rows - 1u) * text_columns + col;
        screen_cell_codepoint[idx] = ' ';
        screen_cell_attr[idx] = current_text_attr;
    }

    redraw_all();
}

static void put_codepoint(uint32_t codepoint) {
    uint32_t idx;

    if (codepoint == '\n') {
        cursor_column = 0;
        cursor_row++;
        if (cursor_row >= text_rows) {
            cursor_row = text_rows - 1u;
            scroll_up();
        }
        return;
    }

    if (codepoint == '\b') {
        if (cursor_column > 0u) {
            cursor_column--;
        } else if (cursor_row > 0u) {
            cursor_row--;
            cursor_column = text_columns - 1u;
        } else {
            return;
        }

        idx = cursor_row * text_columns + cursor_column;
        screen_cell_codepoint[idx] = ' ';
        screen_cell_attr[idx] = current_text_attr;
        draw_codepoint_cell(cursor_column, cursor_row, ' ', current_text_attr);
        return;
    }

    if (cursor_column >= text_columns) {
        cursor_column = 0;
        cursor_row++;
    }
    if (cursor_row >= text_rows) {
        cursor_row = text_rows - 1u;
        scroll_up();
    }

    idx = cursor_row * text_columns + cursor_column;
    screen_cell_codepoint[idx] = codepoint;
    screen_cell_attr[idx] = current_text_attr;
    draw_codepoint_cell(cursor_column, cursor_row, codepoint, current_text_attr);

    cursor_column++;
}

static void reset_ansi_escape(void) {
    ansi_escape_active = 0u;
    ansi_escape_bracket_seen = 0u;
    ansi_escape_len = 0u;
}

/* enough ANSI for the user shell */
static int handle_ansi_byte(uint8_t c) {
    if (!ansi_escape_active) {
        if (c == 0x1Bu) {
            ansi_escape_active = 1u;
            ansi_escape_bracket_seen = 0u;
            ansi_escape_len = 0u;
            return 1;
        }
        return 0;
    }

    if (!ansi_escape_bracket_seen) {
        if (c == '[') {
            ansi_escape_bracket_seen = 1u;
            return 1;
        }
        reset_ansi_escape();
        return 1;
    }

    if ((c >= '0' && c <= '9') || c == ';') {
        if (ansi_escape_len + 1u < sizeof(ansi_escape_buf)) {
            ansi_escape_buf[ansi_escape_len++] = (char)c;
        } else {
            reset_ansi_escape();
        }
        return 1;
    }

    ansi_escape_buf[ansi_escape_len] = '\0';

    if (c == 'J' && ascii_streq(ansi_escape_buf, "2")) {
        gfx_clear();
    } else if (c == 'H' && (ansi_escape_len == 0u || ascii_streq(ansi_escape_buf, "1;1"))) {
        cursor_column = 0u;
        cursor_row = 0u;
    }

    reset_ansi_escape();
    return 1;
}

static int utf8_next(const char **s, uint32_t *cp) {
    const uint8_t *p = (const uint8_t *)*s;

    if (*p == 0u) {
        return 0;
    }

    if ((p[0] & 0x80u) == 0u) {
        *cp = p[0];
        *s += 1;
        return 1;
    }

    if ((p[0] & 0xE0u) == 0xC0u && (p[1] & 0xC0u) == 0x80u) {
        *cp = ((uint32_t)(p[0] & 0x1Fu) << 6) | (uint32_t)(p[1] & 0x3Fu);
        *s += 2;
        return 1;
    }

    if ((p[0] & 0xF0u) == 0xE0u && (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u) {
        *cp = ((uint32_t)(p[0] & 0x0Fu) << 12)
            | ((uint32_t)(p[1] & 0x3Fu) << 6)
            | (uint32_t)(p[2] & 0x3Fu);
        *s += 3;
        return 1;
    }

    *cp = '?';
    *s += 1;
    return 1;
}

void gfx_putc(char c) {
    if (output_target == GFX_OUTPUT_GUI_TERMINAL) {
        char text[2];

        text[0] = c;
        text[1] = '\0';
        terminal_print(text);
        return;
    }

    if (handle_ansi_byte((uint8_t)c)) {
        gfx_present();
        return;
    }
    put_codepoint((uint8_t)c);
    gfx_present();
}

void gfx_print(const char *msg) {
    if (output_target == GFX_OUTPUT_GUI_TERMINAL) {
        terminal_print(msg);
        return;
    }

    const char *p = msg;
    uint32_t cp;

    while (utf8_next(&p, &cp)) {
        if (cp < 128u) {
            if (handle_ansi_byte((uint8_t)cp)) {
                continue;
            }
        } else if (ansi_escape_active) {
            reset_ansi_escape();
        }

        put_codepoint(cp);
    }

    gfx_present();
}

void gfx_write_hex(uint32_t value) {
    char buf[11];
    format_hex_u32(buf, value);
    gfx_print(buf);
}

void gfx_set_output_target(int target) {
    output_target = target;
}

void gfx_init(uint32_t multiboot_info_addr) {
    struct MultibootInfo *mbi = (struct MultibootInfo *)multiboot_info_addr;
    uint32_t fb_phys;
    uint32_t fb_map_start;
    uint32_t fb_map_end;
    uint32_t fb_map_size;
    uint32_t fb_map_off;
    uint32_t i;

    if ((mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) == 0u) {
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    fb_phys = (uint32_t)mbi->framebuffer_addr;
    framebuffer_pitch_bytes = mbi->framebuffer_pitch;
    framebuffer_width_px = mbi->framebuffer_width;
    framebuffer_height_px = mbi->framebuffer_height;
    framebuffer_bits_per_pixel = mbi->framebuffer_bpp;
    framebuffer_bytes_per_pixel = framebuffer_bits_per_pixel / 8u;
    framebuffer_backbuffer_bytes = framebuffer_pitch_bytes * framebuffer_height_px;
    dirty_region_valid = 0u;

    fb_map_start = fb_phys & 0xFFFFF000u;
    fb_map_end = (fb_phys + framebuffer_backbuffer_bytes + PAGE_SIZE - 1u) & 0xFFFFF000u;
    fb_map_size = fb_map_end - fb_map_start;
    fb_map_off = fb_phys - fb_map_start;

    for (i = 0u; i < fb_map_size; i += PAGE_SIZE) {
        if (!vmm_map_page(FRAMEBUFFER_VIRT_BASE + i, fb_map_start + i,
                          VMM_FLAG_PRESENT | VMM_FLAG_WRITE)) {
            for (;;) {
                __asm__ volatile ("cli; hlt");
            }
        }
    }
    framebuffer_base = (uint8_t *)(uintptr_t)(FRAMEBUFFER_VIRT_BASE + fb_map_off);

    if ((framebuffer_bits_per_pixel != 24u && framebuffer_bits_per_pixel != 32u) || framebuffer_base == 0) {
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }
    if (framebuffer_backbuffer_bytes > MAX_BACKBUFFER_BYTES) {
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    text_columns = framebuffer_width_px / FONT_W;
    text_rows = framebuffer_height_px / FONT_H;

    if (text_columns > MAX_COLS) {
        text_columns = MAX_COLS;
    }
    if (text_rows > MAX_ROWS) {
        text_rows = MAX_ROWS;
    }
    if (text_columns == 0u) {
        text_columns = 1u;
    }
    if (text_rows == 0u) {
        text_rows = 1u;
    }

    for (i = 0u; i < framebuffer_backbuffer_bytes; i++) {
        backbuffer[i] = 0u;
    }

    current_cursor = cursor_assets_default();
    current_cursor_frame = (const struct CursorFrame *)0;
    cursor_saved_w = 0;
    cursor_saved_h = 0;
    cursor_drawn = 0u;

    gfx_clear();
    gfx_present();
}
