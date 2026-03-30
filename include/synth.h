#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

typedef enum {
    WAVE_SINE = 0,
    WAVE_SQUARE = 1,
    WAVE_TRIANGLE = 2,
    WAVE_SAWTOOTH = 3,
    WAVE_NOISE = 4
} WaveType;

void synth_generate(int16_t *out, uint32_t num_samples, uint32_t freq_hz, WaveType wave, uint8_t volume);
int synth_alloc_and_generate(uint32_t duration_ms, uint32_t freq_hz, WaveType wave, uint8_t volume);
void sound_startup(void);
void sound_click(void);
void sound_error(void);
void sound_notify(void);
void sound_close_window(void);
void sound_open_window(void);

#endif
