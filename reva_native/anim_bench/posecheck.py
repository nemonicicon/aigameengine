#!/usr/bin/env python3
"""posecheck.py -- the animation lie detector.

Feeds on REVA_POSEDUMP output ([pose] lines for a slot, [hpose] for the hero,
[bhvev] state-machine events) and reports what stills can't show: pops
(single-frame joint teleports), limb pops relative to the body, and transition
smoothness. Samples are timestamped; deltas spanning draw-skipped gaps (>34ms)
are excluded from pop detection and counted separately. A clean report is
necessary, not sufficient -- eyes still finish the job.

usage: posecheck.py <logfile>
"""
import sys, re

def vec(s):
    return [float(v) for v in s.split(",")]

def main(path):
    pose, hpose, events = [], [], []
    rx = re.compile(r"\[pose\] t=([\d.\-]+) si=(\d+) bhv=(\d+) bhvT=([\d.\-]+) spd=([\d.\-]+) xz=([\d.,\-]+) "
                    r"hip=([\d.,\-]+) kL=([\d.,\-]+) kR=([\d.,\-]+) hnd=([\d.,\-]+)")
    hx = re.compile(r"\[hpose\] want=(-?\d+) wph=([\d.\-]+) x=([\d.\-]+) y=([\d.\-]+) z=([\d.\-]+) alt=([\d.\-]+) onG=(\d+) dj=(\d+)")
    for ln in open(path, errors="replace"):
        if "[bhvev]" in ln:
            events.append(ln.strip())
            continue
        m = rx.search(ln)
        if m:
            pose.append(dict(t=float(m.group(1)), si=int(m.group(2)), bhv=int(m.group(3)),
                             bhvT=float(m.group(4)), spd=float(m.group(5)),
                             hip=vec(m.group(7)), kL=vec(m.group(8)),
                             kR=vec(m.group(9)), hnd=vec(m.group(10))))
            continue
        m = hx.search(ln)
        if m:
            hpose.append(dict(want=int(m.group(1)), wph=float(m.group(2)),
                              x=float(m.group(3)), y=float(m.group(4)), z=float(m.group(5)),
                              alt=float(m.group(6)), onG=int(m.group(7)), dj=int(m.group(8))))

    def d3(a, b):
        return sum((x-y)**2 for x, y in zip(a, b)) ** 0.5

    bn = {0: "NONE", 1: "RUN", 2: "ATK", 3: "DOWN"}
    if pose:
        # split by tracked slot (the -5 mode can switch targets)
        n = len(pose)
        trans, pops, limb_pops, gaps = [], [], [], 0
        for i in range(1, n):
            a, b = pose[i-1], pose[i]
            if a["si"] != b["si"]:
                gaps += 1
                continue
            dt = b["t"] - a["t"]
            if a["bhv"] != b["bhv"]:
                trans.append((i, b["t"], a["bhv"], b["bhv"]))
            if dt > 0.034 or dt <= 0:
                gaps += 1
                continue
            if -999.0 in a["hip"] or -999.0 in b["hip"]:
                continue
            hipd = d3(a["hip"], b["hip"])
            if hipd > 0.55:
                pops.append((i, b["t"], "hip", hipd, a["bhv"], b["bhv"]))
            for j in ("kL", "kR", "hnd"):
                if -999.0 in a[j] or -999.0 in b[j]:
                    continue
                rel = d3([a[j][k]-a["hip"][k] for k in range(3)],
                         [b[j][k]-b["hip"][k] for k in range(3)])
                if rel > 0.50:
                    limb_pops.append((i, b["t"], j, rel, a["bhv"], b["bhv"]))
        print(f"[slot] samples={n} contiguous-pairs={n-1-gaps} skipped-gaps={gaps} transitions={len(trans)}")
        print(f"       hip-pops(>0.55u/f)={len(pops)} limb-pops(>0.50u/f)={len(limb_pops)}")
        for i, t, a, b in trans[:14]:
            print(f"  t={t:8.2f}  {bn.get(a,a)} -> {bn.get(b,b)}")
        for rec in (pops + limb_pops)[:12]:
            i, t, j, dd, a, b = rec
            print(f"  POP t={t:8.2f} {j} {dd:.2f}u ({bn.get(a,a)}->{bn.get(b,b)})")
        if not pops and not limb_pops:
            print("  no pops on contiguous frames: transitions continuous at instrument resolution")

    for e in events:
        print(" ", e)

    if hpose:
        n = len(hpose)
        ymax, switches, airfrms = 0.0, [], 0
        for i in range(1, n):
            a, b = hpose[i-1], hpose[i]
            dy = abs(b["y"]-a["y"])
            if dy > ymax:
                ymax = dy
            if a["want"] != b["want"]:
                switches.append((i, a["want"], b["want"], b["onG"]))
            if not b["onG"]:
                airfrms += 1
        print(f"[hero] frames={n} air-frames={airfrms} clip-switches={len(switches)} max|dy|/frame={ymax:.3f}u")
        for i, a, b, g in switches[:14]:
            print(f"  f{i}: clip {a} -> {b} ({'air' if not g else 'ground'})")

if __name__ == "__main__":
    main(sys.argv[1])
