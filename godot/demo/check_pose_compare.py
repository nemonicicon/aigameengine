"""check_pose_compare.py -- Godot's POSE lines against the pack's own forward kinematics.

    godot --headless --path godot --script res://demo/check_pose.gd -- clip=z_rm_sk_kf frames=0,9,15,21 > pose.txt
    python godot/demo/check_pose_compare.py <pack.rpack> pose.txt z_rm_sk_kf

PASS when every joint and the board's origin are within 1 mm and the board's nose axis within 0.5 degrees.
"""
import math, os, sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "skate_src", "author"))
from rpack import Rpack


def q2m(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def main():
    pack, posefile, clip = sys.argv[1], sys.argv[2], sys.argv[3]
    R = Rpack(pack)
    nodes = R.nodes()
    names = [n["name"] for n in nodes]
    cl = {R.clip(c)["name"]: c for c in range(R.nClips)}
    ci, bd = cl[clip], cl.get(clip + "_bd")

    def fk(f):
        loc = {i: [np.array(n["t"]), np.array(n["q"])] for i, n in enumerate(nodes)}
        for n, t, d in R.tracks(ci):
            D = np.frombuffer(d, "<f4")
            if t == 0: loc[n][1] = D[f * 4:f * 4 + 4]
            elif t == 1: loc[n][0] = D[f * 3:f * 3 + 3]
        W = {}
        for i, n in enumerate(nodes):
            M = np.eye(4); M[:3, :3] = q2m(loc[i][1]); M[:3, 3] = loc[i][0]
            W[i] = M if n["parent"] < 0 else W[n["parent"]] @ M
        return {names[i]: W[i][:3, 3] for i in W}

    def board(f):
        T = Q = None
        for n, t, d in R.tracks(bd):
            D = np.frombuffer(d, "<f4")
            if n == 0 and t == 1: T = D[f * 3:f * 3 + 3]
            if n == 0 and t == 0: Q = D[f * 4:f * 4 + 4]
        return T, q2m(Q)[:, 0]

    worst_p, worst_a, n = 0.0, 0.0, 0
    for ln in open(posefile, encoding="utf-8", errors="replace"):
        if not ln.startswith("POSE "): continue
        _, f, nm, x, y, z = ln.split()
        f = int(f); g = np.array([float(x), float(y), float(z)])
        if nm == "board":
            ref = board(f)[0]; e = float(np.linalg.norm(g - ref)); worst_p = max(worst_p, e)
        elif nm == "boardX":
            ref = board(f)[1]; e = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(g / np.linalg.norm(g), ref))))))
            worst_a = max(worst_a, e)
        else:
            ref = fk(f)[nm]; e = float(np.linalg.norm(g - ref)); worst_p = max(worst_p, e)
        n += 1
        print("  f%-3d %-9s godot %s  pack %s  err %.6f" % (f, nm, np.round(g, 4), np.round(ref, 4), e))
    ok = n > 0 and worst_p < 1e-3 and worst_a < 0.5
    print("%d values: worst position error %.6f m, worst board-axis error %.3f deg -> %s" % (n, worst_p, worst_a, "PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
