#include <stdint.h>

#include "app.h"
#include "desktop.h"

#define HELLO_BG          0x101923u
#define HELLO_PANEL       0x172532u
#define HELLO_PANEL_ALT   0x1D3140u
#define HELLO_ACCENT      0x49B99Fu
#define HELLO_ACCENT_DARK 0x257E71u
#define HELLO_TEXT        0xEAF7F4u
#define HELLO_MUTED       0x91AAB4u
#define HELLO_LINE        0x345061u
#define HELLO_BUTTON      0x223947u

#define HELLO_ACTION_COUNT 3

struct HelloAction {
    const char *label;
    const char *hint;
    const char *target;
    int x;
    int y;
    int w;
    int h;
};

static int hello_tip_index;
static struct HelloAction hello_actions[HELLO_ACTION_COUNT];

static const char *hello_tips[] = {
    "Tip: Double-click desktop icons to open apps.",
    "Tip: Drag windows by their title bars.",
    "Tip: Use the Start menu for Terminal, Clock, and System Info.",
    "Tip: Files can open text documents in Notepad."
};

static void hello_on_init(void) {
    hello_tip_index = 0;
}

static int hello_center_x(const char *text, int width) {
    int text_w = app_text_width(text);
    int x = (width - text_w) / 2;

    if (x < 0) {
        x = 0;
    }
    return x;
}

static void hello_set_action(int index, const char *label, const char *hint, const char *target,
                             int x, int y, int w, int h) {
    if (index < 0 || index >= HELLO_ACTION_COUNT) {
        return;
    }
    hello_actions[index].label = label;
    hello_actions[index].hint = hint;
    hello_actions[index].target = target;
    hello_actions[index].x = x;
    hello_actions[index].y = y;
    hello_actions[index].w = w;
    hello_actions[index].h = h;
}

static void hello_draw_action(const struct HelloAction *action) {
    app_draw_rect(action->x, action->y, action->w, action->h, HELLO_BUTTON);
    app_draw_border(action->x, action->y, action->w, action->h, HELLO_LINE);
    app_draw_string(action->x + 12, action->y + 8, action->label, HELLO_TEXT, HELLO_BUTTON);
    app_draw_string(action->x + 12, action->y + 28, action->hint, HELLO_MUTED, HELLO_BUTTON);
    app_draw_rect(action->x + action->w - 18, action->y + 17, 7, 2, HELLO_ACCENT);
    app_draw_rect(action->x + action->w - 13, action->y + 12, 2, 12, HELLO_ACCENT);
}

static void hello_on_draw(int win_x, int win_y, int win_w, int win_h) {
    int i;
    int content_w = app_client_width();
    int content_h = app_client_height();
    int panel_x = 18;
    int panel_y = 92;
    int panel_w = content_w - 36;
    int action_y;
    int action_w;
    int action_gap = 10;
    int glow = app_anim_pingpong(120u, 40);
    int sweep_x;
    uint32_t header = app_blend_color(0x142636u, 0x1C3A48u, (uint32_t)glow, 40u);

    (void)win_x;
    (void)win_y;
    (void)win_w;
    (void)win_h;

    if (content_w <= 0 || content_h <= 0) {
        return;
    }

    if (panel_w < 260) {
        panel_w = content_w - 12;
        panel_x = 6;
    }

    app_clear(HELLO_BG);

    app_draw_rect(0, 0, content_w, 64, header);
    sweep_x = app_anim_saw(180u, content_w + 64) - 64;
    app_draw_rect(sweep_x, 0, 24, 64, app_blend_color(header, HELLO_ACCENT_DARK, 1u, 3u));
    app_draw_rect(0, 62, content_w, 2, HELLO_ACCENT);
    app_draw_string(hello_center_x("Welcome to coffeeOS", content_w), 14,
                    "Welcome to coffeeOS", HELLO_TEXT, header);
    app_draw_string(hello_center_x("Aurora Refresh is ready.", content_w), 38,
                    "Aurora Refresh is ready.", HELLO_MUTED, header);

    app_draw_rect(panel_x, panel_y, panel_w, 72, HELLO_PANEL);
    app_draw_border(panel_x, panel_y, panel_w, 72, HELLO_LINE);
    app_draw_rect(panel_x, panel_y, 6, 72, HELLO_ACCENT_DARK);
    app_draw_string(panel_x + 18, panel_y + 12, "Start here", HELLO_TEXT, HELLO_PANEL);
    app_draw_string(panel_x + 18, panel_y + 34,
                    "Choose a task below or open more tools from Start.",
                    HELLO_MUTED, HELLO_PANEL);

    action_y = panel_y + 92;
    action_w = (panel_w - (action_gap * 2)) / 3;
    if (action_w < 120) {
        action_w = panel_w;
        action_gap = 0;
    }

    hello_set_action(0, "Browse files", "Open Files", "Files",
                     panel_x, action_y, action_w, 54);
    hello_set_action(1, "Write notes", "Open Notepad", "Notepad",
                     panel_x + action_w + action_gap, action_y, action_w, 54);
    hello_set_action(2, "Calculate", "Open Calculator", "Calculator",
                     panel_x + ((action_w + action_gap) * 2), action_y, action_w, 54);

    for (i = 0; i < HELLO_ACTION_COUNT; i++) {
        if (hello_actions[i].x + hello_actions[i].w <= content_w) {
            hello_draw_action(&hello_actions[i]);
        }
    }

    app_draw_rect(panel_x, content_h - 50, panel_w, 32, HELLO_PANEL_ALT);
    app_draw_border(panel_x, content_h - 50, panel_w, 32, HELLO_LINE);
    app_draw_string(panel_x + 10, content_h - 42,
                    hello_tips[hello_tip_index], HELLO_TEXT, HELLO_PANEL_ALT);
}

static int hello_action_at(int x, int y) {
    int i;

    for (i = 0; i < HELLO_ACTION_COUNT; i++) {
        if (app_point_in_rect(x, y, hello_actions[i].x, hello_actions[i].y,
                              hello_actions[i].w, hello_actions[i].h)) {
            return i;
        }
    }
    return -1;
}

static void hello_on_click(int x, int y, int btn) {
    int action_index;

    (void)btn;

    action_index = hello_action_at(x, y);
    if (action_index >= 0) {
        (void)desktop_launch_app_by_title(hello_actions[action_index].target);
        return;
    }

    hello_tip_index++;
    if (hello_tip_index >= (int)(sizeof(hello_tips) / sizeof(hello_tips[0]))) {
        hello_tip_index = 0;
    }
}

App hello_app = {
    .title = "Welcome",
    .x = 220,
    .y = 80,
    .w = 580,
    .h = 340,
    .bg_color = HELLO_BG,
    .on_init = hello_on_init,
    .on_draw = hello_on_draw,
    .on_key = 0,
    .on_click = hello_on_click,
    .on_close = 0,
    .id = "hello-app",
    .flags = APP_FLAG_SINGLE_INSTANCE | APP_FLAG_RESIZABLE | APP_FLAG_ANIMATED,
    .min_w = 360,
    .min_h = 260
};
