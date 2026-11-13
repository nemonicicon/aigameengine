#!/usr/bin/env python3
"""motioncheck.py -- does this motion obey physics, or does it only LOOK continuous?

posecheck.py proves continuity (no pops). Continuity is not correctness: a character
whose feet slide across the floor like it is on ice has perfectly continuous motion and
is completely wrong. This measures the invariants a real body obeys.

    REVA_POSEDUMP=<slot|-5> ... > run.log
    motioncheck.py run.log

  FOOT SKATE   the planted foot must hold still in the world while it carries weight.
               Measured as NET DRIFT across each contact interval (first contact frame to
               last), divided by how far the body moved during that same interval. Summing
               per-frame magnitudes instead would punish fast clips for the honest motion
               at the edges of contact -- that mistake cost a whole sweep in the wrong
               direction. 0.0 = plants perfectly. 1.0 = feet painted on, body sliding.
  CONTACT      the lower foot should sit at floor level: not sunk, not hovering.
  BONE LENGTH  thigh and shin lengths are rig geometry and CANNOT change. Variation
               means a retarget or a blend is stretching the skeleton.
  CADENCE      steps come from plant swaps; stride = speed / step-rate. A gait that is
               not speed-synced shows an implausible stride or no steps while moving.
  KNEE         the knee angle must stay inside a human range; a backwards knee is a
               transfer that landed inside-out.
"""
import sys, re, math

RX = re.compile(
    r"\[pose\] t=([\d.\-]+) si=(-?\d+) bhv=(\d+) bhvT=([\d.\-]+) spd=([\d.\-]+) xz=([\d.,\-]+) "
    r"hip=([\d.,\-]+) uL=([\d.,\-]+) kL=([\d.,\-]+) uR=([\d.,\-]+) kR=([\d.,\-]+) fL=([\d.,\-]+) fR=([\d.,\-]+) hnd=([\d.,\-]+)")

def v(s): return [float(x) for x in s.split(",")]
def d3(a, b): return math.dist(a, b)
def d2(a, b): return math.hypot(a[0]-b[0], a[2]-b[2])

def angle(a, b, c):
    u = [a[i]-b[i] for i in range(3)]; w = [c[i]-b[i] for i in range(3)]
    nu = math.sqrt(sum(x*x for x in u)); nw = math.sqrt(sum(x*x for x in w))
    if nu < 1e-6 or nw < 1e-6: return None
    dot = sum(u[i]*w[i] for i in range(3))/(nu*nw)
    return math.degrees(math.acos(max(-1.0, min(1.0, dot))))

def main(path):
    F = []
    for ln in open(path, errors="replace"):
        m = RX.search(ln)
        if not m: continue
        rec = dict(t=float(m.group(1)), si=int(m.group(2)), bhv=int(m.group(3)),
                   spd=float(m.group(5)), xz=v(m.group(6)), hip=v(m.group(7)),
                   uL=v(m.group(8)), kL=v(m.group(9)), uR=v(m.group(10)),
                   kR=v(m.group(11)), fL=v(m.group(12)), fR=v(m.group(13)))
        if -999.0 in rec["fL"] or -999.0 in rec["fR"]: continue
        F.append(rec)
    if len(F) < 30:
        print("motioncheck: need more samples (got %d) -- is REVA_POSEDUMP set?" % len(F)); return 1

    floor = min(min(f["fL"][1], f["fR"][1]) for f in F)
    CONTACT = floor + 0.08          # a foot within 8cm of its lowest reach is carrying weight
    slips, travels, contacts, thighs, shins, knees, plants = [], [], [], [], [], [], []
    run = []
    prev = None
    for f in F:
        lo = "fL" if f["fL"][1] <= f["fR"][1] else "fR"
        planted = lo if f[lo][1] <= CONTACT else None
        f["planted"] = planted
        contacts.append(min(f["fL"][1], f["fR"][1]))
        for side, up, kn, ft in (("L", "uL", "kL", "fL"), ("R", "uR", "kR", "fR")):
            thighs.append(d3(f[up], f[kn])); shins.append(d3(f[kn], f[ft]))
            a = angle(f[up], f[kn], f[ft])
            if a is not None: knees.append(a)
        if prev is not None:
            dt = f["t"] - prev["t"]
            body_j = math.hypot(f["xz"][0]-prev["xz"][0], f["xz"][1]-prev["xz"][1])
            if not (0 < dt < 0.05 and body_j < 1.5):
                run = []                               # a teleport/gap breaks the contact
            if 0 < dt < 0.05 and body_j < 1.5:
                if planted and prev["planted"] == planted and f["spd"] > 0.5:
                    run.append(f)                    # still the same foot on the ground
                else:
                    if len(run) >= 3:                # a finished contact: measure NET drift
                        a, b = run[0], run[-1]; pf = a["planted"]
                        slips.append(d2(a[pf], b[pf]))
                        travels.append(math.hypot(b["xz"][0]-a["xz"][0], b["xz"][1]-a["xz"][1]))
                    run = [f] if planted else []
                    if planted and not prev["planted"]:
                        plants.append(f["t"])          # a new footfall
        prev = f

    if len(run) >= 3:
        a, b = run[0], run[-1]; pf = a["planted"]
        slips.append(d2(a[pf], b[pf]))
        travels.append(math.hypot(b["xz"][0]-a["xz"][0], b["xz"][1]-a["xz"][1]))

    def stats(a): return (sum(a)/len(a), max(a)) if a else (0.0, 0.0)
    slip_mean, slip_max = stats(slips); trav = sum(travels)
    ratio = (sum(slips)/trav) if trav > 1e-6 else 0.0
    c_mean, c_max = stats(contacts); c_min = min(contacts) if contacts else 0
    th_mean, _ = stats(thighs); sh_mean, _ = stats(shins)
    th_var = (max(thighs)-min(thighs))/th_mean if thighs and th_mean > 1e-6 else 0
    sh_var = (max(shins)-min(shins))/sh_mean if shins and sh_mean > 1e-6 else 0
    kmin = min(knees) if knees else 0; kmax = max(knees) if knees else 0
    moving = [f for f in F if f["spd"] > 0.5]
    span = F[-1]["t"] - F[0]["t"]
    steprate = len(plants)/span if span > 0 else 0
    avgspd = sum(f["spd"] for f in moving)/len(moving) if moving else 0
    stride = (avgspd/steprate) if steprate > 0.05 else 0

    def verdict(ok): return "PASS" if ok else "**FAIL**"
    print("motioncheck: %d samples, %.1fs, slot(s) %s" %
          (len(F), span, sorted(set(f["si"] for f in F))))
    # A RATIO NEEDS CONTACTS. With none, sum(slips)/travel is 0.0 -- which printed as a
    # PERFECT PASS for a body whose feet never touched the ground at all. reva's broken
    # run scored 0.00 that way, and the recorded baseline inherited the fiction. Too few
    # contacts is not a good score, it is no measurement, and for a moving body it is
    # itself the defect.
    if moving and len(slips) < 4:
        print("  FOOT SKATE   only %d contacts in %.1fs of travel -- THE FEET NEVER PLANT; "
              "no ratio can be computed here      **FAIL**" % (len(slips), span))
    else:
        print("  FOOT SKATE   %d contacts, net drift %.3fu each (max %.3f) vs %.1fu travelled -> ratio %.2f  %s"
              % (len(slips), slip_mean, slip_max, trav, ratio, verdict(ratio <= 0.35)))
    print("  CONTACT      lower foot y: mean %.3f min %.3f max %.3f                    %s"
          % (c_mean, c_min, c_max, verdict(-0.25 <= c_min and c_mean <= 0.85)))
    print("  BONE LENGTH  thigh %.3fu (var %.1f%%)   knee-to-foot %.3fu (var %.1f%%, ankle flex)  %s"
          % (th_mean, th_var*100, sh_mean, sh_var*100, verdict(th_var < 0.02)))
    print("  CADENCE      %d plants, %.2f steps/s at %.2fu/s -> stride %.2fu           %s"
          % (len(plants), steprate, avgspd, stride, verdict(0.4 <= stride <= 3.5 if moving else True)))
    print("  KNEE         range %.0f..%.0f deg                                          %s"
          % (kmin, kmax, verdict(kmin > 15 and kmax < 185)))
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
