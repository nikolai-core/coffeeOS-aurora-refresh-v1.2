#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

#include "blkdev.h"

typedef struct __attribute__((packed)) Fat32BPB {
    uint8_t jump_boot[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} Fat32BPB;

typedef struct __attribute__((packed)) Fat32FSInfo {
    uint32_t lead_sig;
    uint8_t reserved1[480];
    uint32_t struct_sig;
    uint32_t free_count;
    uint32_t next_free;
    uint8_t reserved2[12];
    uint32_t trail_sig;
} Fat32FSInfo;

typedef struct __attribute__((packed)) Fat32DirEntry {
    uint8_t name[8];
    uint8_t ext[3];
    uint8_t attributes;
    uint8_t nt_reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;
    uint32_t file_size;
} Fat32DirEntry;

typedef struct __attribute__((packed)) Fat32LFNEntry {
    uint8_t order;
    uint16_t name1[5];
    uint8_t attribute;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t cluster_low;
    uint16_t name3[2];
} Fat32LFNEntry;

#define FAT32_EOC 0x0FFFFFF8u
#define FAT32_BAD 0x0FFFFFF7u
#define FAT32_FREE 0x00000000u
#define FAT32_MASK 0x0FFFFFFFu

#define FAT_ATTR_READ_ONLY 0x01u
#define FAT_ATTR_HIDDEN 0x02u
#define FAT_ATTR_SYSTEM 0x04u
#define FAT_ATTR_VOLUME_ID 0x08u
#define FAT_ATTR_DIRECTORY 0x10u
#define FAT_ATTR_ARCHIVE 0x20u
#define FAT_ATTR_LFN 0x0Fu

#define FAT_ENTRY_DELETED 0xE5u
#define FAT_ENTRY_END 0x00u

#define FAT32_MAX_VOLUMES 2u
#define FAT32_CACHE_BLOCKS 32u
#define FAT32_NAME_MAX 256u
#define FAT32_PATH_MAX 512u

typedef struct Fat32Volume {
    BlockDevice *dev;
    Fat32BPB bpb;
    uint32_t fat_start_lba;
    uint32_t fat2_start_lba;
    uint32_t data_start_lba;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_cluster;
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint32_t next_free;
    int mounted;
    char label[12];
} Fat32Volume;

int fat32_mount(int vol_idx, BlockDevice *dev);
int fat32_unmount(int vol_idx);
Fat32Volume *fat32_get_volume(int vol_idx);
int fat32_fsck(int vol_idx);
int fat32_sync(int vol_idx);

uint32_t fat32_cluster_to_lba(Fat32Volume *vol, uint32_t cluster);
uint32_t fat32_entry_cluster(const Fat32DirEntry *entry);
void fat32_set_entry_cluster(Fat32DirEntry *entry, uint32_t cluster);
int fat32_read_cluster(Fat32Volume *vol, uint32_t cluster, void *buf);
int fat32_write_cluster(Fat32Volume *vol, uint32_t cluster, const void *buf);
int fat32_read_chain(Fat32Volume *vol, uint32_t start_cluster, uint32_t byte_offset,
                     void *buf, uint32_t len);
int fat32_write_chain(Fat32Volume *vol, uint32_t *start_cluster, uint32_t byte_offset,
                      const void *buf, uint32_t len, uint32_t *new_end_cluster);
uint32_t fat32_alloc_cluster(Fat32Volume *vol);
int fat32_free_chain(Fat32Volume *vol, uint32_t cluster);
int fat32_dir_find(Fat32Volume *vol, uint32_t dir_cluster, const char *name,
                   Fat32DirEntry *out_entry, uint32_t *out_lba, uint32_t *out_offset);
int fat32_dir_list(Fat32Volume *vol, uint32_t dir_cluster, Fat32DirEntry *entries, int max_entries);
int fat32_dir_create_entry(Fat32Volume *vol, uint32_t dir_cluster, const Fat32DirEntry *entry,
                           const char *lfn, uint32_t *out_lba, uint32_t *out_offset);
int fat32_dir_delete_entry(Fat32Volume *vol, uint32_t entry_lba, uint32_t entry_offset,
                           uint32_t file_cluster);
int fat32_dir_mark_deleted(Fat32Volume *vol, uint32_t entry_lba, uint32_t entry_offset);
int fat32_update_dir_entry(Fat32Volume *vol, uint32_t lba, uint32_t offset,
                           const Fat32DirEntry *entry);
int fat32_write_barrier(Fat32Volume *vol);
int fat32_cache_flush(Fat32Volume *vol);

#endif
