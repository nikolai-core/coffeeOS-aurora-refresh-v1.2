#ifndef APP_H
#define APP_H

#include <stdint.h>

struct App;

#define APP_FONT_WIDTH 8
#define APP_FONT_HEIGHT 16

#define APP_FLAG_RESIZABLE       (1u << 0)
#define APP_FLAG_SINGLE_INSTANCE (1u << 1)
#define APP_FLAG_SYSTEM          (1u << 2)
#define APP_FLAG_ANIMATED        (1u << 3)

typedef void (*AppInitCallback)(void);
typedef void (*AppDrawCallback)(int win_x, int win_y, int win_w, int win_h);
typedef void (*AppKeyCallback)(char c);
typedef void (*AppClickCallback)(int x, int y, int btn);
typedef void (*AppCloseCallback)(void);
typedef void (*AppRedrawCallback)(struct App *app);

typedef struct App {
    const char *title;
    int x;
    int y;
    int w;
    int h;
    uint32_t bg_color;
    AppInitCallback on_init;
    AppDrawCallback on_draw;
    AppKeyCallback on_key;
    AppClickCallback on_click;
    AppCloseCallback on_close;
    const char *id;
    uint32_t flags;
    int min_w;
    int min_h;
} App;

void app_draw_rect(int x, int y, int w, int h, uint32_t color);
void app_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void app_draw_hline(int x, int y, int w, uint32_t color);
void app_draw_vline(int x, int y, int h, uint32_t color);
void app_draw_border(int x, int y, int w, int h, uint32_t color);
void app_clear(uint32_t color);
void app_draw_button(int x, int y, int w, int h, const char *label,
                     uint32_t bg, uint32_t border, uint32_t fg);

int app_font_width(void);
int app_font_height(void);
int app_text_width(const char *s);
int app_point_in_rect(int px, int py, int x, int y, int w, int h);
int app_clamp_int(int value, int min_value, int max_value);
void app_copy_string(char *dst, uint32_t dst_len, const char *src);
void app_append_string(char *dst, uint32_t dst_len, const char *src);
void app_append_u32(char *dst, uint32_t dst_len, uint32_t value);
void app_format_size(uint32_t size, char *out, uint32_t out_len);
uint32_t app_ticks(void);
uint32_t app_blend_color(uint32_t a, uint32_t b, uint32_t step, uint32_t steps);
int app_anim_saw(uint32_t period_ticks, int max_value);
int app_anim_pingpong(uint32_t period_ticks, int max_value);

/* desktop owns this state */
void app_set_draw_context_for(App *app, int client_x, int client_y, int client_w, int client_h);
void app_set_draw_context(int client_x, int client_y, int client_w, int client_h);
void app_clear_draw_context(void);
App *app_current(void);
int app_client_width(void);
int app_client_height(void);
void app_set_redraw_callback(AppRedrawCallback callback);
void app_request_redraw(App *app);
void app_request_current_redraw(void);

#endif
