#!/usr/bin/env python3

import os
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
CURSOR_MAX_DIM = 32


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


def decode_dib(blob, width_hint, height_hint):
    if len(blob) < 40:
        raise ValueError("short DIB")

    header_size = struct.unpack_from("<I", blob, 0)[0]
    width = struct.unpack_from("<i", blob, 4)[0]
    height_total = struct.unpack_from("<i", blob, 8)[0]
    planes = struct.unpack_from("<H", blob, 12)[0]
    bit_count = struct.unpack_from("<H", blob, 14)[0]
    compression = struct.unpack_from("<I", blob, 16)[0]

    if header_size < 40 or planes != 1 or compression not in (0, 3):
        raise ValueError("unsupported DIB")

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


def decode_cur_blob(blob):
    if blob.startswith(PNG_SIGNATURE):
        width, height, pixels = decode_png(blob)
        return width, height, 0, 0, pixels

    reserved, kind, count = struct.unpack_from("<HHH", blob, 0)
    if reserved != 0 or kind not in (1, 2) or count == 0:
        raise ValueError("invalid icon/cursor blob")

    best = None
    for i in range(count):
        off = 6 + i * 16
        width, height, _, _, hot_x, hot_y, size, img_off = struct.unpack_from("<BBBBHHII", blob, off)
        width = width or 256
        height = height or 256
        chunk = blob[img_off:img_off + size]

        try:
            if chunk.startswith(PNG_SIGNATURE):
                img_w, img_h, pixels = decode_png(chunk)
            else:
                img_w, img_h, pixels = decode_dib(chunk, width, height)
        except Exception:
            continue

        score = img_w * img_h
        if best is None or score < best[0]:
            best = (score, img_w, img_h, hot_x, hot_y, pixels)

    if best is None:
        raise ValueError("no supported cursor frame")
    return best[1], best[2], best[3], best[4], best[5]


def decode_cur(path):
    return decode_cur_blob(open(path, "rb").read())


def scale_cursor_frame(width, height, hot_x, hot_y, pixels, max_dim):
    if width <= 0 or height <= 0:
        raise ValueError("invalid cursor size")

    if width <= max_dim and height <= max_dim:
        return width, height, hot_x, hot_y, pixels

    if width >= height:
        new_width = max_dim
        new_height = max(1, (height * max_dim) // width)
    else:
        new_height = max_dim
        new_width = max(1, (width * max_dim) // height)

    scaled = []
    for y in range(new_height):
        src_y = (y * height) // new_height
        for x in range(new_width):
            src_x = (x * width) // new_width
            scaled.append(pixels[src_y * width + src_x])

    new_hot_x = (hot_x * new_width) // width
    new_hot_y = (hot_y * new_height) // height
    if new_hot_x >= new_width:
        new_hot_x = new_width - 1
    if new_hot_y >= new_height:
        new_hot_y = new_height - 1

    return new_width, new_height, new_hot_x, new_hot_y, scaled


def parse_riff_chunks(blob, start, end):
    out = []
    off = start
    while off + 8 <= end:
        cid = blob[off:off + 4]
        size = struct.unpack_from("<I", blob, off + 4)[0]
        data_start = off + 8
        data_end = min(data_start + size, len(blob))
        out.append((cid, data_start, data_end))
        off = data_start + size + (size & 1)
    return out


def decode_ani(path):
    data = open(path, "rb").read()
    if len(data) < 12 or data[0:4] != b"RIFF" or data[8:12] != b"ACON":
        raise ValueError("invalid ANI")

    chunks = parse_riff_chunks(data, 12, len(data))
    default_rate = 6
    seq = []
    rate = []
    frame_blobs = []

    for cid, data_start, data_end in chunks:
        if cid == b"anih" and data_end - data_start >= 36:
            _, _, _, _, _, _, _, disp_rate, _ = struct.unpack_from("<9I", data, data_start)
            default_rate = disp_rate
        elif cid == b"rate":
            for off in range(data_start, data_end, 4):
                if off + 4 <= data_end:
                    rate.append(struct.unpack_from("<I", data, off)[0])
        elif cid == b"seq ":
            for off in range(data_start, data_end, 4):
                if off + 4 <= data_end:
                    seq.append(struct.unpack_from("<I", data, off)[0])
        elif cid == b"LIST" and data[data_start:data_start + 4] == b"fram":
            for sub_id, sub_start, sub_end in parse_riff_chunks(data, data_start + 4, data_end):
                if sub_id == b"icon":
                    frame_blobs.append(data[sub_start:sub_end])

    decoded = [decode_cur_blob(blob) for blob in frame_blobs]
    if not decoded:
        raise ValueError("no frames in ANI")

    if not seq:
        seq = list(range(len(decoded)))

    frames = []
    for idx, frame_index in enumerate(seq):
        if frame_index < 0 or frame_index >= len(decoded):
            continue
        width, height, hot_x, hot_y, pixels = decoded[frame_index]
        jif = rate[idx] if idx < len(rate) else default_rate
        delay_ticks = max(1, (jif * 100 + 59) // 60)
        frames.append((width, height, hot_x, hot_y, delay_ticks, pixels))

    if not frames:
        raise ValueError("no usable ANI frames")
    return frames


def emit_pixels(lines, symbol, pixels):
    lines.append(f"static const uint32_t {symbol}[{len(pixels)}] = {{")
    for i in range(0, len(pixels), 8):
        chunk = ", ".join(f"0x{val:08X}u" for val in pixels[i:i + 8])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")


def generate(cursor_dir, out_path):
    assets = []
    pixel_defs = []

    if os.path.isdir(cursor_dir):
        for entry in sorted(os.listdir(cursor_dir)):
            path = os.path.join(cursor_dir, entry)
            if not os.path.isfile(path):
                continue
            lower = entry.lower()
            slug = slugify(os.path.splitext(entry)[0])
            if not slug:
                continue

            try:
                if lower.endswith(".cur"):
                    width, height, hot_x, hot_y, pixels = decode_cur(path)
                    width, height, hot_x, hot_y, pixels = scale_cursor_frame(
                        width, height, hot_x, hot_y, pixels, CURSOR_MAX_DIM
                    )
                    assets.append((slug, [(width, height, hot_x, hot_y, 0, pixels)]))
                elif lower.endswith(".ani"):
                    frames = []
                    for width, height, hot_x, hot_y, delay_ticks, pixels in decode_ani(path):
                        width, height, hot_x, hot_y, pixels = scale_cursor_frame(
                            width, height, hot_x, hot_y, pixels, CURSOR_MAX_DIM
                        )
                        frames.append((width, height, hot_x, hot_y, delay_ticks, pixels))
                    assets.append((slug, frames))
            except Exception as exc:
                print(f"warning: skipping {entry}: {exc}", file=sys.stderr)

    lines = [
        '#include <stdint.h>',
        '',
        '#include "ascii_util.h"',
        '#include "cursor_assets.h"',
        '',
    ]

    for slug, frames in assets:
        slug_sym = slug.replace("-", "_")
        for idx, frame in enumerate(frames):
            pixels = frame[5]
            emit_pixels(lines, f"cursor_{slug_sym}_frame_{idx}", pixels)

        lines.append(f"static const struct CursorFrame cursor_{slug_sym}_frames[{len(frames)}] = {{")
        for idx, frame in enumerate(frames):
            width, height, hot_x, hot_y, delay_ticks, _ = frame
            lines.append(
                f"    {{{width}, {height}, {hot_x}, {hot_y}, {delay_ticks}u, cursor_{slug_sym}_frame_{idx}}},"
            )
        lines.append("};")
        lines.append("")

    lines.append("static const struct CursorAsset cursor_assets[] = {")
    for slug, frames in assets:
        slug_sym = slug.replace("-", "_")
        lines.append(f'    {{"{slug}", {len(frames)}u, cursor_{slug_sym}_frames}},')
    lines.append("};")
    lines.append("")
    lines.append("const struct CursorAsset *cursor_assets_find(const char *name) {")
    lines.append("    uint32_t i;")
    lines.append("    for (i = 0; i < (uint32_t)(sizeof(cursor_assets) / sizeof(cursor_assets[0])); i++) {")
    lines.append("        if (ascii_streq(cursor_assets[i].name, name)) {")
    lines.append("            return &cursor_assets[i];")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return (const struct CursorAsset *)0;")
    lines.append("}")
    lines.append("")
    lines.append("const struct CursorAsset *cursor_assets_default(void) {")
    lines.append('    const struct CursorAsset *cur = cursor_assets_find("normal");')
    lines.append("    if (cur != (const struct CursorAsset *)0) {")
    lines.append("        return cur;")
    lines.append("    }")
    if assets:
        lines.append("    return &cursor_assets[0];")
    else:
        lines.append("    return (const struct CursorAsset *)0;")
    lines.append("}")
    lines.append("")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="ascii") as f:
        f.write("\n".join(lines))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_cursors.py <cursor-dir> <out-c>")
    generate(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
