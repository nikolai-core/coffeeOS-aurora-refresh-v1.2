#ifndef VFS_H
#define VFS_H

#include <stdint.h>

#include "blkdev.h"

#define VFS_MAX_OPEN_FILES 16u
#define VFS_MAX_PATH 512u
#define VFS_NAME_MAX 256u

#define VFS_O_READ 0x01u
#define VFS_O_WRITE 0x02u
#define VFS_O_CREATE 0x04u
#define VFS_O_APPEND 0x08u
#define VFS_O_TRUNC 0x10u

#define VFS_TYPE_FILE 0u
#define VFS_TYPE_DIR 1u

#define VFS_OK 0
#define VFS_ERR_NOTFOUND -1
#define VFS_ERR_EXISTS -2
#define VFS_ERR_NOSPACE -3
#define VFS_ERR_IO -4
#define VFS_ERR_BADFD -5
#define VFS_ERR_BADPATH -6
#define VFS_ERR_NOTDIR -7
#define VFS_ERR_ISDIR -8
#define VFS_ERR_NOTEMPTY -9
#define VFS_ERR_TOOLONG -10
#define VFS_ERR_NOMEM -11
#define VFS_ERR_READONLY -12

typedef struct VfsFile {
    int fd;
    int vol_idx;
    uint32_t start_cluster;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t position;
    uint32_t flags;
    int open;
    char path[VFS_MAX_PATH];
    uint32_t entry_lba;
    uint32_t entry_offset;
} VfsFile;

typedef struct VfsDirEntry {
    char name[VFS_NAME_MAX];
    uint32_t size;
    int type;
    uint32_t cluster;
} VfsDirEntry;

void vfs_init(void);
int vfs_mount(int vol_idx, BlockDevice *dev);
int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, uint32_t len);
int vfs_write(int fd, const void *buf, uint32_t len);
int vfs_seek(int fd, int32_t offset, int whence);
uint32_t vfs_tell(int fd);
uint32_t vfs_size(int fd);
int vfs_eof(int fd);
int vfs_mkdir(const char *path);
int vfs_delete(const char *path);
int vfs_rename(const char *old_path, const char *new_path);
int vfs_exists(const char *path);
int vfs_listdir(const char *path, VfsDirEntry *entries, int max_entries);
int vfs_read_file(const char *path, void *buf, uint32_t max_len, uint32_t *out_len);
int vfs_write_file(const char *path, const void *buf, uint32_t len);
int vfs_stat(const char *path, VfsDirEntry *out);
int vfs_create_default_dirs(void);
const char *vfs_strerror(int err);

#endif
