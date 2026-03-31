#include <stdint.h>

#include "io.h"
#include "pit.h"
#include "speaker.h"

#define PIT_CH0_DATA 0x40u
#define PIT_COMMAND  0x43u
#define PIT_INPUT_HZ 1193182u
#define PIC1_DATA    0x21u

static volatile uint32_t pit_ticks;
static volatile uint8_t pit_fs_sync_pending;

/* Program PIT channel 0 and unmask IRQ0 so the timer starts ticking. */
void pit_init(uint32_t hz) {
    uint32_t divisor;
    uint8_t mask;

    if (hz == 0u) {
        hz = 100u;
    }

    divisor = PIT_INPUT_HZ / hz;
    if (divisor == 0u) {
        divisor = 1u;
    }

    pit_ticks = 0u;
    pit_fs_sync_pending = 0u;
    io_out8(PIT_COMMAND, 0x36u);
    io_out8(PIT_CH0_DATA, (uint8_t)(divisor & 0xFFu));
    io_out8(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFFu));

    mask = io_in8(PIC1_DATA);
    mask &= (uint8_t)~1u;
    io_out8(PIC1_DATA, mask);
}

/* Count each IRQ0 so the rest of the kernel can measure time. */
void pit_handle_irq(void) {
    pit_ticks++;
    speaker_update();
}

/* Return the current PIT tick counter. */
uint32_t get_ticks(void) {
    return pit_ticks;
}

/* Return one pending deferred filesystem sync request and clear the flag. */
int pit_take_fs_sync_request(void) {
    if (pit_fs_sync_pending == 0u) {
        return 0;
    }
    pit_fs_sync_pending = 0u;
    return 1;
}

/* Queue one deferred filesystem sync request for the main UI loops. */
void pit_request_fs_sync(void) {
    pit_fs_sync_pending = 1u;
}
