#!/usr/bin/env python3
"""lookcheck.py -- shoot every scene and put ENOUGH FRAMES of each on one sheet to LOOK at.

jacob, 2026-09-03: "there are many many issues with everything from the set and scene to the
plot and camera lol if you take enough screenshots you will find most of them."

He is right, and the instruments this project already has are the reason it needed saying.
`framecheck` asks whether the camera is pointed at a body. `emptycheck` asks whether the
frame is blank. `scenecheck` asks whether anything moved. All three passed a shot of a
pickup truck. NONE of them asks the only question that matters -- what does it LOOK like --
because that question does not have a number, it has an eye, and the eye has to be given
frames.

So this is not a checker. It is a CONTACT SHEET GENERATOR: it shoots a scene through the
film's own camera and staging (straight out of shoot.py's table, so what you see is what
the film gets), samples it evenly across its whole length, and lays the frames out with
their timecode and the line that is being spoken on each one.

    python skate_src/film/lookcheck.py                 # every scene
    python skate_src/film/lookcheck.py --only boarding # one
    python skate_src/film/lookcheck.py --cols 4 --rows 3 --fps 12

⛔ ONE ENGINE AT A TIME. Scenes are shot sequentially on purpose -- two GPU instances on
this machine kill each other's captures.
⛔ AND THE CAMERA RUNS ONE FRAME BEHIND THE SCENE CLOCK, so a frame is sampled at the
MIDPOINT of the line it is labelled with, never at its start.
"""
import argparse
import glob
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
NATIVE = os.path.join(ROOT, "reva_native")
# THE SCRATCH DRIVE. Everything big that this project generates goes here.
OUTROOT = os.environ.get("REVA_OUTROOT", "out")
DRIVER = os.path.join(ROOT, ".claude", "skills", "run-reva", "driver.py")
PY = sys.executable
sys.path.insert(0, HERE)
import shoot  # noqa: E402  -- the film's own shot table is the source of truth

FFMPEG = shoot.FFMPEG


def scn_lines(name):
    """[(start, dur, speaker, text)] straight out of the compiled timeline."""
    p = os.path.join(NATIVE, "scenes", name + ".scn")
    out = []
    try:
        for ln in open(p, encoding="utf-8", errors="replace"):
            if ln.startswith("line "):
                b = ln.split(None, 4)
                out.append((float(b[2]), float(b[3]), int(b[1]), b[4].strip() if len(b) > 4 else ""))
    except OSError:
        pass
    return out


def shoot_scene(row, secs, fps, out_mp4):
    (name, act, subj, partner, p2, setname, stage, hour, camA, camB, _edl) = row
    sun, sky = shoot.LIGHT[hour]
    cmd = [PY, DRIVER, "scene", out_mp4, "--name", name, "--new",
           "--subject", subj, "--partner", partner,
           "--set", setname, "--stage", stage,
           "--sun", sun, "--sky", sky,
           "--seconds", "%g" % secs, "--fps", "%d" % fps]
    if p2:
        cmd += ["--partner2", p2]
    if camA:
        cmd += ["--cam", camA]
    r = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, errors="replace")
    return r.returncode == 0 and os.path.exists(out_mp4), r.stdout


def sheet(name, mp4, secs, cols, rows, outdir):
    """Sample cols*rows frames evenly, label each with its timecode and its line."""
    from PIL import Image, ImageDraw
    n = cols * rows
    lines = scn_lines(name)
    # sample at line midpoints when there are enough lines, else evenly across the take
    if len(lines) >= n:
        step = len(lines) / float(n)
        picks = [lines[min(len(lines) - 1, int(i * step))] for i in range(n)]
        times = [min(secs - 0.1, s + d * 0.5) for (s, d, _sp, _t) in picks]
        labels = ["%5.1fs  %s" % (t, (p[3] or "")[:46]) for t, p in zip(times, picks)]
    else:
        times = [secs * (i + 0.5) / n for i in range(n)]
        labels = ["%5.1fs" % t for t in times]
    tmp = os.path.join(outdir, "_f")
    os.makedirs(tmp, exist_ok=True)
    for f in glob.glob(os.path.join(tmp, "*.png")):
        os.remove(f)
    got = []
    for i, t in enumerate(times):
        p = os.path.join(tmp, "f%02d.png" % i)
        subprocess.run([FFMPEG, "-v", "error", "-ss", "%g" % t, "-i", mp4,
                        "-frames:v", "1", p, "-y"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        got.append(p if os.path.exists(p) else None)
    TW, TH, LAB = 428, 241, 17
    sh = Image.new("RGB", (TW * cols, (TH + LAB) * rows + 22), (12, 13, 17))
    d = ImageDraw.Draw(sh)
    d.text((8, 5), "%s   %s" % (name.upper(), mp4), fill=(150, 160, 180))
    for i, p in enumerate(got):
        x = (i % cols) * TW
        y = 22 + (i // cols) * (TH + LAB)
        if p:
            sh.paste(Image.open(p).convert("RGB").resize((TW, TH)), (x, y))
        else:
            d.rectangle([x, y, x + TW - 2, y + TH], fill=(40, 12, 12))
        d.text((x + 5, y + TH + 3), labels[i][:64], fill=(110, 220, 150))
    out = os.path.join(outdir, "%s.png" % name)
    sh.save(out)
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", default=None, help="comma list of scene names")
    ap.add_argument("--cols", type=int, default=4)
    ap.add_argument("--rows", type=int, default=3)
    # ⛔ DISK IS THE CEILING, NOT THE ENGINE. A frame is an uncompressed 2.76 MB PPM,
    # so `denial` at 12 fps is 2,235 frames = 6.2 GB and forty minutes of writing -- which
    # looked exactly like a hang and was not one. A contact sheet samples twelve frames;
    # it does not need 2,235. At 5 fps the same scene is 930 frames and under 3 GB, and
    # the mp4 still muxes against the wav because the scene clock IS the capture rate.
    ap.add_argument("--fps", type=int, default=5, help="capture fps; a sheet needs 12 frames, not 2000")
    ap.add_argument("--secs", type=float, default=0.0, help="0 = the scene's own length")
    # ⛔ CAPTURES DO NOT LIVE ON C:. jacob, 2026-09-03: "we are low on space on the c
    # drive. can you start using D:\reva902". A frame is an uncompressed 2.76 MB PPM and a
    # sweep of nine scenes stages tens of thousands of them; C: was down to 61 GB and D:
    # has 734. The capture TEMP follows this directory (the driver puts _capframes beside
    # the output), so pointing --out at D: moves the staging as well as the results.
    ap.add_argument("--out", default=os.path.join(OUTROOT, "look"))
    a = ap.parse_args(argv)

    rows = shoot.SHOTS
    if a.only:
        want = set(a.only.split(","))
        rows = [r for r in rows if r[0] in want]
    os.makedirs(a.out, exist_ok=True)
    made = []
    for row in rows:
        name = row[0]
        secs = a.secs or shoot.scnlen(name)
        mp4 = os.path.join(a.out, "%s.mp4" % name)
        print("== %-12s %5.1fs  set=%-12s hour=%s" % (name, secs, row[5], row[7]), flush=True)
        ok, log = shoot_scene(row, secs, a.fps, mp4)
        if not ok:
            print("   SHOOT FAILED\n" + "\n".join(log.splitlines()[-6:]), flush=True)
            continue
        p = sheet(name, mp4, secs, a.cols, a.rows, a.out)
        print("   sheet: %s   <-- LOOK AT IT" % p, flush=True)
        made.append(p)
    print("\n%d sheet(s) in %s" % (len(made), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
