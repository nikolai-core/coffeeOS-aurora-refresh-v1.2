#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define GFX_OUTPUT_FRAMEBUFFER 0
#define GFX_OUTPUT_GUI_TERMINAL 1

void gfx_init(uint32_t multiboot_info_addr);
void gfx_set_output_target(int target);
void gfx_set_attr(uint8_t attr);
uint8_t gfx_get_attr(void);
void gfx_repaint_attr(uint8_t attr);
void gfx_clear(void);
void gfx_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_rect_outline(int x, int y, int w, int h, uint32_t color);
void gfx_draw_hline(int x, int y, int w, uint32_t color);
void gfx_draw_vline(int x, int y, int h, uint32_t color);
void gfx_draw_char_at(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_string_at(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void gfx_fill_screen(uint32_t color);
int gfx_get_width(void);
int gfx_get_height(void);
void gfx_mark_dirty(int x, int y, int w, int h);
void gfx_set_cursor(const char *name);
void gfx_draw_cursor(int x, int y);
void gfx_erase_cursor(int x, int y);
void gfx_present(void);
/* Non-zero when the cursor sprite has been composited into the backbuffer
   and has not yet been erased. desktop.c reads this to decide whether a
   cursor redraw + present is needed on idle frames. */
extern uint8_t cursor_drawn;
void gfx_putc(char c);
void gfx_print(const char *msg);
void gfx_write_hex(uint32_t value);
void gfx_draw_braille(uint8_t dots);

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    gfx_put_pixel(x, y, color);
}

static inline void draw_braille(uint8_t dots) {
    gfx_draw_braille(dots);
}

#endif
