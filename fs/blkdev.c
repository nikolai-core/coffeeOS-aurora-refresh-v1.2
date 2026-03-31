#include <stdint.h>

#include "blkdev.h"
#include "serial.h"

static BlockDevice blkdev_table[MAX_BLKDEVS];

/* Copy one fixed-length ASCII string into a block-device slot name. */
static void blkdev_copy_name(char *dst, const char *src) {
    uint32_t i = 0u;

    while (i + 1u < 16u && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < 16u) {
        dst[i++] = '\0';
    }
}

/* Return non-zero when two small ASCII strings match exactly. */
static int blkdev_name_eq(const char *a, const char *b) {
    uint32_t i = 0u;

    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* Render one unsigned value to serial in decimal. */
static void blkdev_write_u32(uint32_t value) {
    char digits[10];
    uint32_t count = 0u;

    if (value == 0u) {
        serial_write_char('0');
        return;
    }

    while (value != 0u && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (count != 0u) {
        serial_write_char(digits[--count]);
    }
}

/* Clear the fixed block-device registry before any backends register. */
void blkdev_init(void) {
    uint32_t i;

    for (i = 0u; i < MAX_BLKDEVS; i++) {
        blkdev_table[i].name[0] = '\0';
        blkdev_table[i].block_count = 0u;
        blkdev_table[i].block_size = 0u;
        blkdev_table[i].read = (int (*)(struct BlockDevice *, uint32_t, uint32_t, void *))0;
        blkdev_table[i].write = (int (*)(struct BlockDevice *, uint32_t, uint32_t, const void *))0;
        blkdev_table[i].private_data = (void *)0;
        blkdev_table[i].present = 0;
    }
}

/* Register one block device by copying it into the first free static slot. */
int blkdev_register(BlockDevice *dev) {
    uint32_t i;

    if (dev == (BlockDevice *)0 || dev->read == (int (*)(struct BlockDevice *, uint32_t, uint32_t, void *))0
        || dev->write == (int (*)(struct BlockDevice *, uint32_t, uint32_t, const void *))0) {
        return -1;
    }

    for (i = 0u; i < MAX_BLKDEVS; i++) {
        if (!blkdev_table[i].present) {
            blkdev_table[i] = *dev;
            blkdev_copy_name(blkdev_table[i].name, dev->name);
            blkdev_table[i].present = 1;
            return (int)i;
        }
    }

    return -1;
}

/* Return one registered device slot by index when it is valid and present. */
BlockDevice *blkdev_get(int index) {
    if (index < 0 || (uint32_t)index >= MAX_BLKDEVS || !blkdev_table[index].present) {
        return (BlockDevice *)0;
    }
    return &blkdev_table[index];
}

/* Return the registered device whose short name matches exactly. */
BlockDevice *blkdev_find(const char *name) {
    uint32_t i;

    if (name == (const char *)0) {
        return (BlockDevice *)0;
    }

    for (i = 0u; i < MAX_BLKDEVS; i++) {
        if (blkdev_table[i].present && blkdev_name_eq(blkdev_table[i].name, name)) {
            return &blkdev_table[i];
        }
    }
    return (BlockDevice *)0;
}

/* Print the current block-device registry to serial for debugging. */
void blkdev_list(void) {
    uint32_t i;

    for (i = 0u; i < MAX_BLKDEVS; i++) {
        if (!blkdev_table[i].present) {
            continue;
        }

        serial_print("[blkdev] ");
        serial_print(blkdev_table[i].name);
        serial_print(" blocks=");
        blkdev_write_u32(blkdev_table[i].block_count);
        serial_print(" block_size=");
        blkdev_write_u32(blkdev_table[i].block_size);
        serial_print("\n");
    }
}
