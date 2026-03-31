#ifndef RAMDISK_H
#define RAMDISK_H

#include <stdint.h>

#include "blkdev.h"

#define RAMDISK_SIZE_MB 16u
#define RAMDISK_SIZE (RAMDISK_SIZE_MB * 1024u * 1024u)
#define RAMDISK_BLOCKS (RAMDISK_SIZE / BLOCK_SIZE)
#define RAMDISK_VIRT_BASE 0xD8000000u
#define RAMDISK_COPY_WINDOW 0xDC000000u

int ramdisk_init(uint32_t preload_phys_addr, uint32_t preload_size);
void ramdisk_zero(void);
int ramdisk_load_from_memory(const uint8_t *src, uint32_t size);
int ramdisk_has_image(void);
int ramdisk_save_to_module(uint32_t dest_phys, uint32_t size);
int ramdisk_sync_backing_store(void);

#endif
