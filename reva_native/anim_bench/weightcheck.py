"""weightcheck.py -- THE INSTRUMENT FOR THE WILLIAMS LAYER.

Richard Williams' test for a walk, in his own terms, is the FIRST thing he says he looks
for: how much up and down there is on the head. He tells the story of recognising a walk
from fifty yards with out-of-focus peripheral vision, and what he had actually read was
that the head did not rise and fall. The amount of up and down IS the read.

So that is what this measures, and it measures it the only way this project accepts:
ON PIXELS. One body walks a straight path past a locked side lens; for every frame we
find the TOP OF THE HEAD (the highest pixel that is not sky, self-calibrated from the
frame's own top-left corner) and plot it against time.

    python anim_bench/weightcheck.py            # the layer on
    python anim_bench/weightcheck.py --off      # REVA_NOWMS=1, for the comparison
    python anim_bench/weightcheck.py --selftest # no engine; check the head finder

⛔ THE HEAD MUST BE AGAINST THE SKY. If the lens puts a house behind her the finder is
measuring the roofline, and it will happily report a beautifully steady walk. The camera
below is aimed UP the street with nothing behind her on purpose, and the report prints
how many DISTINCT rows the top-of-head took -- a constant row means the finder locked
onto scenery, not onto her, and the number says so instead of the sheet looking fine.

⛔ AND REVA_SHOT IS WHAT ARMS A CAPTURE. SHOTN/SHOTEVERY only say how many and how often,
and the engine never exits once its quota is met, so this polls and kills it.
"""
import io, os, sys, glob, time, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
NAT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "_weight")
SET = os.path.join(NAT, "sets", "wmstest.set")

# a locked side lens with nothing behind her: she walks across, the sky is the backdrop
CAM = "6.0,1.58,-8.0,6.0,1.52,0.0,34"
FIRST, N, EVERY = 240, 72, 2       # 72 frames at every 2nd -> ~4.8s of a 30fps clock

SETSRC = """# wmstest -- ONE body, ONE walk cycle, ON THE SPOT, nothing behind her.
# ⛔ SHE MUST NOT TRAVEL. A head at a fixed world height projects to a DIFFERENT screen
# row as it crosses the frame, and over ten units of street that swamped the few
# centimetres we are trying to measure -- 211 px of perspective on top of a ~14 px bob.
# Walking on the spot removes the trend entirely, and the layer still engages because its
# activity term reads the FEET SWAPPING, which a cycle does whether or not it goes
# anywhere. (Williams says never to ANIMATE a cycle in place. This is measuring one.)
actor v1_r2moves   w_walk   6.00  0.02  0.00   90  1.000  1.0  0.0
"""


def _mask(im, sky, thr=26):
    """Everything that is not the sky, as a 1-bit mask."""
    from PIL import Image, ImageChops
    flat = Image.new("RGB", im.size, tuple(sky))
    d = ImageChops.difference(im, flat).convert("L")
    return d.point(lambda v: 255 if v > thr else 0)


def headrow(im, sky):
    """The top of HER head.

    ⛔ NOT simply the topmost non-sky pixel: the lab draws its HUD in the top-left, and
    a finder that takes the highest pixel in the frame measures the text -- which never
    moves, so it reports a perfectly steady walk and looks like a result. So: find her
    COLUMNS first, in the lower two thirds where only she and her shadow live, then take
    the top of the frame within a band around her. The HUD is never inside that band.
    """
    W, H = im.size
    m = _mask(im, sky)
    low = m.crop((0, int(H * 0.42), W, H))
    bb = low.getbbox()
    if not bb:
        return None
    cx = (bb[0] + bb[2]) // 2
    half = max(8, int(W * 0.07))            # proportional: a fixed 90px band swallowed
    x0, x1 = max(0, cx - half), min(W, cx + half)   # the decoy on a small frame
    col = m.crop((x0, 0, x1, H))
    cb = col.getbbox()
    return cb[1] if cb else None


def selftest():
    """The finder, against a synthetic frame whose head row we chose -- including a
    decoy HUD stripe higher up and further left, which is the failure this exists for."""
    from PIL import Image
    W, H = 200, 150
    sky = (107, 120, 148)
    im = Image.new("RGB", (W, H), sky)
    px = im.load()
    for r in range(60, H):                       # her, on the right
        for c in range(140, 160):
            px[c, r] = (200, 60, 60)
    for c in range(4, 70):                       # the decoy HUD, top-left
        px[c, 12] = (240, 220, 40)
    got = headrow(im, list(sky))
    assert got == 60, "head finder said %r, wanted 60 (it took the HUD)" % got
    assert headrow(Image.new("RGB", (W, H), sky), list(sky)) is None
    print("selftest OK: the finder tracks her past a decoy HUD, and refuses an empty frame")


def main():
    off = "--off" in sys.argv
    exe = "crowd_new.exe"
    with io.open(SET, "w", encoding="utf-8", newline="\n") as f:
        f.write(SETSRC)
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    ctl = os.path.join(HERE, "lab_ctl.txt")
    with io.open(ctl, "w", encoding="utf-8", newline="\n") as f:
        f.write("seq=%d face=0 grid=0 dqs=0 clip=idle set=wmstest "
                "sun=42,200,1.15,1.00,0.95,0.88 sky=0.420,0.470,0.580 cam=%s\n"
                % (os.getpid(), CAM))
    env = dict(os.environ)
    env.update(REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ="r2moves", REVA_ANIMSTATE="0",
               REVA_FIXEDSTEP="30", REVA_NOSKY="1",
               REVA_SHOT=str(FIRST), REVA_SHOTN=str(N), REVA_SHOTEVERY=str(EVERY),
               REVA_SHOT_PATH=os.path.join(OUT, "f.ppm"))
    if off:
        env["REVA_NOWMS"] = "1"
    p = subprocess.Popen([os.path.join(NAT, exe), "400", "packs", "r2moves"],
                         cwd=NAT, env=env,
                         stdout=open(os.path.join(OUT, "engine.log"), "w"),
                         stderr=subprocess.STDOUT)
    t0 = time.time()
    while time.time() - t0 < 150:
        if len(glob.glob(os.path.join(OUT, "*.ppm"))) >= N:
            break
        time.sleep(1.5)
    p.kill()
    time.sleep(0.6)

    fs = sorted(glob.glob(os.path.join(OUT, "*.ppm")))
    if not fs:
        print("NO FRAMES -- see %s" % os.path.join(OUT, "engine.log"))
        return 1
    from PIL import Image
    ys = []
    for f in fs:
        im = Image.open(f).convert("RGB")
        ys.append(headrow(im, list(im.getpixel((2, 2)))))
    good = [y for y in ys if y is not None]
    if len(good) < 8:
        print("only %d/%d frames found a head -- the lens is wrong" % (len(good), len(ys)))
        return 1

    # ⛔ ONE BAD FRAME MUST NOT SET THE RANGE. The finder mis-read exactly one frame out
    # of 72 (it reported row 174 in a series that otherwise lives in 112..127) and a plain
    # min/max turned a 15px bob into a 62px one. Report a ROBUST range and count the
    # outliers out loud instead of hiding them inside the headline.
    sv = sorted(good)
    med = sv[len(sv) // 2]
    keep = [y for y in good if abs(y - med) <= 40]
    out = len(good) - len(keep)
    lo, hi = min(keep), max(keep)
    distinct = len(set(keep))
    # median-filter of 3 before counting turns, so a flat top does not read as chatter
    f = keep[:]
    for i in range(1, len(keep) - 1):
        f[i] = sorted(keep[i - 1:i + 2])[1]
    d = [b - a for a, b in zip(f, f[1:]) if b != a]
    turns = sum(1 for a, b in zip(d, d[1:]) if (a > 0) != (b > 0))
    print("--- weightcheck (%s) ---" % ("layer OFF" if off else "layer ON"))
    print("  frames           : %d (%d found a head, %d outliers dropped)"
          % (len(ys), len(good), out))
    print("  head top rows    : %d..%d   TRAVEL = %d px" % (lo, hi, hi - lo))
    print("  distinct rows    : %d%s" % (distinct,
          "" if distinct > 3 else "   <-- flat: either the walk has no weight, or the finder is on scenery"))
    print("  direction changes: %d  (Williams: a walk rises and falls TWICE per stride)" % turns)
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    else:
        sys.exit(main())
