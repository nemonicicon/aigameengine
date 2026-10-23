#!/usr/bin/env python3
"""z_sit.py -- SITTING, LYING and CARRYING in code, over REST via rig_map (the way the kit's z_gest.py and z_walk.py are
written), so they resolve on any rig the canon knows -- written for THE WORLD AT THE END OF TIME's chapter 2 (10-22):
the navigators "peck away at their computers" (p14), breakfast at the table, dinner in the refectory, dominoes in the
rec room, the captain's table, Werner "webbed into his bed, sound asleep" (p18), a frozen woman in her casket "her eyes
closed but her mouth open, looking as though she were trying to scream" (p34), and Viktor carrying Marie-Claude's
tray "extremely careful" (p18). The banks' sitting clips are named `Rig|Sitting_...`, and a `|` cannot ride a scene
line (it is the line's separator), nor does bankFind reach a pack that is not a bank.

    z_sit.py <pack> [--force] [--hang 10] [--seat 0.46] [--desk 0.74] [--only z_sit,...]

  z_sit       4.0 s LOOP  on a seat `seat` m high: the hips down and back, the thighs level, the shins down to feet
                          flat on the floor (two-bone IK on the body's own legs), the hands on the thighs; breathing
  z_sit_talk  4.0 s LOOP  z_sit, a hand lifting off the thigh now and then, as a talker's does
  z_sit_type  2.0 s LOOP  z_sit at a desk `desk` m high: the forearms forward, the hands at the keys, tapping
  z_rm_lie    4.0 s LOOP  on the back, the head toward -z, the arms along the body, breathing: a mark AT the bed's top
  z_rm_doze   4.0 s LOOP  z_rm_lie with the eyes shut (the blink morphs held at 1)
  z_rm_scream 4.0 s LOOP  z_rm_lie, eyes shut, the mouth open (jaw 0.85): Accardo in her casket
  (z_rm_: a lying clip OWNS ITS ROOT -- otherwise the engine grounds the body by its feet and drives it underground)
  z_carry     2.0 s LOOP  standing, the forearms forward and level, the hands a tray's width apart (hover= it on a walk)
  z_rm_sit, z_rm_sit_talk, z_rm_sit_type   the sits owning their root: for a SET ACTOR whose feet hang from the seat
  (a body whose feet cannot reach the floor from the seat -- a child on a stool -- sits with its legs HANGING)

Units are the pack's rig units -- METRES for a TrueScale pack (the WATEOT cast). The root stays on the floor: a scene's
mark puts the body where it sits (a lying body's mark carries the bed's height). Every clip ends where it starts.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip
from authorlib import (rig_map, fk_world, aim_bone, rotate_bone_world, solve_limb, hold_world_rotation,
                       translate_node_world, qnorm)

FPS = 30.0
X, Y, Z = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]


def morph_indices(p, name):
    import struct
    offM = struct.unpack_from("<15I", p.buf, 4)[14]
    if not offM: return []
    n = struct.unpack_from("<I", p.buf, offM)[0]
    return [i for i in range(n) if p.sname(struct.unpack_from("<4I", p.buf, offM + 4 + i * 16)[1]) == name]


class Body:
    def __init__(self, nodes, RM, hang):
        self.nodes, self.RM, self.hang = nodes, RM, hang
        wp, wq = fk_world(nodes)
        self.wp, self.wq = wp, wq
        self.pel = RM["pelvis"]
        self.legs = {"l": (RM["thigh_l"], RM["calf_l"], RM["foot_l"]), "r": (RM["thigh_r"], RM["calf_r"], RM["foot_r"])}
        self.arms = {"l": (RM["upperarm_l"], RM["lowerarm_l"], RM["hand_l"]), "r": (RM["upperarm_r"], RM["lowerarm_r"], RM["hand_r"])}
        self.thigh = sum(math.dist(wp[a], wp[b]) for a, b, _ in self.legs.values()) / 2.0
        self.shin = sum(math.dist(wp[b], wp[c]) for _, b, c in self.legs.values()) / 2.0
        self.ankle_y = min(wp[c][1] for _, _, c in self.legs.values())
        self.hip_y = sum(wp[a][1] for a, _, _ in self.legs.values()) / 2.0

    def hanging(self, q):
        """the arms hanging `hang` degrees off the body, the elbows soft (as z_gest --hang)"""
        n = self.nodes
        for side, sx in (("l", 1.0), ("r", -1.0)):
            ua, la, ha = self.arms[side]
            th = math.radians(self.hang)
            q[ua] = aim_bone(n, ua, la, [math.sin(th) * sx, -math.cos(th), 0.03], q)
            q[la] = aim_bone(n, la, ha, [math.sin(th) * sx * 0.8, -math.cos(math.radians(12.0)), math.sin(math.radians(12.0))], q)
        return q

    def dangles(self, seat):
        """the legs cannot reach the floor target the seated solve gives them (a thigh's length forward of the hip)"""
        hip = seat + 0.085 * self.thigh / 0.42
        return math.hypot(hip - self.ankle_y, 0.97 * self.thigh) > 0.98 * (self.thigh + self.shin)

    def seated(self, seat, t, back=0.06):
        """the hips down onto a seat `seat` high and a little back, the legs to feet flat on the floor (or, when they
        cannot reach it, hanging)"""
        n, q, tt = self.nodes, {}, {}
        dy = (seat + 0.085 * self.thigh / 0.42 + (0.012 if self.dangles(seat) else 0.0)) - self.hip_y
        tt[self.pel] = translate_node_world(n, self.pel, [0.0, dy, -back], q, tt)
        q[self.pel] = rotate_bone_world(n, self.pel, X, math.radians(-4.0), q)          # the pelvis rolls back a hair
        for side, (th, ca, ft) in self.legs.items():
            wp, _ = fk_world(n, q, tt)
            hp = wp[th]
            tgt = [hp[0] * 1.08, self.ankle_y, hp[2] + self.thigh * 0.97]                   # the shin straight down
            if self.dangles(seat):
                # ⭐ (10-22) A CHILD ON A GROWN-UP'S STOOL: the floor is out of reach (Freddy's legs straightened to find
                # it and he sat as a plank leaning back) -- the thigh lies level on the seat and the shin HANGS from the
                # knee a little forward, swinging (a five-year-old's legs do)
                sw = math.radians(9.0) * math.sin(2 * math.pi * t / 2.0 + (0.0 if side == "l" else math.pi))
                kn = [hp[0] * 1.04, hp[1] - 0.01, hp[2] + self.thigh * 0.995]
                a = math.radians(12.0) + sw
                tgt = [kn[0], kn[1] - self.shin * math.cos(a), kn[2] + self.shin * math.sin(a)]
            q.update(solve_limb(n, (th, ca, ft), tgt, pole=[0.0, 0.0, 1.0], q_override=q, t_override=tt))
            q[ft] = hold_world_rotation(n, ft, self.wq[ft], q, tt)
        # the spine sits up (the pelvis rolled back): the chest comes forward again
        for j, a in (("spine_01", 3.0), ("spine_02", 1.5)):
            q[self.RM[j]] = rotate_bone_world(n, self.RM[j], X, math.radians(a), q)
        return q, tt

    def hands_to(self, q, tt, targets, poles):
        n = self.nodes
        for side in ("l", "r"):
            ua, la, ha = self.arms[side]
            q.update(solve_limb(n, (ua, la, ha), targets[side], pole=poles[side], q_override=q, t_override=tt))
        return q

    def breathe(self, q, t, period=4.0, amp=1.2):
        n = self.nodes
        br = 0.5 * (1.0 - math.cos(2 * math.pi * t / period))
        for j, a in (("spine_02", amp), ("spine_03", amp * 0.8)):
            q[self.RM[j]] = rotate_bone_world(n, self.RM[j], X, math.radians(-a * br), q)
        q[self.RM["head"]] = rotate_bone_world(n, self.RM["head"], Y, math.radians(2.5 * math.sin(2 * math.pi * t / period + 0.8)), q)
        return q


# ---------------------------------------------------------------- the moves: (body, t, D, opt) -> (q, t)
def thigh_tops(B, q, tt, seat, fwd=0.55, up=0.11):
    wp, _ = fk_world(B.nodes, q, tt)
    out = {}
    for side, (th, ca, ft) in B.legs.items():
        h, k = wp[th], wp[ca]
        out[side] = [h[0] + (k[0] - h[0]) * fwd, h[1] + (k[1] - h[1]) * fwd + up * B.thigh / 0.42, h[2] + (k[2] - h[2]) * fwd]
    return out


ELBOW_BACK = {"l": [0.5, -0.2, -1.0], "r": [-0.5, -0.2, -1.0]}


def mv_sit(B, t, D, o):
    q, tt = B.seated(o["seat"], t)
    q = B.hands_to(q, tt, thigh_tops(B, q, tt, o["seat"]), ELBOW_BACK)
    return B.breathe(q, t), tt


def mv_sit_talk(B, t, D, o):
    q, tt = B.seated(o["seat"], t)
    tg = thigh_tops(B, q, tt, o["seat"])
    for side, ph in (("r", 0.0), ("l", 0.5)):          # one hand lifts off the thigh, then the other
        u = max(0.0, math.sin(2 * math.pi * (t / D + ph))) ** 2
        tg[side] = [tg[side][0] * (1.0 - 0.15 * u), tg[side][1] + 0.12 * u * B.thigh / 0.42, tg[side][2] + 0.10 * u]
    q = B.hands_to(q, tt, tg, ELBOW_BACK)
    return B.breathe(q, t), tt


def mv_sit_type(B, t, D, o):
    q, tt = B.seated(o["seat"], t)
    wp, _ = fk_world(B.nodes, q, tt)
    hp = {s: wp[B.legs[s][0]] for s in B.legs}
    tg = {}
    for side, sx, ph in (("l", 1.0, 0.0), ("r", -1.0, 0.5)):
        tap = 0.010 * max(0.0, math.sin(2 * math.pi * (4.0 * t / D * 2 + ph)))
        tg[side] = [hp[side][0] * 0.9 + sx * 0.02, o["desk"] + 0.035 + tap, hp[side][2] + B.thigh * 1.05]
    q = B.hands_to(q, tt, tg, ELBOW_BACK)
    return B.breathe(q, t, period=D), tt


def lying(B, t):
    """on the back: the pelvis turned -90 about X (the face up, the head toward -z), laid at the mark's height"""
    n, q, tt = B.nodes, {}, {}
    q[B.pel] = rotate_bone_world(n, B.pel, X, math.radians(-90.0), q)
    wp, _ = fk_world(n, q, tt)
    tt[B.pel] = translate_node_world(n, B.pel, [0.0, 0.11 * B.thigh / 0.42 - wp[B.pel][1], 0.0], q, tt)
    for side, sx in (("l", 1.0), ("r", -1.0)):        # the arms along the body, palms down on the bed
        ua, la, ha = B.arms[side]
        q[ua] = aim_bone(n, ua, la, [0.16 * sx, -0.08, 1.0], q, tt)
        q[la] = aim_bone(n, la, ha, [0.10 * sx, -0.10, 1.0], q, tt)
    for side, (th, ca, ft) in B.legs.items():          # the feet fall a little open
        q[th] = rotate_bone_world(n, th, Z, math.radians(-3.0 if side == "l" else 3.0), q)
    return q, tt


def mv_lie(B, t, D, o):
    q, tt = lying(B, t)
    return B.breathe(q, t, amp=1.6), tt


def mv_carry(B, t, D, o):
    n, q, tt = B.nodes, {}, {}
    q = B.hanging(q)
    wp, _ = fk_world(n, q, tt)
    tg = {}
    for side, sx in (("l", 1.0), ("r", -1.0)):
        ua, la, ha = B.arms[side]
        sh = wp[ua]
        up_len, fo_len = math.dist(B.wp[ua], B.wp[la]), math.dist(B.wp[la], B.wp[ha])
        # the upper arm hangs, the elbow at the side; the forearm level and forward (a tray carried, not offered):
        # the wrist one forearm in front of the elbow (10-22: up_len * 1.15 forward straightened the arm out like a
        # sleepwalker's)
        tg[side] = [sh[0] * 0.85, sh[1] - up_len * 0.90, sh[2] + fo_len * 0.92 + up_len * 0.12]
    q = B.hands_to(q, tt, tg, {"l": [0.3, -1.0, -0.4], "r": [-0.3, -1.0, -0.4]})
    return B.breathe(q, t, period=D, amp=0.6), tt


MOVES = [("z_sit", 4.0, mv_sit, None), ("z_sit_talk", 4.0, mv_sit_talk, None), ("z_sit_type", 2.0, mv_sit_type, None),
         # ⛔ A LYING CLIP MUST OWN ITS ROOT: the engine grounds a body by its FEET unless the clip is named z_rm_
         # (crowd_kms.c, P3 root ownership) -- Werner's first sleep was driven under the floor, his hands poking up
         ("z_rm_lie", 4.0, mv_lie, None), ("z_rm_doze", 4.0, mv_lie, {"blink_L": 1.0, "blink_R": 1.0, "blink": 1.0}),
         ("z_rm_scream", 4.0, mv_lie, {"blink_L": 1.0, "blink_R": 1.0, "blink": 1.0, "jaw": 0.85}),
         ("z_carry", 2.0, mv_carry, None),
         # the sits again, OWNING THEIR ROOT, for a SET ACTOR whose feet hang (a child on a stool): an actor's clip is
         # grounded by its feet unless it is z_rm_, and hanging feet grounded would sink him into the seat
         ("z_rm_sit", 4.0, mv_sit, None), ("z_rm_sit_talk", 4.0, mv_sit_talk, None), ("z_rm_sit_type", 2.0, mv_sit_type, None)]


def main(argv):
    pack = argv[1]
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    force = "--force" in argv
    opt = {"seat": float(argv[argv.index("--seat") + 1]) if "--seat" in argv else 0.46,
           "desk": float(argv[argv.index("--desk") + 1]) if "--desk" in argv else 0.74}
    hang = float(argv[argv.index("--hang") + 1]) if "--hang" in argv else 10.0
    p = Rpack(pack)
    for old in ("z_lie", "z_sleep", "z_scream"):         # the first names (10-22), before the root lesson
        if old in p.clip_names(): drop_clip(pack, old); p = Rpack(pack)
    out = []
    for name, D, fn, morphs in MOVES:
        if only and name not in only: continue
        if name in p.clip_names():
            if not force: print("%s already present (--force re-authors it)" % name); continue
            drop_clip(pack, name); p = Rpack(pack)
        nodes = p.nodes(); RM = rig_map(p)
        assert not RM["_missing"], "rig incomplete: %s" % RM["_missing"]
        B = Body(nodes, RM, hang)
        frames = int(round(D * FPS)) + 1
        poses = [fn(B, f / FPS, D, opt) for f in range(frames)]
        w0, _ = fk_world(nodes, poses[0][0], poses[0][1]); w1, _ = fk_world(nodes, poses[-1][0], poses[-1][1])
        gap = max(math.dist(w0[i], w1[i]) for i in range(len(nodes)))
        low = min(min(fk_world(nodes, q, tt)[0][i][1] for i in range(len(nodes))) for q, tt in poses[::max(1, frames // 6)])
        tracks = {}
        for i in range(len(nodes)):
            tracks[(i, 0)] = [float(x) for q, tt in poses for x in qnorm(q.get(i, nodes[i]["q"]))]
        tracks[(B.pel, 1)] = [float(x) for q, tt in poses for x in tt.get(B.pel, nodes[B.pel]["t"])]
        for mname, w in (morphs or {}).items():
            for mi in morph_indices(p, mname):
                tracks[(mi, 3)] = [float(w)] * frames
        bi = next((i for i, nm in enumerate(p.node_names()) if "skateboard" in nm.lower() or "board" in nm.lower()), -1)
        if bi >= 0: tracks[(bi, 1)] = [0.0, -50.0, 0.0] * frames
        print("%-10s %3d frames  first-vs-last %.4f  lowest joint %.3f (rest floor %.3f)%s" % (
            name, frames, gap, low, B.ankle_y, "  morphs %s" % sorted(morphs) if morphs else ""))
        out.append((name, FPS, frames, tracks))
    if out:
        append_clips(pack, out, force=True)
        print("appended %d clip(s) to %s" % (len(out), os.path.basename(pack)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
