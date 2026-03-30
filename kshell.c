#include <stdint.h>

#include "ascii_util.h"
#include "desktop.h"
#include "gfx.h"
#include "io.h"
#include "keyboard.h"
#include "kshell.h"
#include "mouse.h"
#include "pmm.h"
#include "pit.h"
#include "serial.h"
#include "synth.h"
#include "userland.h"
#include "x86_cpu.h"

#define KSHELL_HISTORY_SIZE 8u
#define KSHELL_LINE_MAX 128u
#define KSHELL_PROMPT_MAX 32u
#define TABLE_NAME_WIDTH 18u
#define TABLE_DESC_WIDTH 40u
#define REPEAT_LIMIT 10u
#define HEXDUMP_LIMIT 256u
#define COLOR_NAME_COUNT 16u

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

static void cmd_history(void) {
    uint32_t start;
    uint32_t i;

    if (history_count == 0u) {
        print("No commands in history.\n");
        return;
    }

    start = (history_next + KSHELL_HISTORY_SIZE - history_count) % KSHELL_HISTORY_SIZE;
    for (i = 0; i < history_count; i++) {
        uint32_t index = (start + i) % KSHELL_HISTORY_SIZE;
        write_u32(i + 1u);
        print(". ");
        print(history_entries[index]);
        print("\n");
    }
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

static void cmd_reboot(void) {
    uint32_t i;
    struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) null_idt = {0u, 0u};

    print("Rebooting...\n");

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

    if (ascii_streq(normalized, "history")) {
        cmd_history();
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
