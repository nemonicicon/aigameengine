#!/usr/bin/env python3
"""z_km.py -- A GENERATED GAIT ON A BODY: cut a loop out of a Kimodo take (kimodo_gen.py, soma.py) and lay it on a body of another shape, in place, its feet
planted, for a game that moves the body itself. The text-to-motion lane's last step (11-02), jacob on the alien's hand-keyed gaits: "it is not natural looking".

    z_km.py <pack> <table.json> [--force] [--only z_km_mid] [--sidecar out.json] [--steps-table out_ft.json]

    {"home": os.environ.get("CLUB_KIMODO", "kimodo"), "stand": "GAME_IDLE_OFF", "width": 1.0, "accel": 20.0,   # club-engine kit: machine-independent paths
     "gaits": {"mid": {"take": "run_s2", "base": "ALIEN_JOG", "base_seconds": 0.625}, ...},
     "sets": {"": {}, "_board": {"stand": "ALIEN_IDLE_CARRY", "from": {"mid": "ALIEN_JOG_CARRY"}}, ...}}

  take          the folder under <home>/out
  base          an old gait of this body: the bones Kimodo has no word for (fingers, antennae, the eye, the prop mounts) keep what this clip does at the same
                moment of the stride, and the hands keep their set on the forearm
  width         how far apart the feet run, against the generated body's scaled: a body with big shoes needs them further apart than a person
  stretch       (per gait) stride against the scaled one, for a speed the generated body didn't go. It comes out of the leg's reach, so it is a gate
  game_speed    (per gait) what the game really moves the body at, handed on to z_ft.py: Kimodo's fastest take is 3.9 m/s, and a game may run faster than any take
  sit           how much lower the hips ride than the generated body's, in leg lengths (0.015): its legs go almost straight, and this body's foot differs
  lean_back     (per gait) degrees less lean than the source: the upper body comes back about the hips, arms with it, the neck half and the head not at all, so the eyes stay level
  sets          the same gait with the arms of another clip grafted on: a hand holding the board or the camera can't be generated (the C++ port has no
                constraints and the model knows no objects). The bones grafted are measured: those that differ between that clip and this set's plain one;
                "only" limits them to names holding that text, when the other arm differs merely by being still.

THE CUT. A take starts from standing and wanders. Every left-foot landing to the next is a candidate cycle; the one whose end comes closest to its start in a
frame turned to its own heading wins, and what's left of the gap is spread over the cycle, so it closes by construction. The take's travel comes off, so the clip is
in place, and the turn of the heading, so it goes straight along the body's forward.

THE BODY. Different bones get different rules, because the two skeletons agree on different things:
  * trunk (hips, spine, neck, head): the same movement away from upright, on this body's own upright. The generated body's posture isn't this body's posture.
  * arms: the same pose, by canonical frames (retarget.py): rests really differ between bodies, an A-pose's arm "as it was" would be out sideways.
  * legs: by IK to where the generated ankle is, scaled by leg length. Rotations would carry a person's stance onto wide hips and big shoes: the alien's
    hand-keyed feet ran 3.5 x wider than mocap. And a foot is a foot on the ground: on the ground it's pinned, its ball on one line going back at the speed.
  * feet and toes: the same roll away from flat, on this body's own flat. Over the ball of the foot the ankle rises by THIS foot's length.

THE GATES: first == last by construction, and checked; a planted foot on its line within 5 mm; nothing under the floor; no joint past what the stride explains;
every leg reaches. A clip's lineage goes in the sidecar with its numbers: take, seed, prompt, weights' revisions and hashes.
"""
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from rpack import Rpack, append_clips, drop_clip, lint_name
from authorlib import (rig_map, fk_world, solve_limb, hold_world_rotation, translate_node_world, clip_pose, qmul, qinv, qnorm, qslerp, qrot, q_axis_angle, q_between)
from retarget import Basis, char_frame
from soma import SomaTake

FPS = 60.0
UP = [0.0, 1.0, 0.0]
SIDES = ("l", "r")
IDENTITY = [0.0, 0.0, 0.0, 1.0]
# How near the lowest a toe counts as down (metres on the generated body), and how slow over the ground (share of the body's speed).
DOWN_HEIGHT = 0.035
DOWN_SPEED = 0.45
# Leave the take's first and last moments: it starts from a stand, and the end is unconstrained.
SKIP_START = 0.9
SKIP_END = 0.2
# Blend a foot on and off its line over this share of the cycle.
EDGE = 0.04
EDGE_OFF = 0.08
PLANT_TOL = 0.005
# A foot in the air may ask this much past a straight leg (leg lengths): the IK stops at straight and it trails a touch closer. On the ground it's a gate.
AIR_SLACK = 0.08


def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def add(a, b, s=1.0): return [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s]
def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def scale(a, s): return [a[0] * s, a[1] * s, a[2] * s]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def lerp(a, b, u): return a + (b - a) * u
def lerpv(a, b, u): return [lerp(a[k], b[k], u) for k in range(3)]
def smooth(u):
    u = max(0.0, min(1.0, u))
    return u * u * (3 - 2 * u)
def angle_between(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return 2.0 * math.degrees(math.acos(min(1.0, d)))


class Take:
    """A take, measured: every joint's world place and turn on every frame, and when each foot is down. A folder is a generated take; anything else with the same face
    (bvhtake.BvhTake) comes as it is."""
    def __init__(self, folder):
        self.src = SomaTake(folder) if isinstance(folder, str) else folder
        # A generated take starts from a stand; a capture or a loop is already walking.
        self.skip_start, self.skip_end = (SKIP_START, SKIP_END) if isinstance(folder, str) else (0.0, 0.0)
        self.nodes = self.src.nodes()
        self.rm = rig_map(self.src)
        rm = self.rm
        self.frames = self.src.frames
        self.fps = self.src.clip(0)["fps"]
        self.leg = {s: (rm["thigh_" + s], rm["calf_" + s], rm["foot_" + s]) for s in SIDES}
        self.toe = {s: rm["ball_" + s] for s in SIDES}
        self.wp, self.wq = [], []
        for f in range(self.frames):
            q, t = self.src.pose(f)
            w, r = fk_world(self.nodes, q, t)
            self.wp.append(w)
            self.wq.append(r)
        rp, _ = fk_world(self.nodes)
        self.rest_wp = rp
        self.leg_len = sum(math.dist(rp[a], rp[b]) + math.dist(rp[b], rp[c]) for a, b, c in self.leg.values()) / 2.0
        self.pelvis = rm["pelvis"]
        self.speed = [0.0] * self.frames
        for f in range(1, self.frames):
            d = sub(self.wp[f][self.pelvis], self.wp[f - 1][self.pelvis])
            self.speed[f] = math.hypot(d[0], d[2]) * self.fps
        self.down = {}
        self.ankle_floor = min(min(self.wp[f][self.leg[s][2]][1] for f in range(self.frames)) for s in SIDES)
        for s in SIDES:
            toe = self.toe[s]
            floor = min(self.wp[f][toe][1] for f in range(self.frames))
            flags = []
            for f in range(self.frames):
                g = max(f, 1)
                d = sub(self.wp[g][toe], self.wp[g - 1][toe])
                moving = math.hypot(d[0], d[2]) * self.fps
                flags.append(self.wp[f][toe][1] < floor + DOWN_HEIGHT and moving < max(0.25, DOWN_SPEED * self.speed[f]))
            # A flicker isn't a landing: a gap this short closes, then a touch this short goes. In time, not frames: the takes come at 30, 60 and 120 fps.
            gap, touch = int(round(0.07 * self.fps)), int(round(0.05 * self.fps))
            f = 0
            while f < self.frames:
                if flags[f]:
                    f += 1
                    continue
                g = f
                while g < self.frames and not flags[g]:
                    g += 1
                # And a foot that never left the floor hasn't landed again, however it slid: it's the same stance.
                if 0 < f and g < self.frames and (g - f <= gap or all(self.wp[k][toe][1] < floor + DOWN_HEIGHT for k in range(f, g))):
                    for k in range(f, g):
                        flags[k] = True
                f = g
            f = 0
            while f < self.frames:
                if not flags[f]:
                    f += 1
                    continue
                g = f
                while g < self.frames and flags[g]:
                    g += 1
                if g - f < touch:
                    for k in range(f, g):
                        flags[k] = False
                f = g
            self.down[s] = flags

    def landings(self, s):
        return [f for f in range(1, self.frames) if self.down[s][f] and not self.down[s][f - 1]]

    def closure(self, i, j):
        """How far the pose at j is from the pose at i, each turned to the cycle's heading: mean joint turn in degrees, plus hip height and velocity."""
        ri, rj = self.wp[i][self.pelvis], self.wp[j][self.pelvis]
        align = q_axis_angle(UP, -math.atan2(rj[0] - ri[0], rj[2] - ri[2]))
        total, count = 0.0, 0
        for k in range(len(self.nodes)):
            total += angle_between(qmul(align, self.wq[i][k]), qmul(align, self.wq[j][k]))
            count += 1
        return total / count + abs(ri[1] - rj[1]) * 100.0

    def best_cycle(self):
        lo, hi = int(self.skip_start * self.fps), self.frames - 1 - int(self.skip_end * self.fps)
        land = [f for f in self.landings("l") if lo <= f <= hi]
        best = None
        for a, b in zip(land, land[1:]):
            right = [f for f in self.landings("r") if a < f < b]
            if len(right) != 1:
                continue
            err = self.closure(a, b)
            # The two steps should take about as long as each other: a limp reads at once.
            lop = abs((right[0] - a) / float(b - a) - 0.5)
            score = err + 40.0 * lop
            if best is None or score < best[0]:
                best = (score, a, b, err, lop)
        return best


class Cycle:
    """A cut of a take, in place, straight and closed. Phase 0 is the left foot landing."""
    def __init__(self, take, i, j):
        self.take, self.i, self.j = take, i, j
        self.n = j - i
        self.T = self.n / take.fps
        ri, rj = take.wp[i][take.pelvis], take.wp[j][take.pelvis]
        travel = sub(rj, ri)
        travel[1] = 0.0
        self.travel = math.hypot(travel[0], travel[2])
        self.speed = self.travel / self.T
        self.align = q_axis_angle(UP, -math.atan2(travel[0], travel[2]))
        # Positions: turned, the travel off, and what's left of the gap spread over the cycle. Forward is +Z, the body's left +X.
        self.p, self.w = [], []
        first = {}
        count = len(take.nodes)
        pos_end = [self._in_place(j, k) for k in range(count)]
        pos_start = [self._in_place(i, k) for k in range(count)]
        rot_start = [qnorm(qmul(self.align, take.wq[i][k])) for k in range(count)]
        rot_end = [qnorm(qmul(self.align, take.wq[j][k])) for k in range(count)]
        for f in range(self.n):
            u = f / float(self.n)
            pp, ww = [], []
            for k in range(count):
                gap = sub(pos_end[k], pos_start[k])
                pp.append(add(self._in_place(i + f, k), gap, -u))
                delta = qmul(rot_start[k], qinv(rot_end[k]))
                if delta[3] < 0.0:
                    delta = [-x for x in delta]
                ww.append(qnorm(qmul(qslerp(IDENTITY, delta, u), qmul(self.align, take.wq[i + f][k]))))
            self.p.append(pp)
            self.w.append(ww)
        self.down = {s: [take.down[s][i + f] for f in range(self.n)] for s in SIDES}
        self.stance = {s: self._interval(self.down[s]) for s in SIDES}

    def _in_place(self, f, k):
        r0 = self.take.wp[self.i][self.take.pelvis]
        d = sub(self.take.wp[f][k], [r0[0], 0.0, r0[2]])
        return add(qrot(self.align, d), [0.0, 0.0, 1.0], -self.speed * (f - self.i) / self.take.fps)

    @staticmethod
    def _interval(flags):
        """The longest run of True round the cycle, as (start, length) in frames."""
        n = len(flags)
        best = (0, 0)
        for a in range(n):
            if flags[a] and not flags[a - 1]:
                k = 0
                while k < n and flags[(a + k) % n]:
                    k += 1
                if k > best[1]:
                    best = (a, k)
        return best

    def at(self, phase):
        """(positions, rotations) at a phase, interpolated, round the loop."""
        f = (phase % 1.0) * self.n
        a = int(f) % self.n
        b = (a + 1) % self.n
        u = f - int(f)
        return ([lerpv(self.p[a][k], self.p[b][k], u) for k in range(len(self.p[a]))],
                [qnorm(qslerp(self.w[a][k], self.w[b][k], u)) for k in range(len(self.w[a]))])


class Body:
    def __init__(self, p, stand_clip):
        self.p = p
        self.nodes = p.nodes()
        self.names = p.node_names()
        self.rm = rig_map(p)
        assert not self.rm["_missing"], "rig incomplete: %s" % self.rm["_missing"]
        rm = self.rm
        self.leg = {s: (rm["thigh_" + s], rm["calf_" + s], rm["foot_" + s]) for s in SIDES}
        self.toe = {s: rm["ball_" + s] for s in SIDES}
        rp, rq = fk_world(self.nodes)
        self.leg_len = sum(math.dist(rp[a], rp[b]) + math.dist(rp[b], rp[c]) for a, b, c in self.leg.values()) / 2.0
        # No stand named: the rest pose is the stand.
        self.stand_q, self.stand_t = clip_pose(p, stand_clip, 0.0) if stand_clip else ({}, {})
        wp, wq = fk_world(self.nodes, self.stand_q, self.stand_t)
        self.stand_wp, self.stand_wq = wp, wq
        # Forward comes off the stand's hips. Off the feet it's whatever way a stand's feet happen to point, and a rest may face another way than the clips do: the alien
        # rests facing +Z and its clips all go -X.
        hips = sub(wp[self.leg["l"][0]], wp[self.leg["r"][0]])
        hips[1] = 0.0
        self.fwd = norm(cross(norm(hips), UP))
        self.left = norm(cross(UP, self.fwd))
        # The trunk's upright: this body's rest, turned to go the way its clips go. A stand's slouch and a stand's hips are the stand's: a gait has its own.
        rest_hips = sub(rp[self.leg["l"][0]], rp[self.leg["r"][0]])
        rest_hips[1] = 0.0
        turn = q_between(norm(cross(norm(rest_hips), UP)), self.fwd)
        self.upright_wq = [qnorm(qmul(turn, r)) for r in rq]
        self.floor_ankle = {s: wp[self.leg[s][2]][1] for s in SIDES}
        self.floor_toe = {s: wp[self.toe[s]][1] for s in SIDES}
        self.to_toe = {s: qrot(qinv(wq[self.leg[s][2]]), sub(wp[self.toe[s]], wp[self.leg[s][2]])) for s in SIDES}
        # A stand's feet turn out, and a walk's don't: laid on top of a take's own turn they'd go out 19 degrees on every step. A foot in a gait starts from this body's foot,
        # pointing straight along its forward, flat as it stands.
        self.gait_wq = {}
        self.foot_reach = {}
        for s in SIDES:
            along = sub(wp[self.toe[s]], wp[self.leg[s][2]])
            along[1] = 0.0
            self.foot_reach[s] = math.sqrt(dot(along, along))
            straight = q_between(norm(along), self.fwd)
            self.gait_wq[self.leg[s][2]] = qnorm(qmul(straight, wq[self.leg[s][2]]))
            self.gait_wq[self.toe[s]] = qnorm(qmul(straight, wq[self.toe[s]]))
        self.basis = Basis(p)
        self.pelvis_home = lerpv(wp[self.leg["l"][0]], wp[self.leg["r"][0]], 0.5)
        # Which of the generated body's trunk joints drives which of this one's: the top of the back carries the shoulders, so a two-joint spine takes the
        # middle and the top, not the bottom two.
        self.trunk = [(rm["pelvis"], "pelvis")]
        spine = []
        for k in ("spine_01", "spine_02", "spine_03"):
            if rm[k] not in spine:
                spine.append(rm[k])
        self.spine = spine
        self.neck, self.head = rm["neck_01"], rm["head"]
        self.limb_bones = set()
        for s in SIDES:
            self.limb_bones |= set(self.leg[s]) | {self.toe[s]}


def lay_gait(body, cycle, spec, opts, name):
    """The generated cycle on this body: frames at 60 fps, and the stance lines."""
    take = cycle.take
    rm_d, rm_t = take.rm, body.rm
    s_len = body.leg_len / take.leg_len
    stretch = float(spec.get("stretch", 1.0))
    width = float(opts.get("width", 1.0))
    sit = float(spec.get("sit", opts.get("sit", 0.015))) * body.leg_len
    # How long a stride takes on a body this size. A body swings its legs like pendulums, so time goes with the square root of its size (dynamic similarity, Alexander 1989); kept at
    # the donor's time, a shorter body moves in slow motion. Off by default for the old tables: "froude": true.
    hurry = math.sqrt(s_len) if spec.get("froude", opts.get("froude", False)) else 1.0
    frames = int(round(cycle.T * hurry * FPS))
    T = frames / FPS
    clip_speed = cycle.speed * s_len * stretch * cycle.T / T
    fwd, left = body.fwd, body.left
    face = q_between([0.0, 0.0, 1.0], fwd)
    def world_of(v):
        """A generated body's in-place vector, in this body's turned frame, unscaled."""
        return add(add(scale(left, v[0]), scale(UP, v[1])), scale(fwd, v[2]))
    # Canonical frames for the arms.
    dB, tB = opts["donor_basis"], body.basis
    GG = qmul(q_between(tB.fwd, fwd), qmul(char_frame(tB), qinv(char_frame(dB))))
    arm_canon = (6, 7, 8, 9, 10, 11)
    # The base clip's own time for the moment of the stride.
    base = spec["base"]
    base_c = body.p.clip(body.p.clip_names().index(base))
    base_fps = base_c["fps"]
    base_T = float(spec.get("base_seconds", base_c["frames"] / base_fps))
    base_land = float(spec.get("base_land", 0.0))
    # Where each toe's stance line runs, and how each foot turns. A foot's rest means nothing across bodies (CMU's points 12 degrees down, the alien's stand 19 out), but both
    # lie flat on the ground in mid-stance: a foot is the same turn away from that, on this body's own flat foot, pointing the way the donor's pointed there.
    stance = {}
    flat = {}
    for s in SIDES:
        a, k = cycle.stance[s]
        assert k > 0, "%s: the %s foot never comes down in the cut" % (name, s)
        toe_d, ankle_d = take.toe[s], take.leg[s][2]
        n = cycle.n
        along = [cycle.p[(a + m) % n][toe_d][2] + cycle.speed * m / take.fps for m in range(k)]
        across = [cycle.p[(a + m) % n][toe_d][0] for m in range(k)]
        lowest = None
        for m in range(k):
            at = (a + m) % n
            v = sub(cycle.p[at][toe_d], cycle.p[at][ankle_d])
            pitch = math.atan2(-v[1], math.hypot(v[0], v[2]))
            if lowest is None or pitch < lowest[0]:
                lowest = (pitch, at, v)
        _pitch, at, v = lowest
        yaw = math.atan2(v[0], v[2])
        out = q_axis_angle(UP, yaw)
        flat[s] = dict(foot=qinv(cycle.w[at][ankle_d]), toe=qinv(cycle.w[at][toe_d]),
                       foot_base=qnorm(qmul(out, body.gait_wq[body.leg[s][2]])), toe_base=qnorm(qmul(out, body.gait_wq[body.toe[s]])))
        # The line is for the ball of THIS foot: a longer foot's ball sits further ahead of the same ankle.
        reach = body.foot_reach[s]
        stance[s] = dict(start=a / float(n), length=k / float(n), z0=sum(along) / k, x=sum(across) / k,
                         ahead=reach * math.cos(yaw) - v[2] * s_len * stretch, aside=reach * math.sin(yaw) - v[0] * s_len * width)
    stand_hip_d = take.ankle_floor
    upright = body.stand_wq if opts.get("trunk") == "stand" else body.upright_wq
    lean_back = math.radians(float(spec.get("lean_back", 0.0)))
    back = q_axis_angle(left, -lean_back)
    back_half = q_axis_angle(left, -lean_back * 0.5)
    qs, ts, checks = [], [], []
    reach_worst = 0.0
    reach_at = ""
    for f in range(frames):
        ph = f / float(frames)
        pp, ww = cycle.at(ph)
        base_frame = (base_land + ph) * base_T * base_fps
        qb, tb = clip_pose(body.p, base, base_frame, loop=base_T * base_fps)
        q, t = dict(qb), dict(tb)
        # Trunk: the same movement away from upright, on this body's own upright.
        def trunk_world(k_d, bone, turn=back):
            D = qmul(qmul(face, ww[k_d]), qinv(face))
            return qnorm(qmul(turn, qmul(D, upright[bone])))
        pel = rm_t["pelvis"]
        pel_d = rm_d["pelvis"]
        q[pel] = hold_world_rotation(body.nodes, pel, trunk_world(pel_d, pel), q, t)
        # The hip joints go where the donor's go, in leg lengths. Not the pelvis bone: on SOMA it sits 8 cm above them, on CMU 2 cm behind, on the mannequin 5, on the alien neither.
        # Placed by the bone, the alien's legs came out 8% short of the ground, and with the height fixed its trailing knee still never straightened.
        joints_d = lerpv(pp[take.leg["l"][0]], pp[take.leg["r"][0]], 0.5)
        target = add(add(scale(left, joints_d[0] * s_len), scale(fwd, joints_d[2] * s_len * stretch)),
                     [body.pelvis_home[0], 0.0, body.pelvis_home[2]])
        target[1] = (joints_d[1] - stand_hip_d) * s_len + min(body.floor_ankle.values()) - sit
        wp, _ = fk_world(body.nodes, q, t)
        here = lerpv(wp[body.leg["l"][0]], wp[body.leg["r"][0]], 0.5)
        t[pel] = translate_node_world(body.nodes, pel, sub(target, here), q, t)
        donors = {2: [rm_d["spine_02"], rm_d["spine_03"]], 3: [rm_d["spine_01"], rm_d["spine_02"], rm_d["spine_03"]]}[len(body.spine)]
        for bone, k_d in zip(body.spine, donors):
            q[bone] = hold_world_rotation(body.nodes, bone, trunk_world(k_d, bone), q, t)
        q[body.neck] = hold_world_rotation(body.nodes, body.neck, trunk_world(rm_d["neck_01"], body.neck, back_half), q, t)
        q[body.head] = hold_world_rotation(body.nodes, body.head, trunk_world(rm_d["head"], body.head, IDENTITY), q, t)
        # Arms: the same pose, by canonical frames. The hands keep the base clip's set on the forearm.
        for c in arm_canon:
            db, tb_i = dB.pb[c], tB.pb[c]
            if db < 0 or tb_i < 0 or dB.A[c] is None or tB.A[c] is None:
                continue
            D = qmul(ww[db], qinv(dB.W[c]))
            w_t = qnorm(qmul(back, qmul(qmul(GG, qmul(D, dB.A[c])), qmul(qinv(tB.A[c]), tB.W[c]))))
            q[tb_i] = hold_world_rotation(body.nodes, tb_i, w_t, q, t)
        # Legs.
        for s in SIDES:
            leg = body.leg[s]
            d_leg = take.leg[s]
            fl = flat[s]
            foot_w = qnorm(qmul(qmul(qmul(face, qmul(ww[d_leg[2]], fl["foot"])), qinv(face)), fl["foot_base"]))
            r = qrot(foot_w, body.to_toe[s])
            a_d = pp[d_leg[2]]
            a_t = add(add(scale(left, a_d[0] * s_len * width), scale(fwd, a_d[2] * s_len * stretch)), [body.pelvis_home[0], 0.0, body.pelvis_home[2]])
            a_t[1] = (a_d[1] - stand_hip_d) * s_len + body.floor_ankle[s]
            # On the ground: the ball of the foot on its line, going back at the speed, and the ankle wherever this foot's length puts it.
            st = stance[s]
            into = (ph - st["start"]) % 1.0
            w_on = 0.0
            if into < st["length"]:
                # A foot about to leave carries little, and a leg straight at push-off can't quite reach the end of a line laid for another body's: it lets go sooner than it lands.
                w_on = smooth(into / EDGE) * smooth((st["length"] - into) / EDGE_OFF)
            line_z = (st["z0"] - cycle.speed * (into * cycle.T)) * s_len * stretch + st["ahead"]
            line_x = st["x"] * s_len * width + st["aside"]
            ball = add(add(scale(left, line_x), scale(fwd, line_z)), [body.pelvis_home[0], 0.0, body.pelvis_home[2]])
            pinned = [ball[0] - r[0], max(body.floor_ankle[s], body.floor_toe[s] - r[1]), ball[2] - r[2]]
            if w_on > 0.0:
                a_t = lerpv(a_t, pinned, w_on)
                if w_on >= 0.999:
                    checks.append((f, s, into, line_z))
            # Nothing under the floor.
            a_t[1] = max(a_t[1], body.floor_ankle[s] - 0.015, body.floor_toe[s] - r[1])
            k_d = pp[d_leg[1]]
            th_d, an_d = pp[d_leg[0]], pp[d_leg[2]]
            line = norm(sub(an_d, th_d))
            off = sub(k_d, th_d)
            bend = sub(off, scale(line, dot(off, line)))
            pole = norm(add(world_of(norm(bend)) if dot(bend, bend) > 1e-8 else scale(fwd, 1.0), fwd, 0.3))
            wp, _ = fk_world(body.nodes, q, t)
            reach_now = math.dist(wp[leg[0]], a_t) / body.leg_len - AIR_SLACK * (1.0 - w_on)
            if reach_now > reach_worst:
                reach_worst = reach_now
                reach_at = "phase %.2f %s foot, %s, %.3f ahead of the hip, %.3f under it" % (ph, s, "on the ground" if w_on > 0.5 else "in the air", dot(sub(a_t, wp[leg[0]]), fwd), wp[leg[0]][1] - a_t[1])
            q.update(solve_limb(body.nodes, leg, a_t, pole=pole, q_override=q, t_override=t))
            q[leg[2]] = hold_world_rotation(body.nodes, leg[2], foot_w, q, t)
            toe_w = qnorm(qmul(qmul(qmul(face, qmul(ww[take.toe[s]], fl["toe"])), qinv(face)), fl["toe_base"]))
            q[body.toe[s]] = hold_world_rotation(body.nodes, body.toe[s], toe_w, q, t)
        qs.append(q)
        ts.append(t)
    return qs, ts, dict(T=T, speed=clip_speed, checks=checks, reach=reach_worst, reach_at=reach_at, stance=stance, frames=frames, s_len=s_len, stretch=stretch)


def graft(body, qs, spec_base, base_T, base_land, clip_from, plain_from, name, only=""):
    """The same gait with another clip's arms: the bones that clip keeps differently from its plain self, measured over its cycle, come from it at the same moment."""
    p = body.p
    fps_from = p.clip(p.clip_names().index(clip_from))["fps"]
    samples = 12
    diff = {}
    for i in range(samples):
        frame = (i / float(samples)) * base_T * fps_from
        qa, _ = clip_pose(p, clip_from, frame, loop=base_T * fps_from)
        qb, _ = clip_pose(p, plain_from, frame, loop=base_T * fps_from)
        for k in set(qa) | set(qb):
            a = qa.get(k, body.nodes[k]["q"])
            b = qb.get(k, body.nodes[k]["q"])
            diff[k] = diff.get(k, 0.0) + angle_between(a, b) / samples
    bones = set(k for k, v in diff.items() if v > 4.0 and k not in body.limb_bones and k not in body.spine and k != body.rm["pelvis"] and k != body.neck and k != body.head
                and only in body.names[k])
    # A grafted arm comes whole, from the chest out. Its rotations are against its parents: left on the gait's own shoulder, the alien's carrying hand went from 26 cm out at the
    # hip to 47 cm out at the shoulder (jacob: "the board carrying is entirely wrong").
    for s in SIDES:
        if body.rm["upperarm_" + s] in bones:
            bones.add(body.rm["clavicle_" + s])
    bones = sorted(bones)
    out = []
    frames = len(qs)
    for f in range(frames):
        ph = f / float(frames)
        frame = (base_land + ph) * base_T * fps_from
        qa, _ = clip_pose(p, clip_from, frame, loop=base_T * fps_from)
        q = dict(qs[f])
        for k in bones:
            q[k] = qa.get(k, body.nodes[k]["q"])
        out.append(q)
    print("   %s grafts %d bones from %s: %s" % (name, len(bones), clip_from, ", ".join(body.names[k] for k in bones)[:150]))
    return out


def bake(body, name, qs, ts, r, cycle):
    """THE SCORE IS THE TEST."""
    nodes = body.nodes
    frames = len(qs)
    worst_line = 0.0
    fwd = body.fwd
    for f, s, into, line_z in r["checks"]:
        wp, _ = fk_world(nodes, qs[f], ts[f])
        here = dot(sub(wp[body.toe[s]], [body.pelvis_home[0], 0.0, body.pelvis_home[2]]), fwd)
        worst_line = max(worst_line, abs(here - line_z))
    lows = [1e9, 1e9]
    fastest = 0.0
    prev = None
    close = 0.0
    visible = [i for i in range(len(nodes)) if not any(w in body.names[i].lower() for w in ("prop", "grip", "board"))]
    for f in range(frames + 1):
        wp, _ = fk_world(nodes, qs[f % frames], ts[f % frames])
        for s in SIDES:
            lows[0] = min(lows[0], wp[body.leg[s][2]][1] - body.floor_ankle[s])
            lows[1] = min(lows[1], wp[body.toe[s]][1] - body.floor_toe[s])
        if prev is not None:
            fastest = max(fastest, max(math.dist(wp[i], prev[i]) for i in visible) * FPS)
        prev = wp
    print("%-20s %3d f %.3f s %5.2f m/s  plant %.4f  under floor ankle %+.3f toe %+.3f  reach %.2f  fastest %4.1f m/s" %
          (name, frames, r["T"], r["speed"], worst_line, lows[0], lows[1], r["reach"], fastest))
    assert worst_line < PLANT_TOL, "%s: a planted foot left its line by %.4f" % (name, worst_line)
    assert lows[0] > -0.03 and lows[1] > -0.03, "%s: under the floor" % name
    assert r["reach"] < 1.01, "%s: a leg can't reach (%.2f of its length, %s): less stretch, or lower hips" % (name, r["reach"], r["reach_at"])
    assert fastest < 10.0 + 1.6 * r["speed"], "%s: a joint hits %.1f m/s at %.1f m/s: a pop" % (name, fastest, r["speed"])
    tracks = {}
    for i in range(len(nodes)):
        tracks[(i, 0)] = [float(x) for f in range(frames) for x in qnorm(qs[f].get(i, nodes[i]["q"]))]
    for i in set(k for t in ts for k in t):
        tracks[(i, 1)] = [float(x) for f in range(frames) for x in ts[f].get(i, nodes[i]["t"])]
    return (name, FPS, frames, tracks)


def take_source(spec, home):
    """Where a gait comes from: "take" (a folder under <home>/out), "bvh" (a capture under <home>, "first"/"last" its frames), or "pack" and "clip" (a loop that plays in place).
    A gait's own "home" wins over the table's: one table can mix libraries, as jacob picks a source per gait."""
    home = spec.get("home", home)
    if "bvh" in spec:
        from bvhtake import BvhTake
        return BvhTake(os.path.join(home, spec["bvh"]), start=int(spec.get("first", 0)), end=spec.get("last"))
    if "pack" in spec:
        from bvhtake import LoopTake, RENAME_UAL1
        return LoopTake(Rpack(spec["pack"]), spec["clip"], rename=RENAME_UAL1)
    return os.path.join(home, "out", spec["take"])


def main(argv):
    pack, table_path = argv[1], argv[2]
    force = "--force" in argv
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else None
    sidecar = argv[argv.index("--sidecar") + 1] if "--sidecar" in argv else None
    ft_out = argv[argv.index("--steps-table") + 1] if "--steps-table" in argv else None
    table = json.load(open(table_path, encoding="utf-8"))
    home = table.get("home", "")
    p = Rpack(pack)
    body = Body(p, table["stand"])
    print("z_km: body leg %.3f, faces %s" % (body.leg_len, [round(x, 2) for x in body.fwd]))
    out, info = [], {}
    made = {}
    for pace, spec in table["gaits"].items():
        take = Take(take_source(spec, home))
        spec.setdefault("take", spec.get("bvh", spec.get("clip", "")))
        best = take.best_cycle()
        assert best is not None, "%s: no clean cycle in this take" % spec["take"]
        score, i, j, err, lop = best
        if "cut" in spec:
            i, j = spec["cut"]
            err, lop = take.closure(i, j), 0.0
        cycle = Cycle(take, i, j)
        print(" %s: %s frames %d..%d, %.3f s, %.2f m/s on a %.2f m leg, closes to %.1f deg, steps off even by %.2f" % (pace, spec["take"], i, j, cycle.T, cycle.speed, take.leg_len, err, lop))
        qs, ts, r = lay_gait(body, cycle, spec, dict(table, donor_basis=Basis(take.src)), "z_km_" + pace)
        made[pace] = (qs, ts, r, cycle, spec, take)
    gaits_of_set = {}
    for set_name, spec in table["sets"].items():
        for pace, (qs, ts, r, cycle, gspec, take) in made.items():
            source = spec.get("from", {}).get(pace)
            if set_name and not source:
                continue
            name = "z_km_%s%s" % (pace, set_name)
            if only and name not in only:
                continue
            bad = lint_name(name)
            assert not bad, "%s trips the engine's miners: %s" % (name, bad)
            use = qs
            if source:
                use = graft(body, qs, gspec["base"], float(gspec.get("base_seconds", 0.0)) or p.clip(p.clip_names().index(gspec["base"]))["frames"] / 30.0,
                            float(gspec.get("base_land", 0.0)), source, gspec["base"], name, spec.get("only", ""))
            out.append(bake(body, name, use, ts, r, cycle))
            lineage = os.path.join(gspec.get("home", home), "out", gspec["take"], "take.json")
            record = json.load(open(lineage, encoding="utf-8")) if os.path.exists(lineage) else {}
            down = {s: (cycle.stance[s][0] / float(cycle.n)) for s in SIDES}
            info[name] = {"pace": pace, "seconds": r["T"], "speed": r["speed"], "faces": body.fwd, "foot_down": [down["l"], down["r"]], "take": gspec["take"], "credit": gspec.get("credit"), "cut_frames": [cycle.i, cycle.j],
                          "prompt": record.get("segments"), "seed": record.get("seed"), "steps": record.get("steps"), "weights": record.get("weights"), "source": record.get("source")}
            gaits_of_set.setdefault(set_name, {})[pace] = name
    for name in info:
        if name in p.clip_names():
            assert force, "%s already present (--force re-authors it)" % name
            drop_clip(pack, name)
    append_clips(pack, out, force=True)
    print("z_km: appended %d clips to %s" % (len(out), os.path.basename(pack)))
    if sidecar:
        json.dump(info, open(sidecar, "w", encoding="utf-8"), indent=1, sort_keys=True)
    if ft_out:
        steps = {"accel": table["accel"], "paces": {}, "sets": {}}
        for pace, (qs, ts, r, cycle, gspec, take) in made.items():
            steps["paces"][pace] = {"period": r["T"], "speed": r["speed"], "game_speed": float(gspec.get("game_speed", r["speed"]))}
        for set_name, spec in table["sets"].items():
            steps["sets"][set_name] = {"idle": spec.get("stand", table["stand"]), "paces": gaits_of_set.get(set_name, {})}
        json.dump(steps, open(ft_out, "w", encoding="utf-8"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
