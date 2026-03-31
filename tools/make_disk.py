#!/usr/bin/env python3
import os
import struct
import sys


BLOCK_SIZE = 512
SECTORS_PER_CLUSTER = 8
RESERVED_SECTORS = 32
NUM_FATS = 2
ROOT_CLUSTER = 2
MEDIA_TYPE = 0xF8
VOLUME_ID = 0xC0FFEE32
OEM_NAME = b"COFFEEOS"
FS_TYPE = b"FAT32   "
VOLUME_LABEL = b"COFFEEOS   "
FAT_ATTR_VOLUME_ID = 0x08
FAT_ATTR_DIRECTORY = 0x10
FAT_ATTR_ARCHIVE = 0x20
FAT32_EOC = 0x0FFFFFFF
MBR_PARTITION_TYPE_FAT32_LBA = 0x0C
MBR_PARTITION_LBA = 2048


def put_u16(buf, offset, value):
    struct.pack_into("<H", buf, offset, value)


def put_u32(buf, offset, value):
    struct.pack_into("<I", buf, offset, value)


def fat_name83(name):
    upper = name.upper()
    if "." in upper:
        base, ext = upper.rsplit(".", 1)
    else:
        base, ext = upper, ""
    base = "".join("_" if c in "+,;=[]" else c for c in base)[:8]
    ext = "".join("_" if c in "+,;=[]" else c for c in ext)[:3]
    return base.ljust(8).encode("ascii"), ext.ljust(3).encode("ascii")


def make_dirent(name, attr, cluster, size):
    entry = bytearray(32)
    name83, ext83 = fat_name83(name)
    entry[0:8] = name83
    entry[8:11] = ext83
    entry[11] = attr
    put_u16(entry, 20, (cluster >> 16) & 0xFFFF)
    put_u16(entry, 26, cluster & 0xFFFF)
    put_u32(entry, 28, size)
    return entry


def make_dot_entry(name, cluster):
    entry = bytearray(32)
    entry[0:11] = b"           "
    if name == ".":
        entry[0] = ord(".")
    else:
        entry[0] = ord(".")
        entry[1] = ord(".")
    entry[11] = FAT_ATTR_DIRECTORY
    put_u16(entry, 20, (cluster >> 16) & 0xFFFF)
    put_u16(entry, 26, cluster & 0xFFFF)
    return entry


def set_fat_entry(image, fat_start_sector, cluster, value):
    put_u32(image, (fat_start_sector * BLOCK_SIZE) + (cluster * 4), value & 0x0FFFFFFF)


def write_sector(image, lba, data):
    start = lba * BLOCK_SIZE
    image[start:start + len(data)] = data


def write_cluster(image, data_start_sector, cluster, data):
    start = (data_start_sector + ((cluster - 2) * SECTORS_PER_CLUSTER)) * BLOCK_SIZE
    image[start:start + len(data)] = data


def create_fat32_volume(total_sectors, hidden_sectors):
    cluster_estimate = total_sectors // SECTORS_PER_CLUSTER
    fat_size = ((cluster_estimate * 4) + (BLOCK_SIZE - 1)) // BLOCK_SIZE
    fat1_start = RESERVED_SECTORS
    fat2_start = fat1_start + fat_size
    data_start = RESERVED_SECTORS + (NUM_FATS * fat_size)
    total_clusters = (total_sectors - data_start) // SECTORS_PER_CLUSTER
    image = bytearray(total_sectors * BLOCK_SIZE)

    boot = bytearray(BLOCK_SIZE)
    boot[0:3] = bytes([0xEB, 0x58, 0x90])
    boot[3:11] = OEM_NAME
    put_u16(boot, 11, BLOCK_SIZE)
    boot[13] = SECTORS_PER_CLUSTER
    put_u16(boot, 14, RESERVED_SECTORS)
    boot[16] = NUM_FATS
    put_u16(boot, 17, 0)
    put_u16(boot, 19, 0)
    boot[21] = MEDIA_TYPE
    put_u16(boot, 22, 0)
    put_u16(boot, 24, 63)
    put_u16(boot, 26, 16)
    put_u32(boot, 28, hidden_sectors)
    put_u32(boot, 32, total_sectors)
    put_u32(boot, 36, fat_size)
    put_u16(boot, 40, 0)
    put_u16(boot, 42, 0)
    put_u32(boot, 44, ROOT_CLUSTER)
    put_u16(boot, 48, 1)
    put_u16(boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    put_u32(boot, 67, VOLUME_ID)
    boot[71:82] = VOLUME_LABEL
    boot[82:90] = FS_TYPE
    boot[510] = 0x55
    boot[511] = 0xAA
    write_sector(image, 0, boot)
    write_sector(image, 6, boot)

    used_clusters = {
        2: "root",
        3: "home",
        4: "user",
        5: "docs",
        6: "apps",
        7: "tmp",
        8: "welcome",
        9: "coffeeos",
    }
    free_clusters = total_clusters - len(used_clusters)

    fsinfo = bytearray(BLOCK_SIZE)
    put_u32(fsinfo, 0, 0x41615252)
    put_u32(fsinfo, 484, 0x61417272)
    put_u32(fsinfo, 488, free_clusters)
    put_u32(fsinfo, 492, 10)
    put_u32(fsinfo, 508, 0xAA550000)
    write_sector(image, 1, fsinfo)
    write_sector(image, 7, fsinfo)

    set_fat_entry(image, fat1_start, 0, 0x0FFFFFF8)
    set_fat_entry(image, fat1_start, 1, FAT32_EOC)
    for cluster in used_clusters:
        set_fat_entry(image, fat1_start, cluster, FAT32_EOC)
    image[fat2_start * BLOCK_SIZE:(fat2_start + fat_size) * BLOCK_SIZE] = \
        image[fat1_start * BLOCK_SIZE:(fat1_start + fat_size) * BLOCK_SIZE]

    root = bytearray(SECTORS_PER_CLUSTER * BLOCK_SIZE)
    home = bytearray(SECTORS_PER_CLUSTER * BLOCK_SIZE)
    user = bytearray(SECTORS_PER_CLUSTER * BLOCK_SIZE)
    docs = bytearray(SECTORS_PER_CLUSTER * BLOCK_SIZE)
    apps = bytearray(SECTORS_PER_CLUSTER * BLOCK_SIZE)
    tmp = bytearray(SECTORS_PER_CLUSTER * BLOCK_SIZE)

    root[0:32] = make_dirent("COFFEEOS", FAT_ATTR_VOLUME_ID, 0, 0)
    root[32:64] = make_dirent("HOME", FAT_ATTR_DIRECTORY, 3, 0)
    root[64:96] = make_dirent("DOCS", FAT_ATTR_DIRECTORY, 5, 0)
    root[96:128] = make_dirent("APPS", FAT_ATTR_DIRECTORY, 6, 0)
    root[128:160] = make_dirent("TMP", FAT_ATTR_DIRECTORY, 7, 0)

    home[0:32] = make_dot_entry(".", 3)
    home[32:64] = make_dot_entry("..", 2)
    home[64:96] = make_dirent("USER", FAT_ATTR_DIRECTORY, 4, 0)

    user[0:32] = make_dot_entry(".", 4)
    user[32:64] = make_dot_entry("..", 3)

    docs[0:32] = make_dot_entry(".", 5)
    docs[32:64] = make_dot_entry("..", 2)

    apps[0:32] = make_dot_entry(".", 6)
    apps[32:64] = make_dot_entry("..", 2)

    tmp[0:32] = make_dot_entry(".", 7)
    tmp[32:64] = make_dot_entry("..", 2)

    welcome_text = (
        "Welcome to coffeeOS aurora refresh!\n"
        "This is your home directory.\n"
        "Type 'ls /home/user' to see files.\n"
        "Files you save here will persist across reboots.\n"
    ).encode("ascii")
    coffeeos_text = (
        "coffeeOS aurora refresh\n"
        "Created by Johan Joseph\n"
        "Tested by Rayan Abdulsalam\n"
        "Persistent storage enabled.\n"
    ).encode("ascii")

    user[64:96] = make_dirent("WELCOME.TXT", FAT_ATTR_ARCHIVE, 8, len(welcome_text))
    root[160:192] = make_dirent("COFFEEOS.TXT", FAT_ATTR_ARCHIVE, 9, len(coffeeos_text))

    write_cluster(image, data_start, 2, root)
    write_cluster(image, data_start, 3, home)
    write_cluster(image, data_start, 4, user)
    write_cluster(image, data_start, 5, docs)
    write_cluster(image, data_start, 6, apps)
    write_cluster(image, data_start, 7, tmp)
    write_cluster(image, data_start, 8, welcome_text)
    write_cluster(image, data_start, 9, coffeeos_text)

    return image, total_clusters, free_clusters


def create_partitioned_disk(total_bytes):
    total_sectors = total_bytes // BLOCK_SIZE
    partition_sectors = total_sectors - MBR_PARTITION_LBA
    disk = bytearray(total_bytes)
    partition_image, total_clusters, free_clusters = create_fat32_volume(partition_sectors, MBR_PARTITION_LBA)
    mbr = bytearray(BLOCK_SIZE)

    mbr[446 + 0] = 0x00
    mbr[446 + 1:446 + 4] = bytes([0x00, 0x02, 0x00])
    mbr[446 + 4] = MBR_PARTITION_TYPE_FAT32_LBA
    mbr[446 + 5:446 + 8] = bytes([0xFE, 0xFF, 0xFF])
    put_u32(mbr, 446 + 8, MBR_PARTITION_LBA)
    put_u32(mbr, 446 + 12, partition_sectors)
    mbr[510] = 0x55
    mbr[511] = 0xAA

    write_sector(disk, 0, mbr)
    disk[MBR_PARTITION_LBA * BLOCK_SIZE:(MBR_PARTITION_LBA * BLOCK_SIZE) + len(partition_image)] = partition_image
    return disk, total_clusters, free_clusters


def main():
    if len(sys.argv) not in (3, 4):
        print("usage: python3 tools/make_disk.py [output_path] [size_mb] [--partitioned]")
        return 1

    output_path = sys.argv[1]
    size_mb = int(sys.argv[2])
    partitioned = len(sys.argv) == 4 and sys.argv[3] == "--partitioned"
    total_bytes = size_mb * 1024 * 1024

    if partitioned:
        final_image, total_clusters, free_clusters = create_partitioned_disk(total_bytes)
        image_kind = "raw MBR FAT32"
    else:
        total_sectors = total_bytes // BLOCK_SIZE
        final_image, total_clusters, free_clusters = create_fat32_volume(total_sectors, 0)
        image_kind = "raw FAT32"

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as handle:
        handle.write(final_image)

    print(
        f"Created {os.path.basename(output_path)}: {size_mb}MB, {image_kind}, "
        f"{total_clusters} clusters, {free_clusters} free"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
