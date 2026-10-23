"""lhshot.py -- shoot door 11's start menu (the last lighthouse) at several orbit phases.

    python anim_bench/lhshot.py out.png [--t 0,75,150,225] [--frame 300] [--env K=V ...] [--promoted]

Each phase is one boot of crowd_new.exe under REVA_MODE=10 + REVA_FIXEDSTEP=30 with REVA_LHT
setting the clock, so the shot is reproducible. Writes a 2x2 sheet (labelled) plus each frame.
"""
import argparse, os, subprocess, sys, tempfile
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)

def shoot(exe, t, frame, extra, outdir):
    path = os.path.join(outdir, f"lh_t{t}.ppm")
    if os.path.exists(path): os.remove(path)
    env = dict(os.environ, REVA_MODE="10", REVA_WINDOWED="1", REVA_FIXEDSTEP="30",
               REVA_SHOT=str(frame), REVA_SHOT_PATH=path, REVA_LHT=str(t))
    for kv in extra:
        k, v = kv.split("=", 1); env[k] = v
    r = subprocess.run([os.path.join(NATIVE, exe), "400", "packs", "select"], cwd=NATIVE, env=env,
                       capture_output=True, text=True, timeout=180)
    if not os.path.exists(path):
        print(r.stdout[-2000:], r.stderr[-2000:]); sys.exit(f"NO FRAME for t={t}")
    return Image.open(path).convert("RGB")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("--t", default="0,75,150,225")
    ap.add_argument("--frame", type=int, default=300); ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--promoted", action="store_true")
    a = ap.parse_args()
    exe = "crowd.exe" if a.promoted else "crowd_new.exe"
    ts = [x for x in a.t.split(",") if x]
    outdir = os.path.dirname(os.path.abspath(a.out)) or "."
    ims = []
    for t in ts:
        im = shoot(exe, t, a.frame, a.env, outdir)
        im.save(os.path.join(outdir, f"lh_t{t}.png")); ims.append((t, im))
    if len(ims) == 1:
        ims[0][1].save(a.out); print("wrote", a.out); return
    w, h = ims[0][1].size; cols = 2; rows = (len(ims) + 1) // 2
    sheet = Image.new("RGB", (w * cols, h * rows), (40, 40, 40)); d = ImageDraw.Draw(sheet)
    for i, (t, im) in enumerate(ims):
        x, y = (i % cols) * w, (i // cols) * h; sheet.paste(im, (x, y))
        d.text((x + 8, y + 6), f"t={t}s (+{a.frame/30:.0f}s)", fill=(0, 255, 120))
    sheet.save(a.out); print("wrote", a.out)

if __name__ == "__main__":
    main()
