"""objcheck.py -- do things a game makes and takes away as it runs behave? Builds objcheck.c as a game with no display and runs it from games/tower: handles that are never given out twice, the pool running out and coming back, what's solid on a set that says `climb` and on no set, and bodies from the pool. Then the receipt: the same run twice prints the same [simhash] lines at odd tick patterns too, and one more object parts them from the tick it appears.

usage: objcheck.py [--exe PATH]   Run from anywhere. Needs games/tower's props: games/tower/make_assets.py makes them."""
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
        gcc = r"C:\msys64\mingw64\bin\gcc.exe"
        env = dict(os.environ, PATH=r"C:\msys64\mingw64\bin;" + os.environ["PATH"])
        cmd = [gcc, "-O2", "-std=gnu17", "-ffp-contract=off", "-DREVA_HEADLESS", "-DREVA_NODOORS", "-o", out, "crowd_kms.c", "club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "club_bridge.c", "club_net.c", "crowd_win_rc.o", "anim_bench/objcheck.c", "gfx_packet.c", "-Dmain=SDL_main", "-lws2_32", "-lmingw32", "-lSDL2main", "-lSDL2", "-lm"]
    else:
        env = dict(os.environ, GAME="anim_bench/objcheck.c", EXTRA="-DREVA_NODOORS", OUTDIR=os.path.dirname(out))
        cmd = ["sh", "build_pi.sh", "headless"]
    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.returncode:
        sys.exit("it didn't build:\n" + "\n".join(l for l in r.stdout.splitlines() if "error" in l)[:2000])


def run(exe, street, ticks=400, **env_extra):
    env = dict(os.environ, REVA_MODE="9", REVA_TIER="pi", REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60", REVA_EXITTICK=str(ticks), OBJCHECK_STREET=street, **env_extra)
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    p = subprocess.run([exe, "12", "packs", "packs/v1_tower.rpack"], cwd=CWD, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=120)
    lines = p.stdout.splitlines()
    return p, [l for l in lines if l.startswith("[objcheck]")], [l for l in lines if l.startswith("[simhash]") and "(end)" not in l], lines


ap = argparse.ArgumentParser()
ap.add_argument("--exe")
a = ap.parse_args()
exe = a.exe or os.path.join(ROOT, "crowd_objcheck.exe" if os.name == "nt" else "crowd_objcheck")
if not a.exe:
    build(exe)
bad = 0
for street, name in (("tower", "the set that says climb"), ("none", "no set")):
    p, said, hashes, lines = run(exe, street)
    summary = [l for l in said if "checks," in l]
    failed = [l for l in said if "FAILED" in l]
    print(name + ": " + (summary[0][11:] if summary else "NO RESULT, exit %s\n%s" % (p.returncode, "\n".join(lines[-6:]))))
    for l in failed:
        print("  " + l[11:])
    if not summary or failed or p.returncode:
        bad += 1
    if p.returncode == 0 and not hashes:
        print("  no [simhash] lines")
        bad += 1
    again = run(exe, street)[2]
    same = again == hashes
    print("  the same run again: " + ("the same" if same else "DIFFERS"))
    bad += not same
    odd = run(exe, street, REVA_CADENCE="1,1,1,1,2")[2]
    same = odd == hashes
    print("  at an odd tick pattern: " + ("the same" if same else "DIFFERS"))
    bad += not same
    more = run(exe, street, OBJCHECK_EXTRA="1")[2]
    part = next((i for i, (x, y) in enumerate(zip(hashes, more)) if x != y), None)
    tick = hashes[part].split()[2] if part is not None else None
    ok = part is not None and int(tick) >= 200 and int(tick) <= 260
    print("  one more object at tick 200: " + ("parts at tick %s" % tick if ok else "WRONG: %s" % tick))
    bad += not ok
print("all good" if not bad else "%d WRONG" % bad)
sys.exit(1 if bad else 0)
