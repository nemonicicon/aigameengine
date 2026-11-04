#!/usr/bin/env python3
"""make_assets.py -- the rocks game's sprites and sounds, made by code. Run it from anywhere; it writes beside itself: sheet.png, a strip of 16 x 16 cells on transparency (ship, rock, shot, spark, white so the game can tint them), and scenes/sfx/*.wav. --check says whether what's on disk is what this makes."""
import io
import math
import os
import struct
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src"))
from rpackwrite import to_png

CELLS = [
    [  # the ship
        "................", ".......##.......", ".......##.......", "......####......", "......####......", ".....######.....", ".....######.....", "....########....",
        "....##.##.##....", "...###.##.###...", "...##########...", "..############..", "..###.####.###..", ".###..####..###.", ".##....##....##.", "................",
    ],
    [  # a rock
        "................", ".....######.....", "...##########...", "..####.#######..", ".#####..#######.", ".##############.", ".###.##########.", "####..##########",
        "##########.#####", ".#########..###.", ".##############.", ".#####.########.", "..###..#######..", "...##########...", ".....######.....", "................",
    ],
    [  # a shot
        "................", "................", "................", ".......##.......", ".......##.......", ".......##.......", ".......##.......", ".......##.......",
        ".......##.......", ".......##.......", ".......##.......", ".......##.......", "................", "................", "................", "................",
    ],
    [  # a spark
        "................", "..#....#.....#..", "...#...#....#...", "....#..#...#....", ".....#.#..#.....", "......###.......", "#######.########", "......###.......",
        ".....#.#.#......", "....#..#..#.....", "...#...#...#....", "..#....#....#...", ".#.....#.....#..", "................", "................", "................",
    ],
]
RATE = 22050


def sheet():
    w, h = 16 * len(CELLS), 16
    px = bytearray()
    for y in range(h):
        for c in CELLS:
            for x in range(16):
                px += b"\xff\xff\xff\xff" if c[y][x] == "#" else b"\x00\x00\x00\x00"
    return to_png(bytes(px), w, h)


def noise():
    """the same dice every run, so the files don't change"""
    x = 12345
    while True:
        x = (x * 1103515245 + 12345) & 0x7fffffff
        yield (x >> 16) / 16384.0 - 1.0


def sweep(secs, f0, f1, level):
    """a square wave falling from f0 to f1, dying away"""
    out, phase = [], 0.0
    n = int(secs * RATE)
    for i in range(n):
        t = i / n
        phase += (f0 + (f1 - f0) * t) / RATE
        out.append(level * (1.0 if phase % 1.0 < 0.5 else -1.0) * (1.0 - t) ** 2)
    return out


def crash(secs, hold0, hold1, level):
    """noise held for longer and longer, so it falls in pitch as it dies away"""
    out, dice, s, left = [], noise(), 0.0, 0
    n = int(secs * RATE)
    for i in range(n):
        t = i / n
        if left <= 0:
            s, left = next(dice), int(hold0 + (hold1 - hold0) * t)
        left -= 1
        out.append(level * s * (1.0 - t) ** 2)
    return out


def wav(samples):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))
    return b.getvalue()


def mix(a, b):
    return [(a[i] if i < len(a) else 0.0) + (b[i] if i < len(b) else 0.0) for i in range(max(len(a), len(b)))]


def files():
    return {
        "sheet.png": sheet(),
        "scenes/sfx/shot.wav": wav(sweep(0.09, 1500.0, 500.0, 0.45)),
        "scenes/sfx/kill.wav": wav(crash(0.28, 2, 18, 0.7)),
        "scenes/sfx/hurt.wav": wav(mix(sweep(0.45, 220.0, 55.0, 0.5), crash(0.45, 6, 40, 0.5))),
        "scenes/sfx/banner.wav": wav(sweep(0.08, 880.0, 880.0, 0.3) + sweep(0.16, 1320.0, 1320.0, 0.3)),   # the engine pings when a banner goes up
    }


def main():
    bad = 0
    for name, data in files().items():
        path = os.path.join(HERE, name)
        if "--check" in sys.argv:
            same = os.path.exists(path) and open(path, "rb").read() == data
            print(name, "same" if same else "DIFFERS")
            bad += not same
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(data)
            print(path, len(data))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
