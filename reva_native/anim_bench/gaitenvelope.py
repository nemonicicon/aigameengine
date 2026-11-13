#!/usr/bin/env python3
"""gaitenvelope.py -- corpus.py's promise kept for gaits (design/animation_authoring.md section 6): real people walking and running, measured, and any clip held up against what they do (11-02).

    gaitenvelope.py build <cycles.csv> <out.json>            cycles.csv: clip,a,b,... clean cycles of a capture set, frames of each file at 60 fps
    gaitenvelope.py check <envelope.json> <source> ...       a source is what gaitlab.py takes: <pack>#<clip>[:seconds], bvh:<file>, loop:<pack>#<clip>, take:<folder>
    gaitenvelope.py selftest <cycles.csv>                    every subject checked against an envelope built WITHOUT that subject: what a known-good scores

A gait that passed the benches was plainly wrong in motion. A number can't say a gait is good. It can say a gait does something no real person in the set
does, and how far out. So this prints flags, never a verdict: every flag is a place to go and look, and a clip with none can still look wrong. ⛔ It says nothing about charm or style.

How it stays honest (anim_bench/README.md, rule one): a measure is only kept if it tells known-good from known-bad. Known-good is a subject the envelope has never seen (selftest). Known-bad
is what was rejected on sight: the alien's PR 29 walk, jog and run, and my first Kimodo layings. Run them both after touching this.

Gaits are compared with their own kind, by Froude number (speed^2 / (g * leg)), which is what makes a 0.68 m leg and a 0.87 m leg comparable. A band with too few real cycles says so.
The envelope holds statistics, no motion: it can be committed whatever the source's licence, the capture files stay out of the repository.
"""
import csv, json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author", "moves"))
import gaitlab

CMU = os.environ.get("CLUB_CMU", "cmu-mocap-bvh/data")   # machine-independent paths
BANDS = [("slow walk", 0.0, 0.14), ("walk", 0.14, 0.30), ("fast walk", 0.30, 0.60), ("jog", 0.60, 1.10), ("run", 1.10, 2.20), ("beyond the set", 2.20, 99.0)]
# Curves that mean the same on any body. Foot pitch folds past 90 degrees in swing, so it stays out; toe-out reads the bones, and they differ.
CURVES = ["thigh L", "knee L", "thigh R", "knee R", "trunk", "hip turn", "hip drop", "shoulder turn", "arm L", "elbow L", "out L", "arm R", "elbow R", "out R"]
STEP = 5
# The numbers one stride comes down to. "pace" is cycle time against the leg's own swing time, so a small body may step quicker and a slow-motion one shows.
SCALARS = ["pace", "stride", "duty", "apart", "hands_apart", "bob", "head_bob", "trunk_mean", "knee_min", "knee_max", "thigh_range", "arm_range", "elbow_mean", "out_mean", "hip_turn_range", "hip_drop_range", "shoulder_range"]


def band_of(froude):
    for name, lo, hi in BANDS:
        if lo <= froude < hi:
            return name
    return BANDS[-1][0]


def measure(curves, info):
    k = curves
    def rng(key):
        return max(k[key]) - min(k[key])
    def mean(key):
        return sum(k[key]) / len(k[key])
    return {"pace": info["seconds"] * math.sqrt(9.81 / info["leg"]), "stride": info["stride"], "duty": 0.5 * (info["duty_l"] + info["duty_r"]), "apart": info["apart"], "hands_apart": info["hands_apart"],
            "bob": info["bob"], "head_bob": info["head_bob"], "trunk_mean": mean("trunk"), "knee_min": 0.5 * (min(k["knee L"]) + min(k["knee R"])), "knee_max": 0.5 * (max(k["knee L"]) + max(k["knee R"])),
            "thigh_range": 0.5 * (rng("thigh L") + rng("thigh R")), "arm_range": 0.5 * (rng("arm L") + rng("arm R")), "elbow_mean": 0.5 * (mean("elbow L") + mean("elbow R")),
            "out_mean": 0.5 * (mean("out L") + mean("out R")), "hip_turn_range": rng("hip turn"), "hip_drop_range": rng("hip drop"), "shoulder_range": rng("shoulder turn")}


class CycleSource:
    """One cut of a capture."""
    def __init__(self, take, a, b, name):
        from z_km import Cycle
        self.name = name
        self.cycle = Cycle(take, a, b)
        self.rm = take.rm
        self.seconds = self.cycle.T
        self.leg = take.leg_len

    def joints(self, phase):
        p, _ = self.cycle.at(phase)
        return {k: p[v] for k, v in self.rm.items() if not k.startswith("_") and isinstance(v, int) and v >= 0}


def read_cycles(path):
    from bvhtake import BvhTake
    from z_km import Take
    rows = list(csv.DictReader(open(path)))
    by_clip = {}
    for r in rows:
        by_clip.setdefault(r["clip"], []).append(r)
    out = []
    for clip, cuts in sorted(by_clip.items()):
        subject = clip.split("_")[0]
        take = None
        for r in cuts:
            if take is None:
                take = Take(BvhTake("%s/%03d/%s.bvh" % (CMU, int(subject), clip)))
            src = CycleSource(take, int(r["a"]), int(r["b"]), clip)
            try:
                curves, info = gaitlab.read(src)
            except Exception as e:
                print("  %s %s-%s skipped: %s" % (clip, r["a"], r["b"], e))
                continue
            out.append(dict(subject=subject, clip=clip, froude=info["froude"], curves={c: curves[c][::STEP] for c in CURVES}, scalars=measure(curves, info)))
    return out


def envelope(cycles):
    """Per band: each scalar's spread, each curve's middle and how far real cycles stray from it."""
    env = {}
    for name, _lo, _hi in BANDS:
        group = [c for c in cycles if band_of(c["froude"]) == name]
        if not group:
            continue
        band = {"cycles": len(group), "subjects": len(set(c["subject"] for c in group)), "scalars": {}, "curves": {}}
        for s in SCALARS:
            vals = sorted(c["scalars"][s] for c in group)
            band["scalars"][s] = dict(min=vals[0], p10=vals[len(vals) // 10], mid=vals[len(vals) // 2], p90=vals[len(vals) * 9 // 10], max=vals[-1])
        for key in CURVES:
            n = len(group[0]["curves"][key])
            mid = [sorted(c["curves"][key][i] for c in group)[len(group) // 2] for i in range(n)]
            off = sorted(rms(c["curves"][key], mid) for c in group)
            band["curves"][key] = dict(mid=mid, p90=off[len(off) * 9 // 10], max=off[-1])
        env[name] = band
    return env


def rms(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)) / len(a))


def flags(env, froude, scalars, curves, level="max"):
    name = band_of(froude)
    band = env.get(name)
    if band is None:
        return name, None
    out = []
    for s in SCALARS:
        r = band["scalars"][s]
        v = scalars[s]
        lo, hi = r["min"], r["max"]
        if v < lo or v > hi:
            over = (v - hi) if v > hi else (lo - v)
            spread = max(1e-6, r["p90"] - r["p10"])
            out.append((over / spread, "%-15s %7.3f  real %.3f..%.3f, middle %.3f" % (s, v, lo, hi, r["mid"])))
    for key in CURVES:
        off = rms(curves[key], band["curves"][key]["mid"])
        limit = band["curves"][key]["max"]
        if off > limit:
            out.append(((off - limit) / max(1.0, band["curves"][key]["p90"]), "%-15s off the real curve by %.0f deg, real cycles at most %.0f" % (key, off, limit)))
    out.sort(reverse=True)
    return name, out


def main(argv):
    cmd = argv[1]
    if cmd == "build":
        cycles = read_cycles(argv[2])
        env = envelope(cycles)
        json.dump(env, open(argv[3], "w"), indent=0)
        for name, band in env.items():
            print("%-16s %3d cycles, %2d subjects" % (name, band["cycles"], band["subjects"]))
        return 0
    if cmd == "selftest":
        cycles = read_cycles(argv[2])
        subjects = sorted(set(c["subject"] for c in cycles))
        counts = []
        for s in subjects:
            env = envelope([c for c in cycles if c["subject"] != s])
            for c in cycles:
                if c["subject"] != s:
                    continue
                band, out = flags(env, c["froude"], c["scalars"], c["curves"])
                if out is None:
                    continue
                counts.append((len(out), s, c["clip"], band))
        counts.sort()
        print("real cycles, each against an envelope without its own subject:")
        for name, _lo, _hi in BANDS:
            group = [c for c in counts if c[3] == name]
            if group:
                n = len(group)
                print("  %-14s %3d cycles, %2d subjects: flags median %2d, 90%% at or under %2d, worst %2d" % (name, n, len(set(c[1] for c in group)), group[n // 2][0], group[n * 9 // 10][0], group[-1][0]))
        return 0
    env = json.load(open(argv[2]))
    for spec in argv[3:]:
        curves, info = gaitlab.read(gaitlab.source(spec))
        sampled = {c: curves[c][::STEP] for c in CURVES}
        band, out = flags(env, info["froude"], measure(curves, info), sampled)
        have = env.get(band, {})
        print("%s  %.2f m/s, Froude %.2f, a stride takes %.2f s: %s (%d real cycles, %d subjects%s)" % (info["name"], info["speed"], info["froude"], info["seconds"], band, have.get("cycles", 0), have.get("subjects", 0),
              ": too few people to trust the edges, a flag here is a place to look at most" if have and have["subjects"] < 5 else ""))
        if out is None:
            print("    no real cycles this fast in the set: nothing to hold it to")
            continue
        print("    %d flags" % len(out))
        for _w, line in out:
            print("      " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
