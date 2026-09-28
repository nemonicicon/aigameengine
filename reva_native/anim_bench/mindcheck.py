#!/usr/bin/env python3
"""mindcheck.py -- THE MIND BENCH. sets/_mind.set from overhead, N frames apart, at fixed step,
with the engine's [mind]/[nav] log kept. The receipt is the sheet (do they move, do they go
ROUND the houses, does the zombie reach anyone) plus the log (wanders, flees, TURNED).

    mindcheck.py                      4 frames, 5 s apart
    mindcheck.py --frames 6 --every 90 --out out/mind.png
"""
import argparse, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.normpath(os.path.join(HERE, ".."))
EXE = os.path.join(NATIVE, "crowd_new.exe")


def win(p):
    return os.path.abspath(p).replace("/", "\\")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=4)
    ap.add_argument("--every", type=int, default=150)
    ap.add_argument("--first", type=int, default=480)
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--set", default="_mind")
    ap.add_argument("--cam", default="32,44,-12,32,0,-11.5,62")
    ap.add_argument("--tag", default="mind", help="which [tag] lines to count and show")
    ap.add_argument("--env", action="append", default=[], help="extra K=V for the engine")
    ap.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "."), "mindcheck.png"))
    a = ap.parse_args()
    subprocess.run(["taskkill", "/F", "/IM", "crowd_new.exe"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    stem = os.path.join(os.environ.get("TEMP", "."), "mindcheck")
    for f in os.listdir(os.path.dirname(stem)):
        if f.startswith("mindcheck_") and f.endswith(".ppm"):
            os.remove(os.path.join(os.path.dirname(stem), f))
    ctl = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
    with open(ctl, "w") as f:
        f.write("seq=%d face=0 grid=0 dqs=0 set=%s tod=12 shadow=0 post=0 cam=%s\n" % (int(time.time()) % 100000, a.set, a.cam))
    env = dict(os.environ)
    env.update(REVA_AUTOMODE="11", REVA_ANIMSUBJ="r2moves", REVA_ANIMSTATE="0", REVA_WINDOWED="1",
               REVA_FIXEDSTEP=str(a.fps), REVA_NAVDBG="1", REVA_RIGIDDBG="1", REVA_NOAUDIO="1",
               REVA_SHOT=str(a.first), REVA_SHOTN=str(a.frames), REVA_SHOTEVERY=str(a.every),
               REVA_SHOT_PATH=win(stem + ".ppm"), REVA_LABCTL="anim_bench/lab_ctl.txt")
    for kv in a.env:
        k, _, v = kv.partition("="); env[k] = v
    log = open(stem + ".log", "w")
    p = subprocess.Popen([EXE, "400", "packs", "r2moves"], cwd=NATIVE, env=env, stdout=log, stderr=subprocess.STDOUT)
    want = ["%s_%05d.ppm" % (stem, i) for i in range(a.frames)] if a.frames > 1 else [stem + ".ppm"]
    t0 = time.time()
    while time.time() - t0 < 240:
        if all(os.path.exists(w) and os.path.getsize(w) > 1000 for w in want):
            break
        if p.poll() is not None:
            break
        time.sleep(0.5)
    time.sleep(0.5)
    p.kill(); log.close()
    have = [w for w in want if os.path.exists(w)]
    print("frames: %d/%d in %.0fs" % (len(have), a.frames, time.time() - t0))
    lines = open(stem + ".log", encoding="utf-8", errors="replace").read().splitlines()
    nav = [l for l in lines if l.startswith("[nav]")]
    mind = [l for l in lines if l.startswith("[" + a.tag + "]")]
    print("\n".join(nav[:2]))
    print("%d [%s] lines: %d wander, %d flee, %d hunt, %d TURNED, %d bounce, %d at rest" % (len(mind), a.tag,
        sum("wanders" in l for l in mind), sum("FLEES" in l for l in mind),
        sum("hunts" in l for l in mind), sum("TURNED" in l for l in mind), sum("bounce" in l for l in mind), sum("at rest" in l for l in mind)))
    for l in mind[:40]:
        print("  " + l)
    if not have:
        print("mindcheck: FAIL -- no frames"); return 1
    from PIL import Image, ImageDraw
    ims = [Image.open(w).convert("RGB") for w in have]
    w, h = ims[0].size
    cols = 2 if len(ims) > 1 else 1
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols // 2, (h // 2 + 18) * rows), (8, 8, 10))
    d = ImageDraw.Draw(sheet)
    for i, im in enumerate(ims):
        x = (i % cols) * (w // 2); y = (i // cols) * (h // 2 + 18)
        sheet.paste(im.resize((w // 2, h // 2)), (x, y + 18))
        d.text((x + 4, y + 2), "t=%.1fs (frame %d)" % ((a.first + i * a.every - a.first) / a.fps, a.first + i * a.every), fill=(255, 230, 120))
    sheet.save(a.out)
    print("wrote", a.out, " <-- LOOK AT IT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
