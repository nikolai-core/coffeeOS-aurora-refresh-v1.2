#!/usr/bin/env python3
import os
import struct
import sys


BLOCK_SIZE = 512
VHD_FOOTER_SIZE = 512
VHD_DISK_TYPE_FIXED = 2


def put_be_u16(buf, offset, value):
    struct.pack_into(">H", buf, offset, value)


def put_be_u32(buf, offset, value):
    struct.pack_into(">I", buf, offset, value)


def put_be_u64(buf, offset, value):
    struct.pack_into(">Q", buf, offset, value)


def build_fixed_vhd(raw_disk):
    total_size = len(raw_disk)
    footer = bytearray(VHD_FOOTER_SIZE)
    geometry_total = total_size // BLOCK_SIZE

    if geometry_total > 65535 * 16 * 255:
        cylinders = 65535
        heads = 16
        sectors = 255
    else:
        sectors = 17
        cylinders_times_heads = geometry_total // sectors
        heads = (cylinders_times_heads + 1023) // 1024
        if heads < 4:
            heads = 4
        if cylinders_times_heads >= (heads * 1024) or heads > 16:
            sectors = 31
            heads = 16
            cylinders_times_heads = geometry_total // sectors
        if cylinders_times_heads >= (heads * 1024):
            sectors = 63
            heads = 16
            cylinders_times_heads = geometry_total // sectors
        cylinders = cylinders_times_heads // heads

    footer[0:8] = b"conectix"
    put_be_u32(footer, 8, 0x00000002)
    put_be_u32(footer, 12, 0x00010000)
    put_be_u64(footer, 16, 0xFFFFFFFFFFFFFFFF)
    put_be_u32(footer, 24, 0)
    footer[28:32] = b"coOS"
    put_be_u32(footer, 32, 0x00010000)
    footer[36:40] = b"Wi2k"
    put_be_u64(footer, 40, total_size)
    put_be_u64(footer, 48, total_size)
    put_be_u16(footer, 56, cylinders)
    footer[58] = heads & 0xFF
    footer[59] = sectors & 0xFF
    put_be_u32(footer, 60, VHD_DISK_TYPE_FIXED)
    put_be_u32(footer, 64, 0)
    footer[68:72] = b"coOS"
    footer[72:88] = b"coffeeOS aurora".ljust(16, b"\x00")

    checksum = 0
    for i in range(VHD_FOOTER_SIZE):
        if 64 <= i < 68:
            continue
        checksum = (checksum + footer[i]) & 0xFFFFFFFF
    put_be_u32(footer, 64, (~checksum) & 0xFFFFFFFF)
    return raw_disk + footer


def strip_fixed_vhd(vhd_data):
    if len(vhd_data) < VHD_FOOTER_SIZE:
        raise ValueError("file too small to be a fixed VHD")
    footer = vhd_data[-VHD_FOOTER_SIZE:]
    if footer[0:8] != b"conectix":
        raise ValueError("missing VHD footer cookie")
    disk_type = struct.unpack_from(">I", footer, 60)[0]
    if disk_type != VHD_DISK_TYPE_FIXED:
        raise ValueError("only fixed VHD is supported")
    original_size = struct.unpack_from(">Q", footer, 48)[0]
    if original_size > len(vhd_data) - VHD_FOOTER_SIZE:
        raise ValueError("invalid VHD size")
    return vhd_data[:original_size]


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("img-to-vhd", "vhd-to-img"):
        print("usage: python3 tools/convert_disk.py [img-to-vhd|vhd-to-img] [input] [output]")
        return 1

    mode = sys.argv[1]
    input_path = sys.argv[2]
    output_path = sys.argv[3]

    with open(input_path, "rb") as handle:
        data = handle.read()

    if mode == "img-to-vhd":
        out = build_fixed_vhd(data)
    else:
        out = strip_fixed_vhd(data)

    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as handle:
        handle.write(out)

    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
