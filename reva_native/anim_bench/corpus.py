#!/usr/bin/env python3
"""corpus.py -- the club's real mocap, measured: the envelope authored motion is judged in.

v1 scope (P2): gait-class statistics from offline FK over baked clips -- cadence,
contact duty, vertical center-of-mass oscillation. No engine, no playback: this reads
the rpack's own frames, so the numbers are properties of the DATA. The gate before any
of it is trusted as a reference: it must reproduce a number the bench already certified
from the other side (engine playback measured reva's walk at ~1.29-1.33 steps/s, the
clip's authored cadence).

Widens later with the CivicDefense 777 and (jacob's gate) BONES-SEED. See
design/animation_authoring.md SS6.

usage: corpus.py <pack.rpack> [clip-substring ...]
"""
import sys, os, math, struct

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "skate_src", "author"))
from rpack import Rpack
from authorlib import fk_world

def clip_stats(p, ci):
    nodes = p.nodes(); names = p.node_names()
    c = p.clip(ci)
    tracks = p.tracks(ci)
    # feet: prefer ball (toe), else foot
    def find(*cands):
        for n in cands:
            if n in names: return names.index(n)
        return -1
    fl = find("ball_l", "foot_l"); fr = find("ball_r", "foot_r")
    hip = find("pelvis", "hips", "Hips")
    if fl < 0 or fr < 0 or hip < 0: return None
    ys = {"L": [], "R": []}; hipy = []
    for f in range(c["frames"]):
        qo, to = {}, {}
        for (node, typ, data) in tracks:
            comp = 4 if typ == 0 else 3
            v = list(struct.unpack_from("<%df" % comp, data, f * comp * 4))
            (qo if typ == 0 else to)[node] = v
        w, _ = fk_world(nodes, qo, to)
        ys["L"].append(w[fl][1]); ys["R"].append(w[fr][1]); hipy.append(w[hip][1])
    floor = min(min(ys["L"]), min(ys["R"]))
    # PER-SIDE ADAPTIVE contact threshold. The first cut scaled by hip height -- and the
    # mob1 bank is ROTATION-ONLY (hips pinned, com_osc read 0.000), so that threshold
    # was garbage: walks measured 7% duty where a real walk is ~60%. Each foot's own
    # vertical range is the honest ruler on an in-place bake.
    plants, duty = 0, 0
    for side in ("L", "R"):
        lo, hi = min(ys[side]), max(ys[side])
        thr = lo + 0.30 * (hi - lo if hi > lo else 1.0)
        down = [y <= thr for y in ys[side]]
        duty += sum(down)
        for a, b in zip(down, down[1:] + down[:1]):
            if b and not a: plants += 1           # a touchdown (loop-wrapped)
    dur = c["frames"] / c["fps"]
    return dict(name=c["name"], frames=c["frames"], fps=c["fps"], dur=dur,
                cadence=plants / dur, duty=duty / (2.0 * c["frames"]),
                com_osc=(max(hipy) - min(hipy)) / (max(hipy) - floor))

def main(argv):
    p = Rpack(argv[1])
    subs = [s.lower() for s in argv[2:]] or [""]
    print("corpus v1: %s" % argv[1])
    print("  %-28s %5s %6s %9s %6s %8s" % ("clip", "frames", "fps", "steps/s", "duty", "com-osc"))
    rows = []
    for i in range(p.nClips):
        nm = p.clip(i)["name"]
        if not any(s in nm.lower() for s in subs): continue
        st = clip_stats(p, i)
        if not st: continue
        rows.append(st)
        print("  %-28s %5d %6g %9.2f %6.2f %8.3f"
              % (st["name"], st["frames"], st["fps"], st["cadence"], st["duty"], st["com_osc"]))
    return 0 if rows else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv))
