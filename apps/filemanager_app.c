#include <stdint.h>

#include "app.h"
#include "ascii_util.h"
#include "desktop.h"
#include "fat32.h"
#include "keyboard.h"
#include "notepad_app.h"
#include "pit.h"
#include "vfs.h"

/* ── Layout constants ─────────────────────────────────────────── */
#define FM_ADDR_H       26
#define FM_TOOL_H       26
#define FM_STATUS_H     22
#define FM_ROW_H        26
#define FM_BUTTON_W     62
#define FM_BUTTON_GAP    3
#define FM_SCROLLBAR_W  10
#define FM_MAX_ENTRIES  64
#define FM_ICON_W       14
#define FM_ICON_H       14

/* ── coffeeOS aurora dark palette ────────────────────────────── */
/* Background layers */
#define FM_BG           0x1A2228u   /* main window bg   */
#define FM_ADDR_BG      0x111820u   /* address bar      */
#define FM_TOOL_BG      0x1E2B35u   /* toolbar          */
#define FM_STATUS_BG    0x141C24u   /* status bar       */

/* Row colors */
#define FM_ROW_A        0x1E2B35u   /* odd rows         */
#define FM_ROW_B        0x1A2530u   /* even rows        */
#define FM_ROW_HOVER    0x243545u   /* hovered row      */
#define FM_SELECT_BG    0x2A6E5Au   /* selected row bg  */
#define FM_SELECT_STRIPE 0x35876Cu  /* selected row left accent stripe */

/* Text */
#define FM_TEXT         0xC8D8E4u   /* primary text     */
#define FM_TEXT_DIM     0x607A8Au   /* secondary text   */
#define FM_ADDR_TEXT    0xE8F2FAu   /* address bar text */
#define FM_SELECT_TEXT  0xEEFAF6u   /* selected row text*/
#define FM_ACCENT       0x3CB88Au   /* teal accent      */
#define FM_WARN         0xE07050u   /* delete / warning */

/* Icons */
#define FM_DIR_FILL     0x2E78C7u   /* folder fill      */
#define FM_DIR_SHINE    0x5AAAE8u   /* folder highlight */
#define FM_FILE_FILL    0xC8D8E4u   /* file fill        */
#define FM_FILE_LINE    0x8AAABBU   /* file fold line   */

/* Scrollbar */
#define FM_SCROLL       0x3CB88Au   /* thumb            */
#define FM_SCROLL_TRACK 0x152030u   /* track            */

/* Toolbar buttons */
#define FM_BTN_BG       0x253545u
#define FM_BTN_HOVER    0x2E4255u
#define FM_BTN_BORDER   0x3A5060u
#define FM_BTN_TEXT     0xC0D4E0u

/* Separator line */
#define FM_SEP          0x243040u

/* ── State ─────────────────────────────────────────────────────── */
static char        fm_cwd[VFS_MAX_PATH];
static char        fm_prev[VFS_MAX_PATH];
static VfsDirEntry fm_entries[FM_MAX_ENTRIES];
static int         fm_entry_count;
static int         fm_selected;
static int         fm_scroll;
static int         fm_last_click_row;
static uint32_t    fm_last_click_tick;
static int         fm_win_w;   /* cached from last on_draw call */
static int         fm_win_h;   /* cached from last on_draw call */

/* ── String helpers ─────────────────────────────────────────────── */

static void fm_copy(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i = 0u;
    if (dst_len == 0u) return;
    while (src[i] != '\0' && i + 1u < dst_len) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void fm_join_path(const char *base, const char *leaf,
                          char *out, uint32_t out_len) {
    uint32_t i = 0u, j = 0u;
    if (base[0] == '/' && base[1] == '\0') {
        out[j++] = '/';
    } else {
        while (base[i] != '\0' && j + 1u < out_len) out[j++] = base[i++];
        if (j + 1u < out_len) out[j++] = '/';
    }
    i = 0u;
    while (leaf[i] != '\0' && j + 1u < out_len) out[j++] = leaf[i++];
    out[j] = '\0';
}

static void fm_parent_path(const char *path, char *out, uint32_t out_len) {
    uint32_t len;
    if (ascii_streq(path, "/")) { fm_copy(out, out_len, "/"); return; }
    len = ascii_strlen(path);
    while (len > 1u && path[len - 1u] == '/') len--;
    while (len > 1u && path[len - 1u] != '/') len--;
    if (len <= 1u) { fm_copy(out, out_len, "/"); return; }
    if (len >= out_len) len = out_len - 1u;
    { uint32_t i; for (i = 0u; i < len - 1u; i++) out[i] = path[i]; }
    out[len - 1u] = '\0';
}

static const char *fm_extension(const char *name) {
    uint32_t len = ascii_strlen(name);
    while (len > 0u) { len--; if (name[len] == '.') return name + len + 1u; }
    return "";
}

/* ── Icon renderers ─────────────────────────────────────────────── */

/* Draw a small folder icon at (x,y) relative to client area. */
static void fm_draw_folder_icon(int x, int y) {
    /* tab on top-left */
    app_draw_rect(x,     y + 2, 5, 2, FM_DIR_FILL);
    /* main body */
    app_draw_rect(x,     y + 4, FM_ICON_W,     FM_ICON_H - 4, FM_DIR_FILL);
    /* shine strip along top of body */
    app_draw_rect(x + 1, y + 5, FM_ICON_W - 2, 2,             FM_DIR_SHINE);
}

/* Draw a small document icon at (x,y) relative to client area. */
static void fm_draw_file_icon(int x, int y) {
    int fold = 4; /* folded corner size */
    /* body minus top-right corner */
    app_draw_rect(x, y, FM_ICON_W - fold, FM_ICON_H, FM_FILE_FILL);
    app_draw_rect(x + FM_ICON_W - fold, y + fold,
                  fold, FM_ICON_H - fold, FM_FILE_FILL);
    /* folded corner triangle (two thin rects) */
    app_draw_rect(x + FM_ICON_W - fold, y,     fold - 1, fold - 1, FM_BG);
    app_draw_rect(x + FM_ICON_W - fold, y + fold - 1, fold, 1, FM_FILE_LINE);
    app_draw_rect(x + FM_ICON_W - fold - 1, y, 1, fold, FM_FILE_LINE);
    /* two content lines */
    app_draw_rect(x + 2, y + 5, FM_ICON_W - fold - 1, 1, FM_FILE_LINE);
    app_draw_rect(x + 2, y + 8, FM_ICON_W - fold - 1, 1, FM_FILE_LINE);
    app_draw_rect(x + 2, y + 11, (FM_ICON_W - fold - 1) * 2 / 3, 1, FM_FILE_LINE);
}

/* ── Navigation helpers ─────────────────────────────────────────── */

static void fm_refresh(void) {
    int count = vfs_listdir(fm_cwd, fm_entries, FM_MAX_ENTRIES);
    fm_entry_count = (count < 0) ? 0 : count;
    if (fm_selected >= fm_entry_count) fm_selected = fm_entry_count > 0 ? 0 : -1;
    if (fm_selected < 0 && fm_entry_count > 0) fm_selected = 0;
    if (fm_scroll < 0) fm_scroll = 0;
}

static void fm_go_to(const char *path) {
    fm_copy(fm_prev, sizeof(fm_prev), fm_cwd);
    fm_copy(fm_cwd,  sizeof(fm_cwd),  path);
    fm_selected = -1;
    fm_scroll   = 0;
    fm_refresh();
}

static void fm_activate_selected(void) {
    char path[VFS_MAX_PATH];
    if (fm_selected < 0 || fm_selected >= fm_entry_count) return;
    fm_join_path(fm_cwd, fm_entries[fm_selected].name, path, sizeof(path));
    if (fm_entries[fm_selected].type == VFS_TYPE_DIR) {
        fm_go_to(path);
        return;
    }
    if (ascii_streq(fm_extension(fm_entries[fm_selected].name), "txt")) {
        notepad_open_file(path);
        (void)desktop_launch_app_by_title("Notepad");
    }
}

static void fm_go_back(void) {
    if (!ascii_streq(fm_prev, fm_cwd)) {
        char cur[VFS_MAX_PATH];
        fm_copy(cur,     sizeof(cur),     fm_cwd);
        fm_copy(fm_cwd,  sizeof(fm_cwd),  fm_prev);
        fm_copy(fm_prev, sizeof(fm_prev), cur);
        fm_selected = -1; fm_scroll = 0;
        fm_refresh();
    }
}

static void fm_go_up(void) {
    char parent[VFS_MAX_PATH];
    fm_parent_path(fm_cwd, parent, sizeof(parent));
    if (!ascii_streq(parent, fm_cwd)) fm_go_to(parent);
}

static void fm_clamp_scroll(int visible_rows) {
    int max_scroll = fm_entry_count - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (fm_scroll < 0) fm_scroll = 0;
    if (fm_scroll > max_scroll) fm_scroll = max_scroll;
}

/* ── Toolbar button helper ──────────────────────────────────────── */

static void fm_draw_button(int x, int y, int w, int h,
                            const char *label, int hover) {
    app_draw_button(x, y, w, h, label, hover ? FM_BTN_HOVER : FM_BTN_BG,
                    FM_BTN_BORDER, FM_BTN_TEXT);
}

/* ── App callbacks ──────────────────────────────────────────────── */

static void fm_on_init(void) {
    fm_copy(fm_cwd,  sizeof(fm_cwd),  "/");
    fm_copy(fm_prev, sizeof(fm_prev), "/");
    fm_entry_count   = 0;
    fm_selected      = -1;
    fm_scroll        = 0;
    fm_last_click_row  = -1;
    fm_last_click_tick = 0u;
    fm_win_w = 480;
    fm_win_h = 360;
    fm_refresh();
}

static void fm_on_draw(int win_x, int win_y, int win_w, int win_h) {
    int list_y      = FM_ADDR_H + FM_TOOL_H;
    int list_h      = win_h - list_y - FM_STATUS_H;
    int visible_rows = list_h / FM_ROW_H;
    int col_type    = win_w - 160;
    int col_size    = win_w - 80;
    int i;

    (void)win_x; (void)win_y;

    /* Cache for on_click which doesn't receive dimensions */
    fm_win_w = win_w;
    fm_win_h = win_h;

    if (visible_rows < 1) visible_rows = 1;
    fm_clamp_scroll(visible_rows);

    /* ── Base fill ── */
    app_clear(FM_BG);

    /* ── Address bar ── */
    app_draw_rect(0, 0, win_w, FM_ADDR_H, FM_ADDR_BG);
    /* teal left accent */
    app_draw_vline(0, 0, FM_ADDR_H, FM_ACCENT);
    app_draw_vline(1, 0, FM_ADDR_H, FM_ACCENT);
    app_draw_rect(app_anim_saw(180u, win_w + 32) - 32, FM_ADDR_H - 3, 32, 2,
                  app_blend_color(FM_ADDR_BG, FM_ACCENT, 1u, 2u));
    app_draw_string(10, (FM_ADDR_H - 16) / 2 + 1, fm_cwd, FM_ADDR_TEXT, FM_ADDR_BG);
    /* bottom separator */
    app_draw_hline(0, FM_ADDR_H - 1, win_w, FM_ACCENT);

    /* ── Toolbar ── */
    app_draw_rect(0, FM_ADDR_H, win_w, FM_TOOL_H, FM_TOOL_BG);
    {
        int bx = 4;
        int by = FM_ADDR_H + 3;
        int bh = FM_TOOL_H - 6;
        fm_draw_button(bx,                           by, FM_BUTTON_W, bh, "Back",    0);
        fm_draw_button(bx + FM_BUTTON_W + FM_BUTTON_GAP, by, FM_BUTTON_W, bh, "Up",      0);
        fm_draw_button(bx + (FM_BUTTON_W + FM_BUTTON_GAP) * 2, by, FM_BUTTON_W, bh, "Refresh", 0);
    }
    /* column headers */
    {
        int hx = 30 + FM_ICON_W + 6;
        int hy = FM_ADDR_H + (FM_TOOL_H - 14) / 2;
        app_draw_string(hx,        hy, "Name",  FM_TEXT_DIM, FM_TOOL_BG);
        app_draw_string(col_type,  hy, "Type",  FM_TEXT_DIM, FM_TOOL_BG);
        app_draw_string(col_size,  hy, "Size",  FM_TEXT_DIM, FM_TOOL_BG);
    }
    app_draw_hline(0, FM_ADDR_H + FM_TOOL_H - 1, win_w, FM_SEP);

    /* ── File list ── */
    for (i = 0; i < visible_rows; i++) {
        int entry_index = fm_scroll + i;
        int y           = list_y + i * FM_ROW_H;
        int is_dir, is_sel;
        uint32_t row_bg, text_col;

        if (entry_index >= fm_entry_count) {
            app_draw_rect(0, y, win_w, FM_ROW_H,
                          (i & 1) ? FM_ROW_B : FM_ROW_A);
            continue;
        }

        is_sel = (entry_index == fm_selected);
        is_dir = (fm_entries[entry_index].type == VFS_TYPE_DIR);

        if (is_sel) {
            row_bg   = FM_SELECT_BG;
            text_col = FM_SELECT_TEXT;
        } else {
            row_bg   = (i & 1) ? FM_ROW_B : FM_ROW_A;
            text_col = FM_TEXT;
        }

        app_draw_rect(0, y, win_w, FM_ROW_H, row_bg);

        /* selection accent stripe on left edge */
        if (is_sel) {
            uint32_t stripe = app_blend_color(FM_SELECT_STRIPE, FM_ACCENT,
                                              (uint32_t)app_anim_pingpong(90u, 20), 20u);
            app_draw_vline(0, y, FM_ROW_H, FM_SELECT_STRIPE);
            app_draw_vline(1, y, FM_ROW_H, stripe);
            app_draw_vline(2, y, FM_ROW_H, stripe);
        }

        /* icon */
        {
            int icon_x = 8;
            int icon_y = y + (FM_ROW_H - FM_ICON_H) / 2;
            if (is_dir) fm_draw_folder_icon(icon_x, icon_y);
            else        fm_draw_file_icon  (icon_x, icon_y);
        }

        /* name */
        app_draw_string(8 + FM_ICON_W + 6,
                        y + (FM_ROW_H - 16) / 2 + 1,
                        fm_entries[entry_index].name,
                        text_col, row_bg);

        /* type / extension */
        if (is_dir) {
            app_draw_string(col_type, y + (FM_ROW_H - 16) / 2 + 1,
                            "DIR",
                            is_sel ? FM_ACCENT : FM_TEXT_DIM, row_bg);
        } else {
            const char *ext = fm_extension(fm_entries[entry_index].name);
            app_draw_string(col_type, y + (FM_ROW_H - 16) / 2 + 1,
                            ext[0] != '\0' ? ext : "FILE",
                            is_sel ? text_col : FM_TEXT_DIM, row_bg);
        }

        /* size */
        if (!is_dir) {
            char size_text[16];
            app_format_size(fm_entries[entry_index].size,
                            size_text, sizeof(size_text));
            app_draw_string(col_size, y + (FM_ROW_H - 16) / 2 + 1,
                            size_text,
                            is_sel ? text_col : FM_TEXT_DIM, row_bg);
        }

        /* subtle row separator */
        if (!is_sel) {
            app_draw_hline(0, y + FM_ROW_H - 1, win_w, FM_SEP);
        }
    }

    /* ── Scrollbar ── */
    if (fm_entry_count > visible_rows) {
        int sb_x    = win_w - FM_SCROLLBAR_W;
        int thumb_h = (visible_rows * list_h) / fm_entry_count;
        int thumb_y;

        if (thumb_h < 16) thumb_h = 16;
        thumb_y = list_y + (fm_scroll * (list_h - thumb_h))
                         / (fm_entry_count - visible_rows);
        app_draw_rect(sb_x, list_y, FM_SCROLLBAR_W, list_h, FM_SCROLL_TRACK);
        /* thumb with rounded feel — three rects */
        app_draw_rect(sb_x + 2, thumb_y + 2,
                      FM_SCROLLBAR_W - 4, thumb_h - 4, FM_SCROLL);
    }

    /* ── Status bar ── */
    app_draw_hline(0, win_h - FM_STATUS_H, win_w, FM_ACCENT);
    app_draw_rect(0, win_h - FM_STATUS_H + 1, win_w, FM_STATUS_H - 1, FM_STATUS_BG);
    {
        Fat32Volume *vol = fat32_get_volume(0);
        char status[64];
        char free_text[16];
        char count_text[12];
        uint32_t free_bytes = 0u;
        uint32_t value, n, i2;
        char num[12];

        if (vol != (Fat32Volume *)0)
            free_bytes = vol->free_clusters * vol->bytes_per_cluster;

        app_format_size(free_bytes, free_text, sizeof(free_text));

        /* build count string */
        value = (uint32_t)fm_entry_count; n = 0u;
        if (value == 0u) num[n++] = '0';
        while (value != 0u && n < sizeof(num)) {
            num[n++] = (char)('0' + (value % 10u)); value /= 10u;
        }
        i2 = 0u;
        while (n > 0u && i2 + 1u < sizeof(count_text))
            count_text[i2++] = num[--n];
        count_text[i2] = '\0';

        /* assemble: "N items  |  Free: XMB" */
        i2 = 0u;
        { uint32_t j = 0u; while (count_text[j] && i2 + 1u < sizeof(status)) status[i2++] = count_text[j++]; }
        { const char *s = " items  \xb7  Free: "; uint32_t j = 0u; while (s[j] && i2 + 1u < sizeof(status)) status[i2++] = s[j++]; }
        { uint32_t j = 0u; while (free_text[j] && i2 + 1u < sizeof(status)) status[i2++] = free_text[j++]; }
        status[i2] = '\0';

        app_draw_string(8, win_h - FM_STATUS_H + (FM_STATUS_H - 16) / 2 + 1,
                        status, FM_TEXT_DIM, FM_STATUS_BG);

        /* right-side hint */
        if (fm_selected >= 0 && fm_selected < fm_entry_count) {
            const char *hint = fm_entries[fm_selected].type == VFS_TYPE_DIR
                               ? "Double-click to open"
                               : "Double-click to open in Notepad";
            int hint_x = win_w - app_text_width(hint) - 8;
            if (hint_x > win_w / 2)
                app_draw_string(hint_x,
                                win_h - FM_STATUS_H + (FM_STATUS_H - 16) / 2 + 1,
                                hint, FM_TEXT_DIM, FM_STATUS_BG);
        }
    }
}

/* ── Key handler ─────────────────────────────────────────────────── */

static void fm_on_key(char c) {
    uint8_t key = (uint8_t)c;

    if (c == '\b') { fm_go_up(); return; }

    if (key == KEY_DELETE && fm_selected >= 0 && fm_selected < fm_entry_count) {
        char path[VFS_MAX_PATH];
        fm_join_path(fm_cwd, fm_entries[fm_selected].name, path, sizeof(path));
        (void)vfs_delete(path);
        fm_refresh();
        return;
    }

    if (key == KEY_F5) { fm_refresh(); return; }

    /* Arrow key navigation */
    if (key == 0x48) { /* up arrow */
        if (fm_selected > 0) { fm_selected--; }
        return;
    }
    if (key == 0x50) { /* down arrow */
        if (fm_selected < fm_entry_count - 1) { fm_selected++; }
        return;
    }
    if (c == '\n' || c == '\r') {
        fm_activate_selected();
        return;
    }
}

/* ── Click handler ───────────────────────────────────────────────── */

static void fm_on_click(int x, int y, int btn) {
    /* Use cached window dimensions so we never use hardcoded values */
    int win_w       = fm_win_w;
    int win_h       = fm_win_h;
    int list_y      = FM_ADDR_H + FM_TOOL_H;
    int list_h      = win_h - list_y - FM_STATUS_H;
    int visible_rows = list_h / FM_ROW_H;
    int bx          = 4;
    int by          = FM_ADDR_H + 3;
    int bh          = FM_TOOL_H - 6;

    (void)btn;
    if (visible_rows < 1) visible_rows = 1;
    fm_clamp_scroll(visible_rows);

    /* ── Toolbar buttons ── */
    if (y >= by && y < by + bh) {
        if (x >= bx && x < bx + FM_BUTTON_W) {
            fm_go_back(); return;
        }
        if (x >= bx + FM_BUTTON_W + FM_BUTTON_GAP &&
            x < bx + (FM_BUTTON_W + FM_BUTTON_GAP) * 2) {
            fm_go_up(); return;
        }
        if (x >= bx + (FM_BUTTON_W + FM_BUTTON_GAP) * 2 &&
            x < bx + (FM_BUTTON_W + FM_BUTTON_GAP) * 3) {
            fm_refresh(); return;
        }
    }

    /* ── Scrollbar ── */
    if (x >= win_w - FM_SCROLLBAR_W &&
        y >= list_y && y < list_y + list_h &&
        fm_entry_count > visible_rows) {
        if (y < list_y + list_h / 2) fm_scroll--;
        else                          fm_scroll++;
        fm_clamp_scroll(visible_rows);
        return;
    }

    /* ── Row click ── */
    if (y >= list_y && y < list_y + list_h) {
        int row         = (y - list_y) / FM_ROW_H;
        int entry_index = fm_scroll + row;
        uint32_t now    = get_ticks();

        if (entry_index >= 0 && entry_index < fm_entry_count) {
            fm_selected = entry_index;
            /* double-click detection: same row within ~350ms (~35 ticks at 100Hz) */
            if (fm_last_click_row == entry_index &&
                (now - fm_last_click_tick) <= 35u) {
                fm_activate_selected();
            }
            fm_last_click_row  = entry_index;
            fm_last_click_tick = now;
        }
    }
}

/* ── Close ───────────────────────────────────────────────────────── */

static void fm_on_close(void) {
    fm_entry_count     = 0;
    fm_selected        = -1;
    fm_scroll          = 0;
    fm_last_click_row  = -1;
    fm_last_click_tick = 0u;
}

/* ── App descriptor ──────────────────────────────────────────────── */

App filemanager_app = {
    .title = "Files",
    .x = 60,
    .y = 100,
    .w = 480,
    .h = 360,
    .bg_color = FM_BG,
    .on_init = fm_on_init,
    .on_draw = fm_on_draw,
    .on_key = fm_on_key,
    .on_click = fm_on_click,
    .on_close = fm_on_close,
    .id = "files",
    .flags = APP_FLAG_SINGLE_INSTANCE | APP_FLAG_RESIZABLE | APP_FLAG_ANIMATED,
    .min_w = 360,
    .min_h = 240
};
