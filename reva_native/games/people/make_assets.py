#!/usr/bin/env python3
"""make_assets.py -- what the character creator needs on disk, which is hardly anything: a body to boot with and a floor. Its people are built while it runs (engine/people.inc). Run it from anywhere; it writes beside itself.

usage: make_assets.py            writes them
       make_assets.py --check    compares what's on disk, byte for byte"""
import importlib.util
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "skate_src"))
from rpackwrite import rigid_skin, to_png, write_pack

spec = importlib.util.spec_from_file_location("sample_body", os.path.join(HERE, "..", "sample", "make_body.py"))
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)

SET = """# the character creator's floor
climb
prop pp_floor 0 0 0 0 1
"""


def build_floor(dst):
    w, h = 8, 8
    px = bytearray()
    for y in range(h):
        for x in range(w):
            c = (112, 108, 100) if (x // 4 + y // 4) % 2 else (100, 96, 90)
            px += bytes(c) + b"\xff"
    pos, nrm, uv, idx = [], [], [], []
    S = 30.0
    for p, t in (((-S, 0, S), (0, 16)), ((S, 0, S), (16, 16)), ((S, 0, -S), (16, 0)), ((-S, 0, -S), (0, 0))):
        pos.extend(p)
        nrm.extend((0, 1, 0))
        uv.extend(t)
    idx.extend((0, 1, 2, 0, 2, 3))
    nodes = [{"name": "Armature", "parent": -1, "kind": 0, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "floor", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    joints, weights, bone_map = rigid_skin(4, 0)
    mesh = {"name": "pp_floor", "node": 1, "pos": pos, "nrm": nrm, "uv": uv, "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0}
    write_pack(dst, nodes, [mesh], [], [to_png(bytes(px), w, h)], verbose=False)


def pack_bytes(fn):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.rpack")
        fn(p)
        return open(p, "rb").read()


def files():
    return {"packs/v1_boot.rpack": pack_bytes(B.build), "props/pp_floor.rpack": pack_bytes(build_floor), "sets/people.set": SET.encode()}


def main():
    bad = 0
    for name, data in files().items():
        path = os.path.join(HERE, name)
        if "--check" in sys.argv:
            same = os.path.exists(path) and open(path, "rb").read() == data
            print(name + ": " + ("same as the code builds" if same else "DIFFERS"))
            bad += not same
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(data)
            print(path, len(data), "bytes")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
