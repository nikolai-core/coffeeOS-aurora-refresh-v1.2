#include <stdint.h>

#include "blkdev.h"
#include "fat32.h"
#include "fat32_format.h"
#include "serial.h"

/* Copy a raw byte range without assuming libc is available. */
static void fat32_format_memcpy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

/* Fill a raw byte range with zeroes. */
static void fat32_format_memzero(void *dst, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = 0u;
    }
}

/* Pad a FAT volume label to exactly 11 bytes with trailing spaces. */
static void fat32_format_label(uint8_t out[11], const char *label) {
    uint32_t i = 0u;

    while (i < 11u) {
        out[i] = ' ';
        i++;
    }
    if (label == (const char *)0) {
        return;
    }
    for (i = 0u; i < 11u && label[i] != '\0'; i++) {
        char c = label[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        out[i] = (uint8_t)c;
    }
}

/* Split an 11-byte FAT volume label into 8.3 directory-entry fields. */
static void fat32_format_label_entry(const uint8_t label[11], Fat32DirEntry *entry) {
    uint32_t i;

    for (i = 0u; i < 8u; i++) {
        entry->name[i] = label[i];
    }
    for (i = 0u; i < 3u; i++) {
        entry->ext[i] = label[8u + i];
    }
}

/* Format a blank block device as a minimal FAT32 volume. */
int fat32_format(BlockDevice *dev, const char *volume_label) {
    uint8_t sector[BLOCK_SIZE];
    uint8_t zero_sector[BLOCK_SIZE];
    Fat32BPB *bpb = (Fat32BPB *)sector;
    Fat32FSInfo *fsinfo = (Fat32FSInfo *)sector;
    Fat32DirEntry *dirent = (Fat32DirEntry *)sector;
    uint32_t fat_size_32;
    uint32_t reserved_sectors = 32u;
    uint32_t sectors_per_cluster = 8u;
    uint32_t cluster_estimate;
    uint32_t fat1_start;
    uint32_t fat2_start;
    uint32_t data_start;
    uint32_t root_dir_lba;
    uint32_t i;
    uint8_t padded_label[11];

    if (dev == (BlockDevice *)0 || dev->write == (int (*)(struct BlockDevice *, uint32_t, uint32_t, const void *))0
        || dev->block_size != BLOCK_SIZE) {
        return -1;
    }

    fat32_format_memzero(zero_sector, sizeof(zero_sector));
    fat32_format_label(padded_label, volume_label);

    cluster_estimate = dev->block_count / sectors_per_cluster;
    fat_size_32 = ((cluster_estimate * 4u) + (BLOCK_SIZE - 1u)) / BLOCK_SIZE;
    fat1_start = reserved_sectors;
    fat2_start = fat1_start + fat_size_32;
    data_start = reserved_sectors + (2u * fat_size_32);
    root_dir_lba = data_start;

    serial_print("[fat32_format] writing BPB\n");
    fat32_format_memzero(sector, sizeof(sector));
    bpb->jump_boot[0] = 0xEBu;
    bpb->jump_boot[1] = 0x58u;
    bpb->jump_boot[2] = 0x90u;
    fat32_format_memcpy(bpb->oem_name, "COFFEEOS", 8u);
    bpb->bytes_per_sector = BLOCK_SIZE;
    bpb->sectors_per_cluster = (uint8_t)sectors_per_cluster;
    bpb->reserved_sectors = (uint16_t)reserved_sectors;
    bpb->num_fats = 2u;
    bpb->root_entry_count = 0u;
    bpb->total_sectors_16 = 0u;
    bpb->media_type = 0xF8u;
    bpb->fat_size_16 = 0u;
    bpb->sectors_per_track = 1u;
    bpb->num_heads = 1u;
    bpb->hidden_sectors = 0u;
    bpb->total_sectors_32 = dev->block_count;
    bpb->fat_size_32 = fat_size_32;
    bpb->ext_flags = 0u;
    bpb->fs_version = 0u;
    bpb->root_cluster = 2u;
    bpb->fs_info_sector = 1u;
    bpb->backup_boot_sector = 6u;
    bpb->drive_number = 0x80u;
    bpb->boot_signature = 0x29u;
    bpb->volume_id = 0xC0FFEE32u;
    fat32_format_memcpy(bpb->volume_label, padded_label, 11u);
    fat32_format_memcpy(bpb->fs_type, "FAT32   ", 8u);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    if (dev->write(dev, 0u, 1u, sector) != 0) {
        return -1;
    }

    serial_print("[fat32_format] writing FSInfo\n");
    fat32_format_memzero(sector, sizeof(sector));
    fsinfo->lead_sig = 0x41615252u;
    fsinfo->struct_sig = 0x61417272u;
    fsinfo->free_count = cluster_estimate > 2u ? cluster_estimate - 2u : 0u;
    fsinfo->next_free = 3u;
    fsinfo->trail_sig = 0xAA550000u;
    if (dev->write(dev, 1u, 1u, sector) != 0) {
        return -1;
    }

    serial_print("[fat32_format] writing backup boot sector\n");
    fat32_format_memzero(sector, sizeof(sector));
    bpb = (Fat32BPB *)sector;
    bpb->jump_boot[0] = 0xEBu;
    bpb->jump_boot[1] = 0x58u;
    bpb->jump_boot[2] = 0x90u;
    fat32_format_memcpy(bpb->oem_name, "COFFEEOS", 8u);
    bpb->bytes_per_sector = BLOCK_SIZE;
    bpb->sectors_per_cluster = (uint8_t)sectors_per_cluster;
    bpb->reserved_sectors = (uint16_t)reserved_sectors;
    bpb->num_fats = 2u;
    bpb->media_type = 0xF8u;
    bpb->total_sectors_32 = dev->block_count;
    bpb->fat_size_32 = fat_size_32;
    bpb->root_cluster = 2u;
    bpb->fs_info_sector = 1u;
    bpb->backup_boot_sector = 6u;
    bpb->boot_signature = 0x29u;
    bpb->volume_id = 0xC0FFEE32u;
    fat32_format_memcpy(bpb->volume_label, padded_label, 11u);
    fat32_format_memcpy(bpb->fs_type, "FAT32   ", 8u);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
    if (dev->write(dev, 6u, 1u, sector) != 0) {
        return -1;
    }

    fat32_format_memzero(sector, sizeof(sector));
    fsinfo = (Fat32FSInfo *)sector;
    fsinfo->lead_sig = 0x41615252u;
    fsinfo->struct_sig = 0x61417272u;
    fsinfo->free_count = cluster_estimate > 2u ? cluster_estimate - 2u : 0u;
    fsinfo->next_free = 3u;
    fsinfo->trail_sig = 0xAA550000u;
    if (dev->write(dev, 7u, 1u, sector) != 0) {
        return -1;
    }

    serial_print("[fat32_format] writing FAT1\n");
    for (i = 0u; i < fat_size_32; i++) {
        if (dev->write(dev, fat1_start + i, 1u, zero_sector) != 0) {
            return -1;
        }
    }
    fat32_format_memzero(sector, sizeof(sector));
    ((uint32_t *)sector)[0] = 0x0FFFFFF8u;
    ((uint32_t *)sector)[1] = 0x0FFFFFFFu;
    ((uint32_t *)sector)[2] = 0x0FFFFFFFu;
    if (dev->write(dev, fat1_start, 1u, sector) != 0) {
        return -1;
    }

    serial_print("[fat32_format] writing FAT2\n");
    for (i = 0u; i < fat_size_32; i++) {
        if (dev->write(dev, fat2_start + i, 1u, zero_sector) != 0) {
            return -1;
        }
    }
    if (dev->write(dev, fat2_start, 1u, sector) != 0) {
        return -1;
    }

    serial_print("[fat32_format] writing root directory\n");
    for (i = 0u; i < sectors_per_cluster; i++) {
        if (dev->write(dev, root_dir_lba + i, 1u, zero_sector) != 0) {
            return -1;
        }
    }

    fat32_format_memzero(sector, sizeof(sector));
    dirent = (Fat32DirEntry *)sector;
    fat32_format_label_entry(padded_label, dirent);
    dirent->attributes = FAT_ATTR_VOLUME_ID;
    if (dev->write(dev, root_dir_lba, 1u, sector) != 0) {
        return -1;
    }

    return 0;
}
