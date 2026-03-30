#!/usr/bin/env python3

import os
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
TARGET_ICON_SIZE = 32


def slugify(name):
    out = []
    dash = False
    for ch in name.lower():
        if ("a" <= ch <= "z") or ("0" <= ch <= "9"):
            out.append(ch)
            dash = False
        elif not dash and out:
            out.append("-")
            dash = True
    while out and out[-1] == "-":
        out.pop()
    return "".join(out)


def paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def unfilter_png(raw, width, height, bpp):
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    off = 0

    for _ in range(height):
        filt = raw[off]
        off += 1
        cur = bytearray(raw[off:off + stride])
        off += stride

        if filt == 1:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + left) & 0xFF
        elif filt == 2:
            for i in range(stride):
                cur[i] = (cur[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                up = prev[i]
                cur[i] = (cur[i] + ((left + up) // 2)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                left = cur[i - bpp] if i >= bpp else 0
                up = prev[i]
                up_left = prev[i - bpp] if i >= bpp else 0
                cur[i] = (cur[i] + paeth(left, up, up_left)) & 0xFF
        elif filt != 0:
            raise ValueError(f"unsupported PNG filter {filt}")

        rows.append(bytes(cur))
        prev = cur

    return rows


def decode_png(blob):
    if not blob.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG")

    off = 8
    width = 0
    height = 0
    bit_depth = 0
    color_type = 0
    interlace = 0
    palette = []
    palette_alpha = []
    idat = bytearray()

    while off + 8 <= len(blob):
        length = struct.unpack_from(">I", blob, off)[0]
        off += 4
        chunk_type = blob[off:off + 4]
        off += 4
        chunk_data = blob[off:off + length]
        off += length + 4

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk_data)
        elif chunk_type == b"PLTE":
            palette = [tuple(chunk_data[i:i + 3]) for i in range(0, len(chunk_data), 3)]
        elif chunk_type == b"tRNS":
            palette_alpha = list(chunk_data)
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if bit_depth != 8 or interlace != 0:
        raise ValueError("unsupported PNG format")

    if color_type == 6:
        channels = 4
    elif color_type == 2:
        channels = 3
    elif color_type == 3:
        channels = 1
    else:
        raise ValueError("unsupported PNG color type")

    raw = zlib.decompress(bytes(idat))
    rows = unfilter_png(raw, width, height, channels)
    pixels = []

    if color_type == 6:
        for row in rows:
            for i in range(0, len(row), 4):
                r, g, b, a = row[i:i + 4]
                pixels.append((a << 24) | (r << 16) | (g << 8) | b)
    elif color_type == 2:
        for row in rows:
            for i in range(0, len(row), 3):
                r, g, b = row[i:i + 3]
                pixels.append(0xFF000000 | (r << 16) | (g << 8) | b)
    else:
        for row in rows:
            for idx in row:
                if idx >= len(palette):
                    pixels.append(0)
                    continue
                r, g, b = palette[idx]
                a = palette_alpha[idx] if idx < len(palette_alpha) else 0xFF
                pixels.append((a << 24) | (r << 16) | (g << 8) | b)

    return width, height, pixels


def decode_dib(blob, width_hint, height_hint, bit_count_hint):
    if len(blob) < 40:
        raise ValueError("short DIB")

    header_size = struct.unpack_from("<I", blob, 0)[0]
    if header_size < 40:
        raise ValueError("unsupported DIB header")

    width = struct.unpack_from("<i", blob, 4)[0]
    height_total = struct.unpack_from("<i", blob, 8)[0]
    planes = struct.unpack_from("<H", blob, 12)[0]
    bit_count = struct.unpack_from("<H", blob, 14)[0]
    compression = struct.unpack_from("<I", blob, 16)[0]

    if planes != 1 or compression not in (0, 3):
        raise ValueError("unsupported DIB encoding")

    width = width if width > 0 else width_hint
    actual_height = abs(height_total) // 2
    if actual_height == 0:
        actual_height = height_hint
    top_down = height_total < 0

    dib_off = header_size
    pixels = [0] * (width * actual_height)

    if bit_count == 32:
        row_stride = width * 4
        for y in range(actual_height):
            src_y = y if top_down else (actual_height - 1 - y)
            row_off = dib_off + (src_y * row_stride)
            row = blob[row_off:row_off + row_stride]
            for x in range(width):
                b = row[x * 4 + 0]
                g = row[x * 4 + 1]
                r = row[x * 4 + 2]
                a = row[x * 4 + 3]
                if a == 0 and (r or g or b):
                    a = 0xFF
                pixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b
        return width, actual_height, pixels

    if bit_count != 24:
        raise ValueError("unsupported DIB bit depth")

    row_stride = ((width * 3 + 3) // 4) * 4
    xor_size = row_stride * actual_height
    mask_row_stride = ((width + 31) // 32) * 4
    mask_off = dib_off + xor_size

    for y in range(actual_height):
        src_y = y if top_down else (actual_height - 1 - y)
        row_off = dib_off + (src_y * row_stride)
        mask_y = y if top_down else (actual_height - 1 - y)
        mask_row_off = mask_off + (mask_y * mask_row_stride)
        row = blob[row_off:row_off + row_stride]
        mask_row = blob[mask_row_off:mask_row_off + mask_row_stride]

        for x in range(width):
            b = row[x * 3 + 0]
            g = row[x * 3 + 1]
            r = row[x * 3 + 2]
            mask_byte = mask_row[x // 8]
            mask_bit = 7 - (x % 8)
            a = 0x00 if ((mask_byte >> mask_bit) & 1) else 0xFF
            pixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b

    return width, actual_height, pixels


def decode_ico(path):
    data = open(path, "rb").read()
    if data.startswith(PNG_SIGNATURE):
        return decode_png(data)
    if len(data) < 6:
        raise ValueError("short ICO")

    reserved, kind, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or kind != 1 or count == 0:
        raise ValueError("invalid ICO")

    best = None

    for idx in range(count):
        off = 6 + idx * 16
        width, height, _, _, planes, bpp, size, image_off = struct.unpack_from("<BBBBHHII", data, off)
        width = width or 256
        height = height or 256
        blob = data[image_off:image_off + size]

        try:
            if blob.startswith(PNG_SIGNATURE):
                img_w, img_h, pixels = decode_png(blob)
            else:
                img_w, img_h, pixels = decode_dib(blob, width, height, bpp)
        except Exception:
            continue

        score = img_w * img_h
        if best is None or score > best[0]:
            best = (score, img_w, img_h, pixels)

    if best is None:
        raise ValueError(f"no supported images in {path}")

    return best[1], best[2], best[3]


def scale_icon(width, height, pixels, target):
    scaled = []
    for y in range(target):
        src_y = (y * height) // target
        for x in range(target):
            src_x = (x * width) // target
            scaled.append(pixels[src_y * width + src_x])
    return scaled


def emit_icon_array(lines, name, pixels):
    lines.append(f"static const uint32_t icon_{name}[{len(pixels)}] = {{")
    for i in range(0, len(pixels), 8):
        chunk = ", ".join(f"0x{val:08X}u" for val in pixels[i:i + 8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")


def generate(icon_dir, out_path):
    icons = []
    if os.path.isdir(icon_dir):
        for entry in sorted(os.listdir(icon_dir)):
            if not entry.lower().endswith(".ico"):
                continue
            if ":zone.identifier" in entry.lower():
                continue
            slug = slugify(os.path.splitext(entry)[0])
            if not slug:
                continue
            try:
                width, height, pixels = decode_ico(os.path.join(icon_dir, entry))
            except Exception as exc:
                print(f"warning: skipping {entry}: {exc}", file=sys.stderr)
                continue
            icons.append((slug, scale_icon(width, height, pixels, TARGET_ICON_SIZE)))

    lines = [
        '#include <stdint.h>',
        '',
        '#include "ascii_util.h"',
        '#include "icon_assets.h"',
        '',
    ]

    for slug, pixels in icons:
        emit_icon_array(lines, slug.replace("-", "_"), pixels)

    lines.append("static const struct IconAsset icon_assets[] = {")
    for slug, _ in icons:
        lines.append(
            f'    {{"{slug}", {TARGET_ICON_SIZE}, {TARGET_ICON_SIZE}, icon_{slug.replace("-", "_")}}},'
        )
    lines.append("};")
    lines.append("")
    lines.append("const struct IconAsset *icon_assets_find(const char *name) {")
    lines.append("    uint32_t i;")
    lines.append("")
    lines.append("    for (i = 0; i < (uint32_t)(sizeof(icon_assets) / sizeof(icon_assets[0])); i++) {")
    lines.append("        if (ascii_streq(icon_assets[i].name, name)) {")
    lines.append("            return &icon_assets[i];")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return (const struct IconAsset *)0;")
    lines.append("}")
    lines.append("")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="ascii") as out:
        out.write("\n".join(lines))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_icons.py <icon-dir> <out-c>")
    generate(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
