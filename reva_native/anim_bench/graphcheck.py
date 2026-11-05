"""graphcheck.py -- does the animation graph work on every body? Builds graphcheck.c as a game with no display and runs it from games/tower: states by speed, by a flag that's used up, by a value it was given and by a clip ending, an event landing once for the right body, nothing while hidden, starting again from the pool, being taken off, and a file read again when it changes. Then the receipt: the same run twice, and at odd tick patterns, prints the same events on the same ticks and the same [simhash] lines, and one more punch parts them from the tick it lands.

usage: graphcheck.py [--exe PATH]   Run from anywhere."""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.stdout.reconfigure(encoding="utf-8", errors="replace")
CWD = os.path.join(ROOT, "games", "tower")


def build(out):
    if os.name == "nt":
        env = dict(os.environ, PATH=r"C:\msys64\mingw64\bin;" + os.environ["PATH"])
        cmd = [r"C:\msys64\mingw64\bin\gcc.exe", "-O2", "-std=gnu17", "-ffp-contract=off", "-DREVA_HEADLESS", "-DREVA_NODOORS", "-o", out, "crowd_kms.c", "club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "club_bridge.c", "club_net.c", "crowd_win_rc.o", "anim_bench/graphcheck.c", "gfx_packet.c", "-Dmain=SDL_main", "-lws2_32", "-lmingw32", "-lSDL2main", "-lSDL2", "-lm"]
    else:
        env = dict(os.environ, GAME="anim_bench/graphcheck.c", EXTRA="-DREVA_NODOORS", OUTDIR=os.path.dirname(out))
        cmd = ["sh", "build_pi.sh", "headless"]
    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.returncode:
        sys.exit("it didn't build:\n" + "\n".join(l for l in r.stdout.splitlines() if "error" in l)[:2000])


def run(exe, **env_extra):
    env = dict(os.environ, REVA_MODE="9", REVA_TIER="pi", REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60", REVA_EXITTICK="400", **env_extra)
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    p = subprocess.run([exe, "12", "packs", "packs/v1_tower.rpack"], cwd=CWD, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=120)
    lines = p.stdout.splitlines()
    return p, [l for l in lines if l.startswith("[graphcheck]")], [l for l in lines if l.startswith("[simhash]") and "(end)" not in l], lines


ap = argparse.ArgumentParser()
ap.add_argument("--exe")
a = ap.parse_args()
exe = a.exe or os.path.join(ROOT, "crowd_graphcheck.exe" if os.name == "nt" else "crowd_graphcheck")
if not a.exe:
    build(exe)
bad = 0
p, said, hashes, lines = run(exe)
events = [l for l in said if " event " in l]
summary = [l for l in said if "checks," in l]
failed = [l for l in said if "FAILED" in l]
print("bodies: " + (summary[0][13:] if summary else "NO RESULT, exit %s\n%s" % (p.returncode, "\n".join(lines[-6:]))) + ", %d events heard" % len(events))
for l in failed:
    print("  " + l[13:])
if not summary or failed or p.returncode or not hashes or not events:
    bad += 1
again = run(exe)
same = again[1] == said and again[2] == hashes
print("  the same run again: " + ("the same" if same else "DIFFERS"))
bad += not same
for c in ("1,1,1,1,2", "2,2,3"):
    odd = run(exe, REVA_CADENCE=c)
    same = [l for l in odd[1] if " event " in l] == events
    sim = odd[2] == hashes
    print("  at tick pattern %s: events %s, rules %s" % (c, "on the same ticks" if same else "DIFFER", "the same" if sim else "DIFFER"))
    bad += (not same) + (not sim)
more = run(exe, GRAPHCHECK_EXTRA="1")[2]
part = next((i for i, (x, y) in enumerate(zip(hashes, more)) if x != y), None)
tick = hashes[part].split()[2] if part is not None else None
ok = part is not None and 240 <= int(tick) <= 300
print("  one more punch at tick 240: " + ("parts at tick %s" % tick if ok else "WRONG: %s" % tick))
bad += not ok
print("all good" if not bad else "%d WRONG" % bad)
sys.exit(1 if bad else 0)
