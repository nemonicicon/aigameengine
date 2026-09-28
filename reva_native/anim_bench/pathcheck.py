#!/usr/bin/env python3
"""pathcheck.py -- the same body down every draw path. Do they agree?

    python reva_native/anim_bench/pathcheck.py [--subject kaeli_pj] [--clip idle]

WHY THIS EXISTS. The club draws a skinned body through THREE separate code paths -- the
HERO, the scene PARTNER, and a SET ACTOR -- and they have drifted apart repeatedly, because
a fix goes in where the bug was seen and nowhere else. In one session:

  * the ACTOR path had no tint for an untextured mesh, so a blue garment rendered BROWN;
  * the PARTNER path had the same gap and rendered the same garment INVISIBLE -- kaeli
    stood in the street in her pyjamas and came back naked, while REVA_CLOTHDBG happily
    reported a garment-sized box in exactly the right place;
  * cloth itself was bolted to the partner only, so neither the hero nor an actor could
    wear it, and the four-angle bench (which drives its subject as an ACTOR) could not see
    a garment at all.

Every one of those is the same shape of bug and NOTHING COULD SEE IT. shotsweep compares a
frame to its own past, so a defect present in all its shots is invisible to it; packcheck
reads files and never renders. This compares the paths TO EACH OTHER, which is the axis
neither of them covers.

⭐ THE CAMERA MOVES, NOT THE BODY. The three paths put a body in three different places
(the lab's hero sits at z -150, the partner beside it, an actor at the set origin), so each
run places the lens at the SAME OFFSET from whichever body it is drawing.

⛔ AND IT DOES NOT PIXEL-DIFF THEM, because the first version did and that was the wrong
test. The paths run different POSES -- the lab hero has its own state tree, the partner
plays the ctl, the actor plays the set -- so a straight diff read 17% before a single bug
existed and would have read 17% forever. Comparing incomparable things and calling the
number a regression is how a bench teaches people to ignore it.
⭐ SO IT MEASURES WHAT THE TEST IS ACTUALLY FOR: how much GARMENT each path drew, by
counting pixels near the garment's own tint. Pose-independent. A path that drops the mesh
goes to zero; a path that draws it near-white goes to zero too, because white is not the
colour it was asked for -- which is exactly the hero bug this found on its first run.
⛔ A FAIL is a summons to open the three PNGs, not a verdict.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.abspath(os.path.join(HERE, ".."))
ROOT = os.path.abspath(os.path.join(NATIVE, ".."))
PY = os.environ.get("CLUB_PYTHON", sys.executable)

SUN = "40,150,1.20,1.00,0.96,0.90"
SKY = "0.44,0.50,0.62"
# where the lens sits relative to whichever body this run is drawing
OFF = (2.60, 1.70, -3.90)
AIM = (0.00, 1.35, 0.00)
FOV = 42


def cam(bx, by, bz):
    return "%g,%g,%g,%g,%g,%g,%d" % (bx + OFF[0], by + OFF[1], bz + OFF[2],
                                     bx + AIM[0], by + AIM[1], bz + AIM[2], FOV)


def shoot(name, ctl, out, frame, subject, extra_env=None):
    p = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
    open(p, "w").write("seq=%d %s\n" % (int(time.time() * 7) % 100000, ctl))
    for x in ("crowd.exe", "crowd_new.exe"):
        subprocess.run(["taskkill", "/IM", x, "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ppm = out[:-4] + ".ppm"
    if os.path.exists(ppm):
        os.remove(ppm)
    e = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
             REVA_AUTOMODE="11", REVA_ANIMSUBJ=subject, REVA_ANIMSTATE="0",
             REVA_FIXEDSTEP="30", REVA_NOSKY="1",
             REVA_SHOT=str(frame), REVA_SHOTN="1", REVA_SHOT_PATH=ppm)
    if extra_env:
        e.update(extra_env)
    subprocess.run([os.path.join(NATIVE, "crowd.exe"), "400", "packs", subject],
                   cwd=NATIVE, env=e, timeout=300,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(ppm):
        return None
    from PIL import Image
    Image.open(ppm).convert("RGB").save(out)
    os.remove(ppm)
    return out


# ⛔ THE TOLERANCE IS TIGHT ON PURPOSE. kaeli has BLUE HAIR, near enough to a blue
# pyjama that a loose match scored two thousand 'garment' pixels on a body wearing
# nothing -- the known-bad run failed for the right answer and the wrong reason. Pick a
# garment tint that does not collide with the body, and keep this narrow.
def garment_area(with_png, without_png):
    """The garment is WHAT CHANGED when the mesh was hidden.

    ⛔ THREE COLOUR-MATCHING ATTEMPTS FAILED BEFORE THIS, each differently: a loose RGB
    match counted kaeli's BLUE HAIR as garment; a tight one rejected the LIT garment,
    because shading slides a colour a long way from its base; and matching HUE counted the
    entire SKY, which is the same blue. Colour cannot answer this question on a body whose
    hair, sky and clothes are all blue.
    ⭐ SO DO NOT ASK IT IN COLOUR. Render each path twice -- once normally, once with
    REVA_HIDEMESH suppressing the garment -- and difference the pair. Same path, same frame,
    same pose, one mesh apart: the difference IS the garment, in pixels, with no threshold
    to tune and nothing to confuse it.
    """
    from PIL import Image, ImageChops
    A = Image.open(with_png).convert("RGB")
    B = Image.open(without_png).convert("RGB")
    d = ImageChops.difference(A, B).convert("L").point(lambda v: 255 if v >= 10 else 0)
    n = sum(d.histogram()[255:])
    # ⭐ AND THE COLOUR OF WHAT CHANGED. Hiding a white garment and hiding a blue one leave
    # the SAME amount of difference, so area alone would have sailed past the hero drawing
    # this garment near-white -- which is the bug this file found on its first run. The diff
    # already knows which pixels are garment; average them.
    px, dp = A.load(), d.load()
    w, h = A.size
    r = g = b = 0
    c = 0
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            if dp[x, y]:
                q = px[x, y]
                r += q[0]; g += q[1]; b += q[2]; c += 1
    mean = (r / c, g / c, b / c) if c else (0.0, 0.0, 0.0)
    return n, mean


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--subject", default="kaeli_pj")
    ap.add_argument("--clip", default="idle")
    ap.add_argument("--frame", type=int, default=470)
    ap.add_argument("--mesh", default="~cloth",
                    help="substring of the mesh to suppress for the second render")
    ap.add_argument("--maxcol", type=float, default=62.0,
                    help="max mean-colour deviation between paths. Loose because the LAB "
                         "deliberately lights its hero differently -- neutral studio light, "
                         "so it reads 105,132,201 against the partner's 147,174,212 on the "
                         "same fabric. That is a real difference and not a defect. What this "
                         "must catch is the hero drawing it NEAR-WHITE, which was ~90 off.")
    ap.add_argument("--minfrac", type=float, default=0.25,
                    help="a path under this fraction of the best has LOST the mesh. Loose on "
                         "purpose: the paths run different poses, so a crouching actor shows "
                         "less garment than a standing hero and that is not a defect. What "
                         "this must catch is zero.")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()

    out = os.path.join(HERE, "_path")
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out, exist_ok=True)

    # the ACTOR gets a set with nothing in it but the actor
    open(os.path.join(NATIVE, "sets", "_pathcheck.set"), "w").write(
        "\n".join([
            "# _pathcheck.set -- WRITTEN BY anim_bench/pathcheck.py, overwritten every run.",
            "# Nothing but the subject: a prop in frame would be a difference the other two",
            "# paths cannot reproduce.",
            "actor v1_%s  %s   0 0 0  90  1.0" % (a.subject, a.clip),
            ""]))

    HX, HZ = 0.0, -150.0        # where the lab parks its hero
    GAP = 8.0                   # far enough that the hero is out of the partner's frame
    common = "face=0 grid=0 dqs=0 sun=%s sky=%s" % (SUN, SKY)

    runs = [
        ("hero", "%s partner=off clip=%s cam=%s" % (common, a.clip, cam(HX, 0, HZ))),
        ("partner", "%s partner=%s pclip=%s pgap=%g pyaw=90 cam=%s"
         % (common, a.subject, a.clip, GAP, cam(HX, 0, HZ + GAP))),
        ("actor", "%s partner=off set=_pathcheck cam=%s" % (common, cam(0, 0, 0))),
    ]
    got, gone = {}, {}
    for name, ctl in runs:
        got[name] = shoot(name, ctl, os.path.join(out, name + ".png"), a.frame, a.subject)
        # ...and the same frame again with the garment suppressed
        gone[name] = shoot(name, ctl, os.path.join(out, name + "_off.png"), a.frame,
                           a.subject, {"REVA_HIDEMESH": a.mesh})
        print("  %-8s %s" % (name, "ok" if got[name] and gone[name] else "NO FRAME"))

    if not (all(got.values()) and all(gone.values())):
        print("missing frames -- cannot compare")
        return 1

    print()
    meas = {n: garment_area(got[n], gone[n]) for n in got}
    counts = {n: meas[n][0] for n in meas}
    means = {n: meas[n][1] for n in meas}
    top = max(counts.values()) or 1
    bad = False
    for n in ("hero", "partner", "actor"):
        frac = counts[n] / float(top)
        miss = frac < a.minfrac
        bad |= miss
        m = means[n]
        print("  %-8s garment %7d px  %5.0f%% of best   colour %3.0f,%3.0f,%3.0f%s"
              % (n, counts[n], frac * 100, m[0], m[1], m[2], "   <-- FAIL" if miss else ""))
    print()
    if top < 2000:
        print("FAIL: NO path drew a meaningful amount of garment (best %d px)." % top)
        print("Either the subject has no `#rrggbb` mesh, or every path has lost it.")
        return 1
    # ⭐ AND THE COLOURS MUST AGREE. Same fabric, three paths.
    ref = means[max(counts, key=counts.get)]
    for n in means:
        m = means[n]
        dev = max(abs(m[k] - ref[k]) for k in range(3))
        if dev > a.maxcol:
            bad = True
            print("  %-8s colour is %.0f off the best path -- drawing the same garment a "
                  "different colour" % (n, dev))
    if bad:
        print("FAIL: a draw path is missing the garment, or drawing it the wrong colour.")
        print("A FAIL is a summons, not a verdict: open the three PNGs in %s and LOOK." % out)
        print("The usual cause is a fix that landed on one path and not the others --")
        print("a tint, a matrix restore, a new layer hooked into only the place it was needed.")
        return 1
    print("all three draw paths render the garment -- %s" % out)
    if not a.keep:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
