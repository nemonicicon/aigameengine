#!/usr/bin/env python3
"""z_greet.py -- P6e specimen 1: a wave with REAL FINGERS.

Raise the right arm, spread the hand wide open with a per-finger wiggle, clench to a
fist, open again, lower. The first authored motion in the club with articulated hands.
All axes derived per rig and sign-audited (HandRig); the asserts are geometric:

    OPEN  -- fingertip span (index tip to pinky tip) grows past 1.25x its rest span
    FIST  -- mean fingertip-to-palm distance shrinks under 0.6x rest

usage: z_greet.py <moves-pack>
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips
from authorlib import (fk_world, solve_limb, rotate_bone_world, profile, ease_smooth,
                       qnorm, declare, find_hands, HandRig)

DUR, FPS = 3.4, 30.0

def main(pack_path):
    p = Rpack(pack_path)
    if "z_greet" in p.clip_names():
        print("z_greet already present"); return 0
    nodes = p.nodes(); names = p.node_names()
    idle = p.clip_names().index("a_idle")
    base_q, base_t, base = {}, {}, {}
    for (node, typ, data) in p.tracks(idle):
        comp = 4 if typ == 0 else 3
        v = list(struct.unpack_from("<%df" % comp, data, 0))
        base[(node, typ)] = v
        if typ == 0: base_q[node] = v
        elif typ == 1: base_t[node] = v

    armR = tuple(names.index(n) for n in ("upperarm_r", "lowerarm_r", "hand_r"))
    hands = find_hands(p, nodes, names, names.index("hand_l"), names.index("hand_r"))
    assert "R" in hands, "no right-hand fingers resolved"
    hr = HandRig(nodes, base_q, base_t, hands["R"], armR[1])
    spec = hands["R"]
    print("z_greet: %d fingers resolved, curl sign %+.0f"
          % (len(spec["fingers"]), hr.curl_sign))

    wp0, _ = fk_world(nodes, base_q, base_t)
    sh = wp0[armR[0]]
    armlen = math.dist(wp0[armR[0]], wp0[armR[1]]) + math.dist(wp0[armR[1]], wp0[armR[2]])
    fwd = [wp0[names.index("ball_r")][k] - wp0[names.index("foot_r")][k] for k in range(3)]
    fwd[1] = 0.0; fl = math.hypot(fwd[0], fwd[2]); fwd = [fwd[0]/fl, 0.0, fwd[2]/fl]
    # the wave target: up and a little forward-out, elbow kept bent (0.82 reach)
    up_t = [sh[0] + fwd[0]*0.25*armlen, sh[1] + 0.42*armlen, sh[2] + fwd[2]*0.25*armlen]

    raise_p = [(0.20, 0.0, ease_smooth), (0.75, 1.0, None),
               (2.85, 1.0, ease_smooth), (3.30, 0.0, None)]
    open_p  = [(0.55, 0.0, ease_smooth), (0.95, 1.0, None),      # spread open
               (1.55, 1.0, ease_smooth), (1.90, 0.0, None),      # ...to fist
               (2.30, 0.0, ease_smooth), (2.70, 1.0, None),      # open again
               (3.00, 1.0, ease_smooth), (3.35, 0.3, None)]
    fist_p  = [(1.55, 0.0, ease_smooth), (1.95, 1.0, None),
               (2.30, 1.0, ease_smooth), (2.65, 0.0, None)]

    # author-time verification probes
    def measure(qo):
        w, _ = fk_world(nodes, qo, base_t)
        t = spec["tips"]
        span = math.dist(w[t["index"]], w[t["pinky"]]) if "index" in t and "pinky" in t else 0
        palm = w[spec["hand"]]
        mtp = sum(math.dist(w[k], palm) for k in t.values()) / max(1, len(t))
        return span, mtp
    span0, mtp0 = measure(dict(base_q))

    frames = int(DUR * FPS)
    qtr = {}
    finger_bones = [b for ch in spec["fingers"].values() for b in ch]
    checks = {"span": 0.0, "fist": 9e9}
    for f in range(frames):
        t = f / FPS
        ar, op, fi = profile(raise_p, t), profile(open_p, t), profile(fist_p, t)
        qo = dict(base_q)
        if ar > 1e-4:
            hang = wp0[armR[2]]
            tg = [hang[k] + (up_t[k] - hang[k]) * ar for k in range(3)]
            qo.update(solve_limb(nodes, armR, tg, pole=fwd, q_override=qo, t_override=base_t))
        wig = {}
        if op > 0.7 and fi < 0.05:
            for i, fn in enumerate(spec["fingers"]):
                if fn != "thumb":
                    wig[fn] = 7.0 * math.sin(t * 9.0 + i * 1.7)     # per-finger life
        hr.pose(qo, curl=fi, spread=op * (1.0 - fi), thumb=fi, wiggle=wig)
        if 1.05 <= t <= 1.45:
            s, _m = measure(qo); checks["span"] = max(checks["span"], s)
        if 2.0 <= t <= 2.25:
            _s, m = measure(qo); checks["fist"] = min(checks["fist"], m)
        for b in list(armR) + finger_bones:
            # a finger untouched this frame holds its REST (fingers are not in
            # a_idle's tracks, so rest is the honest neutral)
            qtr.setdefault(b, []).extend(qnorm(qo.get(b, base_q.get(b, nodes[b]["q"]))))

    print("z_greet: open span %.2fx rest   fist tip-to-palm %.2fx rest"
          % (checks["span"] / span0, checks["fist"] / mtp0))
    assert checks["span"] / span0 > 1.25, "the hand never opened"
    assert checks["fist"] / mtp0 < 0.60, "the fist never closed"

    tracks = {}
    for (node, typ), v in base.items():
        tracks[(node, typ)] = v * frames
    for b, vals in qtr.items(): tracks[(b, 0)] = vals
    append_clips(pack_path, [("z_greet", FPS, frames, tracks)])
    declare(pack_path, "z_greet", {
        "fps": FPS, "frames": frames, "terminal": "base", "max_speed": 11.0,
        "contacts": [
            {"part": "fL", "t": [0.0, 3.4], "floor": True, "still_tol": 0.04},
            {"part": "fR", "t": [0.0, 3.4], "floor": True, "still_tol": 0.04},
        ]})
    print("z_greet: appended (%d frames, %d tracks)" % (frames, len(tracks)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
