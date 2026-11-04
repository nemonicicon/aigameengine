#!/usr/bin/env python3
"""gaitlab.py -- a gait read the way a gait lab reads one: every joint's angle through the stride, side by side with a reference (11-02).

    gaitlab.py <source> <source> ...      a source is  <pack>#<clip>[:cycle seconds]  or  take:<folder>   (a Kimodo take: its best cycle, before anything is done to it)
    Add --rms to compare every source against the first, one line a joint.

jacob, on a gait that passed every other bench: "how can you look at how he is moving and say it is okay?" He was right. A bench that only measures the feet can't see a
walk, and neither can anyone reading single frames. What a gait lab does is plot the joints against known-good curves, so that is what this does. Phase 0 is the left foot
landing; every angle is in the plane of travel, in degrees, the same on any body whatever its bones are called:

    thigh       from straight down, + forward       knee   bend, 0 is straight
    shin        from straight down, + forward       foot   pitch from its own flat at stand: + toes down
    trunk       lean, + forward                     arm    upper arm from down, + forward      elbow  bend, 0 is straight
    hip turn    hip line's yaw, + right hip forward     shoulder turn   the shoulder line's yaw against the hips, against is the natural sign
    hip drop    hip line's roll, + right side low       out   the upper arm away from the body, + out

Numbers to hold them to: real mocap on this same bench. What the textbooks give for a walk (Perry & Burnfield): knee about 5 at contact, 15 to 20 loading, near 5 in mid stance, 60 at
the peak of swing; thigh about +25 at contact to -15 at toe-off. A curve near a reference doesn't make a gait good; jacob's eye does. A curve far off is a thing that's wrong.
"""
import json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author", "moves"))
from rpack import Rpack
from authorlib import rig_map, fk_world, clip_pose

UP = [0.0, 1.0, 0.0]
STEPS = 100


def sub(a, b): return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
def add(a, b): return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def length(a): return math.sqrt(dot(a, a))
def norm(a):
    l = length(a) or 1.0
    return [a[0] / l, a[1] / l, a[2] / l]
def cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


class PackSource:
    """A clip in a pack, as named joints round its cycle."""
    def __init__(self, spec):
        path, clip = spec.split("#", 1)
        seconds = None
        if ":" in clip:
            clip, sec = clip.rsplit(":", 1)
            seconds = float(sec)
        self.name = clip
        self.p = Rpack(path)
        self.nodes = self.p.nodes()
        self.rm = rig_map(self.p)
        c = self.p.clip(self.p.clip_names().index(clip))
        self.clip = clip
        self.fps = c["fps"]
        self.period = seconds * c["fps"] if seconds else c["frames"]
        self.seconds = self.period / self.fps
        wp, _ = fk_world(self.nodes)
        rm = self.rm
        self.leg = (math.dist(wp[rm["thigh_l"]], wp[rm["calf_l"]]) + math.dist(wp[rm["calf_l"]], wp[rm["foot_l"]]))

    def joints(self, phase):
        q, t = clip_pose(self.p, self.clip, (phase % 1.0) * self.period, loop=self.period)
        w, _ = fk_world(self.nodes, q, t)
        return {k: w[v] for k, v in self.rm.items() if not k.startswith("_") and isinstance(v, int) and v >= 0}


class TakeSource:
    """A take's best cycle, straight and in place, before any body but its own: take:<Kimodo folder>, bvh:<file>[@first-last], loop:<pack>#<clip> (in place, moved along by its feet)."""
    def __init__(self, spec):
        from z_km import Take, Cycle
        from bvhtake import BvhTake, LoopTake, RENAME_UAL1
        kind, what = spec.split(":", 1)
        if kind == "take":
            self.name = os.path.basename(what)
            take = Take(what)
        elif kind == "bvh":
            path, _, span = what.partition("@")
            a, _, b = span.partition("-")
            self.name = os.path.basename(path).rsplit(".", 1)[0] + ("@" + span if span else "")
            take = Take(BvhTake(path, start=int(a) if a else 0, end=int(b) if b else None))
        else:
            path, clip = what.split("#", 1)
            self.name = clip
            take = Take(LoopTake(Rpack(path), clip, rename=RENAME_UAL1))
        best = take.best_cycle()
        assert best, "%s has no clean cycle" % spec
        self.cycle = Cycle(take, best[1], best[2])
        self.rm = take.rm
        self.seconds = self.cycle.T
        self.leg = take.leg_len

    def joints(self, phase):
        p, _ = self.cycle.at(phase)
        return {k: p[v] for k, v in self.rm.items() if not k.startswith("_") and isinstance(v, int) and v >= 0}


def source(spec):
    return TakeSource(spec) if spec.split(":", 1)[0] in ("take", "bvh", "loop") else PackSource(spec)


def read(src):
    rows = [src.joints(i / float(STEPS)) for i in range(STEPS)]
    # Forward: the opposite of how a foot on the ground goes, on both feet.
    floor = {s: min(r["ball_" + s][1] for r in rows) for s in "lr"}
    drift = [0.0, 0.0, 0.0]
    for s in "lr":
        for i in range(STEPS):
            if rows[i]["ball_" + s][1] < floor[s] + 0.03 * src.leg:
                drift = add(drift, sub(rows[(i + 1) % STEPS]["ball_" + s], rows[i]["ball_" + s]))
    drift[1] = 0.0
    fwd = norm([-drift[0], 0.0, -drift[2]])
    left = norm(cross(UP, fwd))
    # Phase 0: the left foot landing: where the swing ends and the ball comes down.
    down = [rows[i]["ball_l"][1] < floor["l"] + 0.03 * src.leg for i in range(STEPS)]
    start = next((i for i in range(STEPS) if down[i] and not down[i - 1]), 0)
    down_r = [rows[i]["ball_r"][1] < floor["r"] + 0.03 * src.leg for i in range(STEPS)]
    rows = rows[start:] + rows[:start]
    down = down[start:] + down[:start]
    down_r = down_r[start:] + down_r[:start]

    def sag(v):
        return math.degrees(math.atan2(dot(v, fwd), -v[1]))

    def yaw(v):
        return math.degrees(math.atan2(dot(v, fwd), dot(v, left)))

    def pitch(a, b):
        v = sub(b, a)
        return math.degrees(math.atan2(-v[1], max(1e-6, math.hypot(dot(v, fwd), dot(v, left)))))

    def bend(a, b, c):
        u, v = norm(sub(a, b)), norm(sub(c, b))
        return 180.0 - math.degrees(math.acos(max(-1.0, min(1.0, dot(u, v)))))

    # The foot's pitch when it lies flat: its pitch at the flattest point of its time on the ground.
    flat = {s: min(pitch(r["foot_" + s], r["ball_" + s]) for r, d in zip(rows, down if s == "l" else down_r) if d) for s in "lr"}
    curves = {k: [] for k in ("thigh L", "knee L", "shin L", "foot L", "toe out L", "thigh R", "knee R", "foot R", "trunk", "hip turn", "hip drop", "shoulder turn", "arm L", "elbow L", "out L", "arm R", "elbow R", "out R", "hip height", "head height", "foot L height")}
    for r in rows:
        hips = sub(r["thigh_l"], r["thigh_r"])
        shoulders = sub(r["upperarm_l"], r["upperarm_r"])
        for s, S in (("l", "L"), ("r", "R")):
            th = sag(sub(r["calf_" + s], r["thigh_" + s]))
            sh = sag(sub(r["foot_" + s], r["calf_" + s]))
            curves["thigh " + S].append(th)
            curves["knee " + S].append(bend(r["thigh_" + s], r["calf_" + s], r["foot_" + s]))
            curves["foot " + S].append(pitch(r["foot_" + s], r["ball_" + s]) - flat[s])
            ua = sub(r["lowerarm_" + s], r["upperarm_" + s])
            curves["arm " + S].append(sag(ua))
            curves["elbow " + S].append(bend(r["upperarm_" + s], r["lowerarm_" + s], r["hand_" + s]))
            side = 1.0 if s == "l" else -1.0
            curves["out " + S].append(math.degrees(math.atan2(side * dot(ua, left), -ua[1])))
        curves["shin L"].append(sag(sub(r["foot_l"], r["calf_l"])))
        v = sub(r["ball_l"], r["foot_l"])
        curves["toe out L"].append(math.degrees(math.atan2(dot(v, left), dot(v, fwd))))
        lean = sub(r["neck_01"], r["pelvis"])
        curves["trunk"].append(math.degrees(math.atan2(dot(lean, fwd), lean[1])))
        curves["hip turn"].append(yaw(hips))
        curves["hip drop"].append(math.degrees(math.asin(max(-1.0, min(1.0, hips[1] / (length(hips) or 1.0))))))
        curves["shoulder turn"].append(yaw(shoulders) - yaw(hips))
        curves["hip height"].append(r["pelvis"][1] / src.leg)
        curves["head height"].append(r["head"][1] / src.leg)
        curves["foot L height"].append((r["foot_l"][1] - min(q["foot_l"][1] for q in rows)) / src.leg)
    info = dict(name=src.name, seconds=src.seconds, leg=src.leg, duty_l=sum(1 for d in down if d) / float(STEPS), duty_r=sum(1 for d in down_r if d) / float(STEPS),
                right_lands=next((i for i in range(STEPS) if down_r[i] and not down_r[i - 1]), -1) / float(STEPS))
    # How fast it goes: how fast a foot on the ground goes back, in the middle half of its time there.
    rates = []
    for s, flags in (("l", down), ("r", down_r)):
        run = [i for i in range(STEPS) if flags[i] and flags[(i + 1) % STEPS]]
        for i in run:
            d = sub(rows[(i + 1) % STEPS]["ball_" + s], rows[i]["ball_" + s])
            rates.append(math.hypot(d[0], d[2]) * STEPS / src.seconds)
    rates.sort()
    speed = rates[len(rates) // 2] if rates else 0.0
    info["speed"] = speed
    info["froude"] = speed * speed / (9.81 * src.leg)
    info["stride"] = speed * src.seconds / src.leg
    # The feet: how far apart across the line of travel, and the hands; the head's and hips' rise and fall. All in leg lengths.
    apart = [abs(dot(sub(r["ball_l"], r["ball_r"]), left)) for r in rows]
    info["apart"] = sum(apart) / len(apart) / src.leg
    wide = [abs(dot(sub(r["hand_l"], r["hand_r"]), left)) for r in rows]
    info["hands_apart"] = sum(wide) / len(wide) / src.leg
    bob = [r["pelvis"][1] for r in rows]
    info["bob"] = (max(bob) - min(bob)) / src.leg
    head = [r["head"][1] for r in rows]
    info["head_bob"] = (max(head) - min(head)) / src.leg
    # How much of the toe-out is on the ground, where it shows.
    ground = [c for c, d in zip(curves["toe out L"], down) if d]
    info["toe_out_ground"] = sum(ground) / max(1, len(ground))
    return curves, info


ORDER = ["thigh L", "knee L", "foot L", "thigh R", "knee R", "trunk", "hip turn", "hip drop", "shoulder turn", "arm L", "elbow L", "out L", "arm R", "elbow R", "hip height", "head height"]


def main(argv):
    rms = "--rms" in argv
    specs = [a for a in argv[1:] if not a.startswith("--")]
    data = [read(source(s)) for s in specs]
    print("%-14s" % "" + "".join("%-30s" % d[1]["name"][:28] for d in data))
    print("%-14s" % "cycle, s" + "".join("%-30.3f" % d[1]["seconds"] for d in data))
    print("%-14s" % "leg, m" + "".join("%-30.3f" % d[1]["leg"] for d in data))
    print("%-14s" % "foot down" + "".join("%-30s" % ("%.2f, right lands at %.2f" % (d[1]["duty_l"], d[1]["right_lands"])) for d in data))
    print("%-14s" % "toe out" + "".join("%-30.0f" % d[1]["toe_out_ground"] for d in data))
    print("%-14s" % "m/s" + "".join("%-30.2f" % d[1]["speed"] for d in data))
    print("%-14s" % "Froude" + "".join("%-30.2f" % d[1]["froude"] for d in data))
    print("%-14s" % "stride, legs" + "".join("%-30.2f" % d[1]["stride"] for d in data))
    print("%-14s" % "feet apart" + "".join("%-30.3f" % d[1]["apart"] for d in data))
    print("%-14s" % "hands apart" + "".join("%-30.3f" % d[1]["hands_apart"] for d in data))
    print("%-14s" % "hip bob" + "".join("%-30.3f" % d[1]["bob"] for d in data))
    print("%-14s" % "head bob" + "".join("%-30.3f" % d[1]["head_bob"] for d in data))
    if rms:
        ref = data[0][0]
        for key in ORDER + ["toe out L"]:
            row = "%-14s" % key
            for curves, _info in data:
                c = curves[key]
                # Heights differ with the body's stance: compare their swing about their own mean.
                if "height" in key:
                    mean_a, mean_b = sum(c) / len(c), sum(ref[key]) / len(ref[key])
                    e = math.sqrt(sum((a - mean_a - b + mean_b) ** 2 for a, b in zip(c, ref[key])) / len(c))
                    row += "%-30s" % ("off by %.3f" % e)
                else:
                    e = math.sqrt(sum((a - b) ** 2 for a, b in zip(c, ref[key])) / len(c))
                    row += "%-30s" % ("off by %.1f, %.0f..%.0f" % (e, min(c), max(c)))
            print(row)
        return 0
    for key in ORDER:
        print(key)
        for curves, info in data:
            c = curves[key]
            fmt = "%6.2f" if "height" in key else "%6.0f"
            print("  %-16s" % info["name"][:16] + "".join(fmt % c[i] for i in range(0, STEPS, 10)) + "   range %.2f..%.2f" % (min(c), max(c)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
