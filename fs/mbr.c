#include <stdint.h>

#include "blkdev.h"
#include "mbr.h"
#include "serial.h"

#define MBR_PARTITION_COUNT 4u
#define MBR_SIGNATURE_OFFSET 510u
#define MBR_PARTITION_OFFSET 446u
#define MBR_TYPE_FAT32_CHS 0x0Bu
#define MBR_TYPE_FAT32_LBA 0x0Cu

typedef struct __attribute__((packed)) MbrPartitionEntry {
    uint8_t bootable;
    uint8_t start_chs[3];
    uint8_t type;
    uint8_t end_chs[3];
    uint32_t start_lba;
    uint32_t sector_count;
} MbrPartitionEntry;

typedef struct MbrPartitionDevice {
    BlockDevice *parent;
    uint32_t start_lba;
    uint32_t sector_count;
} MbrPartitionDevice;

static BlockDevice mbr_partition_devices[MBR_PARTITION_COUNT];
static MbrPartitionDevice mbr_partition_data[MBR_PARTITION_COUNT];

/* Read one partitioned block device by rebasing the caller LBA onto the parent device. */
static int mbr_partition_read(BlockDevice *dev, uint32_t lba, uint32_t count, void *buf) {
    MbrPartitionDevice *part = (MbrPartitionDevice *)dev->private_data;

    if (part == (MbrPartitionDevice *)0 || part->parent == (BlockDevice *)0 || buf == (void *)0
        || count == 0u || lba >= part->sector_count || count > part->sector_count - lba) {
        return -1;
    }
    return part->parent->read(part->parent, part->start_lba + lba, count, buf);
}

/* Write one partitioned block device by rebasing the caller LBA onto the parent device. */
static int mbr_partition_write(BlockDevice *dev, uint32_t lba, uint32_t count, const void *buf) {
    MbrPartitionDevice *part = (MbrPartitionDevice *)dev->private_data;

    if (part == (MbrPartitionDevice *)0 || part->parent == (BlockDevice *)0 || buf == (const void *)0
        || count == 0u || lba >= part->sector_count || count > part->sector_count - lba) {
        return -1;
    }
    return part->parent->write(part->parent, part->start_lba + lba, count, buf);
}

/* Format one small decimal number for serial partition scan logs. */
static void mbr_write_u32(uint32_t value) {
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

/* Parse an MBR sector and register any FAT32 partitions as child block devices. */
int mbr_register_partitions(BlockDevice *dev) {
    uint8_t sector[BLOCK_SIZE];
    uint32_t i;

    if (dev == (BlockDevice *)0 || dev->read == (int (*)(struct BlockDevice *, uint32_t, uint32_t, void *))0) {
        return -1;
    }
    if (dev->read(dev, 0u, 1u, sector) != 0) {
        return -1;
    }
    if (sector[MBR_SIGNATURE_OFFSET] != 0x55u || sector[MBR_SIGNATURE_OFFSET + 1u] != 0xAAu) {
        serial_print("[mbr] no valid MBR signature\n");
        return -1;
    }

    for (i = 0u; i < MBR_PARTITION_COUNT; i++) {
        const MbrPartitionEntry *entry =
            (const MbrPartitionEntry *)(const void *)(sector + MBR_PARTITION_OFFSET + (i * sizeof(MbrPartitionEntry)));

        if ((entry->type != MBR_TYPE_FAT32_CHS && entry->type != MBR_TYPE_FAT32_LBA)
            || entry->sector_count == 0u) {
            continue;
        }

        mbr_partition_data[i].parent = dev;
        mbr_partition_data[i].start_lba = entry->start_lba;
        mbr_partition_data[i].sector_count = entry->sector_count;

        mbr_partition_devices[i].name[0] = dev->name[0];
        mbr_partition_devices[i].name[1] = dev->name[1];
        mbr_partition_devices[i].name[2] = dev->name[2];
        mbr_partition_devices[i].name[3] = 'p';
        mbr_partition_devices[i].name[4] = (char)('1' + i);
        mbr_partition_devices[i].name[5] = '\0';
        mbr_partition_devices[i].block_count = entry->sector_count;
        mbr_partition_devices[i].block_size = BLOCK_SIZE;
        mbr_partition_devices[i].read = mbr_partition_read;
        mbr_partition_devices[i].write = mbr_partition_write;
        mbr_partition_devices[i].private_data = &mbr_partition_data[i];
        mbr_partition_devices[i].present = 1;

        if (blkdev_register(&mbr_partition_devices[i]) >= 0) {
            serial_print("[mbr] registered ");
            serial_print(mbr_partition_devices[i].name);
            serial_print(" start=");
            mbr_write_u32(entry->start_lba);
            serial_print(" sectors=");
            mbr_write_u32(entry->sector_count);
            serial_print("\n");
        }
    }

    return 0;
}
