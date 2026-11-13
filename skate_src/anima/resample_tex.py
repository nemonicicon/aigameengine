"""resample_tex.py -- bring a recovered UE texture set down to club size, power-of-two.

WHY: Anima's maps come out of UE at their authoring size -- her skin is four 4096x4096
sheets and her armstraps/boots/sleeves are 4000x4000. Exported straight into a GLB that is
a 907 MB file, and every one of those sheets is 64 MB of RGBA once the engine uploads it.

TWO SEPARATE REASONS TO RESAMPLE, and the second is the one that is easy to miss:
  1. SIZE. A club body is looked at from a metre away in a close-up and from across a
     street the rest of the time. 4096 buys nothing at either distance.
  2. POWER OF TWO. 4000x4000 is NOT a power of two, and the engine's mipmap generation is
     POT-guarded (GLES2 only guarantees mipmaps and repeat wrapping for POT). So her boots
     and sleeves would have shipped WITHOUT mipmaps and shimmered at distance, while her
     4096 skin got them -- an inconsistency invisible in any still.

The FACE keeps more resolution than the rest: it is the only surface a close-up lands on.
"""
import os
import sys

from PIL import Image

# the face is what a close-up frames; everything else is seen at conversational distance
BIG = ("head", "body", "arm", "leg")


def pot(n, cap):
    """largest power of two <= min(n, cap)"""
    n = min(n, cap)
    p = 1
    while p * 2 <= n:
        p *= 2
    return p


def main():
    src, dst = sys.argv[1], sys.argv[2]
    cap_big = int(sys.argv[3]) if len(sys.argv) > 3 else 2048
    cap_small = int(sys.argv[4]) if len(sys.argv) > 4 else 1024
    os.makedirs(dst, exist_ok=True)
    tot_in = tot_out = 0
    for fn in sorted(os.listdir(src)):
        if not fn.lower().endswith(".png"):
            continue
        p = os.path.join(src, fn)
        tot_in += os.path.getsize(p)
        im = Image.open(p)
        low = fn.lower()
        cap = cap_big if any(("_" + b + "_") in low for b in BIG) else cap_small
        w, h = pot(im.size[0], cap), pot(im.size[1], cap)
        if (w, h) != im.size:
            im = im.resize((w, h), Image.LANCZOS)
        out = os.path.join(dst, fn)
        im.save(out, optimize=True)
        tot_out += os.path.getsize(out)
        print("  %-42s %5dx%-5d -> %4dx%-4d" % (fn, *Image.open(p).size, w, h))
    print("%.1f MB -> %.1f MB" % (tot_in / 1e6, tot_out / 1e6))


main()
