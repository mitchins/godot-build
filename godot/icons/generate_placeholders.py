#!/usr/bin/env python3
"""Generates placeholder app icons for the M1 sample export presets.

Solid-color PNGs written with the standard library only. Real game icons
replace these when original art exists; placeholders keep exports runnable.
"""

import pathlib
import struct
import zlib

SIZES = [
    "iphone_120x120", "iphone_180x180",
    "ipad_76x76", "ipad_152x152", "ipad_167x167",
    "app_store_1024x1024",
    "spotlight_40x40", "spotlight_80x80",
    "settings_29x29", "settings_58x58",
    "notification_20x20", "notification_40x40", "notification_60x60",
    "settings_87x87", "spotlight_120x120",
    "ipad_40x40", "ipad_80x80", "iphone_80x80",
    "notification_76x76", "notification_114x114",
    "ios_128x128", "ios_136x136", "ios_192x192",
]


def png(path: pathlib.Path, size: int, rgb: tuple) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = b"".join(b"\x00" + bytes(rgb) * size for _ in range(size))
    payload = (b"\x89PNG\r\n\x1a\n"
               + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
               + chunk(b"IDAT", zlib.compress(raw, 9))
               + chunk(b"IEND", b""))
    path.write_bytes(payload)


def main() -> None:
    out_dir = pathlib.Path(__file__).resolve().parent
    for name in SIZES:
        size = int(name.rsplit("x", 1)[1])
        png(out_dir / f"{name}.png", size, (48, 48, 56))
    print(f"generated {len(SIZES)} placeholder icons in {out_dir}")


if __name__ == "__main__":
    main()
