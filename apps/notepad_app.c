#include <stdint.h>

#include "app.h"

#define NOTEPAD_MAX_CHARS  4096
#define NOTEPAD_FONT_W     8
#define NOTEPAD_FONT_H     16
#define NOTEPAD_PADDING    4

#define NOTEPAD_BG              0x1E2228u
#define NOTEPAD_FG              0xD4D4D4u
#define NOTEPAD_CURSOR_FG       0x1E2228u
#define NOTEPAD_CURSOR_BG       0xE8A830u
#define NOTEPAD_SCROLLBAR       0x4A5060u
#define NOTEPAD_SCROLLBAR_TRACK 0x2A2F38u

static char notepad_buf[NOTEPAD_MAX_CHARS];
static int  notepad_len;
static int  notepad_scroll;

static int notepad_cols;
static int notepad_rows;
static int notepad_count_rows(void) {
    int col = 0;
    int row = 0;
    int i;

    if (notepad_cols <= 0) {
        return 1;
    }

    for (i = 0; i < notepad_len; i++) {
        if (notepad_buf[i] == '\n') {
            row++;
            col = 0;
        } else {
            col++;
            if (col >= notepad_cols) {
                col = 0;
                row++;
            }
        }
    }
    return row + 1;
}

static void notepad_clamp_scroll(void) {
    int total = notepad_count_rows();
    int max_scroll = total - notepad_rows;

    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (notepad_scroll > max_scroll) {
        notepad_scroll = max_scroll;
    }
    if (notepad_scroll < 0) {
        notepad_scroll = 0;
    }
}

static void notepad_scroll_to_cursor(void) {
    int total = notepad_count_rows();
    int cursor_row = total - 1;

    if (notepad_rows <= 0) {
        return;
    }

    if (cursor_row < notepad_scroll) {
        notepad_scroll = cursor_row;
    } else if (cursor_row >= notepad_scroll + notepad_rows) {
        notepad_scroll = cursor_row - notepad_rows + 1;
    }

    notepad_clamp_scroll();
}

static void notepad_on_init(void) {
    notepad_len    = 0;
    notepad_scroll = 0;
    notepad_cols   = 0;
    notepad_rows   = 0;
    notepad_buf[0] = '\0';
}

static void notepad_on_draw(int win_x, int win_y, int win_w, int win_h) {
    int scrollbar_w;
    int client_w;
    int col;
    int row;
    int i;
    int total_rows;
    int cursor_drawn;

    (void)win_x;
    (void)win_y;

    scrollbar_w = 6;
    client_w    = win_w - (NOTEPAD_PADDING * 2) - scrollbar_w - 2;

    notepad_cols = client_w / NOTEPAD_FONT_W;
    if (notepad_cols < 1) {
        notepad_cols = 1;
    }
    notepad_rows = (win_h - (NOTEPAD_PADDING * 2)) / NOTEPAD_FONT_H;
    if (notepad_rows < 1) {
        notepad_rows = 1;
    }

    notepad_clamp_scroll();

    app_clear(NOTEPAD_BG);
    col = 0;
    row = 0;
    cursor_drawn = 0;

    for (i = 0; i <= notepad_len; i++) {
        char ch = (i < notepad_len) ? notepad_buf[i] : '\0';
        int visible_row = row - notepad_scroll;
        int px;
        int py;
        int draw_cursor;

        draw_cursor = (i == notepad_len);

        if (visible_row >= 0 && visible_row < notepad_rows) {
            px = NOTEPAD_PADDING + col * NOTEPAD_FONT_W;
            py = NOTEPAD_PADDING + visible_row * NOTEPAD_FONT_H;

            if (draw_cursor && !cursor_drawn) {
                char cur_char_str[2];
                cur_char_str[0] = ' ';
                cur_char_str[1] = '\0';
                app_draw_rect(px, py, NOTEPAD_FONT_W, NOTEPAD_FONT_H, NOTEPAD_CURSOR_BG);
                app_draw_string(px, py, cur_char_str, NOTEPAD_CURSOR_FG, NOTEPAD_CURSOR_BG);
                cursor_drawn = 1;
            } else if (!draw_cursor) {
                char one[2];
                one[0] = ch;
                one[1] = '\0';

                if (ch != '\n') {
                    app_draw_string(px, py, one, NOTEPAD_FG, NOTEPAD_BG);
                }
            }
        }

        if (ch == '\n') {
            row++;
            col = 0;
        } else if (ch != '\0') {
            col++;
            if (notepad_cols > 0 && col >= notepad_cols) {
                col = 0;
                row++;
            }
        }
    }

    total_rows = notepad_count_rows();
    {
        int sb_x     = win_w - scrollbar_w - 1;
        int sb_y     = NOTEPAD_PADDING;
        int sb_h     = win_h - (NOTEPAD_PADDING * 2);
        int thumb_h;
        int thumb_y;

        app_draw_rect(sb_x, sb_y, scrollbar_w, sb_h, NOTEPAD_SCROLLBAR_TRACK);

        if (total_rows > notepad_rows && sb_h > 0) {
            thumb_h = (notepad_rows * sb_h) / total_rows;
            if (thumb_h < 4) {
                thumb_h = 4;
            }
            thumb_y = sb_y + (notepad_scroll * (sb_h - thumb_h)) / (total_rows - notepad_rows);
            app_draw_rect(sb_x, thumb_y, scrollbar_w, thumb_h, NOTEPAD_SCROLLBAR);
        }
    }
}

static void notepad_on_key(char c) {
    if (c == '\b') {
        /* Backspace */
        if (notepad_len > 0) {
            notepad_len--;
            notepad_buf[notepad_len] = '\0';
        }
    } else if (c == '\r' || c == '\n') {
        /* Enter — insert newline */
        if (notepad_len < NOTEPAD_MAX_CHARS - 1) {
            notepad_buf[notepad_len++] = '\n';
            notepad_buf[notepad_len]   = '\0';
        }
    } else if (c >= 0x20 && c < 0x7F) {
        /* Printable ASCII */
        if (notepad_len < NOTEPAD_MAX_CHARS - 1) {
            notepad_buf[notepad_len++] = c;
            notepad_buf[notepad_len]   = '\0';
        }
    }

    notepad_scroll_to_cursor();
}

static void notepad_on_click(int x, int y, int btn) {
    (void)x;
    (void)y;
    (void)btn;
}

static void notepad_on_close(void) {
    notepad_len    = 0;
    notepad_scroll = 0;
    notepad_buf[0] = '\0';
}

App notepad_app = {
    "Notepad",
    100,
    80,
    500,
    340,
    0x1E2228u,
    notepad_on_init,
    notepad_on_draw,
    notepad_on_key,
    notepad_on_click,
    notepad_on_close
};
