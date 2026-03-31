#include <stdint.h>

#include "io.h"
#include "keyboard.h"

#define PIC_MASTER_DATA_PORT 0x21u
#define PS2_DATA_PORT 0x60u
#define KEYBOARD_RING_SIZE 256u

static volatile char keyboard_ring[KEYBOARD_RING_SIZE];
static volatile uint32_t keyboard_ring_head;
static volatile uint32_t keyboard_ring_tail;
static volatile uint32_t keyboard_ring_count;

static uint8_t is_shift_down;
static uint8_t is_caps_lock_on;
static int kb_ctrl_held;

static const char scancode_normal[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0
};

static const char scancode_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z',
    'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0
};

static void keyboard_ring_push(char c) {
    if (keyboard_ring_count >= KEYBOARD_RING_SIZE) {
        return;
    }

    keyboard_ring[keyboard_ring_tail] = c;
    keyboard_ring_tail = (keyboard_ring_tail + 1u) % KEYBOARD_RING_SIZE;
    keyboard_ring_count++;
}

int keyboard_read_char(char *out) {
    if (keyboard_ring_count == 0u) {
        return 0;
    }

    *out = keyboard_ring[keyboard_ring_head];
    keyboard_ring_head = (keyboard_ring_head + 1u) % KEYBOARD_RING_SIZE;
    keyboard_ring_count--;
    return 1;
}

void keyboard_init(void) {
    uint8_t mask;

    // Tiny ring buffer so the IRQ handler can just drop bytes and leave.
    keyboard_ring_head = 0u;
    keyboard_ring_tail = 0u;
    keyboard_ring_count = 0u;

    is_shift_down = 0u;
    is_caps_lock_on = 0u;
    kb_ctrl_held = 0;

    mask = io_in8((uint16_t)PIC_MASTER_DATA_PORT);
    mask &= (uint8_t)~(1u << 1);
    io_out8((uint16_t)PIC_MASTER_DATA_PORT, mask);
}

void keyboard_handle_irq(void) {
    uint8_t scancode = io_in8((uint16_t)PS2_DATA_PORT);

    if (scancode == 0x1Du) {
        kb_ctrl_held = 1;
        return;
    }

    if (scancode == 0x9Du) {
        kb_ctrl_held = 0;
        return;
    }

    if (scancode == 0x3Fu) {
        keyboard_ring_push((char)KEY_F5);
        return;
    }

    if (scancode == 0x53u) {
        keyboard_ring_push((char)KEY_DELETE);
        return;
    }

    if (scancode == 0x2Au || scancode == 0x36u) {
        is_shift_down = 1u;
        return;
    }

    if (scancode == 0xAAu || scancode == 0xB6u) {
        is_shift_down = 0u;
        return;
    }

    if (scancode == 0x3Au) {
        is_caps_lock_on ^= 1u;
        return;
    }

    if ((scancode & 0x80u) != 0u) {
        return;
    }

    if (scancode < 128u) {
        char c = is_shift_down ? scancode_shift[scancode] : scancode_normal[scancode];

        if (c >= 'a' && c <= 'z') {
            if ((is_caps_lock_on ^ is_shift_down) != 0u) {
                c = (char)(c - 'a' + 'A');
            }
        } else if (c >= 'A' && c <= 'Z') {
            if ((is_caps_lock_on ^ is_shift_down) == 0u) {
                c = (char)(c - 'A' + 'a');
            }
        }

        if (c != 0) {
            keyboard_ring_push(c);
        }
    }
}

int keyboard_ctrl_held(void) {
    return kb_ctrl_held;
}
