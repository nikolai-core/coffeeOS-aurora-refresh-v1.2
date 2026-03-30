#include <stdint.h>

#include "audio.h"
#include "serial.h"
#include "sb16.h"
#include "speaker.h"

int audio_backend = AUDIO_BACKEND_NONE;

static AudioSource audio_sources[AUDIO_MAX_SOURCES];
static uint8_t audio_master_volume = 200u;

/* zero the software mixer state before the hardware comes up */
static void audio_clear_sources(void) {
    uint32_t i;

    for (i = 0u; i < AUDIO_MAX_SOURCES; i++) {
        audio_sources[i].samples = (int16_t *)0;
        audio_sources[i].total_samples = 0u;
        audio_sources[i].position = 0u;
        audio_sources[i].volume = 0u;
        audio_sources[i].looping = 0u;
        audio_sources[i].active = 0u;
        audio_sources[i].paused = 0u;
    }
}

/* SB16 first, then drop back to the speaker if the DSP is missing */
void audio_init(void) {
    uint8_t major = 0u;
    uint8_t minor = 0u;

    speaker_init();
    audio_clear_sources();
    audio_backend = AUDIO_BACKEND_NONE;

    if (!sb16_reset()) {
        serial_print("[coffeeOS] audio: SB16 not found, speaker-only fallback\n");
        audio_backend = AUDIO_BACKEND_SPEAKER;
        return;
    }

    sb16_get_version(&major, &minor);
    serial_print("[coffeeOS] audio: SB16 DSP ");
    serial_write_hex((uint32_t)major);
    serial_print(".");
    serial_write_hex((uint32_t)minor);
    serial_print("\n");

    sb16_set_sample_rate((uint16_t)AUDIO_SAMPLE_RATE);
    sb16_set_volume(audio_master_volume, audio_master_volume);
    sb16_set_fill_callback(sb16_fill_callback);
    sb16_prime_buffers();
    sb16_begin_output((uint8_t)(AUDIO_CHANNELS == 2u), (uint8_t)AUDIO_BITS);
    audio_backend = AUDIO_BACKEND_SB16;
}

/* simple sum-and-clamp mixer for a tiny fixed source count */
void audio_mix(int16_t *out, uint32_t num_samples) {
    uint32_t i;
    uint32_t src_idx;

    if (out == (int16_t *)0) {
        return;
    }

    for (i = 0u; i < num_samples; i++) {
        int32_t mixed = 0;

        for (src_idx = 0u; src_idx < AUDIO_MAX_SOURCES; src_idx++) {
            AudioSource *src = &audio_sources[src_idx];

            if (!src->active || src->paused || src->samples == (int16_t *)0) {
                continue;
            }

            mixed += (int32_t)((src->samples[src->position] * src->volume) / 255u);
            src->position++;

            if (src->position >= src->total_samples) {
                if (src->looping) {
                    src->position = 0u;
                } else {
                    src->active = 0u;
                    src->position = src->total_samples;
                }
            }
        }

        mixed = (mixed * audio_master_volume) / 255;
        if (mixed > 32767) {
            mixed = 32767;
        } else if (mixed < -32768) {
            mixed = -32768;
        }

        out[i] = (int16_t)mixed;
    }
}

/* the SB16 IRQ path hands us one idle DMA half to refill */
void sb16_fill_callback(uint8_t *dma_buf, uint16_t len) {
    if (dma_buf == (uint8_t *)0 || len == 0u) {
        return;
    }

    audio_mix((int16_t *)(void *)dma_buf, len / (uint16_t)sizeof(int16_t));
}

/* fixed slot allocator, good enough until we have a heap or scheduler */
int audio_play(int16_t *samples, uint32_t count, uint8_t volume, uint8_t loop) {
    uint32_t i;

    if (samples == (int16_t *)0 || count == 0u || audio_backend != AUDIO_BACKEND_SB16) {
        return -1;
    }

    for (i = 0u; i < AUDIO_MAX_SOURCES; i++) {
        if (!audio_sources[i].active) {
            audio_sources[i].samples = samples;
            audio_sources[i].total_samples = count;
            audio_sources[i].position = 0u;
            audio_sources[i].volume = volume;
            audio_sources[i].looping = loop;
            audio_sources[i].active = 1u;
            audio_sources[i].paused = 0u;
            return (int)i;
        }
    }

    return -1;
}

/* stop one source slot immediately */
void audio_stop(int slot) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES) {
        return;
    }

    audio_sources[slot].active = 0u;
    audio_sources[slot].paused = 0u;
    audio_sources[slot].position = audio_sources[slot].total_samples;
}

/* pause leaves position intact so resume can continue from there */
void audio_pause(int slot) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES || !audio_sources[slot].active) {
        return;
    }

    audio_sources[slot].paused = 1u;
}

/* resume only matters for an active slot */
void audio_resume(int slot) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES || !audio_sources[slot].active) {
        return;
    }

    audio_sources[slot].paused = 0u;
}

/* per-source volume is just a byte scalar for now */
void audio_set_volume(int slot, uint8_t volume) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES) {
        return;
    }

    audio_sources[slot].volume = volume;
}

/* tiny GUI helpers need to read the current source volume back */
uint8_t audio_get_volume(int slot) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES) {
        return 0u;
    }

    return audio_sources[slot].volume;
}

/* master volume drives both the software mix and the SB16 mixer register */
void audio_set_master_volume(uint8_t volume) {
    audio_master_volume = volume;

    if (audio_backend == AUDIO_BACKEND_SB16) {
        sb16_set_volume(volume, volume);
    }
}

/* mixer app reads this to seed the top slider */
uint8_t audio_get_master_volume(void) {
    return audio_master_volume;
}

/* GUI VU meters only need to know if a slot is live right now */
int audio_is_playing(int slot) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES) {
        return 0;
    }

    return audio_sources[slot].active && !audio_sources[slot].paused;
}

/* synth.c needs to know whether a slot can be reused */
int audio_slot_in_use(int slot) {
    if (slot < 0 || slot >= (int)AUDIO_MAX_SOURCES) {
        return 0;
    }

    return audio_sources[slot].active;
}
