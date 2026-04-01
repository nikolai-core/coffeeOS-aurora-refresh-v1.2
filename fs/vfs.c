#include <stdint.h>

#include "ascii_util.h"
#include "fat32.h"
#include "vfs.h"

static VfsFile vfs_files[VFS_MAX_OPEN_FILES];
static uint8_t vfs_cluster_scratch[BLOCK_SIZE * 8u];

/* Return non-zero when one FAT value marks end-of-chain. */
static int vfs_is_eoc(uint32_t value) {
    return value >= FAT32_EOC;
}

/* Copy one ASCII string into a fixed-size destination buffer. */
static void vfs_copy_string(char *dst, uint32_t dst_len, const char *src) {
    uint32_t i = 0u;

    if (dst_len == 0u) {
        return;
    }
    while (i + 1u < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Copy a raw byte range without libc. */
static void vfs_memcpy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
}

/* Fill one raw byte range with one byte value. */
static void vfs_memset(void *dst, uint8_t value, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < len; i++) {
        d[i] = value;
    }
}

/* Convert one path component into an uppercase FAT 8.3 name. */
static void vfs_to_83(const char *lfn, uint8_t *name83) {
    uint32_t i;
    uint32_t len = 0u;
    uint32_t last_dot = 0xFFFFFFFFu;
    uint32_t base_i = 0u;
    uint32_t ext_i = 0u;

    for (i = 0u; i < 11u; i++) {
        name83[i] = ' ';
    }
    while (lfn[len] != '\0' && len < VFS_NAME_MAX - 1u) {
        if (lfn[len] == '.') {
            last_dot = len;
        }
        len++;
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
            if (base_i < 8u) {
                name83[base_i++] = (uint8_t)c;
            }
        } else if (ext_i < 3u) {
            name83[8u + ext_i++] = (uint8_t)c;
        }
    }
}

/* Convert one FAT 8.3 name into a printable dotted ASCII string. */
static void vfs_from_83(const uint8_t *name83, const uint8_t *ext83, char *out) {
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

/* Decode one LFN chunk into a flat ASCII buffer using the low UTF-16 byte only. */
static void vfs_lfn_decode(const Fat32LFNEntry *lfn, char *name) {
    uint32_t base = ((lfn->order & 0x1Fu) - 1u) * 13u;
    uint32_t pos = base;
    uint32_t i;
    const uint16_t *parts[3] = {lfn->name1, lfn->name2, lfn->name3};
    const uint32_t sizes[3] = {5u, 6u, 2u};
    uint32_t group;

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
            if (pos + 1u < VFS_NAME_MAX) {
                name[pos++] = (char)(ch & 0x00FFu);
                name[pos] = '\0';
            }
        }
    }
}

/* Return the mounted root cluster for one VFS volume. */
static uint32_t vfs_root_cluster(int vol_idx) {
    Fat32Volume *vol = fat32_get_volume(vol_idx);

    if (vol == (Fat32Volume *)0) {
        return 0u;
    }
    return vol->root_cluster;
}

/* Read one FAT entry directly from disk for directory-chain traversal helpers. */
static uint32_t vfs_read_fat_direct(Fat32Volume *vol, uint32_t cluster) {
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];
    uint32_t fat_off = cluster * 4u;
    uint32_t lba = vol->fat_start_lba + (fat_off / BLOCK_SIZE);
    uint32_t offset = fat_off % BLOCK_SIZE;

    if (vol->dev->read(vol->dev, lba, 1u, sector) != 0) {
        return FAT32_BAD;
    }
    return ((((uint32_t)sector[offset + 0u])
        | ((uint32_t)sector[offset + 1u] << 8)
        | ((uint32_t)sector[offset + 2u] << 16)
        | ((uint32_t)sector[offset + 3u] << 24)) & FAT32_MASK);
}

/* Split an absolute path into path components without supporting . or .. . */
static int vfs_split_path(const char *path, char components[][VFS_NAME_MAX], int max_components) {
    int count = 0;
    uint32_t i = 1u;

    if (path == (const char *)0 || path[0] != '/') {
        return VFS_ERR_BADPATH;
    }
    if (path[1] == '\0') {
        return 0;
    }

    while (path[i] != '\0') {
        uint32_t out = 0u;

        while (path[i] == '/') {
            i++;
        }
        if (path[i] == '\0') {
            break;
        }
        if (count >= max_components) {
            return VFS_ERR_TOOLONG;
        }
        while (path[i] != '\0' && path[i] != '/') {
            if (out + 1u >= VFS_NAME_MAX) {
                return VFS_ERR_TOOLONG;
            }
            components[count][out++] = path[i++];
        }
        components[count][out] = '\0';
        count++;
    }

    return count;
}

/* Resolve one absolute path to its entry and containing directory. */
static int vfs_resolve_path(int vol_idx, const char *path, uint32_t *out_cluster, uint32_t *out_parent_cluster,
                            Fat32DirEntry *out_entry, uint32_t *out_entry_lba, uint32_t *out_entry_offset) {
    Fat32Volume *vol = fat32_get_volume(vol_idx);
    /* static to avoid stack overflow — not reentrant */
    static char components[16][VFS_NAME_MAX];
    int count;
    int i;
    uint32_t current;
    uint32_t parent;
    Fat32DirEntry entry;
    uint32_t entry_lba = 0u;
    uint32_t entry_offset = 0u;

    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }

    count = vfs_split_path(path, components, 16);
    if (count < 0) {
        return count;
    }

    current = vol->root_cluster;
    parent = current;
    if (count == 0) {
        if (out_cluster != (uint32_t *)0) {
            *out_cluster = current;
        }
        if (out_parent_cluster != (uint32_t *)0) {
            *out_parent_cluster = current;
        }
        if (out_entry != (Fat32DirEntry *)0) {
            vfs_memset(out_entry, 0u, sizeof(*out_entry));
            out_entry->attributes = FAT_ATTR_DIRECTORY;
            fat32_set_entry_cluster(out_entry, current);
        }
        if (out_entry_lba != (uint32_t *)0) {
            *out_entry_lba = 0u;
        }
        if (out_entry_offset != (uint32_t *)0) {
            *out_entry_offset = 0u;
        }
        return VFS_OK;
    }

    for (i = 0; i < count; i++) {
        if (!fat32_dir_find(vol, current, components[i], &entry, &entry_lba, &entry_offset)) {
            return VFS_ERR_NOTFOUND;
        }
        parent = current;
        current = fat32_entry_cluster(&entry);
        if (i + 1 < count && (entry.attributes & FAT_ATTR_DIRECTORY) == 0u) {
            return VFS_ERR_NOTDIR;
        }
    }

    if (out_cluster != (uint32_t *)0) {
        *out_cluster = current;
    }
    if (out_parent_cluster != (uint32_t *)0) {
        *out_parent_cluster = parent;
    }
    if (out_entry != (Fat32DirEntry *)0) {
        *out_entry = entry;
    }
    if (out_entry_lba != (uint32_t *)0) {
        *out_entry_lba = entry_lba;
    }
    if (out_entry_offset != (uint32_t *)0) {
        *out_entry_offset = entry_offset;
    }
    return VFS_OK;
}

/* Split one absolute path into parent directory path and final component name. */
static int vfs_split_parent(const char *path, char *parent, char *leaf) {
    uint32_t len;
    uint32_t i;

    if (path == (const char *)0 || path[0] != '/') {
        return VFS_ERR_BADPATH;
    }
    len = ascii_strlen(path);
    if (len <= 1u) {
        return VFS_ERR_BADPATH;
    }
    while (len > 1u && path[len - 1u] == '/') {
        len--;
    }
    i = len;
    while (i > 0u && path[i - 1u] != '/') {
        i--;
    }
    if (i >= len) {
        return VFS_ERR_BADPATH;
    }
    vfs_copy_string(leaf, VFS_NAME_MAX, path + i);
    if (i == 0u) {
        return VFS_ERR_BADPATH;
    }
    if (i == 1u) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        uint32_t p;
        for (p = 0u; p < i && p + 1u < VFS_MAX_PATH; p++) {
            parent[p] = path[p];
        }
        parent[i] = '\0';
    }
    return VFS_OK;
}

/* Update one open file's backing directory entry after size or cluster changes. */
static int vfs_sync_open_file(VfsFile *file) {
    Fat32Volume *vol;
    /* static to avoid stack overflow — not reentrant */
    static uint8_t sector[BLOCK_SIZE];
    Fat32DirEntry *entry;

    if (file == (VfsFile *)0) {
        return VFS_ERR_BADFD;
    }
    vol = fat32_get_volume(file->vol_idx);
    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }
    if (file->entry_lba == 0u && file->entry_offset == 0u && file->path[0] == '/' && file->path[1] == '\0') {
        return VFS_OK;
    }
    if (vol->dev->read(vol->dev, file->entry_lba, 1u, sector) != 0) {
        return VFS_ERR_IO;
    }
    entry = (Fat32DirEntry *)(void *)(sector + file->entry_offset);
    entry->file_size = file->file_size;
    fat32_set_entry_cluster(entry, file->start_cluster);
    if (fat32_update_dir_entry(vol, file->entry_lba, file->entry_offset, entry) != 0) {
        return VFS_ERR_IO;
    }
    if (fat32_cache_flush(vol) != 0) {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}

/* Scan one directory into VFS-facing entries while reconstructing LFN names when present. */
static int vfs_scan_directory(Fat32Volume *vol, uint32_t dir_cluster, VfsDirEntry *entries, int max_entries) {
    uint32_t cluster = dir_cluster;
    int count = 0;
    char lfn_name[VFS_NAME_MAX];
    uint32_t safety = 0u;

    lfn_name[0] = '\0';
    while (cluster >= 2u && cluster <= vol->total_clusters + 1u) {
        uint32_t lba = fat32_cluster_to_lba(vol, cluster);
        uint32_t sector_index;

        if (++safety > vol->total_clusters) {
            return VFS_ERR_IO;
        }
        for (sector_index = 0u; sector_index < vol->sectors_per_cluster; sector_index++) {
            /* static to avoid stack overflow — not reentrant */
            static uint8_t sector[BLOCK_SIZE];
            uint32_t entry_index;

            if (vol->dev->read(vol->dev, lba + sector_index, 1u, sector) != 0) {
                return VFS_ERR_IO;
            }
            for (entry_index = 0u; entry_index < (BLOCK_SIZE / sizeof(Fat32DirEntry)); entry_index++) {
                Fat32DirEntry *entry = (Fat32DirEntry *)(void *)(sector + (entry_index * sizeof(Fat32DirEntry)));

                if (entry->name[0] == FAT_ENTRY_END) {
                    return count;
                }
                if (entry->attributes == FAT_ATTR_LFN) {
                    Fat32LFNEntry *lfn = (Fat32LFNEntry *)(void *)entry;

                    if ((lfn->order & 0x40u) != 0u) {
                        lfn_name[0] = '\0';
                    }
                    vfs_lfn_decode(lfn, lfn_name);
                    continue;
                }
                if (entry->name[0] == FAT_ENTRY_DELETED) {
                    lfn_name[0] = '\0';
                    continue;
                }
                if (count < max_entries) {
                    if (lfn_name[0] != '\0') {
                        vfs_copy_string(entries[count].name, VFS_NAME_MAX, lfn_name);
                    } else {
                        vfs_from_83(entry->name, entry->ext, entries[count].name);
                    }
                    entries[count].size = entry->file_size;
                    entries[count].type = (entry->attributes & FAT_ATTR_DIRECTORY) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                    entries[count].cluster = fat32_entry_cluster(entry);
                    count++;
                }
                lfn_name[0] = '\0';
            }
        }

        {
            uint32_t next = vfs_read_fat_direct(vol, cluster);
            if (vfs_is_eoc(next) || next == FAT32_FREE || next == FAT32_BAD) {
                break;
            }
            cluster = next;
        }
    }
    return count;
}

/* Return non-zero when one directory contains only . and .. entries. */
static int vfs_dir_empty(Fat32Volume *vol, uint32_t dir_cluster) {
    /* static to avoid stack overflow — not reentrant */
    static VfsDirEntry entries[32];
    int count;
    int i;

    count = vfs_scan_directory(vol, dir_cluster, entries, 32);
    if (count < 0) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (!ascii_streq(entries[i].name, ".") && !ascii_streq(entries[i].name, "..")) {
            return 0;
        }
    }
    return 1;
}

/* Initialize the fixed VFS open-file table. */
void vfs_init(void) {
    uint32_t i;

    for (i = 0u; i < VFS_MAX_OPEN_FILES; i++) {
        vfs_files[i].fd = (int)i;
        vfs_files[i].open = 0;
        vfs_files[i].path[0] = '\0';
    }
}

/* Mount one block device through the FAT32 backend. */
int vfs_mount(int vol_idx, BlockDevice *dev) {
    return fat32_mount(vol_idx, dev) == 0 ? VFS_OK : VFS_ERR_IO;
}

/* Open one file by absolute path and create it when requested. */
int vfs_open(const char *path, uint32_t flags) {
    Fat32Volume *vol = fat32_get_volume(0);
    int fd;
    uint32_t cluster = 0u;
    uint32_t parent_cluster = 0u;
    Fat32DirEntry entry;
    uint32_t entry_lba = 0u;
    uint32_t entry_offset = 0u;
    int r;
    int created = 0;

    if (vol == (Fat32Volume *)0 || path == (const char *)0 || path[0] != '/') {
        return VFS_ERR_BADPATH;
    }
    for (fd = 0; fd < (int)VFS_MAX_OPEN_FILES; fd++) {
        if (!vfs_files[fd].open) {
            break;
        }
    }
    if (fd >= (int)VFS_MAX_OPEN_FILES) {
        return VFS_ERR_NOMEM;
    }

    r = vfs_resolve_path(0, path, &cluster, &parent_cluster, &entry, &entry_lba, &entry_offset);
    if (r == VFS_ERR_NOTFOUND && (flags & VFS_O_CREATE) != 0u) {
        char parent[VFS_MAX_PATH];
        char leaf[VFS_NAME_MAX];
        uint8_t name83[11];
        uint32_t new_cluster;
        Fat32DirEntry new_entry;

        r = vfs_split_parent(path, parent, leaf);
        if (r != VFS_OK) {
            return r;
        }
        r = vfs_resolve_path(0, parent, &parent_cluster, (uint32_t *)0, &entry, (uint32_t *)0, (uint32_t *)0);
        if (r != VFS_OK) {
            return r;
        }
        if ((entry.attributes & FAT_ATTR_DIRECTORY) == 0u) {
            return VFS_ERR_NOTDIR;
        }

        new_cluster = fat32_alloc_cluster(vol);
        if (new_cluster == FAT32_BAD) {
            return VFS_ERR_NOSPACE;
        }
        vfs_memset(vfs_cluster_scratch, 0u, vol->bytes_per_cluster);
        if (fat32_write_cluster(vol, new_cluster, vfs_cluster_scratch) != 0 || fat32_cache_flush(vol) != 0) {
            return VFS_ERR_IO;
        }

        vfs_memset(&new_entry, 0u, sizeof(new_entry));
        vfs_to_83(leaf, name83);
        vfs_memcpy(new_entry.name, name83, 8u);
        vfs_memcpy(new_entry.ext, name83 + 8u, 3u);
        new_entry.attributes = FAT_ATTR_ARCHIVE;
        fat32_set_entry_cluster(&new_entry, new_cluster);
        new_entry.file_size = 0u;

        if (fat32_dir_create_entry(vol, parent_cluster, &new_entry, leaf, &entry_lba, &entry_offset) != 0) {
            return VFS_ERR_IO;
        }
        entry = new_entry;
        cluster = new_cluster;
        created = 1;
    } else if (r != VFS_OK) {
        return r;
    }

    if ((entry.attributes & FAT_ATTR_DIRECTORY) != 0u) {
        return VFS_ERR_ISDIR;
    }

    vfs_files[fd].fd = fd;
    vfs_files[fd].vol_idx = 0;
    vfs_files[fd].start_cluster = cluster;
    vfs_files[fd].current_cluster = cluster;
    vfs_files[fd].file_size = entry.file_size;
    vfs_files[fd].position = 0u;
    vfs_files[fd].flags = flags;
    vfs_files[fd].open = 1;
    vfs_files[fd].entry_lba = entry_lba;
    vfs_files[fd].entry_offset = entry_offset;
    vfs_copy_string(vfs_files[fd].path, VFS_MAX_PATH, path);

    if ((flags & VFS_O_TRUNC) != 0u && !created) {
        uint32_t new_cluster = fat32_alloc_cluster(vol);

        if (new_cluster == FAT32_BAD) {
            vfs_files[fd].open = 0;
            return VFS_ERR_NOSPACE;
        }
        if (fat32_free_chain(vol, vfs_files[fd].start_cluster) != 0) {
            vfs_files[fd].open = 0;
            return VFS_ERR_IO;
        }
        vfs_memset(vfs_cluster_scratch, 0u, vol->bytes_per_cluster);
        if (fat32_write_cluster(vol, new_cluster, vfs_cluster_scratch) != 0) {
            vfs_files[fd].open = 0;
            return VFS_ERR_IO;
        }
        vfs_files[fd].start_cluster = new_cluster;
        vfs_files[fd].current_cluster = new_cluster;
        vfs_files[fd].file_size = 0u;
        vfs_files[fd].position = 0u;
        if (vfs_sync_open_file(&vfs_files[fd]) != VFS_OK) {
            vfs_files[fd].open = 0;
            return VFS_ERR_IO;
        }
    }

    if ((flags & VFS_O_APPEND) != 0u) {
        vfs_files[fd].position = vfs_files[fd].file_size;
    }
    return fd;
}

/* Close one VFS file descriptor slot. */
int vfs_close(int fd) {
    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return VFS_ERR_BADFD;
    }
    vfs_files[fd].open = 0;
    vfs_files[fd].path[0] = '\0';
    return VFS_OK;
}

/* Read bytes from one open VFS file descriptor. */
int vfs_read(int fd, void *buf, uint32_t len) {
    Fat32Volume *vol;
    uint32_t remaining;
    int n;

    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return VFS_ERR_BADFD;
    }
    if ((vfs_files[fd].flags & VFS_O_READ) == 0u) {
        return VFS_ERR_READONLY;
    }
    if (vfs_files[fd].position >= vfs_files[fd].file_size) {
        return 0;
    }
    remaining = vfs_files[fd].file_size - vfs_files[fd].position;
    if (len > remaining) {
        len = remaining;
    }
    vol = fat32_get_volume(vfs_files[fd].vol_idx);
    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }
    n = fat32_read_chain(vol, vfs_files[fd].start_cluster, vfs_files[fd].position, buf, len);
    if (n < 0) {
        return VFS_ERR_IO;
    }
    vfs_files[fd].position += (uint32_t)n;
    return n;
}

/* Write bytes to one open VFS file descriptor and persist the updated file size. */
int vfs_write(int fd, const void *buf, uint32_t len) {
    Fat32Volume *vol;
    int n;
    uint32_t new_end = 0u;

    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return VFS_ERR_BADFD;
    }
    if ((vfs_files[fd].flags & VFS_O_WRITE) == 0u) {
        return VFS_ERR_READONLY;
    }
    vol = fat32_get_volume(vfs_files[fd].vol_idx);
    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }

    n = fat32_write_chain(vol, &vfs_files[fd].start_cluster, vfs_files[fd].position, buf, len, &new_end);
    if (n < 0) {
        return VFS_ERR_IO;
    }
    vfs_files[fd].current_cluster = new_end;
    if (vfs_files[fd].position + (uint32_t)n > vfs_files[fd].file_size) {
        vfs_files[fd].file_size = vfs_files[fd].position + (uint32_t)n;
    }
    vfs_files[fd].position += (uint32_t)n;
    if (vfs_sync_open_file(&vfs_files[fd]) != VFS_OK) {
        return VFS_ERR_IO;
    }
    if (fat32_cache_flush(vol) != 0) {
        return VFS_ERR_IO;
    }
    return n;
}

/* Seek one open VFS file descriptor relative to start, current, or end. */
int vfs_seek(int fd, int32_t offset, int whence) {
    int64_t next;

    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return VFS_ERR_BADFD;
    }

    if (whence == 0) {
        next = offset;
    } else if (whence == 1) {
        next = (int64_t)vfs_files[fd].position + offset;
    } else if (whence == 2) {
        next = (int64_t)vfs_files[fd].file_size + offset;
    } else {
        return VFS_ERR_BADPATH;
    }

    if (next < 0) {
        next = 0;
    }
    vfs_files[fd].position = (uint32_t)next;
    return (int)vfs_files[fd].position;
}

/* Return the current logical byte offset for one open VFS file. */
uint32_t vfs_tell(int fd) {
    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return 0u;
    }
    return vfs_files[fd].position;
}

/* Return the current file size for one open VFS file. */
uint32_t vfs_size(int fd) {
    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return 0u;
    }
    return vfs_files[fd].file_size;
}

/* Return non-zero when one open VFS file is positioned at EOF. */
int vfs_eof(int fd) {
    if (fd < 0 || (uint32_t)fd >= VFS_MAX_OPEN_FILES || !vfs_files[fd].open) {
        return 1;
    }
    return vfs_files[fd].position >= vfs_files[fd].file_size;
}

/* Create one new directory and seed it with . and .. entries. */
int vfs_mkdir(const char *path) {
    Fat32Volume *vol = fat32_get_volume(0);
    char parent[VFS_MAX_PATH];
    char leaf[VFS_NAME_MAX];
    uint32_t parent_cluster;
    Fat32DirEntry parent_entry;
    uint32_t new_cluster;
    Fat32DirEntry entry;
    uint8_t name83[11];

    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }
    if (vfs_exists(path)) {
        return VFS_ERR_EXISTS;
    }
    if (vfs_split_parent(path, parent, leaf) != VFS_OK) {
        return VFS_ERR_BADPATH;
    }
    if (vfs_resolve_path(0, parent, &parent_cluster, (uint32_t *)0, &parent_entry, (uint32_t *)0, (uint32_t *)0) != VFS_OK) {
        return VFS_ERR_NOTFOUND;
    }
    if ((parent_entry.attributes & FAT_ATTR_DIRECTORY) == 0u) {
        return VFS_ERR_NOTDIR;
    }

    new_cluster = fat32_alloc_cluster(vol);
    if (new_cluster == FAT32_BAD) {
        return VFS_ERR_NOSPACE;
    }
    vfs_memset(vfs_cluster_scratch, 0u, vol->bytes_per_cluster);
    {
        Fat32DirEntry *dot = (Fat32DirEntry *)(void *)vfs_cluster_scratch;
        Fat32DirEntry *dotdot = dot + 1;

        vfs_memset(dot, 0u, sizeof(Fat32DirEntry) * 2u);
        dot->name[0] = '.';
        dot->attributes = FAT_ATTR_DIRECTORY;
        fat32_set_entry_cluster(dot, new_cluster);
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';
        dotdot->attributes = FAT_ATTR_DIRECTORY;
        fat32_set_entry_cluster(dotdot, parent_cluster);
    }
    if (fat32_write_cluster(vol, new_cluster, vfs_cluster_scratch) != 0 || fat32_cache_flush(vol) != 0) {
        return VFS_ERR_IO;
    }

    vfs_memset(&entry, 0u, sizeof(entry));
    vfs_to_83(leaf, name83);
    vfs_memcpy(entry.name, name83, 8u);
    vfs_memcpy(entry.ext, name83 + 8u, 3u);
    entry.attributes = FAT_ATTR_DIRECTORY;
    fat32_set_entry_cluster(&entry, new_cluster);
    return fat32_dir_create_entry(vol, parent_cluster, &entry, leaf, (uint32_t *)0, (uint32_t *)0) == 0
        ? VFS_OK : VFS_ERR_IO;
}

/* Delete one file or empty directory by removing its directory entry and cluster chain. */
int vfs_delete(const char *path) {
    Fat32Volume *vol = fat32_get_volume(0);
    uint32_t cluster;
    uint32_t parent_cluster;
    Fat32DirEntry entry;
    uint32_t entry_lba;
    uint32_t entry_offset;
    int r;

    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }
    if (ascii_streq(path, "/")) {
        return VFS_ERR_BADPATH;
    }
    r = vfs_resolve_path(0, path, &cluster, &parent_cluster, &entry, &entry_lba, &entry_offset);
    if (r != VFS_OK) {
        return r;
    }
    if ((entry.attributes & FAT_ATTR_DIRECTORY) != 0u && !vfs_dir_empty(vol, cluster)) {
        return VFS_ERR_NOTEMPTY;
    }
    if (fat32_dir_delete_entry(vol, entry_lba, entry_offset, cluster) != 0) {
        return VFS_ERR_IO;
    }
    return fat32_cache_flush(vol) == 0 ? VFS_OK : VFS_ERR_IO;
}

/* Rename one file by creating a new entry and deleting the old one without touching its data chain. */
int vfs_rename(const char *old_path, const char *new_path) {
    Fat32Volume *vol = fat32_get_volume(0);
    uint32_t old_cluster;
    uint32_t old_parent;
    Fat32DirEntry old_entry;
    uint32_t old_lba;
    uint32_t old_offset;
    char new_parent_path[VFS_MAX_PATH];
    char new_leaf[VFS_NAME_MAX];
    uint32_t new_parent_cluster;
    Fat32DirEntry new_parent_entry;
    Fat32DirEntry new_entry;
    uint8_t name83[11];
    int r;

    if (vol == (Fat32Volume *)0) {
        return VFS_ERR_IO;
    }
    r = vfs_resolve_path(0, old_path, &old_cluster, &old_parent, &old_entry, &old_lba, &old_offset);
    if (r != VFS_OK) {
        return r;
    }
    if ((old_entry.attributes & FAT_ATTR_DIRECTORY) != 0u) {
        return VFS_ERR_ISDIR;
    }
    if (vfs_exists(new_path)) {
        return VFS_ERR_EXISTS;
    }
    if (vfs_split_parent(new_path, new_parent_path, new_leaf) != VFS_OK) {
        return VFS_ERR_BADPATH;
    }
    r = vfs_resolve_path(0, new_parent_path, &new_parent_cluster, (uint32_t *)0, &new_parent_entry,
                         (uint32_t *)0, (uint32_t *)0);
    if (r != VFS_OK) {
        return r;
    }
    if ((new_parent_entry.attributes & FAT_ATTR_DIRECTORY) == 0u) {
        return VFS_ERR_NOTDIR;
    }

    new_entry = old_entry;
    vfs_to_83(new_leaf, name83);
    vfs_memcpy(new_entry.name, name83, 8u);
    vfs_memcpy(new_entry.ext, name83 + 8u, 3u);
    if (fat32_dir_create_entry(vol, new_parent_cluster, &new_entry, new_leaf,
                               (uint32_t *)0, (uint32_t *)0) != 0) {
        return VFS_ERR_IO;
    }
    if (fat32_dir_mark_deleted(vol, old_lba, old_offset) != 0) {
        return VFS_ERR_IO;
    }
    return fat32_cache_flush(vol) == 0 ? VFS_OK : VFS_ERR_IO;
}

/* Return non-zero when one absolute path resolves to a real filesystem entry. */
int vfs_exists(const char *path) {
    uint32_t cluster;
    uint32_t parent_cluster;
    Fat32DirEntry entry;

    return vfs_resolve_path(0, path, &cluster, &parent_cluster, &entry, (uint32_t *)0, (uint32_t *)0) == VFS_OK;
}

/* Enumerate one directory into VFS-facing name/type/size records. */
int vfs_listdir(const char *path, VfsDirEntry *entries, int max_entries) {
    Fat32Volume *vol = fat32_get_volume(0);
    uint32_t cluster;
    Fat32DirEntry entry;
    int r;

    if (vol == (Fat32Volume *)0 || entries == (VfsDirEntry *)0 || max_entries <= 0) {
        return VFS_ERR_BADPATH;
    }
    r = vfs_resolve_path(0, path, &cluster, (uint32_t *)0, &entry, (uint32_t *)0, (uint32_t *)0);
    if (r != VFS_OK) {
        return r;
    }
    if ((entry.attributes & FAT_ATTR_DIRECTORY) == 0u && !ascii_streq(path, "/")) {
        return VFS_ERR_NOTDIR;
    }
    return vfs_scan_directory(vol, ascii_streq(path, "/") ? vfs_root_cluster(0) : cluster, entries, max_entries);
}

/* Read one whole file into a caller-provided buffer. */
int vfs_read_file(const char *path, void *buf, uint32_t max_len, uint32_t *out_len) {
    int fd;
    int n;

    fd = vfs_open(path, VFS_O_READ);
    if (fd < 0) {
        return fd;
    }
    n = vfs_read(fd, buf, max_len);
    vfs_close(fd);
    if (n < 0) {
        return n;
    }
    if (out_len != (uint32_t *)0) {
        *out_len = (uint32_t)n;
    }
    return VFS_OK;
}

/* Write one whole file by creating it if needed and truncating any old content. */
int vfs_write_file(const char *path, const void *buf, uint32_t len) {
    int fd;
    int n;

    fd = vfs_open(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (fd < 0) {
        return fd;
    }
    n = vfs_write(fd, buf, len);
    vfs_close(fd);
    return (n < 0) ? n : VFS_OK;
}

/* Return basic metadata for one filesystem path. */
int vfs_stat(const char *path, VfsDirEntry *out) {
    uint32_t cluster;
    Fat32DirEntry entry;
    int r;

    if (out == (VfsDirEntry *)0) {
        return VFS_ERR_BADPATH;
    }
    if (ascii_streq(path, "/")) {
        vfs_copy_string(out->name, VFS_NAME_MAX, "/");
        out->size = 0u;
        out->type = VFS_TYPE_DIR;
        out->cluster = vfs_root_cluster(0);
        return VFS_OK;
    }
    r = vfs_resolve_path(0, path, &cluster, (uint32_t *)0, &entry, (uint32_t *)0, (uint32_t *)0);
    if (r != VFS_OK) {
        return r;
    }
    {
        char parent[VFS_MAX_PATH];
        char leaf[VFS_NAME_MAX];

        if (vfs_split_parent(path, parent, leaf) == VFS_OK) {
            vfs_copy_string(out->name, VFS_NAME_MAX, leaf);
        } else {
            vfs_from_83(entry.name, entry.ext, out->name);
        }
    }
    out->size = entry.file_size;
    out->type = (entry.attributes & FAT_ATTR_DIRECTORY) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    out->cluster = cluster;
    return VFS_OK;
}

/* Create the default directory tree and seed files on a freshly formatted volume. */
int vfs_create_default_dirs(void) {
    static const char welcome_text[] =
        "Welcome to coffeeOS aurora refresh!\n"
        "This is your home directory.\n"
        "Type 'ls /home/user' to see files.\n"
        "Files you save here will persist across reboots.\n";
    static const char info_text[] =
        "coffeeOS aurora refresh\n"
        "Created by Johan Joseph\n"
        "Tested by Rayan Abdulsalam\n"
        "Persistent storage enabled.\n";

    (void)vfs_mkdir("/home");
    (void)vfs_mkdir("/home/user");
    (void)vfs_mkdir("/docs");
    (void)vfs_mkdir("/apps");
    (void)vfs_mkdir("/tmp");
    if (vfs_write_file("/home/user/welcome.txt", welcome_text, ascii_strlen(welcome_text)) != VFS_OK) {
        return VFS_ERR_IO;
    }
    if (vfs_write_file("/coffeeos.txt", info_text, ascii_strlen(info_text)) != VFS_OK) {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}

/* Return a short human-readable string for one VFS status code. */
const char *vfs_strerror(int err) {
    if (err == VFS_OK) return "ok";
    if (err == VFS_ERR_NOTFOUND) return "not found";
    if (err == VFS_ERR_EXISTS) return "already exists";
    if (err == VFS_ERR_NOSPACE) return "no space";
    if (err == VFS_ERR_IO) return "i/o error";
    if (err == VFS_ERR_BADFD) return "bad file descriptor";
    if (err == VFS_ERR_BADPATH) return "bad path";
    if (err == VFS_ERR_NOTDIR) return "not a directory";
    if (err == VFS_ERR_ISDIR) return "is a directory";
    if (err == VFS_ERR_NOTEMPTY) return "directory not empty";
    if (err == VFS_ERR_TOOLONG) return "name too long";
    if (err == VFS_ERR_NOMEM) return "no free slots";
    if (err == VFS_ERR_READONLY) return "access denied";
    return "unknown error";
}
