"""patch_subtitle.py -- the subtitles were rendering garbage.

jacob: "some of the subtitles are messed up."

MEASURED: 21 EM DASHES (U+2014) across the seven scripts. The engine's caption font is a
bitmap ASCII set, so every one of them drew as junk -- and they are on exactly the lines
that needed to land:

    "He's caught. There's no weight on it, he's slipped his ##### "
    "That's a squad car. That's #####Hey, that's help, that's #####"

The em dashes stay in the SCRIPT, because say_piper wants them -- a dash is a real prosodic
break and Piper reads it as one. They are folded to ASCII only on the way into the `.scn`,
which is the text the engine actually draws. That way the writing keeps its punctuation and
the screen keeps its alphabet, and any future script is safe without anybody remembering
this.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes\compile_scene.py"
s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)

a = """    lines = []
    cast_names = {"0": "REVA", "1": "KAELI"}"""
b = '''    lines = []
    cast_names = {"0": "REVA", "1": "KAELI"}
    # ⛔ THE CAPTION FONT IS BITMAP ASCII. Anything above 0x7E draws as junk, and the
    # scripts are full of typographic punctuation because it reads better and because
    # Piper treats a dash as a real prosodic break. Fold on the way into the .scn only:
    # the script keeps its punctuation, the screen keeps its alphabet.
    SCREEN = {
        0x2014: " - ", 0x2013: "-", 0x2018: "'", 0x2019: "'",
        0x201C: '"', 0x201D: '"', 0x2026: "...", 0x00A0: " ",
    }

    def screen_text(t):
        t = t.translate(SCREEN)
        t = "".join(c if 32 <= ord(c) <= 126 else "?" for c in t)
        while "  " in t:
            t = t.replace("  ", " ")
        return t.strip()
'''
if s.count(a) != 1:
    print("!! cast_names anchor x%d" % s.count(a))
    sys.exit(1)
s = s.replace(a, b)
print("   ok: the folding table")

# find where the line text is written into the .scn and fold it there
import re
m = re.search(r'^.*f\.write\(.*"line .*$', s, re.M)
if not m:
    print("!! could not find the line writer; showing candidates:")
    for ln in s.splitlines():
        if "line " in ln and "write" in ln:
            print("   " + ln.strip())
    sys.exit(1)
print("   line writer: " + m.group(0).strip()[:110])
io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("compile_scene.py: folding table added (writer patched separately)")
