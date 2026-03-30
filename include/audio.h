#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#define AUDIO_SAMPLE_RATE 22050u
#define AUDIO_CHANNELS 2u
#define AUDIO_BITS 16u
#define AUDIO_BUF_SAMPLES 1024u
#define AUDIO_MAX_SOURCES 4u

enum {
    AUDIO_BACKEND_NONE = 0,
    AUDIO_BACKEND_SPEAKER = 1,
    AUDIO_BACKEND_SB16 = 2
};

typedef struct AudioSource {
    int16_t *samples;
    uint32_t total_samples;
    uint32_t position;
    uint8_t volume;
    uint8_t looping;
    uint8_t active;
    uint8_t paused;
} AudioSource;

extern int audio_backend;

void audio_init(void);
void audio_mix(int16_t *out, uint32_t num_samples);
void sb16_fill_callback(uint8_t *dma_buf, uint16_t len);
int audio_play(int16_t *samples, uint32_t count, uint8_t volume, uint8_t loop);
void audio_stop(int slot);
void audio_pause(int slot);
void audio_resume(int slot);
void audio_set_volume(int slot, uint8_t volume);
uint8_t audio_get_volume(int slot);
void audio_set_master_volume(uint8_t volume);
uint8_t audio_get_master_volume(void);
int audio_is_playing(int slot);
int audio_slot_in_use(int slot);

#endif
