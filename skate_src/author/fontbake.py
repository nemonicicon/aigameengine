#!/usr/bin/env python3
"""fontbake.py -- the wide glyphs a language needs, baked from a font into the file the TV's text reads (queue step 26).

    python skate_src/author/fontbake.py OUT.tvw SRC [SRC ...] [--ranges 3040-30FF,4E00-9FFF] [--text FILE] [--index N] [--sheet OUT.png]
    python skate_src/author/fontbake.py --selftest       both bitmap readers against glyphs written by hand

The TV font is the engine's own and is all that ships with it. It has Latin with its accents, Greek and Cyrillic. Chinese, Japanese and Korean are thousands of glyphs
nobody here drew, so they come from a font the game's maker has the right to use, baked once into data/<name>.tvw: 16 x 16 dots a glyph, which the engine draws in one
cell at the large reading size. A language's file names it (`wide <name>`, engine/lang.inc).

SRC can be:
  a .hex file        GNU Unifont's own format, `4E00:` and 64 hex digits (or 32, for a half-width glyph, which is centred)
  a .bdf file        a bitmap font, 16 dots tall or less
  a .ttf .otf .ttc   an outline font, drawn at 16 dots with no smoothing (--index picks a face of a .ttc). One with bitmaps of its own at that size comes out cleanest
Sources are tried in the order given, and the first that has a glyph gives it.

Which glyphs: --ranges (hex code points, a-b, comma between), or --text FILE for exactly the characters a UTF-8 file uses, which is what a game with one script of
dialogue wants. With neither: kana, the CJK ideographs, Hangul, their punctuation and the full-width forms. What the TV font already has is never taken from a file.

⛔ THE FILE CARRIES THE FONT'S GLYPHS, SO THE FONT'S LICENCE GOES WITH IT. GNU Unifont (SIL OFL 1.1) may ship with a game, with its notice. A font that came with an
operating system may not: bake from one to look at a page on your own machine, and never commit or ship the result. data/*.tvw is ignored by git for that reason.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
DEFAULT = "3000-303F,3040-30FF,31F0-31FF,4E00-9FFF,AC00-D7A3,FF00-FFEF"


def tv_has():
    """what the TV font draws itself"""
    import tvfont_more
    return set(range(32, 128)) | set(tvfont_more.DRAWN) | set(tvfont_more.SAME) | set(tvfont_more.COMPOSED) | set(tvfont_more.FLIPPED)


def rows_of_hex(h):
    if len(h) == 64:
        return [int(h[i * 4:i * 4 + 4], 16) for i in range(16)]
    if len(h) == 32:
        return [int(h[i * 2:i * 2 + 2], 16) << 4 for i in range(16)]          # half-width: in the middle of the cell
    return None


class Hex:
    def __init__(self, path):
        self.g = {}
        for ln in open(path, encoding="ascii", errors="replace"):
            m = re.match(r"^([0-9A-Fa-f]{4,6}):([0-9A-Fa-f]+)\s*$", ln)
            if m:
                r = rows_of_hex(m.group(2))
                if r:
                    self.g[int(m.group(1), 16)] = r

    def get(self, cp):
        return self.g.get(cp)


class Bdf:
    def __init__(self, path, text=None):
        self.g = {}
        cp, bbx, rows, on, asc, adv = None, None, [], False, 14, 16
        for ln in (text.splitlines() if text is not None else open(path, encoding="latin-1")):
            t = ln.split()
            if not t:
                continue
            if t[0] == "FONT_ASCENT":
                asc = int(t[1])
            elif t[0] == "ENCODING":
                cp = int(t[1])
            elif t[0] == "DWIDTH":
                adv = int(t[1])
            elif t[0] == "BBX":
                bbx = [int(v) for v in t[1:5]]
            elif t[0] == "BITMAP":
                on, rows = True, []
            elif t[0] == "ENDCHAR":
                if cp is not None and cp >= 0 and bbx and bbx[1] <= 16 and bbx[0] <= 16:
                    w, h, xo, yo = bbx
                    out = [0] * 16
                    top = max(0, min(16 - h, asc - (h + yo)))                       # the box's top row, counted down from the font's ascent
                    left = max(0, min(16 - w, xo + (4 if adv <= 8 else 0)))         # a half-width glyph sits in the middle of the cell
                    for y, (v, nb) in enumerate(rows[:h]):                          # a row is hex padded to whole bytes, its left dot the top bit
                        out[top + y] = ((v << 16 >> nb) >> left) & 0xFFFF
                    self.g[cp] = out
                on, cp, bbx, adv = False, None, None, 16
            elif on:
                rows.append((int(t[0], 16), len(t[0]) * 4))

    def get(self, cp):
        return self.g.get(cp)


class Outline:
    def __init__(self, path, index=0):
        from PIL import ImageFont
        self.font = ImageFont.truetype(path, 16, index=index)
        try:
            from fontTools.ttLib import TTFont
            self.has = set(TTFont(path, fontNumber=index, lazy=True).getBestCmap().keys())
        except Exception:
            self.has = None
        self.blank = self._rows("￿")

    def _rows(self, ch):
        from PIL import Image, ImageDraw
        im = Image.new("1", (16, 16), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        l, t, r, b = self.font.getbbox(ch)
        d.text(((16 - (l + r)) // 2 if r - l <= 16 else -l, 0), ch, font=self.font, fill=1)
        px = im.load()
        return [sum((1 << (15 - x)) for x in range(16) if px[x, y]) for y in range(16)]

    def get(self, cp):
        if self.has is not None and cp not in self.has:
            return None
        r = self._rows(chr(cp))
        return None if (not any(r) or (self.has is None and r == self.blank)) else r


def open_src(path, index):
    e = os.path.splitext(path)[1].lower()
    return Hex(path) if e == ".hex" else Bdf(path) if e == ".bdf" else Outline(path, index)


def selftest():
    """both bitmap readers against glyphs written here by hand, so they're known right with no font on the machine"""
    import tempfile
    want = [0x8001 if y in (0, 15) else 0x4002 if y in (1, 14) else 0x0180 for y in range(16)]
    hx = "4E00:" + "".join("%04X" % r for r in want) + "\n0041:" + "18" * 16 + "\n"
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "t.hex")
        open(p, "w").write(hx)
        h = Hex(p)
    assert h.get(0x4E00) == want, "a full-width .hex glyph"
    assert h.get(0x41) == [0x0180] * 16, "a half-width .hex glyph goes in the middle"
    bdf = ("STARTFONT 2.1\nFONT_ASCENT 14\nFONT_DESCENT 2\nSTARTCHAR a\nENCODING 19968\nDWIDTH 16 0\nBBX 16 16 0 -2\nBITMAP\n" + "\n".join("%04X" % r for r in want) + "\nENDCHAR\n"
           "STARTCHAR b\nENCODING 12354\nDWIDTH 16 0\nBBX 3 2 5 4\nBITMAP\nE0\nA0\nENDCHAR\nENDFONT\n")
    b = Bdf(None, bdf)
    assert b.get(0x4E00) == want, "a .bdf glyph that fills its cell"
    small = b.get(0x3042)
    assert small[8] == 0b0000011100000000 and small[9] == 0b0000010100000000 and sum(small) == small[8] + small[9], "a small .bdf glyph is where its box says: %s" % ["%04x" % r for r in small]
    print("fontbake: both bitmap readers are right (a full cell, a half-width glyph in the middle, a small box placed by its offsets)")


def main(argv):
    if "--selftest" in argv:
        return selftest()
    if len(argv) < 3:
        sys.exit(__doc__)
    out = argv[1]

    def opt(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default
    skip = {i + 1 for i, a in enumerate(argv) if a in ("--ranges", "--text", "--index", "--sheet")}
    srcs = [a for i, a in enumerate(argv[2:], 2) if not a.startswith("--") and i not in skip]
    index = int(opt("--index", "0"))
    own = tv_has()
    if opt("--text"):
        want = sorted({ord(c) for c in open(opt("--text"), encoding="utf-8").read() if ord(c) > 32 and ord(c) < 0x10000} - own)
    else:
        want = []
        for part in opt("--ranges", DEFAULT).split(","):
            a, _, b = part.partition("-")
            want += [c for c in range(int(a, 16), int(b or a, 16) + 1) if c not in own]
        want = sorted(set(want))
    fonts = [open_src(s, index) for s in srcs]
    got = {}
    for cp in want:
        for f in fonts:
            r = f.get(cp)
            if r:
                got[cp] = r
                break
    cps = sorted(got)
    assert cps and len(cps) < 65536, "%d glyphs" % len(cps)
    with open(out, "wb") as f:
        f.write(b"TVW1" + struct.pack("<IHH", len(cps), 16, 0))
        f.write(struct.pack("<%dH" % len(cps), *cps))
        for cp in cps:
            f.write(struct.pack(">16H", *got[cp]))
    print("%s: %d glyphs of the %d asked for, from %s (%d bytes)" % (os.path.normpath(out), len(cps), len(want), ", ".join(os.path.basename(s) for s in srcs), os.path.getsize(out)))
    if opt("--sheet"):
        from PIL import Image
        show = cps[::max(1, len(cps) // 512)][:512]
        im = Image.new("L", (32 * 18 + 2, ((len(show) + 31) // 32) * 18 + 2), 30)
        px = im.load()
        for i, cp in enumerate(show):
            for y in range(16):
                for x in range(16):
                    if got[cp][y] >> (15 - x) & 1:
                        px[2 + (i % 32) * 18 + x, 2 + (i // 32) * 18 + y] = 240
        im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(opt("--sheet"))
        print("sheet: " + opt("--sheet"))


if __name__ == "__main__":
    main(sys.argv)
