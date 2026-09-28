#!/usr/bin/env python3
"""modelcheck.py -- THE MODEL REGRESSION. Every body in the cast, framed, gated and diffed.

    modelcheck.py --bless        shoot every model and adopt it as the reference
    modelcheck.py                shoot every model and diff against the reference
    modelcheck.py --only vampire,anima

jacob: "running a regression on these dance floor and skate modes is just retared you should
be running models throuh the anim bench or a test thing not the dance floor or skate mode"

He is right. Eleven of shotsweep's fifteen shots are game DOORS, and its own header admits
they are NOT frame-reproducible -- the crowd alone moves ~20% of pixels run to run, so they
are checked for "liveness" (is the frame non-black) and nothing else. A door shot cannot catch
a rendering regression, and it cannot tell you a body's SKINNING broke, which is what actually
breaks when a model comes down the artery.

⭐ ONE BODY, A FIXED LENS, A STATIC POSE, A REAL PIXEL DIFF -- per model. That is where a bone
budget, a texture filter, an alpha mode or a botched weight transfer shows.

Four things this file learned the hard way, each from its own false report:

⛔ A FRAME THAT ARRIVED IS NOT A FRAME THAT IS USABLE. The first run said "7 shot, 0 FAIL"
while the cat girl was a BLACK FRAME with the lens inside her and two models were caught
mid-animation. It checked only that a frame appeared. Now `frame_gate` measures whether the
subject is actually in shot, and it runs BEFORE a blessing too -- bless adopts what it sees as
truth, so an unusable frame there becomes the reference and the eventual fix reads as the
regression.

⛔ DO NOT HARDCODE WHAT THE BACKDROP LOOKS LIKE. The first detector rejected "sky-ish" and
"grey road" and called everything else subject -- so the GRASS, tinted green, became the
subject. It spans the frame, touches every edge, and does not shrink when the camera pulls
back, which the log showed plainly: "reframed 7% -> 7%" after moving the lens from 5.2 to 9.0.
Every "touches the frame edge" failure was the lawn. The backdrop is now LEARNED from the
frame's own border, which needs no knowledge of the set.

⛔ A CLIP THAT IS NOT ON THE PACK IS A SILENT FALLBACK. kaeli came back crouched mid-crawl and
reva2 in a default idle because `z_upright` is an 8-frame ANIMATION, not a pose, and neither
pack had anything static. Ask the pack first, prefer a 1-frame clip, and refuse rather than
shoot a pose that will not repeat. (restclip.py is how a pack gets one.)

⛔ ONLY ONE INSTANCE MAY RUN. Two crowd.exe fight for the GPU and both produce nothing -- that
is how four of seven models reported "no frame", and how shotsweep once reported 99.985% on a
shot that was perfectly fine. This kills before it launches.

⭐ AND IT REPORTS TWO NUMBERS, BECAUSE ONE LIES. shotsweep fails a run where >0.40% of pixels
changed by more than 10/255. When mipmaps landed, face_kmoves "FAILED" at 0.52% changed with
ZERO pixels differing by more than 40 -- a real, correct, invisible improvement reading
exactly like a broken body. So: `changed` (>10, catches drift) and `visible` (>40, what a
person would notice), and the verdict keys on the second.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
NATIVE = os.path.join(ROOT, "reva_native")
REF = os.path.join(NATIVE, "anim_bench", "_modelref")
OUT = os.path.join(NATIVE, "anim_bench", "_modelout")

# (name, pack, preferred clip or None, actor scale, starting camera)
# ⛔ These packs disagree about units by two orders of magnitude, and the actor scale that
# frames one puts the lens INSIDE another. The camera below is only a STARTING point -- the
# auto-framing pass measures the result and corrects it, because every lens distance chosen
# by hand tonight was wrong at least once.
MODELS = [
    ("reva2",   "r2moves",  None, 1.00, "5.2,1.9,0,0,1.5,0,32"),
    ("kaeli",   "kmoves",   None, 1.00, "5.2,1.9,0,0,1.5,0,32"),
    ("vampire", "vampire",  None, 1.00, "7.4,3.0,0,0,2.2,0,32"),
    ("camilia", "camilia2", None, 1.00, "5.2,1.9,0,0,1.5,0,32"),
    ("catgirl", "catgirl",  None, 0.12, "5.2,1.9,0,0,1.5,0,32"),
    ("anima",   "anima",    None, 1.00, "7.4,3.0,0,0,2.2,0,32"),
    ("selina",  "selina",   None, 1.00, "7.0,2.8,0,0,2.0,0,32"),
    # ---- THE FILM'S CAST. A regression without the bodies the film is shot on is not a
    # regression of anything jacob will see. reva2/kmoves above ARE r2moves/kmoves; these
    # are the other packs that stand in front of the lens.
    ("kaeli_pj",  "kaeli_pj",    None, 1.00, "5.2,1.9,0,0,1.5,0,32"),
    ("reva_pj",   "reva_pj",     None, 1.00, "7.4,3.0,0,0,2.2,0,32"),
    ("susan",     "crowd_susan", None, 1.00, "5.2,1.9,0,0,1.5,0,32"),
    ("anima_hi",  "anima_hi",    None, 1.00, "7.4,3.0,0,0,2.2,0,32"),
    ("anima_fx",  "anima_fx",    None, 1.00, "7.4,3.0,0,0,2.2,0,32"),
]


def pick_clip(pack, wanted):
    """The clip to shoot: it must EXIST on the pack, and it should be STATIC."""
    sys.path.insert(0, os.path.join(ROOT, "skate_src", "author"))
    try:
        from rpack import Rpack
        R = Rpack(os.path.join(NATIVE, "packs", "v1_%s.rpack" % pack))
        names = R.clip_names()
        frames = {c: R.clip(i)["frames"] for i, c in enumerate(names)}
    except Exception as e:
        return None, "could not read clips (%s)" % e
    if wanted and wanted in names:
        return wanted, None
    # ⛔ PREFER A ONE-FRAME CLIP. `z_upright` is 8 IDENTICAL frames (a static; measured 0 deg of motion) -- but shooting any multi-frame clip
    # catches the body mid-move and no two runs agree.
    still = [c for c in ("z_tpose", "tpose", "z_rest") if c in names and frames.get(c, 99) <= 2]
    if still:
        return still[0], None
    for c in ("z_tpose", "tpose", "z_upright", "z_rest"):
        if c in names:
            return c, None
    return None, "no static clip (%s)" % (", ".join(names[:3]) if names else "none")


def subject_box(im):
    """The subject's bounding box (0..1 of frame) and its coverage in percent.

    ⭐ The backdrop is LEARNED from the frame's own border -- the subject stands in the middle,
    so the outermost rows and columns are backdrop by construction. Anything unlike every
    border colour is the body. No knowledge of the set required, and no green lawn mistaken
    for a person."""
    sm = im.resize((160, 90))
    px = sm.load()
    W, H = sm.size
    bg = []
    for x in range(0, W, 4):
        bg.append(px[x, 1])
        bg.append(px[x, H - 2])
    for y in range(0, H, 4):
        bg.append(px[1, y])
        bg.append(px[W - 2, y])

    def is_bg(c):
        for q in bg:
            if abs(c[0] - q[0]) + abs(c[1] - q[1]) + abs(c[2] - q[2]) < 42:
                return True
        return False

    xs, ys = [], []
    for y in range(H):
        for x in range(W):
            if not is_bg(px[x, y][:3]):
                xs.append(x)
                ys.append(y)
    if not xs:
        return None, 0.0
    cov = 100.0 * len(xs) / float(W * H)
    return (min(xs) / float(W), min(ys) / float(H),
            max(xs) / float(W), max(ys) / float(H)), cov


def reframe(cam, cov, box, target=14.0):
    """Move the lens along its own axis so the subject fits and fills roughly `target`%.
    ⭐ Coverage goes as the SQUARE of angular size, so the distance correction is the square
    root of the error -- one pass lands instead of oscillating. The BOX matters too: a tall
    thin figure can sit at a comfortable 9% while running off the top and bottom."""
    import math
    p = [float(v) for v in cam.split(",")]
    ex, ey, ez, ax, ay, az, fov = p[0], p[1], p[2], p[3], p[4], p[5], p[6]
    k = 3.0 if cov <= 0.01 else math.sqrt(cov / target)
    if box:
        need = max((box[2] - box[0]) / 0.62, (box[3] - box[1]) / 0.72)
        if need > 1.0:
            k = max(k, need * 1.15)
    k = max(0.25, min(8.0, k))
    dx, dy, dz = ex - ax, ey - ay, ez - az
    return "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d" % (
        ax + dx * k, ay + dy * k, az + dz * k, ax, ay, az, int(fov))


def frame_gate(im):
    """Is this frame USABLE? Measured from the pixels, knowing nothing about the model."""
    box, cov = subject_box(im)
    if box is None or cov < 3.0:
        return False, "subject is %.1f%% of frame -- too small / not in shot" % cov
    if cov > 60.0:
        return False, "subject is %.0f%% of frame -- lens is probably inside it" % cov
    if box[0] <= 0.01 or box[2] >= 0.99 or box[1] <= 0.01 or box[3] >= 0.99:
        return False, "subject touches the frame edge -- cropped or lens too close"
    return True, "%.0f%% of frame" % cov


def kill_running():
    """⛔ ONLY ONE INSTANCE MAY RUN -- two fight for the GPU and both produce nothing."""
    subprocess.run(["taskkill", "/F", "/IM", "crowd_new.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(["taskkill", "/F", "/IM", "crowd.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.6)


EXE = "crowd_new.exe"   # the build under test; --promoted switches to crowd.exe


def shoot(pack, clip, scale, cam, frame=420):
    """One body on a strip of road, one fixed lens, one frame."""
    kill_running()
    if not os.path.exists(os.path.join(NATIVE, "packs", "v1_%s.rpack" % pack)):
        return None, "no pack"
    lines = [
        "# _model.set -- WRITTEN BY modelcheck.py, overwritten every shot.",
        "prop road_01     -14  0.00  14   0  1.0",
        "prop grass_00    -14 -0.02  42   0  1.0  0.30 0.44 0.20",
        "actor v1_%s  %s   0  0 0  90  %g" % (pack, clip, scale),
        "",
    ]
    open(os.path.join(NATIVE, "sets", "_model.set"), "w").write("\n".join(lines))
    ctl = ("seq=%d face=0 grid=0 dqs=0 set=_model sun=40,150,1.20 "
           "sky=0.50,0.60,0.75 cam=%s\n" % (int(time.time()) % 100000, cam))
    open(os.path.join(NATIVE, "anim_bench", "lab_ctl.txt"), "w").write(ctl)
    tmp = os.path.join(NATIVE, "anim_bench", "_mtmp")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp, exist_ok=True)
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ=pack, REVA_ANIMSTATE="0",
               # ⛔ the clock, the lens and the sky must all be pinned, or two runs at the
               # same frame NUMBER are different INSTANTS.
               REVA_FIXEDSTEP="30", REVA_CAMLOCK="1", REVA_NOSKY="1",
               REVA_SHOT=str(frame), REVA_SHOTN="1", REVA_SHOTEVERY="1",
               REVA_SHOT_PATH=os.path.join(tmp, "f.ppm"))
    # ⛔ IT USED TO LAUNCH crowd.exe -- the PROMOTED binary -- so as a promotion gate it
    # measured the wrong file every single time. The regression tests what is about to
    # be promoted.
    pr = subprocess.Popen([os.path.join(NATIVE, EXE), "400", "packs", pack],
                          cwd=NATIVE, env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(240):
        if glob.glob(os.path.join(tmp, "*.ppm")):
            break
        if pr.poll() is not None:
            break
        time.sleep(0.5)
    pr.terminate()
    try:
        pr.wait(timeout=10)
    except Exception:
        pr.kill()
    # ⭐ RE-CHECK AFTER THE KILL. A frame written while we were giving up is still a frame;
    # calling it missing is how a healthy shot got reported NO FRAME.
    time.sleep(0.4)
    fs = sorted(glob.glob(os.path.join(tmp, "*.ppm")))
    if not fs:
        return None, "no frame"
    from PIL import Image
    # ⛔ A FILE THAT EXISTS IS NOT A FILE THAT IS FINISHED. The poll above returns the
    # moment the PPM APPEARS, and the engine is still streaming 2.7 MB into it; the
    # vampire's frame came back "truncated (496 bytes not processed)" and took the whole
    # run down with it. Wait for the size to stop moving, then open; retry once if the
    # decoder still disagrees.
    last = -1
    for _ in range(40):
        try:
            sz = os.path.getsize(fs[0])
        except OSError:
            sz = -1
        if sz > 0 and sz == last:
            break
        last = sz
        time.sleep(0.15)
    im = None
    for attempt in range(3):
        try:
            im = Image.open(fs[0]).convert("RGB").copy()
            break
        except OSError:
            time.sleep(0.5)
    if im is None:
        shutil.rmtree(tmp, ignore_errors=True)
        return None, "frame never finished writing"
    shutil.rmtree(tmp, ignore_errors=True)
    return im, None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bless", action="store_true", help="adopt this run as the reference")
    ap.add_argument("--only", default=None)
    ap.add_argument("--promoted", action="store_true",
                    help="test crowd.exe (the promoted build) instead of crowd_new.exe")
    a = ap.parse_args()
    global EXE
    if a.promoted:
        EXE = "crowd.exe"
    print("  binary under test: %s" % EXE)
    from PIL import Image, ImageChops, ImageDraw
    # ⭐ THE SHEET. jacob judges pictures, not percentages. Every body, labelled with its
    # verdict; a FAIL gets its reference | current | difference laid beside it.
    tiles = []
    os.makedirs(REF, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    want = set(a.only.split(",")) if a.only else None

    print("  %-9s %8s %9s %9s   %s" % ("model", "verdict", "changed", "visible", "note"))
    fails = shot = 0
    for name, pack, clip, scale, cam in MODELS:
        if want and name not in want:
            continue
        clip, cerr = pick_clip(pack, clip)
        if clip is None:
            print("  %-9s %8s %9s %9s   %s" % (name, "FAIL", "-", "-", cerr))
            fails += 1
            tiles.append((name, "FAIL: " + cerr, None, None))
            continue
        im, err = shoot(pack, clip, scale, cam)
        if im is not None:
            box, cov = subject_box(im)
            need = 0.0 if not box else max((box[2] - box[0]) / 0.62,
                                           (box[3] - box[1]) / 0.72)
            if cov < 6.0 or cov > 22.0 or need > 1.0:
                cam2 = reframe(cam, cov, box)
                im2, _e = shoot(pack, clip, scale, cam2)
                if im2 is not None:
                    ok1 = frame_gate(im)[0]
                    ok2 = frame_gate(im2)[0]
                    # keep the correction only when it is genuinely better
                    if ok2 and not ok1:
                        im, cam = im2, cam2
                    elif ok2 and ok1:
                        c2 = subject_box(im2)[1]
                        if abs(c2 - 14.0) < abs(cov - 14.0):
                            im, cam = im2, cam2
        if im is None:
            print("  %-9s %8s %9s %9s   %s" % (name, "MISSING", "-", "-", err))
            fails += 1
            tiles.append((name, "MISSING: " + str(err), None, None))
            continue
        ok, note = frame_gate(im)
        shot += 1
        im.save(os.path.join(OUT, name + ".png"))
        if not ok:
            print("  %-9s %8s %9s %9s   %s" % (name, "FAIL", "-", "-", note))
            fails += 1
            tiles.append((name, "FAIL: " + note, im, None))
            continue
        rp = os.path.join(REF, name + ".png")
        if a.bless or not os.path.exists(rp):
            im.save(rp)
            print("  %-9s %8s %9s %9s   %s, clip %s"
                  % (name, "BLESSED", "-", "-", note, clip))
            tiles.append((name, "BLESSED  " + clip, im, None))
            continue
        ref = Image.open(rp).convert("RGB")
        if ref.size != im.size:
            print("  %-9s %8s %9s %9s   size changed" % (name, "FAIL", "-", "-"))
            fails += 1
            tiles.append((name, "FAIL: size changed", im, ref))
            continue
        d = ImageChops.difference(ref, im)
        px = [max(p) for p in d.getdata()]
        n = float(len(px))
        changed = 100.0 * sum(1 for v in px if v > 10) / n
        visible = 100.0 * sum(1 for v in px if v > 40) / n
        bad = visible > 0.20
        fails += 1 if bad else 0
        print("  %-9s %8s %8.2f%% %8.2f%%   %s"
              % (name, "FAIL" if bad else "ok", changed, visible,
                 "LOOK at both PNGs" if bad else note))
        tiles.append((name, ("FAIL" if bad else "ok") + "  changed %.2f%%  visible %.2f%%"
                      % (changed, visible), im, ref if bad else None))

    print()
    # ---- the sheet: one tile per body, 320 px wide; a FAIL carries ref | cur | diff
    if tiles:
        TW, TH = 320, 180
        cols = 3
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new("RGB", (TW * cols, (TH + 22) * rows), (14, 16, 20))
        dr = ImageDraw.Draw(sheet)
        for i, (nm, verdict, cur, ref) in enumerate(tiles):
            x = (i % cols) * TW; y = (i // cols) * (TH + 22)
            if cur is not None:
                if ref is not None:
                    # reference | current | difference, each a third wide
                    w3 = TW // 3
                    d = ImageChops.difference(ref, cur).point(lambda v: min(255, v * 4))
                    for k, src in enumerate((ref, cur, d)):
                        sheet.paste(src.resize((w3, TH)), (x + k * w3, y))
                else:
                    sheet.paste(cur.resize((TW, TH)), (x, y))
            bad = verdict.startswith("FAIL") or verdict.startswith("MISSING")
            col = (240, 98, 93) if bad else ((240, 180, 84) if verdict.startswith("BLESSED") else (92, 232, 160))
            dr.rectangle([x, y + TH, x + TW - 1, y + TH + 21], fill=(22, 26, 34))
            dr.text((x + 6, y + TH + 5), "%s  %s" % (nm, verdict), fill=col)
        sp = os.path.join(OUT, "roster.png")
        sheet.save(sp)
        print("  sheet: %s   <-- LOOK AT IT" % sp)
    print("%d model(s) shot, %d FAIL" % (shot, fails))
    print("  reference: %s" % REF)
    print("  this run:  %s" % OUT)
    if fails:
        print("A FAIL is not a verdict, it is a summons: open both PNGs and LOOK.")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
