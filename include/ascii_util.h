#ifndef ASCII_UTIL_H
#define ASCII_UTIL_H

#include <stdint.h>

static inline uint32_t ascii_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static inline char ascii_tolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static inline int ascii_streq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static inline int ascii_starts_with(const char *s, const char *prefix) {
    uint32_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static inline void ascii_trim_lower_copy(char *dst, uint32_t dst_max_len, const char *src) {
    uint32_t start = 0;
    uint32_t end = ascii_strlen(src);
    uint32_t out = 0;
    uint32_t i;

    if (dst_max_len == 0u) {
        return;
    }

    while (src[start] == ' ') {
        start++;
    }

    while (end > start && src[end - 1u] == ' ') {
        end--;
    }

    for (i = start; i < end && out + 1u < dst_max_len; i++) {
        dst[out++] = ascii_tolower(src[i]);
    }
    dst[out] = '\0';
}

static inline void ascii_trim_copy(char *dst, uint32_t dst_max_len, const char *src) {
    uint32_t start = 0;
    uint32_t end = ascii_strlen(src);
    uint32_t out = 0;
    uint32_t i;

    if (dst_max_len == 0u) {
        return;
    }

    while (src[start] == ' ') {
        start++;
    }

    while (end > start && src[end - 1u] == ' ') {
        end--;
    }

    for (i = start; i < end && out + 1u < dst_max_len; i++) {
        dst[out++] = src[i];
    }
    dst[out] = '\0';
}

static inline uint8_t ascii_parse_hex_u8(const char *s, int *ok) {
    uint32_t value = 0;
    uint32_t digits = 0;

    while (*s == ' ') {
        s++;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s != '\0' && digits < 2u) {
        char c = *s;
        uint32_t nibble;

        if (c >= '0' && c <= '9') {
            nibble = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = 10u + (uint32_t)(c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            nibble = 10u + (uint32_t)(c - 'A');
        } else {
            break;
        }

        value = (value << 4) | nibble;
        digits++;
        s++;
    }

    while (*s == ' ') {
        s++;
    }

    if (digits == 0u || *s != '\0') {
        *ok = 0;
        return 0u;
    }

    *ok = 1;
    return (uint8_t)value;
}

static inline uint32_t ascii_parse_u32(const char *s, int *ok) {
    uint32_t value = 0;
    uint32_t digits = 0;

    while (*s == ' ') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = value * 10u + (uint32_t)(*s - '0');
        digits++;
        s++;
    }

    while (*s == ' ') {
        s++;
    }

    if (digits == 0u || *s != '\0') {
        *ok = 0;
        return 0u;
    }

    *ok = 1;
    return value;
}

static inline uint32_t ascii_parse_hex_u32(const char *s, int *ok) {
    uint32_t value = 0;
    uint32_t digits = 0;

    while (*s == ' ') {
        s++;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }

    while (*s != '\0' && digits < 8u) {
        char c = *s;
        uint32_t nibble;

        if (c >= '0' && c <= '9') {
            nibble = (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = 10u + (uint32_t)(c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            nibble = 10u + (uint32_t)(c - 'A');
        } else {
            break;
        }

        value = (value << 4) | nibble;
        digits++;
        s++;
    }

    while (*s == ' ') {
        s++;
    }

    if (digits == 0u || *s != '\0') {
        *ok = 0;
        return 0u;
    }

    *ok = 1;
    return value;
}

#endif
