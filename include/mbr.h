#ifndef MBR_H
#define MBR_H

#include "blkdev.h"

/* Parse one MBR disk and register any FAT32 partitions as child block devices. */
int mbr_register_partitions(BlockDevice *dev);

#endif
