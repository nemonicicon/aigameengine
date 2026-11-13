#!/usr/bin/env python3
"""posturecheck.py -- is this body STANDING, or is it merely not moving?

motioncheck.py measures a gait: foot skate over travel, cadence, stride. Point it at an
idle and most of it degenerates -- zero contacts, zero stride -- while the things that
actually go wrong in a stand go unmeasured. A retarget that lands a body splay-legged,
sunk to the shins, pelvis rolled twenty degrees and torso wrung around is perfectly
continuous and perfectly still, and completely wrong.

A stand has its own invariants. These are they, all scale-free (divided by the subject
own leg length or own height) so ten models of ten different sizes go in one table and
compare honestly:

  GROUND      the ankle sits at floor level. Not sunk through it, not hovering above it.
  DOUBLE      both feet carry weight -- their heights match. One foot in the air is a
              pose, not a stand.
  PELVIS      the hip axis is level. Roll here is the classic rest-orientation mismatch.
  SHOULDER    the shoulder axis is level.
  LEAN        the spine is vertical: the hip->head vector stands up.
  BALANCE     the head is over the middle of the feet. This is the postural invariant --
              a real body that fails it falls over.
  STANCE      feet are about hip-width apart. Splayed or crossed = a bad transfer.
  KNEE        near-extended, and the two legs agree with each other.
  TORSION     shoulder axis and hip axis point the same way; a wrung torso is inside-out.
  BONE        upper-leg length is rig geometry and cannot vary. (Measure upleg->knee, ONE
              joint. hip->knee spans two and changes honestly with leg swing -- that
              mistake was logged as a defect once already.)
  STILL       an idle does not travel and does not jitter.

usage: posturecheck.py <log> [--label NAME] [--tsv]
"""
import sys, re, math

RX = re.compile(
    r"\[pose\] t=([\d.\-]+) si=(-?\d+) bhv=(\d+) bhvT=([\d.\-]+) spd=([\d.\-]+) xz=([\d.,\-]+) "
    r"hip=([\d.,\-]+) uL=([\d.,\-]+) kL=([\d.,\-]+) uR=([\d.,\-]+) kR=([\d.,\-]+) "
    r"fL=([\d.,\-]+) fR=([\d.,\-]+) hnd=([\d.,\-]+) head=([\d.,\-]+) shL=([\d.,\-]+) shR=([\d.,\-]+) "
    r"uaL=([\d.,\-]+) uaR=([\d.,\-]+) foR=([\d.,\-]+)(?: grp=([\d.,\-]+))?(?: grpL=([\d.,\-]+))?")

def v(s):  return [float(x) for x in s.split(",")]
def d3(a, b): return math.dist(a, b)
def dxz(a, b): return math.hypot(a[0]-b[0], a[2]-b[2])
def mean(a): return sum(a)/len(a) if a else 0.0

def angle(a, b, c):
    u = [a[i]-b[i] for i in range(3)]; w = [c[i]-b[i] for i in range(3)]
    nu = math.sqrt(sum(x*x for x in u)); nw = math.sqrt(sum(x*x for x in w))
    if nu < 1e-6 or nw < 1e-6: return None
    return math.degrees(math.acos(max(-1.0, min(1.0, sum(u[i]*w[i] for i in range(3))/(nu*nw)))))

JOINTS = ("hip", "uL", "kL", "uR", "kR", "fL", "fR", "head", "shL", "shR",
          "uaL", "uaR", "hnd", "foR")
FIELDS = ("hip", "uL", "kL", "uR", "kR", "fL", "fR", "hnd", "head", "shL", "shR",
          "uaL", "uaR", "foR", "grp", "grpL")   # grp/grpL optional: the REAL hands, absent in old logs

def load(path):
    F = []
    for ln in open(path, errors="replace"):
        m = RX.search(ln)
        if not m: continue
        g = m.groups()
        # Another subsystem printf-ing from the same stdout can land INSIDE a pose line
        # and truncate a field. One torn line must not take down a 700-frame run: parse
        # defensively and drop the frame if anything is short or unreadable.
        ok = True
        try:
            r = dict(t=float(g[0]), si=int(g[1]), spd=float(g[4]), xz=v(g[5]))
            if len(r["xz"]) != 2: ok = False
            for i, k in enumerate(FIELDS):
                raw = g[6 + i]
                if raw is None:
                    if k in ("grp", "grpL"): continue   # optional fields, old logs
                    ok = False; break
                c = v(raw)
                if len(c) != 3: ok = False; break
                r[k] = c
        except ValueError:
            ok = False
        # a missing bone dumps -999; a frame without the full skeleton cannot be scored
        if not ok or any(-999.0 in r[k] for k in JOINTS): continue
        F.append(r)
    return F

def measure(F):
    """Every metric as a raw number. Verdicts are applied separately, so the same
    numbers can be printed with or without thresholds."""
    M = {}
    upleg, shin, kneeL, kneeR = [], [], [], []
    ground, dbl, pelv, shldr, lean, bal, stance, tors = [], [], [], [], [], [], [], []
    armL, armR = [], []
    for f in F:
        # ARM HANG: in a stand the arms hang. Measured as the angle of the upper-arm ->
        # forearm vector away from straight down. An arm the retarget never reached stays
        # in its bind pose and reads ~90 deg -- straight out sideways, which is exactly how
        # Character_01 was caught on the review wall while every leg metric said PASS.
        for side, ua, fo in (("L", "uaL", "hnd"), ("R", "uaR", "foR")):
            d = [f[fo][i] - f[ua][i] for i in range(3)]
            n = math.sqrt(sum(x * x for x in d))
            if n > 1e-5:
                a = math.degrees(math.acos(max(-1.0, min(1.0, -d[1] / n))))
                (armL if side == "L" else armR).append(a)
        upleg += [d3(f["uL"], f["kL"]), d3(f["uR"], f["kR"])]
        shin += [d3(f["kL"], f["fL"]), d3(f["kR"], f["fR"])]
        aL = angle(f["uL"], f["kL"], f["fL"]); aR = angle(f["uR"], f["kR"], f["fR"])
        if aL is not None: kneeL.append(aL)
        if aR is not None: kneeR.append(aR)
        ground.append(min(f["fL"][1], f["fR"][1]))
        dbl.append(abs(f["fL"][1] - f["fR"][1]))
        hipSep = dxz(f["uL"], f["uR"])
        if hipSep > 1e-4:
            pelv.append(math.degrees(math.atan2(abs(f["uL"][1] - f["uR"][1]), hipSep)))
            stance.append(dxz(f["fL"], f["fR"]) / hipSep)
        shSep = dxz(f["shL"], f["shR"])
        if shSep > 1e-4:
            shldr.append(math.degrees(math.atan2(abs(f["shL"][1] - f["shR"][1]), shSep)))
        dy = f["head"][1] - f["hip"][1]
        if dy > 1e-4:
            lean.append(math.degrees(math.atan2(dxz(f["head"], f["hip"]), dy)))
        mid = [(f["fL"][i] + f["fR"][i]) / 2 for i in range(3)]
        bal.append(math.hypot(f["head"][0] - mid[0], f["head"][2] - mid[2]))
        # torsion: shoulder axis vs hip axis on the ground plane, wrapped to +-90
        ha = math.atan2(f["uL"][0] - f["uR"][0], f["uL"][2] - f["uR"][2])
        sa = math.atan2(f["shL"][0] - f["shR"][0], f["shL"][2] - f["shR"][2])
        d = math.degrees(sa - ha)
        while d > 90: d -= 180
        while d < -90: d += 180
        tors.append(d)

    M["upleg"] = mean(upleg); M["shin"] = mean(shin)
    M["legLen"] = M["upleg"] + M["shin"]
    M["uplegVar"] = (max(upleg) - min(upleg)) / M["upleg"] if upleg and M["upleg"] > 1e-6 else 0.0
    M["bodyH"] = mean([f["head"][1] for f in F]) - mean(ground)
    L = M["legLen"] if M["legLen"] > 1e-6 else 1.0
    H = M["bodyH"] if M["bodyH"] > 1e-6 else 1.0
    M["ground"] = mean(ground); M["groundMin"] = min(ground); M["groundN"] = mean(ground) / L
    M["double"] = mean(dbl) / L
    M["pelvis"] = mean(pelv); M["shoulder"] = mean(shldr)
    M["lean"] = mean(lean); M["balance"] = mean(bal) / H
    M["stance"] = mean(stance)
    M["kneeL"] = mean(kneeL); M["kneeR"] = mean(kneeR)
    M["kneeAsym"] = abs(M["kneeL"] - M["kneeR"])
    M["armL"] = mean(armL); M["armR"] = mean(armR)
    M["torsion"] = mean(tors)

    # --- stillness, measured on CLIP STEPS, not render frames -------------------
    # Clips are sampled nearest-frame with no interpolation, so at 97fps against an
    # 18fps bake every pose is held ~5 render frames. Measuring "biggest single-frame
    # move" therefore measures the render rate, not the animation -- it called manny a
    # popping mess on the first pass. Collapse identical consecutive poses into steps
    # first; everything below is then render-rate independent.
    steps = []
    for f in F:
        key = tuple(round(c, 4) for k in JOINTS for c in f[k])
        if not steps or steps[-1][0] != key: steps.append((key, f))
    S = [s[1] for s in steps]
    M["steps"] = len(S)
    M["clipfps"] = (len(S) - 1) / (F[-1]["t"] - F[0]["t"]) if len(S) > 1 else 0.0

    # LIVE: does the body actually move at all? A stand held on one frozen frame is as
    # wrong as a stand that thrashes -- and is invisible to every other test here.
    hr = max(max(f["head"][i] for f in S) - min(f["head"][i] for f in S) for i in range(3))
    M["live"] = hr / (M["bodyH"] if M["bodyH"] > 1e-6 else 1.0)

    # POP: an isolated excursion -- a step that jumps away from its neighbour and
    # straight back, which is a corrupt baked frame rather than motion.
    T = 0.03 * L
    pops = []
    for i in range(1, len(S) - 1):
        for k in JOINTS:
            a, b, c = S[i - 1][k], S[i][k], S[i + 1][k]
            ab, bc, ac = d3(a, b), d3(b, c), d3(a, c)
            if ab > T and bc > T and ac < 0.4 * max(ab, bc):
                pops.append((S[i]["t"], k, ab)); break
    M["pops"] = len(pops); M["popList"] = pops
    M["drift"] = math.hypot(F[-1]["xz"][0] - F[0]["xz"][0], F[-1]["xz"][1] - F[0]["xz"][1])
    M["span"] = F[-1]["t"] - F[0]["t"]; M["n"] = len(F)
    return M

# (key, label, unit, lo, hi, why) -- a value outside [lo,hi] is a defect.
#
# WHERE THESE BANDS COME FROM. The first set was reasoned from anatomy and was wrong in
# the expensive direction: BALANCE at 0.10 failed nine of ten subjects, and the pixels
# said nine of ten were standing fine. A metric that fails almost everything is usually
# a bad threshold, not a bad fleet. So each band below is now set from the population of
# stands CONFIRMED upright on a nine-angle wall, widened to clear the worst good one,
# and checked to still catch the known-bad (crowd_susan asleep on the floor, reva frozen
# mid-stride). BALANCE is deliberately coarse: the dump carries the ANKLE, not the toe,
# so the true support centre sits some way forward of the point measured against.
TESTS = [
    ("groundN",  "GROUND",    "x leg",    -0.02,  0.14, "ankle at floor level"),
    ("double",   "DOUBLE",    "x leg",     0.0,   0.06, "both feet carry weight"),
    ("pelvis",   "PELVIS",    "deg",       0.0,   8.0,  "hip axis level"),
    ("shoulder", "SHOULDER",  "deg",       0.0,  15.0,  "shoulder axis level"),
    ("lean",     "LEAN",      "deg",       0.0,  12.0,  "spine vertical"),
    ("balance",  "BALANCE",   "x height",  0.0,   0.30, "head over mid-foot"),
    ("stance",   "STANCE",    "x hip",     0.35,  3.20, "feet about hip width"),
    ("kneeL",    "KNEE L",    "deg",     150.0, 185.0,  "near-extended"),
    ("kneeR",    "KNEE R",    "deg",     150.0, 185.0,  "near-extended"),
    ("kneeAsym", "KNEE ASYM", "deg",       0.0,  15.0,  "legs agree"),
    ("armL",     "ARM L",     "deg",       0.0,  50.0,  "arm hangs, not stuck out"),
    ("armR",     "ARM R",     "deg",       0.0,  50.0,  "arm hangs, not stuck out"),
    ("torsion",  "TORSION",   "deg",     -20.0,  20.0,  "torso not wrung"),
    ("uplegVar", "BONE",      "frac",      0.0,   0.02, "upper leg cannot stretch"),
    ("live",     "LIVE",      "x height",  0.002, 0.15, "breathes, does not thrash"),
    ("pops",     "POPS",      "count",     0.0,   0.0,  "no corrupt baked frames"),
    ("drift",    "DRIFT",     "u",         0.0,   0.35, "a stand does not travel"),
]

def synth(pelvis_deg=0.0, tors_deg=0.0, splay=1.0, arm_deg=0.0, n=60):
    """A standing skeleton with faults injected at KNOWN magnitudes."""
    hipSep, shSep = 0.30, 0.40
    pr = math.radians(pelvis_deg); tr = math.radians(tors_deg); ar = math.radians(arm_deg)
    dy = math.tan(pr) * hipSep / 2
    # the right arm swings out from straight-down by arm_deg; the left stays hanging
    ua, fl = 0.30, 0.28
    F = []
    for i in range(n):
        f = {"t": i * 0.01, "si": -1, "spd": 0.0, "xz": [0.0, 0.0],
             "hip":  [0.0, 1.00, 0.0],
             "uL":   [hipSep / 2, 1.00 + dy, 0.0], "uR": [-hipSep / 2, 1.00 - dy, 0.0],
             "kL":   [hipSep / 2 * splay, 0.55, 0.0], "kR": [-hipSep / 2 * splay, 0.55, 0.0],
             "fL":   [hipSep / 2 * splay, 0.08, 0.0], "fR": [-hipSep / 2 * splay, 0.08, 0.0],
             "head": [0.0, 1.70, 0.0],
             "shL":  [shSep / 2 * math.cos(tr), 1.50, shSep / 2 * math.sin(tr)],
             "shR":  [-shSep / 2 * math.cos(tr), 1.50, -shSep / 2 * math.sin(tr)],
             "uaL":  [shSep / 2, 1.50 - ua, 0.0], "uaR": [-shSep / 2, 1.50 - ua, 0.0],
             "hnd":  [shSep / 2, 1.50 - ua - fl, 0.0],
             "foR":  [-shSep / 2 - fl * math.sin(ar), 1.50 - ua - fl * math.cos(ar), 0.0]}
        F.append(f)
    return F

def selftest():
    """Rule one of this bench: validate a metric against a known-good AND a known-bad
    before believing it. Two of the first three 'defects' found here were bugs in the
    measuring tool. So: inject a fault of known size, demand the number back."""
    bad = 0
    for deg in (0.0, 5.0, 12.0, 25.0):
        got = measure(synth(pelvis_deg=deg))["pelvis"]
        ok = abs(got - deg) < 0.05; bad += not ok
        print("  pelvis %5.1f deg -> %6.2f  %s" % (deg, got, "ok" if ok else "WRONG"))
    for deg in (0.0, 10.0, 30.0):
        got = abs(measure(synth(tors_deg=deg))["torsion"])
        ok = abs(got - deg) < 0.05; bad += not ok
        print("  torsion %4.1f deg -> %6.2f  %s" % (deg, got, "ok" if ok else "WRONG"))
    for s in (1.0, 2.0, 3.5):
        got = measure(synth(splay=s))["stance"]
        ok = abs(got - s) < 0.01; bad += not ok
        print("  splay  %5.1f x   -> %6.2f  %s" % (s, got, "ok" if ok else "WRONG"))
    for deg in (0.0, 45.0, 90.0):
        m = measure(synth(arm_deg=deg))
        ok = abs(m["armR"] - deg) < 0.05 and m["armL"] < 0.05; bad += not ok
        print("  arm out %4.1f deg -> R %6.2f  L %5.2f  %s"
              % (deg, m["armR"], m["armL"], "ok" if ok else "WRONG"))
    print("selftest: %s" % ("all metrics read back what was injected" if not bad
                            else "%d METRIC(S) WRONG -- fix the tool before trusting a run" % bad))
    return 1 if bad else 0

def main(argv):
    if "--selftest" in argv:
        return selftest()
    path = argv[1]
    label = "subject"; tsv = False
    if "--label" in argv: label = argv[argv.index("--label") + 1]
    if "--tsv" in argv: tsv = True
    F = load(path)
    if len(F) < 30:
        print("posturecheck: %s -- need more samples (got %d); is REVA_POSEDUMP=-1 set, "
              "and is this an 11-joint dump?" % (label, len(F)))
        return 2
    M = measure(F)
    fails = [t for t in TESTS if not (t[3] <= M[t[0]] <= t[4])]
    if tsv:
        print("\t".join([label] + ["%.4f" % M[t[0]] for t in TESTS] +
                        ["%d" % len(fails), ",".join(t[1] for t in fails)]))
        return 0
    print("posturecheck: %s -- %d samples over %.1fs   [leg %.3fu, height %.3fu, "
          "clip stepping %.1f/s]"
          % (label, M["n"], M["span"], M["legLen"], M["bodyH"], M["clipfps"]))
    for key, name, unit, lo, hi, why in TESTS:
        ok = lo <= M[key] <= hi
        print("  %-10s %9.3f %-9s %-26s %s"
              % (name, M[key], unit, why,
                 "PASS" if ok else "**FAIL** (want %.2f..%.2f)" % (lo, hi)))
    for t, k, d in M["popList"][:4]:
        print("      pop: %s jumps %.3fu and back at t=%.2f" % (k, d, t))
    print("  ground abs %.3fu (min %.3f)  knee %.0f/%.0f  stance %.2f  ->  %s"
          % (M["ground"], M["groundMin"], M["kneeL"], M["kneeR"], M["stance"],
             "STANDS" if not fails else "%d defect(s): %s"
             % (len(fails), ", ".join(t[1] for t in fails))))
    return 0 if not fails else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv))
