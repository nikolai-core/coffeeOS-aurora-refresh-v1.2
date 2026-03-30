#include <stdint.h>

#include "io.h"
#include "pit.h"
#include "speaker.h"

#define SPEAKER_PORT 0x61u
#define SPEAKER_PIT_DATA 0x42u
#define SPEAKER_PIT_CMD 0x43u
#define SPEAKER_MIN_HZ 20u
#define SPEAKER_MAX_HZ 20000u
#define PIT_INPUT_HZ 1193182u

static uint32_t speaker_stop_tick;
static int speaker_beep_active;
static int speaker_available;

/* just probe the gate port so the rest of the driver can stay quiet if it is missing */
void speaker_init(void) {
    (void)io_in8(SPEAKER_PORT);
    speaker_stop();
    speaker_stop_tick = 0u;
    speaker_beep_active = 0;
    speaker_available = 1;
}

/* channel 2 square wave, then route it to the speaker gate */
void speaker_play_tone(uint32_t frequency_hz) {
    uint32_t hz = frequency_hz;
    uint32_t divisor;
    uint8_t gate;

    if (!speaker_available) {
        return;
    }

    if (hz < SPEAKER_MIN_HZ) {
        hz = SPEAKER_MIN_HZ;
    }
    if (hz > SPEAKER_MAX_HZ) {
        hz = SPEAKER_MAX_HZ;
    }

    divisor = PIT_INPUT_HZ / hz;
    if (divisor == 0u) {
        divisor = 1u;
    }

    io_out8(SPEAKER_PIT_CMD, 0xB6u);
    io_out8(SPEAKER_PIT_DATA, (uint8_t)(divisor & 0xFFu));
    io_out8(SPEAKER_PIT_DATA, (uint8_t)((divisor >> 8) & 0xFFu));

    gate = io_in8(SPEAKER_PORT);
    io_out8(SPEAKER_PORT, (uint8_t)(gate | 0x03u));
}

/* drop both gate bits so the square wave is disconnected */
void speaker_stop(void) {
    uint8_t gate = io_in8(SPEAKER_PORT);

    io_out8(SPEAKER_PORT, (uint8_t)(gate & (uint8_t)~0x03u));
    speaker_beep_active = 0;
    speaker_stop_tick = 0u;
}

/* non-blocking beep, speaker_update shuts it off later */
void speaker_beep(uint32_t freq_hz, uint32_t duration_ms) {
    uint32_t ticks;

    if (!speaker_available) {
        return;
    }

    speaker_play_tone(freq_hz);
    ticks = (duration_ms + 9u) / 10u;
    if (ticks == 0u) {
        ticks = 1u;
    }
    speaker_stop_tick = get_ticks() + ticks;
    speaker_beep_active = 1;
}

/* checked from the GUI loop, and harmless to call more often */
void speaker_update(void) {
    if (!speaker_beep_active) {
        return;
    }

    if ((int32_t)(get_ticks() - speaker_stop_tick) >= 0) {
        speaker_stop();
    }
}
