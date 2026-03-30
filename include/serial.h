#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

#define SERIAL_LOG_LINES 16u
#define SERIAL_LOG_LINE_LEN 80u

struct SerialLogBuffer {
    char lines[SERIAL_LOG_LINES][SERIAL_LOG_LINE_LEN];
    uint32_t count;
    uint32_t next;
};

void serial_init(void);
void serial_write_char(char c);
void serial_print(const char *s);
void serial_write_hex(uint32_t value);
const struct SerialLogBuffer *serial_get_log(void);

#endif
