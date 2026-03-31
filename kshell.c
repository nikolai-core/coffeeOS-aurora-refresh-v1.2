#include <stdint.h>

#include "ascii_util.h"
#include "desktop.h"
#include "fat32.h"
#include "gfx.h"
#include "io.h"
#include "keyboard.h"
#include "kshell.h"
#include "mouse.h"
#include "notepad_app.h"
#include "pmm.h"
#include "pit.h"
#include "ramdisk.h"
#include "serial.h"
#include "synth.h"
#include "userland.h"
#include "vfs.h"
#include "x86_cpu.h"

#define KSHELL_HISTORY_SIZE 8u
#define KSHELL_LINE_MAX 128u
#define KSHELL_PROMPT_MAX 32u
#define TABLE_NAME_WIDTH 18u
#define TABLE_DESC_WIDTH 40u
#define REPEAT_LIMIT 10u
#define HEXDUMP_LIMIT 256u
#define COLOR_NAME_COUNT 16u
#define LS_NAME_WIDTH 24u
#define LS_TYPE_WIDTH 8u
#define LS_SIZE_WIDTH 12u
#define CAT_LIMIT 4096u
#define COPY_LIMIT (64u * 1024u)

struct CommandInfo {
    const char *name;
    const char *description;
};

static const struct CommandInfo kshell_commands[] = {
    {"help", "Show this command table"},
    {"ver, version", "Show the current OS version"},
    {"cls, clear", "Clear the screen"},
    {"setprompt [text]", "Change the kernel prompt"},
    {"color, colour [hex]", "Repaint the console attribute"},
    {"color list, colour list", "List all 16 VGA color codes"},
    {"mem", "Show physical memory page usage"},
    {"sysinfo", "Show basic system information"},
    {"uptime", "Show elapsed uptime in seconds"},
    {"history", "Show recent commands"},
    {"echo [text]", "Print text back to the console"},
    {"repeat [n] [cmd]", "Run another command up to 10 times"},
    {"sleep [n]", "Busy-wait for up to 10 seconds"},
    {"motd", "Show the startup banner again"},
    {"dmesg", "Replay recent serial debug lines"},
    {"memmap", "Show the Multiboot memory map"},
    {"stack", "Print an approximate call stack"},
    {"kernel", "Explain GUI-mode kernel shell access"},
    {"gui", "Enter mouse-driven framebuffer mode"},
    {"userspace", "Load and run the ring 3 userland shell"},
    {"hexdump [addr] [len]", "Dump memory bytes in hex and ASCII"},
    {"cpuinfo, cpu info", "Show the CPU brand string"},
    {"ls [path]", "List directory contents"},
    {"cat [path]", "Print file contents"},
    {"write [path] [text]", "Create or overwrite a text file"},
    {"mkdir [path]", "Create a directory"},
    {"rm [path]", "Delete one file"},
    {"rmdir [path]", "Delete one empty directory"},
    {"cp [src] [dst]", "Copy one file up to 64KB"},
    {"mv [src] [dst]", "Rename or move one file"},
    {"stat [path]", "Show file metadata"},
    {"df", "Show mounted ramdisk usage"},
    {"touch [path]", "Create an empty file if missing"},
    {"edit [path]", "Open a file in Notepad"},
    {"pwd", "Print the current working directory"},
    {"cd [path]", "Change the current working directory"},
    {"history save", "Write history to /history.txt"},
    {"history load", "Load history from /history.txt"},
    {"sync", "Flush filesystem cache to disk"},
    {"secret", "Print the hidden ASCII art"},
    {"credits", "Show project credits"},
    {"reboot", "Reset the machine"},
    {"panic", "Trigger an invalid opcode fault"}
};

static const char *vga_color_names[COLOR_NAME_COUNT] = {
    "Black", "Blue", "Green", "Cyan",
    "Red", "Magenta", "Brown", "Light Gray",
    "Dark Gray", "Light Blue", "Light Green", "Light Cyan",
    "Light Red", "Light Magenta", "Yellow", "White"
};

static char history_entries[KSHELL_HISTORY_SIZE][KSHELL_LINE_MAX];
static uint32_t history_count;
static uint32_t history_next;
static char prompt_text[KSHELL_PROMPT_MAX] = "coffeeos> ";
static char cwd[VFS_MAX_PATH] = "/";
static char kshell_cat_buffer[CAT_LIMIT + 1u];
static uint8_t kshell_copy_buffer[COPY_LIMIT];

static void print_spaces(uint32_t count);

static void print(const char *s) {
    gfx_print(s);
}

static void putc_out(char c) {
    gfx_putc(c);
}

static void print_prompt(void) {
    print(prompt_text);
}

static void write_u32(uint32_t value) {
    char buf[11];
    int i = 0;

    if (value == 0u) {
        putc_out('0');
        return;
    }

    while (value > 0u && i < 10) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0) {
        i--;
        putc_out(buf[i]);
    }
}

static void write_hex_u64(uint64_t value) {
    static const char *digits = "0123456789ABCDEF";
    int shift;

    print("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        putc_out(digits[(uint32_t)((value >> shift) & 0xFu)]);
    }
}

static void copy_string(char *dst, uint32_t dst_max_len, const char *src) {
    uint32_t i = 0;

    if (dst_max_len == 0u) {
        return;
    }

    while (src[i] != '\0' && i + 1u < dst_max_len) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

/* Print one signed integer in decimal for filesystem status output. */
static void write_i32(int32_t value) {
    if (value < 0) {
        putc_out('-');
        write_u32((uint32_t)(-value));
        return;
    }
    write_u32((uint32_t)value);
}

/* Print one VFS error string with a trailing newline. */
static void print_vfs_error(int err) {
    print("Error: ");
    print(vfs_strerror(err));
    print("\n");
}

/* Resolve one shell path against the current working directory. */
static int kshell_resolve_path(const char *input, char *out, uint32_t out_max) {
    uint32_t i = 0u;
    uint32_t j = 0u;

    if (input == (const char *)0 || input[0] == '\0' || out_max == 0u) {
        return VFS_ERR_BADPATH;
    }

    if (input[0] == '/') {
        copy_string(out, out_max, input);
        return VFS_OK;
    }

    if (cwd[0] == '/' && cwd[1] == '\0') {
        out[j++] = '/';
    } else {
        while (cwd[i] != '\0' && j + 1u < out_max) {
            out[j++] = cwd[i++];
        }
        if (j + 1u >= out_max) {
            return VFS_ERR_TOOLONG;
        }
        out[j++] = '/';
    }

    i = 0u;
    while (input[i] != '\0' && j + 1u < out_max) {
        out[j++] = input[i++];
    }
    if (input[i] != '\0') {
        return VFS_ERR_TOOLONG;
    }
    out[j] = '\0';
    return VFS_OK;
}

/* Split one path into a parent path and final leaf name. */
static int kshell_split_parent(const char *path, char *parent, char *leaf) {
    uint32_t len;
    uint32_t i;

    if (path == (const char *)0 || path[0] != '/') {
        return VFS_ERR_BADPATH;
    }
    len = ascii_strlen(path);
    while (len > 1u && path[len - 1u] == '/') {
        len--;
    }
    if (len <= 1u) {
        return VFS_ERR_BADPATH;
    }

    i = len;
    while (i > 0u && path[i - 1u] != '/') {
        i--;
    }
    if (i == 0u || i >= len) {
        return VFS_ERR_BADPATH;
    }

    copy_string(leaf, VFS_NAME_MAX, path + i);
    if (i == 1u) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        uint32_t p;

        for (p = 0u; p < i && p + 1u < VFS_MAX_PATH; p++) {
            parent[p] = path[p];
        }
        parent[i] = '\0';
    }
    return VFS_OK;
}

/* Move one absolute path to its parent directory. */
static void kshell_path_up(const char *path, char *out, uint32_t out_max) {
    char parent[VFS_MAX_PATH];
    char leaf[VFS_NAME_MAX];

    if (ascii_streq(path, "/")) {
        copy_string(out, out_max, "/");
        return;
    }
    if (kshell_split_parent(path, parent, leaf) == VFS_OK) {
        copy_string(out, out_max, parent);
        return;
    }
    copy_string(out, out_max, "/");
}

/* Print a fixed-width ls table border. */
static void print_ls_border(void) {
    uint32_t i;

    putc_out('+');
    for (i = 0; i < LS_NAME_WIDTH + 2u; i++) putc_out('-');
    putc_out('+');
    for (i = 0; i < LS_TYPE_WIDTH + 2u; i++) putc_out('-');
    putc_out('+');
    for (i = 0; i < LS_SIZE_WIDTH + 2u; i++) putc_out('-');
    print("+\n");
}

/* Print one fixed-width ls table row. */
static void print_ls_row(const char *name, const char *type, uint32_t size) {
    uint32_t name_len = ascii_strlen(name);
    uint32_t type_len = ascii_strlen(type);
    char size_text[11];
    uint32_t size_len = 0u;
    uint32_t value = size;

    if (value == 0u) {
        size_text[size_len++] = '0';
    } else {
        while (value != 0u && size_len < sizeof(size_text)) {
            size_text[size_len++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
    }

    print("| ");
    print(name);
    if (name_len < LS_NAME_WIDTH) print_spaces(LS_NAME_WIDTH - name_len);
    print(" | ");
    print(type);
    if (type_len < LS_TYPE_WIDTH) print_spaces(LS_TYPE_WIDTH - type_len);
    print(" | ");
    if (size_len < LS_SIZE_WIDTH) print_spaces(LS_SIZE_WIDTH - size_len);
    while (size_len > 0u) putc_out(size_text[--size_len]);
    print(" |\n");
}

static int split_once(const char *src, char *first, uint32_t first_max_len,
                      char *rest, uint32_t rest_max_len) {
    uint32_t i = 0;
    uint32_t first_out = 0;
    uint32_t rest_out = 0;

    while (src[i] == ' ') {
        i++;
    }

    while (src[i] != '\0' && src[i] != ' ' && first_out + 1u < first_max_len) {
        first[first_out++] = src[i++];
    }
    first[first_out] = '\0';

    while (src[i] == ' ') {
        i++;
    }

    while (src[i] != '\0' && rest_out + 1u < rest_max_len) {
        rest[rest_out++] = src[i++];
    }
    rest[rest_out] = '\0';

    return first_out != 0u;
}

static void print_spaces(uint32_t count) {
    while (count > 0u) {
        putc_out(' ');
        count--;
    }
}

static void print_table_border(void) {
    uint32_t i;

    putc_out('+');
    for (i = 0; i < TABLE_NAME_WIDTH + 2u; i++) {
        putc_out('-');
    }
    putc_out('+');
    for (i = 0; i < TABLE_DESC_WIDTH + 2u; i++) {
        putc_out('-');
    }
    print("+\n");
}

static void print_table_row(const char *name, const char *description) {
    uint32_t name_len = ascii_strlen(name);
    uint32_t desc_len = ascii_strlen(description);

    print("| ");
    print(name);
    if (name_len < TABLE_NAME_WIDTH) {
        print_spaces(TABLE_NAME_WIDTH - name_len);
    }
    print(" | ");
    print(description);
    if (desc_len < TABLE_DESC_WIDTH) {
        print_spaces(TABLE_DESC_WIDTH - desc_len);
    }
    print(" |\n");
}

static void print_command_table(void) {
    uint32_t i;

    print_table_border();
    print_table_row("Command", "Description");
    print_table_border();
    for (i = 0; i < (uint32_t)(sizeof(kshell_commands) / sizeof(kshell_commands[0])); i++) {
        print_table_row(kshell_commands[i].name, kshell_commands[i].description);
    }
    print_table_border();
}

static void print_banner(void) {
    print("coffeeOS developer preview\n");
    print("Type 'help' to list commands.\n");
}

static void history_add(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    copy_string(history_entries[history_next], KSHELL_LINE_MAX, line);
    history_next = (history_next + 1u) % KSHELL_HISTORY_SIZE;
    if (history_count < KSHELL_HISTORY_SIZE) {
        history_count++;
    }
}

static void cmd_history(const char *trimmed) {
    const char *arg = trimmed + 7;
    uint32_t start;
    uint32_t i;

    while (*arg == ' ') {
        arg++;
    }

    if (ascii_streq(arg, "save")) {
        char buffer[1024];
        uint32_t pos = 0u;

        start = (history_next + KSHELL_HISTORY_SIZE - history_count) % KSHELL_HISTORY_SIZE;
        for (i = 0u; i < history_count; i++) {
            uint32_t index = (start + i) % KSHELL_HISTORY_SIZE;
            uint32_t j = 0u;

            while (history_entries[index][j] != '\0' && pos + 2u < sizeof(buffer)) {
                buffer[pos++] = history_entries[index][j++];
            }
            if (pos + 1u < sizeof(buffer)) {
                buffer[pos++] = '\n';
            }
        }
        serial_print("[kshell] history save: calling vfs_write_file\n");
        if (vfs_write_file("/history.txt", buffer, pos) != 0) {
            serial_print("[kshell] history save: vfs_write_file returned\n");
            print("Failed to save history.\n");
        } else {
            serial_print("[kshell] history save: vfs_write_file returned\n");
            print("History saved to /history.txt\n");
        }
        return;
    }

    if (ascii_streq(arg, "load")) {
        char buffer[1024];
        uint32_t len = 0u;
        uint32_t line_start = 0u;
        serial_print("[kshell] history load: calling vfs_read_file\n");
        int r = vfs_read_file("/history.txt", buffer, sizeof(buffer) - 1u, &len);
        serial_print("[kshell] history load: vfs_read_file returned\n");

        if (r != VFS_OK) {
            print_vfs_error(r);
            return;
        }
        buffer[len] = '\0';
        history_count = 0u;
        history_next = 0u;
        for (i = 0u; i <= len; i++) {
            if (buffer[i] == '\n' || buffer[i] == '\0') {
                buffer[i] = '\0';
                history_add(buffer + line_start);
                line_start = i + 1u;
            }
        }
        print("History loaded from /history.txt\n");
        return;
    }

    if (history_count == 0u) {
        print("No commands in history.\n");
        return;
    }

    start = (history_next + KSHELL_HISTORY_SIZE - history_count) % KSHELL_HISTORY_SIZE;
    for (i = 0u; i < history_count; i++) {
        uint32_t index = (start + i) % KSHELL_HISTORY_SIZE;
        write_u32(i + 1u);
        print(". ");
        print(history_entries[index]);
        print("\n");
    }
}

/* List one directory through the VFS layer. */
static void cmd_ls(const char *trimmed) {
    const char *arg = trimmed + 2;
    char resolved[VFS_MAX_PATH];
    /* static to avoid stack overflow — not reentrant */
    static VfsDirEntry entries[64];
    int count;
    int i;
    int r;

    while (*arg == ' ') {
        arg++;
    }
    if (*arg == '\0') {
        copy_string(resolved, sizeof(resolved), "/");
    } else if (kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: ls [path]\n");
        return;
    }

    serial_print("[kshell] ls: calling vfs_listdir\n");
    count = vfs_listdir(resolved, entries, 64);
    serial_print("[kshell] ls: vfs_listdir returned\n");
    if (count < 0) {
        print_vfs_error(count);
        return;
    }

    print_ls_border();
    print_ls_row("Name", "Type", 0u);
    print_ls_border();
    for (i = 0; i < count; i++) {
        print_ls_row(entries[i].name, entries[i].type == VFS_TYPE_DIR ? "DIR" : "FILE", entries[i].size);
    }
    if (count == 0) {
        print_ls_row("(empty)", "-", 0u);
    }
    print_ls_border();
    r = count;
    (void)r;
}

/* Print up to 4096 bytes from one text file. */
static void cmd_cat(const char *trimmed) {
    const char *arg = trimmed + 3;
    char resolved[VFS_MAX_PATH];
    uint32_t len = 0u;
    int r;

    while (*arg == ' ') {
        arg++;
    }
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: cat [path]\n");
        return;
    }

    serial_print("[kshell] cat: calling vfs_read_file\n");
    r = vfs_read_file(resolved, kshell_cat_buffer, CAT_LIMIT, &len);
    serial_print("[kshell] cat: vfs_read_file returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    kshell_cat_buffer[len] = '\0';
    print(kshell_cat_buffer);
    if (len == CAT_LIMIT) {
        VfsDirEntry st;

        if (vfs_stat(resolved, &st) == VFS_OK && st.size > CAT_LIMIT) {
            print("\n... (truncated)\n");
            return;
        }
    }
    if (len == 0u || kshell_cat_buffer[len - 1u] != '\n') {
        print("\n");
    }
}

/* Create or overwrite one file with the remaining command text. */
static void cmd_write_file_text(const char *trimmed) {
    char args[KSHELL_LINE_MAX];
    char path_arg[VFS_MAX_PATH];
    char text[KSHELL_LINE_MAX];
    char resolved[VFS_MAX_PATH];
    int r;

    copy_string(args, sizeof(args), trimmed + 5);
    if (!split_once(args, path_arg, sizeof(path_arg), text, sizeof(text))) {
        print("Usage: write [path] [text]\n");
        return;
    }
    if (kshell_resolve_path(path_arg, resolved, sizeof(resolved)) != VFS_OK) {
        print_vfs_error(VFS_ERR_BADPATH);
        return;
    }
    serial_print("[kshell] write: calling vfs_write_file\n");
    r = vfs_write_file(resolved, text, ascii_strlen(text));
    serial_print("[kshell] write: vfs_write_file returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    print("Wrote file.\n");
}

/* Create one directory at the resolved shell path. */
static void cmd_mkdir_fs(const char *trimmed) {
    const char *arg = trimmed + 5;
    char resolved[VFS_MAX_PATH];
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: mkdir [path]\n");
        return;
    }
    serial_print("[kshell] mkdir: calling vfs_mkdir\n");
    r = vfs_mkdir(resolved);
    serial_print("[kshell] mkdir: vfs_mkdir returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    print("Directory created.\n");
}

/* Delete one file while refusing directory targets. */
static void cmd_rm(const char *trimmed) {
    const char *arg = trimmed + 2;
    char resolved[VFS_MAX_PATH];
    VfsDirEntry st;
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: rm [path]\n");
        return;
    }
    serial_print("[kshell] rm: calling vfs_stat\n");
    r = vfs_stat(resolved, &st);
    serial_print("[kshell] rm: vfs_stat returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    if (st.type == VFS_TYPE_DIR) {
        print("Refusing to delete a directory with rm. Use rmdir.\n");
        return;
    }
    serial_print("[kshell] rm: calling vfs_delete\n");
    r = vfs_delete(resolved);
    serial_print("[kshell] rm: vfs_delete returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    print("File deleted.\n");
}

/* Delete one empty directory through the VFS layer. */
static void cmd_rmdir_fs(const char *trimmed) {
    const char *arg = trimmed + 5;
    char resolved[VFS_MAX_PATH];
    VfsDirEntry st;
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: rmdir [path]\n");
        return;
    }
    serial_print("[kshell] rmdir: calling vfs_stat\n");
    r = vfs_stat(resolved, &st);
    serial_print("[kshell] rmdir: vfs_stat returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    if (st.type != VFS_TYPE_DIR) {
        print("Target is not a directory.\n");
        return;
    }
    serial_print("[kshell] rmdir: calling vfs_delete\n");
    r = vfs_delete(resolved);
    serial_print("[kshell] rmdir: vfs_delete returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    print("Directory deleted.\n");
}

/* Copy one file into another path using a fixed 64KB transfer buffer. */
static void cmd_cp(const char *trimmed) {
    char args[KSHELL_LINE_MAX];
    char src_arg[VFS_MAX_PATH];
    char dst_arg[VFS_MAX_PATH];
    char src[VFS_MAX_PATH];
    char dst[VFS_MAX_PATH];
    uint32_t len = 0u;
    int r;

    copy_string(args, sizeof(args), trimmed + 2);
    if (!split_once(args, src_arg, sizeof(src_arg), dst_arg, sizeof(dst_arg)) || dst_arg[0] == '\0') {
        print("Usage: cp [src] [dst]\n");
        return;
    }
    if (kshell_resolve_path(src_arg, src, sizeof(src)) != VFS_OK
        || kshell_resolve_path(dst_arg, dst, sizeof(dst)) != VFS_OK) {
        print_vfs_error(VFS_ERR_BADPATH);
        return;
    }
    serial_print("[kshell] cp: calling vfs_read_file\n");
    r = vfs_read_file(src, kshell_copy_buffer, COPY_LIMIT, &len);
    serial_print("[kshell] cp: vfs_read_file returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    {
        VfsDirEntry st;

        if (vfs_stat(src, &st) == VFS_OK && st.size > COPY_LIMIT) {
            print("Error: file too large for cp buffer.\n");
            return;
        }
    }
    serial_print("[kshell] cp: calling vfs_write_file\n");
    r = vfs_write_file(dst, kshell_copy_buffer, len);
    serial_print("[kshell] cp: vfs_write_file returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    print("File copied.\n");
}

/* Rename one file path through the VFS layer. */
static void cmd_mv(const char *trimmed) {
    char args[KSHELL_LINE_MAX];
    char src_arg[VFS_MAX_PATH];
    char dst_arg[VFS_MAX_PATH];
    char src[VFS_MAX_PATH];
    char dst[VFS_MAX_PATH];
    int r;

    copy_string(args, sizeof(args), trimmed + 2);
    if (!split_once(args, src_arg, sizeof(src_arg), dst_arg, sizeof(dst_arg)) || dst_arg[0] == '\0') {
        print("Usage: mv [src] [dst]\n");
        return;
    }
    if (kshell_resolve_path(src_arg, src, sizeof(src)) != VFS_OK
        || kshell_resolve_path(dst_arg, dst, sizeof(dst)) != VFS_OK) {
        print_vfs_error(VFS_ERR_BADPATH);
        return;
    }
    serial_print("[kshell] mv: calling vfs_rename\n");
    r = vfs_rename(src, dst);
    serial_print("[kshell] mv: vfs_rename returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    print("File moved.\n");
}

/* Print one file or directory metadata record. */
static void cmd_stat_fs(const char *trimmed) {
    const char *arg = trimmed + 4;
    char resolved[VFS_MAX_PATH];
    VfsDirEntry st;
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: stat [path]\n");
        return;
    }
    serial_print("[kshell] stat: calling vfs_stat\n");
    r = vfs_stat(resolved, &st);
    serial_print("[kshell] stat: vfs_stat returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }

    print("Name: ");
    print(st.name);
    print("\nType: ");
    print(st.type == VFS_TYPE_DIR ? "DIR" : "FILE");
    print("\nSize: ");
    write_u32(st.size);
    print("\nStart cluster: ");
    write_u32(st.cluster);
    print("\n");
}

/* Print total, used, and free space for the mounted ramdisk volume. */
static void cmd_df(void) {
    Fat32Volume *vol = fat32_get_volume(0);
    uint32_t total_blocks;
    uint32_t free_blocks;
    uint32_t used_blocks;
    uint32_t used_percent;

    if (vol == (Fat32Volume *)0 || vol->dev == (BlockDevice *)0) {
        print("Filesystem not mounted.\n");
        return;
    }
    total_blocks = vol->dev->block_count;
    free_blocks = vol->free_clusters * vol->sectors_per_cluster;
    if (free_blocks > total_blocks) {
        free_blocks = total_blocks;
    }
    used_blocks = total_blocks - free_blocks;
    used_percent = total_blocks == 0u ? 0u : (used_blocks * 100u) / total_blocks;

    print("Total blocks: ");
    write_u32(total_blocks);
    print("\nUsed blocks: ");
    write_u32(used_blocks);
    print("\nFree blocks: ");
    write_u32(free_blocks);
    print("\nUsed percent: ");
    write_u32(used_percent);
    print("%\n");
}

/* Create one empty file if it does not already exist. */
static void cmd_touch(const char *trimmed) {
    const char *arg = trimmed + 5;
    char resolved[VFS_MAX_PATH];
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: touch [path]\n");
        return;
    }
    serial_print("[kshell] touch: calling vfs_exists\n");
    if (vfs_exists(resolved)) {
        serial_print("[kshell] touch: vfs_exists returned\n");
        return;
    }
    serial_print("[kshell] touch: vfs_exists returned\n");
    serial_print("[kshell] touch: calling vfs_write_file\n");
    r = vfs_write_file(resolved, "", 0u);
    serial_print("[kshell] touch: vfs_write_file returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
}

/* Open one file in Notepad, creating it first when missing. */
static void cmd_edit(const char *trimmed) {
    const char *arg = trimmed + 4;
    char resolved[VFS_MAX_PATH];
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0' || kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print("Usage: edit [path]\n");
        return;
    }

    serial_print("[kshell] edit: calling vfs_exists\n");
    r = vfs_exists(resolved);
    serial_print("[kshell] edit: vfs_exists returned\n");
    if (!r) {
        serial_print("[kshell] edit: calling vfs_write_file\n");
        r = vfs_write_file(resolved, "", 0u);
        serial_print("[kshell] edit: vfs_write_file returned\n");
        if (r != VFS_OK) {
            print_vfs_error(r);
            return;
        }
    }

    notepad_open_file(resolved);
    (void)desktop_launch_app_by_title("Notepad");
}

/* Print the current shell working directory. */
static void cmd_pwd(void) {
    print(cwd);
    print("\n");
}

/* Change the current shell working directory. */
static void cmd_cd(const char *trimmed) {
    const char *arg = trimmed + 2;
    char resolved[VFS_MAX_PATH];
    VfsDirEntry st;
    int r;

    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        copy_string(cwd, sizeof(cwd), "/");
        return;
    }
    if (kshell_resolve_path(arg, resolved, sizeof(resolved)) != VFS_OK) {
        print_vfs_error(VFS_ERR_BADPATH);
        return;
    }
    serial_print("[kshell] cd: calling vfs_stat\n");
    r = vfs_stat(resolved, &st);
    serial_print("[kshell] cd: vfs_stat returned\n");
    if (r != VFS_OK) {
        print_vfs_error(r);
        return;
    }
    if (st.type != VFS_TYPE_DIR) {
        print("Target is not a directory.\n");
        return;
    }
    copy_string(cwd, sizeof(cwd), resolved);
}

static void cmd_echo(const char *trimmed) {
    const char *text = trimmed + 4;

    while (*text == ' ') {
        text++;
    }

    print(text);
    print("\n");
}

static void cmd_uptime(void) {
    uint32_t ticks = get_ticks();

    print("Uptime: ");
    write_u32(ticks / 100u);
    print(" seconds (");
    write_u32(ticks);
    print(" ticks @ 100 Hz)\n");
}

static void cmd_sleep(const char *trimmed) {
    const char *arg = trimmed + 5;
    uint32_t seconds;
    uint32_t start;
    int ok = 0;

    seconds = ascii_parse_u32(arg, &ok);
    if (!ok || seconds == 0u) {
        print("Usage: sleep [n]\n");
        return;
    }
    if (seconds > 10u) {
        seconds = 10u;
    }

    start = get_ticks();
    while ((get_ticks() - start) < seconds * 100u) {
    }
}

static void cmd_dmesg(void) {
    const struct SerialLogBuffer *log = serial_get_log();
    uint32_t start;
    uint32_t i;

    if (log->count == 0u) {
        print("No serial log lines captured.\n");
        return;
    }

    start = (log->next + SERIAL_LOG_LINES - log->count) % SERIAL_LOG_LINES;
    for (i = 0; i < log->count; i++) {
        uint32_t index = (start + i) % SERIAL_LOG_LINES;
        print(log->lines[index]);
        print("\n");
    }
}

static void cmd_memmap(void) {
    uint32_t cursor = 0u;
    struct PmmMemoryRegion region;

    print("+--------------------+--------------------+-----------+\n");
    print("| Base               | Length             | Type      |\n");
    print("+--------------------+--------------------+-----------+\n");
    while (pmm_memmap_next(&cursor, &region)) {
        print("| ");
        write_hex_u64(region.base);
        print(" | ");
        write_hex_u64(region.length);
        print(" | ");
        if (region.type == 1u) {
            print("available");
        } else {
            print("reserved ");
        }
        print(" |\n");
    }
    print("+--------------------+--------------------+-----------+\n");
}

/* frame-pointer walk, good enough for crash triage */
static void cmd_stack(void) {
    uint32_t *frame;
    uint32_t i;

    __asm__ volatile ("mov %%ebp, %0" : "=r"(frame));
    print("Approximate stack trace:\n");
    for (i = 0; i < 8u && frame != (uint32_t *)0; i++) {
        uint32_t *next = (uint32_t *)(uintptr_t)frame[0];
        uint32_t return_addr = frame[1];

        write_u32(i);
        print(": ");
        gfx_write_hex(return_addr);
        print("\n");

        if (next <= frame) {
            break;
        }
        frame = next;
    }
}

static void cmd_sysinfo(void) {
    uint32_t total = pmm_total_pages();
    uint32_t used = pmm_used_pages();
    uint32_t ticks = get_ticks();

    print("System: coffeeOS Developer Preview [Beta]\n");
    print("Mode: kernel shell\n");
    print("Console attribute: 0x");
    gfx_write_hex(gfx_get_attr());
    print("\n");
    print("PMM pages: total=");
    write_u32(total);
    print(", used=");
    write_u32(used);
    print(", free=");
    write_u32(total - used);
    print("\n");
    print("Uptime seconds: ");
    write_u32(ticks / 100u);
    print("\n");
}

static void cmd_color_list(void) {
    uint32_t i;

    print("+-------+---------------+\n");
    print("| Index | VGA Color     |\n");
    print("+-------+---------------+\n");
    for (i = 0; i < COLOR_NAME_COUNT; i++) {
        print("| ");
        write_u32(i);
        if (i < 10u) {
            print("     | ");
        } else {
            print("    | ");
        }
        print(vga_color_names[i]);
        if (ascii_strlen(vga_color_names[i]) < 13u) {
            print_spaces(13u - ascii_strlen(vga_color_names[i]));
        }
        print(" |\n");
    }
    print("+-------+---------------+\n");
}

static void cmd_setprompt(const char *trimmed) {
    const char *text = trimmed + 9;

    while (*text == ' ') {
        text++;
    }

    if (*text == '\0') {
        print("Usage: setprompt [text]\n");
        return;
    }

    copy_string(prompt_text, (uint32_t)sizeof(prompt_text), text);
    print("Prompt updated.\n");
}

static void cmd_gui(void) {
    desktop_run();
}

static void cmd_kernel(void) {
    if (desktop_is_running()) {
        desktop_request_kernel_shell();
        return;
    }

    print("Already in the kernel shell.\n");
}

static void cmd_userspace(void) {
    if (userland_active()) {
        print("Userland is already running.\n");
        return;
    }

    print("Loading userland...\n");
    userland_start(0u);
}

static void cmd_cpu_info(void) {
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    char brand[49];
    uint32_t i;

    for (i = 0; i < 49u; i++) {
        brand[i] = '\0';
    }

    x86_cpuid(0x80000000u, 0, &a, &b, &c, &d);
    if (a < 0x80000004u) {
        print("CPU Info: brand string unavailable\n");
        return;
    }

    x86_cpuid(0x80000002u, 0, &a, &b, &c, &d);
    ((uint32_t *)brand)[0] = a;
    ((uint32_t *)brand)[1] = b;
    ((uint32_t *)brand)[2] = c;
    ((uint32_t *)brand)[3] = d;

    x86_cpuid(0x80000003u, 0, &a, &b, &c, &d);
    ((uint32_t *)brand)[4] = a;
    ((uint32_t *)brand)[5] = b;
    ((uint32_t *)brand)[6] = c;
    ((uint32_t *)brand)[7] = d;

    x86_cpuid(0x80000004u, 0, &a, &b, &c, &d);
    ((uint32_t *)brand)[8] = a;
    ((uint32_t *)brand)[9] = b;
    ((uint32_t *)brand)[10] = c;
    ((uint32_t *)brand)[11] = d;

    print("CPU Info: ");
    print(brand);
    print("\n");
}

static void cmd_secret(void) {
    print(
        "         ..\n"
        "      ..  ..\n"
        "            ..\n"
        "             ..\n"
        "            ..\n"
        "           ..\n"
        "         ..\n"
        "##       ..    ####\n"
        "##.............##  ##\n"
        "##.............##   ##\n"
        "##.............## ##\n"
        "##.............###\n"
        " ##...........##\n"
        "  #############\n"
        "  #############\n"
        "#################\n");
}

static void cmd_credits(void) {
    print("coffeeOS created by Johan Joseph, tested by Rayan Abdulsalam\n");
}

static void print_hex_byte(uint8_t value) {
    const char *digits = "0123456789ABCDEF";
    putc_out(digits[(value >> 4) & 0x0Fu]);
    putc_out(digits[value & 0x0Fu]);
}

static void print_ascii_or_dot(uint8_t value) {
    if (value >= 32u && value <= 126u) {
        putc_out((char)value);
    } else {
        putc_out('.');
    }
}

/* keep this bounded so bad addresses hurt less */
static void cmd_hexdump(const char *trimmed) {
    char args[KSHELL_LINE_MAX];
    char addr_text[KSHELL_LINE_MAX];
    char len_text[KSHELL_LINE_MAX];
    uint32_t addr;
    uint32_t len;
    uint32_t offset;
    int ok_addr = 0;
    int ok_len = 0;

    copy_string(args, (uint32_t)sizeof(args), trimmed + 7);
    if (!split_once(args, addr_text, (uint32_t)sizeof(addr_text), len_text, (uint32_t)sizeof(len_text))) {
        print("Usage: hexdump [addr] [len]\n");
        return;
    }

    addr = ascii_parse_hex_u32(addr_text, &ok_addr);
    len = ascii_parse_hex_u32(len_text, &ok_len);
    if (!ok_addr || !ok_len || len == 0u) {
        print("Usage: hexdump [addr] [len]\n");
        return;
    }

    if (len > HEXDUMP_LIMIT) {
        len = HEXDUMP_LIMIT;
    }

    for (offset = 0; offset < len; offset += 16u) {
        uint32_t row_len = len - offset;
        uint32_t i;
        const uint8_t *p;

        if (row_len > 16u) {
            row_len = 16u;
        }

        gfx_write_hex(addr + offset);
        print("  ");
        p = (const uint8_t *)(uintptr_t)(addr + offset);
        for (i = 0; i < 16u; i++) {
            if (i < row_len) {
                print_hex_byte(p[i]);
            } else {
                print("  ");
            }
            putc_out(' ');
        }

        print(" |");
        for (i = 0; i < row_len; i++) {
            print_ascii_or_dot(p[i]);
        }
        for (; i < 16u; i++) {
            putc_out(' ');
        }
        print("|\n");
    }
}

/* Flush the mounted filesystem and persist the ramdisk backing image when needed. */
static void cmd_sync(void) {
    if (fat32_sync(0) != 0) {
        print("Filesystem sync failed.\n");
        return;
    }
    (void)ramdisk_sync_backing_store();
    print("Filesystem synced.\n");
}

static void cmd_reboot(void) {
    uint32_t i;
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0u, 0u};

    print("Rebooting...\n");
    (void)fat32_sync(0);
    (void)ramdisk_sync_backing_store();
    serial_print("[reboot] filesystem synced\n");

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

static void kshell_dispatch_normalized(const char *trimmed, const char *normalized);

static void cmd_repeat(const char *trimmed) {
    char args[KSHELL_LINE_MAX];
    char count_text[KSHELL_LINE_MAX];
    char nested_trimmed[KSHELL_LINE_MAX];
    char nested_normalized[KSHELL_LINE_MAX];
    uint32_t count;
    uint32_t i;
    int ok = 0;

    copy_string(args, (uint32_t)sizeof(args), trimmed + 6);
    if (!split_once(args, count_text, (uint32_t)sizeof(count_text),
                    nested_trimmed, (uint32_t)sizeof(nested_trimmed))) {
        print("Usage: repeat [n] [command]\n");
        return;
    }

    count = ascii_parse_u32(count_text, &ok);
    if (!ok || count == 0u) {
        print("Usage: repeat [n] [command]\n");
        return;
    }

    if (nested_trimmed[0] == '\0') {
        print("Usage: repeat [n] [command]\n");
        return;
    }

    if (count > REPEAT_LIMIT) {
        count = REPEAT_LIMIT;
    }

    ascii_trim_lower_copy(nested_normalized, (uint32_t)sizeof(nested_normalized), nested_trimmed);
    for (i = 0; i < count; i++) {
        kshell_dispatch_normalized(nested_trimmed, nested_normalized);
    }
}

static void kshell_dispatch_normalized(const char *trimmed, const char *normalized) {
    if (ascii_streq(normalized, "ver") || ascii_streq(normalized, "version")) {
        print("CoffeeOS - Developer Preview [Beta]\n");
        return;
    }

    if (ascii_streq(normalized, "cls") || ascii_streq(normalized, "clear")) {
        gfx_clear();
        return;
    }

    if (ascii_streq(normalized, "help")) {
        print_command_table();
        return;
    }

    if (ascii_streq(normalized, "history") || ascii_starts_with(normalized, "history ")) {
        cmd_history(trimmed);
        return;
    }

    if (ascii_streq(normalized, "motd")) {
        print_banner();
        return;
    }

    if (ascii_streq(normalized, "sysinfo")) {
        cmd_sysinfo();
        return;
    }

    if (ascii_streq(normalized, "uptime")) {
        cmd_uptime();
        return;
    }

    if (ascii_streq(normalized, "sleep") || ascii_starts_with(normalized, "sleep ")) {
        cmd_sleep(trimmed);
        return;
    }

    if (ascii_streq(normalized, "dmesg")) {
        cmd_dmesg();
        return;
    }

    if (ascii_streq(normalized, "memmap")) {
        cmd_memmap();
        return;
    }

    if (ascii_streq(normalized, "stack")) {
        cmd_stack();
        return;
    }

    if (ascii_streq(normalized, "kernel")) {
        cmd_kernel();
        return;
    }

    if (ascii_streq(normalized, "gui")) {
        cmd_gui();
        return;
    }

    if (ascii_streq(normalized, "userspace")) {
        cmd_userspace();
        return;
    }

    if (ascii_streq(normalized, "echo") || ascii_starts_with(normalized, "echo ")) {
        cmd_echo(trimmed);
        return;
    }

    if (ascii_streq(normalized, "setprompt") || ascii_starts_with(normalized, "setprompt ")) {
        cmd_setprompt(trimmed);
        return;
    }

    if (ascii_streq(normalized, "repeat") || ascii_starts_with(normalized, "repeat ")) {
        cmd_repeat(trimmed);
        return;
    }

    if (ascii_streq(normalized, "hexdump") || ascii_starts_with(normalized, "hexdump ")) {
        cmd_hexdump(trimmed);
        return;
    }

    if (ascii_streq(normalized, "color") || ascii_streq(normalized, "colour")
        || ascii_starts_with(normalized, "color ") || ascii_starts_with(normalized, "colour ")) {
        const char *arg = trimmed;
        const char *normalized_arg = normalized;
        int prefix = ascii_starts_with(normalized, "colour") ? 6 : 5;
        int ok = 0;
        uint8_t attr;

        arg += prefix;
        normalized_arg += prefix;
        while (*arg == ' ') {
            arg++;
        }
        while (*normalized_arg == ' ') {
            normalized_arg++;
        }

        if (ascii_streq(normalized_arg, "list")) {
            cmd_color_list();
            return;
        }

        attr = ascii_parse_hex_u8(arg, &ok);
        if (!ok) {
            print("Usage: color [hex] | color list\n");
            return;
        }

        gfx_repaint_attr(attr);
        print("Text attribute set to ");
        gfx_write_hex(attr);
        print("\n");
        return;
    }

    if (ascii_streq(normalized, "mem")) {
        uint32_t total = pmm_total_pages();
        uint32_t used = pmm_used_pages();
        uint32_t free = total - used;

        print("PMM pages - total: ");
        write_u32(total);
        print(", used: ");
        write_u32(used);
        print(", free: ");
        write_u32(free);
        print("\n");
        return;
    }

    if (ascii_streq(normalized, "panic")) {
        __asm__ volatile ("ud2");
        return;
    }

    if (ascii_streq(normalized, "reboot")) {
        cmd_reboot();
        return;
    }

    if (ascii_streq(normalized, "sync")) {
        cmd_sync();
        return;
    }

    if (ascii_streq(normalized, "ls") || ascii_starts_with(normalized, "ls ")) {
        cmd_ls(trimmed);
        return;
    }

    if (ascii_streq(normalized, "cat") || ascii_starts_with(normalized, "cat ")) {
        cmd_cat(trimmed);
        return;
    }

    if (ascii_streq(normalized, "write") || ascii_starts_with(normalized, "write ")) {
        cmd_write_file_text(trimmed);
        return;
    }

    if (ascii_streq(normalized, "mkdir") || ascii_starts_with(normalized, "mkdir ")) {
        cmd_mkdir_fs(trimmed);
        return;
    }

    if (ascii_streq(normalized, "rm") || ascii_starts_with(normalized, "rm ")) {
        cmd_rm(trimmed);
        return;
    }

    if (ascii_streq(normalized, "rmdir") || ascii_starts_with(normalized, "rmdir ")) {
        cmd_rmdir_fs(trimmed);
        return;
    }

    if (ascii_streq(normalized, "cp") || ascii_starts_with(normalized, "cp ")) {
        cmd_cp(trimmed);
        return;
    }

    if (ascii_streq(normalized, "mv") || ascii_starts_with(normalized, "mv ")) {
        cmd_mv(trimmed);
        return;
    }

    if (ascii_streq(normalized, "stat") || ascii_starts_with(normalized, "stat ")) {
        cmd_stat_fs(trimmed);
        return;
    }

    if (ascii_streq(normalized, "df")) {
        cmd_df();
        return;
    }

    if (ascii_streq(normalized, "touch") || ascii_starts_with(normalized, "touch ")) {
        cmd_touch(trimmed);
        return;
    }

    if (ascii_streq(normalized, "edit") || ascii_starts_with(normalized, "edit ")) {
        cmd_edit(trimmed);
        return;
    }

    if (ascii_streq(normalized, "pwd")) {
        cmd_pwd();
        return;
    }

    if (ascii_streq(normalized, "cd") || ascii_starts_with(normalized, "cd ")) {
        cmd_cd(trimmed);
        return;
    }

    if (ascii_streq(normalized, "cpuinfo") || ascii_streq(normalized, "cpu info")) {
        cmd_cpu_info();
        return;
    }

    if (ascii_streq(normalized, "secret")) {
        cmd_secret();
        return;
    }

    if (ascii_streq(normalized, "credits")) {
        cmd_credits();
        return;
    }

    print("Unknown command. Type 'help'.\n");
    sound_error();
}

void kshell_dispatch_command(const char *line) {
    char trimmed[KSHELL_LINE_MAX];
    char normalized[KSHELL_LINE_MAX];

    ascii_trim_copy(trimmed, (uint32_t)sizeof(trimmed), line);
    ascii_trim_lower_copy(normalized, (uint32_t)sizeof(normalized), line);
    if (trimmed[0] == '\0') {
        return;
    }

    history_add(trimmed);
    kshell_dispatch_normalized(trimmed, normalized);
}

void kshell_run(void) {
    char line[KSHELL_LINE_MAX];
    uint32_t len = 0;
    char c;

    print_banner();
    print_prompt();

    for (;;) {
        if (pit_take_fs_sync_request()) {
            (void)fat32_sync(0);
            (void)ramdisk_sync_backing_store();
        }
        if (!keyboard_read_char(&c)) {
            continue;
        }

        if (c == '\n') {
            gfx_putc('\n');
            line[len] = '\0';
            kshell_dispatch_command(line);
            len = 0;
            print_prompt();
            continue;
        }

        if (c == '\b') {
            if (len > 0u) {
                len--;
                gfx_putc('\b');
            }
            continue;
        }

        if (len + 1u < sizeof(line)) {
            line[len++] = c;
            gfx_putc(c);
        }
    }
}

void user_mode_entry(void) {
    kshell_run();
    for (;;) {
    }
}
