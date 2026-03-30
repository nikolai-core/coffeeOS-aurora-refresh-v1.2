#include <stdint.h>

#include "ascii_util.h"
#include "syscall.h"
#include "x86_cpu.h"

#define SHELL_HISTORY_SIZE 8u
#define SHELL_LINE_MAX 128u
#define TABLE_NAME_WIDTH 18u
#define TABLE_DESC_WIDTH 40u

struct CommandInfo {
    const char *name;
    const char *description;
};

static const struct CommandInfo shell_commands[] = {
    {"help", "Show this command table"},
    {"exit", "Exit the userland shell task"},
    {"kernel", "Leave userland and enter the kernel shell"},
    {"ver, version", "Show the current OS version"},
    {"cls, clear", "Clear the terminal"},
    {"echo [text]", "Print text back to the console"},
    {"history", "Show recent commands"},
    {"sysinfo", "Show basic system information"},
    {"uptime", "Show elapsed uptime in seconds"},
    {"motd", "Show the startup banner again"},
    {"color, colour", "Request a console color change"},
    {"mem", "Report memory command availability"},
    {"panic", "Report panic command availability"},
    {"cpuinfo, cpu info", "Show the CPU brand string"},
    {"secret", "Print the hidden ASCII art"},
    {"credits", "Show project credits"}
};

static char history_entries[SHELL_HISTORY_SIZE][SHELL_LINE_MAX];
static uint32_t history_count;
static uint32_t history_next;

static void print(const char *s) {
    (void)sys_write(s, ascii_strlen(s));
}

static void print_hex_byte(uint8_t value) {
    char buf[3];
    const char *digits = "0123456789abcdef";
    buf[0] = digits[(value >> 4) & 0xF];
    buf[1] = digits[value & 0xF];
    buf[2] = '\0';
    print(buf);
}

static void write_u32(uint32_t value) {
    char buf[11];
    int i = 0;

    if (value == 0u) {
        print("0");
        return;
    }

    while (value > 0u && i < 10) {
        buf[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0) {
        char c;
        i--;
        c = buf[i];
        (void)sys_write(&c, 1u);
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

static void print_spaces(uint32_t count) {
    while (count > 0u) {
        print(" ");
        count--;
    }
}

static void print_table_border(void) {
    uint32_t i;

    print("+");
    for (i = 0; i < TABLE_NAME_WIDTH + 2u; i++) {
        print("-");
    }
    print("+");
    for (i = 0; i < TABLE_DESC_WIDTH + 2u; i++) {
        print("-");
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
    for (i = 0; i < (uint32_t)(sizeof(shell_commands) / sizeof(shell_commands[0])); i++) {
        print_table_row(shell_commands[i].name, shell_commands[i].description);
    }
    print_table_border();
}

static void print_banner(void) {
    print("coffeeOS userland shell\n");
    print("Type 'help' to list commands.\n");
}

static void history_add(const char *line) {
    if (line[0] == '\0') {
        return;
    }

    copy_string(history_entries[history_next], SHELL_LINE_MAX, line);
    history_next = (history_next + 1u) % SHELL_HISTORY_SIZE;
    if (history_count < SHELL_HISTORY_SIZE) {
        history_count++;
    }
}

static void print_history(void) {
    uint32_t start;
    uint32_t i;

    if (history_count == 0u) {
        print("No commands in history.\n");
        return;
    }

    start = (history_next + SHELL_HISTORY_SIZE - history_count) % SHELL_HISTORY_SIZE;
    for (i = 0; i < history_count; i++) {
        uint32_t index = (start + i) % SHELL_HISTORY_SIZE;
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
    uint32_t ticks = sys_gettime();

    print("Uptime: ");
    write_u32(ticks / 100u);
    print(" seconds (");
    write_u32(ticks);
    print(" ticks @ 100 Hz)\n");
}

static void print_sysinfo(void) {
    uint32_t ticks = sys_gettime();

    print("System: coffeeOS Developer Preview [Beta]\n");
    print("Mode: userland shell (ring 3)\n");
    print("Prompt: user>\n");
    print("History slots: ");
    write_u32(SHELL_HISTORY_SIZE);
    print("\n");
    print("Uptime seconds: ");
    write_u32(ticks / 100u);
    print("\n");
}

static void cmd_cpu_info(void) {
    uint32_t a, b, c, d;
    char brand[49];
    uint32_t i;

    for (i = 0; i < sizeof(brand); i++) {
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

static void print_secret(void) {
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

static void print_credits(void) {
    print("coffeeOS created by Johan Joseph, tested by Rayan Abdulsalam\n");
}

int main(void) {
    char line[SHELL_LINE_MAX];
    char trimmed[SHELL_LINE_MAX];
    char normalized[SHELL_LINE_MAX];
    uint32_t len = 0;

    print_banner();

    for (;;) {
        char c;
        print("user> ");
        len = 0;

        for (;;) {
            while (sys_readchar(&c) == 0) {
            }

            if (c == '\n') {
                print("\n");
                line[len] = '\0';
                break;
            }

            if (c == '\b') {
                if (len > 0u) {
                    len--;
                    print("\b");
                }
                continue;
            }

            if (len + 1u < sizeof(line)) {
                line[len++] = c;
                (void)sys_write(&c, 1u);
            }
        }

        ascii_trim_copy(trimmed, (uint32_t)sizeof(trimmed), line);
        ascii_trim_lower_copy(normalized, (uint32_t)sizeof(normalized), line);

        if (trimmed[0] == '\0') {
            continue;
        }

        history_add(trimmed);

        if (ascii_streq(normalized, "exit")) {
            print("bye\n");
            sys_exit(0);
        } else if (ascii_streq(normalized, "kernel")) {
            print("Switching to kernel shell...\n");
            sys_exit(1);
        } else if (ascii_streq(normalized, "help")) {
            print_command_table();
        } else if (ascii_streq(normalized, "ver") || ascii_streq(normalized, "version")) {
            print("CoffeeOS - Developer Preview [Beta]\n");
        } else if (ascii_streq(normalized, "cls") || ascii_streq(normalized, "clear")) {
            /* standard clear + home */
            print("\033[2J\033[H");
        } else if (ascii_streq(normalized, "history")) {
            print_history();
        } else if (ascii_streq(normalized, "motd")) {
            print_banner();
        } else if (ascii_streq(normalized, "sysinfo")) {
            print_sysinfo();
        } else if (ascii_streq(normalized, "uptime")) {
            cmd_uptime();
        } else if (ascii_streq(normalized, "echo") || ascii_starts_with(normalized, "echo ")) {
            cmd_echo(trimmed);
        } else if (ascii_streq(normalized, "color") || ascii_streq(normalized, "colour")
                   || ascii_starts_with(normalized, "color ") || ascii_starts_with(normalized, "colour ")) {
            const char *arg = trimmed;
            int prefix = ascii_starts_with(normalized, "colour") ? 6 : 5;
            int ok = 0;
            uint8_t value;

            arg += prefix;
            value = ascii_parse_hex_u8(arg, &ok);
            if (!ok) {
                print("Usage: color [hex]\n");
            } else {
                print("Color command applies in kernel shell only; requested ");
                print_hex_byte(value);
                print("\n");
            }
        } else if (ascii_streq(normalized, "mem")) {
            print("mem command unavailable in userland\n");
        } else if (ascii_streq(normalized, "panic")) {
            print("panic command disabled from userland\n");
        } else if (ascii_streq(normalized, "cpuinfo") || ascii_streq(normalized, "cpu info")) {
            cmd_cpu_info();
        } else if (ascii_streq(normalized, "secret")) {
            print_secret();
        } else if (ascii_streq(normalized, "credits")) {
            print_credits();
        } else {
            print("Unknown command. Type 'help'.\n");
        }
    }
}
