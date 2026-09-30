#!/usr/bin/env python3
"""z_gest.py -- THE GESTURE SET, composed over REST via rig_map (like z_armsup), so it resolves on any
rig the canon knows -- written for ROBOT NEKO ep 16, whose two bodies (CLANKER, X3D: Mixamo-named,
built by code) carry fourteen skate clips and nothing a talking body can do.

    z_gest.py <pack> [--only z_idle,z_hello,...] [--force]

  z_idle    4.0 s LOOP  a stand that breathes: the chest lifts, the weight sways, the head drifts. First
                        frame == last, so a scene can loop it under any line.
  z_hello   2.0 s       the right arm comes up beside the head and waves three times, then hangs again
  z_bow     2.4 s       a bow from the hips, 42 degrees, held, the head keeping a little of the eyeline
  z_point   2.0 s       the right arm out to the front, straight, held, back
  z_shrug   1.6 s       both clavicles up, forearms out and open, a beat, down
  z_nod     1.2 s       two nods
  z_no      1.4 s       two shakes
  z_think   2.6 s       the right hand to the chin, held, back (a hand-to-face move; the face is a box)

Every clip ends where it starts (first == last), so in a scene it loops rather than holds (the
transition rule: a >20 degree first-to-last gap plays once and holds). Axes: the club's forward is +z
(Mixamo rigs face +z), up +y, the body's left +x. Signs were MEASURED on the lab bench before the first
episode line was written (ep 15's ledger: a facing is measured, never guessed twice).
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip
from authorlib import rig_map, fk_world, aim_bone, rotate_bone_world, qnorm, ease_smooth, ease_settle, ease_anticipate

FPS = 30.0
X, Y, Z = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]

def ramp(t, t0, t1):
    """0 before t0, 1 after t1, smooth between"""
    if t1 <= t0: return 1.0 if t >= t1 else 0.0
    return ease_smooth(max(0.0, min(1.0, (t - t0) / (t1 - t0))))

def hang_angle(nodes, RM, side):
    """how far from straight down the resting upper arm already hangs (degrees), so a raise starts from it"""
    wp, _ = fk_world(nodes)
    ua, la = RM["upperarm_" + side], RM["lowerarm_" + side]
    d = [wp[la][k] - wp[ua][k] for k in range(3)]
    l = math.sqrt(sum(x * x for x in d)) or 1.0
    return math.degrees(math.acos(max(-1.0, min(1.0, -d[1] / l))))

def arm_dir(th_deg, sx, fwd):
    """a direction for an upper arm th degrees from straight down, out to its own side, fwd toward +z"""
    th = math.radians(th_deg)
    return [math.sin(th) * sx * (1.0 - abs(fwd)), -math.cos(th), fwd * math.sin(th) if fwd else 0.0]

# ---------------------------------------------------------------- the moves: t (s) -> q overrides
def mv_idle(nodes, RM, t, D):
    q = {}
    br = 0.5 * (1.0 - math.cos(2 * math.pi * 0.5 * t))          # breath: 2 cycles in 4 s
    sw = math.sin(2 * math.pi * 0.25 * t)                          # weight sway: 1 cycle
    hd = math.sin(2 * math.pi * 0.25 * t + 1.1)                    # head drift
    for j, a in (("spine_01", 1.3), ("spine_02", 1.2), ("spine_03", 1.0)):
        q[RM[j]] = rotate_bone_world(nodes, RM[j], X, math.radians(-a * br), q)      # chest lifts (tips back a hair)
        q[RM[j]] = rotate_bone_world(nodes, RM[j], Z, math.radians(0.9 * sw), q)     # and sways
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], Y, math.radians(4.0 * hd), q)
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], X, math.radians(1.5 * br), q)
    for side, sx in (("l", 1.0), ("r", -1.0)):
        ua = RM["upperarm_" + side]
        q[ua] = rotate_bone_world(nodes, ua, Z, math.radians(-1.5 * br * sx), q)     # arms ease off the body on the in-breath
    return q

def mv_hello(nodes, RM, t, D):
    q = {}
    side, sx = "r", -1.0
    ua, la, ha = RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
    up = ramp(t, 0.0, 0.40) * (1.0 - ramp(t, 1.55, 2.0))
    h0 = hang_angle(nodes, RM, side)
    th = h0 + (150.0 - h0) * up
    wave = math.radians(22.0) * ramp(t, 0.35, 0.55) * (1.0 - ramp(t, 1.45, 1.6)) * math.sin(2 * math.pi * 3.0 * (t - 0.4))
    q[ua] = aim_bone(nodes, ua, la, arm_dir(th, sx, 0.15), q)
    tf = math.radians(th - 55.0 * up) + wave          # the forearm folds in toward the head, and waves
    q[la] = aim_bone(nodes, la, ha, [math.sin(tf) * sx * 0.55, -math.cos(tf), 0.25 * up], q)
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], Y, math.radians(-6.0 * up * sx), q)
    return q

def mv_bow(nodes, RM, t, D):
    q = {}
    a = ease_anticipate(min(1.0, t / 0.75)) if t < 0.75 else (1.0 if t < 1.55 else 1.0 - ease_settle(min(1.0, (t - 1.55) / 0.85)))
    a = max(0.0, min(1.0, a))
    for j, deg in (("spine_01", 16.0), ("spine_02", 14.0), ("spine_03", 12.0)):
        q[RM[j]] = rotate_bone_world(nodes, RM[j], X, math.radians(deg * a), q)      # forward from the hips: +x tips +y toward +z
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], X, math.radians(-14.0 * a), q)   # keeps a little of the eyeline
    for side, sx in (("l", 1.0), ("r", -1.0)):
        ua = RM["upperarm_" + side]
        q[ua] = rotate_bone_world(nodes, ua, X, math.radians(-30.0 * a), q)          # the arms stay hanging, not swung out front
    return q

def mv_point(nodes, RM, t, D):
    q = {}
    side, sx = "r", -1.0
    ua, la, ha = RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
    up = ramp(t, 0.0, 0.45) * (1.0 - ramp(t, 1.45, 1.95))
    d = [0.12 * sx * up, -1.0 + 1.05 * up, 1.0 * up]              # from hanging to straight out front
    q[ua] = aim_bone(nodes, ua, la, d, q)
    q[la] = aim_bone(nodes, la, ha, d, q)
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], Y, math.radians(8.0 * up * sx), q)
    return q

def mv_shrug(nodes, RM, t, D):
    q = {}
    up = ramp(t, 0.0, 0.30) * (1.0 - ramp(t, 1.0, 1.5))
    for side, sx in (("l", 1.0), ("r", -1.0)):
        cl, ua, la, ha = RM["clavicle_" + side], RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
        if cl >= 0:
            q[cl] = rotate_bone_world(nodes, cl, Z, math.radians(-9.0 * up * sx), q)   # the shoulder lifts
        h0 = hang_angle(nodes, RM, side)
        q[ua] = aim_bone(nodes, ua, la, arm_dir(h0 + 22.0 * up, sx, 0.10 * up), q)
        tf = math.radians(h0 + 22.0 * up) + math.radians(70.0 * up)
        q[la] = aim_bone(nodes, la, ha, [math.sin(tf) * sx * 0.7, -math.cos(tf), 0.55 * up], q)   # forearms out and open
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], Z, math.radians(7.0 * up), q)             # a tilt
    return q

def mv_nod(nodes, RM, t, D):
    q = {}
    a = math.radians(12.0) * max(0.0, math.sin(2 * math.pi * (t / D) * 2.0))                        # two nods, down only
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], X, a, q)
    q[RM["neck_01"]] = rotate_bone_world(nodes, RM["neck_01"], X, a * 0.35, q)
    return q

def mv_no(nodes, RM, t, D):
    q = {}
    a = math.radians(16.0) * math.sin(2 * math.pi * (t / D) * 2.0) * ramp(t, 0.0, 0.15) * (1.0 - ramp(t, D - 0.15, D))
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], Y, a, q)
    return q

def mv_think(nodes, RM, t, D):
    q = {}
    side, sx = "r", -1.0
    ua, la, ha = RM["upperarm_" + side], RM["lowerarm_" + side], RM["hand_" + side]
    up = ramp(t, 0.0, 0.55) * (1.0 - ramp(t, 2.0, 2.6))
    h0 = hang_angle(nodes, RM, side)
    q[ua] = aim_bone(nodes, ua, la, arm_dir(h0 + 20.0 * up, sx, 0.35 * up), q)
    tf = math.radians(h0 + 20.0 * up) + math.radians(118.0 * up)        # the forearm folds up, the hand toward the chin
    q[la] = aim_bone(nodes, la, ha, [math.sin(tf) * sx * 0.35, -math.cos(tf), 0.30 * up], q)
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], X, math.radians(6.0 * up), q)
    q[RM["head"]] = rotate_bone_world(nodes, RM["head"], Z, math.radians(-5.0 * up), q)
    return q

MOVES = [("z_idle", 4.0, mv_idle), ("z_hello", 2.0, mv_hello), ("z_bow", 2.4, mv_bow), ("z_point", 2.0, mv_point),
         ("z_shrug", 1.6, mv_shrug), ("z_nod", 1.2, mv_nod), ("z_no", 1.4, mv_no), ("z_think", 2.6, mv_think)]

def main(argv):
    pack = argv[1]
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    force = "--force" in argv
    p = Rpack(pack); nodes = p.nodes(); RM = rig_map(p)
    assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
    out = []
    for name, D, fn in MOVES:
        if only and name not in only: continue
        if name in p.clip_names():
            if not force: print("%s already present (--force re-authors it)" % name); continue
            drop_clip(pack, name); p = Rpack(pack); nodes = p.nodes()
        frames = int(round(D * FPS))
        tracks = {}
        qs = [fn(nodes, RM, f / FPS, D) for f in range(frames)]
        # THE SCORE IS THE TEST: first == last (a loop), and the feet never left the floor (rotation only)
        wp0, _ = fk_world(nodes, qs[0]); wp1, _ = fk_world(nodes, qs[-1])
        gap = max(math.dist(wp0[i], wp1[i]) for i in range(len(nodes)))
        for i in range(len(nodes)):
            tracks[(i, 0)] = [float(x) for j in range(frames) for x in qnorm(qs[j].get(i, nodes[i]["q"]))]
        # ⭐ THE BOARD GOES BACK IN THE FOLDER. A rigid board is a one-bone mesh on its own root node (the baker,
        # 09-30); in a clip that does not track that node it sits at rest -- under the feet of a body that is
        # bowing. A gesture parks it 50 rig units under the floor; the skate clips bring it back.
        bi = next((i for i, nm in enumerate(p.node_names()) if "skateboard" in nm.lower() or "board" in nm.lower()), -1)
        if bi >= 0: tracks[(bi, 1)] = [0.0, -50.0, 0.0] * frames
        out.append((name, FPS, frames, tracks))
        print("%-8s %d frames  first-vs-last %.4f rig u" % (name, frames, gap))
    if out:
        append_clips(pack, out, force=True)
        print("appended %d clip(s) to %s" % (len(out), os.path.basename(pack)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
