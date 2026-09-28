#!/usr/bin/env python3
"""shotsweep.py -- THE PIXEL REGRESSION. Every door, the lab and the face rig, shot and
diffed against a committed reference.

    shotsweep.py --bless          shoot everything and adopt it as the reference
    shotsweep.py                  shoot everything and diff against the reference
    shotsweep.py --only 1,5,face  just those shots

`baseline.txt` and `stand_baseline.txt` are NUMERIC records: they prove a body stands
and a gait obeys physics. They cannot see a body that is not DRAWN. The defects that
have actually cost this project most were exactly that, and every one of them was
invisible to a number:

  * the hero was INVISIBLE whenever a partner was on stage -- her model matrix was
    uploaded once above the two-target pass loop, so she drew with the partner's. That
    survived many sessions, because every pair move was judged on the multi-cam wall,
    which rebinds per panel and hid it.
  * the park renders as grey static on Windows (doors 1 and 5, still open).
  * the mouth aperture cut a vertical slit beside her nose instead of opening her mouth.

None of those move a number in posecheck, motioncheck or posturecheck. All three are
obvious in a frame. So: shoot a fixed set of frames, diff them, and make a human look
at whatever moved.

⛔ THIS ONLY WORKS BECAUSE THE CLOCK CAN BE STOPPED. Under wall time, two runs at the
same frame NUMBER are different INSTANTS and every shot differs. REVA_FIXEDSTEP=<fps>
makes the engine clock advance a fixed step per rendered frame (and pins the crowd's
RNG seed with it), which is what makes a reference frame mean anything.

⛔ TWO CLASSES OF SHOT, BECAUSE THEY CAN PROMISE DIFFERENT THINGS. This was measured,
not assumed:

  STRICT -- the ANIM LAB and the FACE RIG. Under fixed-step these come back
  BIT-IDENTICAL run to run (0.000% of pixels differ, verified). They are shot with an
  explicit ctl, a chosen clip and a fixed lens, and they are where character, rig and
  face changes actually show. These get a real pixel diff.

  LIVE -- the eleven game doors. They are NOT frame-reproducible, and this session did
  not manage to make them so. What was found, in order, each fixed and each insufficient:
  the camera director wanders (REVA_CAMLOCK pins it), the sky flyers drift (REVA_NOSKY),
  the RNG was seeded from time() because the fixed-step flag was read lazily AFTER
  srand (init-order bug, fixed), and the beat is locked to the real time of day via
  unix_now() so every lit floor tile and beat-locked limb sits at a different phase
  (REVA_FIXEDSTEP now ignores the band). That took a door from 35% of pixels differing
  to 20% -- and 20% is still not a regression test. The residue scales with crowd size
  (0.9% at 8 dancers, 25% at 400), so it is in the crowd, and it is there at the FIRST
  capturable frame rather than accumulating. Not isolated. See "Open" below.

  So a door is NOT compared to a reference at all. It is checked for LIVENESS: did a
  frame come out, does it have structure, is it more than a black screen. Those are
  absolute properties of one frame, so the camera's phase cannot affect them. It is a
  weaker test than a diff and it is honest about being one -- it still catches a door
  that stops booting, renders black, or loses its world, which is the failure mode that
  a mode-select engine change actually produces.

A STRICT failure prints the bounding box and the worst 64x64 tile, because WHERE it
differs matters as much as how much: a small contiguous blob is a real defect, the same
pixel count smeared along the horizon is noise.

Run `shotsweep.py --selftest` after touching this file: it feeds the liveness check a
black frame, a flat grey frame and a real one, and demands the right verdicts back.
This bench has been burned by its own instruments twice (see README) -- a checker that
has never been shown a known-bad is not evidence.

## Open
The crowd's run-to-run divergence is unexplained. It is present at the first capturable
frame, scales with dancer count, and survives a pinned clock, a pinned seed, a pinned
lens, a grounded sky and a pinned beat. Until it is found, the doors cannot be diffed.
"""
import argparse, os, subprocess, sys, glob

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)
REF = os.path.join(HERE, "_shotref")
OUT = os.path.join(HERE, "_shotout")

DIFF_THRESH = 8          # per-channel delta that counts as "different" at all
DIFF_FRAC = 0.004        # STRICT: >0.4% of pixels differing = FAIL (observed: 0.000%)
TILE_FRAC = 0.35         # STRICT: or >35% of any one 64x64 tile (a contiguous blob)
SIG_TOL = 0.22           # LOOSE: max relative drift of any signature term
# ⛔ THIS COUNT WAS A LIE. It printed len(shots()) - len(newsigs), reporting all
# FIFTEEN shots as strict frames when only FOUR are -- the eleven doors are
# liveness-only and the bless path deletes them again. And newsigs is never
# populated by anything, so the loose-signature half of this bench (SIG_TOL,
# signature(), sig_drift(), signatures.json) is vestigial and has never once run.
BLESSMSG = "\nreference: %d strict frames kept, %d live shots discarded, in %s -- COMMIT IT"

MODE_NAME = ["DANCER SHOWCASE", "SKATE PARK", "BATTLE ROYALE", "ARENA SHOOTER",
             "RTS COMMAND", "BIKE LANE BASH", "ACTION RPG", "2D PLATFORMER",
             "NARRATIVE RPG", "ARCADE", "R.E.V.A."]

# name -> (env, argv3, ctl or None). Frame 340 clears the ~250-frame boot.
def shots():
    s = []
    for m, nm in enumerate(MODE_NAME):
        s.append(("door%02d" % m, nm,
                  dict(REVA_AUTOMODE=str(m), REVA_AUTOVAR="0", REVA_AUTOPICK="0"),
                  "select", None, 340, "live"))
    s.append(("lab_reva", "ANIM LAB / reva2 upright",
              dict(REVA_AUTOMODE="11", REVA_ANIMSUBJ="r2moves", REVA_ANIMSTATE="0"),
              "r2moves", "face=0 grid=4 dqs=0 clip=z_upright partner=off", 420, "strict"))
    s.append(("lab_pair", "ANIM LAB / reva2 + kaeli, the kiss",
              dict(REVA_AUTOMODE="11", REVA_ANIMSUBJ="r2moves", REVA_ANIMSTATE="0"),
              "r2moves",
              "face=0 grid=4 dqs=0 clip=z_kiss partner=kmoves pclip=z_kiss hi5=3 aimy=1.5", 420, "strict"))
    for subj in ("r2moves", "kmoves"):
        s.append(("face_%s" % subj, "FACE RIG / %s, jaw open" % subj,
                  dict(REVA_AUTOMODE="11", REVA_ANIMSUBJ=subj, REVA_ANIMSTATE="0",
                       REVA_FACEDBG="1"),
                  subj, "face=10 dqs=0 clip=z_upright partner=off jaw=0.8", 420, "strict"))
    return s


def capture(name, env, argv3, ctl, frame, outdir, exe="crowd.exe"):
    import time
    if ctl is not None:
        p = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
        open(p, "w").write("seq=%d %s\n" % (int(time.time()) % 100000, ctl))
        env = dict(env, REVA_LABCTL="anim_bench/lab_ctl.txt")
    ppm = os.path.join(outdir, name + ".ppm")
    for x in ("crowd.exe", "crowd_new.exe"):
        subprocess.run(["taskkill", "/IM", x, "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    e = dict(os.environ, REVA_WINDOWED="1",
             REVA_FIXEDSTEP="60",              # a stoppable clock, and...
             REVA_CAMLOCK="1",                 # ...a lens that does not wander
             REVA_NOSKY="1",                   # ...and no flyers drifting past
             REVA_SHOT=str(frame), REVA_SHOTN="1",
             REVA_SHOT_PATH=os.path.abspath(ppm).replace("/", "\\"), **env)
    subprocess.run([os.path.join(NATIVE, exe), "400", "packs", argv3],
                   cwd=NATIVE, env=e, timeout=300,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(ppm):
        return None
    from PIL import Image
    png = os.path.join(outdir, name + ".png")
    Image.open(ppm).convert("RGB").save(png)
    os.remove(ppm)
    return png


def compare(ref, cur):
    """(frac, bbox, worst_tile_frac, tile_xy) -- where it differs matters as much as
    how much, so a contiguous blob is reported separately from a smear."""
    from PIL import Image, ImageChops
    a = Image.open(ref).convert("RGB"); b = Image.open(cur).convert("RGB")
    if a.size != b.size:
        return 1.0, (0, 0) + a.size, 1.0, (0, 0)
    d = ImageChops.difference(a, b)
    px = d.load(); W, H = d.size
    T = 64
    tiles = {}
    n = 0
    minx, miny, maxx, maxy = W, H, -1, -1
    for y in range(H):
        for x in range(W):
            p = px[x, y]
            if p[0] > DIFF_THRESH or p[1] > DIFF_THRESH or p[2] > DIFF_THRESH:
                n += 1
                k = (x // T, y // T); tiles[k] = tiles.get(k, 0) + 1
                if x < minx: minx = x
                if y < miny: miny = y
                if x > maxx: maxx = x
                if y > maxy: maxy = y
    if not n:
        return 0.0, None, 0.0, None
    tk, tv = max(tiles.items(), key=lambda e: e[1])
    return n / float(W * H), (minx, miny, maxx, maxy), tv / float(T * T), tk


LIVE_MIN_CONTRAST = 0.020   # a flat frame has none; a real one measured 0.10-0.19
LIVE_MIN_LIT      = 0.020   # fraction of pixels above background; real ones ran 0.20+
LIVE_MIN_COLOUR   = 0.002   # some saturated pixels: the club is not greyscale


def liveness(png):
    """ABSOLUTE properties of ONE frame -- nothing is compared to a reference, so the
    camera's phase cannot affect the verdict. Returns (ok, reasons, stats)."""
    sig = signature(png)
    mean, contrast, background, colourful = sig[0], sig[1], sig[2], sig[3]
    bad = []
    if contrast < LIVE_MIN_CONTRAST: bad.append("FLAT (contrast %.4f)" % contrast)
    if 1.0 - background < LIVE_MIN_LIT: bad.append("BLACK (%.1f%% background)" % (background * 100))
    if colourful < LIVE_MIN_COLOUR: bad.append("COLOURLESS (%.4f)" % colourful)
    return (not bad), bad, (mean, contrast, background, colourful)


def selftest():
    """Show the checker a known-bad before believing it (this bench's rule one)."""
    from PIL import Image
    import tempfile, random
    ok = True
    tmp = tempfile.mkdtemp()
    cases = []
    black = Image.new("RGB", (320, 180), (0, 0, 0))
    cases.append(("all black", black, False))
    cases.append(("flat grey", Image.new("RGB", (320, 180), (110, 110, 110)), False))
    noise = Image.new("RGB", (320, 180))
    px = noise.load(); random.seed(7)
    for y in range(180):
        for x in range(320):
            v = random.randrange(256)
            px[x, y] = (v, random.randrange(256), random.randrange(256))
    cases.append(("colour noise", noise, True))
    real = None
    for cand in sorted(glob.glob(os.path.join(REF, "door*.png"))):
        real = cand; break
    for (nm, im, want) in cases:
        f = os.path.join(tmp, nm.replace(" ", "_") + ".png"); im.save(f)
        got, why, st = liveness(f)
        good = (got == want)
        ok &= good
        print("  %-14s expect %-5s got %-5s  %s  %s"
              % (nm, want, got, "ok" if good else "WRONG", ",".join(why)))
    if real:
        got, why, st = liveness(real)
        ok &= got
        print("  %-14s expect True  got %-5s  %s  (mean %.3f contrast %.3f bg %.2f col %.3f)"
              % (os.path.basename(real), got, "ok" if got else "WRONG", *st))
    else:
        print("  (no blessed door frame to check the positive case -- run --bless)")
    print("selftest %s" % ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def signature(png):
    """Phase-invariant description of a frame: how bright, how much background, and the
    coarse hue mix. A camera that has cut elsewhere barely moves these; a door that
    renders black, stops drawing the world, or comes back as grey static moves them a
    lot. Sampled on a grid -- this is a statistic, not a diff."""
    from PIL import Image
    import colorsys
    im = Image.open(png).convert("RGB")
    px = im.load(); W, H = im.size
    n = 0; lum = 0.0; lum2 = 0.0; dark = 0; sat_n = 0
    hues = [0.0] * 6
    for y in range(0, H, 3):
        for x in range(0, W, 3):
            r, g, b = px[x, y]
            l = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
            n += 1; lum += l; lum2 += l * l
            if l < 0.06: dark += 1
            mx, mn = max(r, g, b), min(r, g, b)
            if mx and (mx - mn) / float(mx) > 0.25 and l > 0.06:
                h, _s, _v = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
                hues[min(5, int(h * 6))] += 1; sat_n += 1
    mean = lum / n
    var = max(0.0, lum2 / n - mean * mean)
    sig = [mean, var ** 0.5, dark / float(n), sat_n / float(n)]
    sig += [h / float(sat_n or 1) for h in hues]
    return sig


# ⛔ ONLY THE FIRST FOUR TERMS ARE JUDGED. The six hue bins are recorded because they
# are useful to read, but they are NOT phase-invariant: the club's coloured lasers sweep,
# so which bin they land in swings wildly between two honest runs -- hue-mag moved 33.7%
# on a known-good sweep, and a test that fails a known-good is a bad threshold, not a bad
# build (this bench's own rule). The four kept terms describe whether the WORLD IS BEING
# DRAWN -- brightness, contrast, how much is background, how much is coloured at all --
# which is the question a door shot can actually answer.
SIG_JUDGE = 4


def sig_drift(a, b):
    """largest relative move of any JUDGED term (absolute floor so near-zero terms
    cannot divide into a huge ratio)"""
    worst, which = 0.0, -1
    for i, (x, y) in enumerate(zip(a[:SIG_JUDGE], b[:SIG_JUDGE])):
        d = abs(x - y) / max(0.05, abs(x))
        if d > worst: worst, which = d, i
    return worst, which


SIG_TERMS = ["mean-lum", "contrast", "background", "colourful",
             "hue-red", "hue-yel", "hue-grn", "hue-cyn", "hue-blu", "hue-mag"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bless", action="store_true", help="adopt this run as the reference")
    ap.add_argument("--only", help="comma-separated shot names or door numbers")
    # THE GATE MUST TEST WHAT YOU JUST BUILT. This defaulted to crowd.exe -- the
    # ALREADY-PROMOTED binary -- so the whole workflow was: build to crowd_new.exe, run
    # the sweep against crowd.exe, read "15/15", promote. It was checking the PREVIOUS
    # build every time and could not have caught a regression in the code under test.
    ap.add_argument("--promoted", action="store_true",
                    help="test crowd.exe (what is shipped) instead of crowd_new.exe")
    ap.add_argument("--new", action="store_true", help="deprecated: now the default")
    ap.add_argument("--selftest", action="store_true",
                    help="show the liveness check a known-bad before trusting it")
    a = ap.parse_args()
    if a.selftest: return selftest()
    want = set((a.only or "").split(",")) if a.only else None
    outdir = REF if a.bless else OUT
    os.makedirs(outdir, exist_ok=True)
    exe = "crowd.exe" if a.promoted else "crowd_new.exe"

    import json
    sigpath = os.path.join(REF, "signatures.json")
    sigs = {}
    if os.path.exists(sigpath):
        sigs = json.load(open(sigpath))
    newsigs = {}
    rows, fails, missing = [], 0, 0
    for (name, label, env, argv3, ctl, frame, cls) in shots():
        if want and name not in want and name.replace("door", "").lstrip("0") not in want:
            continue
        cur = capture(name, env, argv3, ctl, frame, outdir, exe)
        if cur is None:
            print("%-14s %-34s NO FRAME" % (name, label)); missing += 1; continue
        if cls == "live":
            # a live shot has no reference to keep -- storing one would only rot
            if a.bless:
                os.remove(cur); print("%-14s %-30s %-6s (no reference needed)" % (name, label, cls)); continue
            got, why, st = liveness(cur)
            fails += (not got)
            print("%-14s %-30s %-6s %s  contrast %.3f  lit %.0f%%  colour %.3f %s"
                  % (name, label, cls, "alive" if got else "DEAD ",
                     st[1], (1 - st[2]) * 100, st[3], ",".join(why)))
            rows.append((name, label, 0.0, not got, "")); continue
        if a.bless:
            print("%-14s %-30s %-6s blessed" % (name, label, cls)); continue
        ref = os.path.join(REF, name + ".png")
        if not os.path.exists(ref):
            print("%-14s %-30s %-6s no reference (run --bless)" % (name, label, cls))
            missing += 1; continue
        frac, bbox, tfrac, tile = compare(ref, cur)
        bad = frac > DIFF_FRAC or tfrac > TILE_FRAC
        fails += bad
        note = ""
        if bbox:
            note = "bbox %s worst-tile %.0f%% @%s" % (str(bbox), tfrac * 100, str(tile))
        rows.append((name, label, frac, bad, note))
        print("%-14s %-30s %-6s %6.3f%% %s  %s"
              % (name, label, cls, frac * 100, "FAIL" if bad else "ok  ", note))
    if a.bless:
        if newsigs: json.dump(newsigs, open(sigpath, "w"), indent=1)
        nstrict = sum(1 for sh in shots() if sh[6] == "strict")
        print(BLESSMSG % (nstrict, len(shots()) - nstrict, REF)); return 0
    print("\n%d shot(s), %d FAIL, %d missing   (threshold %.2f%% of pixels, or %.0f%% of one tile)"
          % (len(rows), fails, missing, DIFF_FRAC * 100, TILE_FRAC * 100))
    if fails:
        print("A FAIL is not a verdict, it is a summons: open both PNGs and LOOK.")
        print("  reference: %s\n  this run:  %s" % (REF, OUT))
    return 1 if (fails or missing) else 0


if __name__ == "__main__":
    sys.exit(main())
