#!/usr/bin/env python3
"""bvhtake.py -- a motion capture take as a body the kit can read: a BVH file behind the Rpack interface, so rig_map, fk_world, retarget.Basis, z_km's Take and the benches work on
it without a pack on disk (11-02). soma.py is the same thing for a generated take. The gaits had been measured against nothing real.
Real people walking are the reference, and these files are full of them.

    take = BvhTake("cmu-mocap-bvh/data/035/35_17.bvh")                     # metres at 60 fps, whatever the file is in
    take = BvhTake("Neutral_FW.bvh", start=327, end=2000)                                                        # frames of the file, 100STYLE's Frame_Cuts.csv gives them

Two families so far. CMU (Bruce Hahne's BVH release): 120 fps, in CMU's own unit, 0.056444 m. 100STYLE (Mason et al., CC-BY 4.0): 60 fps, centimetres. The unit goes by the family, read off
its joint names, unless `scale` says otherwise.

The names are NOT passed through, for the same reason as soma.py. ⛔ 100STYLE calls the UPPER ARM "LeftShoulder" and the clavicle "LeftCollar", and the kit's matchers read LeftShoulder as the
clavicle. Its four-bone chest loses its lowest bone to the canon's count, so the three the canon keeps run to the top, where the shoulders hang.
Rest: whatever the file's offsets say, with identity local rotations. ⛔ CMU's legs rest splayed like an A: its frame 0 is the T-pose. That is fine for a transfer, which works from
the rest it is told about, and never fine as a stance.

⛔ Licences stay with the file. CMU: free for commercial use, the raw data may not be resold, and a credit is asked. 100STYLE: CC-BY 4.0, credit required. Anything shipped
from these needs the line in the game's credits.
"""
import math

RENAME_100STYLE = {"Chest": "Waist", "Chest2": "Spine", "Chest3": "Spine1", "Chest4": "Spine2",
                   "LeftCollar": "LeftShoulder", "LeftShoulder": "LeftArm", "LeftElbow": "LeftForeArm", "LeftWrist": "LeftHand",
                   "LeftHip": "LeftUpLeg", "LeftKnee": "LeftLeg", "LeftAnkle": "LeftFoot", "LeftToe": "LeftToeBase",
                   "RightCollar": "RightShoulder", "RightShoulder": "RightArm", "RightElbow": "RightForeArm", "RightWrist": "RightHand",
                   "RightHip": "RightUpLeg", "RightKnee": "RightLeg", "RightAnkle": "RightFoot", "RightToe": "RightToeBase"}
CMU_UNIT = 0.056444
CM = 0.01
AXIS = {"X": [1.0, 0.0, 0.0], "Y": [0.0, 1.0, 0.0], "Z": [0.0, 0.0, 1.0]}


def _qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz]


class BvhTake:
    """Reads like an Rpack with one clip, "take"."""
    def __init__(self, path, start=0, end=None, scale=None, fps=60.0):
        self.path = path
        names, parents, offsets, channels = [], [], [], []
        stack = []
        with open(path, "r", errors="replace") as f:
            for line in f:
                t = line.split()
                if not t:
                    continue
                if t[0] in ("ROOT", "JOINT"):
                    names.append(t[1])
                    parents.append(stack[-1] if stack else -1)
                    offsets.append([0.0, 0.0, 0.0])
                    channels.append([])
                    stack.append(len(names) - 1)
                elif t[0] == "End":
                    stack.append(-1)
                elif t[0] == "OFFSET":
                    if stack[-1] >= 0:
                        offsets[stack[-1]] = [float(t[1]), float(t[2]), float(t[3])]
                elif t[0] == "CHANNELS":
                    channels[stack[-1]] = t[2:]
                elif t[0] == "}":
                    stack.pop()
                elif t[0] == "MOTION":
                    break
            count = int(f.readline().split()[-1])
            dt = float(f.readline().split()[-1])
            every = max(1, int(round(1.0 / dt / fps)))
            self.fps = 1.0 / dt / every
            # ⛔ Hahne's CMU release added a T-pose as frame 0: its feet are under the floor the walk has, and a jump from there to the first real frame.
            start = max(1 if "LHipJoint" in names else 0, start)
            last = count if end is None else min(count, end)
            rows = []
            for i, line in enumerate(f):
                if i >= last:
                    break
                if i >= start and (i - start) % every == 0:
                    rows.append([float(v) for v in line.split()])
        self.frames = len(rows)
        family_100 = "LeftCollar" in names
        self.unit = scale if scale is not None else (CM if family_100 else CMU_UNIT)
        self.names = [RENAME_100STYLE.get(n, n) for n in names] if family_100 else list(names)
        self.parents = parents
        self.offsets = [[v * self.unit for v in o] for o in offsets]
        self.channels = channels
        self.rows = rows
        at = 0
        self.first = []
        for c in channels:
            self.first.append(at)
            at += len(c)

    def clip_names(self):
        return ["take"]

    def clip(self, ci):
        return {"name": "take", "fps": self.fps, "frames": self.frames}

    def nodes(self):
        return [dict(name=self.names[i], parent=self.parents[i], kind=1, t=list(self.offsets[i]), q=[0.0, 0.0, 0.0, 1.0], s=[1.0, 1.0, 1.0]) for i in range(len(self.names))]

    def node_names(self):
        return list(self.names)

    def node_index(self, name):
        return self.names.index(name)

    def pose(self, f):
        """({node: q}, {node: t}) for fk_world, on a whole frame."""
        row = self.rows[f]
        q, t = {}, {}
        for j, chans in enumerate(self.channels):
            at = self.first[j]
            turn = [0.0, 0.0, 0.0, 1.0]
            place = None
            for k, c in enumerate(chans):
                v = row[at + k]
                if c.endswith("rotation"):
                    a = math.radians(v) * 0.5
                    s = math.sin(a)
                    ax = AXIS[c[0]]
                    turn = _qmul(turn, [ax[0] * s, ax[1] * s, ax[2] * s, math.cos(a)])
                else:
                    if place is None:
                        place = list(self.offsets[j])
                    place["XYZ".index(c[0])] += v * self.unit
            q[j] = turn
            if place is not None:
                t[j] = place
        return q, t


class LoopTake:
    """A loop that plays in place (a clip in a pack: Quaternius's, a score's) as a take that really walks: repeated, moved along at the speed its own feet say, on a skeleton whose
    rest has identity rotations, like the others here. So it can be cut, measured and laid by the same tools as a capture. Forward is whichever way a foot on the ground goes backward.

        take = LoopTake(Rpack("ual1.rpack"), "Walk_Loop", rename=RENAME_UAL1)

    ⛔ The speed is what the feet say, and a foot on the ground only holds still if that is right. A wrong one shows up as no foot ever counting as down.
    """
    def __init__(self, pack, clip, rename=None, cycles=5, fps=60.0, speed=None, seconds=None):
        from authorlib import fk_world, clip_pose, qmul, qinv, qrot, rig_map
        self._fk, self._pose, self._qmul, self._qinv, self._qrot = fk_world, clip_pose, qmul, qinv, qrot
        self.pack = pack
        self.clip_name = clip
        c = pack.clip(pack.clip_names().index(clip))
        self.period = c["frames"]
        self.seconds = seconds if seconds else c["frames"] / float(c["fps"])
        self.clip_fps = self.period / self.seconds
        self.fps = fps
        self.frames = int(round(cycles * self.seconds * fps))
        self.src_nodes = pack.nodes()
        names = pack.node_names()
        self.names = [(rename or {}).get(n, n) for n in names]
        rp, rq = fk_world(self.src_nodes)
        self.rest_p, self.rest_q = rp, rq
        self.parents = [nd["parent"] for nd in self.src_nodes]
        self.offsets = []
        for i, nd in enumerate(self.src_nodes):
            p = nd["parent"]
            self.offsets.append(list(rp[i]) if p < 0 else [rp[i][k] - rp[p][k] for k in range(3)])
        # What's around the character is not the character: a rig's scale-holding root stays out of the way by keeping its identity.
        self.along = [0.0, 0.0, 0.0]
        rm = rig_map(self)
        self.rm = rm
        toe = {s: rm["ball_" + s] for s in "lr"}
        samples = []
        for i in range(self.period * 2):
            w, _ = self._world(i * 0.5)
            samples.append((w[toe["l"]], w[toe["r"]]))
        drift = [0.0, 0.0, 0.0]
        rates = []
        for side in (0, 1):
            lo = min(s[side][1] for s in samples)
            for a, b in zip(samples, samples[1:]):
                if a[side][1] < lo + 0.03 and b[side][1] < lo + 0.03:
                    d = [b[side][k] - a[side][k] for k in range(3)]
                    drift = [drift[k] + d[k] for k in range(3)]
                    rates.append(math.hypot(d[0], d[2]) * self.clip_fps * 2.0)
        l = math.hypot(drift[0], drift[2]) or 1.0
        self.along = [-drift[0] / l, 0.0, -drift[2] / l]
        rates.sort()
        self.speed = speed if speed is not None else (rates[len(rates) // 2] if rates else 0.0)

    def clip_names(self):
        return ["take"]

    def clip(self, ci):
        return {"name": "take", "fps": self.fps, "frames": self.frames}

    def nodes(self):
        return [dict(name=self.names[i], parent=self.parents[i], kind=1, t=list(self.offsets[i]), q=[0.0, 0.0, 0.0, 1.0], s=[1.0, 1.0, 1.0]) for i in range(len(self.names))]

    def node_names(self):
        return list(self.names)

    def node_index(self, name):
        return self.names.index(name)

    def _world(self, frame):
        q, t = self._pose(self.pack, self.clip_name, frame, loop=self.period)
        return self._fk(self.src_nodes, q, t)

    def pose(self, f):
        """({node: q}, {node: t}) for fk_world, on a whole frame of the take."""
        w, r = self._world((f / self.fps) * self.clip_fps)
        q, t = {}, {}
        turn = [self._qmul(r[i], self._qinv(self.rest_q[i])) for i in range(len(r))]
        go = f / self.fps * self.speed
        for i, p in enumerate(self.parents):
            if p < 0:
                q[i] = turn[i]
                t[i] = [w[i][k] + self.along[k] * go for k in range(3)]
            else:
                q[i] = self._qmul(self._qinv(turn[p]), turn[i])
                # A bone's own travel (a hips track under a root) is kept: only a bone that really moved away from its rest offset gets one.
                local = self._qrot(self._qinv(turn[p]), [w[i][k] - w[p][k] for k in range(3)])
                if max(abs(local[k] - self.offsets[i][k]) for k in range(3)) > 1e-5:
                    t[i] = local
        return q, t


# Quaternius's Universal Animation Library 1 names its bones for Rigify; the kit's matchers want the canon's. As blender/build_ual_skater.py renames them in the game.
RENAME_UAL1 = {"DEF-hips": "hips", "DEF-spine.001": "spine", "DEF-spine.002": "spine_mid", "DEF-spine.003": "chest", "DEF-neck": "neck", "DEF-head": "head",
               "DEF-shoulder.L": "clavicle_L", "DEF-upper_arm.L": "upper_arm_L", "DEF-forearm.L": "forearm_L", "DEF-hand.L": "hand_L",
               "DEF-shoulder.R": "clavicle_R", "DEF-upper_arm.R": "upper_arm_R", "DEF-forearm.R": "forearm_R", "DEF-hand.R": "hand_R",
               "DEF-thigh.L": "thigh_L", "DEF-shin.L": "shin_L", "DEF-foot.L": "foot_L", "DEF-toe.L": "toe_L",
               "DEF-thigh.R": "thigh_R", "DEF-shin.R": "shin_R", "DEF-foot.R": "foot_R", "DEF-toe.R": "toe_R"}
