#!/usr/bin/env python3
"""compare_sheet.py -- x3d's sheet-scale renders against her character sheet: side by side, overlaid, scored.

    python skate_src/x3d/compare_sheet.py D:/reva902/x3d/x3d_sheet.png D:/reva902/x3d/build [--out compare.png]

build_x3d.py renders cmp_front/side/back.png at the sheet's own scale (1 px = 2.42 mm) through the same crop
windows cut here, in the sheet's pose (arms hanging), on a transparent film. For each view:
  [the sheet] [the render] [the render with the SHEET's outline in red] [the sheet with the RENDER's in cyan]
and the silhouette IoU -- a number that goes UP as the shape converges. The sheet's figure is everything that is
not its grey-and-white checkerboard (neutral and light).
⛔ The IoU is a shape score, not a likeness verdict: a face can be wrong with a perfect outline. Look at the tiles.
"""
import os, sys

import numpy as np
from PIL import Image, ImageDraw

CROPS = {"front": (20, 25, 280, 745), "side": (262, 25, 470, 745), "back": (640, 25, 860, 745)}


def outline(mask):
    m = mask
    e = m & ~(np.roll(m, 1, 0) & np.roll(m, -1, 0) & np.roll(m, 1, 1) & np.roll(m, -1, 1))
    return e


def main():
    sheet_path, build = sys.argv[1], sys.argv[2]
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else os.path.join(build, "compare.png")
    sheet = Image.open(sheet_path).convert("RGB")
    a = np.asarray(sheet).astype(int)
    bg = ((a.max(2) - a.min(2)) <= 6) & (a.min(2) >= 222)
    rows = []
    for view, (x0, y0, x1, y1) in CROPS.items():
        s_rgb = np.asarray(sheet.crop((x0, y0, x1, y1))).copy()
        s_mask = ~bg[y0:y1, x0:x1]
        r = Image.open(os.path.join(build, "cmp_%s.png" % view)).convert("RGBA")
        if r.size != (x1 - x0, y1 - y0):
            r = r.resize((x1 - x0, y1 - y0))
        ra = np.asarray(r).astype(float)
        r_mask = ra[..., 3] > 127
        white = np.full(ra.shape[:2] + (3,), 255.0)
        r_rgb = (ra[..., :3] * (ra[..., 3:] / 255.0) + white * (1 - ra[..., 3:] / 255.0)).astype(np.uint8)
        inter = (s_mask & r_mask).sum(); uni = (s_mask | r_mask).sum()
        iou = inter / max(uni, 1)
        ov1 = r_rgb.copy(); ov1[outline(s_mask)] = (230, 30, 30)
        ov2 = s_rgb.copy(); ov2[outline(r_mask)] = (0, 200, 230)
        row = np.concatenate([s_rgb, r_rgb, ov1, ov2], 1)
        img = Image.fromarray(row)
        ImageDraw.Draw(img).text((6, 6), "%s  IoU %.3f" % (view, iou), fill=(0, 0, 0))
        rows.append(img)
        print("%-6s silhouette IoU %.3f  (sheet %d px, render %d px)" % (view, iou, s_mask.sum(), r_mask.sum()))
    W = max(r.size[0] for r in rows); Hh = sum(r.size[1] for r in rows)
    sheet_out = Image.new("RGB", (W, Hh), (255, 255, 255))
    y = 0
    for r in rows:
        sheet_out.paste(r, (0, y)); y += r.size[1]
    sheet_out.save(out)
    print("wrote", out)


if __name__ == "__main__":
    main()
