"""trk3_demo.py -- the set's timeline, third format, on one small set made by code (queue step 21, 11-04).

What it shows, and what a show's own writer has to put in a TRK3 file (engine/settrack.inc has the format):
  - a mover showing one picture of a sheet, a different one every second and a half      (mover <stem> grid=2x2, the `picture` column)
  - a mover washing over to a colour and fading out                                        (the `r g b wash` and `alpha` columns)
  - a place for scene slot 0's head and eyes to follow                                     (a `look` row)
  - two set actors on the same clip, the second one starting half way through it           (actor ... clip>0.5)

It writes props/trk3_card.rpack, sets/_trk3_demo.set and sets/_trk3_demo.trk beside the engine. Then:
  cd reva_native/scenes && python compile_scene.py _trk3_demo.txt
  python .claude/skills/run-reva/driver.py scene out.mp4 --name _trk3_demo --seconds 6 --fps 30 --subject reva902 --partner off --set _trk3_demo --stage 0,0,0 --new
"""
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src"))
from rpackwrite import rigid_skin, to_png, write_pack

CELL = 64
FONT = {"0": "111101101101111", "1": "010110010010111", "2": "111001111100111", "3": "111001111001111"}   # 3 x 5
COLOURS = [(200, 60, 60), (60, 160, 70), (60, 90, 200), (210, 180, 50)]


def sheet():
    """four pictures in a 2 x 2 sheet: a colour and a digit each, 0 and 1 on the top row"""
    w = h = CELL * 2
    px = bytearray(w * h * 4)
    for k in range(4):
        ox, oy = (k % 2) * CELL, (k // 2) * CELL
        for y in range(CELL):
            for x in range(CELL):
                c = COLOURS[k]
                gx, gy = (x - 20) // 8, (y - 12) // 8
                if 0 <= gx < 3 and 0 <= gy < 5 and FONT[str(k)][gy * 3 + gx] == "1":
                    c = (245, 245, 240)
                i = ((oy + y) * w + ox + x) * 4
                px[i:i + 4] = bytes(c) + b"\xff"
    return to_png(bytes(px), w, h)


def card(dst):
    """a card standing on the ground, facing +z, its whole texture across it"""
    pos = [-0.8, 0, 0, 0.8, 0, 0, 0.8, 1.2, 0, -0.8, 1.2, 0]
    nrm = [0, 0, 1] * 4
    uv = [0, 1, 1, 1, 1, 0, 0, 0]
    idx = [0, 1, 2, 0, 2, 3]
    nodes = [{"name": "Armature", "parent": -1, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "card", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(4, 0)
    mesh = {"name": "trk3_card", "node": 1, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map,
            "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0}
    write_pack(dst, nodes, [mesh], [], [sheet()], verbose=False)


def track(dst, fps=30, frames=180):
    """TRK3: no lights, no beams, two movers, one look"""
    rows = bytearray()
    for f in range(frames):
        u = f / (frames - 1)
        rows += struct.pack("<3f", 0.30, 0.34, 0.42)                                                     # the sky
        # mover 0: the card on the left shows a new picture every second and a half
        rows += struct.pack("<15f", -1.3, 0.0, -1.5, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0.0, 1.0, float((f // 45) % 4))
        # mover 1: the card on the right keeps picture 0, washes over to red and thins out
        rows += struct.pack("<15f", 1.3, 0.0, -1.5, 0, 0, 0, 1, 1, 1, 1.0, 0.1, 0.1, u, 1.0 - 0.8 * u, 0.0)
        # mover 2: a card that goes up and comes down again, with a curtain pinned under it (queue step 23: cloth on a prop)
        rows += struct.pack("<15f", -3.7, 2.0 + 1.3 * math.sin(math.pi * u), -1.5, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0.0, 1.0, 1.0)
        # look 0: something for her to follow, round in front of her and up and down
        rows += struct.pack("<4f", 2.6 * math.sin(2 * math.pi * f / 90), 2.6 + 1.3 * math.sin(2 * math.pi * f / 60), 4.0, 1.0)
    with open(dst, "wb") as o:
        o.write(("TRK3 %g %d 0 0 3 1\n" % (fps, frames)).encode())
        o.write(rows)


SET = """# _trk3_demo -- made by scenes/trk3_demo.py: the timeline's third format on one small set
sun 50 140 1.0
sky 0.30 0.34 0.42
mover trk3_card grid=2x2
mover trk3_card grid=2x2
mover trk3_card grid=2x2
track sets/_trk3_demo.trk
# a curtain pinned along the bottom of the third card, in that card's own space: it goes where the card goes
curtain 0 0 0 1.6 1.5 0 0.75 0.12 0.12 mover=2
# the same clip twice: the second starts half way through it
actor v1_reva902 z_rm_sk_push      3.4 0 -1.0 0 1.0
actor v1_reva902 z_rm_sk_push>0.5  5.0 0 -1.0 0 1.0
"""


def main():
    card(os.path.join(NATIVE, "props", "trk3_card.rpack"))
    track(os.path.join(NATIVE, "sets", "_trk3_demo.trk"))
    with open(os.path.join(NATIVE, "sets", "_trk3_demo.set"), "w", newline="\n") as o:
        o.write(SET)
    print("wrote props/trk3_card.rpack, sets/_trk3_demo.set, sets/_trk3_demo.trk")


if __name__ == "__main__":
    main()
