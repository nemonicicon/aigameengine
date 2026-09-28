#!/usr/bin/env python3
"""filmstrip.py -- motion on paper.

Takes a REVA_SHOTN burst and produces (1) a contact-sheet strip of a cropped region
across every frame and (2) a ghost overlay (all frames averaged) where smooth arcs
read as trails and pops read as doubled bodies. Judge TIMING and CONTINUITY here;
judge poses in the stills.

usage: filmstrip.py out.png X0 Y0 X1 Y1 frame1.png frame2.png ...
"""
import sys
from PIL import Image, ImageChops

def main():
    out = sys.argv[1]
    x0, y0, x1, y1 = map(int, sys.argv[2:6])
    files = sys.argv[6:]
    crops = [Image.open(f).convert("RGB").crop((x0, y0, x1, y1)) for f in files]
    w, h = crops[0].size
    strip = Image.new("RGB", (w*len(crops) + 2*(len(crops)-1), h), (10, 4, 18))
    for i, c in enumerate(crops):
        strip.paste(c, (i*(w+2), 0))
    strip.save(out)
    acc = crops[0].point(lambda p: p // len(crops))
    for c in crops[1:]:
        acc = ImageChops.add(acc, c.point(lambda p: p // len(crops)))
    acc.save(out.replace(".png", "_ghost.png"))
    print(f"strip {strip.size[0]}x{strip.size[1]} from {len(crops)} frames -> {out} (+_ghost)")

if __name__ == "__main__":
    main()
