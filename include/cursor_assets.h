#ifndef CURSOR_ASSETS_H
#define CURSOR_ASSETS_H

#include <stdint.h>

struct CursorAsset {
    const char *name;
    uint32_t frame_count;
    const struct CursorFrame *frames;
};

struct CursorFrame {
    int width;
    int height;
    int hotspot_x;
    int hotspot_y;
    uint32_t delay_ticks;
    const uint32_t *pixels;
};

const struct CursorAsset *cursor_assets_find(const char *name);
const struct CursorAsset *cursor_assets_default(void);

#endif
