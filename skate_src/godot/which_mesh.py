"""which_mesh.py -- which of an export's meshes has triangles near a point (Godot's metres), and which way they face: for finding what a collision in a
port met (a game's own trace gives the place in the set's metres, Godot's y plus the export's lift).

usage: which_mesh.py EXPORT_DIR NAME X Y Z [--r 0.4] [--lift 0]   (Y in the set's metres; --lift is what the export added, so Godot's y = Y - lift)"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from godot2set import accessor, node_matrix, read_glb   # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("export")
    ap.add_argument("name")
    ap.add_argument("x", type=float)
    ap.add_argument("y", type=float)
    ap.add_argument("z", type=float)
    ap.add_argument("--r", type=float, default=0.4)
    ap.add_argument("--lift", type=float, default=0.0)
    a = ap.parse_args()
    js, bin_ = read_glb(os.path.join(a.export, a.name + ".glb"))
    at = np.array([a.x, a.y - a.lift, a.z])
    found = {}

    def walk(ni, parent):
        nd = js["nodes"][ni]
        W = parent @ node_matrix(nd)
        if "mesh" in nd:
            for pr in js["meshes"][nd["mesh"]]["primitives"]:
                p = accessor(js, bin_, pr["attributes"]["POSITION"]).astype(np.float64)
                p = (W[:3, :3] @ p.T).T + W[:3, 3]
                idx = accessor(js, bin_, pr["indices"]).reshape(-1, 3) if "indices" in pr else np.arange(len(p)).reshape(-1, 3)
                t = p[idx]
                c = t.mean(axis=1)
                lo, hi = t.min(axis=1), t.max(axis=1)
                near = np.all((lo - a.r <= at) & (at <= hi + a.r), axis=1)
                for k in np.nonzero(near)[0]:
                    n = np.cross(t[k, 1] - t[k, 0], t[k, 2] - t[k, 0])
                    l = np.linalg.norm(n)
                    if l < 1e-12:
                        continue
                    n /= l
                    found.setdefault(nd.get("name", "?"), []).append((c[k], n, lo[k], hi[k]))
        for ch in nd.get("children", []):
            walk(ch, W)
    for r in js["scenes"][js.get("scene", 0)]["nodes"]:
        walk(r, np.eye(4))
    for name, tris in found.items():
        print("%s: %d triangles near" % (name, len(tris)))
        for c, n, lo, hi in tris[:8]:
            print("   centre (%.2f %.2f %.2f) normal (%.2f %.2f %.2f)  y %.2f..%.2f" % (c[0], c[1], c[2], n[0], n[1], n[2], lo[1], hi[1]))
    if not found:
        print("nothing within %.2f m" % a.r)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
