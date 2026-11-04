"""physcheck.py -- does the physics a game can reach behave? Builds physcheck.c as a game with no display and runs it from games/tower, on the set that says `climb` and on no set: loose things falling, landing flat, sleeping and waking, meeting each other, walls and bodies, a stack coming down, and bodies going limp. Then the receipt: the same run twice, and at odd tick patterns, hears the same hits on the same ticks and prints the same [simhash] lines, and one more loose thing parts them from when it appears. A limp body changes the picture and not the rules, and the ragdoll's own log says how it fell: its feet stay near the floor as it goes down, and it doesn't crawl once it's there.

usage: physcheck.py [--exe PATH]   Run from anywhere. Needs games/tower's props: games/tower/make_assets.py makes them."""
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
        cmd = [r"C:\msys64\mingw64\bin\gcc.exe", "-O2", "-std=gnu17", "-ffp-contract=off", "-DREVA_HEADLESS", "-DKHRONOS_STATIC", "-DREVA_NODOORS", "-o", out, "crowd_kms.c", "club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "club_bridge.c", "crowd_win_rc.o", "anim_bench/physcheck.c", "gl_null.c", "gfx_packet.c", "-Dmain=SDL_main", "-lws2_32", "-lmingw32", "-lSDL2main", "-lSDL2", "-lm"]
    else:
        env = dict(os.environ, GAME="anim_bench/physcheck.c", EXTRA="-DREVA_NODOORS", OUTDIR=os.path.dirname(out))
        cmd = ["sh", "build_pi.sh", "headless"]
    r = subprocess.run(cmd, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    if r.returncode:
        sys.exit("it didn't build:\n" + "\n".join(l for l in r.stdout.splitlines() if "error" in l)[:2000])


def run(exe, street, **env_extra):
    env = dict(os.environ, REVA_MODE="9", REVA_TIER="pi", REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60", REVA_EXITTICK="700", PHYSCHECK_STREET=street, **env_extra)
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    p = subprocess.run([exe, "24", "packs", "packs/v1_tower.rpack"], cwd=CWD, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", timeout=120)
    lines = p.stdout.splitlines()
    return p, [l for l in lines if l.startswith("[physcheck]")], [l for l in lines if l.startswith("[simhash]") and "(end)" not in l], [l for l in lines if l.startswith("[packet]")], lines


ap = argparse.ArgumentParser()
ap.add_argument("--exe")
a = ap.parse_args()
exe = a.exe or os.path.join(ROOT, "crowd_physcheck.exe" if os.name == "nt" else "crowd_physcheck")
if not a.exe:
    build(exe)
bad = 0
for street, name in (("tower", "the set that says climb"), ("none", "no set")):
    p, said, hashes, packets, lines = run(exe, street)
    hits = [l for l in said if " hit tick " in l]
    summary = [l for l in said if "checks," in l]
    failed = [l for l in said if "FAILED" in l]
    print(name + ": " + (summary[0][12:] if summary else "NO RESULT, exit %s\n%s" % (p.returncode, "\n".join(lines[-6:]))) + ", %d hits heard" % len(hits))
    for l in failed:
        print("  " + l[12:])
    if not summary or failed or p.returncode or not hashes or not hits:
        bad += 1
    again = run(exe, street)
    same = again[1] == said and again[2] == hashes and again[3] == packets
    print("  the same run again: " + ("the same, picture too" if same else "DIFFERS"))
    bad += not same
    for c in ("1,1,1,1,2", "2,2,3"):
        odd = run(exe, street, REVA_CADENCE=c)
        ok1 = [l for l in odd[1] if " hit tick " in l] == hits
        ok2 = odd[2] == hashes
        got = {l.split()[2]: l for l in odd[3]}
        ok3 = all(l == got[l.split()[2]] for l in packets if l.split()[2] in got)
        print("  at tick pattern %s: hits %s, rules %s, picture %s" % (c, "on the same ticks" if ok1 else "DIFFER", "the same" if ok2 else "DIFFER", "the same" if ok3 else "DIFFERS"))
        bad += (not ok1) + (not ok2) + (not ok3)
    more = run(exe, street, PHYSCHECK_EXTRA="1")[2]
    part = next((i for i, (x, y) in enumerate(zip(hashes, more)) if x != y), None)
    tick = hashes[part].split()[2] if part is not None else None
    ok = part is not None and 240 <= int(tick) <= 300
    print("  one more at tick 240: " + ("parts at tick %s" % tick if ok else "WRONG: %s" % tick))
    bad += not ok
    nolimp = run(exe, street, PHYSCHECK_NOLIMP="1")
    same = nolimp[2] == hashes
    part = next((l.split()[2] for l, m in zip(packets, nolimp[3]) if l != m), None)
    ok = same and part is not None and 140 <= int(part) <= 240
    print("  a limp body: " + ("the rules the same, and the picture parts at tick %s" % part if ok else "WRONG: rules %s, picture parts at %s" % ("the same" if same else "differ", part)))
    bad += not ok
    falls, cur = [], None
    for l in run(exe, street, REVA_RDDBG="1")[4]:
        if "LIMP at" in l:
            cur = []
            falls.append(cur)
        elif l.startswith("[rd] t=") and cur is not None:
            f = dict(x.split("=", 1) for x in l.split()[1:] if "=" in x)
            hx, hy, hz = f["hips"].strip("()").split(",")
            cur.append((float(f["t"]), float(hx), float(hz), float(f["lowest"]) - float(f["floor"])))
    for i, fall in enumerate(falls):
        if not fall:
            continue
        kick = max(r[3] for r in fall)
        late = [r for r in fall if r[0] >= 1.0]
        crawl = max((((r[1] - late[-1][1]) ** 2 + (r[2] - late[-1][2]) ** 2) ** 0.5 for r in late), default=0.0)
        ok = kick < 0.3 and crawl < 0.5
        print("  a fall: its feet are up to %.2f off the floor, and it moves %.2f units after the first second%s" % (kick, crawl, "" if ok else ": WRONG"))
        bad += not ok
print("all good" if not bad else "%d WRONG" % bad)
sys.exit(1 if bad else 0)
