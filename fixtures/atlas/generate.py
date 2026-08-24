#!/usr/bin/env python3
"""Deterministic synthetic atlas acceptance fixture (M4 slice 4).

Everything here is original content generated from fixed formulas — no
extracted bytes. Same spec -> identical files, and the C++ unit suite, the
fbtool contract gate, and the Godot consumer-boundary test all read these
bytes (or re-derive them from these formulas) independently.

Layout (mirrors the brief exactly):
  TILES000.ART  range 0..3, numtiles 4 (own extent)
    0: zero-dimension empty tile
    1: 8x8, index_a, pivot (1,2)
    2: 5x3 non-square, index_b, pivot (-1,0)
    3: 4x4 animation anchor, index_c, frames=3 type=forward speed=5,
       pivot (-2,3)
  TILES001.ART  range 8..10, numtiles 11 (composed namespace extent)
    deliberate gap 4..7
    8: 6x2, index_d
    9: 2x6, index_e
    10: zero-dimension empty tile
  PALETTE.DAT   768 + int16(8) + 8*256 shade rows + 65536 translucency
  LOOKUP.DAT    1 + 2*257 swaps + 1*768 alternate palette

Index generators are deliberately asymmetric in x and y so that an
accidental row/column transposition or off-by-one placement changes the
bytes everywhere, not just in a corner.

ART layout (COMPATIBILITY_SCOPE row 0d): int32 version/numtiles/start/end,
then widths i16[], heights i16[], picanm i32[], then the pixel blob with
per-tile file order = column-major (pixels[x*h + y]) per the published
description this repo adopts at the atlas boundary.
"""

import struct
from pathlib import Path


def index_a(x, y):
    return (17 + 29 * x + 53 * y) & 0xFF


def index_b(x, y):
    return (0x80 + 3 * x + 5 * y) & 0xFF


def index_c(x, y):
    return (0x40 + 7 * x + 11 * y) & 0xFF


def index_d(x, y):
    return (0xC0 + x + y) & 0xFF


def index_e(x, y):
    return (0x30 + 17 * x + y) & 0xFF


def picanm_raw(frames, anim_type, xc, yc, speed):
    return (
        (frames & 0x3F)
        | ((anim_type & 0x3) << 6)
        | ((xc & 0xFF) << 8)
        | ((yc & 0xFF) << 16)
        | ((speed & 0xF) << 24)
    )


def tile_bytes(w, h, gen):
    # column-major file order
    return bytes(gen(x, y) for x in range(w) for y in range(h))


def art_bytes(tiles, start, numtiles):
    end = start + len(tiles) - 1
    out = struct.pack("<iiii", 1, numtiles, start, end)
    out += struct.pack("<" + "h" * len(tiles), *[t["w"] for t in tiles])
    out += struct.pack("<" + "h" * len(tiles), *[t["h"] for t in tiles])
    out += struct.pack("<" + "i" * len(tiles), *[t["raw"] for t in tiles])
    for t in tiles:
        out += tile_bytes(t["w"], t["h"], t["gen"])
    return out


def palette_dat():
    out = bytes((i * 7) & 0x3F for i in range(768))  # 6-bit components
    out += struct.pack("<h", 8)
    for r in range(8):
        out += bytes((c + 29 * r) & 0xFF for c in range(256))
    out += bytes((i + j * 3) & 0xFF for i in range(256) for j in range(256))
    return out


def lookup_dat():
    out = bytes([2])  # swap count
    out += bytes([4]) + bytes((c * 3) & 0xFF for c in range(256))   # swap 0
    out += bytes([8]) + bytes((c * 11) & 0xFF for c in range(256))  # swap 1
    out += bytes((i * 5) & 0x3F for i in range(768))                # alt palette
    return out


def generate(out_dir: Path):
    art_a = art_bytes(
        [
            {"w": 0, "h": 0, "raw": 0, "gen": None},
            {"w": 8, "h": 8, "raw": picanm_raw(0, 0, 1, 2, 0), "gen": index_a},
            {"w": 5, "h": 3, "raw": picanm_raw(0, 0, -1, 0, 0), "gen": index_b},
            {"w": 4, "h": 4, "raw": picanm_raw(3, 2, -2, 3, 5), "gen": index_c},
        ],
        start=0,
        numtiles=4,
    )
    art_b = art_bytes(
        [
            {"w": 6, "h": 2, "raw": 0, "gen": index_d},
            {"w": 2, "h": 6, "raw": 0, "gen": index_e},
            {"w": 0, "h": 0, "raw": 0, "gen": None},
        ],
        start=8,
        numtiles=11,
    )
    files = {
        "TILES000.ART": art_a,
        "TILES001.ART": art_b,
        "PALETTE.DAT": palette_dat(),
        "LOOKUP.DAT": lookup_dat(),
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, blob in files.items():
        (out_dir / name).write_bytes(blob)
    return files


if __name__ == "__main__":
    here = Path(__file__).resolve().parent
    files = generate(here)
    for name, blob in files.items():
        print(f"{name}: {len(blob)} bytes")
