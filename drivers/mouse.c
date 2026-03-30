#include <stdint.h>

#include "gfx.h"
#include "io.h"
#include "mouse.h"
#include "serial.h"

#define PIC1_DATA 0x21u
#define PIC2_DATA 0xA1u
#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_COMMAND_PORT 0x64u
#define DEBUG_MOUSE 0

#if DEBUG_MOUSE
static void mouse_debug_hex(const char *label, uint32_t value) {
    serial_print(label);
    serial_write_hex(value);
    serial_print("\n");
}
#else
static void mouse_debug_hex(const char *label, uint32_t value) {
    (void)label;
    (void)value;
}
#endif

static uint8_t mouse_packet[4];
static uint32_t mouse_packet_index;
static uint32_t mouse_packet_size;
static volatile int mouse_delta_x;
static volatile int mouse_delta_y;
static volatile int mouse_buttons;
static volatile int mouse_wheel;
static volatile int mouse_packet_ready;
static volatile int mouse_x;
static volatile int mouse_y;

static void mouse_wait_write(void) {
    uint32_t i;
    for (i = 0; i < 100000u; i++) {
        if ((io_in8(PS2_STATUS_PORT) & 0x02u) == 0u) {
            return;
        }
    }
}

static uint8_t mouse_wait_read(void) {
    uint32_t i;
    for (i = 0; i < 100000u; i++) {
        if ((io_in8(PS2_STATUS_PORT) & 0x01u) != 0u) {
            return io_in8(PS2_DATA_PORT);
        }
    }
    return 0u;
}

static uint8_t mouse_write_device(uint8_t value) {
    mouse_wait_write();
    io_out8(PS2_COMMAND_PORT, 0xD4u);
    mouse_wait_write();
    io_out8(PS2_DATA_PORT, value);
    return mouse_wait_read();
}

static uint8_t mouse_set_sample_rate(uint8_t rate) {
    uint8_t ack_cmd = mouse_write_device(0xF3u);
    uint8_t ack_rate = mouse_write_device(rate);

#if DEBUG_MOUSE
    mouse_debug_hex("[mouse] set_rate=", rate);
    mouse_debug_hex("[mouse] ack_cmd=", ack_cmd);
    mouse_debug_hex("[mouse] ack_rate=", ack_rate);
#endif
    return (ack_cmd == 0xFAu && ack_rate == 0xFAu) ? 1u : 0u;
}

void mouse_init(void) {
    uint8_t status;
    uint8_t mask1;
    uint8_t mask2;
    uint8_t device_id;
    uint8_t ack;
    uint8_t sample_ok;

    mouse_packet_index = 0u;
    mouse_packet_size = 3u;
    mouse_packet_ready = 0;
    mouse_delta_x = 0;
    mouse_delta_y = 0;
    mouse_buttons = 0;
    mouse_wheel = 0;
    mouse_x = gfx_get_width() / 2;
    mouse_y = gfx_get_height() / 2;

    mouse_wait_write();
    io_out8(PS2_COMMAND_PORT, 0xA8u);

    mouse_wait_write();
    io_out8(PS2_COMMAND_PORT, 0x20u);
    status = mouse_wait_read();
    status |= 0x02u;
    status &= (uint8_t)~0x20u;
    mouse_wait_write();
    io_out8(PS2_COMMAND_PORT, 0x60u);
    mouse_wait_write();
    io_out8(PS2_DATA_PORT, status);

    ack = mouse_write_device(0xF6u);
    mouse_debug_hex("[mouse] reset_defaults_ack=", ack);

    /* wheel packets need the IntelliMouse sample-rate handshake */
    sample_ok = 1u;
    sample_ok &= mouse_set_sample_rate(200u);
    sample_ok &= mouse_set_sample_rate(100u);
    sample_ok &= mouse_set_sample_rate(80u);
    ack = mouse_write_device(0xF2u);
    device_id = mouse_wait_read();
    mouse_debug_hex("[mouse] get_id_ack=", ack);
    mouse_debug_hex("[mouse] device_id=", device_id);
    mouse_debug_hex("[mouse] intellimouse_enabled=", (device_id == 3u || device_id == 4u) ? 1u : 0u);
    if (!sample_ok) {
        mouse_debug_hex("[mouse] sample_sequence_failed=", 1u);
    }
    if (device_id == 3u || device_id == 4u) {
        mouse_packet_size = 4u;
    }

    ack = mouse_write_device(0xEAu);
    mouse_debug_hex("[mouse] stream_mode_ack=", ack);
    ack = mouse_write_device(0xF4u);
    mouse_debug_hex("[mouse] enable_stream_ack=", ack);

    mask1 = io_in8(PIC1_DATA);
    mask1 &= (uint8_t)~(1u << 2);
    io_out8(PIC1_DATA, mask1);

    mask2 = io_in8(PIC2_DATA);
    mask2 &= (uint8_t)~(1u << 4);
    io_out8(PIC2_DATA, mask2);
}

void mouse_handle_irq(void) {
    uint8_t value = io_in8(PS2_DATA_PORT);
    int dx;
    int dy;
    int wheel = 0;

    if (mouse_packet_index == 0u && (value & 0x08u) == 0u) {
        return;
    }

    mouse_packet[mouse_packet_index++] = value;
    if (mouse_packet_index < mouse_packet_size) {
        return;
    }
    mouse_packet_index = 0u;

    dx = (int)(int8_t)mouse_packet[1];
    dy = (int)(int8_t)mouse_packet[2];
    if (mouse_packet_size == 4u) {
        int raw = (int)(int8_t)mouse_packet[3];
        int nibble = raw & 0x0Fu;
        mouse_debug_hex("[mouse] wheel_raw=", (uint32_t)(uint8_t)mouse_packet[3]);
        if (nibble == 1) {
            wheel = 1;
        } else if (nibble == 0x0Fu) {
            wheel = -1;
        }
    }

    mouse_delta_x = dx;
    mouse_delta_y = dy;
    mouse_buttons = (int)(mouse_packet[0] & 0x07u);
    mouse_wheel = wheel;
    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0) {
        mouse_x = 0;
    }
    if (mouse_y < 0) {
        mouse_y = 0;
    }
    if (mouse_x >= gfx_get_width()) {
        mouse_x = gfx_get_width() - 1;
    }
    if (mouse_y >= gfx_get_height()) {
        mouse_y = gfx_get_height() - 1;
    }

    mouse_packet_ready = 1;
}

int mouse_get_state(int *dx, int *dy, int *buttons, int *wheel) {
    if (!mouse_packet_ready) {
        return 0;
    }

    if (dx != (int *)0) {
        *dx = mouse_delta_x;
    }
    if (dy != (int *)0) {
        *dy = mouse_delta_y;
    }
    if (buttons != (int *)0) {
        *buttons = mouse_buttons;
    }
    if (wheel != (int *)0) {
        *wheel = mouse_wheel;
    }
    mouse_packet_ready = 0;
    return 1;
}

void mouse_get_pos(int *x, int *y) {
    if (x != (int *)0) {
        *x = mouse_x;
    }
    if (y != (int *)0) {
        *y = mouse_y;
    }
}
