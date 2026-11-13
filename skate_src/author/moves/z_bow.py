#!/usr/bin/env python3
"""z_bow.py -- P1: the first authored MOTION. A bow: lean to 45 deg, hold, return.

What this proves beyond the hello pose: keyframed TIMING (ease in, hold, ease out),
motion distributed across a chain (three spine joints share the bend), and -- the P1
lesson in miniature -- authoring to the METRIC rather than to joint angles. Rotating
three joints 15 deg each does not put the hip->head vector at 45 deg (the hips stay,
the head arcs), so the score solves the per-joint angle until the MEASURED lean is
45.00 at author time. The declared quantity is the checked quantity.

Direction is derived, not guessed: "forward" = the way the toes point (ball - ankle in
the base pose), and the bend axis is the hip lateral axis. No hand-tuned signs.

Gate: the engine-measured lean plateau reads 45 +/- 2; continuity clean; ends at base.

usage: z_bow.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (fk_world, rotate_bone_world, profile, ease_smooth, ease_settle,
                       qnorm)

DUR, FPS = 2.4, 30.0
TARGET = 45.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_bow" in p.clip_names():
        print("z_bow already present"); return 0
    nodes = p.nodes()
    names = p.node_names()
    idle = p.clip_names().index("a_idle")

    base = {}; base_q = {}
    for (node, typ, data) in p.tracks(idle):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v

    spine = [names.index(n) for n in ("spine_01", "spine_02", "spine_03")]
    hip, head = names.index("pelvis"), names.index("head")
    thL, thR = names.index("thigh_l"), names.index("thigh_r")
    foot, ball = names.index("foot_r"), names.index("ball_r")

    wp, _ = fk_world(nodes, base_q)
    lat = [wp[thL][k] - wp[thR][k] for k in range(3)]            # bend axis: hip lateral
    fwd = [wp[ball][k] - wp[foot][k] for k in range(3)]          # forward: where toes point
    fwd[1] = 0.0

    def lean_of(qo):
        w, _ = fk_world(nodes, qo)
        d = [w[head][k] - w[hip][k] for k in range(3)]
        h = math.hypot(d[0], d[2])
        return math.degrees(math.atan2(h, d[1]))

    rest_lean = lean_of(base_q)

    def pose_at(per_joint_deg):
        """Bend the three spine joints per_joint_deg each about the lateral axis,
        sequentially (each FK sees the bend above it). Sign chosen so the head moves
        the way the toes point."""
        for sgn in (1.0, -1.0):
            qo = dict(base_q)
            for b in spine:
                qo[b] = rotate_bone_world(nodes, b, lat, sgn * math.radians(per_joint_deg), qo)
            w, _ = fk_world(nodes, qo)
            head_d = [w[head][k] - wp[head][k] for k in range(3)]
            if head_d[0] * fwd[0] + head_d[2] * fwd[2] > 0:
                return qo
        return qo   # degenerate fwd: fall through with the last sign

    # ---- solve: per-joint angle such that MEASURED lean == TARGET ----
    lo, hi = 0.0, 40.0
    for _ in range(40):                       # bisection; lean is monotone in the bend
        mid = (lo + hi) / 2
        if lean_of(pose_at(mid)) < TARGET: lo = mid
        else: hi = mid
    per_joint = (lo + hi) / 2
    got = lean_of(pose_at(per_joint))
    print("z_bow: %.3f deg/joint -> lean %.2f (rest %.2f, want %g)" % (per_joint, got, rest_lean, TARGET))
    assert abs(got - TARGET) < 0.01

    # ---- timing: ease down (settle into the hold), hold, ease back up ----
    keys = [(0.00, 0.0, ease_smooth),
            (0.70, 1.0, None),               # down by 0.7s
            (1.60, 1.0, ease_smooth),        # hold
            (2.30, 0.0, None)]               # back up by 2.3s; tail frames = base

    frames = int(DUR * FPS)
    qtracks = {b: [] for b in spine}
    for f in range(frames):
        t = f / FPS
        amt = profile(keys, t) * per_joint
        qo = pose_at(amt) if amt > 1e-6 else dict(base_q)
        for b in spine:
            qtracks[b].extend(qnorm(qo[b]))

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b in spine:
        tracks[(b, 0)] = qtracks[b]
    append_clips(pack_path, [("z_bow", FPS, frames, tracks)])
    print("z_bow: appended (%d frames @ %g, %d tracks)" % (frames, FPS, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
