#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>

void speaker_init(void);
void speaker_play_tone(uint32_t frequency_hz);
void speaker_stop(void);
void speaker_beep(uint32_t freq_hz, uint32_t duration_ms);
void speaker_update(void);

#endif
