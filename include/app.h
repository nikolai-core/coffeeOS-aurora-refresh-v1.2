#ifndef APP_H
#define APP_H

#include <stdint.h>

typedef struct App {
    const char *title;
    int x;
    int y;
    int w;
    int h;
    uint32_t bg_color;
    void (*on_init)(void);
    void (*on_draw)(int win_x, int win_y, int win_w, int win_h);
    void (*on_key)(char c);
    void (*on_click)(int x, int y, int btn);
    void (*on_close)(void);
} App;

void app_draw_rect(int x, int y, int w, int h, uint32_t color);
void app_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);
void app_draw_hline(int x, int y, int w, uint32_t color);
void app_draw_vline(int x, int y, int h, uint32_t color);
void app_clear(uint32_t color);

/* desktop owns this state */
void app_set_draw_context(int client_x, int client_y, int client_w, int client_h);
void app_clear_draw_context(void);

#endif
