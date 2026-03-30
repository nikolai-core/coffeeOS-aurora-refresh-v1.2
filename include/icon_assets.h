#ifndef ICON_ASSETS_H
#define ICON_ASSETS_H

#include <stdint.h>

struct IconAsset {
    const char *name;
    int width;
    int height;
    const uint32_t *pixels;
};

const struct IconAsset *icon_assets_find(const char *name);

#endif
