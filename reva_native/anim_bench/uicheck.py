"""uicheck.py -- do the menus behave? Builds uicheck.c as a game with no display, writes a tape of what a player's pads do, and runs it: each kind of row, the stick's repeat, the keyboard, tabs, a long menu, and a second player's. Then the receipt: the same at odd tick patterns and at every reading size, and one more press of a button parts it from that tick.

usage: uicheck.py [--exe PATH]   Run from anywhere."""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
CWD = os.path.join(ROOT, "games", "sample")
TICKS = 800
# what's pressed on which tick of the game's, for one tick each. uicheck.c has the other half of this list
PAD0 = [(10, "A"), (20, "up"), (30, "down"), (40, "down"), (50, "A"), (60, "right"), (70, "down"), (80, "right"),
        (230, "down"), (240, "right"), (250, "right"), (260, "right"), (270, "left"), (280, "A"), (290, "down"),
        (300, "A"), (310, "A"), (320, "right"), (330, "A"), (340, "down"), (350, "A"), (360, "B"), (370, "down"), (380, "down"), (390, "down"), (400, "A"), (410, "left"), (420, "left"), (430, "A"),
        (440, "A"), (442, "A"), (444, "A"), (446, "A"), (448, "A"), (450, "A"), (460, "St"),
        (480, "down"), (490, "A"), (500, "B"), (510, "RB"), (630, "A"), (640, "LB"), (670, "down")]
HELD0 = [(90, 40, "right"), (140, 85, "left"), (520, 100, "down")]
PAD1 = [(660, "down"), (680, "A")]


def bits(f):
    return "%08x" % struct.unpack("<I", struct.pack("<f", f))[0]


def tape(path, first, extra=None):
    """The tape's stick is the floor's own: x is left-positive, and y is down-positive. first: the tape line the game's first tick reads"""
    p0, p1 = {}, {}
    for t, what in PAD0 + ([(extra, "A")] if extra else []):
        p0.setdefault(t, set()).add(what)
    for t, n, what in HELD0:
        for k in range(t, t + n):
            p0.setdefault(k, set()).add(what)
    for t, what in PAD1:
        p1.setdefault(t, set()).add(what)
    z = bits(0.0)
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v3  seed 12345  written by uicheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv, then pads 1 to 3: on lx ly rx ry rt held; floats as bits)\n")
        for line in range(1, TICKS + 2):
            t = line - first + 1
            a, b = p0.get(t, set()), p1.get(t, set())
            lx = 1.0 if "left" in a else -1.0 if "right" in a else 0.0
            ly = -1.0 if "up" in a else 1.0 if "down" in a else 0.0
            s = "%d %s %s %s %s %s %d %d 0 0 %d %d %d 0 0 %s %s 1 1 0" % (line, bits(lx), bits(ly), z, z, z, "A" in a, "B" in a, "LB" in a, "RB" in a, "St" in a, z, z)
            s += " 1 %s %s %s %s %s %x" % (z, bits(1.0 if "down" in b else 0.0), z, z, z, 1 if "A" in b else 0)
            s += " 0 %s %s %s %s %s 0 0 %s %s %s %s %s 0\n" % (z, z, z, z, z, z, z, z, z, z)
            f.write(s)


def build(out):
    if os.name == "nt":
        env = dict(os.environ, PATH=r"C:\msys64\mingw64\bin;" + os.environ["PATH"])
        cmd = [r"C:\msys64\mingw64\bin\gcc.exe", "-O2", "-std=gnu17", "-ffp-contract=off", "-DREVA_HEADLESS", "-DREVA_NODOORS", "-o", out, "crowd_kms.c", "club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "club_bridge.c", "club_net.c", "crowd_win_rc.o", "anim_bench/uicheck.c", "gfx_packet.c", "-Dmain=SDL_main", "-lws2_32", "-lmingw32", "-lSDL2main", "-lSDL2", "-lm"]
    else:
        env = dict(os.environ, GAME="anim_bench/uicheck.c", EXTRA="-DREVA_NODOORS", OUTDIR=os.path.dirname(out))
        cmd = ["sh", "build_pi.sh", "headless"]
    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.returncode:
        sys.exit("it didn't build:\n" + "\n".join(l for l in r.stdout.splitlines() if "error" in l)[:2000])


def run(exe, tp, **env_extra):
    env = dict(os.environ, REVA_MODE="9", REVA_TIER="pi", REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60", REVA_EXITTICK=str(TICKS), REVA_REPLAY=tp, **env_extra)
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    p = subprocess.run([exe, "12", "packs", "packs/v1_sample.rpack"], cwd=CWD, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=120)
    lines = p.stdout.splitlines()
    return p, [l for l in lines if l.startswith("[uicheck]")], [l for l in lines if l.startswith("[simhash]") and "(end)" not in l], [l for l in lines if l.startswith("[packet]")], lines


ap = argparse.ArgumentParser()
ap.add_argument("--exe")
ap.add_argument("--first", type=int, default=int(os.environ.get("UICHECK_FIRST", "1")))
a = ap.parse_args()
exe = a.exe or os.path.join(ROOT, "crowd_uicheck.exe" if os.name == "nt" else "crowd_uicheck")
if not a.exe:
    build(exe)
tp = os.path.join(CWD, "_uicheck.tape")
tp2 = os.path.join(CWD, "_uicheck2.tape")
tape(tp, a.first)
tape(tp2, a.first, extra=700)
bad = 0
p, said, hashes, packets, lines = run(exe, tp)
summary = [l for l in said if "checks," in l]
failed = [l for l in said if "FAILED" in l]
print((summary[0][10:] if summary else "NO RESULT, exit %s\n%s" % (p.returncode, "\n".join(lines[-6:]))))
for l in failed:
    print("  " + l[10:])
if not summary or failed or p.returncode or not hashes:
    bad += 1
again = run(exe, tp)
same = again[1] == said and again[2] == hashes and again[3] == packets
print("the same run again: " + ("the same, picture too" if same else "DIFFERS"))
bad += not same
for c in ("1,1,1,1,2", "2,2,3"):
    odd = run(exe, tp, REVA_CADENCE=c)
    ok1 = odd[1] == said
    ok2 = odd[2] == hashes
    got = {l.split()[2]: l for l in odd[3]}
    ok3 = all(l == got[l.split()[2]] for l in packets if l.split()[2] in got)
    print("at tick pattern %s: checks %s, rules %s, picture %s" % (c, "the same" if ok1 else "DIFFER", "the same" if ok2 else "DIFFER", "the same" if ok3 else "DIFFERS"))
    bad += (not ok1) + (not ok2) + (not ok3)
for read in ("medium", "large"):
    r = run(exe, tp, REVA_READING=read)
    ok1 = r[1] == said
    ok2 = r[2] == hashes
    ok3 = r[3] != packets
    print("at reading size %s: checks %s, rules %s, picture %s" % (read, "the same" if ok1 else "DIFFER", "the same" if ok2 else "DIFFER", "parts, as it should" if ok3 else "THE SAME"))
    bad += (not ok1) + (not ok2) + (not ok3)
more = run(exe, tp2)[2]
part = next((l.split()[2] for x, l in zip(hashes, more) if x != l), None)
ok = part is not None and 700 <= int(part) <= 780
print("one more press at tick 700: " + ("parts at tick %s" % part if ok else "WRONG: %s" % part))
bad += not ok
for f in (tp, tp2):
    if os.path.exists(f):
        os.remove(f)
print("all good" if not bad else "%d WRONG" % bad)
sys.exit(1 if bad else 0)
