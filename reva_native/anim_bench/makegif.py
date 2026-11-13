#!/usr/bin/env python3
"""makegif.py -- turn a REVA_SHOTN burst into an animated GIF.

Contact sheets show poses side by side; they do not show TIMING. A loop plays at the
real cadence, which is the only way a human (or a reviewer squinting at frames) can
judge weight, hang time and snap.

    makegif.py out.gif 40 crop_x0 y0 x1 y1 frame*.ppm [--gain G]
        (40 = ms per frame; use the capture's REVA_SHOTEVERY/60 to keep real time)

--gain multiplies every channel by G before quantising. The lab stage is DARK -- a body
on the wall measures ~18/255 mean, kaeli and reva2 alike -- and a move cannot be judged
in pixels nobody can see. The gain is uniform across the whole frame and every frame, so
it changes exposure and nothing else: no crop, no per-frame levelling, no curve. Poses,
timing and contacts read exactly as captured.
"""
import sys
from PIL import Image, ImageEnhance

def main():
    argv = sys.argv[:]
    gain = 1.0
    if "--gain" in argv:
        i = argv.index("--gain"); gain = float(argv[i + 1]); del argv[i:i + 2]
    out, ms = argv[1], int(argv[2])
    x0, y0, x1, y1 = map(int, argv[3:7])
    files = argv[7:]
    frames = [Image.open(f).convert("RGB").crop((x0, y0, x1, y1)) for f in files]
    if gain != 1.0:
        frames = [ImageEnhance.Brightness(f).enhance(gain) for f in frames]
    if not frames:
        raise SystemExit("no frames")
    w, h = frames[0].size
    scale = min(1.0, 480.0 / max(w, h))
    if scale < 1.0:
        frames = [f.resize((int(w*scale), int(h*scale))) for f in frames]
    pal = [f.convert("P", palette=Image.ADAPTIVE, colors=64) for f in frames]
    pal[0].save(out, save_all=True, append_images=pal[1:], duration=ms, loop=0, optimize=True)
    print("%s: %d frames, %dms each (%.1fs loop)" % (out, len(pal), ms, len(pal)*ms/1000.0))

if __name__ == "__main__":
    main()
