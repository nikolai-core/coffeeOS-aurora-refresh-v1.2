#ifndef FAT32_FORMAT_H
#define FAT32_FORMAT_H

#include "blkdev.h"

int fat32_format(BlockDevice *dev, const char *volume_label);

#endif
