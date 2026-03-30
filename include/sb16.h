#ifndef SB16_H
#define SB16_H

#include <stdint.h>

#define SB16_BASE         0x220u
#define SB16_RESET        (SB16_BASE + 0x6u)
#define SB16_READ         (SB16_BASE + 0xAu)
#define SB16_WRITE        (SB16_BASE + 0xCu)
#define SB16_READ_STATUS  (SB16_BASE + 0xEu)
#define SB16_ACK_16       (SB16_BASE + 0xFu)
#define SB16_MIXER_PORT   (SB16_BASE + 0x4u)
#define SB16_MIXER_DATA   (SB16_BASE + 0x5u)

int sb16_reset(void);
void sb16_write_dsp(uint8_t cmd);
uint8_t sb16_read_dsp(void);
void sb16_set_sample_rate(uint16_t hz);
void sb16_get_version(uint8_t *major, uint8_t *minor);
void sb16_set_volume(uint8_t master, uint8_t pcm);
void sb16_set_fill_callback(void (*callback)(uint8_t *dma_buf, uint16_t len));
void sb16_prime_buffers(void);
void sb16_begin_output(uint8_t stereo, uint8_t bits);
void sb16_start_playback(uint8_t *buf, uint16_t len, uint8_t stereo, uint8_t bits);
void sb16_stop(void);
void sb16_resume(void);
void sb16_handle_irq(void);

#endif
