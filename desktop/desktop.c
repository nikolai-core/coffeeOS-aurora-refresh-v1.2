#include <stdint.h>

#include "app.h"
#include "app_registry.h"
#include "ascii_util.h"
#include "desktop.h"
#include "fat32.h"
#include "gfx.h"
#include "icon_assets.h"
#include "io.h"
#include "keyboard.h"
#include "kshell.h"
#include "mouse.h"
#include "pit.h"
#include "pmm.h"
#include "ramdisk.h"
#include "serial.h"
#include "speaker.h"
#include "synth.h"
#include "x86_cpu.h"

#define DESKTOP_BACKGROUND_COLOR 0x2B5F7Cu
#define DESKTOP_WINDOW_MAX 24u
#define DESKTOP_TITLEBAR_HEIGHT 20
#define DESKTOP_BORDER_WIDTH 1
#define DESKTOP_TASKBAR_HEIGHT 24
#define DESKTOP_WINDOW_MIN_WIDTH 120
#define DESKTOP_WINDOW_MIN_HEIGHT 60
#define TERMINAL_HISTORY_LINES 200u
#define TERMINAL_HISTORY_COLS 80u
#define TERMINAL_INPUT_MAX 128u
#define TERMINAL_PROMPT "> "
#define TERMINAL_COMMAND_PROMPT "coffeeos> "
#define DESKTOP_INFO_LINES 5u
#define DESKTOP_INFO_PADDING 6
#define DESKTOP_INFO_LINE_SPACING 18
#define DESKTOP_INFO_WINDOW_WIDTH ((50 * 8) + (DESKTOP_INFO_PADDING * 2))
#define DESKTOP_INFO_WINDOW_HEIGHT 132
#define WINDOW_BUTTON_WIDTH 18
#define WINDOW_BUTTON_HEIGHT 14
#define WINDOW_BUTTON_GAP 2
#define WINDOW_BUTTON_RIGHT_MARGIN 4
#define WINDOW_BUTTON_COUNT 3
#define TASKBAR_BUTTON_PADDING_X 6
#define TASKBAR_BUTTON_GAP 4
#define TASKBAR_BUTTON_MIN_WIDTH 56
#define TASKBAR_START_BUTTON_WIDTH 88
#define START_MENU_WIDTH 240
#define START_MENU_ENTRY_HEIGHT 18
#define START_MENU_PADDING 6
#define DESKTOP_ICON_TILE_SIZE 48
#define DESKTOP_ICON_LABEL_HEIGHT 16
#define DESKTOP_ICON_CELL_WIDTH 120
#define DESKTOP_ICON_CELL_HEIGHT 86
#define DESKTOP_ICON_PADDING_X 16
#define DESKTOP_ICON_PADDING_Y 16
#define DESKTOP_ICON_DOUBLE_CLICK_TICKS 30u
#define DESKTOP_ICON_DRAG_THRESHOLD 4
#define DESKTOP_ICON_INSTANCE_MAX 64
#define DESKTOP_BUILTIN_LAUNCHER_COUNT 3
#define DESKTOP_START_MENU_ACTION_COUNT 3
#define TERMINAL_SCROLLBAR_WIDTH 4
#define TERMINAL_SCROLL_STEP 3
#define DEBUG_MOUSE 0

#if DEBUG_MOUSE
static void desktop_debug_hex(const char *label, uint32_t value) {
    serial_print(label);
    serial_write_hex(value);
    serial_print("\n");
}
#else
static void desktop_debug_hex(const char *label, uint32_t value) {
    (void)label;
    (void)value;
}
#endif

enum DesktopWindowButton {
    DESKTOP_WINDOW_BUTTON_NONE = 0,
    DESKTOP_WINDOW_BUTTON_MINIMIZE = 1,
    DESKTOP_WINDOW_BUTTON_MAXIMIZE = 2,
    DESKTOP_WINDOW_BUTTON_CLOSE = 3
};

enum DesktopResizeEdge {
    DESKTOP_RESIZE_NONE = 0,
    DESKTOP_RESIZE_LEFT = 1,
    DESKTOP_RESIZE_RIGHT = 2,
    DESKTOP_RESIZE_TOP = 4,
    DESKTOP_RESIZE_BOTTOM = 8
};

struct TerminalState {
    char lines[TERMINAL_HISTORY_LINES][TERMINAL_HISTORY_COLS + 1u];
    char input[TERMINAL_INPUT_MAX];
    uint32_t line_count;
    uint32_t cursor_pos;
    uint32_t input_len;
    uint32_t scroll_offset;
};

struct DesktopIcon {
    int launcher_index;
    int x;
    int y;
    int in_use;
};

static struct Window desktop_windows[DESKTOP_WINDOW_MAX];
static struct Window *desktop_draw_order[DESKTOP_WINDOW_MAX];
static struct Window *desktop_terminal_window;
static struct Window *desktop_info_window;
static struct Window *desktop_clock_window;
static uint32_t desktop_window_count;
static struct Window *desktop_drag_window;
static int desktop_drag_offset_x;
static int desktop_drag_offset_y;
static struct Window *desktop_resize_window;
static int desktop_resize_edges;
static int desktop_resize_start_x;
static int desktop_resize_start_y;
static int desktop_resize_orig_x;
static int desktop_resize_orig_y;
static int desktop_resize_orig_w;
static int desktop_resize_orig_h;
static int desktop_cursor_x;
static int desktop_cursor_y;
static int desktop_last_buttons;
static struct Window *desktop_pressed_window;
static struct Window *desktop_pressed_taskbar_window;
static int desktop_pressed_button;
static struct Window *desktop_hover_window;
static struct Window *desktop_hover_taskbar_window;
static struct Window *desktop_hover_app_window;
static int desktop_hover_button;
static int desktop_taskbar_dirty;
static int desktop_scene_dirty;
static int desktop_running;
static int desktop_exit_to_shell;
static int desktop_start_menu_open;
static int desktop_focused_icon;
static int desktop_last_icon_click_index;
static uint32_t desktop_last_icon_click_tick;
static struct DesktopIcon desktop_icons[DESKTOP_ICON_INSTANCE_MAX];
static int desktop_icon_count;
static int desktop_pressed_icon;
static int desktop_pressed_icon_start_x;
static int desktop_pressed_icon_start_y;
static int desktop_pressed_icon_copy;
static int desktop_drag_icon;
static int desktop_drag_icon_offset_x;
static int desktop_drag_icon_offset_y;
static int desktop_drag_icon_copy;
static struct Window *desktop_app_windows[MAX_APPS];

static struct TerminalState desktop_terminal_state;
static char info_lines[DESKTOP_INFO_LINES][48];
static char cpu_brand[49];
static uint32_t info_last_refresh_sec = 0xFFFFFFFFu;
static uint32_t desktop_last_clock_second = 0xFFFFFFFFu;
static uint32_t desktop_last_info_bucket = 0xFFFFFFFFu;
static uint32_t desktop_busy_until_tick;

static void desktop_icon_key_from_title(const char *title, char *out, uint32_t out_len) {
    uint32_t i = 0;
    uint32_t j = 0;
    int dash = 0;

    if (out_len == 0u) {
        return;
    }

    while (title[i] != '\0' && j + 1u < out_len) {
        char c = ascii_tolower(title[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out[j++] = c;
            dash = 0;
        } else if (!dash && j != 0u) {
            out[j++] = '-';
            dash = 1;
        }
        i++;
    }

    while (j != 0u && out[j - 1u] == '-') {
        j--;
    }
    out[j] = '\0';
}

static uint32_t desktop_blend_icon_pixel(uint32_t src, uint32_t bg) {
    uint32_t a = (src >> 24) & 0xFFu;
    uint32_t sr = (src >> 16) & 0xFFu;
    uint32_t sg = (src >> 8) & 0xFFu;
    uint32_t sb = src & 0xFFu;
    uint32_t br = (bg >> 16) & 0xFFu;
    uint32_t bgc = (bg >> 8) & 0xFFu;
    uint32_t bb = bg & 0xFFu;
    uint32_t r;
    uint32_t g;
    uint32_t b;

    if (a == 0u) {
        return bg;
    }
    if (a == 0xFFu) {
        return src & 0x00FFFFFFu;
    }

    r = ((sr * a) + (br * (255u - a))) / 255u;
    g = ((sg * a) + (bgc * (255u - a))) / 255u;
    b = ((sb * a) + (bb * (255u - a))) / 255u;
    return (r << 16) | (g << 8) | b;
}

static void desktop_draw_icon_bitmap(int x, int y, const struct IconAsset *icon, uint32_t bg) {
    int row;
    int col;

    if (icon == (const struct IconAsset *)0 || icon->pixels == (const uint32_t *)0) {
        return;
    }

    for (row = 0; row < icon->height; row++) {
        for (col = 0; col < icon->width; col++) {
            uint32_t pixel = icon->pixels[(row * icon->width) + col];
            uint32_t a = (pixel >> 24) & 0xFFu;

            if (a == 0u) {
                continue;
            }

            gfx_put_pixel((uint32_t)(x + col), (uint32_t)(y + row),
                          desktop_blend_icon_pixel(pixel, bg));
        }
    }
}

static int desktop_window_is_visible(const struct Window *window);
static int desktop_get_client_rect(const struct Window *window, int *x, int *y, int *w, int *h);
static int desktop_get_window_button_rect(const struct Window *window, int which, int *x, int *y, int *w, int *h);
static int desktop_hit_window_button(const struct Window *window, int x, int y);
static int desktop_get_start_button_rect(int *x, int *y, int *w, int *h);
static int desktop_point_in_start_button(int x, int y);
static int desktop_get_start_menu_rect(int *x, int *y, int *w, int *h);
static int desktop_point_in_start_menu(int x, int y);
static int desktop_start_menu_entry_at(int x, int y);
static int desktop_get_taskbar_button_rect(const struct Window *window, int *x, int *y, int *w, int *h);
static struct Window *desktop_find_taskbar_window_at(int x, int y);
static int desktop_get_icon_rect(int icon_index, int *x, int *y, int *w, int *h);
static int desktop_find_icon_at(int x, int y);
static void desktop_init_icons(void);
static int desktop_icon_create(int launcher_index, int x, int y);
static void desktop_move_icon(int icon_index, int x, int y);
static void desktop_snap_icon_to_grid(int icon_index);
static int desktop_open_icon(int icon_index);
static int desktop_launch_app(int app_index);
static int desktop_open_builtin_window(int builtin_index);
static int desktop_open_launcher_icon(int icon_index);
static int desktop_launcher_icon_count(void);
static const char *desktop_launcher_icon_title(int icon_index);
static uint32_t desktop_launcher_icon_color(int icon_index);
static int desktop_start_menu_entry_count(void);
static const char *desktop_start_menu_entry_title(int entry_index);
static int desktop_activate_start_menu_entry(int entry_index);
static void desktop_system_reboot(void);
static void desktop_system_shutdown(void);
static int desktop_focus_window(struct Window *window);
static struct Window *desktop_focused_window(void);
static void desktop_focus_top_visible_window(void);
static void desktop_restore_window(struct Window *window);
static void desktop_maximize_window(struct Window *window);
static void desktop_close_window(struct Window *window);
static void desktop_minimize_window(struct Window *window);
static int desktop_update_hover_state(int x, int y);
static int desktop_point_in_terminal_client(const struct Window *window, int x, int y);
static int desktop_window_resize_edges(const struct Window *window, int x, int y);
static void desktop_resize_window_to_cursor(struct Window *window, int x, int y);
static uint32_t desktop_terminal_scroll_max(int history_visible);
static void desktop_terminal_scroll(int delta);
static void desktop_draw_window_controls(struct Window *window, uint32_t title_color);
static int desktop_any_visible_window_dirty(void);
static void desktop_begin_busy(uint32_t ticks);

/* Return the fixed 8px bitmap-font width used by the framebuffer text renderer. */
static int desktop_font_width(void) {
    return 8;
}

/* Return the fixed 16px bitmap-font height used by the framebuffer text renderer. */
static int desktop_font_height(void) {
    return 16;
}

/* Center bitmap-font text vertically inside a UI bar and clamp it to the bar top. */
static int desktop_center_text_y(int bar_y, int bar_height) {
    int text_y = bar_y + (bar_height / 2) - (desktop_font_height() / 2);

    if (text_y < bar_y) {
        text_y = bar_y;
    }
    return text_y;
}

/* Copy a short ASCII string into a fixed-size desktop buffer. */
static void desktop_copy_string(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i = 0u;

    if (dst_len == 0u) {
        return;
    }

    while (src[i] != '\0' && i + 1u < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Append one ASCII string onto another fixed-size desktop buffer. */
static void desktop_append_string(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i = ascii_strlen(dst);
    uint32_t j = 0u;

    if (i >= dst_len) {
        return;
    }

    while (src[j] != '\0' && i + 1u < dst_len) {
        dst[i++] = src[j++];
    }
    dst[i] = '\0';
}

/* Append an unsigned integer in decimal to a fixed-size desktop buffer. */
static void desktop_append_u32(char *dst, uint32_t dst_len, uint32_t value) {
    char digits[11];
    uint32_t count = 0u;
    uint32_t i;

    if (value == 0u) {
        desktop_append_string(dst, dst_len, "0");
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
        desktop_append_string(dst, dst_len, one);
    }
}

/* Clamp a signed coordinate into an inclusive desktop range. */
static int desktop_clamp(int value, int minimum, int maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

/* Return a darker or lighter version of a 0xRRGGBB color. */
static uint32_t desktop_adjust_color(uint32_t color, int delta) {
    int r = (int)((color >> 16) & 0xFFu) + delta;
    int g = (int)((color >> 8) & 0xFFu) + delta;
    int b = (int)(color & 0xFFu) + delta;

    r = desktop_clamp(r, 0, 255);
    g = desktop_clamp(g, 0, 255);
    b = desktop_clamp(b, 0, 255);

    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* Build an HH:MM:SS string from elapsed seconds since boot. */
static void desktop_format_clock(uint32_t seconds, char out[9]) {
    uint32_t hours = seconds / 3600u;
    uint32_t minutes = (seconds / 60u) % 60u;
    uint32_t secs = seconds % 60u;

    out[0] = (char)('0' + ((hours / 10u) % 10u));
    out[1] = (char)('0' + (hours % 10u));
    out[2] = ':';
    out[3] = (char)('0' + (minutes / 10u));
    out[4] = (char)('0' + (minutes % 10u));
    out[5] = ':';
    out[6] = (char)('0' + (secs / 10u));
    out[7] = (char)('0' + (secs % 10u));
    out[8] = '\0';
}

/* Read and cache the CPU brand string once for the system info widget. */
static void desktop_init_cpu_brand(void) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t i;

    for (i = 0u; i < sizeof(cpu_brand); i++) {
        cpu_brand[i] = '\0';
    }

    x86_cpuid(0x80000000u, 0u, &a, &b, &c, &d);
    if (a < 0x80000004u) {
        desktop_copy_string(cpu_brand, (uint32_t)sizeof(cpu_brand), "Brand unavailable");
        return;
    }

    x86_cpuid(0x80000002u, 0u, &a, &b, &c, &d);
    ((uint32_t *)cpu_brand)[0] = a;
    ((uint32_t *)cpu_brand)[1] = b;
    ((uint32_t *)cpu_brand)[2] = c;
    ((uint32_t *)cpu_brand)[3] = d;

    x86_cpuid(0x80000003u, 0u, &a, &b, &c, &d);
    ((uint32_t *)cpu_brand)[4] = a;
    ((uint32_t *)cpu_brand)[5] = b;
    ((uint32_t *)cpu_brand)[6] = c;
    ((uint32_t *)cpu_brand)[7] = d;

    x86_cpuid(0x80000004u, 0u, &a, &b, &c, &d);
    ((uint32_t *)cpu_brand)[8] = a;
    ((uint32_t *)cpu_brand)[9] = b;
    ((uint32_t *)cpu_brand)[10] = c;
    ((uint32_t *)cpu_brand)[11] = d;
}

static void desktop_terminal_reset(void) {
    uint32_t row;

    for (row = 0u; row < TERMINAL_HISTORY_LINES; row++) {
        desktop_terminal_state.lines[row][0] = '\0';
    }

    desktop_terminal_state.input[0] = '\0';
    desktop_terminal_state.line_count = 0u;
    desktop_terminal_state.cursor_pos = 0u;
    desktop_terminal_state.input_len = 0u;
    desktop_terminal_state.scroll_offset = 0u;
}

static uint32_t desktop_terminal_scroll_max(int history_visible) {
    if (history_visible <= 0) {
        return 0u;
    }
    if (desktop_terminal_state.line_count <= (uint32_t)history_visible) {
        return 0u;
    }
    return desktop_terminal_state.line_count - (uint32_t)history_visible;
}

void desktop_terminal_clear(void) {
    desktop_terminal_reset();
    if (desktop_terminal_window != (struct Window *)0) {
        /* Clearing terminal content invalidates its cached window contents. */
        desktop_terminal_window->content_dirty = 1;
    }
}

static void desktop_terminal_push_line(void) {
    uint32_t row;

    if (desktop_terminal_state.line_count < TERMINAL_HISTORY_LINES) {
        desktop_terminal_state.lines[desktop_terminal_state.line_count][0] = '\0';
        desktop_terminal_state.line_count++;
    } else {
        for (row = 1u; row < TERMINAL_HISTORY_LINES; row++) {
            desktop_copy_string(desktop_terminal_state.lines[row - 1u],
                                TERMINAL_HISTORY_COLS + 1u,
                                desktop_terminal_state.lines[row]);
        }
        desktop_terminal_state.lines[TERMINAL_HISTORY_LINES - 1u][0] = '\0';
    }
}

static char *desktop_terminal_current_line(void) {
    if (desktop_terminal_state.line_count == 0u) {
        desktop_terminal_push_line();
    }

    return desktop_terminal_state.lines[desktop_terminal_state.line_count - 1u];
}

static void desktop_terminal_append_char(char c) {
    char *line = desktop_terminal_current_line();
    uint32_t len = ascii_strlen(line);

    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        desktop_terminal_push_line();
        return;
    }

    if (len >= TERMINAL_HISTORY_COLS) {
        desktop_terminal_push_line();
        line = desktop_terminal_current_line();
        len = ascii_strlen(line);
    }

    if (len < TERMINAL_HISTORY_COLS) {
        line[len] = c;
        line[len + 1u] = '\0';
    }
}

void terminal_print(const char *s) {
    uint32_t was_at_bottom = (desktop_terminal_state.scroll_offset == 0u);

    while (*s != '\0') {
        desktop_terminal_append_char(*s);
        s++;
    }

    if (was_at_bottom) {
        desktop_terminal_state.scroll_offset = 0u;
    }
    if (desktop_terminal_window != (struct Window *)0) {
        /* New terminal output is one of the few events that actually needs a window redraw. */
        desktop_terminal_window->content_dirty = 1;
    }
}

/* Refresh the system info text once per elapsed second. */
static void desktop_refresh_info_cache(void) {
    uint32_t seconds = get_ticks() / 100u;

    if (info_last_refresh_sec == seconds) {
        return;
    }
    info_last_refresh_sec = seconds;

    desktop_copy_string(info_lines[0], 48u, "CPU:");
    desktop_copy_string(info_lines[1], 48u, cpu_brand);

    desktop_copy_string(info_lines[2], 48u, "Pages used: ");
    desktop_append_u32(info_lines[2], 48u, pmm_used_pages());
    desktop_append_string(info_lines[2], 48u, "/");
    desktop_append_u32(info_lines[2], 48u, pmm_total_pages());

    desktop_copy_string(info_lines[3], 48u, "Pages free: ");
    desktop_append_u32(info_lines[3], 48u, pmm_total_pages() - pmm_used_pages());

    desktop_copy_string(info_lines[4], 48u, "Uptime: ");
    desktop_append_u32(info_lines[4], 48u, seconds);
    desktop_append_string(info_lines[4], 48u, " sec");
}

static void desktop_terminal_submit_input(void) {
    if (desktop_terminal_state.input_len == 0u) {
        return;
    }

    terminal_print(TERMINAL_COMMAND_PROMPT);
    terminal_print(desktop_terminal_state.input);
    terminal_print("\n");
    kshell_dispatch_command(desktop_terminal_state.input);
    desktop_terminal_state.input[0] = '\0';
    desktop_terminal_state.input_len = 0u;
    desktop_terminal_state.cursor_pos = 0u;
    if (desktop_terminal_window != (struct Window *)0) {
        desktop_terminal_window->content_dirty = 1;
    }
}

static void desktop_terminal_handle_key(char key) {
    if (key == '\r') {
        return;
    }

    if (key == '\n') {
        desktop_terminal_submit_input();
        return;
    }

    if (key == '\b') {
        if (desktop_terminal_state.input_len > 0u) {
            desktop_terminal_state.input_len--;
            desktop_terminal_state.cursor_pos = desktop_terminal_state.input_len;
            desktop_terminal_state.input[desktop_terminal_state.input_len] = '\0';
            if (desktop_terminal_window != (struct Window *)0) {
                desktop_terminal_window->content_dirty = 1;
            }
        }
        return;
    }

    if (key >= 32 && key <= 126) {
        if (desktop_terminal_state.input_len + 1u < TERMINAL_INPUT_MAX) {
            desktop_terminal_state.input[desktop_terminal_state.input_len++] = key;
            desktop_terminal_state.input[desktop_terminal_state.input_len] = '\0';
            desktop_terminal_state.cursor_pos = desktop_terminal_state.input_len;
            if (desktop_terminal_window != (struct Window *)0) {
                desktop_terminal_window->content_dirty = 1;
            }
        }
    }
}

/* Draw the interactive terminal client area. */
static void desktop_draw_terminal(struct Window *window) {
    int padding = 4;
    int client_x = window->x + DESKTOP_BORDER_WIDTH + padding;
    int client_y = window->y + DESKTOP_TITLEBAR_HEIGHT + padding;
    int client_width = window->width - (DESKTOP_BORDER_WIDTH * 2) - (padding * 2);
    int client_height = window->height - DESKTOP_TITLEBAR_HEIGHT - DESKTOP_BORDER_WIDTH - (padding * 2);
    int line_height = desktop_font_height();
    int visible_lines = client_height / line_height;
    int history_visible;
    int history_start;
    int text_width;
    int i;
    char input_line[TERMINAL_INPUT_MAX + 3u];
    uint32_t scroll_max;

    if (visible_lines <= 0) {
        return;
    }

    gfx_draw_rect(client_x, client_y, client_width, client_height, 0x000000u);

    history_visible = visible_lines - 1;
    if (history_visible < 0) {
        history_visible = 0;
    }

    scroll_max = desktop_terminal_scroll_max(history_visible);
    if (desktop_terminal_state.scroll_offset > scroll_max) {
        desktop_terminal_state.scroll_offset = scroll_max;
    }

    text_width = client_width - TERMINAL_SCROLLBAR_WIDTH - 4;
    if (text_width < 0) {
        text_width = 0;
    }

    history_start = (int)desktop_terminal_state.line_count - history_visible - (int)desktop_terminal_state.scroll_offset;
    if (history_start < 0) {
        history_start = 0;
    }

    if (desktop_terminal_state.scroll_offset > 0u) {
        desktop_debug_hex("[desktop] draw_total_lines=", desktop_terminal_state.line_count);
        desktop_debug_hex("[desktop] draw_visible_lines=", (uint32_t)history_visible);
        desktop_debug_hex("[desktop] draw_scroll_offset=", desktop_terminal_state.scroll_offset);
        desktop_debug_hex("[desktop] draw_history_start=", (uint32_t)history_start);
    }

    for (i = 0; i < history_visible; i++) {
        int line_index = history_start + i;

        if ((uint32_t)line_index >= desktop_terminal_state.line_count) {
            break;
        }

        gfx_draw_string_at(client_x, client_y + (i * line_height),
                           desktop_terminal_state.lines[line_index],
                           0xFFFFFFu, 0x000000u);
    }

    desktop_copy_string(input_line, (uint32_t)sizeof(input_line), TERMINAL_PROMPT);
    desktop_append_string(input_line, (uint32_t)sizeof(input_line), desktop_terminal_state.input);
    gfx_draw_string_at(client_x, client_y + ((visible_lines - 1) * line_height),
                       input_line, 0xFFFFFFu, 0x000000u);

    /* Draw a narrow scrollbar so longer terminal history stays navigable with the wheel. */
    {
        int track_x = client_x + client_width - TERMINAL_SCROLLBAR_WIDTH;
        int thumb_y = client_y;
        int thumb_height = client_height;

        gfx_draw_rect(track_x, client_y, TERMINAL_SCROLLBAR_WIDTH, client_height, 0x2B2B2Bu);
        if (desktop_terminal_state.line_count > 0u && history_visible > 0) {
            uint32_t total_lines = desktop_terminal_state.line_count;
            if ((uint32_t)history_visible < total_lines) {
                thumb_height = (client_height * history_visible) / (int)total_lines;
                if (thumb_height < 8) {
                    thumb_height = 8;
                }
                if (thumb_height > client_height) {
                    thumb_height = client_height;
                }
                if (scroll_max > 0u) {
                    thumb_y = client_y + (int)(((client_height - thumb_height)
                        * desktop_terminal_state.scroll_offset) / scroll_max);
                }
            }
        }
        gfx_draw_rect(track_x, thumb_y, TERMINAL_SCROLLBAR_WIDTH, thumb_height, 0xB8B8B8u);
    }

    if (desktop_terminal_state.scroll_offset > 0u) {
        gfx_draw_char_at(client_x + text_width - 8, client_y, '^', 0xFFFFFFu, 0x000000u);
    } else if (scroll_max > 0u) {
        gfx_draw_char_at(client_x + text_width - 8, client_y, 'v', 0xFFFFFFu, 0x000000u);
    }

    {
        int cursor_x = client_x + (int)((ascii_strlen(TERMINAL_PROMPT) + desktop_terminal_state.cursor_pos)
                          * (uint32_t)desktop_font_width());
        int cursor_y = client_y + ((visible_lines - 1) * line_height);

        /* Keep the terminal caret steady so the window only redraws on real input/output changes. */
        gfx_draw_rect(cursor_x, cursor_y, 2, desktop_font_height(), 0xFFFFFFu);
    }
}

/* Clip one info line to the window width and append "..." for truncated CPU brand text. */
static void desktop_clip_info_line(char *dst, uint32_t dst_len, const char *src,
                                   int max_chars, int ellipsis) {
    uint32_t i = 0u;
    uint32_t src_len = ascii_strlen(src);
    uint32_t limit;

    if (dst_len == 0u) {
        return;
    }

    if (max_chars <= 0) {
        dst[0] = '\0';
        return;
    }

    limit = (uint32_t)max_chars;
    if (limit + 1u > dst_len) {
        limit = dst_len - 1u;
    }

    if (ellipsis && src_len > limit && limit > 3u) {
        for (i = 0u; i < limit - 3u; i++) {
            dst[i] = src[i];
        }
        dst[i++] = '.';
        dst[i++] = '.';
        dst[i++] = '.';
        dst[i] = '\0';
        return;
    }

    while (src[i] != '\0' && i < limit) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Draw the system info client area. */
static void desktop_draw_info(struct Window *window) {
    int client_x = window->x + DESKTOP_INFO_PADDING;
    int client_y = window->y + DESKTOP_TITLEBAR_HEIGHT + DESKTOP_INFO_PADDING;
    int client_width = window->width - (DESKTOP_INFO_PADDING * 2);
    int max_chars = client_width / desktop_font_width();
    char clipped[49];
    uint32_t i;

    desktop_refresh_info_cache();
    if (max_chars < 0) {
        max_chars = 0;
    }
    for (i = 0u; i < DESKTOP_INFO_LINES; i++) {
        desktop_clip_info_line(clipped, (uint32_t)sizeof(clipped), info_lines[i],
                               max_chars, i == 1u);
        gfx_draw_string_at(client_x, client_y + (int)(i * (desktop_font_height() + 2)), clipped,
                           0x17110Cu, window->bg_color);
    }
}

/* Draw the live boot clock client area. */
static void desktop_draw_clock(struct Window *window) {
    char clock_text[9];
    uint32_t seconds = get_ticks() / 100u;

    desktop_format_clock(seconds, clock_text);
    gfx_draw_string_at(window->x + 24, window->y + DESKTOP_TITLEBAR_HEIGHT + 14, clock_text,
                       0x1B140Eu, window->bg_color);
}

static int desktop_window_is_visible(const struct Window *window) {
    return window != (const struct Window *)0 && !window->hidden && !window->minimized;
}

static int desktop_get_client_rect(const struct Window *window, int *x, int *y, int *w, int *h) {
    int client_x;
    int client_y;
    int client_w;
    int client_h;

    if (window == (const struct Window *)0) {
        return 0;
    }

    client_x = window->x + DESKTOP_BORDER_WIDTH;
    client_y = window->y + DESKTOP_TITLEBAR_HEIGHT;
    client_w = window->width - (DESKTOP_BORDER_WIDTH * 2);
    client_h = window->height - DESKTOP_TITLEBAR_HEIGHT - DESKTOP_BORDER_WIDTH;

    if (client_w <= 0 || client_h <= 0) {
        return 0;
    }

    if (x != (int *)0) *x = client_x;
    if (y != (int *)0) *y = client_y;
    if (w != (int *)0) *w = client_w;
    if (h != (int *)0) *h = client_h;
    return 1;
}

static struct Window *desktop_focused_window(void) {
    int i;

    for (i = (int)desktop_window_count - 1; i >= 0; i--) {
        struct Window *window = desktop_draw_order[i];

        if (desktop_window_is_visible(window) && window->focused) {
            return window;
        }
    }

    return (struct Window *)0;
}

/* Return non-zero when a point lies inside the taskbar strip. */
static int desktop_point_in_taskbar(int x, int y) {
    return y >= (gfx_get_height() - DESKTOP_TASKBAR_HEIGHT) && x >= 0 && x < gfx_get_width();
}

/* Return the Start button rectangle inside the taskbar strip. */
static int desktop_get_start_button_rect(int *x, int *y, int *w, int *h) {
    int button_x = 8;
    int button_y = gfx_get_height() - DESKTOP_TASKBAR_HEIGHT + 4;
    int button_w = TASKBAR_START_BUTTON_WIDTH;
    int button_h = DESKTOP_TASKBAR_HEIGHT - 8;

    if (x != (int *)0) *x = button_x;
    if (y != (int *)0) *y = button_y;
    if (w != (int *)0) *w = button_w;
    if (h != (int *)0) *h = button_h;
    return 1;
}

/* Return non-zero when a point lies inside the Start button. */
static int desktop_point_in_start_button(int x, int y) {
    int bx;
    int by;
    int bw;
    int bh;

    (void)desktop_get_start_button_rect(&bx, &by, &bw, &bh);
    return x >= bx && y >= by && x < bx + bw && y < by + bh;
}

/* Return the Start menu popup rectangle anchored above the taskbar Start button. */
static int desktop_get_start_menu_rect(int *x, int *y, int *w, int *h) {
    int menu_x;
    int menu_y;
    int menu_w = START_MENU_WIDTH;
    int menu_h = (START_MENU_PADDING * 2) + (desktop_start_menu_entry_count() * START_MENU_ENTRY_HEIGHT);
    int start_x;
    int start_y;
    int start_w;
    int start_h;

    if (!desktop_start_menu_open) {
        return 0;
    }
    if (menu_h <= (START_MENU_PADDING * 2)) {
        menu_h = (START_MENU_PADDING * 2) + START_MENU_ENTRY_HEIGHT;
    }

    (void)desktop_get_start_button_rect(&start_x, &start_y, &start_w, &start_h);
    (void)start_y;
    (void)start_w;
    (void)start_h;
    menu_x = start_x;
    menu_y = gfx_get_height() - DESKTOP_TASKBAR_HEIGHT - menu_h - 2;

    if (menu_x + menu_w > gfx_get_width()) {
        menu_x = gfx_get_width() - menu_w;
    }
    if (menu_x < 0) {
        menu_x = 0;
    }
    if (menu_y < 0) {
        menu_y = 0;
    }

    if (x != (int *)0) *x = menu_x;
    if (y != (int *)0) *y = menu_y;
    if (w != (int *)0) *w = menu_w;
    if (h != (int *)0) *h = menu_h;
    return 1;
}

/* Return non-zero when a point lies inside the Start menu popup. */
static int desktop_point_in_start_menu(int x, int y) {
    int mx;
    int my;
    int mw;
    int mh;

    if (!desktop_get_start_menu_rect(&mx, &my, &mw, &mh)) {
        return 0;
    }
    return x >= mx && y >= my && x < mx + mw && y < my + mh;
}

/* Return the Start menu entry index under the cursor, or -1. */
static int desktop_start_menu_entry_at(int x, int y) {
    int mx;
    int my;
    int mw;
    int mh;
    int index;

    if (!desktop_get_start_menu_rect(&mx, &my, &mw, &mh)) {
        return -1;
    }
    if (x < mx || y < my || x >= mx + mw || y >= my + mh) {
        return -1;
    }

    index = (y - (my + START_MENU_PADDING)) / START_MENU_ENTRY_HEIGHT;
    if (index < 0 || index >= desktop_start_menu_entry_count()) {
        return -1;
    }
    return index;
}

/* Return how many desktop launcher icons are available. */
static int desktop_launcher_icon_count(void) {
    return DESKTOP_BUILTIN_LAUNCHER_COUNT + app_count;
}

/* Return the icon label for one launcher index. */
static const char *desktop_launcher_icon_title(int icon_index) {
    if (icon_index == 0) {
        return "Terminal";
    }
    if (icon_index == 1) {
        return "Clock";
    }
    if (icon_index == 2) {
        return "System Info";
    }
    if (icon_index >= DESKTOP_BUILTIN_LAUNCHER_COUNT
        && icon_index < desktop_launcher_icon_count()) {
        App *app = app_registry[icon_index - DESKTOP_BUILTIN_LAUNCHER_COUNT];
        if (app != (App *)0 && app->title != (const char *)0) {
            return app->title;
        }
    }
    return "App";
}

/* Return the icon tile color for one launcher index. */
static uint32_t desktop_launcher_icon_color(int icon_index) {
    if (icon_index == 0) {
        return 0x2B7A78u;
    }
    if (icon_index == 1) {
        return 0x3D5A80u;
    }
    if (icon_index == 2) {
        return 0x8E6C88u;
    }
    return 0x2E6E8Eu;
}

static int desktop_icon_create(int launcher_index, int x, int y) {
    if (desktop_icon_count >= DESKTOP_ICON_INSTANCE_MAX) {
        return -1;
    }

    desktop_icons[desktop_icon_count].launcher_index = launcher_index;
    desktop_icons[desktop_icon_count].x = x;
    desktop_icons[desktop_icon_count].y = y;
    desktop_icons[desktop_icon_count].in_use = 1;
    desktop_icon_count++;
    return desktop_icon_count - 1;
}

static int desktop_icon_grid_cols(void) {
    int cols = (gfx_get_width() - (DESKTOP_ICON_PADDING_X * 2)) / DESKTOP_ICON_CELL_WIDTH;

    if (cols <= 0) {
        cols = 1;
    }
    return cols;
}

static int desktop_icon_grid_rows(void) {
    int rows = ((gfx_get_height() - DESKTOP_TASKBAR_HEIGHT) - (DESKTOP_ICON_PADDING_Y * 2))
        / DESKTOP_ICON_CELL_HEIGHT;

    if (rows <= 0) {
        rows = 1;
    }
    return rows;
}

static void desktop_icon_cell_position(int col, int row, int *x, int *y) {
    int cell_x = DESKTOP_ICON_PADDING_X + (col * DESKTOP_ICON_CELL_WIDTH);
    int cell_y = DESKTOP_ICON_PADDING_Y + (row * DESKTOP_ICON_CELL_HEIGHT);
    int icon_x = cell_x + ((DESKTOP_ICON_CELL_WIDTH - DESKTOP_ICON_TILE_SIZE) / 2);

    if (x != (int *)0) {
        *x = icon_x;
    }
    if (y != (int *)0) {
        *y = cell_y;
    }
}

static int desktop_find_icon_in_cell(int col, int row, int skip_icon) {
    int i;
    int cell_x;
    int cell_y;

    desktop_icon_cell_position(col, row, &cell_x, &cell_y);
    for (i = 0; i < desktop_icon_count; i++) {
        if (i == skip_icon || !desktop_icons[i].in_use) {
            continue;
        }
        if (desktop_icons[i].x == cell_x && desktop_icons[i].y == cell_y) {
            return i;
        }
    }
    return -1;
}

static void desktop_init_icons(void) {
    int launcher_count = desktop_launcher_icon_count();
    int cols;
    int i;

    desktop_icon_count = 0;
    for (i = 0; i < DESKTOP_ICON_INSTANCE_MAX; i++) {
        desktop_icons[i].in_use = 0;
    }

    cols = desktop_icon_grid_cols();

    for (i = 0; i < launcher_count && i < DESKTOP_ICON_INSTANCE_MAX; i++) {
        int col = i % cols;
        int row = i / cols;
        int icon_x;
        int icon_y;

        desktop_icon_cell_position(col, row, &icon_x, &icon_y);
        (void)desktop_icon_create(i, icon_x, icon_y);
    }
}

/* Return non-zero when a point lies inside a window's rectangle. */
static int desktop_point_in_window(const struct Window *window, int x, int y) {
    if (!desktop_window_is_visible(window)) {
        return 0;
    }
    return x >= window->x && y >= window->y
        && x < window->x + window->width && y < window->y + window->height;
}

/* Return icon rectangle for one launcher index in the desktop icon grid. */
static int desktop_get_icon_rect(int icon_index, int *x, int *y, int *w, int *h) {
    int icon_w = DESKTOP_ICON_TILE_SIZE;
    int icon_h = DESKTOP_ICON_TILE_SIZE + 2 + DESKTOP_ICON_LABEL_HEIGHT;

    if (icon_index < 0 || icon_index >= desktop_icon_count || !desktop_icons[icon_index].in_use) {
        return 0;
    }

    if (x != (int *)0) *x = desktop_icons[icon_index].x;
    if (y != (int *)0) *y = desktop_icons[icon_index].y;
    if (w != (int *)0) *w = icon_w;
    if (h != (int *)0) *h = icon_h;
    return 1;
}

/* Return the launcher icon index under a point on the desktop background, or -1. */
static int desktop_find_icon_at(int x, int y) {
    int i;

    for (i = 0; i < desktop_icon_count; i++) {
        int ix;
        int iy;
        int iw;
        int ih;

        if (!desktop_get_icon_rect(i, &ix, &iy, &iw, &ih)) {
            continue;
        }
        if (x >= ix && y >= iy && x < ix + iw && y < iy + ih) {
            return i;
        }
    }
    return -1;
}

static void desktop_move_icon(int icon_index, int x, int y) {
    int max_x = gfx_get_width() - DESKTOP_ICON_TILE_SIZE;
    int max_y = (gfx_get_height() - DESKTOP_TASKBAR_HEIGHT)
        - (DESKTOP_ICON_TILE_SIZE + 2 + DESKTOP_ICON_LABEL_HEIGHT);

    if (icon_index < 0 || icon_index >= desktop_icon_count || !desktop_icons[icon_index].in_use) {
        return;
    }
    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    desktop_icons[icon_index].x = desktop_clamp(x, 0, max_x);
    desktop_icons[icon_index].y = desktop_clamp(y, 0, max_y);
}

static void desktop_snap_icon_to_grid(int icon_index) {
    int cols;
    int rows;
    int col;
    int row;
    int best_col;
    int best_row;
    int best_dist = 0x7FFFFFFF;
    int r;
    int c;

    if (icon_index < 0 || icon_index >= desktop_icon_count || !desktop_icons[icon_index].in_use) {
        return;
    }

    cols = desktop_icon_grid_cols();
    rows = desktop_icon_grid_rows();
    col = (desktop_icons[icon_index].x - DESKTOP_ICON_PADDING_X + (DESKTOP_ICON_CELL_WIDTH / 2))
        / DESKTOP_ICON_CELL_WIDTH;
    row = (desktop_icons[icon_index].y - DESKTOP_ICON_PADDING_Y + (DESKTOP_ICON_CELL_HEIGHT / 2))
        / DESKTOP_ICON_CELL_HEIGHT;
    col = desktop_clamp(col, 0, cols - 1);
    row = desktop_clamp(row, 0, rows - 1);

    best_col = col;
    best_row = row;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            int dist;

            if (desktop_find_icon_in_cell(c, r, icon_index) >= 0) {
                continue;
            }

            dist = ((c - col) < 0 ? (col - c) : (c - col))
                + ((r - row) < 0 ? (row - r) : (r - row));
            if (dist < best_dist) {
                best_dist = dist;
                best_col = c;
                best_row = r;
            }
        }
    }

    desktop_icon_cell_position(best_col, best_row, &desktop_icons[icon_index].x, &desktop_icons[icon_index].y);
}

/* Restore and focus a built-in desktop window by launcher index. */
static int desktop_open_builtin_window(int builtin_index) {
    struct Window *window = (struct Window *)0;

    if (builtin_index == 0) {
        window = desktop_terminal_window;
    } else if (builtin_index == 1) {
        window = desktop_clock_window;
    } else if (builtin_index == 2) {
        window = desktop_info_window;
    }

    if (window == (struct Window *)0) {
        return 0;
    }

    desktop_restore_window(window);
    window->content_dirty = 1;
    desktop_begin_busy(8u);
    (void)desktop_focus_window(window);
    sound_open_window();
    return 1;
}

/* Create or show an app window and move it to focused/topmost state. */
static int desktop_launch_app(int app_index) {
    App *app;
    struct Window *window;

    if (app_index < 0 || app_index >= app_count) {
        return 0;
    }

    app = app_registry[app_index];
    if (app == (App *)0) {
        return 0;
    }

    window = desktop_app_windows[app_index];
    if (window == (struct Window *)0) {
        window = desktop_create_window(app->x, app->y, app->w, app->h, app->title,
                                       app->bg_color, (WindowDrawCallback)0, app);
        if (window == (struct Window *)0) {
            return 0;
        }
        desktop_app_windows[app_index] = window;
        desktop_begin_busy(10u);
        sound_open_window();
    } else {
        window->hidden = 0;
        window->minimized = 0;
        desktop_begin_busy(6u);
        sound_open_window();
    }

    window->content_dirty = 1;
    desktop_taskbar_dirty = 1;
    (void)desktop_focus_window(window);
    return 1;
}

/* Open one registered app window by its exact title text. */
int desktop_launch_app_by_title(const char *title) {
    int i;

    if (title == (const char *)0) {
        return 0;
    }
    for (i = 0; i < app_count; i++) {
        App *app = app_registry[i];

        if (app != (App *)0 && ascii_streq(app->title, title)) {
            return desktop_launch_app(i);
        }
    }
    return 0;
}

/* Update one window title by matching the current title or owning app title. */
int desktop_set_window_title(const char *old_title, const char *new_title) {
    uint32_t i;

    if (old_title == (const char *)0 || new_title == (const char *)0) {
        return 0;
    }
    for (i = 0u; i < desktop_window_count; i++) {
        struct Window *window = desktop_draw_order[i];

        if (window == (struct Window *)0) {
            continue;
        }
        if (ascii_streq(window->title, old_title)
            || (window->app != (struct App *)0 && window->app->title != (const char *)0
                && ascii_streq(window->app->title, old_title))) {
            desktop_copy_string(window->title, (uint32_t)sizeof(window->title), new_title);
            window->content_dirty = 1;
            desktop_taskbar_dirty = 1;
            return 1;
        }
    }
    return 0;
}

/* Open one launcher icon target by index. */
static int desktop_open_launcher_icon(int icon_index) {
    if (icon_index < 0 || icon_index >= desktop_launcher_icon_count()) {
        return 0;
    }
    if (icon_index < DESKTOP_BUILTIN_LAUNCHER_COUNT) {
        return desktop_open_builtin_window(icon_index);
    }
    return desktop_launch_app(icon_index - DESKTOP_BUILTIN_LAUNCHER_COUNT);
}

static int desktop_open_icon(int icon_index) {
    if (icon_index < 0 || icon_index >= desktop_icon_count || !desktop_icons[icon_index].in_use) {
        return 0;
    }
    return desktop_open_launcher_icon(desktop_icons[icon_index].launcher_index);
}

/* Return how many entries the Start menu should render. */
static int desktop_start_menu_entry_count(void) {
    return desktop_launcher_icon_count() + DESKTOP_START_MENU_ACTION_COUNT;
}

/* Return a Start menu entry label by index. */
static const char *desktop_start_menu_entry_title(int entry_index) {
    int launcher_count = desktop_launcher_icon_count();

    if (entry_index < 0 || entry_index >= desktop_start_menu_entry_count()) {
        return "";
    }
    if (entry_index < launcher_count) {
        return desktop_launcher_icon_title(entry_index);
    }
    if (entry_index == launcher_count) {
        return "Kernel Shell";
    }
    if (entry_index == launcher_count + 1) {
        return "Reboot";
    }
    return "Shut Down";
}

/* Reboot using keyboard-controller reset, then triple-fault fallback. */
static void desktop_system_reboot(void) {
    uint32_t i;
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0u, 0u};

    (void)fat32_sync(0);

    for (i = 0; i < 0x10000u; i++) {
        if ((io_in8(0x64u) & 0x02u) == 0u) {
            io_out8(0x64u, 0xFEu);
            break;
        }
        io_wait();
    }

    __asm__ volatile ("cli");
    __asm__ volatile ("lidt %0" : : "m"(null_idt));
    __asm__ volatile ("int3");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Stop execution for shutdown-style behavior when poweroff is unavailable. */
static void desktop_system_shutdown(void) {
    __asm__ volatile ("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

/* Activate one Start menu entry and run its launcher/action behavior. */
static int desktop_activate_start_menu_entry(int entry_index) {
    int launcher_count = desktop_launcher_icon_count();

    if (entry_index < 0 || entry_index >= desktop_start_menu_entry_count()) {
        return 0;
    }
    if (entry_index < launcher_count) {
        return desktop_open_launcher_icon(entry_index);
    }
    if (entry_index == launcher_count) {
        desktop_request_kernel_shell();
        return 1;
    }
    if (entry_index == launcher_count + 1) {
        desktop_system_reboot();
        return 1;
    }

    desktop_system_shutdown();
    return 1;
}

static int desktop_get_window_button_rect(const struct Window *window, int which, int *x, int *y, int *w, int *h) {
    int button_x;
    int button_y;

    if (!desktop_window_is_visible(window)) {
        return 0;
    }
    if (which < DESKTOP_WINDOW_BUTTON_MINIMIZE || which > DESKTOP_WINDOW_BUTTON_CLOSE) {
        return 0;
    }

    button_y = window->y + (DESKTOP_TITLEBAR_HEIGHT - WINDOW_BUTTON_HEIGHT) / 2;
    button_x = window->x + window->width - WINDOW_BUTTON_RIGHT_MARGIN - WINDOW_BUTTON_WIDTH
        - ((DESKTOP_WINDOW_BUTTON_CLOSE - which) * (WINDOW_BUTTON_WIDTH + WINDOW_BUTTON_GAP));

    if (x != (int *)0) *x = button_x;
    if (y != (int *)0) *y = button_y;
    if (w != (int *)0) *w = WINDOW_BUTTON_WIDTH;
    if (h != (int *)0) *h = WINDOW_BUTTON_HEIGHT;
    return 1;
}

static int desktop_hit_window_button(const struct Window *window, int x, int y) {
    int which;

    if (!desktop_window_is_visible(window)) {
        return DESKTOP_WINDOW_BUTTON_NONE;
    }

    for (which = DESKTOP_WINDOW_BUTTON_MINIMIZE; which <= DESKTOP_WINDOW_BUTTON_CLOSE; which++) {
        int bx;
        int by;
        int bw;
        int bh;

        (void)desktop_get_window_button_rect(window, which, &bx, &by, &bw, &bh);
        if (x >= bx && y >= by && x < bx + bw && y < by + bh) {
            return which;
        }
    }

    return DESKTOP_WINDOW_BUTTON_NONE;
}

/* Return non-zero when a point lies in the draggable titlebar strip, excluding the control buttons. */
static int desktop_point_in_titlebar(const struct Window *window, int x, int y) {
    int buttons_left;

    if (!desktop_point_in_window(window, x, y) || y >= window->y + DESKTOP_TITLEBAR_HEIGHT) {
        return 0;
    }

    buttons_left = window->x + window->width - WINDOW_BUTTON_RIGHT_MARGIN
        - (WINDOW_BUTTON_COUNT * WINDOW_BUTTON_WIDTH)
        - ((WINDOW_BUTTON_COUNT - 1) * WINDOW_BUTTON_GAP);
    return x < buttons_left;
}

static int desktop_get_taskbar_button_rect(const struct Window *window, int *x, int *y, int *w, int *h) {
    uint32_t i;
    int next_x = 8 + TASKBAR_START_BUTTON_WIDTH + 8;
    int button_y = gfx_get_height() - DESKTOP_TASKBAR_HEIGHT + 4;
    int button_h = DESKTOP_TASKBAR_HEIGHT - 8;

    if (window == (const struct Window *)0 || window->hidden) {
        return 0;
    }

    for (i = 0u; i < desktop_window_count; i++) {
        struct Window *entry = desktop_draw_order[i];
        if (entry->hidden) {
            continue;
        }
        if (entry->app == (struct App *)0) {
            continue;
        }

        {
            int label_width = (int)ascii_strlen(entry->title) * desktop_font_width();
            int button_w = label_width + (TASKBAR_BUTTON_PADDING_X * 2);
            if (button_w < TASKBAR_BUTTON_MIN_WIDTH) {
                button_w = TASKBAR_BUTTON_MIN_WIDTH;
            }

            if (entry == window) {
                if (x != (int *)0) *x = next_x;
                if (y != (int *)0) *y = button_y;
                if (w != (int *)0) *w = button_w;
                if (h != (int *)0) *h = button_h;
                return 1;
            }

            next_x += button_w + TASKBAR_BUTTON_GAP;
        }
    }

    return 0;
}

static struct Window *desktop_find_taskbar_window_at(int x, int y) {
    uint32_t i;

    if (!desktop_point_in_taskbar(x, y)) {
        return (struct Window *)0;
    }

    for (i = 0u; i < desktop_window_count; i++) {
        struct Window *window = desktop_draw_order[i];
        int bx;
        int by;
        int bw;
        int bh;

        if (!desktop_get_taskbar_button_rect(window, &bx, &by, &bw, &bh)) {
            continue;
        }
        if (x >= bx && y >= by && x < bx + bw && y < by + bh) {
            return window;
        }
    }

    return (struct Window *)0;
}

/* Find the topmost window under the current pointer location. */
static struct Window *desktop_find_window_at(int x, int y) {
    int i;

    for (i = (int)desktop_window_count - 1; i >= 0; i--) {
        struct Window *window = desktop_draw_order[i];
        if (desktop_point_in_window(window, x, y)) {
            return window;
        }
    }
    return (struct Window *)0;
}

/* Move one window to the front and update focus flags. */
static int desktop_focus_window(struct Window *window) {
    uint32_t i;
    uint32_t found = DESKTOP_WINDOW_MAX;
    int changed = 0;
    int was_focused = 0;

    if (window != (struct Window *)0) {
        was_focused = window->focused;
    }

    for (i = 0u; i < desktop_window_count; i++) {
        if (desktop_draw_order[i]->focused != 0) {
            changed = 1;
        }
        desktop_draw_order[i]->focused = 0;
        if (desktop_draw_order[i] == window) {
            found = i;
        }
    }
    if (window == (struct Window *)0) {
        return changed;
    }

    if (!was_focused) {
        changed = 1;
    }
    window->focused = 1;
    if (found < desktop_window_count) {
        for (i = found; i + 1u < desktop_window_count; i++) {
            desktop_draw_order[i] = desktop_draw_order[i + 1u];
        }
        desktop_draw_order[desktop_window_count - 1u] = window;
    }
    return changed;
}

static void desktop_focus_top_visible_window(void) {
    int i;

    for (i = (int)desktop_window_count - 1; i >= 0; i--) {
        if (desktop_window_is_visible(desktop_draw_order[i])) {
            (void)desktop_focus_window(desktop_draw_order[i]);
            return;
        }
    }
    (void)desktop_focus_window((struct Window *)0);
}

static void desktop_restore_window(struct Window *window) {
    if (window == (struct Window *)0) {
        return;
    }

    window->hidden = 0;
    window->minimized = 0;
    window->content_dirty = 1;
}

static void desktop_maximize_window(struct Window *window) {
    if (!desktop_window_is_visible(window)) {
        return;
    }

    if (!window->maximized) {
        window->restore_x = window->x;
        window->restore_y = window->y;
        window->restore_width = window->width;
        window->restore_height = window->height;
        window->x = 0;
        window->y = 0;
        window->width = gfx_get_width();
        window->height = gfx_get_height() - DESKTOP_TASKBAR_HEIGHT;
        window->maximized = 1;
    } else {
        window->x = window->restore_x;
        window->y = window->restore_y;
        window->width = window->restore_width;
        window->height = window->restore_height;
        window->maximized = 0;
    }
    window->content_dirty = 1;
}

static void desktop_close_window(struct Window *window) {
    if (window == (struct Window *)0) {
        return;
    }

    if (window->app != (struct App *)0 && window->app->on_close != (void (*)(void))0) {
        window->app->on_close();
    }

    window->hidden = 1;
    window->minimized = 0;
    window->focused = 0;
    if (window->app != (struct App *)0) {
        desktop_taskbar_dirty = 1;
    }
    sound_close_window();
}

static void desktop_minimize_window(struct Window *window) {
    if (window == (struct Window *)0) {
        return;
    }

    window->minimized = 1;
    window->hidden = 0;
    window->focused = 0;
}

static int desktop_update_hover_state(int x, int y) {
    struct Window *hover_window = desktop_find_window_at(x, y);
    struct Window *hover_taskbar_window = desktop_find_taskbar_window_at(x, y);
    int hover_button = DESKTOP_WINDOW_BUTTON_NONE;
    int changed = 0;

    if (hover_window != (struct Window *)0) {
        hover_button = desktop_hit_window_button(hover_window, x, y);
    }

    if (hover_window != desktop_hover_window || hover_button != desktop_hover_button) {
        if (hover_button != DESKTOP_WINDOW_BUTTON_NONE) {
            desktop_debug_hex("[desktop] hover_button=", (uint32_t)hover_button);
            desktop_debug_hex("[desktop] hover_x=", (uint32_t)x);
            desktop_debug_hex("[desktop] hover_y=", (uint32_t)y);
        }
        desktop_hover_window = hover_window;
        desktop_hover_button = hover_button;
        changed = 1;
    }
    if (hover_taskbar_window != desktop_hover_taskbar_window) {
        desktop_hover_taskbar_window = hover_taskbar_window;
        changed = 1;
    }

    return changed;
}

/* Draw the desktop wallpaper area behind all windows. */
static void desktop_draw_background(void) {
    gfx_fill_screen(DESKTOP_BACKGROUND_COLOR);
}

/* Draw all launcher icons on the desktop background grid. */
static void desktop_draw_icons(void) {
    int i;

    for (i = 0; i < desktop_icon_count; i++) {
        int launcher_index;
        const char *title;
        const struct IconAsset *icon;
        char icon_key[48];
        int ix;
        int iy;
        int iw;
        int ih;
        uint32_t tile = desktop_launcher_icon_color(i);
        uint32_t border = 0x183B4Cu;
        int label_y;
        int text_w;
        int text_x;

        if (!desktop_get_icon_rect(i, &ix, &iy, &iw, &ih)) {
            continue;
        }

        launcher_index = desktop_icons[i].launcher_index;
        title = desktop_launcher_icon_title(launcher_index);

        if (i == desktop_focused_icon) {
            tile = 0x5E9FC2u;
            border = 0xD8F0FFu;
        }

        tile = desktop_launcher_icon_color(launcher_index);
        if (i == desktop_focused_icon) {
            tile = 0x5E9FC2u;
        }

        desktop_icon_key_from_title(title, icon_key, sizeof(icon_key));
        icon = icon_assets_find(icon_key);

        gfx_draw_rect(ix, iy, DESKTOP_ICON_TILE_SIZE, DESKTOP_ICON_TILE_SIZE, tile);
        gfx_draw_rect_outline(ix, iy, DESKTOP_ICON_TILE_SIZE, DESKTOP_ICON_TILE_SIZE, border);
        if (icon != (const struct IconAsset *)0) {
            desktop_draw_icon_bitmap(ix + ((DESKTOP_ICON_TILE_SIZE - icon->width) / 2),
                                     iy + ((DESKTOP_ICON_TILE_SIZE - icon->height) / 2),
                                     icon, tile);
        }

        label_y = iy + DESKTOP_ICON_TILE_SIZE + 2;
        text_w = (int)ascii_strlen(title) * desktop_font_width();
        text_x = ix + (DESKTOP_ICON_TILE_SIZE - text_w) / 2;
        if (text_x < ix) {
            text_x = ix;
        }
        gfx_draw_string_at(text_x, label_y, title, 0xF2FBFFu, DESKTOP_BACKGROUND_COLOR);
    }
}

/* Draw the Start menu popup above the taskbar when it is open. */
static void desktop_draw_start_menu(void) {
    int mx;
    int my;
    int mw;
    int mh;
    int mouse_x;
    int mouse_y;
    int i;

    if (!desktop_get_start_menu_rect(&mx, &my, &mw, &mh)) {
        return;
    }

    mouse_get_pos(&mouse_x, &mouse_y);

    gfx_draw_rect(mx, my, mw, mh, 0x1C2A33u);
    gfx_draw_rect_outline(mx, my, mw, mh, 0x5E7382u);

    for (i = 0; i < desktop_start_menu_entry_count(); i++) {
        const char *title = desktop_start_menu_entry_title(i);
        int ey = my + START_MENU_PADDING + (i * START_MENU_ENTRY_HEIGHT);
        uint32_t bg = 0x1C2A33u;

        if (mouse_x >= mx + 2 && mouse_x < mx + mw - 2
            && mouse_y >= ey && mouse_y < ey + START_MENU_ENTRY_HEIGHT) {
            bg = 0x2C4553u;
        }

        gfx_draw_rect(mx + 2, ey, mw - 4, START_MENU_ENTRY_HEIGHT, bg);
        gfx_draw_string_at(mx + START_MENU_PADDING, ey + 1, title, 0xE7F4FFu, bg);
    }
}

/* Draw the always-on-top taskbar and its live time display. */
static void desktop_draw_taskbar(void) {
    char clock_text[9];
    uint32_t seconds = get_ticks() / 100u;
    int taskbar_y = gfx_get_height() - DESKTOP_TASKBAR_HEIGHT;
    int text_y;
    int right_x;
    uint32_t i;

    desktop_format_clock(seconds, clock_text);
    text_y = desktop_center_text_y(taskbar_y, DESKTOP_TASKBAR_HEIGHT);
    gfx_draw_rect(0, taskbar_y,
                  gfx_get_width(), DESKTOP_TASKBAR_HEIGHT, 0x15232Eu);
    gfx_draw_hline(0, taskbar_y,
                   gfx_get_width(), 0x3C576Bu);

    {
        int bx;
        int by;
        int bw;
        int bh;
        uint32_t fill;

        (void)desktop_get_start_button_rect(&bx, &by, &bw, &bh);
        fill = desktop_start_menu_open ? 0x47606Fu : 0x2A3D48u;
        if (desktop_point_in_start_button(desktop_cursor_x, desktop_cursor_y)) {
            fill = 0x3D5563u;
        }
        const char *start_text = "coffeeOS";
        int start_text_x = bx + (bw - ((int)ascii_strlen(start_text) * desktop_font_width())) / 2;

        if (start_text_x < bx + 4) {
            start_text_x = bx + 4;
        }
        gfx_draw_rect(bx, by, bw, bh, fill);
        gfx_draw_rect_outline(bx, by, bw, bh, 0x6E8694u);
        gfx_draw_string_at(start_text_x, by + (bh - desktop_font_height()) / 2,
                           start_text, 0xE7F4FFu, fill);
    }

    for (i = 0u; i < desktop_window_count; i++) {
        struct Window *window = desktop_draw_order[i];
        int bx;
        int by;
        int bw;
        int bh;
        uint32_t fill;

        if (!desktop_get_taskbar_button_rect(window, &bx, &by, &bw, &bh)) {
            continue;
        }

        fill = (window == desktop_hover_taskbar_window) ? 0x41525Eu : 0x233742u;
        gfx_draw_rect(bx, by, bw, bh, fill);
        gfx_draw_rect_outline(bx, by, bw, bh, 0x607380u);
        gfx_draw_string_at(bx + TASKBAR_BUTTON_PADDING_X,
                           by + (bh - desktop_font_height()) / 2,
                           window->title, 0xE7F4FFu, fill);
    }

    right_x = gfx_get_width() - 8 - ((int)ascii_strlen(clock_text) * 8);
    gfx_draw_string_at(right_x, text_y, clock_text, 0xE7F4FFu, 0x15232Eu);
    desktop_draw_start_menu();
}

/* Draw the full desktop scene without the mouse cursor. */
static void desktop_draw_scene(void) {
    struct Window *focused_window;
    uint32_t i;

    desktop_draw_background();
    desktop_draw_icons();
    focused_window = desktop_focused_window();
    for (i = 0u; i < desktop_window_count; i++) {
        struct Window *window = desktop_draw_order[i];

        if (window == focused_window) {
            continue;
        }
        if (desktop_window_is_visible(window)) {
            desktop_draw_window(window);
        }
    }
    if (desktop_window_is_visible(focused_window)) {
        desktop_draw_window(focused_window);
    }
    desktop_draw_taskbar();
}

static int desktop_any_visible_window_dirty(void) {
    uint32_t i;

    for (i = 0u; i < desktop_window_count; i++) {
        if (desktop_window_is_visible(desktop_draw_order[i]) && desktop_draw_order[i]->content_dirty) {
            return 1;
        }
    }

    return 0;
}

static int desktop_point_in_terminal_client(const struct Window *window, int x, int y) {
    int padding = 4;
    int client_x = window->x + DESKTOP_BORDER_WIDTH + padding;
    int client_y = window->y + DESKTOP_TITLEBAR_HEIGHT + padding;
    int client_w = window->width - (DESKTOP_BORDER_WIDTH * 2) - (padding * 2);
    int client_h = window->height - DESKTOP_TITLEBAR_HEIGHT - DESKTOP_BORDER_WIDTH - (padding * 2);

    return desktop_window_is_visible(window)
        && x >= client_x && y >= client_y
        && x < client_x + client_w && y < client_y + client_h;
}

static int desktop_window_resize_edges(const struct Window *window, int x, int y) {
    int edge = 4;
    int edges = DESKTOP_RESIZE_NONE;

    if (!desktop_window_is_visible(window) || window->maximized) {
        return DESKTOP_RESIZE_NONE;
    }
    if (x < window->x || y < window->y || x >= window->x + window->width || y >= window->y + window->height) {
        return DESKTOP_RESIZE_NONE;
    }

    if (x < window->x + edge) {
        edges |= DESKTOP_RESIZE_LEFT;
    }
    if (x >= window->x + window->width - edge) {
        edges |= DESKTOP_RESIZE_RIGHT;
    }
    if (y < window->y + edge) {
        edges |= DESKTOP_RESIZE_TOP;
    }
    if (y >= window->y + window->height - edge) {
        edges |= DESKTOP_RESIZE_BOTTOM;
    }

    return edges;
}

static void desktop_resize_window_to_cursor(struct Window *window, int x, int y) {
    int dx;
    int dy;
    int nx;
    int ny;
    int nw;
    int nh;
    int max_w;
    int max_h;

    if (window == (struct Window *)0 || desktop_resize_edges == DESKTOP_RESIZE_NONE) {
        return;
    }

    dx = x - desktop_resize_start_x;
    dy = y - desktop_resize_start_y;
    nx = desktop_resize_orig_x;
    ny = desktop_resize_orig_y;
    nw = desktop_resize_orig_w;
    nh = desktop_resize_orig_h;

    if ((desktop_resize_edges & DESKTOP_RESIZE_LEFT) != 0) {
        nx = desktop_resize_orig_x + dx;
        nw = desktop_resize_orig_w - dx;
        if (nw < DESKTOP_WINDOW_MIN_WIDTH) {
            nw = DESKTOP_WINDOW_MIN_WIDTH;
            nx = desktop_resize_orig_x + desktop_resize_orig_w - nw;
        }
        if (nx < 0) {
            nx = 0;
            nw = desktop_resize_orig_x + desktop_resize_orig_w;
        }
    }
    if ((desktop_resize_edges & DESKTOP_RESIZE_RIGHT) != 0) {
        nw = desktop_resize_orig_w + dx;
        max_w = gfx_get_width() - nx;
        if (nw > max_w) {
            nw = max_w;
        }
        if (nw < DESKTOP_WINDOW_MIN_WIDTH) {
            nw = DESKTOP_WINDOW_MIN_WIDTH;
        }
    }
    if ((desktop_resize_edges & DESKTOP_RESIZE_TOP) != 0) {
        ny = desktop_resize_orig_y + dy;
        nh = desktop_resize_orig_h - dy;
        if (nh < DESKTOP_WINDOW_MIN_HEIGHT) {
            nh = DESKTOP_WINDOW_MIN_HEIGHT;
            ny = desktop_resize_orig_y + desktop_resize_orig_h - nh;
        }
        if (ny < 0) {
            ny = 0;
            nh = desktop_resize_orig_y + desktop_resize_orig_h;
        }
    }
    if ((desktop_resize_edges & DESKTOP_RESIZE_BOTTOM) != 0) {
        nh = desktop_resize_orig_h + dy;
        max_h = (gfx_get_height() - DESKTOP_TASKBAR_HEIGHT) - ny;
        if (nh > max_h) {
            nh = max_h;
        }
        if (nh < DESKTOP_WINDOW_MIN_HEIGHT) {
            nh = DESKTOP_WINDOW_MIN_HEIGHT;
        }
    }

    max_w = gfx_get_width() - nx;
    max_h = (gfx_get_height() - DESKTOP_TASKBAR_HEIGHT) - ny;
    if (nw > max_w) {
        nw = max_w;
    }
    if (nh > max_h) {
        nh = max_h;
    }
    if (nw < DESKTOP_WINDOW_MIN_WIDTH) {
        nw = DESKTOP_WINDOW_MIN_WIDTH;
    }
    if (nh < DESKTOP_WINDOW_MIN_HEIGHT) {
        nh = DESKTOP_WINDOW_MIN_HEIGHT;
    }

    window->x = nx;
    window->y = ny;
    window->width = nw;
    window->height = nh;
    window->content_dirty = 1;
}

static const char *desktop_cursor_for_window_client(const struct Window *window, int left_down) {
    (void)left_down;

    if (window == (const struct Window *)0) {
        return "normal";
    }
    if (window == desktop_terminal_window) {
        return "text";
    }
    if (ascii_streq(window->title, "Paint")) {
        return "precision";
    }
    if (ascii_streq(window->title, "Notepad")) {
        return "text";
    }
    return "normal";
}

static const char *desktop_cursor_for_launcher(int icon_index) {
    (void)icon_index;
    return "link";
}

static const char *desktop_cursor_for_start_menu_entry(int entry_index) {
    int launcher_count = desktop_launcher_icon_count();

    if (entry_index < 0) {
        return "normal";
    }
    if (entry_index < launcher_count) {
        return desktop_cursor_for_launcher(entry_index);
    }
    return "link";
}

static const char *desktop_pick_resize_cursor(const struct Window *window, int x, int y) {
    int edges = desktop_window_resize_edges(window, x, y);

    if (edges == (DESKTOP_RESIZE_LEFT | DESKTOP_RESIZE_TOP)
        || edges == (DESKTOP_RESIZE_RIGHT | DESKTOP_RESIZE_BOTTOM)) {
        return "diagonal-resize-1";
    }
    if (edges == (DESKTOP_RESIZE_RIGHT | DESKTOP_RESIZE_TOP)
        || edges == (DESKTOP_RESIZE_LEFT | DESKTOP_RESIZE_BOTTOM)) {
        return "diagonal-resize-2";
    }
    if ((edges & (DESKTOP_RESIZE_LEFT | DESKTOP_RESIZE_RIGHT)) != 0) {
        return "horizontal-resize";
    }
    if ((edges & (DESKTOP_RESIZE_TOP | DESKTOP_RESIZE_BOTTOM)) != 0) {
        return "vertical-resize";
    }
    return (const char *)0;
}

static const char *desktop_pick_cursor(int x, int y, int buttons) {
    struct Window *window = desktop_find_window_at(x, y);
    int left_down = (buttons & 0x01) != 0;
    int icon_index = desktop_find_icon_at(x, y);
    int start_entry = desktop_start_menu_entry_at(x, y);
    const char *resize_cursor = (const char *)0;

    (void)x;
    (void)y;
    if ((int32_t)(desktop_busy_until_tick - get_ticks()) > 0) {
        return "working";
    }

    if (desktop_drag_icon >= 0) {
        return desktop_drag_icon_copy ? "alternate" : "move";
    }
    if (desktop_resize_window != (struct Window *)0) {
        if (desktop_resize_edges == (DESKTOP_RESIZE_LEFT | DESKTOP_RESIZE_TOP)
            || desktop_resize_edges == (DESKTOP_RESIZE_RIGHT | DESKTOP_RESIZE_BOTTOM)) {
            return "diagonal-resize-1";
        }
        if (desktop_resize_edges == (DESKTOP_RESIZE_RIGHT | DESKTOP_RESIZE_TOP)
            || desktop_resize_edges == (DESKTOP_RESIZE_LEFT | DESKTOP_RESIZE_BOTTOM)) {
            return "diagonal-resize-2";
        }
        if ((desktop_resize_edges & (DESKTOP_RESIZE_LEFT | DESKTOP_RESIZE_RIGHT)) != 0) {
            return "horizontal-resize";
        }
        if ((desktop_resize_edges & (DESKTOP_RESIZE_TOP | DESKTOP_RESIZE_BOTTOM)) != 0) {
            return "vertical-resize";
        }
        return "normal";
    }
    if (desktop_drag_window != (struct Window *)0) {
        return "move";
    }
    if (desktop_start_menu_open && start_entry >= 0) {
        return desktop_cursor_for_start_menu_entry(start_entry);
    }
    if (desktop_point_in_start_button(x, y)) {
        return "link";
    }
    if (desktop_find_taskbar_window_at(x, y) != (struct Window *)0) {
        return "link";
    }
    if (icon_index >= 0) {
        if ((buttons & 0x02) != 0) {
            return "alternate";
        }
        return desktop_cursor_for_launcher(desktop_icons[icon_index].launcher_index);
    }
    if (window != (struct Window *)0) {
        int button = desktop_hit_window_button(window, x, y);

        resize_cursor = desktop_pick_resize_cursor(window, x, y);
        if (resize_cursor != (const char *)0) {
            return resize_cursor;
        }

        if (button == DESKTOP_WINDOW_BUTTON_CLOSE) {
            return "link";
        }
        if (button == DESKTOP_WINDOW_BUTTON_MAXIMIZE) {
            return "link";
        }
        if (button == DESKTOP_WINDOW_BUTTON_MINIMIZE) {
            return "link";
        }
        if (desktop_point_in_titlebar(window, x, y)) {
            return "move";
        }
        if (window->app != (struct App *)0) {
            int client_x;
            int client_y;
            int client_w;
            int client_h;

            if (desktop_get_client_rect(window, &client_x, &client_y, &client_w, &client_h)
                && x >= client_x && y >= client_y
                && x < client_x + client_w && y < client_y + client_h) {
                return desktop_cursor_for_window_client(window, left_down);
            }
        } else if (window == desktop_terminal_window || window == desktop_clock_window || window == desktop_info_window) {
            return desktop_cursor_for_window_client(window, left_down);
        }
    }

    return "normal";
}

static void desktop_begin_busy(uint32_t ticks) {
    uint32_t until = get_ticks() + ticks;

    if ((int32_t)(until - desktop_busy_until_tick) > 0) {
        desktop_busy_until_tick = until;
    }
}

static void desktop_terminal_scroll(int delta) {
    int history_visible;
    uint32_t max_offset;
    int next_offset;

    if (desktop_terminal_window == (struct Window *)0 || !desktop_window_is_visible(desktop_terminal_window)) {
        return;
    }

    history_visible = ((desktop_terminal_window->height - DESKTOP_TITLEBAR_HEIGHT - DESKTOP_BORDER_WIDTH - 8)
        / desktop_font_height()) - 1;
    max_offset = desktop_terminal_scroll_max(history_visible);
    next_offset = (int)desktop_terminal_state.scroll_offset + delta;
    if (next_offset < 0) {
        next_offset = 0;
    }
    if ((uint32_t)next_offset > max_offset) {
        next_offset = (int)max_offset;
    }
    if ((uint32_t)next_offset != desktop_terminal_state.scroll_offset) {
        desktop_debug_hex("[desktop] scroll_apply_delta=", (uint32_t)delta);
        desktop_debug_hex("[desktop] scroll_apply_offset=", (uint32_t)next_offset);
        desktop_debug_hex("[desktop] scroll_apply_max=", max_offset);
        desktop_terminal_state.scroll_offset = (uint32_t)next_offset;
        desktop_terminal_window->content_dirty = 1;
    }
}

static void desktop_draw_window_button_icon(int which, int x, int y, int w, int h, uint32_t color) {
    int cx = x + (w / 2);
    int cy = y + (h / 2);

    if (which == DESKTOP_WINDOW_BUTTON_CLOSE) {
        int i;
        for (i = 0; i < 6; i++) {
            gfx_put_pixel((uint32_t)(cx - 3 + i), (uint32_t)(cy - 3 + i), color);
            gfx_put_pixel((uint32_t)(cx - 3 + i), (uint32_t)(cy + 2 - i), color);
        }
    } else if (which == DESKTOP_WINDOW_BUTTON_MAXIMIZE) {
        gfx_draw_rect_outline(x + 5, y + 3, 8, 6, color);
    } else if (which == DESKTOP_WINDOW_BUTTON_MINIMIZE) {
        gfx_draw_hline(x + 4, y + h - 4, 8, color);
    }
}

static void desktop_draw_window_controls(struct Window *window, uint32_t title_color) {
    int which;

    for (which = DESKTOP_WINDOW_BUTTON_MINIMIZE; which <= DESKTOP_WINDOW_BUTTON_CLOSE; which++) {
        int bx;
        int by;
        int bw;
        int bh;
        uint32_t fill = title_color;
        uint32_t border = desktop_adjust_color(title_color, -48);
        uint32_t icon = 0xF6F3E8u;

        (void)desktop_get_window_button_rect(window, which, &bx, &by, &bw, &bh);
        if (window == desktop_hover_window && which == desktop_hover_button) {
            fill = (which == DESKTOP_WINDOW_BUTTON_CLOSE) ? 0xCC0000u : 0x3B3B3Bu;
            border = (which == DESKTOP_WINDOW_BUTTON_CLOSE) ? 0x7A0000u : 0x191919u;
        }

        gfx_draw_rect(bx, by, bw, bh, fill);
        gfx_draw_rect_outline(bx, by, bw, bh, border);
        desktop_draw_window_button_icon(which, bx, by, bw, bh, icon);
    }
}

/* Handle mouse clicks, focus changes, dragging, and wheel scrolling. */
static int desktop_handle_mouse(int x, int y, int buttons) {
    int left_down = (buttons & 0x01) != 0;
    int right_down = (buttons & 0x02) != 0;
    int was_left_down = (desktop_last_buttons & 0x01) != 0;
    int was_right_down = (desktop_last_buttons & 0x02) != 0;
    int scene_dirty = 0;
    struct Window *window_under_cursor = desktop_find_window_at(x, y);
    struct Window *hover_app_window = (struct Window *)0;

    if (desktop_update_hover_state(x, y)) {
        scene_dirty = 1;
        desktop_taskbar_dirty = 1;
    }

    if (desktop_start_menu_open && left_down && !was_left_down) {
        int start_menu_entry = desktop_start_menu_entry_at(x, y);

        if (start_menu_entry >= 0) {
            desktop_start_menu_open = 0;
            desktop_taskbar_dirty = 1;
            scene_dirty = 1;
            scene_dirty |= desktop_activate_start_menu_entry(start_menu_entry);
            desktop_last_buttons = buttons;
            return scene_dirty;
        }

        if (!desktop_point_in_start_menu(x, y) && !desktop_point_in_start_button(x, y)) {
            desktop_start_menu_open = 0;
            desktop_taskbar_dirty = 1;
            scene_dirty = 1;
            desktop_last_buttons = buttons;
            return scene_dirty;
        }
    }

    if (window_under_cursor != (struct Window *)0 && window_under_cursor->app != (struct App *)0) {
        int client_x;
        int client_y;
        int client_w;
        int client_h;

        if (desktop_get_client_rect(window_under_cursor, &client_x, &client_y, &client_w, &client_h)
            && x >= client_x && y >= client_y
            && x < client_x + client_w && y < client_y + client_h) {
            /* App content can include hover states, so repaint while the pointer is inside. */
            window_under_cursor->content_dirty = 1;
            hover_app_window = window_under_cursor;
        }
    }

    if (hover_app_window != desktop_hover_app_window) {
        if (desktop_hover_app_window != (struct Window *)0) {
            desktop_hover_app_window->content_dirty = 1;
        }
        desktop_hover_app_window = hover_app_window;
    }

    if ((left_down || right_down) && desktop_pressed_icon >= 0 && desktop_drag_icon < 0) {
        int dx = x - desktop_pressed_icon_start_x;
        int dy = y - desktop_pressed_icon_start_y;

        if ((dx < 0 ? -dx : dx) >= DESKTOP_ICON_DRAG_THRESHOLD
            || (dy < 0 ? -dy : dy) >= DESKTOP_ICON_DRAG_THRESHOLD) {
            int drag_index = desktop_pressed_icon;

            if (desktop_pressed_icon_copy) {
                int new_icon = desktop_icon_create(desktop_icons[desktop_pressed_icon].launcher_index,
                                                   desktop_icons[desktop_pressed_icon].x,
                                                   desktop_icons[desktop_pressed_icon].y);
                if (new_icon >= 0) {
                    drag_index = new_icon;
                    desktop_focused_icon = new_icon;
                    desktop_last_icon_click_index = -1;
                }
            }

            desktop_drag_icon = drag_index;
            desktop_drag_icon_copy = desktop_pressed_icon_copy;
            desktop_drag_icon_offset_x = x - desktop_icons[drag_index].x;
            desktop_drag_icon_offset_y = y - desktop_icons[drag_index].y;
            scene_dirty = 1;
        }
    }

    if ((left_down || right_down) && desktop_drag_icon >= 0) {
        int old_x = desktop_icons[desktop_drag_icon].x;
        int old_y = desktop_icons[desktop_drag_icon].y;

        desktop_move_icon(desktop_drag_icon, x - desktop_drag_icon_offset_x, y - desktop_drag_icon_offset_y);
        if (desktop_icons[desktop_drag_icon].x != old_x || desktop_icons[desktop_drag_icon].y != old_y) {
            scene_dirty = 1;
        }
    }

    if (left_down && !was_left_down) {
        desktop_pressed_button = DESKTOP_WINDOW_BUTTON_NONE;
        desktop_pressed_window = (struct Window *)0;
        desktop_pressed_taskbar_window = (struct Window *)0;
        desktop_drag_window = (struct Window *)0;
        desktop_resize_window = (struct Window *)0;
        desktop_resize_edges = DESKTOP_RESIZE_NONE;
        desktop_pressed_icon = -1;
        desktop_drag_icon = -1;
        desktop_drag_icon_copy = 0;

        if (desktop_point_in_taskbar(x, y)) {
            if (desktop_point_in_start_button(x, y)) {
                desktop_start_menu_open = !desktop_start_menu_open;
                desktop_taskbar_dirty = 1;
                scene_dirty = 1;
                desktop_last_buttons = buttons;
                return scene_dirty;
            }
            desktop_pressed_taskbar_window = desktop_find_taskbar_window_at(x, y);
            desktop_last_buttons = buttons;
            return scene_dirty;
        }

        if (window_under_cursor != (struct Window *)0) {
            int resize_edges;

            desktop_focused_icon = -1;
            scene_dirty |= desktop_focus_window(window_under_cursor);
            desktop_pressed_button = desktop_hit_window_button(window_under_cursor, x, y);
            resize_edges = desktop_window_resize_edges(window_under_cursor, x, y);
            if (desktop_pressed_button != DESKTOP_WINDOW_BUTTON_NONE) {
                desktop_debug_hex("[desktop] button_press=", (uint32_t)desktop_pressed_button);
                desktop_debug_hex("[desktop] button_press_x=", (uint32_t)x);
                desktop_debug_hex("[desktop] button_press_y=", (uint32_t)y);
                desktop_pressed_window = window_under_cursor;
            } else if (resize_edges != DESKTOP_RESIZE_NONE) {
                desktop_resize_window = window_under_cursor;
                desktop_resize_edges = resize_edges;
                desktop_resize_start_x = x;
                desktop_resize_start_y = y;
                desktop_resize_orig_x = window_under_cursor->x;
                desktop_resize_orig_y = window_under_cursor->y;
                desktop_resize_orig_w = window_under_cursor->width;
                desktop_resize_orig_h = window_under_cursor->height;
            } else if (desktop_point_in_titlebar(window_under_cursor, x, y)) {
                desktop_drag_window = window_under_cursor;
                desktop_drag_offset_x = x - window_under_cursor->x;
                desktop_drag_offset_y = y - window_under_cursor->y;
            } else if (window_under_cursor->app != (struct App *)0
                       && window_under_cursor->app->on_click != (void (*)(int, int, int))0) {
                int client_x;
                int client_y;
                int client_w;
                int client_h;

                if (desktop_get_client_rect(window_under_cursor, &client_x, &client_y, &client_w, &client_h)
                    && x >= client_x && y >= client_y
                    && x < client_x + client_w && y < client_y + client_h) {
                    sound_click();
                    window_under_cursor->app->on_click(x - client_x, y - client_y, buttons & 0x07);
                    window_under_cursor->content_dirty = 1;
                }
            }
        } else {
            int icon_index = desktop_find_icon_at(x, y);

            if (icon_index >= 0) {
                desktop_focused_icon = icon_index;
                desktop_pressed_icon = icon_index;
                desktop_pressed_icon_start_x = x;
                desktop_pressed_icon_start_y = y;
                desktop_pressed_icon_copy = 0;
                scene_dirty = 1;
            } else if (desktop_focused_icon != -1) {
                desktop_focused_icon = -1;
                desktop_last_icon_click_index = -1;
                scene_dirty = 1;
            }
        }
    }

    if (right_down && !was_right_down && window_under_cursor == (struct Window *)0 && !desktop_point_in_taskbar(x, y)) {
        int icon_index = desktop_find_icon_at(x, y);

        if (icon_index >= 0) {
            desktop_focused_icon = icon_index;
            desktop_pressed_icon = icon_index;
            desktop_pressed_icon_start_x = x;
            desktop_pressed_icon_start_y = y;
            desktop_pressed_icon_copy = 1;
            desktop_drag_icon = -1;
            desktop_drag_icon_copy = 0;
            desktop_last_icon_click_index = -1;
            scene_dirty = 1;
        }
    }

    if (left_down && desktop_resize_window != (struct Window *)0) {
        int old_x = desktop_resize_window->x;
        int old_y = desktop_resize_window->y;
        int old_w = desktop_resize_window->width;
        int old_h = desktop_resize_window->height;

        desktop_resize_window_to_cursor(desktop_resize_window, x, y);
        if (desktop_resize_window->x != old_x || desktop_resize_window->y != old_y
            || desktop_resize_window->width != old_w || desktop_resize_window->height != old_h) {
            scene_dirty = 1;
        }
    }

    if (left_down && desktop_drag_window != (struct Window *)0) {
        int old_x = desktop_drag_window->x;
        int old_y = desktop_drag_window->y;
        desktop_move_window(desktop_drag_window, x - desktop_drag_offset_x, y - desktop_drag_offset_y);
        if (desktop_drag_window->x != old_x || desktop_drag_window->y != old_y) {
            /* Moving a window changes overlap ordering, so redraw the scene once instead of every tick. */
            scene_dirty = 1;
        }
    }

    if (!left_down && was_left_down) {
        if (desktop_drag_icon >= 0 && !desktop_drag_icon_copy) {
            desktop_snap_icon_to_grid(desktop_drag_icon);
            scene_dirty = 1;
        }
        if (desktop_pressed_taskbar_window != (struct Window *)0
            && desktop_find_taskbar_window_at(x, y) == desktop_pressed_taskbar_window) {
            desktop_restore_window(desktop_pressed_taskbar_window);
            scene_dirty = 1;
            desktop_taskbar_dirty = 1;
            scene_dirty |= desktop_focus_window(desktop_pressed_taskbar_window);
        } else if (desktop_pressed_window != (struct Window *)0
                   && desktop_hit_window_button(desktop_pressed_window, x, y) == desktop_pressed_button) {
            desktop_debug_hex("[desktop] button_release=", (uint32_t)desktop_pressed_button);
            desktop_debug_hex("[desktop] button_release_x=", (uint32_t)x);
            desktop_debug_hex("[desktop] button_release_y=", (uint32_t)y);
            if (desktop_pressed_button == DESKTOP_WINDOW_BUTTON_CLOSE) {
                desktop_close_window(desktop_pressed_window);
                desktop_focus_top_visible_window();
                if (desktop_pressed_window->app != (struct App *)0) {
                    desktop_taskbar_dirty = 1;
                }
            } else if (desktop_pressed_button == DESKTOP_WINDOW_BUTTON_MAXIMIZE) {
                desktop_maximize_window(desktop_pressed_window);
                scene_dirty |= desktop_focus_window(desktop_pressed_window);
            } else if (desktop_pressed_button == DESKTOP_WINDOW_BUTTON_MINIMIZE) {
                desktop_minimize_window(desktop_pressed_window);
                desktop_focus_top_visible_window();
                desktop_taskbar_dirty = 1;
            }
            scene_dirty = 1;
        } else if (desktop_pressed_icon >= 0 && desktop_drag_icon < 0 && !desktop_pressed_icon_copy) {
            if (desktop_focused_icon != desktop_pressed_icon) {
                desktop_focused_icon = desktop_pressed_icon;
                scene_dirty = 1;
            } else if (desktop_last_icon_click_index == desktop_pressed_icon) {
                scene_dirty |= desktop_open_icon(desktop_pressed_icon);
                desktop_focused_icon = -1;
                desktop_last_icon_click_index = -1;
            } else {
                desktop_last_icon_click_index = desktop_pressed_icon;
            }
        }

        desktop_drag_window = (struct Window *)0;
        desktop_resize_window = (struct Window *)0;
        desktop_resize_edges = DESKTOP_RESIZE_NONE;
        desktop_pressed_window = (struct Window *)0;
        desktop_pressed_taskbar_window = (struct Window *)0;
        desktop_pressed_button = DESKTOP_WINDOW_BUTTON_NONE;
        desktop_pressed_icon = -1;
        desktop_drag_icon = -1;
        desktop_drag_icon_copy = 0;
    } else if (!left_down) {
        desktop_drag_window = (struct Window *)0;
        desktop_resize_window = (struct Window *)0;
        desktop_resize_edges = DESKTOP_RESIZE_NONE;
    }

    if (!right_down && was_right_down) {
        if (desktop_drag_icon >= 0 && desktop_drag_icon_copy) {
            desktop_snap_icon_to_grid(desktop_drag_icon);
            scene_dirty = 1;
        }
        desktop_pressed_icon = -1;
        desktop_drag_icon = -1;
        desktop_drag_icon_copy = 0;
    }

    desktop_last_buttons = buttons;
    return scene_dirty;
}

static void desktop_draw_window_contents(struct Window *window, int force_content) {
    if (window->app != (struct App *)0 && window->app->on_draw != (void (*)(int, int, int, int))0
        && (force_content || window->content_dirty)) {
        int client_x;
        int client_y;
        int client_w;
        int client_h;

        if (desktop_get_client_rect(window, &client_x, &client_y, &client_w, &client_h)) {
            app_set_draw_context(client_x, client_y, client_w, client_h);
            window->app->on_draw(client_x, client_y, client_w, client_h);
            app_clear_draw_context();
        }
        window->content_dirty = 0;
        return;
    }

    if (window->on_draw != (WindowDrawCallback)0 && (force_content || window->content_dirty)) {
        window->on_draw(window);
        window->content_dirty = 0;
    }
}

static void desktop_redraw_window(struct Window *window, int force_content) {
    uint32_t title_color = window->focused
        ? desktop_adjust_color(window->bg_color, 24)
        : desktop_adjust_color(window->bg_color, -24);
    int title_text_y = desktop_center_text_y(window->y, DESKTOP_TITLEBAR_HEIGHT);

    gfx_draw_rect(window->x, window->y, window->width, window->height, window->bg_color);
    gfx_draw_rect_outline(window->x, window->y, window->width, window->height, 0x1A1A1Au);
    gfx_draw_rect(window->x + DESKTOP_BORDER_WIDTH, window->y + DESKTOP_BORDER_WIDTH,
                  window->width - (DESKTOP_BORDER_WIDTH * 2), DESKTOP_TITLEBAR_HEIGHT - DESKTOP_BORDER_WIDTH,
                  title_color);
    gfx_draw_string_at(window->x + 6, title_text_y, window->title, 0xF6F3E8u, title_color);
    desktop_draw_window_controls(window, title_color);
    desktop_draw_window_contents(window, force_content);
}

/* Populate the initial desktop windows and reset the desktop state. */
static void desktop_init(void) {
    uint32_t i;

    desktop_window_count = 0u;
    desktop_terminal_window = (struct Window *)0;
    desktop_info_window = (struct Window *)0;
    desktop_clock_window = (struct Window *)0;
    desktop_drag_window = (struct Window *)0;
    desktop_drag_offset_x = 0;
    desktop_drag_offset_y = 0;
    desktop_resize_window = (struct Window *)0;
    desktop_resize_edges = DESKTOP_RESIZE_NONE;
    desktop_resize_start_x = 0;
    desktop_resize_start_y = 0;
    desktop_resize_orig_x = 0;
    desktop_resize_orig_y = 0;
    desktop_resize_orig_w = 0;
    desktop_resize_orig_h = 0;
    desktop_last_buttons = 0;
    desktop_pressed_window = (struct Window *)0;
    desktop_pressed_taskbar_window = (struct Window *)0;
    desktop_pressed_button = DESKTOP_WINDOW_BUTTON_NONE;
    desktop_hover_window = (struct Window *)0;
    desktop_hover_taskbar_window = (struct Window *)0;
    desktop_hover_app_window = (struct Window *)0;
    desktop_hover_button = DESKTOP_WINDOW_BUTTON_NONE;
    info_last_refresh_sec = 0xFFFFFFFFu;
    desktop_last_clock_second = 0xFFFFFFFFu;
    desktop_last_info_bucket = 0xFFFFFFFFu;
    desktop_busy_until_tick = 0u;
    desktop_taskbar_dirty = 1;
    desktop_scene_dirty = 1;
    desktop_start_menu_open = 0;
    desktop_focused_icon = -1;
    desktop_last_icon_click_index = -1;
    desktop_last_icon_click_tick = 0u;
    desktop_pressed_icon = -1;
    desktop_pressed_icon_start_x = 0;
    desktop_pressed_icon_start_y = 0;
    desktop_pressed_icon_copy = 0;
    desktop_drag_icon = -1;
    desktop_drag_icon_offset_x = 0;
    desktop_drag_icon_offset_y = 0;
    desktop_drag_icon_copy = 0;

    for (i = 0u; i < DESKTOP_WINDOW_MAX; i++) {
        desktop_draw_order[i] = (struct Window *)0;
    }
    for (i = 0u; i < MAX_APPS; i++) {
        desktop_app_windows[i] = (struct Window *)0;
    }
    desktop_init_icons();

    desktop_init_cpu_brand();
    desktop_terminal_reset();
    terminal_print("coffeeOS developer preview\n");
    terminal_print("Type 'help' to list commands.\n");
    desktop_terminal_window =
        desktop_create_window(20, 40, 660, 320, "Terminal", 0x20333Cu, desktop_draw_terminal,
                              (struct App *)0);
    desktop_info_window =
        desktop_create_window(440, 40, DESKTOP_INFO_WINDOW_WIDTH, DESKTOP_INFO_WINDOW_HEIGHT,
                              "System Info", 0xD7C7A9u, desktop_draw_info, (struct App *)0);
    desktop_clock_window =
        desktop_create_window(440, 180, 200, 60, "Clock", 0xE2B36Eu, desktop_draw_clock,
                              (struct App *)0);
    (void)desktop_focus_window(desktop_terminal_window);
    if (desktop_terminal_window != (struct Window *)0) {
        desktop_terminal_window->content_dirty = 1;
    }
    if (desktop_info_window != (struct Window *)0) {
        desktop_info_window->content_dirty = 1;
    }
    if (desktop_clock_window != (struct Window *)0) {
        desktop_clock_window->content_dirty = 1;
    }
}

/* Create one window in the fixed desktop window array. */
struct Window *desktop_create_window(int x, int y, int width, int height,
                                     const char *title, uint32_t bg_color,
                                     WindowDrawCallback on_draw, struct App *app) {
    struct Window *window;

    if (desktop_window_count >= DESKTOP_WINDOW_MAX) {
        return (struct Window *)0;
    }

    window = &desktop_windows[desktop_window_count];
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    desktop_copy_string(window->title, (uint32_t)sizeof(window->title), title);
    window->bg_color = bg_color;
    window->focused = 0;
    window->content_dirty = 1;
    window->hidden = 0;
    window->minimized = 0;
    window->maximized = 0;
    window->restore_x = x;
    window->restore_y = y;
    window->restore_width = width;
    window->restore_height = height;
    window->app = app;
    window->on_draw = on_draw;
    desktop_draw_order[desktop_window_count] = window;
    desktop_window_count++;
    desktop_move_window(window, x, y);
    if (app != (struct App *)0 && app->on_init != (void (*)(void))0) {
        app->on_init();
    }
    return window;
}

/* Move a window while keeping it on screen above the taskbar. */
void desktop_move_window(struct Window *window, int x, int y) {
    int max_x = gfx_get_width() - window->width;
    int max_y = (gfx_get_height() - DESKTOP_TASKBAR_HEIGHT) - window->height;

    if (max_x < 0) {
        max_x = 0;
    }
    if (max_y < 0) {
        max_y = 0;
    }

    window->x = desktop_clamp(x, 0, max_x);
    window->y = desktop_clamp(y, 0, max_y);
}

/* Draw one window frame and then let its app callback paint the client area. */
void desktop_draw_window(struct Window *window) {
    desktop_redraw_window(window, 1);
}

/* Run the desktop event loop until a hotkey returns control to the shell. */
void desktop_run(void) {
    int buttons = 0;
    int last_cursor_x;
    int last_cursor_y;

    desktop_running = 1;
    desktop_exit_to_shell = 0;

    /* Clear any old text-console frame before the desktop draws its first scene. */
    gfx_fill_screen(DESKTOP_BACKGROUND_COLOR);
    gfx_present();

    app_registry_init();
    desktop_init();
    desktop_begin_busy(12u);
    gfx_set_output_target(GFX_OUTPUT_GUI_TERMINAL);
    mouse_get_pos(&desktop_cursor_x, &desktop_cursor_y);
    last_cursor_x = desktop_cursor_x;
    last_cursor_y = desktop_cursor_y;
    desktop_draw_scene();
    gfx_set_cursor(desktop_pick_cursor(desktop_cursor_x, desktop_cursor_y, buttons));
    gfx_draw_cursor(desktop_cursor_x, desktop_cursor_y);
    gfx_present();
    sound_startup();

    for (;;) {
        char key;
        uint32_t frame_start = get_ticks();
        uint32_t seconds = frame_start / 100u;
        uint32_t info_bucket = frame_start / 100u;
        int window_dirty;
        int mouse_moved = 0;
        int rendered = 0;
        int wheel = 0;

        speaker_update();
        if (pit_take_fs_sync_request()) {
            (void)fat32_sync(0);
            (void)ramdisk_sync_backing_store();
        }

        if (mouse_get_state((int *)0, (int *)0, &buttons, &wheel)) {
            mouse_get_pos(&desktop_cursor_x, &desktop_cursor_y);
            mouse_moved = (desktop_cursor_x != last_cursor_x) || (desktop_cursor_y != last_cursor_y);
            if (mouse_moved && desktop_start_menu_open) {
                desktop_taskbar_dirty = 1;
            }
            desktop_scene_dirty |= desktop_handle_mouse(desktop_cursor_x, desktop_cursor_y, buttons);
            if (wheel != 0) {
                desktop_debug_hex("[desktop] scroll_detected=", (uint32_t)wheel);
                desktop_debug_hex("[desktop] scroll_cursor_x=", (uint32_t)desktop_cursor_x);
                desktop_debug_hex("[desktop] scroll_cursor_y=", (uint32_t)desktop_cursor_y);
            }
            if (wheel != 0
                && desktop_terminal_window != (struct Window *)0
                && desktop_point_in_terminal_client(desktop_terminal_window, desktop_cursor_x, desktop_cursor_y)) {
                desktop_terminal_scroll((wheel > 0) ? TERMINAL_SCROLL_STEP : -TERMINAL_SCROLL_STEP);
            }
        }

        while (keyboard_read_char(&key)) {
            struct Window *focused_window = desktop_focused_window();

            if (focused_window != (struct Window *)0
                && focused_window->app != (struct App *)0
                && focused_window->app->on_key != (void (*)(char))0) {
                focused_window->app->on_key(key);
                focused_window->content_dirty = 1;
            } else if (desktop_terminal_window != (struct Window *)0
                && desktop_terminal_window->focused
                && desktop_window_is_visible(desktop_terminal_window)) {
                desktop_terminal_handle_key(key);
            }
        }

        window_dirty = desktop_any_visible_window_dirty();
        if (mouse_moved || desktop_scene_dirty || window_dirty || desktop_taskbar_dirty) {
            gfx_erase_cursor(last_cursor_x, last_cursor_y);
        }

        if (desktop_exit_to_shell) {
            desktop_running = 0;
            gfx_set_output_target(GFX_OUTPUT_FRAMEBUFFER);
            gfx_clear();
            return;
        }

        if (desktop_last_clock_second != seconds) {
            desktop_last_clock_second = seconds;
            if (desktop_clock_window != (struct Window *)0) {
                /* Clock redraws only when the displayed second actually changes. */
                desktop_clock_window->content_dirty = 1;
            }
            desktop_taskbar_dirty = 1;
        }

        if (desktop_last_info_bucket != info_bucket) {
            desktop_last_info_bucket = info_bucket;
            if (desktop_info_window != (struct Window *)0) {
                /* System info refreshes on the same 100 Hz second boundary used for uptime/pages. */
                desktop_info_window->content_dirty = 1;
            }
        }

        if (desktop_scene_dirty) {
            desktop_draw_scene();
            desktop_scene_dirty = 0;
            desktop_taskbar_dirty = 0;
            rendered = 1;
        } else {
            if (window_dirty) {
                desktop_draw_scene();
                desktop_taskbar_dirty = 0;
                rendered = 1;
            } else if (desktop_taskbar_dirty) {
                desktop_draw_taskbar();
                desktop_taskbar_dirty = 0;
                rendered = 1;
            }
        }

        if (mouse_moved || rendered) {
            gfx_set_cursor(desktop_pick_cursor(desktop_cursor_x, desktop_cursor_y, buttons));
            gfx_draw_cursor(desktop_cursor_x, desktop_cursor_y);
            gfx_present();
            last_cursor_x = desktop_cursor_x;
            last_cursor_y = desktop_cursor_y;
        }

        /* Cap the desktop loop at ~50 FPS on a 100 Hz PIT so idle GUI mode stops burning the whole CPU. */
        while ((int32_t)(get_ticks() - (frame_start + 2u)) < 0) {
        }
    }
}

int desktop_is_running(void) {
    return desktop_running;
}

void desktop_request_kernel_shell(void) {
    if (desktop_running) {
        desktop_exit_to_shell = 1;
    }
}
