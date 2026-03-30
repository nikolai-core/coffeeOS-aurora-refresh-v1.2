#ifndef FORMAT_H
#define FORMAT_H

#include <stdint.h>

static inline void format_hex_u32(char out[11], uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    uint32_t nibble_index;

    out[0] = '0';
    out[1] = 'x';

    for (nibble_index = 0; nibble_index < 8u; nibble_index++) {
        uint32_t shift = (7u - nibble_index) * 4u;
        out[2u + nibble_index] = digits[(value >> shift) & 0xFu];
    }

    out[10] = '\0';
}

#endif

