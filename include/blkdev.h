#ifndef BLKDEV_H
#define BLKDEV_H

#include <stdint.h>

#define BLOCK_SIZE 512u
#define MAX_BLKDEVS 4u

typedef struct BlockDevice {
    char name[16];
    uint32_t block_count;
    uint32_t block_size;
    int (*read)(struct BlockDevice *dev, uint32_t lba, uint32_t count, void *buf);
    int (*write)(struct BlockDevice *dev, uint32_t lba, uint32_t count, const void *buf);
    void *private_data;
    int present;
} BlockDevice;

void blkdev_init(void);
int blkdev_register(BlockDevice *dev);
BlockDevice *blkdev_get(int index);
BlockDevice *blkdev_find(const char *name);
void blkdev_list(void);

#endif
