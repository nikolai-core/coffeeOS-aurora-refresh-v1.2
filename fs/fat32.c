#include <stdint.h>

#include "ascii_util.h"
#include "fat32.h"
#include "serial.h"

#define DEBUG_FAT32 0
#define FAT32_CLUSTER_SCRATCH_SIZE (BLOCK_SIZE * 8u)
#define FAT32_MAX_LFN_SLOTS 20u

typedef struct SectorCacheEntry {
    uint32_t lba;
    int dirty;
    int valid;
    uint8_t data[BLOCK_SIZE];
} SectorCacheEntry;

static Fat32Volume fat32_volumes[FAT32_MAX_VOLUMES];
static SectorCacheEntry sector_cache[FAT32_CACHE_BLOCKS];
static BlockDevice *sector_cache_owner[FAT32_CACHE_BLOCKS];
static uint8_t fat32_cluster_scratch[FAT32_CLUSTER_SCRATCH_SIZE];

/* Return non-zero when one FAT value marks end-of-chain. */
static int fat32_is_eoc(uint32_t value) {
    return value >= FAT32_EOC;
}

/* Emit one periodic trace during guarded FAT chain walks. */
static void fat32_trace_chain_walk(uint32_t safety, uint32_t cluster) {
    if ((safety % 1000u) == 0u && safety > 0u) {
        serial_print("[fat32] chain walk iteration ");
        serial_write_hex(safety);
        serial_print(" cluster=");
        serial_write_hex(cluster);
        serial_print("\n");
    }
}

/* Copy a raw byte range without libc. */
static void fat32_memcpy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

/* Fill a raw byte range with one byte value. */
static void fat32_memset(void *dst, uint8_t value, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = value;
    }
}

/* Compare two raw byte ranges exactly. */
static int fat32_memcmp(const void *a, const void *b, uint32_t len) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        if (pa[i] != pb[i]) {
            return (int)pa[i] - (int)pb[i];
        }
    }
    return 0;
}

/* Return the length of one ASCII string up to a fixed maximum. */
static uint32_t fat32_strnlen(const char *s, uint32_t max_len) {
    uint32_t len = 0u;

    while (len < max_len && s[len] != '\0') {
        len++;
    }
    return len;
}

/* Return non-zero when two ASCII strings match case-insensitively. */
static int fat32_name_eq_ci(const char *a, const char *b) {
    uint32_t i = 0u;

    while (a[i] != '\0' && b[i] != '\0') {
        if (ascii_tolower(a[i]) != ascii_tolower(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

/* Copy one short ASCII string into a fixed FAT32 label buffer. */
static void fat32_copy_label(char *dst, uint32_t dst_len, const uint8_t *src, uint32_t src_len) {
    uint32_t out = 0u;

    while (out + 1u < dst_len && out < src_len) {
        dst[out] = (char)src[out];
        out++;
    }
    while (out > 0u && dst[out - 1u] == ' ') {
        out--;
    }
    dst[out] = '\0';
}

/* Return the mounted volume index for one volume pointer, or -1. */
static int fat32_volume_index(Fat32Volume *vol) {
    uint32_t i;

    for (i = 0u; i < FAT32_MAX_VOLUMES; i++) {
        if (&fat32_volumes[i] == vol) {
            return (int)i;
        }
    }
    return -1;
}

/* Emit one debug log line when FAT32 tracing is enabled. */
static void fat32_debug(const char *msg) {
#if DEBUG_FAT32
    serial_print(msg);
#else
    (void)msg;
#endif
}

/* Flush one dirty direct-mapped cache slot back to its owning device. */
static int cache_flush_slot(uint32_t slot) {
    if (slot >= FAT32_CACHE_BLOCKS || !sector_cache[slot].valid || !sector_cache[slot].dirty
        || sector_cache_owner[slot] == (BlockDevice *)0) {
        return 0;
    }

    if (sector_cache_owner[slot]->write(sector_cache_owner[slot], sector_cache[slot].lba, 1u,
                                        sector_cache[slot].data) != 0) {
        return -1;
    }

    sector_cache[slot].dirty = 0;
    return 0;
}

/* Read one sector through the small direct-mapped FAT32 cache. */
static int cache_read(Fat32Volume *vol, uint32_t lba, void *buf) {
    uint32_t slot;

    if (vol == (Fat32Volume *)0 || vol->dev == (BlockDevice *)0 || buf == (void *)0) {
        return -1;
    }

    slot = lba % FAT32_CACHE_BLOCKS;
    if (!sector_cache[slot].valid || sector_cache[slot].lba != lba || sector_cache_owner[slot] != vol->dev) {
        if (cache_flush_slot(slot) != 0) {
            return -1;
        }
        if (vol->dev->read(vol->dev, lba, 1u, sector_cache[slot].data) != 0) {
            return -1;
        }
        sector_cache[slot].lba = lba;
        sector_cache[slot].dirty = 0;
        sector_cache[slot].valid = 1;
        sector_cache_owner[slot] = vol->dev;
    }

    fat32_memcpy(buf, sector_cache[slot].data, BLOCK_SIZE);
    return 0;
}

/* Write one sector into the direct-mapped FAT32 cache without immediate write-through. */
static int cache_write(Fat32Volume *vol, uint32_t lba, const void *buf) {
    uint32_t slot;

    if (vol == (Fat32Volume *)0 || vol->dev == (BlockDevice *)0 || buf == (const void *)0) {
        return -1;
    }

    slot = lba % FAT32_CACHE_BLOCKS;
    if (!sector_cache[slot].valid || sector_cache[slot].lba != lba || sector_cache_owner[slot] != vol->dev) {
        if (cache_flush_slot(slot) != 0) {
            return -1;
        }
        sector_cache[slot].lba = lba;
        sector_cache[slot].valid = 1;
        sector_cache_owner[slot] = vol->dev;
    }

    fat32_memcpy(sector_cache[slot].data, buf, BLOCK_SIZE);
    sector_cache[slot].dirty = 1;
    return 0;
}

/* Flush all dirty cached sectors owned by one mounted FAT32 volume. */
int fat32_cache_flush(Fat32Volume *vol) {
    uint32_t i;

    if (vol == (Fat32Volume *)0 || vol->dev == (BlockDevice *)0) {
        return -1;
    }

    for (i = 0u; i < FAT32_CACHE_BLOCKS; i++) {
        if (sector_cache_owner[i] == vol->dev && cache_flush_slot(i) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Mark one cached LBA invalid after direct device writes bypass the cache. */
static void cache_invalidate_lba(Fat32Volume *vol, uint32_t lba) {
    uint32_t slot = lba % FAT32_CACHE_BLOCKS;

    if (slot < FAT32_CACHE_BLOCKS && sector_cache[slot].valid && sector_cache[slot].lba == lba
        && sector_cache_owner[slot] == vol->dev) {
        sector_cache[slot].valid = 0;
        sector_cache[slot].dirty = 0;
        sector_cache_owner[slot] = (BlockDevice *)0;
    }
}

/* Return the cluster number encoded in one on-disk directory entry. */
uint32_t fat32_entry_cluster(const Fat32DirEntry *entry) {
    if (entry == (const Fat32DirEntry *)0) {
        return 0u;
    }
    return ((uint32_t)entry->cluster_high << 16) | (uint32_t)entry->cluster_low;
}

/* Store one cluster number into the split high/low fields of a directory entry. */
void fat32_set_entry_cluster(Fat32DirEntry *entry, uint32_t cluster) {
    if (entry == (Fat32DirEntry *)0) {
        return;
    }
    entry->cluster_high = (uint16_t)((cluster >> 16) & 0xFFFFu);
    entry->cluster_low = (uint16_t)(cluster & 0xFFFFu);
}

/* Read one FAT entry and return the next cluster value with reserved bits masked off. */
static uint32_t fat32_read_fat(Fat32Volume *vol, uint32_t cluster) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];
    uint32_t fat_offset;
    uint32_t lba;
    uint32_t offset;
    uint32_t value;

    if (vol == (Fat32Volume *)0 || cluster < 2u || cluster > vol->total_clusters + 1u) {
        return FAT32_BAD;
    }

    fat_offset = cluster * 4u;
    lba = vol->fat_start_lba + (fat_offset / BLOCK_SIZE);
    offset = fat_offset % BLOCK_SIZE;
    if (cache_read(vol, lba, sector) != 0) {
        return FAT32_BAD;
    }

    value = ((uint32_t)sector[offset + 0u])
        | ((uint32_t)sector[offset + 1u] << 8)
        | ((uint32_t)sector[offset + 2u] << 16)
        | ((uint32_t)sector[offset + 3u] << 24);
    return value & FAT32_MASK;
}

/* Write one FAT entry to both FAT copies while preserving each entry's top reserved nibble. */
static int fat32_write_fat(Fat32Volume *vol, uint32_t cluster, uint32_t value) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];
    uint32_t fat_offset;
    uint32_t lba1;
    uint32_t lba2;
    uint32_t offset;
    uint32_t old_value;
    uint32_t new_value;

    if (vol == (Fat32Volume *)0 || cluster < 2u || cluster > vol->total_clusters + 1u) {
        return -1;
    }

    fat_offset = cluster * 4u;
    lba1 = vol->fat_start_lba + (fat_offset / BLOCK_SIZE);
    lba2 = vol->fat2_start_lba + (fat_offset / BLOCK_SIZE);
    offset = fat_offset % BLOCK_SIZE;

    if (cache_read(vol, lba1, sector) != 0) {
        return -1;
    }
    old_value = ((uint32_t)sector[offset + 0u])
        | ((uint32_t)sector[offset + 1u] << 8)
        | ((uint32_t)sector[offset + 2u] << 16)
        | ((uint32_t)sector[offset + 3u] << 24);
    new_value = (old_value & 0xF0000000u) | (value & FAT32_MASK);
    sector[offset + 0u] = (uint8_t)(new_value & 0xFFu);
    sector[offset + 1u] = (uint8_t)((new_value >> 8) & 0xFFu);
    sector[offset + 2u] = (uint8_t)((new_value >> 16) & 0xFFu);
    sector[offset + 3u] = (uint8_t)((new_value >> 24) & 0xFFu);
    if (cache_write(vol, lba1, sector) != 0) {
        return -1;
    }

    if (cache_read(vol, lba2, sector) != 0) {
        return -1;
    }
    old_value = ((uint32_t)sector[offset + 0u])
        | ((uint32_t)sector[offset + 1u] << 8)
        | ((uint32_t)sector[offset + 2u] << 16)
        | ((uint32_t)sector[offset + 3u] << 24);
    new_value = (old_value & 0xF0000000u) | (value & FAT32_MASK);
    sector[offset + 0u] = (uint8_t)(new_value & 0xFFu);
    sector[offset + 1u] = (uint8_t)((new_value >> 8) & 0xFFu);
    sector[offset + 2u] = (uint8_t)((new_value >> 16) & 0xFFu);
    sector[offset + 3u] = (uint8_t)((new_value >> 24) & 0xFFu);
    if (cache_write(vol, lba2, sector) != 0) {
        return -1;
    }

    return 0;
}

/* Update the in-memory FSInfo fields and both on-disk FSInfo sectors. */
static int fat32_update_fsinfo(Fat32Volume *vol) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];
    Fat32FSInfo *fsinfo = (Fat32FSInfo *)sector;

    if (vol == (Fat32Volume *)0 || vol->dev == (BlockDevice *)0) {
        return -1;
    }

    if (vol->dev->read(vol->dev, vol->bpb.fs_info_sector, 1u, sector) != 0) {
        return -1;
    }
    if (fsinfo->lead_sig != 0x41615252u || fsinfo->struct_sig != 0x61417272u) {
        fat32_memset(sector, 0u, sizeof(sector));
        fsinfo->lead_sig = 0x41615252u;
        fsinfo->struct_sig = 0x61417272u;
        fsinfo->trail_sig = 0xAA550000u;
    }
    fsinfo->free_count = vol->free_clusters;
    fsinfo->next_free = vol->next_free;
    if (vol->dev->write(vol->dev, vol->bpb.fs_info_sector, 1u, sector) != 0) {
        return -1;
    }
    cache_invalidate_lba(vol, vol->bpb.fs_info_sector);

    if (vol->dev->write(vol->dev, vol->bpb.backup_boot_sector + 1u, 1u, sector) != 0) {
        return -1;
    }
    cache_invalidate_lba(vol, vol->bpb.backup_boot_sector + 1u);
    return 0;
}

/* Flush cached writes and then persist FAT/FSInfo ordering for crash tolerance. */
int fat32_write_barrier(Fat32Volume *vol) {
    if (fat32_cache_flush(vol) != 0) {
        return -1;
    }
    return fat32_update_fsinfo(vol);
}

/* Allocate one free cluster and mark it as a single-cluster EOC chain. */
uint32_t fat32_alloc_cluster(Fat32Volume *vol) {
    uint32_t cluster;
    uint32_t searched = 0u;
    uint32_t last_cluster;

    if (vol == (Fat32Volume *)0 || vol->total_clusters == 0u) {
        return FAT32_BAD;
    }

    if (vol->next_free < 2u || vol->next_free > vol->total_clusters + 1u) {
        vol->next_free = 2u;
    }

    cluster = vol->next_free;
    last_cluster = vol->total_clusters + 1u;
    while (searched < vol->total_clusters) {
        if (fat32_read_fat(vol, cluster) == FAT32_FREE) {
            if (fat32_write_fat(vol, cluster, FAT32_EOC) != 0) {
                return FAT32_BAD;
            }
            if (vol->free_clusters != 0u) {
                vol->free_clusters--;
            }
            vol->next_free = (cluster == last_cluster) ? 2u : (cluster + 1u);
            if (fat32_write_barrier(vol) != 0) {
                return FAT32_BAD;
            }
            fat32_debug("[fat32] alloc cluster\n");
            return cluster;
        }

        cluster = (cluster == last_cluster) ? 2u : (cluster + 1u);
        searched++;
    }

    return FAT32_BAD;
}

/* Free an entire FAT chain and return it to the free-cluster pool. */
int fat32_free_chain(Fat32Volume *vol, uint32_t cluster) {
    uint32_t current = cluster;
    uint32_t freed = 0u;
    uint32_t safety = 0u;

    if (vol == (Fat32Volume *)0 || current < 2u) {
        return 0;
    }

    while (current >= 2u && current <= vol->total_clusters + 1u) {
        uint32_t next = fat32_read_fat(vol, current);

        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return -1;
        }
        fat32_trace_chain_walk(safety, current);

        if (fat32_write_fat(vol, current, FAT32_FREE) != 0) {
            return -1;
        }
        freed++;
        if (fat32_is_eoc(next) || next == FAT32_BAD || next == FAT32_FREE) {
            break;
        }
        current = next;
    }

    vol->free_clusters += freed;
    if (cluster < vol->next_free || vol->next_free < 2u) {
        vol->next_free = cluster;
    }
    return fat32_write_barrier(vol);
}

/* Count how many clusters belong to one chain from its first cluster to EOC. */
static uint32_t fat32_chain_length(Fat32Volume *vol, uint32_t cluster) {
    uint32_t length = 0u;
    uint32_t current = cluster;
    uint32_t safety = 0u;

    while (current >= 2u && current <= vol->total_clusters + 1u) {
        uint32_t next = fat32_read_fat(vol, current);

        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return FAT32_BAD;
        }
        fat32_trace_chain_walk(safety, current);

        length++;
        if (fat32_is_eoc(next) || next == FAT32_BAD || next == FAT32_FREE) {
            break;
        }
        current = next;
    }
    return length;
}

/* Return the nth cluster in a chain, or FAT32_BAD when the chain is too short. */
static uint32_t fat32_get_nth_cluster(Fat32Volume *vol, uint32_t start_cluster, uint32_t n) {
    uint32_t current = start_cluster;
    uint32_t i;
    uint32_t safety = 0u;

    if (current < 2u) {
        return FAT32_BAD;
    }

    for (i = 0u; i < n; i++) {
        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return FAT32_BAD;
        }
        fat32_trace_chain_walk(safety, current);
        current = fat32_read_fat(vol, current);
        if (fat32_is_eoc(current) || current == FAT32_BAD || current == FAT32_FREE) {
            return FAT32_BAD;
        }
    }
    return current;
}

/* Convert one data-cluster number into the first device LBA that backs it. */
uint32_t fat32_cluster_to_lba(Fat32Volume *vol, uint32_t cluster) {
    return vol->data_start_lba + ((cluster - 2u) * vol->sectors_per_cluster);
}

/* Read one full cluster through the sector cache into a caller-provided buffer. */
int fat32_read_cluster(Fat32Volume *vol, uint32_t cluster, void *buf) {
    uint32_t lba;
    uint32_t i;

    if (vol == (Fat32Volume *)0 || buf == (void *)0 || cluster < 2u
        || cluster > vol->total_clusters + 1u || vol->bytes_per_cluster > FAT32_CLUSTER_SCRATCH_SIZE) {
        return -1;
    }

    lba = fat32_cluster_to_lba(vol, cluster);
    for (i = 0u; i < vol->sectors_per_cluster; i++) {
        if (cache_read(vol, lba + i, (uint8_t *)buf + (i * BLOCK_SIZE)) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Write one full cluster into the cache from a caller-provided buffer. */
int fat32_write_cluster(Fat32Volume *vol, uint32_t cluster, const void *buf) {
    uint32_t lba;
    uint32_t i;

    if (vol == (Fat32Volume *)0 || buf == (const void *)0 || cluster < 2u
        || cluster > vol->total_clusters + 1u || vol->bytes_per_cluster > FAT32_CLUSTER_SCRATCH_SIZE) {
        return -1;
    }

    lba = fat32_cluster_to_lba(vol, cluster);
    for (i = 0u; i < vol->sectors_per_cluster; i++) {
        if (cache_write(vol, lba + i, (const uint8_t *)buf + (i * BLOCK_SIZE)) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Read bytes from a cluster chain starting at one byte offset into the file. */
int fat32_read_chain(Fat32Volume *vol, uint32_t start_cluster, uint32_t byte_offset,
                     void *buf, uint32_t len) {
    uint8_t *out = (uint8_t *)buf;
    uint32_t done = 0u;
    uint32_t safety = 0u;

    if (len == 0u) {
        return 0;
    }
    if (vol == (Fat32Volume *)0 || buf == (void *)0 || start_cluster < 2u) {
        return -1;
    }

    while (done < len) {
        uint32_t absolute = byte_offset + done;
        uint32_t cluster_index = absolute / vol->bytes_per_cluster;
        uint32_t cluster_off = absolute % vol->bytes_per_cluster;
        uint32_t cluster = fat32_get_nth_cluster(vol, start_cluster, cluster_index);
        uint32_t chunk = vol->bytes_per_cluster - cluster_off;

        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return -1;
        }
        fat32_trace_chain_walk(safety, cluster);
        if (cluster == FAT32_BAD) {
            return (int)done;
        }
        if (chunk > len - done) {
            chunk = len - done;
        }
        if (fat32_read_cluster(vol, cluster, fat32_cluster_scratch) != 0) {
            return -1;
        }

        fat32_memcpy(out + done, fat32_cluster_scratch + cluster_off, chunk);
        done += chunk;
    }

    return (int)done;
}

/* Write bytes into a cluster chain, extending it when the caller writes past EOC. */
int fat32_write_chain(Fat32Volume *vol, uint32_t *start_cluster, uint32_t byte_offset,
                      const void *buf, uint32_t len, uint32_t *new_end_cluster) {
    const uint8_t *in = (const uint8_t *)buf;
    uint32_t done = 0u;
    uint32_t chain_start;
    uint32_t safety = 0u;

    if (vol == (Fat32Volume *)0 || start_cluster == (uint32_t *)0 || buf == (const void *)0) {
        return -1;
    }
    if (len == 0u) {
        if (new_end_cluster != (uint32_t *)0) {
            *new_end_cluster = *start_cluster;
        }
        return 0;
    }

    chain_start = *start_cluster;
    if (chain_start < 2u) {
        chain_start = fat32_alloc_cluster(vol);
        if (chain_start == FAT32_BAD) {
            return -1;
        }
        fat32_memset(fat32_cluster_scratch, 0u, vol->bytes_per_cluster);
        if (fat32_write_cluster(vol, chain_start, fat32_cluster_scratch) != 0 || fat32_cache_flush(vol) != 0) {
            return -1;
        }
        *start_cluster = chain_start;
    }

    while (done < len) {
        uint32_t absolute = byte_offset + done;
        uint32_t cluster_index = absolute / vol->bytes_per_cluster;
        uint32_t cluster_off = absolute % vol->bytes_per_cluster;
        uint32_t current = chain_start;
        uint32_t idx;
        uint32_t chunk;

        for (idx = 0u; idx < cluster_index; idx++) {
            uint32_t next = fat32_read_fat(vol, current);

            if (++safety > vol->total_clusters) {
                serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
                return -1;
            }
            fat32_trace_chain_walk(safety, current);

            if (fat32_is_eoc(next) || next == FAT32_FREE || next == FAT32_BAD) {
                uint32_t added = fat32_alloc_cluster(vol);

                if (added == FAT32_BAD) {
                    return -1;
                }
                fat32_memset(fat32_cluster_scratch, 0u, vol->bytes_per_cluster);
                if (fat32_write_cluster(vol, added, fat32_cluster_scratch) != 0) {
                    return -1;
                }
                if (fat32_write_fat(vol, current, added) != 0 || fat32_write_barrier(vol) != 0) {
                    return -1;
                }
                next = added;
            }
            current = next;
        }

        if (fat32_read_cluster(vol, current, fat32_cluster_scratch) != 0) {
            return -1;
        }
        chunk = vol->bytes_per_cluster - cluster_off;
        if (chunk > len - done) {
            chunk = len - done;
        }
        fat32_memcpy(fat32_cluster_scratch + cluster_off, in + done, chunk);
        if (fat32_write_cluster(vol, current, fat32_cluster_scratch) != 0) {
            return -1;
        }
        done += chunk;
    }

    if (new_end_cluster != (uint32_t *)0) {
        *new_end_cluster = chain_start;
    }
    return (int)done;
}

/* Convert one long filename candidate into an uppercase FAT 8.3 byte array. */
static void fat32_to_83(const char *lfn, uint8_t *name83) {
    char base[9];
    char ext[4];
    uint32_t i;
    uint32_t last_dot = 0xFFFFFFFFu;
    uint32_t len = fat32_strnlen(lfn, FAT32_NAME_MAX - 1u);
    uint32_t bi = 0u;
    uint32_t ei = 0u;

    for (i = 0u; i < 11u; i++) {
        name83[i] = ' ';
    }
    for (i = 0u; i < len; i++) {
        if (lfn[i] == '.') {
            last_dot = i;
        }
    }

    for (i = 0u; i < len; i++) {
        char c = lfn[i];
        int is_ext = (last_dot != 0xFFFFFFFFu && i > last_dot);

        if (c == '.') {
            continue;
        }
        if (c == '+' || c == ',' || c == ';' || c == '=' || c == '[' || c == ']') {
            c = '_';
        }
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }

        if (!is_ext) {
            if (bi < 8u) {
                base[bi++] = c;
            }
        } else if (ei < 3u) {
            ext[ei++] = c;
        }
    }

    for (i = 0u; i < bi; i++) {
        name83[i] = (uint8_t)base[i];
    }
    for (i = 0u; i < ei; i++) {
        name83[8u + i] = (uint8_t)ext[i];
    }
}

/* Convert one FAT 8.3 directory entry name into a printable dotted ASCII string. */
static void fat32_from_83(const uint8_t *name83, const uint8_t *ext83, char *out) {
    uint32_t oi = 0u;
    uint32_t i;

    for (i = 0u; i < 8u && name83[i] != ' '; i++) {
        out[oi++] = (char)name83[i];
    }
    if (ext83[0] != ' ') {
        out[oi++] = '.';
        for (i = 0u; i < 3u && ext83[i] != ' '; i++) {
            out[oi++] = (char)ext83[i];
        }
    }
    out[oi] = '\0';
}

/* Compute the FAT long-filename checksum for one 8.3 name. */
static uint8_t fat32_lfn_checksum(const uint8_t *name83) {
    uint8_t sum = 0u;
    uint32_t i;

    for (i = 0u; i < 11u; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + name83[i]);
    }
    return sum;
}

/* Decode one LFN entry chunk into a flat ASCII filename buffer. */
static void fat32_lfn_decode_chunk(const Fat32LFNEntry *lfn, char *name) {
    uint32_t base = ((lfn->order & 0x1Fu) - 1u) * 13u;
    uint32_t pos = base;
    uint32_t i;
    uint16_t name1[5];
    uint16_t name2[6];
    uint16_t name3[2];
    const uint16_t *parts[3];
    const uint32_t sizes[3] = {5u, 6u, 2u};
    uint32_t group;

    fat32_memcpy(name1, lfn->name1, sizeof(name1));
    fat32_memcpy(name2, lfn->name2, sizeof(name2));
    fat32_memcpy(name3, lfn->name3, sizeof(name3));
    parts[0] = name1;
    parts[1] = name2;
    parts[2] = name3;

    for (group = 0u; group < 3u; group++) {
        for (i = 0u; i < sizes[group]; i++) {
            uint16_t ch = parts[group][i];

            if (ch == 0x0000u) {
                name[pos] = '\0';
                return;
            }
            if (ch == 0xFFFFu) {
                continue;
            }
            if (pos + 1u < FAT32_NAME_MAX) {
                name[pos++] = (char)(ch & 0x00FFu);
                name[pos] = '\0';
            }
        }
    }
}

/* Scan one directory chain and return the matching entry location and metadata. */
int fat32_dir_find(Fat32Volume *vol, uint32_t dir_cluster, const char *name,
                   Fat32DirEntry *out_entry, uint32_t *out_lba, uint32_t *out_offset) {
    uint32_t cluster = dir_cluster;
    char lfn_name[FAT32_NAME_MAX];
    uint32_t safety = 0u;

    if (vol == (Fat32Volume *)0 || name == (const char *)0 || dir_cluster < 2u) {
        return 0;
    }

    lfn_name[0] = '\0';
    while (cluster >= 2u && cluster <= vol->total_clusters + 1u) {
        uint32_t base_lba = fat32_cluster_to_lba(vol, cluster);
        uint32_t sector_index;

        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return 0;
        }
        fat32_trace_chain_walk(safety, cluster);
        for (sector_index = 0u; sector_index < vol->sectors_per_cluster; sector_index++) {
            /* static to avoid stack overflow — not reentrant */
            static uint8_t sector[BLOCK_SIZE];
            uint32_t entry_index;

            if (cache_read(vol, base_lba + sector_index, sector) != 0) {
                return 0;
            }

            for (entry_index = 0u; entry_index < (BLOCK_SIZE / sizeof(Fat32DirEntry)); entry_index++) {
                Fat32DirEntry *entry = (Fat32DirEntry *)(void *)(sector + (entry_index * sizeof(Fat32DirEntry)));

                if (entry->name[0] == FAT_ENTRY_END) {
                    return 0;
                }
                if (entry->attributes == FAT_ATTR_LFN) {
                    Fat32LFNEntry *lfn = (Fat32LFNEntry *)(void *)entry;

                    if ((lfn->order & 0x40u) != 0u) {
                        lfn_name[0] = '\0';
                    }
                    fat32_lfn_decode_chunk(lfn, lfn_name);
                    continue;
                }
                if (entry->name[0] == FAT_ENTRY_DELETED) {
                    lfn_name[0] = '\0';
                    continue;
                }

                {
                    char short_name[16];

                    fat32_from_83(entry->name, entry->ext, short_name);
                    if ((lfn_name[0] != '\0' && fat32_name_eq_ci(lfn_name, name))
                        || fat32_name_eq_ci(short_name, name)) {
                        if (out_entry != (Fat32DirEntry *)0) {
                            *out_entry = *entry;
                        }
                        if (out_lba != (uint32_t *)0) {
                            *out_lba = base_lba + sector_index;
                        }
                        if (out_offset != (uint32_t *)0) {
                            *out_offset = entry_index * sizeof(Fat32DirEntry);
                        }
                        return 1;
                    }
                }
                lfn_name[0] = '\0';
            }
        }

        {
            uint32_t next = fat32_read_fat(vol, cluster);

            if (fat32_is_eoc(next) || next == FAT32_FREE || next == FAT32_BAD) {
                break;
            }
            cluster = next;
        }
    }

    return 0;
}

/* List short directory entries in one directory chain for callers that only need metadata. */
int fat32_dir_list(Fat32Volume *vol, uint32_t dir_cluster, Fat32DirEntry *entries, int max_entries) {
    uint32_t cluster = dir_cluster;
    int count = 0;
    uint32_t safety = 0u;

    if (vol == (Fat32Volume *)0 || entries == (Fat32DirEntry *)0 || max_entries <= 0 || dir_cluster < 2u) {
        return 0;
    }

    while (cluster >= 2u && cluster <= vol->total_clusters + 1u) {
        uint32_t base_lba = fat32_cluster_to_lba(vol, cluster);
        uint32_t sector_index;

        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return count;
        }
        fat32_trace_chain_walk(safety, cluster);
        for (sector_index = 0u; sector_index < vol->sectors_per_cluster; sector_index++) {
            /* static to avoid stack overflow — not reentrant */
            static uint8_t sector[BLOCK_SIZE];
            uint32_t entry_index;

            if (cache_read(vol, base_lba + sector_index, sector) != 0) {
                return count;
            }
            for (entry_index = 0u; entry_index < (BLOCK_SIZE / sizeof(Fat32DirEntry)); entry_index++) {
                Fat32DirEntry *entry = (Fat32DirEntry *)(void *)(sector + (entry_index * sizeof(Fat32DirEntry)));

                if (entry->name[0] == FAT_ENTRY_END) {
                    return count;
                }
                if (entry->name[0] == FAT_ENTRY_DELETED || entry->attributes == FAT_ATTR_LFN) {
                    continue;
                }
                if (count < max_entries) {
                    entries[count] = *entry;
                    count++;
                }
            }
        }

        {
            uint32_t next = fat32_read_fat(vol, cluster);

            if (fat32_is_eoc(next) || next == FAT32_FREE || next == FAT32_BAD) {
                break;
            }
            cluster = next;
        }
    }
    return count;
}

/* Find enough free directory slots, extending the directory chain when needed, then write one new entry. */
int fat32_dir_create_entry(Fat32Volume *vol, uint32_t dir_cluster, const Fat32DirEntry *entry,
                           const char *lfn, uint32_t *out_lba, uint32_t *out_offset) {
    uint32_t short_name[3];
    uint8_t name83[11];
    char short_text[16];
    int need_lfn = 0;
    uint32_t lfn_len;
    uint32_t lfn_slots = 0u;
    uint32_t required_slots;
    uint32_t cluster = dir_cluster;
    uint32_t safety = 0u;

    if (vol == (Fat32Volume *)0 || entry == (const Fat32DirEntry *)0 || dir_cluster < 2u) {
        return -1;
    }

    fat32_memcpy(name83, entry->name, 8u);
    fat32_memcpy(name83 + 8u, entry->ext, 3u);
    fat32_from_83(entry->name, entry->ext, short_text);
    if (lfn != (const char *)0 && lfn[0] != '\0' && !fat32_name_eq_ci(lfn, short_text)) {
        need_lfn = 1;
        lfn_len = fat32_strnlen(lfn, FAT32_NAME_MAX - 1u);
        lfn_slots = (lfn_len + 12u) / 13u;
        if (lfn_slots > FAT32_MAX_LFN_SLOTS) {
            return -1;
        }
    }
    required_slots = 1u + lfn_slots;

    while (1) {
        uint32_t base_lba = fat32_cluster_to_lba(vol, cluster);
        uint32_t run_lba = 0u;
        uint32_t run_offset = 0u;
        uint32_t run_count = 0u;
        uint32_t sector_index;

        if (++safety > vol->total_clusters) {
            serial_print("[fat32] chain walk exceeded total_clusters — circular chain?\n");
            return -1;
        }
        fat32_trace_chain_walk(safety, cluster);
        for (sector_index = 0u; sector_index < vol->sectors_per_cluster; sector_index++) {
            /* static to avoid stack overflow — not reentrant */
            static uint8_t sector[BLOCK_SIZE];
            uint32_t entry_index;

            if (cache_read(vol, base_lba + sector_index, sector) != 0) {
                return -1;
            }
            for (entry_index = 0u; entry_index < (BLOCK_SIZE / sizeof(Fat32DirEntry)); entry_index++) {
                Fat32DirEntry *scan = (Fat32DirEntry *)(void *)(sector + (entry_index * sizeof(Fat32DirEntry)));
                int free_slot = (scan->name[0] == FAT_ENTRY_END || scan->name[0] == FAT_ENTRY_DELETED);

                if (free_slot) {
                    if (run_count == 0u) {
                        run_lba = base_lba + sector_index;
                        run_offset = entry_index * sizeof(Fat32DirEntry);
                    }
                    run_count++;
                    if (run_count >= required_slots) {
                        uint32_t write_slot;
                        uint32_t checksum = fat32_lfn_checksum(name83);

                        for (write_slot = 0u; write_slot < required_slots; write_slot++) {
                            uint32_t slot_lba = run_lba + ((run_offset + (write_slot * sizeof(Fat32DirEntry))) / BLOCK_SIZE);
                            uint32_t slot_off = (run_offset + (write_slot * sizeof(Fat32DirEntry))) % BLOCK_SIZE;
                            /* static to avoid stack overflow — not reentrant */
                            static uint8_t write_sector[BLOCK_SIZE];

                            if (cache_read(vol, slot_lba, write_sector) != 0) {
                                return -1;
                            }
                            if (need_lfn && write_slot < lfn_slots) {
                                Fat32LFNEntry *lfn_entry = (Fat32LFNEntry *)(void *)(write_sector + slot_off);
                                uint32_t ordinal = lfn_slots - write_slot;
                                uint32_t order = ordinal;
                                uint32_t base = (ordinal - 1u) * 13u;
                                uint32_t i;
                                uint16_t chars[13];

                                fat32_memset(lfn_entry, 0xFFu, sizeof(Fat32LFNEntry));
                                if (write_slot == 0u) {
                                    order |= 0x40u;
                                }
                                lfn_entry->order = (uint8_t)order;
                                lfn_entry->attribute = FAT_ATTR_LFN;
                                lfn_entry->type = 0u;
                                lfn_entry->checksum = (uint8_t)checksum;
                                lfn_entry->cluster_low = 0u;

                                for (i = 0u; i < 13u; i++) {
                                    uint32_t pos = base + i;
                                    if (lfn[pos] == '\0') {
                                        chars[i] = 0x0000u;
                                        while (++i < 13u) {
                                            chars[i] = 0xFFFFu;
                                        }
                                        break;
                                    }
                                    chars[i] = (uint16_t)(uint8_t)lfn[pos];
                                }
                                for (i = 0u; i < 5u; i++) {
                                    lfn_entry->name1[i] = chars[i];
                                }
                                for (i = 0u; i < 6u; i++) {
                                    lfn_entry->name2[i] = chars[5u + i];
                                }
                                for (i = 0u; i < 2u; i++) {
                                    lfn_entry->name3[i] = chars[11u + i];
                                }
                            } else {
                                Fat32DirEntry *dst = (Fat32DirEntry *)(void *)(write_sector + slot_off);
                                *dst = *entry;
                            }
                            if (cache_write(vol, slot_lba, write_sector) != 0) {
                                return -1;
                            }
                        }
                        if (fat32_cache_flush(vol) != 0) {
                            return -1;
                        }
                        if (out_lba != (uint32_t *)0) {
                            *out_lba = run_lba + ((run_offset + (lfn_slots * sizeof(Fat32DirEntry))) / BLOCK_SIZE);
                        }
                        if (out_offset != (uint32_t *)0) {
                            *out_offset = (run_offset + (lfn_slots * sizeof(Fat32DirEntry))) % BLOCK_SIZE;
                        }
                        return 0;
                    }
                } else {
                    run_count = 0u;
                }
            }
        }

        {
            uint32_t next = fat32_read_fat(vol, cluster);

            if (fat32_is_eoc(next) || next == FAT32_FREE || next == FAT32_BAD) {
                uint32_t added = fat32_alloc_cluster(vol);

                if (added == FAT32_BAD) {
                    return -1;
                }
                fat32_memset(fat32_cluster_scratch, 0u, vol->bytes_per_cluster);
                if (fat32_write_cluster(vol, added, fat32_cluster_scratch) != 0) {
                    return -1;
                }
                if (fat32_write_fat(vol, cluster, added) != 0 || fat32_write_barrier(vol) != 0) {
                    return -1;
                }
                cluster = added;
            } else {
                cluster = next;
            }
        }
    }
}

/* Mark one directory entry deleted and reclaim its cluster chain if it had one. */
int fat32_dir_delete_entry(Fat32Volume *vol, uint32_t entry_lba, uint32_t entry_offset,
                           uint32_t file_cluster) {
    if (file_cluster >= 2u && fat32_free_chain(vol, file_cluster) != 0) {
        return -1;
    }
    return fat32_dir_mark_deleted(vol, entry_lba, entry_offset);
}

/* Mark one directory entry deleted in place without freeing clusters. */
int fat32_dir_mark_deleted(Fat32Volume *vol, uint32_t entry_lba, uint32_t entry_offset) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];

    if (vol == (Fat32Volume *)0 || entry_offset >= BLOCK_SIZE) {
        return -1;
    }
    if (cache_read(vol, entry_lba, sector) != 0) {
        return -1;
    }
    sector[entry_offset] = FAT_ENTRY_DELETED;
    if (cache_write(vol, entry_lba, sector) != 0) {
        return -1;
    }
    return fat32_cache_flush(vol);
}

/* Update one directory entry in place after metadata changes such as size updates. */
int fat32_update_dir_entry(Fat32Volume *vol, uint32_t lba, uint32_t offset, const Fat32DirEntry *entry) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];

    if (vol == (Fat32Volume *)0 || entry == (const Fat32DirEntry *)0 || offset + sizeof(Fat32DirEntry) > BLOCK_SIZE) {
        return -1;
    }
    if (cache_read(vol, lba, sector) != 0) {
        return -1;
    }
    fat32_memcpy(sector + offset, entry, sizeof(Fat32DirEntry));
    return cache_write(vol, lba, sector);
}

/* Mount one FAT32 block device and cache its BPB and placement geometry. */
int fat32_mount(int vol_idx, BlockDevice *dev) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];
    Fat32Volume *vol;
    Fat32FSInfo *fsinfo;
    uint32_t total_sectors;
    uint32_t data_sectors;

    if (vol_idx < 0 || (uint32_t)vol_idx >= FAT32_MAX_VOLUMES || dev == (BlockDevice *)0
        || dev->read == (int (*)(struct BlockDevice *, uint32_t, uint32_t, void *))0) {
        return -1;
    }

    if (dev->read(dev, 0u, 1u, sector) != 0) {
        return -1;
    }
    if (sector[510] != 0x55u || sector[511] != 0xAAu) {
        return -1;
    }

    vol = &fat32_volumes[vol_idx];
    fat32_memset(vol, 0u, sizeof(*vol));
    fat32_memcpy(&vol->bpb, sector, sizeof(Fat32BPB));
    if (vol->bpb.bytes_per_sector != BLOCK_SIZE || vol->bpb.num_fats != 2u
        || fat32_memcmp(vol->bpb.fs_type, "FAT32   ", 8u) != 0 || vol->bpb.root_cluster < 2u
        || vol->bpb.sectors_per_cluster == 0u || vol->bpb.sectors_per_cluster > 8u) {
        return -1;
    }

    vol->dev = dev;
    vol->fat_start_lba = vol->bpb.reserved_sectors;
    vol->fat2_start_lba = vol->fat_start_lba + vol->bpb.fat_size_32;
    vol->data_start_lba = vol->bpb.reserved_sectors + (vol->bpb.num_fats * vol->bpb.fat_size_32);
    vol->sectors_per_cluster = vol->bpb.sectors_per_cluster;
    vol->bytes_per_cluster = vol->sectors_per_cluster * BLOCK_SIZE;
    vol->root_cluster = vol->bpb.root_cluster;
    total_sectors = vol->bpb.total_sectors_32 != 0u ? vol->bpb.total_sectors_32 : vol->bpb.total_sectors_16;
    data_sectors = total_sectors - vol->data_start_lba;
    vol->total_clusters = data_sectors / vol->sectors_per_cluster;
    fat32_copy_label(vol->label, sizeof(vol->label), vol->bpb.volume_label, 11u);
    vol->mounted = 1;

    if (dev->read(dev, vol->bpb.fs_info_sector, 1u, sector) == 0) {
        fsinfo = (Fat32FSInfo *)sector;
        if (fsinfo->lead_sig == 0x41615252u && fsinfo->struct_sig == 0x61417272u
            && fsinfo->trail_sig == 0xAA550000u) {
            vol->free_clusters = fsinfo->free_count;
            vol->next_free = fsinfo->next_free;
        }
    }
    if (vol->free_clusters == 0u || vol->free_clusters > vol->total_clusters) {
        vol->free_clusters = vol->total_clusters > 1u ? vol->total_clusters - 1u : 0u;
    }
    if (vol->next_free < 2u || vol->next_free > vol->total_clusters + 1u) {
        vol->next_free = 2u;
    }

    return 0;
}

/* Flush and drop one mounted FAT32 volume slot. */
int fat32_unmount(int vol_idx) {
    Fat32Volume *vol;

    if (vol_idx < 0 || (uint32_t)vol_idx >= FAT32_MAX_VOLUMES) {
        return -1;
    }
    vol = &fat32_volumes[vol_idx];
    if (!vol->mounted) {
        return 0;
    }
    if (fat32_sync(vol_idx) != 0) {
        return -1;
    }
    fat32_memset(vol, 0u, sizeof(*vol));
    return 0;
}

/* Return one mounted FAT32 volume slot by index. */
Fat32Volume *fat32_get_volume(int vol_idx) {
    if (vol_idx < 0 || (uint32_t)vol_idx >= FAT32_MAX_VOLUMES || !fat32_volumes[vol_idx].mounted) {
        return (Fat32Volume *)0;
    }
    return &fat32_volumes[vol_idx];
}

/* Flush the cache and update the mounted volume's FSInfo sectors. */
int fat32_sync(int vol_idx) {
    Fat32Volume *vol = fat32_get_volume(vol_idx);

    if (vol == (Fat32Volume *)0) {
        return -1;
    }
    if (fat32_cache_flush(vol) != 0) {
        return -1;
    }
    return fat32_update_fsinfo(vol);
}

/* Validate and repair basic FAT32 invariants on one mounted volume. */
int fat32_fsck(int vol_idx) {
    Fat32Volume *vol = fat32_get_volume(vol_idx);
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector0[BLOCK_SIZE];
    /* static to avoid stack overflow — not reentrant */
    static uint8_t fat1[BLOCK_SIZE];
    /* static to avoid stack overflow — not reentrant */
    static uint8_t fat2[BLOCK_SIZE];
    uint32_t free_count = 0u;
    uint32_t cluster;
    uint32_t sector_index;
    int issues = 0;

    if (vol == (Fat32Volume *)0) {
        return -1;
    }
    if (fat32_cache_flush(vol) != 0) {
        return -1;
    }
    if (vol->dev->read(vol->dev, 0u, 1u, sector0) != 0) {
        return -1;
    }
    if (sector0[510] != 0x55u || sector0[511] != 0xAAu || fat32_memcmp(vol->bpb.fs_type, "FAT32   ", 8u) != 0
        || vol->bpb.bytes_per_sector != BLOCK_SIZE || vol->bpb.num_fats != 2u || vol->bpb.root_cluster < 2u) {
        return -1;
    }

    for (cluster = 2u; cluster <= vol->total_clusters + 1u; cluster++) {
        uint32_t value = fat32_read_fat(vol, cluster);

        if (value == FAT32_FREE) {
            free_count++;
        }
    }
    if (free_count != vol->free_clusters) {
        vol->free_clusters = free_count;
        issues++;
    }

    for (sector_index = 0u; sector_index < vol->bpb.fat_size_32; sector_index++) {
        if (vol->dev->read(vol->dev, vol->fat_start_lba + sector_index, 1u, fat1) != 0
            || vol->dev->read(vol->dev, vol->fat2_start_lba + sector_index, 1u, fat2) != 0) {
            return -1;
        }
        if (fat32_memcmp(fat1, fat2, BLOCK_SIZE) != 0) {
            if (vol->dev->write(vol->dev, vol->fat2_start_lba + sector_index, 1u, fat1) != 0) {
                return -1;
            }
            issues++;
        }
    }

    if (fat32_update_fsinfo(vol) != 0) {
        return -1;
    }
    return issues;
}
