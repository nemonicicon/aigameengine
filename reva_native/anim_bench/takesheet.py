"""takesheet.py -- a recorded TAKE (driver.py scene's mp4) on one sheet: one frame per line, or two takes side by side.
(10-23, the film pass's sample rounds.) perline.py is its sibling for a sweep's folder of frames; this reads the mp4.

  takesheet.py sheet  <mp4> <out.png> [--n 16] [--cols 4] [--w 480]       evenly through the take
  takesheet.py lines  <mp4> <scn> <out.png> [--cols 4] [--w 480]          one tile per scene line, at its middle
  takesheet.py compare <out.png> <oldmp4> <oldscn> <newmp4> <newscn> <k,k,...> [--w 560] [--old LABEL] [--new LABEL]
      rows = the script's line k in each take, OLD | NEW, each tile bannered (readable at 25%). Matched by LINE, not
      by time: a round that changes the voices or the gaps moves every line.
  takesheet.py strip <out.png> <oldmp4> <oldscn> <newmp4> <newscn> <k> [--n 6] [--step 0.5] [--w 320]
      line k IN MOTION: n frames `step` s apart from 0.4 s into it, a row per take (a still cannot show a head move)
⛔ A .scn `line` record is `line <spk> <t0> <DURATION> <text>` -- the fourth field is not the end time.
"""
import glob, os, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFont

FF = __import__("shutil").which("ffmpeg") or sorted(glob.glob(__import__("os").path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages\Gyan.FFmpeg*\ffmpeg*\bin\ffmpeg.exe")))[0]   # club-engine kit: machine-independent paths
FP = FF[:-len("ffmpeg.exe")] + "ffprobe.exe"
TMP = tempfile.mkdtemp(prefix="vsheet_")


def font(sz):
    for f in (r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf"):
        if os.path.exists(f): return ImageFont.truetype(f, sz)
    return ImageFont.load_default()


def dur(mp4):
    return float(subprocess.check_output([FP, "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", mp4]).decode())


def grab(mp4, t, w):
    p = os.path.join(TMP, "f_%d_%.3f.png" % (abs(hash(mp4)) % 100000, t))
    subprocess.run([FF, "-v", "error", "-y", "-ss", "%.3f" % max(0.0, t), "-i", mp4, "-frames:v", "1", "-vf", "scale=%d:-2" % w, p], check=True)
    return Image.open(p).convert("RGB")


def label(im, txt, sz=None, where="tl", fill=(255, 255, 255), bg=(0, 0, 0)):
    d = ImageDraw.Draw(im); sz = sz or max(14, im.width // 22); f = font(sz)
    x0, y0, x1, y1 = d.textbbox((0, 0), txt, font=f)
    w, h = x1 - x0 + 12, y1 - y0 + 10
    x = 4 if "l" in where else im.width - w - 4
    y = 4 if "t" in where else im.height - h - 4
    d.rectangle([x, y, x + w, y + h], fill=bg)
    d.text((x + 6 - x0, y + 5 - y0), txt, font=f, fill=fill)
    return im


def tile(ims, cols, pad=6, bg=(24, 24, 28)):
    w, h = ims[0].size; rows = (len(ims) + cols - 1) // cols
    S = Image.new("RGB", (cols * w + (cols + 1) * pad, rows * h + (rows + 1) * pad), bg)
    for i, im in enumerate(ims):
        S.paste(im, (pad + (i % cols) * (w + pad), pad + (i // cols) * (h + pad)))
    return S


def scn_lines(scn):
    out = []
    for ln in open(scn, encoding="utf-8"):
        p = ln.split(" ", 4)
        if p[0] == "line": out.append((float(p[2]), float(p[2]) + float(p[3]), p[4].strip() if len(p) > 4 else ""))
    return out


def opt(a, k, d):
    return type(d)(a[a.index(k) + 1]) if k in a else d


def main(a):
    if a[0] == "sheet":
        mp4, out = a[1], a[2]; n, cols, w = opt(a, "--n", 16), opt(a, "--cols", 4), opt(a, "--w", 480)
        T = dur(mp4); ts = [T * (i + 0.5) / n for i in range(n)]
        tile([label(grab(mp4, t, w), "%.1f s" % t) for t in ts], cols).save(out)
    elif a[0] == "lines":
        mp4, scn, out = a[1], a[2], a[3]; cols, w = opt(a, "--cols", 4), opt(a, "--w", 480)
        L = scn_lines(scn)
        tile([label(grab(mp4, (t0 + t1) / 2, w), "%d  %.1f s" % (k, (t0 + t1) / 2)) for k, (t0, t1, _) in enumerate(L)], cols).save(out)
    elif a[0] == "strip":           # (10-23) a line IN MOTION: n frames `step` s apart, one row per take
        out, om, os_, nm, ns = a[1:6]; k = int(a[6])
        n, step, w = opt(a, "--n", 6), opt(a, "--step", 0.5), opt(a, "--w", 320)
        lo, ln = opt(a, "--old", "V2"), opt(a, "--new", "V3")
        rows = []
        for mp4, scn, lab, bg in ((om, os_, lo, (110, 30, 30)), (nm, ns, ln, (20, 90, 40))):
            t0, t1, _ = scn_lines(scn)[k]
            rows += [label(grab(mp4, t0 + 0.4 + step * i, w), "%s  +%.1f s" % (lab, 0.4 + step * i), bg=bg) for i in range(n)]
        tile(rows, n).save(out)
        print("wrote", out); return
    elif a[0] == "compare":
        out, om, os_, nm, ns = a[1:6]; ks = [int(x) for x in a[6].split(",")]
        w = opt(a, "--w", 560); lo, ln = opt(a, "--old", "V1 (sent)"), opt(a, "--new", "V2 (now)")
        Lo, Ln = scn_lines(os_), scn_lines(ns)
        ims = []
        for k in ks:
            (a0, a1, _), (b0, b1, _) = Lo[k], Ln[k]
            fo = 0.35 if a1 - a0 < 8 else 0.25          # inside the line, past its first cut
            ims.append(label(grab(om, a0 + (a1 - a0) * fo, w), lo, bg=(110, 30, 30)))
            ims.append(label(grab(nm, b0 + (b1 - b0) * fo, w), ln, bg=(20, 90, 40)))
        tile(ims, 2).save(out)
    print("wrote", a[2] if a[0] in ("sheet",) else (a[3] if a[0] == "lines" else a[1]))


if __name__ == "__main__":
    main(sys.argv[1:])
