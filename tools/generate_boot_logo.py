#!/usr/bin/env python3

import os
import sys

from PIL import Image

TARGET_WIDTH = 240


def generate(input_path, output_path):
    image = Image.open(input_path).convert("RGB")
    width, height = image.size
    target_height = max(1, (height * TARGET_WIDTH) // width)
    image = image.resize((TARGET_WIDTH, target_height), Image.Resampling.LANCZOS)

    pixels = []
    for r, g, b in image.getdata():
        pixels.append((r << 16) | (g << 8) | b)

    lines = [
        "#include <stdint.h>",
        "",
        '#include "boot_logo_asset.h"',
        "",
        f"static const uint32_t boot_logo_pixels[{len(pixels)}] = {{",
    ]
    for idx in range(0, len(pixels), 8):
        chunk = ", ".join(f"0x{value:06X}u" for value in pixels[idx:idx + 8])
        lines.append(f"    {chunk},")
    lines.extend([
        "};",
        "",
        "const struct BootLogoAsset boot_logo_asset = {",
        f"    {TARGET_WIDTH},",
        f"    {target_height},",
        "    boot_logo_pixels",
        "};",
        "",
    ])

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="ascii") as out:
        out.write("\n".join(lines))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: generate_boot_logo.py <input-image> <out-c>")
    generate(sys.argv[1], sys.argv[2])


if __name__ == "__main__":
    main()
