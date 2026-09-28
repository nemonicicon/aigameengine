#!/usr/bin/env python3
"""movecheck.py -- did the ENGINE play what the score DECLARED?

The authoring plan's central property (design/animation_authoring.md SS4): a score's
declarations double as its test fixture. The score already proved its constraints in
author-time FK; this closes the loop on the other side -- the pose dump of the engine
actually playing the clip. Division of labour, decided at P2:

  author-time (the score)   exact positions in RIG space, deterministic, asserted
  engine-time (this tool)   the physical invariants in WORLD space: a declared contact
                            is ON THE FLOOR and STILL inside its window, the motion is
                            continuous, and the move ends where it said it would

World-space exactness is deliberately NOT checked here -- the model matrix (heading,
travel, grounding) moves the body legitimately, and stationarity-on-the-floor IS the
physical content of a contact (the same lesson as motioncheck's net-drift metric).

All tolerances are in LEG-LENGTH units measured from the dump's own opening stand, so
the same declaration checks any subject at any scale.

usage: movecheck.py <pack>.decl.json <move> <posedump-segment> [--label X]
       (the segment = pose lines from the move's start; the once=1 driver produces it)
"""
import sys, json, math

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
import posturecheck as PC

PART = dict(fL="fL", fR="fR", hnd="hnd", foR="foR", grp="grp", grpL="grpL", hip="hip", head="head")

def main(argv):
    declf, move, dumpf = argv[1], argv[2], argv[3]
    label = argv[argv.index("--label") + 1] if "--label" in argv else move
    decl = json.load(open(declf))[move]
    F = PC.load(dumpf)
    if len(F) < 30:
        print("movecheck: %s -- only %d samples" % (label, len(F))); return 2
    t0 = F[0]["t"]
    for f in F: f["t"] -= t0                      # clip-relative time

    # the reference stand: the move's own first 5 frames
    ref = F[:5]
    leg = PC.measure(F)["legLen"]
    floorY = sum(min(f["fL"][1], f["fR"][1]) for f in ref) / len(ref)

    fails = []
    print("movecheck: %s -- %d samples over %.2fs  [leg %.3fu, floor %.3fu]"
          % (label, len(F), F[-1]["t"], leg, floorY))

    # ---- contacts ----
    for c in decl.get("contacts", []):
        part = PART[c["part"]]
        w0, w1 = c["t"]
        slack = min(0.10, (w1 - w0) / 4.0)        # poll jitter, but never eat a short window
        win = [f for f in F if w0 + slack <= f["t"] <= w1 - slack]
        if len(win) < 5:
            fails.append("%s window empty" % c["part"])
            print("  CONTACT %-4s [%.2f..%.2f]  window empty                       **FAIL**"
                  % (c["part"], w0, w1)); continue
        if "min_y" in c:
            # an AIRBORNE window: the part must stay ABOVE the floor by min_y legs
            miny = min(f[part][1] for f in win) - floorY
            ok = miny >= c["min_y"] * leg
            if not ok: fails.append("%s not airborne" % c["part"])
            print("  AIRBORNE %-4s [%.2f..%.2f] lowest y-floor %.3fu (need >= %.3f)         %s"
                  % (c["part"], w0, w1, miny, c["min_y"] * leg, "PASS" if ok else "**FAIL**"))
            continue
        drift = math.dist(win[0][part], win[-1][part])
        maxy = max(f[part][1] for f in win)
        still_tol = c.get("still_tol", 0.03) * leg
        floor_tol = c.get("floor_tol", 0.06) * leg
        ok_still = drift <= still_tol
        ok_floor = (maxy - floorY) <= floor_tol if c.get("floor", True) else True
        if not ok_still: fails.append("%s drifts" % c["part"])
        if not ok_floor: fails.append("%s off floor" % c["part"])
        print("  CONTACT %-4s [%.2f..%.2f]  drift %.3fu (tol %.3f)  y-floor %.3fu (tol %.3f)  %s"
              % (c["part"], w0, w1, drift, still_tol, maxy - floorY, floor_tol,
                 "PASS" if ok_still and ok_floor else "**FAIL**"))

    # ---- continuity: worst joint SPEED, legs per second ----
    # Displacement-per-frame conflated render hitches with data pops: a dt spike to
    # 0.038s makes the interpolator legitimately cross 2.3 baked frames in one render
    # step. Velocity is rate-independent (the same lesson as the clip-stepping fix).
    # The declaration may carry its own max_speed: a cartwheel's hand really does move
    # ~12 legs/s; a walk's never should. A true single-frame teleport (the frame-0
    # split-stance flash was 40+ legs/s) still towers over any honest motion.
    worst = 0.0
    for a, b in zip(F, F[1:]):
        dt = b["t"] - a["t"]
        if not (0.004 < dt < 0.06): continue
        d = max(PC.d3(a[k], b[k]) for k in PC.JOINTS) / dt
        worst = max(worst, d)
    speed_tol = decl.get("max_speed", 4.0) * leg
    ok = worst < speed_tol
    if not ok: fails.append("pop")
    print("  CONTINUITY   worst joint speed %.1fu/s (tol %.1f)                    %s"
          % (worst, speed_tol, "PASS" if ok else "**FAIL**"))

    # ---- terminal: the declared ending ----
    term = decl.get("terminal", "base")
    if term == "base":
        # SHAPE-relative: subtract the hip, so a move that ends displaced (a cartwheel
        # travels ~1.7 legs) still counts as returning to its base POSE
        def shape(f):
            return {k: [f[k][i] - f["hip"][i] for i in range(3)] for k in PC.JOINTS}
        head = [shape(f) for f in F[:8]]; tail = [shape(f) for f in F[-8:]]
        dm = sum(max(PC.d3(a[k], b[k]) for k in PC.JOINTS)
                 for a, b in zip(head, tail)) / len(head)
        ok = dm < 0.06 * leg
        if not ok: fails.append("does not return to base")
        print("  TERMINAL     mean joint delta first-vs-last %.3fu (tol %.3f)      %s"
              % (dm, 0.06 * leg, "PASS" if ok else "**FAIL**"))
    elif term.startswith("loop"):
        a, b = F[0], F[-1]
        dm = max(PC.d3(a[k], b[k]) for k in PC.JOINTS)
        ok = dm < 0.06 * leg
        if not ok: fails.append("loop not closed")
        print("  LOOP CLOSED  first-vs-last %.3fu (tol %.3f)                       %s"
              % (dm, 0.06 * leg, "PASS" if ok else "**FAIL**"))

    # ---- periodicity: a traveling cycle must CLOSE in shape ----
    # decl "periodic": {"t0":, "t1":, "period":} -- across [t0,t1], the hip-relative
    # pose at t must match the pose at t+period. The loop-closure test for cycles that
    # move (an absolute compare would just measure the travel).
    per = decl.get("periodic")
    if per:
        def shape2(f):
            return {k: [f[k][i] - f["hip"][i] for i in range(3)] for k in PC.JOINTS}
        T = per["period"]
        ds = []
        for f in F:
            if not (per["t0"] <= f["t"] <= per["t1"] - T): continue
            tgt = f["t"] + T
            g = min(F, key=lambda x: abs(x["t"] - tgt))
            if abs(g["t"] - tgt) > 0.03: continue
            sa, sb = shape2(f), shape2(g)
            ds.append(max(PC.d3(sa[k], sb[k]) for k in PC.JOINTS))
        if ds:
            dm = sum(ds) / len(ds)
            ok = dm < 0.08 * leg
            if not ok: fails.append("cycle does not close")
            print("  PERIODIC     mean shape delta at +%.2fs: %.3fu (tol %.3f)          %s"
                  % (T, dm, 0.08 * leg, "PASS" if ok else "**FAIL**"))
        else:
            fails.append("periodic window empty")
            print("  PERIODIC     no comparable samples                                **FAIL**")

    print("  -> %s" % ("DECLARED = PLAYED" if not fails else "%d defect(s): %s" % (len(fails), ", ".join(fails))))
    return 0 if not fails else 1

if __name__ == "__main__":
    sys.exit(main(sys.argv))
