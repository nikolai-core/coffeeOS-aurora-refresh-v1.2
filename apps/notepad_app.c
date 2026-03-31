#include <stdint.h>

#include "app.h"
#include "desktop.h"
#include "keyboard.h"
#include "notepad_app.h"
#include "vfs.h"

#define NOTEPAD_MAX_CHARS  4096
#define NOTEPAD_FONT_W     8
#define NOTEPAD_FONT_H     16
#define NOTEPAD_PADDING    4
#define NOTEPAD_STATUS_H   22

#define NOTEPAD_BG              0x1E2228u
#define NOTEPAD_FG              0xD4D4D4u
#define NOTEPAD_CURSOR_FG       0x1E2228u
#define NOTEPAD_CURSOR_BG       0xE8A830u
#define NOTEPAD_SCROLLBAR       0x4A5060u
#define NOTEPAD_SCROLLBAR_TRACK 0x2A2F38u
#define NOTEPAD_FLASH_OK        0x6FD08Cu
#define NOTEPAD_FLASH_ERR       0xD85C5Cu
#define NOTEPAD_OPEN_BG         0x161A1Fu

static char notepad_buf[NOTEPAD_MAX_CHARS];
static int  notepad_len;
static int  notepad_scroll;
static int  notepad_cols;
static int  notepad_rows;
static char notepad_current_path[VFS_MAX_PATH];
static int  notepad_save_flash;
static int  notepad_dirty;
static int  notepad_open_mode;
static char notepad_open_buf[VFS_MAX_PATH];
static int  notepad_open_len;
static int  notepad_open_error_flash;
static char notepad_title[64];

/* Copy one small string into a fixed buffer. */
static void notepad_copy(char *dst, uint32_t dst_len, const char *src) {
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

/* Return the filename component of one path. */
static const char *notepad_filename(const char *path) {
    uint32_t i = 0u;
    uint32_t last = 0u;

    while (path[i] != '\0') {
        if (path[i] == '/') {
            last = i + 1u;
        }
        i++;
    }
    return path + last;
}

/* Update the visible window title for the Notepad app. */
static void notepad_set_title(const char *title) {
    char old_title[64];

    notepad_copy(old_title, sizeof(old_title), notepad_title);
    notepad_copy(notepad_title, sizeof(notepad_title), title);
    (void)desktop_set_window_title("Notepad", title);
    if (old_title[0] != '\0') {
        (void)desktop_set_window_title(old_title, title);
    }
}

/* Rebuild the title from the current path and dirty state. */
static void notepad_refresh_title(void) {
    char title[64];
    const char *name;
    uint32_t pos = 0u;
    uint32_t i = 0u;

    if (notepad_current_path[0] == '\0') {
        notepad_set_title("Notepad");
        return;
    }

    name = notepad_filename(notepad_current_path);
    if (notepad_dirty && pos + 1u < sizeof(title)) {
        title[pos++] = '*';
    }
    while (name[i] != '\0' && pos + 1u < sizeof(title)) {
        title[pos++] = name[i++];
    }
    title[pos] = '\0';
    notepad_set_title(title);
}

/* Save the current buffer to its active path or a generated untitled path. */
static void notepad_save(void) {
    char path[VFS_MAX_PATH];
    int r;
    int i;

    if (notepad_current_path[0] != '\0') {
        notepad_copy(path, sizeof(path), notepad_current_path);
    } else {
        if (!vfs_exists("/home/user/untitled.txt")) {
            notepad_copy(path, sizeof(path), "/home/user/untitled.txt");
        } else {
            path[0] = '\0';
            for (i = 1; i <= 9; i++) {
                char candidate[VFS_MAX_PATH];
                char digit = (char)('0' + i);

                notepad_copy(candidate, sizeof(candidate), "/home/user/untitled");
                {
                    uint32_t len = 19u;
                    candidate[len++] = digit;
                    candidate[len++] = '.';
                    candidate[len++] = 't';
                    candidate[len++] = 'x';
                    candidate[len++] = 't';
                    candidate[len] = '\0';
                }
                if (!vfs_exists(candidate)) {
                    notepad_copy(path, sizeof(path), candidate);
                    break;
                }
            }
            if (path[0] == '\0') {
                notepad_save_flash = 0;
                notepad_open_error_flash = 60;
                return;
            }
        }
    }

    r = vfs_write_file(path, notepad_buf, (uint32_t)notepad_len);
    if (r != VFS_OK) {
        notepad_open_error_flash = 60;
        return;
    }

    notepad_copy(notepad_current_path, sizeof(notepad_current_path), path);
    notepad_dirty = 0;
    notepad_save_flash = 60;
    notepad_refresh_title();
}

/* Load one file into the Notepad buffer and reset view and title state. */
void notepad_open_file(const char *path) {
    uint32_t len = 0u;

    if (path == (const char *)0 || vfs_read_file(path, notepad_buf, NOTEPAD_MAX_CHARS - 1u, &len) != VFS_OK) {
        return;
    }

    notepad_buf[len] = '\0';
    notepad_len = (int)len;
    notepad_scroll = 0;
    notepad_dirty = 0;
    notepad_open_mode = 0;
    notepad_open_len = 0;
    notepad_open_buf[0] = '\0';
    notepad_open_error_flash = 0;
    notepad_copy(notepad_current_path, sizeof(notepad_current_path), path);
    notepad_refresh_title();
}

/* Count wrapped visual rows in the current document buffer. */
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

/* Clamp the scroll offset so it stays inside the rendered document. */
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

/* Keep the last logical line visible after edits. */
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

/* Mark the document dirty and refresh the title indicator. */
static void notepad_mark_dirty(void) {
    notepad_dirty = 1;
    notepad_refresh_title();
}

/* Clear the document into a new unsaved state. */
static void notepad_new_document(void) {
    notepad_len = 0;
    notepad_scroll = 0;
    notepad_buf[0] = '\0';
    notepad_current_path[0] = '\0';
    notepad_dirty = 0;
    notepad_open_mode = 0;
    notepad_open_len = 0;
    notepad_open_buf[0] = '\0';
    notepad_open_error_flash = 0;
    notepad_save_flash = 0;
    notepad_set_title("Notepad");
}

/* Initialize the editor state and preload the welcome file when present. */
static void notepad_on_init(void) {
    notepad_len = 0;
    notepad_scroll = 0;
    notepad_cols = 0;
    notepad_rows = 0;
    notepad_buf[0] = '\0';
    notepad_current_path[0] = '\0';
    notepad_save_flash = 0;
    notepad_dirty = 0;
    notepad_open_mode = 0;
    notepad_open_buf[0] = '\0';
    notepad_open_len = 0;
    notepad_open_error_flash = 0;
    notepad_title[0] = '\0';
    notepad_set_title("Notepad");

    if (vfs_exists("/home/user/welcome.txt")) {
        notepad_open_file("/home/user/welcome.txt");
    }
}

/* Draw the editor contents, status flash, scrollbar, and optional open prompt. */
static void notepad_on_draw(int win_x, int win_y, int win_w, int win_h) {
    int scrollbar_w;
    int client_w;
    int col;
    int row;
    int i;
    int total_rows;
    int cursor_drawn;
    int content_h;

    (void)win_x;
    (void)win_y;

    scrollbar_w = 6;
    content_h = win_h - (NOTEPAD_PADDING * 2) - (notepad_open_mode ? NOTEPAD_STATUS_H : 0);
    client_w = win_w - (NOTEPAD_PADDING * 2) - scrollbar_w - 2;

    notepad_cols = client_w / NOTEPAD_FONT_W;
    if (notepad_cols < 1) {
        notepad_cols = 1;
    }
    notepad_rows = content_h / NOTEPAD_FONT_H;
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

        draw_cursor = (i == notepad_len) && !notepad_open_mode;

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
        int sb_x = win_w - scrollbar_w - 1;
        int sb_y = NOTEPAD_PADDING;
        int sb_h = content_h;
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

    if (notepad_save_flash > 0) {
        app_draw_string(win_w - 56, 6, "Saved.", NOTEPAD_FLASH_OK, NOTEPAD_BG);
        notepad_save_flash--;
    }

    if (notepad_open_mode) {
        uint32_t prompt_bg = (notepad_open_error_flash > 0) ? NOTEPAD_FLASH_ERR : NOTEPAD_OPEN_BG;
        char open_line[VFS_MAX_PATH + 8];
        uint32_t p = 0u;
        uint32_t j = 0u;

        open_line[p++] = 'O';
        open_line[p++] = 'p';
        open_line[p++] = 'e';
        open_line[p++] = 'n';
        open_line[p++] = ':';
        open_line[p++] = ' ';
        while (notepad_open_buf[j] != '\0' && p + 2u < sizeof(open_line)) {
            open_line[p++] = notepad_open_buf[j++];
        }
        open_line[p++] = '_';
        open_line[p] = '\0';

        app_draw_rect(0, win_h - NOTEPAD_STATUS_H, win_w, NOTEPAD_STATUS_H, prompt_bg);
        app_draw_string(6, win_h - NOTEPAD_STATUS_H + 3, open_line, NOTEPAD_FG, prompt_bg);
        if (notepad_open_error_flash > 0) {
            notepad_open_error_flash--;
        }
    }
}

/* Handle editor typing plus Ctrl shortcuts for save/new/open. */
static void notepad_on_key(char c) {
    if (keyboard_ctrl_held()) {
        if (c == 's' || c == 'S') {
            notepad_save();
            return;
        }
        if (c == 'n' || c == 'N') {
            notepad_new_document();
            return;
        }
        if (c == 'o' || c == 'O') {
            notepad_open_mode = 1;
            notepad_open_len = 0;
            notepad_open_buf[0] = '\0';
            notepad_open_error_flash = 0;
            return;
        }
    }

    if (notepad_open_mode) {
        if (c == 27) {
            notepad_open_mode = 0;
            notepad_open_len = 0;
            notepad_open_buf[0] = '\0';
            notepad_open_error_flash = 0;
            return;
        }
        if (c == '\r' || c == '\n') {
            if (vfs_exists(notepad_open_buf)) {
                notepad_open_file(notepad_open_buf);
                notepad_open_mode = 0;
                notepad_open_len = 0;
                notepad_open_buf[0] = '\0';
                notepad_open_error_flash = 0;
            } else {
                notepad_open_error_flash = 60;
            }
            return;
        }
        if (c == '\b') {
            if (notepad_open_len > 0) {
                notepad_open_len--;
                notepad_open_buf[notepad_open_len] = '\0';
            }
            return;
        }
        if (c >= 0x20 && c < 0x7F && notepad_open_len < VFS_MAX_PATH - 1) {
            notepad_open_buf[notepad_open_len++] = c;
            notepad_open_buf[notepad_open_len] = '\0';
            return;
        }
        return;
    }

    if (c == '\b') {
        if (notepad_len > 0) {
            notepad_len--;
            notepad_buf[notepad_len] = '\0';
            notepad_mark_dirty();
        }
    } else if (c == '\r' || c == '\n') {
        if (notepad_len < NOTEPAD_MAX_CHARS - 1) {
            notepad_buf[notepad_len++] = '\n';
            notepad_buf[notepad_len] = '\0';
            notepad_mark_dirty();
        }
    } else if (c >= 0x20 && c < 0x7F) {
        if (notepad_len < NOTEPAD_MAX_CHARS - 1) {
            notepad_buf[notepad_len++] = c;
            notepad_buf[notepad_len] = '\0';
            notepad_mark_dirty();
        }
    }

    notepad_scroll_to_cursor();
}

/* Ignore mouse clicks for now. */
static void notepad_on_click(int x, int y, int btn) {
    (void)x;
    (void)y;
    (void)btn;
}

/* Reset editor state when the window closes. */
static void notepad_on_close(void) {
    notepad_len = 0;
    notepad_scroll = 0;
    notepad_buf[0] = '\0';
    notepad_current_path[0] = '\0';
    notepad_save_flash = 0;
    notepad_dirty = 0;
    notepad_open_mode = 0;
    notepad_open_buf[0] = '\0';
    notepad_open_len = 0;
    notepad_open_error_flash = 0;
    notepad_title[0] = '\0';
    notepad_set_title("Notepad");
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
