#!/usr/bin/env python3
"""make_assets.py -- the tower game's level, body and sounds, made by code. Run it from anywhere; it writes beside itself: props/tw_level.rpack and the three things the game makes as it runs, packs/v1_tower.rpack, sets/tower.set, scenes/sfx/*.wav. --check says whether what's on disk is what this makes.
The level is plain boxes and ramps with nothing special about them: the engine works out where a body can walk from their triangles (engine/spans.inc), which is what `climb` in the set asks for. Sizes here are club units, 1.75 a metre."""
import importlib.util
import io
import math
import os
import struct
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src"))
from rpackwrite import rigid_skin, to_png, write_pack

spec = importlib.util.spec_from_file_location("sample_body", os.path.join(HERE, "..", "sample", "make_body.py"))
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)

RATE = 22050
SWATCH = [(92, 96, 104), (78, 82, 90), (150, 120, 84), (120, 132, 150), (168, 88, 60), (70, 92, 70), (196, 190, 170)]
FLOOR_A, FLOOR_B, DECK, RAMP, STAIR, WALL, BRIDGE = range(7)


def texture():
    n = len(SWATCH)
    px = bytearray()
    for _ in range(4):
        for c in SWATCH:
            for _ in range(4):
                px += bytes(c) + b"\xff"
    return to_png(bytes(px), n * 4, 4)


class Level:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def quad(self, a, b, c, d, swatch):
        """corners in order, counter-clockwise from the side it faces"""
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = d[0] - a[0], d[1] - a[1], d[2] - a[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        l = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        base = len(self.pos) // 3
        u = (swatch + 0.5) / len(SWATCH)
        for p in (a, b, c, d):
            self.pos += p
            self.nrm += (nx / l, ny / l, nz / l)
            self.uv += (u, 0.5)
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def box(self, x0, x1, y0, y1, z0, z1, swatch):
        self.quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), swatch)   # top
        self.quad((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), swatch)   # bottom
        self.quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), swatch)   # +z
        self.quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), swatch)   # -z
        self.quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), swatch)   # +x
        self.quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), swatch)   # -x

    def ramp_x(self, x_low, x_high, y_low, y_high, z0, z1, swatch):
        """a solid wedge rising along x"""
        a, b = (x_low, y_low), (x_high, y_high)
        if x_low < x_high:
            self.quad((a[0], a[1], z1), (b[0], b[1], z1), (b[0], b[1], z0), (a[0], a[1], z0), swatch)
            self.quad((b[0], 0, z1), (b[0], 0, z0), (b[0], b[1], z0), (b[0], b[1], z1), swatch)
        else:
            self.quad((b[0], b[1], z1), (a[0], a[1], z1), (a[0], a[1], z0), (b[0], b[1], z0), swatch)
            self.quad((b[0], 0, z0), (b[0], 0, z1), (b[0], b[1], z1), (b[0], b[1], z0), swatch)
        self.quad((a[0], 0, z1), (b[0], 0, z1), (b[0], b[1], z1), (a[0], a[1], z1), swatch)
        self.quad((b[0], 0, z0), (a[0], 0, z0), (a[0], a[1], z0), (b[0], b[1], z0), swatch)


# the shape of the level, and the marks that go with it. The hero starts at +z, looking towards -z
DECK_Y, TOP_Y = 3.0, 6.5
def build_level():
    L = Level()
    # the ground, in squares you can see yourself moving across
    for i in range(-15, 15):
        for j in range(-15, 15):
            x0, z0 = i * 3.0, j * 3.0
            L.quad((x0, 0, z0 + 3), (x0 + 3, 0, z0 + 3), (x0 + 3, 0, z0), (x0, 0, z0), FLOOR_A if (i + j) % 2 else FLOOR_B)
    L.box(-8, 8, 0, DECK_Y, -16, -4, DECK)                       # the first deck
    L.ramp_x(22, 8, 0, DECK_Y, -12, -8, RAMP)                    # a ramp up to it from the side
    L.box(-8, 0, DECK_Y, TOP_Y, -16, -4, DECK)                   # the second deck, on top of the first
    for s in range(10):                                          # stairs from the first deck to the second, ten steps
        L.box(6 - (s + 1) * 0.6, 6 - s * 0.6, DECK_Y, DECK_Y + (s + 1) * 0.35, -16, -10, STAIR)
    L.box(-5, -1, TOP_Y - 0.3, TOP_Y, -4, 6, BRIDGE)             # a bridge you can walk under
    L.box(-6, 0, 0, TOP_Y, 6, 12, DECK)                          # to a tower at the far end
    L.box(14, 30, 0, 5, 2, 2.5, WALL)                            # a wall with a gap to go round
    L.box(14, 30, 0, 5, -22, -21.5, WALL)
    nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "level", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(len(L.pos) // 3, 0)
    ibm = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    mesh = {"name": "level", "node": 1, "pos": L.pos, "nrm": L.nrm, "uv": L.uv, "idx": L.idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": ibm, "tex": 0, "nearest": True}
    return write_to_bytes(nodes, [mesh], [], [texture()])


def build_box(sx, sy, sz, swatch):
    """something the game puts in the world as it runs: one box, with its feet at y 0"""
    L = Level()
    L.box(-sx / 2, sx / 2, 0, sy, -sz / 2, sz / 2, swatch)
    nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "box", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(len(L.pos) // 3, 0)
    ibm = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    mesh = {"name": "box", "node": 1, "pos": L.pos, "nrm": L.nrm, "uv": L.uv, "idx": L.idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": ibm, "tex": 0, "nearest": True}
    return write_to_bytes(nodes, [mesh], [], [texture()])


def write_to_bytes(nodes, meshes, clips, textures):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.rpack")
        write_pack(p, nodes, meshes, clips, textures, verbose=False)
        return open(p, "rb").read()


SET = """# the tower game's level. `climb` says its surfaces are walked on: engine/spans.inc reads where from the triangles. make_assets.py writes this file.
climb
prop tw_level 0 0 0 0 1
mark start 0 0 16 180
mark spawn 22 0 24 180
mark beacon0 4 %(d)s -6 0
mark beacon1 -4 %(t)s -12 0
mark beacon2 -3 %(t)s 9 0
trigger beacon0 4 %(d)s -6 4 4 4
trigger beacon1 -4 %(t)s -12 4 4 4
trigger beacon2 -3 %(t)s 9 4 4 4
mark route0 26 0 -10 0
mark route1 10 0 -10 0
mark route2 4 0 -6 0
mark route3 8 0 -13 0
mark route4 -1 0 -13 0
mark route5 -3 0 -12 0
mark route6 -3 0 2 0
mark route7 -3 0 9 0
mark crate0 7.2 %(d)s -5 0
mark crate1 8 0 12 0
mark crate2 8 1.6 12 0
mark crate3 -8 0 14 0
mark lift 3 0 -2 0
""" % {"d": DECK_Y, "t": TOP_Y}


# ---- the body: the sample's figure with three more clips
def punch(u):
    s = math.sin(math.pi * min(u / 0.7, 1.0))   # the fist is furthest out at 0.35, which is where hero.anim's event is
    r = B.arms(0.1, -1.5 * s, 1.1 * (1.0 - s))
    r["mixamorig:Spine"] = B.QY(0.5 * s)
    return r, 0.0


def jump(u):
    k = math.sin(math.pi * u)
    r = B.arms(-0.6 * k, -0.6 * k, 0.5)
    r["mixamorig:LeftUpLeg"] = B.QX(-0.7 * k)
    r["mixamorig:RightUpLeg"] = B.QX(-0.4 * k)
    r["mixamorig:LeftLeg"] = B.QX(1.2 * k)
    r["mixamorig:RightLeg"] = B.QX(0.7 * k)
    return r, 0.0


def fall(u):
    s = math.sin(B.TAU * u)
    r = B.arms(-0.5 + 0.1 * s, -0.5 - 0.1 * s, 0.4)
    r["mixamorig:LeftUpLeg"] = B.QX(-0.25)
    r["mixamorig:RightUpLeg"] = B.QX(0.1)
    r["mixamorig:LeftLeg"] = B.QX(0.5)
    r["mixamorig:RightLeg"] = B.QX(0.3)
    return r, 0.0


def build_body():
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.rpack")
        B.build(p, [B.clip("punch", 16, punch), B.clip("jump", 10, jump), B.clip("fall", 20, fall)])
        return open(p, "rb").read()


# ---- sounds
def noise():
    x = 12345
    while True:
        x = (x * 1103515245 + 12345) & 0x7fffffff
        yield (x >> 16) / 16384.0 - 1.0


def sweep(secs, f0, f1, level):
    out, phase = [], 0.0
    n = int(secs * RATE)
    for i in range(n):
        t = i / n
        phase += (f0 + (f1 - f0) * t) / RATE
        out.append(level * math.sin(phase * 2 * math.pi) * (1.0 - t) ** 2)
    return out


def thud(secs, level):
    out, dice, s = [], noise(), 0.0
    n = int(secs * RATE)
    for i in range(n):
        t = i / n
        s += (next(dice) - s) * 0.08
        out.append(level * s * (1.0 - t) ** 3)
    return out


def music():
    """eight bars of a slow bass and a few high notes, by code, that loop without a click"""
    beat = int(RATE * 0.5)
    bass = [55.0, 55.0, 65.41, 49.0]
    high = [440.0, 523.25, 659.25, 587.33, 523.25, 440.0, 392.0, 329.63]
    out = [0.0] * (beat * 32)
    for b in range(32):
        f = bass[(b // 8) % 4]
        for i in range(beat * 2 if b % 2 == 0 else 0):
            t = i / RATE
            k = (b * beat + i) % len(out)
            out[k] += 0.28 * math.sin(2 * math.pi * f * t) * min(1.0, i / 400.0) * (1.0 - i / (beat * 2)) ** 0.5
        if b % 4 == 2:
            g = high[(b // 4) % 8]
            for i in range(beat):
                t = i / RATE
                k = (b * beat + i) % len(out)
                out[k] += 0.1 * math.sin(2 * math.pi * g * t) * min(1.0, i / 200.0) * (1.0 - i / beat) ** 2
    return out


def wav(samples):
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767)) for s in samples))
    return b.getvalue()


def files():
    return {
        "props/tw_level.rpack": build_level(),
        "props/tw_crate.rpack": build_box(1.6, 1.6, 1.6, STAIR),
        "props/tw_beacon.rpack": build_box(0.5, 3.0, 0.5, BRIDGE),
        "props/tw_lift.rpack": build_box(3.0, 0.4, 3.0, RAMP),
        "packs/v1_tower.rpack": build_body(),
        "sets/tower.set": SET.encode(),
        "scenes/sfx/hit.wav": wav(thud(0.2, 2.5)),
        "scenes/sfx/step.wav": wav(thud(0.07, 1.2)),
        "scenes/sfx/ping.wav": wav(sweep(0.35, 880.0, 1320.0, 0.5)),
        "scenes/sfx/banner.wav": wav(sweep(0.2, 660.0, 880.0, 0.3)),
        "music.wav": wav(music()),
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
