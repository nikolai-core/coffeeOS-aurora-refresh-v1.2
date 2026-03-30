#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdint.h>

struct App;
struct Window;

typedef void (*WindowDrawCallback)(struct Window *window);

struct Window {
    int x;
    int y;
    int width;
    int height;
    char title[64];
    uint32_t bg_color;
    int focused;
    int content_dirty;
    int hidden;
    int minimized;
    int maximized;
    int restore_x;
    int restore_y;
    int restore_width;
    int restore_height;
    struct App *app;
    WindowDrawCallback on_draw;
};

struct Window *desktop_create_window(int x, int y, int width, int height,
                                     const char *title, uint32_t bg_color,
                                     WindowDrawCallback on_draw, struct App *app);
void desktop_move_window(struct Window *window, int x, int y);
void desktop_draw_window(struct Window *window);
void desktop_terminal_clear(void);
void terminal_print(const char *s);
void desktop_run(void);
int desktop_is_running(void);
void desktop_request_kernel_shell(void);

#endif
