#include <stdint.h>

#include "format.h"
#include "io.h"
#include "serial.h"

#define SERIAL_COM1_PORT 0x3F8u

static struct SerialLogBuffer serial_log;
static char current_line[SERIAL_LOG_LINE_LEN];
static uint32_t current_line_len;

static int serial_tx_ready(void) {
    return (io_in8((uint16_t)(SERIAL_COM1_PORT + 5u)) & 0x20u) != 0u;
}

static void serial_log_push_line(void) {
    uint32_t i;

    for (i = 0; i < current_line_len && i + 1u < SERIAL_LOG_LINE_LEN; i++) {
        serial_log.lines[serial_log.next][i] = current_line[i];
    }
    serial_log.lines[serial_log.next][i] = '\0';

    serial_log.next = (serial_log.next + 1u) % SERIAL_LOG_LINES;
    if (serial_log.count < SERIAL_LOG_LINES) {
        serial_log.count++;
    }
    current_line_len = 0u;
}

static void serial_log_char(char c) {
    if (c == '\r') {
        return;
    }

    if (c == '\n') {
        serial_log_push_line();
        return;
    }

    if (current_line_len + 1u < SERIAL_LOG_LINE_LEN) {
        current_line[current_line_len++] = c;
    }
}

void serial_init(void) {
    uint32_t line;
    uint32_t col;

    io_out8((uint16_t)(SERIAL_COM1_PORT + 1u), 0x00u); /* disable interrupts */
    io_out8((uint16_t)(SERIAL_COM1_PORT + 3u), 0x80u); /* DLAB on */
    io_out8((uint16_t)(SERIAL_COM1_PORT + 0u), 0x03u); /* divisor low (38400 baud if clock 115200) */
    io_out8((uint16_t)(SERIAL_COM1_PORT + 1u), 0x00u); /* divisor high */
    io_out8((uint16_t)(SERIAL_COM1_PORT + 3u), 0x03u); /* 8 bits, no parity, one stop bit */
    io_out8((uint16_t)(SERIAL_COM1_PORT + 2u), 0xC7u); /* enable FIFO, clear, 14 byte threshold */
    io_out8((uint16_t)(SERIAL_COM1_PORT + 4u), 0x0Bu); /* IRQs enabled, RTS and DSR set */

    serial_log.count = 0u;
    serial_log.next = 0u;
    current_line_len = 0u;
    for (line = 0; line < SERIAL_LOG_LINES; line++) {
        for (col = 0; col < SERIAL_LOG_LINE_LEN; col++) {
            serial_log.lines[line][col] = '\0';
        }
    }
}

void serial_write_char(char c) {
    serial_log_char(c);

    while (!serial_tx_ready()) {
    }
    io_out8((uint16_t)SERIAL_COM1_PORT, (uint8_t)c);
}

void serial_print(const char *s) {
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            serial_write_char('\r');
        }
        serial_write_char(c);
    }
}

void serial_write_hex(uint32_t value) {
    char buf[11];
    format_hex_u32(buf, value);
    serial_print(buf);
}

const struct SerialLogBuffer *serial_get_log(void) {
    return &serial_log;
}
