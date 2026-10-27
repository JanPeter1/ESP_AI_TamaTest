"""Convert the CC0 RGB PNG sprites into small, transparent LVGL C images."""

from pathlib import Path
import struct
import zlib

ASSET_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = ASSET_DIR.parent
AVATAR_SIZE = 48


def paeth(left: int, above: int, upper_left: int) -> int:
    estimate = left + above - upper_left
    distances = (abs(estimate - left), abs(estimate - above), abs(estimate - upper_left))
    return (left, above, upper_left)[distances.index(min(distances))]


def read_rgb_png(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a PNG: {path}")
    offset = 8
    compressed = bytearray()
    width = height = color_type = bit_depth = interlace = 0
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4 : offset + 8]
        chunk = data[offset + 8 : offset + 8 + size]
        offset += size + 12
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", chunk
            )
            if bit_depth != 8 or color_type not in (2, 6) or interlace:
                raise ValueError(f"Expected non-interlaced 8-bit RGB/RGBA PNG: {path}")
        elif kind == b"IDAT":
            compressed.extend(chunk)
        elif kind == b"IEND":
            break

    channels = 3 if color_type == 2 else 4
    stride = width * channels
    source = zlib.decompress(compressed)
    rows: list[bytearray] = []
    cursor = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = source[cursor]
        cursor += 1
        row = bytearray(source[cursor : cursor + stride])
        cursor += stride
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = previous[i]
            upper_left = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                row[i] = (row[i] + left) & 0xFF
            elif filter_type == 2:
                row[i] = (row[i] + above) & 0xFF
            elif filter_type == 3:
                row[i] = (row[i] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                row[i] = (row[i] + paeth(left, above, upper_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"Unsupported PNG filter {filter_type}")
        rows.append(row)
        previous = row

    pixels = []
    for row in rows:
        for x in range(width):
            i = x * channels
            pixels.append((row[i], row[i + 1], row[i + 2]))
    return width, height, pixels


def make_avatar(path: Path) -> bytes:
    width, height, pixels = read_rgb_png(path)
    opaque = [
        (i % width, i // width)
        for i, (red, green, blue) in enumerate(pixels)
        if min(red, green, blue) < 248
    ]
    if not opaque:
        raise ValueError(f"No non-white pixels in {path}")
    left = min(x for x, _ in opaque)
    right = max(x for x, _ in opaque) + 1
    top = min(y for _, y in opaque)
    bottom = max(y for _, y in opaque) + 1
    crop_w, crop_h = right - left, bottom - top
    scale = min(AVATAR_SIZE / crop_w, AVATAR_SIZE / crop_h)
    scaled_w, scaled_h = max(1, round(crop_w * scale)), max(1, round(crop_h * scale))
    origin_x = (AVATAR_SIZE - scaled_w) // 2
    origin_y = (AVATAR_SIZE - scaled_h) // 2

    rgb = bytearray(AVATAR_SIZE * AVATAR_SIZE * 2)
    alpha = bytearray(AVATAR_SIZE * AVATAR_SIZE)
    for dy in range(scaled_h):
        sy = top + min(crop_h - 1, int(dy * crop_h / scaled_h))
        for dx in range(scaled_w):
            sx = left + min(crop_w - 1, int(dx * crop_w / scaled_w))
            red, green, blue = pixels[sy * width + sx]
            out = (origin_y + dy) * AVATAR_SIZE + origin_x + dx
            color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            rgb[out * 2] = color & 0xFF
            rgb[out * 2 + 1] = color >> 8
            alpha[out] = 0 if min(red, green, blue) >= 248 else 0xFF
    return bytes(rgb + alpha)


def c_bytes(name: str, data: bytes) -> str:
    lines = []
    for start in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in data[start : start + 16]))
    return f"static const uint8_t {name}_data[] = {{\n" + ",\n".join(lines) + "\n};\n"


def main() -> None:
    sprites = {
        "cyd_avatar_cat": ASSET_DIR / "pixel_cat_cc0.png",
        "cyd_avatar_dog": ASSET_DIR / "pixel_dog_cc0.png",
    }
    source = [
        '#include "cyd_avatars.h"',
        "",
        "#define AVATAR_SIZE 48",
        "#define AVATAR_STRIDE (AVATAR_SIZE * 2)",
        "",
    ]
    for name, path in sprites.items():
        image = make_avatar(path)
        source.append(c_bytes(name, image))
        source.append(
            f"const lv_image_dsc_t {name} = {{\n"
            "    .header = {\n"
            "        .magic = LV_IMAGE_HEADER_MAGIC,\n"
            "        .cf = LV_COLOR_FORMAT_RGB565A8,\n"
            "        .w = AVATAR_SIZE,\n"
            "        .h = AVATAR_SIZE,\n"
            "        .stride = AVATAR_STRIDE,\n"
            "    },\n"
            f"    .data_size = sizeof({name}_data),\n"
            f"    .data = {name}_data,\n"
            "};\n"
        )
    source.append(
        "const lv_image_dsc_t *cyd_avatar_get(uint8_t avatar_id) {\n"
        "    return avatar_id == 1 ? &cyd_avatar_dog : &cyd_avatar_cat;\n"
        "}\n"
    )
    (OUTPUT_DIR / "cyd_avatars.c").write_text("\n".join(source), encoding="utf-8")
    (OUTPUT_DIR / "cyd_avatars.h").write_text(
        "#pragma once\n\n#include <stdint.h>\n#include \"lvgl.h\"\n\n"
        "extern const lv_image_dsc_t cyd_avatar_cat;\n"
        "extern const lv_image_dsc_t cyd_avatar_dog;\n"
        "const lv_image_dsc_t *cyd_avatar_get(uint8_t avatar_id);\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
