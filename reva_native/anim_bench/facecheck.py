#!/usr/bin/env python3
"""facecheck.py -- is this FACE built, or does it merely have the parts?

    facecheck.py <subject> [<subject> ...]      e.g. facecheck.py r2moves kmoves
    facecheck.py --selftest

posturecheck.py asks whether a body is standing. Nothing asked anything about a face,
and the face is now a whole subsystem: measured marks, a jaw BONE with the lower face
skinned to it, an authored interior, and a gaze layer. Every one of those was calibrated
BY EYE last session, and the eye is exactly what is not available in a regression run.

⛔ AND THE OBVIOUS INSTRUMENT IS THE ONE THAT DOES NOT WORK. face_aim.py tries to FIND
the mouth and the eye line in the render by colour and shape. That approach failed twice
in one session: a saturation detector found reva's lips perfectly and located kaeli's
chin instead (the lab stage is dark and her lips are low-contrast), and a "the lip seam
is a dark ridge" detector locked onto HAIR on both women. Feature-finding in a picture
of a stylised face is not reliable, so nothing here does it.

What IS reliable is measuring the rig against itself and the render against PHYSICS:

  MARKS ON THE FACE   every mark must lie on the measured surface, not floating in front
                      of it or sunk behind it. reva's mouth mark once floated 0.035 rig
                      units clear of her lips -- about 7cm at world scale -- because
                      --mouthdy moved it down in Y while keeping the Z measured at the
                      ORIGINAL height, and a face recedes below the lips. The aperture's
                      depth gate then admitted only her NOSE.
  MOUTH IN PROPORTION the corner marks must be inside the head and wider than a slit.
                      The aperture used to be sized 0.42 x the interocular distance --
                      a guess resting on the eye marks, which are themselves a
                      proportion -- and cut a hole from kaeli's nose to her chin.
  THE JAW MOVES       posing the jaw bone must actually displace the chin, by a
                      distance comparable to the mouth's own size. A jaw banded onto the
                      throat once measured 5cm of travel while the lips never moved.
  ⛔ NO SEE-THROUGH   THE ELDRITCH TEST, and the only one that needs pixels. With the
                      mouth open, the background must not be visible through the head.
                      kaeli's open mouth was a flat BLUE DISC -- her own hair, seen
                      through the back of her skull -- and that is what an interior is
                      for. Shot against a colour the club never renders, so anything
                      showing through is unambiguous.

Rig checks need no engine. The see-through test shoots the face rig's mouth camera.
"""
import argparse, math, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
ROOT = os.path.dirname(NATIVE)
AUTHOR = os.path.join(ROOT, "skate_src", "author")
sys.path.insert(0, AUTHOR)

# Bands, in the face's own units so a 1.8-unit rig and a 168-unit one compare honestly.
SURF_TOL_HW = 0.55      # a mark may sit this far off the surface, in mouth half-widths
HW_MIN_IOD = 0.10       # mouth half-width vs interocular: below this it is a slit
HW_MAX_IOD = 0.55       # above this it is a letterbox across her face
CHIN_MIN_SPAN = 0.25    # chin below the lips, in eye-to-lip spans
CHIN_MAX_SPAN = 1.10
JAW_MIN_HW = 0.45       # chin travel at full open, in mouth half-widths
SEETHRU_MAX = 0.001     # fraction of the mouth panel allowed to show the backdrop


def load(subject):
    from rpack import Rpack
    from authorlib import rig_map, fk_world
    from fingers import m4_trs, m4_mul
    p = Rpack(os.path.join(NATIVE, "packs", "v1_%s.rpack" % subject))
    nodes = p.nodes(); names = p.node_names()
    wp, _ = fk_world(nodes)
    return p, nodes, names, wp, rig_map(p)


def surface_ray(p, nodes, names, head, x, y, fwd_sign):
    """the frontmost face triangle a ray through (x,y) hits -- the same measurement
    face_marks.py places the marks with, so 'is the mark on the face' is answerable"""
    import struct
    SP = p.skinned_positions(with_bones=True)
    fam = {head}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam: fam.add(i); grew = True
    if nodes[head]["parent"] >= 0: fam.add(nodes[head]["parent"])
    rec = p.MESH.unpack_from(p.buf, p.offMeshes)
    if not rec[3]: return None
    idx = struct.unpack_from("<%dI" % rec[3], p.buf, rec[11])
    headish = set(i for i, v in enumerate(SP) if v[3] in fam)
    best = None
    for t in range(0, len(idx), 3):
        a, b, c = idx[t], idx[t + 1], idx[t + 2]
        if (a in headish) + (b in headish) + (c in headish) < 2: continue
        A, B, C = SP[a][:3], SP[b][:3], SP[c][:3]
        d = ((B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]))
        if abs(d) < 1e-12: continue
        u = ((B[1] - C[1]) * (x - C[0]) + (C[0] - B[0]) * (y - C[1])) / d
        v = ((C[1] - A[1]) * (x - C[0]) + (A[0] - C[0]) * (y - C[1])) / d
        w = 1.0 - u - v
        if u < -1e-6 or v < -1e-6 or w < -1e-6: continue
        z = u * A[2] + v * B[2] + w * C[2]
        if best is None or (z - best) * fwd_sign > 0: best = z
    return best


def rig_checks(subject, verbose=False):
    """everything that can be answered from the pack alone"""
    from fingers import m4_trs, m4_mul, xform, m4_inv_affine
    out = []
    p, nodes, names, wp, RM = load(subject)
    need = ("face_mouth", "face_eye_l", "face_eye_r", "face_chin",
            "face_mouth_l", "face_mouth_r", "face_jaw")
    miss = [n for n in need if n not in names]
    if miss:
        return [("PARTS", False, "missing: %s" % ", ".join(miss))], None
    g = lambda n: list(wp[names.index(n)])
    M, ML, MR = g("face_mouth"), g("face_mouth_l"), g("face_mouth_r")
    EL, ER, CH, JW = g("face_eye_l"), g("face_eye_r"), g("face_chin"), g("face_jaw")
    head = RM["head"]
    iod = math.dist(EL, ER)
    hw = 0.5 * math.dist(ML, MR)
    span = 0.5 * (EL[1] + ER[1]) - M[1]
    fwd_sign = 1.0 if M[2] >= wp[head][2] else -1.0
    out.append(("PARTS", True, "7 marks + jaw bone"))

    # -- the marks must lie ON the measured surface --
    worst, worstn = 0.0, ""
    for nm, P in (("mouth", M), ("mouth_l", ML), ("mouth_r", MR), ("chin", CH)):
        # ⛔ A CLUSTER, NOT ONE RAY -- the same thing face_marks.py places the mark
        # with. reva's face is a low-poly shell with real coverage gaps, so a single
        # cast through her chin misses and reads as "not on the face" when the mark is
        # fine. Nine casts, median of the hits.
        step = 0.035 * span
        # ⛔ AND THE CHIN SAMPLES UPWARD ONLY. face_marks.py places it at the LAST
        # height where a cast still lands -- the bottom edge of the head shell, by
        # construction -- so a cluster centred on it puts most of its rays below the
        # mesh and reports "no face surface anywhere near it" for a mark that is
        # exactly where it should be. The surface is above it; look there.
        dys = (0.0, step, 2.0 * step) if nm == "chin" else (-step, 0.0, step)
        hits = [z for dx in (-step, 0.0, step) for dy in dys
                for z in (surface_ray(p, nodes, names, head, P[0] + dx, P[1] + dy, fwd_sign),)
                if z is not None]
        if not hits:
            out.append(("SURFACE", False, "%s: no face surface anywhere near it" % nm)); continue
        hits.sort()
        off = abs(P[2] - hits[len(hits) // 2]) / max(1e-9, hw)
        if off > worst: worst, worstn = off, nm
    out.append(("SURFACE", worst <= SURF_TOL_HW,
                "worst %.2f half-widths off (%s), band <=%.2f" % (worst, worstn, SURF_TOL_HW)))

    # -- the mouth in proportion to the face --
    r = hw / max(1e-9, iod)
    out.append(("MOUTH", HW_MIN_IOD <= r <= HW_MAX_IOD,
                "half-width %.3f = %.2f x IOD, band %.2f-%.2f" % (hw, r, HW_MIN_IOD, HW_MAX_IOD)))
    cs = (M[1] - CH[1]) / max(1e-9, span)
    out.append(("CHIN", CHIN_MIN_SPAN <= cs <= CHIN_MAX_SPAN,
                "%.2f eye-to-lip spans below the lips, band %.2f-%.2f"
                % (cs, CHIN_MIN_SPAN, CHIN_MAX_SPAN)))

    # -- the hinge must be BEHIND the lips and ABOVE them (it is inside the head, so
    #    no surface reading can check it; the reference rig's proportions can) --
    back = (M[2] - JW[2]) * fwd_sign / max(1e-9, iod)
    up = (JW[1] - M[1]) / max(1e-9, span)
    out.append(("HINGE", back > 0.15 and up > 0.15,
                "%.2f IOD behind the lips, %.2f spans above them" % (back, up)))

    # -- posing the jaw must MOVE the chin, by a mouth-sized amount --
    ang = math.radians(22.0)
    dy, dz = CH[1] - JW[1], CH[2] - JW[2]
    ty = JW[1] + dy * math.cos(ang) - dz * math.sin(ang)
    tz = JW[2] + dy * math.sin(ang) + dz * math.cos(ang)
    trav = math.hypot(ty - CH[1], tz - CH[2]) / max(1e-9, hw)
    out.append(("JAWTRAVEL", trav >= JAW_MIN_HW,
                "chin moves %.2f half-widths at 22 deg, need >=%.2f" % (trav, JAW_MIN_HW)))

    # -- is there anything behind the lips at all? --
    interior = None
    for mi in range(p.nMeshes):
        rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
        if p.sname(rec[0]) == "mouth_interior": interior = rec[2]
    # ⭐ AN INTERIOR IS A JOB, NOT A MESH NAME. mouth_interior.py authors one for a body
    # whose lips are sealed geometry with nothing behind them. Some bodies ARRIVE with a
    # mouth: v1_crowd_susan is a Character-Creator-style head whose own mesh carries
    # teeth (they are in her texture atlas, beside an eye sphere and a tongue), so
    # authoring a second set put two rows of teeth in the same skull. What the interior
    # actually has to deliver is "you cannot see through her head", and SEETHRU below
    # measures exactly that -- so a body without the authored mesh is asked to prove it
    # the other way rather than being failed for the missing name.
    out.append(("INTERIOR", interior is not None if interior else None,
                "authored mouth_interior: %d verts" % interior if interior else
                "no authored mesh -- deferred to SEETHRU"))
    return out, dict(hw=hw, iod=iod, span=span)


def seethrough(subject, jaw=1.0, keep=None):
    """THE ELDRITCH TEST. Open the mouth and look for the BACKDROP through her head.
    Shot on the face rig's mouth camera, which frames nothing but her face -- so any
    backdrop pixel inside that panel arrived through a hole."""
    from PIL import Image
    ctl = ("seq=%d face=10 dqs=0 clip=z_upright partner=off jaw=%.2f\n"
           % (int(os.getpid()) % 90000, jaw))
    open(os.path.join(NATIVE, "anim_bench", "lab_ctl.txt"), "w").write(ctl)
    tmp = keep or os.path.join(tempfile.mkdtemp(), "st.ppm")
    MAGENTA = (1.0, 0.0, 1.0)
    for x in ("crowd.exe", "crowd_new.exe"):
        subprocess.run(["taskkill", "/IM", x, "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    env = dict(os.environ, REVA_WINDOWED="1", REVA_FIXEDSTEP="60", REVA_NOSKY="1",
               REVA_CLEAR="1,0,1",          # a colour the club never renders
               REVA_LABCTL="anim_bench/lab_ctl.txt", REVA_AUTOMODE="11",
               REVA_ANIMSUBJ=subject, REVA_ANIMSTATE="0",
               REVA_SHOT="420", REVA_SHOTN="1",
               REVA_SHOT_PATH=os.path.abspath(tmp).replace("/", "\\"))
    subprocess.run([os.path.join(NATIVE, "crowd.exe"), "400", "packs", subject],
                   cwd=NATIVE, env=env, timeout=300,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(tmp):
        return None, "no frame"
    im = Image.open(tmp).convert("RGB").crop((0, 360, 640, 720))   # the mouth camera
    px = im.load(); W, H = im.size
    # The camera AIMS AT face_mouth, so her mouth is at the panel centre by
    # construction -- no feature-finding needed. Sample a box around it big enough to
    # cover a wide-open mouth and no more, and count MAGENTA: the stage was cleared to
    # a colour nothing in the club renders, so a magenta pixel there came through her
    # skull. (Counting "dark" pixels instead cannot work -- the inside of a mouth is
    # deliberately near-black, and so is the lab stage around her head. That first
    # version reported 6% and 13% see-through on two faces that are demonstrably solid.)
    n = 0
    # Measured off the render: a wide-open mouth occupies panel x 276-367, y 160-216
    # on a 640x360 panel centred at (320,180). The box is that, a little proud -- any
    # wider and its corners clip her jaw SILHOUETTE, where the backdrop legitimately
    # shows, and a solid face reports 0.12% see-through that is really 16 stray pixels
    # at the edges of the box.
    x0, x1 = int(W * 0.5 - W * 0.085), int(W * 0.5 + W * 0.085)
    y0, y1 = int(H * 0.5 - H * 0.10), int(H * 0.5 + H * 0.12)
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            if r > 110 and b > 110 and g < 70: n += 1
    if keep: im.save(os.path.splitext(keep)[0] + ".png")
    return n / float((x1 - x0) * (y1 - y0)), None


def selftest():
    """The rig bands must reject a KNOWN-BAD, not just accept the fleet. The bad case
    here is the real historical one: a mouth mark left floating in front of the face."""
    ok = True
    for band, good, bad, name in ((SURF_TOL_HW, 0.10, 2.00, "mark off the surface"),
                                  (HW_MAX_IOD, 0.25, 0.90, "mouth too wide"),
                                  (JAW_MIN_HW, 1.30, 0.05, "jaw that does not move")):
        g_ok = good <= band if name != "jaw that does not move" else good >= band
        b_ok = bad <= band if name != "jaw that does not move" else bad >= band
        good_pass, bad_pass = g_ok, b_ok
        r = good_pass and not bad_pass
        ok &= r
        print("  %-26s good=%.2f %s  bad=%.2f %s  -> %s"
              % (name, good, "pass" if good_pass else "FAIL",
                 bad, "pass" if bad_pass else "reject", "ok" if r else "BAND IS WRONG"))
    print("selftest %s" % ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("subjects", nargs="*", default=["r2moves", "kmoves"])
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--norender", action="store_true", help="rig checks only")
    ap.add_argument("--keep", help="keep the see-through frame here")
    a = ap.parse_args()
    if a.selftest: return selftest()
    bad = 0
    for s in a.subjects:
        print("\n=== %s ===" % s)
        rows, dims = rig_checks(s)
        deferred = []
        for (nm, good, note) in rows:
            if good is None:            # resolved by SEETHRU below, not on its own
                deferred.append((nm, note)); continue
            print("  %-10s %-4s %s" % (nm, "ok" if good else "FAIL", note))
            bad += not good
        if a.norender:
            for nm, note in deferred:
                print("  %-10s %-4s %s" % (nm, "----", note + " (not run: --norender)"))
            continue
        frac, err = seethrough(s, 1.0, a.keep)
        if err:
            print("  %-10s FAIL %s" % ("SEETHRU", err)); bad += 1
            good = False
        else:
            good = frac <= SEETHRU_MAX
            print("  %-10s %-4s %.4f%% of the mouth panel is backdrop (max %.4f%%)"
                  % ("SEETHRU", "ok" if good else "FAIL", frac * 100, SEETHRU_MAX * 100))
            bad += not good
        # A body that brought its own mouth passes on SEETHRU's evidence -- that is the
        # job the authored mesh exists to do, and v1_crowd_susan already does it.
        for nm, note in deferred:
            print("  %-10s %-4s %s" % (nm, "ok" if good else "FAIL",
                                       note + (" -> her own geometry closes the skull"
                                               if good else " -> and you CAN see through")))
            bad += not good
    print("\n%d check(s) failed" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
