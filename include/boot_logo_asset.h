#ifndef BOOT_LOGO_ASSET_H
#define BOOT_LOGO_ASSET_H

#include <stdint.h>

struct BootLogoAsset {
    int width;
    int height;
    const uint32_t *pixels;
};

extern const struct BootLogoAsset boot_logo_asset;

#endif
