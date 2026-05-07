#include <stdint.h>

#include "boot_animation.h"
#include "boot_logo_asset.h"
#include "gfx.h"
#include "pit.h"

#define BOOT_BG 0x000000u
#define BOOT_TEXT 0xBEB7A6u
#define BOOT_BAR 0xD8D0BEu
#define BOOT_FRAMES 90u
#define BOOT_FRAME_TICKS 2u
#define BOOT_RARE_LABEL_THRESHOLD 3941u

static uint32_t boot_text_width(const char *text) {
    uint32_t len = 0u;

    while (text[len] != '\0') {
        len++;
    }
    return len * 8u;
}

static uint32_t boot_rdtsc_low(void) {
    uint32_t low;
    uint32_t high;

    __asm__ volatile ("rdtsc" : "=a"(low), "=d"(high));
    return low ^ (high * 0x9E3779B9u);
}

static uint32_t boot_entropy_word(uint32_t salt) {
    uint32_t value = boot_rdtsc_low() ^ get_ticks() ^ (salt * 0x85EBCA6Bu);

    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

static int boot_use_rare_label(void) {
    uint32_t i;
    uint32_t low = boot_entropy_word(0u);

    /*
     * 3941 / 2^384 is roughly 1e-112 as a boot probability,
     * i.e. about 0.000...0001% at the scale requested.
     */
    if (low >= BOOT_RARE_LABEL_THRESHOLD) {
        return 0;
    }

    for (i = 1u; i < 12u; i++) {
        if (boot_entropy_word(i) != 0u) {
            return 0;
        }
    }
    return 1;
}

static uint32_t boot_blend(uint32_t a, uint32_t b, uint32_t step, uint32_t steps) {
    uint32_t ar;
    uint32_t ag;
    uint32_t ab;
    uint32_t br;
    uint32_t bg;
    uint32_t bb;
    uint32_t r;
    uint32_t g;
    uint32_t blue;

    if (steps == 0u || step >= steps) {
        return b;
    }

    ar = (a >> 16) & 0xFFu;
    ag = (a >> 8) & 0xFFu;
    ab = a & 0xFFu;
    br = (b >> 16) & 0xFFu;
    bg = (b >> 8) & 0xFFu;
    bb = b & 0xFFu;
    r = ((ar * (steps - step)) + (br * step)) / steps;
    g = ((ag * (steps - step)) + (bg * step)) / steps;
    blue = ((ab * (steps - step)) + (bb * step)) / steps;
    return (r << 16) | (g << 8) | blue;
}

static void boot_draw_logo_image(int dst_x, int dst_y, int reveal, uint32_t fade_step, uint32_t fade_steps) {
    int px;
    int py;
    int reveal_w = (boot_logo_asset.width * reveal) / 100;

    for (py = 0; py < boot_logo_asset.height; py++) {
        for (px = 0; px < reveal_w; px++) {
            uint32_t pixel = boot_logo_asset.pixels[(py * boot_logo_asset.width) + px];

            if (fade_steps != 0u) {
                pixel = boot_blend(pixel, BOOT_BG, fade_step, fade_steps);
            }
            gfx_put_pixel((uint32_t)(dst_x + px), (uint32_t)(dst_y + py), pixel);
        }
    }
}

static void boot_wait_until(uint32_t tick) {
    while ((int32_t)(get_ticks() - tick) < 0) {
        __asm__ volatile ("hlt");
    }
}

void boot_animation_run(void) {
    uint32_t frame;
    int width = gfx_get_width();
    int height = gfx_get_height();
    int center_x;
    int logo_x;
    int logo_y;
    const char *boot_label;

    if (width <= 0 || height <= 0) {
        return;
    }

    center_x = width / 2;
    logo_x = (width - boot_logo_asset.width) / 2;
    logo_y = (height - boot_logo_asset.height) / 2 - 16;
    boot_label = boot_use_rare_label() ? "jjaamjmdec" : "coffeeOS";

    for (frame = 0u; frame <= BOOT_FRAMES; frame++) {
        uint32_t next_tick = get_ticks() + BOOT_FRAME_TICKS;
        int reveal = (int)((frame * 100u) / BOOT_FRAMES);
        int text_x;
        int bar_w = width / 4;
        int bar_x = (width - bar_w) / 2;
        int bar_y = logo_y + boot_logo_asset.height + 36;
        int fill_w = (bar_w * reveal) / 100;

        gfx_fill_screen(BOOT_BG);
        boot_draw_logo_image(logo_x, logo_y, reveal, 0u, 0u);

        text_x = center_x - (int)(boot_text_width(boot_label) / 2u);
        gfx_draw_string_at(text_x, logo_y + boot_logo_asset.height + 12, boot_label, BOOT_TEXT, BOOT_BG);

        gfx_draw_rect(bar_x, bar_y, bar_w, 2, 0x282420u);
        gfx_draw_rect(bar_x, bar_y, fill_w, 2, BOOT_BAR);
        gfx_present();
        boot_wait_until(next_tick);
    }

    for (frame = 0u; frame < 18u; frame++) {
        uint32_t next_tick = get_ticks() + BOOT_FRAME_TICKS;
        uint32_t bg = boot_blend(BOOT_BG, 0x0D151Cu, frame, 18u);

        gfx_fill_screen(bg);
        boot_draw_logo_image(logo_x, logo_y, 100, frame, 18u);
        gfx_present();
        boot_wait_until(next_tick);
    }
}
