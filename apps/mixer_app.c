#include <stdint.h>

#include "app.h"
#include "ascii_util.h"
#include "audio.h"
#include "mouse.h"
#include "synth.h"

#define MIXER_PADDING 10
#define MIXER_TRACK_H 8
#define MIXER_THUMB_W 12
#define MIXER_THUMB_H 16
#define MIXER_MUTE_W 38
#define MIXER_MUTE_H 18
#define MIXER_VU_W 12
#define MIXER_VU_H 32
#define MIXER_TEST_BUTTON_W 44
#define MIXER_TEST_BUTTON_H 22
#define MIXER_LABEL_COLOR 0xCCCCCCu
#define MIXER_BG 0x1E1E1Eu
#define MIXER_TRACK 0x3A3A3Au
#define MIXER_THUMB 0xE0E0E0u
#define MIXER_MUTE_ON 0xFF4444u
#define MIXER_MUTE_OFF 0x444444u
#define MIXER_VU_ON 0x44FF44u
#define MIXER_VU_OFF 0x333333u
#define MIXER_BUTTON 0x2A5298u
#define MIXER_BUTTON_HOVER 0x3A62A8u

enum MixerDragTarget {
    MIXER_DRAG_NONE = -2,
    MIXER_DRAG_MASTER = -1
};

static int mixer_client_x;
static int mixer_client_y;
static int mixer_client_w;
static int mixer_client_h;
static int mixer_drag_target;
static uint8_t mixer_master_volume;
static uint8_t mixer_channel_volume[AUDIO_MAX_SOURCES];
static uint8_t mixer_channel_muted[AUDIO_MAX_SOURCES];
static int mixer_hover_wave;

/* tiny decimal formatter for slider labels */
static void mixer_append_u32(char *buf, int buf_len, uint32_t val) {
    char digits[10];
    int n = 0;
    int len = (int)ascii_strlen(buf);
    int idx;

    if (len >= buf_len - 1) {
        return;
    }
    if (val == 0u) {
        buf[len] = '0';
        buf[len + 1] = '\0';
        return;
    }

    while (val > 0u && n < (int)(sizeof(digits) / sizeof(digits[0]))) {
        digits[n++] = (char)('0' + (val % 10u));
        val /= 10u;
    }

    for (idx = n - 1; idx >= 0 && len < buf_len - 1; idx--) {
        buf[len++] = digits[idx];
    }
    buf[len] = '\0';
}

/* map a pointer x offset back to the 0..255 slider range */
static uint8_t mixer_value_from_x(int local_x, int track_x, int track_w) {
    int rel = local_x - track_x;

    if (rel < 0) {
        rel = 0;
    }
    if (rel > track_w) {
        rel = track_w;
    }

    return (uint8_t)((rel * 255) / track_w);
}

/* one helper for the long slider rows */
static void mixer_slider_rect(int channel, int *x, int *y, int *w, int *h) {
    int row_y;
    int top = MIXER_PADDING + 22;

    if (channel == MIXER_DRAG_MASTER) {
        if (x != (int *)0) *x = MIXER_PADDING;
        if (y != (int *)0) *y = top;
        if (w != (int *)0) *w = mixer_client_w - (MIXER_PADDING * 2);
        if (h != (int *)0) *h = MIXER_TRACK_H;
        return;
    }

    row_y = MIXER_PADDING + 58 + (channel * 48);
    if (x != (int *)0) *x = MIXER_PADDING + 34;
    if (y != (int *)0) *y = row_y + 16;
    if (w != (int *)0) *w = mixer_client_w - (MIXER_PADDING * 2) - 70;
    if (h != (int *)0) *h = MIXER_TRACK_H;
}

/* mute sits to the right of each channel slider */
static void mixer_mute_rect(int channel, int *x, int *y, int *w, int *h) {
    int sx;
    int sy;
    int sw;
    int sh;

    mixer_slider_rect(channel, &sx, &sy, &sw, &sh);
    if (x != (int *)0) *x = sx + sw + 8;
    if (y != (int *)0) *y = sy - 4;
    if (w != (int *)0) *w = MIXER_MUTE_W;
    if (h != (int *)0) *h = MIXER_MUTE_H;
}

/* VU meter is just a live/inactive lamp for now */
static void mixer_vu_rect(int channel, int *x, int *y, int *w, int *h) {
    int sx;
    int sy;

    mixer_slider_rect(channel, &sx, &sy, (int *)0, (int *)0);
    if (x != (int *)0) *x = MIXER_PADDING + 12;
    if (y != (int *)0) *y = sy - 12;
    if (w != (int *)0) *w = MIXER_VU_W;
    if (h != (int *)0) *h = MIXER_VU_H;
}

/* bottom row test buttons */
static void mixer_test_button_rect(int wave_idx, int *x, int *y, int *w, int *h) {
    int start_y = mixer_client_h - MIXER_PADDING - MIXER_TEST_BUTTON_H;
    int start_x = MIXER_PADDING;

    if (x != (int *)0) *x = start_x + (wave_idx * (MIXER_TEST_BUTTON_W + 4));
    if (y != (int *)0) *y = start_y;
    if (w != (int *)0) *w = MIXER_TEST_BUTTON_W;
    if (h != (int *)0) *h = MIXER_TEST_BUTTON_H;
}

/* update audio state every frame so dragging feels immediate */
static void mixer_commit_audio(void) {
    int idx;

    audio_set_master_volume(mixer_master_volume);
    for (idx = 0; idx < (int)AUDIO_MAX_SOURCES; idx++) {
        audio_set_volume(idx, mixer_channel_muted[idx] ? 0u : mixer_channel_volume[idx]);
    }
}

/* slider drags use the current pointer position, not just the initial click */
static void mixer_update_drag(void) {
    int mouse_x;
    int mouse_y;
    int buttons = 0;
    int track_x;
    int track_y;
    int track_w;
    int track_h;
    int local_x;

    mouse_get_state((int *)0, (int *)0, &buttons, (int *)0);
    if (mixer_drag_target == MIXER_DRAG_NONE) {
        return;
    }

    if ((buttons & 0x01) == 0) {
        mixer_drag_target = MIXER_DRAG_NONE;
        return;
    }

    mouse_get_pos(&mouse_x, &mouse_y);
    local_x = mouse_x - mixer_client_x;
    (void)mouse_y;

    mixer_slider_rect(mixer_drag_target, &track_x, &track_y, &track_w, &track_h);
    if (mixer_drag_target == MIXER_DRAG_MASTER) {
        mixer_master_volume = mixer_value_from_x(local_x, track_x, track_w);
    } else if (mixer_drag_target >= 0 && mixer_drag_target < (int)AUDIO_MAX_SOURCES) {
        mixer_channel_volume[mixer_drag_target] = mixer_value_from_x(local_x, track_x, track_w);
    }

    (void)track_y;
    (void)track_h;
    mixer_commit_audio();
}

/* shared slider chrome for master and per-channel rows */
static void mixer_draw_slider(int x, int y, int w, uint8_t val) {
    int thumb_x = x + ((w - MIXER_THUMB_W) * val) / 255;

    app_draw_rect(x, y, w, MIXER_TRACK_H, MIXER_TRACK);
    app_draw_rect(thumb_x, y - ((MIXER_THUMB_H - MIXER_TRACK_H) / 2), MIXER_THUMB_W, MIXER_THUMB_H, MIXER_THUMB);
}

/* button hit testing stays in app-local coordinates */
static int mixer_point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

/* hover only matters for the test tone row */
static void mixer_update_hover(void) {
    int mouse_x;
    int mouse_y;
    int local_x;
    int local_y;
    int idx;

    mouse_get_pos(&mouse_x, &mouse_y);
    local_x = mouse_x - mixer_client_x;
    local_y = mouse_y - mixer_client_y;
    mixer_hover_wave = -1;

    for (idx = 0; idx < 5; idx++) {
        int x;
        int y;
        int w;
        int h;

        mixer_test_button_rect(idx, &x, &y, &w, &h);
        if (mixer_point_in_rect(local_x, local_y, x, y, w, h)) {
            mixer_hover_wave = idx;
            return;
        }
    }
}

/* each test button maps straight to one waveform */
static void mixer_play_test(int wave_idx) {
    WaveType wave = WAVE_SINE;

    if (wave_idx == 1) {
        wave = WAVE_SQUARE;
    } else if (wave_idx == 2) {
        wave = WAVE_TRIANGLE;
    } else if (wave_idx == 3) {
        wave = WAVE_SAWTOOTH;
    } else if (wave_idx == 4) {
        wave = WAVE_NOISE;
    }

    (void)synth_alloc_and_generate(500u, 440u, wave, 200u);
}

/* seed the sliders from the current audio state */
static void mixer_on_init(void) {
    int idx;

    mixer_client_x = 0;
    mixer_client_y = 0;
    mixer_client_w = 0;
    mixer_client_h = 0;
    mixer_drag_target = MIXER_DRAG_NONE;
    mixer_hover_wave = -1;
    mixer_master_volume = audio_get_master_volume();
    if (mixer_master_volume == 0u) {
        mixer_master_volume = 200u;
    }

    for (idx = 0; idx < (int)AUDIO_MAX_SOURCES; idx++) {
        mixer_channel_volume[idx] = audio_get_volume(idx);
        if (mixer_channel_volume[idx] == 0u) {
            mixer_channel_volume[idx] = 200u;
        }
        mixer_channel_muted[idx] = 0u;
    }

    mixer_commit_audio();
}

/* all mixer drawing lives here so the app stays stateless outside its controls */
static void mixer_on_draw(int win_x, int win_y, int win_w, int win_h) {
    static const char *wave_labels[5] = {"Sine", "Square", "Tri", "Saw", "Noise"};
    int idx;
    char label[24];

    mixer_client_x = win_x;
    mixer_client_y = win_y;
    mixer_client_w = win_w;
    mixer_client_h = win_h;

    mixer_update_drag();
    mixer_update_hover();

    app_clear(MIXER_BG);

    app_draw_string(MIXER_PADDING, MIXER_PADDING, "Master", MIXER_LABEL_COLOR, MIXER_BG);
    {
        int sx;
        int sy;
        int sw;
        int sh;

        mixer_slider_rect(MIXER_DRAG_MASTER, &sx, &sy, &sw, &sh);
        mixer_draw_slider(sx, sy, sw, mixer_master_volume);
        label[0] = '\0';
        mixer_append_u32(label, (int)sizeof(label), mixer_master_volume);
        app_draw_string(sx + sw - 24, sy - 18, label, MIXER_LABEL_COLOR, MIXER_BG);
        (void)sh;
    }

    for (idx = 0; idx < (int)AUDIO_MAX_SOURCES; idx++) {
        int sx;
        int sy;
        int sw;
        int mx;
        int my;
        int mw;
        int mh;
        int vx;
        int vy;
        int vw;
        int vh;

        label[0] = 'C';
        label[1] = 'h';
        label[2] = ' ';
        label[3] = (char)('1' + idx);
        label[4] = '\0';

        mixer_slider_rect(idx, &sx, &sy, &sw, (int *)0);
        mixer_mute_rect(idx, &mx, &my, &mw, &mh);
        mixer_vu_rect(idx, &vx, &vy, &vw, &vh);

        app_draw_string(MIXER_PADDING, sy - 12, label, MIXER_LABEL_COLOR, MIXER_BG);
        mixer_draw_slider(sx, sy, sw, mixer_channel_volume[idx]);
        app_draw_rect(mx, my, mw, mh, mixer_channel_muted[idx] ? MIXER_MUTE_ON : MIXER_MUTE_OFF);
        app_draw_string(mx + 8, my + 3, "Mute", 0xFFFFFFu, mixer_channel_muted[idx] ? MIXER_MUTE_ON : MIXER_MUTE_OFF);
        app_draw_rect(vx, vy, vw, vh, audio_is_playing(idx) ? MIXER_VU_ON : MIXER_VU_OFF);
    }

    app_draw_string(MIXER_PADDING, mixer_client_h - MIXER_PADDING - MIXER_TEST_BUTTON_H - 18,
                    "Test Sound", MIXER_LABEL_COLOR, MIXER_BG);
    for (idx = 0; idx < 5; idx++) {
        int bx;
        int by;
        int bw;
        int bh;
        uint32_t color = (idx == mixer_hover_wave) ? MIXER_BUTTON_HOVER : MIXER_BUTTON;

        mixer_test_button_rect(idx, &bx, &by, &bw, &bh);
        app_draw_rect(bx, by, bw, bh, color);
        app_draw_string(bx + 5, by + 4, wave_labels[idx], 0xFFFFFFu, color);
    }
}

/* keyboard shortcuts are tiny but useful for testing in a VM */
static void mixer_on_key(char c) {
    if (c >= '1' && c <= '5') {
        mixer_play_test(c - '1');
    }
}

/* click routing is just sliders, mute toggles, and the test buttons */
static void mixer_on_click(int x, int y, int btn) {
    int idx;

    if ((btn & 0x01) == 0) {
        return;
    }

    for (idx = 0; idx < (int)AUDIO_MAX_SOURCES; idx++) {
        int mx;
        int my;
        int mw;
        int mh;

        mixer_mute_rect(idx, &mx, &my, &mw, &mh);
        if (mixer_point_in_rect(x, y, mx, my, mw, mh)) {
            mixer_channel_muted[idx] = (uint8_t)!mixer_channel_muted[idx];
            mixer_commit_audio();
            return;
        }
    }

    for (idx = 0; idx < 5; idx++) {
        int bx;
        int by;
        int bw;
        int bh;

        mixer_test_button_rect(idx, &bx, &by, &bw, &bh);
        if (mixer_point_in_rect(x, y, bx, by, bw, bh)) {
            mixer_play_test(idx);
            return;
        }
    }

    {
        int sx;
        int sy;
        int sw;
        int sh;

        mixer_slider_rect(MIXER_DRAG_MASTER, &sx, &sy, &sw, &sh);
        if (mixer_point_in_rect(x, y, sx, sy - 6, sw, sh + 12)) {
            mixer_drag_target = MIXER_DRAG_MASTER;
            mixer_master_volume = mixer_value_from_x(x, sx, sw);
            mixer_commit_audio();
            return;
        }
    }

    for (idx = 0; idx < (int)AUDIO_MAX_SOURCES; idx++) {
        int sx;
        int sy;
        int sw;
        int sh;

        mixer_slider_rect(idx, &sx, &sy, &sw, &sh);
        if (mixer_point_in_rect(x, y, sx, sy - 6, sw, sh + 12)) {
            mixer_drag_target = idx;
            mixer_channel_volume[idx] = mixer_value_from_x(x, sx, sw);
            mixer_commit_audio();
            return;
        }
    }
}

App mixer_app = {
    .title = "Audio Mixer",
    .x = 560, .y = 80, .w = 260, .h = 320,
    .bg_color = MIXER_BG,
    .on_init = mixer_on_init,
    .on_draw = mixer_on_draw,
    .on_key = mixer_on_key,
    .on_click = mixer_on_click,
    .on_close = 0
};
