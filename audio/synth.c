#include <stdint.h>

#include "audio.h"
#include "speaker.h"
#include "synth.h"

#define SYNTH_TABLE_BITS 8u
#define SYNTH_TABLE_SIZE (1u << SYNTH_TABLE_BITS)
#define SYNTH_PHASE_SHIFT (32u - SYNTH_TABLE_BITS)
#define SYNTH_MAX_SECONDS 2u
#define SYNTH_BUF_SAMPLES (AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * SYNTH_MAX_SECONDS)

static int16_t synth_buffers[AUDIO_MAX_SOURCES][SYNTH_BUF_SAMPLES];
static uint32_t synth_noise_state = 0x12345678u;

static const int16_t synth_sine_table[SYNTH_TABLE_SIZE] = {
    0,804,1607,2410,3211,4011,4808,5602,6393,7179,7962,8739,9512,10278,11039,11793,
    12539,13279,14010,14732,15446,16150,16845,17530,18204,18868,19519,20159,20787,21403,22005,22594,
    23170,23731,24279,24811,25329,25831,26318,26789,27244,27683,28105,28510,28898,29269,29622,29957,
    30274,30572,30853,31114,31356,31580,31784,31968,32133,32279,32404,32510,32596,32662,32708,32734,
    32740,32726,32692,32637,32563,32468,32353,32218,32063,31888,31693,31479,31245,30991,30718,30425,
    30113,29782,29432,29063,28675,28269,27845,27403,26943,26465,25970,25458,24929,24384,23822,23244,
    22650,22041,21416,20776,20122,19453,18770,18074,17364,16642,15907,15160,14402,13632,12851,12060,
    11259,10449,9630,8802,7967,7124,6274,5418,4556,3688,2816,1940,1060,177,-707,-1591,
    -2474,-3355,-4234,-5109,-5982,-6849,-7712,-8571,-9423,-10270,-11109,-11942,-12766,-13582,-14388,-15185,
    -15971,-16746,-17510,-18262,-19001,-19728,-20442,-21142,-21829,-22501,-23158,-23800,-24426,-25036,-25630,-26207,
    -26766,-27308,-27832,-28337,-28823,-29289,-29736,-30163,-30569,-30955,-31319,-31663,-31984,-32284,-32561,-32767,
    -32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,-32767,
    -32767,-32767,-32767,-32767,-32641,-32364,-32063,-31738,-31389,-31016,-30620,-30200,-29757,-29291,-28803,-28292,
    -27759,-27203,-26627,-26029,-25410,-24771,-24112,-23433,-22735,-22018,-21283,-20530,-19760,-18973,-18170,-17352,
    -16519,-15672,-14812,-13939,-13054,-12158,-11251,-10335,-9410,-8477,-7537,-6591,-5640,-4684,-3725,-2763,
    -1800,-837
};

/* the speaker backend only gets simple tones, not mixed PCM */
static int synth_backend_allows_pcm(void) {
    return audio_backend == AUDIO_BACKEND_SB16;
}

/* tiny LCG is enough for hiss/noise effects */
static uint32_t synth_next_noise(void) {
    synth_noise_state = synth_noise_state * 1103515245u + 12345u;
    return synth_noise_state;
}

/* clamp pitch a bit so bad syscall args do not explode the phase math */
static uint32_t synth_clamp_freq(uint32_t freq_hz) {
    if (freq_hz < 20u) {
        return 20u;
    }
    if (freq_hz > 20000u) {
        return 20000u;
    }
    return freq_hz;
}

/* generate interleaved stereo samples straight into the destination buffer */
void synth_generate(int16_t *out, uint32_t num_samples, uint32_t freq_hz, WaveType wave, uint8_t volume) {
    uint32_t phase = 0u;
    uint32_t step;
    uint32_t idx;
    int16_t amp = (int16_t)((32767 * volume) / 255u);

    if (out == (int16_t *)0 || num_samples == 0u) {
        return;
    }

    freq_hz = synth_clamp_freq(freq_hz);
    step = (freq_hz * SYNTH_TABLE_SIZE * 256u) / AUDIO_SAMPLE_RATE;
    if (step == 0u) {
        step = 1u;
    }

    for (idx = 0u; idx + 1u < num_samples; idx += 2u) {
        int16_t sample = 0;
        uint32_t table_idx = (phase >> 8) & (SYNTH_TABLE_SIZE - 1u);

        if (wave == WAVE_SINE) {
            sample = (int16_t)((synth_sine_table[table_idx] * amp) / 32767);
        } else if (wave == WAVE_SQUARE) {
            sample = (table_idx < (SYNTH_TABLE_SIZE / 2u)) ? amp : (int16_t)-amp;
        } else if (wave == WAVE_TRIANGLE) {
            int32_t tri;

            if (table_idx < (SYNTH_TABLE_SIZE / 2u)) {
                tri = -32767 + (int32_t)((table_idx * 65534u) / (SYNTH_TABLE_SIZE / 2u));
            } else {
                tri = 32767 - (int32_t)(((table_idx - (SYNTH_TABLE_SIZE / 2u)) * 65534u) / (SYNTH_TABLE_SIZE / 2u));
            }
            sample = (int16_t)((tri * amp) / 32767);
        } else if (wave == WAVE_SAWTOOTH) {
            int32_t saw = -32767 + (int32_t)((table_idx * 65534u) / SYNTH_TABLE_SIZE);
            sample = (int16_t)((saw * amp) / 32767);
        } else {
            sample = (int16_t)(((int32_t)(synth_next_noise() >> 16) - 32768) * amp / 32767);
        }

        out[idx] = sample;
        out[idx + 1u] = sample;
        phase += step;
    }
}

/* fixed backing buffers match the fixed mixer source count */
int synth_alloc_and_generate(uint32_t duration_ms, uint32_t freq_hz, WaveType wave, uint8_t volume) {
    uint32_t frames;
    uint32_t sample_count;
    uint32_t slot_idx;
    int slot;

    if (audio_backend == AUDIO_BACKEND_SPEAKER) {
        speaker_beep(freq_hz, duration_ms);
        return -1;
    }
    if (!synth_backend_allows_pcm()) {
        return -1;
    }

    frames = (duration_ms * AUDIO_SAMPLE_RATE) / 1000u;
    if (frames == 0u) {
        frames = 1u;
    }
    if (frames > (AUDIO_SAMPLE_RATE * SYNTH_MAX_SECONDS)) {
        frames = AUDIO_SAMPLE_RATE * SYNTH_MAX_SECONDS;
    }

    sample_count = frames * AUDIO_CHANNELS;
    for (slot_idx = 0u; slot_idx < AUDIO_MAX_SOURCES; slot_idx++) {
        if (!audio_slot_in_use((int)slot_idx)) {
            synth_generate(synth_buffers[slot_idx], sample_count, freq_hz, wave, volume);
            slot = audio_play(synth_buffers[slot_idx], sample_count, volume, 0u);
            if (slot >= 0) {
                return slot;
            }
        }
    }

    return -1;
}

/* speaker fallback just gets one clean beep instead of layered chords */
static void sound_play_simple(uint32_t freq_hz, uint32_t duration_ms, WaveType wave, uint8_t volume) {
    (void)synth_alloc_and_generate(duration_ms, freq_hz, wave, volume);
}

/* startup is deliberately short so the desktop still feels snappy */
void sound_startup(void) {
    if (audio_backend == AUDIO_BACKEND_SB16) {
        (void)synth_alloc_and_generate(120u, 440u, WAVE_SINE, 160u);
        (void)synth_alloc_and_generate(140u, 554u, WAVE_SINE, 150u);
        (void)synth_alloc_and_generate(180u, 659u, WAVE_SINE, 140u);
        return;
    }

    speaker_beep(880u, 120u);
}

/* tiny UI click */
void sound_click(void) {
    sound_play_simple(800u, 20u, WAVE_SQUARE, 180u);
}

/* one low tone is enough to flag a bad command or parse error */
void sound_error(void) {
    sound_play_simple(240u, 180u, WAVE_SAWTOOTH, 190u);
}

/* general purpose notification */
void sound_notify(void) {
    sound_play_simple(1000u, 100u, WAVE_SINE, 170u);
}

/* close sound keeps the pitch lower than open */
void sound_close_window(void) {
    sound_play_simple(420u, 80u, WAVE_TRIANGLE, 150u);
}

/* quick upward-feeling open cue */
void sound_open_window(void) {
    sound_play_simple(620u, 80u, WAVE_TRIANGLE, 150u);
}
