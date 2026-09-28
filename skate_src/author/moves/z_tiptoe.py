#!/usr/bin/env python3
"""z_tiptoe.py -- P6e specimen 2: rise onto the toes. REAL TOE ARTICULATION.

Heels lift, the balls of the feet stay planted and the TOES BEND at the ball joint
(held world-flat while the foot above them rotates) -- which is what a toe joint is
for. The body rises by exactly the geometry the foot rotation produces, solved not
tuned: rotate the feet, measure where the balls went, translate the pelvis so they
return to the floor.

    asserts -- ankles rise > 0.05 legs; balls stay within 0.012 legs of rest

usage: z_tiptoe.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (fk_world, rotate_bone_world, translate_node_world,
                       hold_world_rotation, profile, ease_smooth, qnorm, declare,
                       find_toes)

DUR, FPS = 2.6, 30.0
RISE_DEG = 32.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_tiptoe" in p.clip_names():
        print("z_tiptoe already present"); return 0
    nodes = p.nodes(); names = p.node_names()
    idle = p.clip_names().index("a_idle")
    base_q, base_t, base = {}, {}, {}
    for (node, typ, data) in p.tracks(idle):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v
        elif typ == 1: base_t[node] = v

    pelvis = names.index("pelvis")
    footL, footR = names.index("foot_l"), names.index("foot_r")
    legL = tuple(names.index(n) for n in ("thigh_l", "calf_l", "foot_l"))
    toes = find_toes(p, nodes, names, footL, footR)
    assert "L" in toes and "R" in toes, "no ball joints resolved"
    ballL, ballR = toes["L"], toes["R"]
    wp0, wq0 = fk_world(nodes, base_q, base_t)
    L = math.dist(wp0[legL[0]], wp0[legL[1]]) + math.dist(wp0[legL[1]], wp0[legL[2]])
    lat = [wp0[legL[0]][k] - wp0[names.index("thigh_r")][k] for k in range(3)]
    b0 = {b: list(wp0[b]) for b in (ballL, ballR)}

    # rise sign audit -- ON THE BALL, not the ankle: the foot rotates about its own
    # origin (the ankle joint), so the ankle cannot move from its own rotation. The
    # sign that drops the BALL is the rise sign (the body then lifts by that drop).
    def probe(sgn):
        qo = dict(base_q)
        for b in (footL, footR):
            qo[b] = rotate_bone_world(nodes, b, lat, sgn * math.radians(20), qo)
        w, _ = fk_world(nodes, qo, base_t)
        return b0[ballL][1] - w[ballL][1]          # positive = ball dropped
    SGN = 1.0 if probe(1.0) > probe(-1.0) else -1.0

    rise = [(0.25, 0.0, ease_smooth), (0.80, 1.0, None),
            (1.75, 1.0, ease_smooth), (2.35, 0.0, None)]

    frames = int(DUR * FPS)
    qtr = {}; ttr = []
    checks = {"ankle": 0.0, "ball": 0.0}
    for f in range(frames):
        t = f / FPS
        r = profile(rise, t)
        qo = dict(base_q); to = dict(base_t)
        for b in (footL, footR):
            qo[b] = rotate_bone_world(nodes, b, lat, SGN * math.radians(RISE_DEG) * r, qo)
        # toes stay flat on the floor while the foot rotates above them -- the ball
        # joint articulates by exactly the rise angle
        for b in (ballL, ballR):
            qo[b] = hold_world_rotation(nodes, b, wq0[b], qo, to)
        # the balls moved when the feet rotated: measure, and lift the body so they
        # return to the floor. Solved, not tuned.
        w, _ = fk_world(nodes, qo, to)
        dy = sum(b0[b][1] - w[b][1] for b in (ballL, ballR)) / 2.0
        to[pelvis] = translate_node_world(nodes, pelvis, [0.0, dy, 0.0], base_q, base_t)
        w2, _ = fk_world(nodes, qo, to)
        if r > 0.98:
            checks["ankle"] = max(checks["ankle"], (w2[footL][1] - wp0[footL][1]) / L)
        checks["ball"] = max(checks["ball"],
                             max(abs(w2[b][1] - b0[b][1]) for b in (ballL, ballR)) / L)
        for b in (footL, footR, ballL, ballR):
            qtr.setdefault(b, []).extend(qnorm(qo[b]))
        ttr.extend(to[pelvis])

    print("z_tiptoe: ankle rise %.3f L   ball drift %.4f L (sign %+.0f)"
          % (checks["ankle"], checks["ball"], SGN))
    assert checks["ankle"] > 0.05, "the heels never lifted"
    assert checks["ball"] < 0.012, "the toes left the floor"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    tracks[(pelvis, 1)] = ttr
    append_clips(pack_path, [("z_tiptoe", FPS, frames, tracks)])
    declare(pack_path, "z_tiptoe", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 6.0,
        "contacts": [
            {"part": "fL", "t": [0.9, 1.7], "floor": False, "still_tol": 0.06},
            {"part": "fR", "t": [0.9, 1.7], "floor": False, "still_tol": 0.06},
        ]})
    print("z_tiptoe: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
