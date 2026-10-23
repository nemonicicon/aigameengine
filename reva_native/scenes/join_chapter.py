"""join_chapter.py -- a run of scene takes joined in order into one file, with an index of where each scene starts.

    python join_chapter.py <takes dir> <first> <last> [--take scene_{n}.mp4] [--script scene_{n}.txt]
                           [--label {n}] [--out joined.mp4] [--index <out>_index.txt]

{n} is the scene's number. --take names each take inside <takes dir>; --script names its script in this folder (the
index takes a scene's title from the script's first line, and from a generated header's `scene N.N, <TITLE> -- ...`
the title alone); --label is how the index names a scene. The joined file and the index are written next to the
takes. Stream copy: every take comes out of the same driver (h264 + aac), so nothing is re-encoded.

Built for THE WORLD AT THE END OF TIME's chapter 2 (10-23), which it joins as:

    python join_chapter.py <production>/chapter2 3 31 --take "wateot_2_{n}_rough.mp4" --script "wateot_2_{n}.txt" \\
        --label "2.{n}" --out wateot_ch2_rough.mp4 --index wateot_ch2_index.txt
"""
import argparse, glob, os, re, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
FF = (glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages", "Gyan.FFmpeg*",
                             "ffmpeg*", "bin", "ffmpeg.exe")) or ["ffmpeg"])[0]
FP = FF.replace("ffmpeg.exe", "ffprobe.exe") if FF.endswith(".exe") else "ffprobe"


def title_of(script):
    if not os.path.exists(script): return ""
    first = open(script, encoding="utf-8").readline().strip()
    m = re.search(r"scene [0-9.]+, (.*?) -- ", first)
    return m.group(1) if m else first.lstrip("#").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir"); ap.add_argument("first", type=int); ap.add_argument("last", type=int)
    ap.add_argument("--take", default="scene_{n}.mp4")
    ap.add_argument("--script", default="scene_{n}.txt")
    ap.add_argument("--label", default="{n}")
    ap.add_argument("--out", default="joined.mp4")
    ap.add_argument("--index", default=None, help="default: <out>_index.txt")
    a = ap.parse_args()
    out = os.path.join(a.dir, a.out)
    lst = os.path.join(a.dir, "_concat.txt")
    idx, t = [], 0.0
    with open(lst, "w", encoding="utf-8") as f:
        for n in range(a.first, a.last + 1):
            p = os.path.join(a.dir, a.take.format(n=n))
            assert os.path.exists(p), "missing take: " + p
            f.write("file '%s'\n" % p.replace("\\", "/"))
            d = float(subprocess.run([FP, "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", p],
                                     capture_output=True, text=True).stdout.strip())
            idx.append("%2d:%02d  %-4s  %s" % (int(t // 60), int(t % 60), a.label.format(n=n),
                                               title_of(os.path.join(HERE, a.script.format(n=n)))))
            t += d
    subprocess.run([FF, "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-c", "copy", out], check=True)
    os.remove(lst)
    ix = os.path.join(a.dir, a.index or os.path.splitext(a.out)[0] + "_index.txt")
    open(ix, "w", encoding="utf-8").write("\n".join(idx) + "\n")
    print("\n".join(idx))
    print("total %d:%02d -> %s (index %s)" % (int(t // 60), int(t % 60), out, os.path.basename(ix)))


if __name__ == "__main__":
    main()
