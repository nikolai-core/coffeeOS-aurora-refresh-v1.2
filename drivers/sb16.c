#include <stdint.h>

#include "dma.h"
#include "io.h"
#include "paging.h"
#include "sb16.h"

#define PIC1_CMD 0x20u
#define PIC1_DATA 0x21u
#define PIC2_CMD 0xA0u
#define PIC_EOI 0x20u
#define SB16_IRQ 5u
#define SB16_DMA_CHANNEL 5u
#define SB16_DMA_MODE_SINGLE 0x48u
#define SB16_DMA_BUFSZ 4096u

static void (*sb16_callback)(uint8_t *dma_buf, uint16_t len);

__attribute__((section(".dma"), aligned(65536)))
static uint8_t dma_buf_a[SB16_DMA_BUFSZ];
__attribute__((section(".dma"), aligned(65536)))
static uint8_t dma_buf_b[SB16_DMA_BUFSZ];

static uint8_t *sb16_active_buf;
static uint16_t sb16_active_len;
static uint8_t sb16_active_stereo;
static uint8_t sb16_active_bits;
static int sb16_running;

static uint32_t sb16_dma_phys(uint8_t *buf) {
    return (uint32_t)(uintptr_t)buf - KERNEL_VIRT_BASE;
}

/* DSP writes stall while bit 7 stays set */
void sb16_write_dsp(uint8_t cmd) {
    uint32_t tries = 100000u;

    while (tries-- > 0u) {
        if ((io_in8(SB16_WRITE) & 0x80u) == 0u) {
            io_out8(SB16_WRITE, cmd);
            return;
        }
    }
}

/* read once the DSP says a byte is waiting */
uint8_t sb16_read_dsp(void) {
    uint32_t tries = 100000u;

    while (tries-- > 0u) {
        if ((io_in8(SB16_READ_STATUS) & 0x80u) != 0u) {
            return io_in8(SB16_READ);
        }
    }

    return 0u;
}

/* the classic reset pulse and 0xAA sanity byte */
int sb16_reset(void) {
    uint32_t tries = 100000u;

    io_out8(SB16_RESET, 1u);
    io_wait();
    io_wait();
    io_out8(SB16_RESET, 0u);

    while (tries-- > 0u) {
        if ((io_in8(SB16_READ_STATUS) & 0x80u) != 0u) {
            return io_in8(SB16_READ) == 0xAAu;
        }
    }

    return 0;
}

/* 0x41 is the SB16 output sample-rate command */
void sb16_set_sample_rate(uint16_t hz) {
    uint16_t rate = hz;

    if (rate != 8000u && rate != 11025u && rate != 22050u && rate != 44100u) {
        rate = 22050u;
    }

    sb16_write_dsp(0x41u);
    sb16_write_dsp((uint8_t)((rate >> 8) & 0xFFu));
    sb16_write_dsp((uint8_t)(rate & 0xFFu));
}

/* enough to tell whether QEMU gave us a real SB16 DSP */
void sb16_get_version(uint8_t *major, uint8_t *minor) {
    sb16_write_dsp(0xE1u);
    if (major != (uint8_t *)0) {
        *major = sb16_read_dsp();
    }
    if (minor != (uint8_t *)0) {
        *minor = sb16_read_dsp();
    }
}

/* mixer volumes are 4-bit nibbles mirrored into left/right */
void sb16_set_volume(uint8_t master, uint8_t pcm) {
    uint8_t master_nibble = (uint8_t)(master >> 4);
    uint8_t pcm_nibble = (uint8_t)(pcm >> 4);
    uint8_t mix_val;

    mix_val = (uint8_t)((master_nibble << 4) | master_nibble);
    io_out8(SB16_MIXER_PORT, 0x22u);
    io_out8(SB16_MIXER_DATA, mix_val);

    mix_val = (uint8_t)((pcm_nibble << 4) | pcm_nibble);
    io_out8(SB16_MIXER_PORT, 0x04u);
    io_out8(SB16_MIXER_DATA, mix_val);
}

/* audio.c owns the mixer callback, the driver just invokes it */
void sb16_set_fill_callback(void (*callback)(uint8_t *dma_buf, uint16_t len)) {
    sb16_callback = callback;
}

/* fill both halves before the first IRQ so startup is clean */
void sb16_prime_buffers(void) {
    if (sb16_callback == (void (*)(uint8_t *, uint16_t))0) {
        return;
    }

    sb16_callback(dma_buf_a, SB16_DMA_BUFSZ);
    sb16_callback(dma_buf_b, SB16_DMA_BUFSZ);
}

/* start from buffer A once both halves are primed */
void sb16_begin_output(uint8_t stereo, uint8_t bits) {
    uint8_t mask = io_in8(PIC1_DATA);

    mask &= (uint8_t)~(1u << SB16_IRQ);
    io_out8(PIC1_DATA, mask);
    sb16_start_playback(dma_buf_a, SB16_DMA_BUFSZ, stereo, bits);
}

/* program one DMA block and kick 16-bit output */
void sb16_start_playback(uint8_t *buf, uint16_t len, uint8_t stereo, uint8_t bits) {
    uint8_t mode = 0x30u;
    uint16_t count = len - 1u;

    if (buf == (uint8_t *)0 || len == 0u) {
        return;
    }

    sb16_active_buf = buf;
    sb16_active_len = len;
    sb16_active_stereo = stereo;
    sb16_active_bits = bits;

    dma_setup_channel(SB16_DMA_CHANNEL, sb16_dma_phys(buf), count, SB16_DMA_MODE_SINGLE);

    sb16_write_dsp((bits == 16u && stereo != 0u) ? 0xB6u : 0xC6u);
    if (stereo != 0u) {
        mode |= 0x20u;
    }
    if (bits == 16u) {
        mode |= 0x10u;
    }
    sb16_write_dsp(mode);
    sb16_write_dsp((uint8_t)(count & 0xFFu));
    sb16_write_dsp((uint8_t)((count >> 8) & 0xFFu));

    sb16_running = 1;
}

/* pause the current stream without tearing down the callback state */
void sb16_stop(void) {
    sb16_write_dsp(0xD5u);
    sb16_running = 0;
}

/* resume after a stop or pause */
void sb16_resume(void) {
    sb16_write_dsp(0xD6u);
    sb16_running = 1;
}

/* IRQ5 ack, refill the idle half, then arm the next block */
void sb16_handle_irq(void) {
    uint8_t *next_buf;

    (void)io_in8(SB16_ACK_16);

    if (!sb16_running || sb16_active_len == 0u) {
        io_out8(PIC1_CMD, PIC_EOI);
        return;
    }

    next_buf = (sb16_active_buf == dma_buf_a) ? dma_buf_b : dma_buf_a;
    if (sb16_callback != (void (*)(uint8_t *, uint16_t))0) {
        sb16_callback(next_buf, SB16_DMA_BUFSZ);
    }

    sb16_start_playback(next_buf, sb16_active_len, sb16_active_stereo, sb16_active_bits);

    (void)PIC2_CMD;
    io_out8(PIC1_CMD, PIC_EOI);
}
