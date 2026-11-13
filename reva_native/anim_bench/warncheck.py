"""warncheck.py -- the engine has no compiler warnings, in any way it's built, and this keeps it that way. A warning is the compiler saying a line looks like a mistake, and one among none gets read. Any warning fails, and it says where. They were cleared on 11-04, and the ones that were real are in engine_plan.md.

usage: warncheck.py    Run from anywhere. A minute or two. Uses -O1 because some warnings need the optimizer to see them. CC= names another compiler, and off Windows it does the Pi's builds:
       docker exec -e CC=aarch64-linux-gnu-gcc revalinux python3 /src/anim_bench/warncheck.py"""
import glob
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GCC = os.environ.get("CC") or (r"C:\msys64\mingw64\bin\gcc.exe" if os.name == "nt" else "gcc")
CORE = ["club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "club_bridge.c", "club_net.c", "gfx_packet.c"]
GAMES = sorted(os.path.relpath(p, ROOT).replace(os.sep, "/") for p in glob.glob(os.path.join(ROOT, "games", "*", "*.c")))   # every game's file
BASE = ["-O1", "-std=gnu17", "-ffp-contract=off", "-Wall", "-Wextra", "-fno-diagnostics-show-caret"]
if os.name == "nt":
    BASE.append("-Dmain=SDL_main")
BUILDS = [("no display", ["-DREVA_HEADLESS", "-I."] + ([] if os.name == "nt" else ["-DREVA_SDL"]), ["crowd_kms.c"] + CORE + GAMES),   # -I.: a game anywhere includes "engine.h" by the include path (11-06)
          ("as the kit gets it", ["-DREVA_HEADLESS", "-DREVA_NODOORS"] + ([] if os.name == "nt" else ["-DREVA_SDL"]), ["crowd_kms.c"]),
          ("the renderer", ["-DREVA_VK", "-I."], ["crowd_kms.c", "gfx_vk.c"]),
          ("the engine's own rasterizer", ["-DREVA_SOFT", "-I."] + ([] if os.name == "nt" else ["-DREVA_SDL"]), ["crowd_kms.c", "gfx_soft.c", "gfx_packet.c"])]
if os.name != "nt":
    BUILDS[2] = ("the renderer", ["-DREVA_VK", "-I/usr/include/libdrm"], ["crowd_kms.c", "gfx_vk.c"])
    BUILDS.append(("a game on the Pi", ["-DREVA_NODOORS", "-DREVA_VK", "-DREVA_GAMELIB", "-I/usr/include/libdrm"], ["crowd_kms.c"]))
env = dict(os.environ)
if os.name == "nt":
    env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
null = "NUL" if os.name == "nt" else "/dev/null"
seen = []
for name, flags, srcs in BUILDS:
    for s in srcs:
        p = subprocess.run([GCC] + BASE + flags + ["-c", s, "-o", null], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        if p.returncode != 0:
            sys.exit(f"{s}, {name}, did not compile:\n" + "\n".join(p.stdout.splitlines()[-10:]))
        for l in p.stdout.splitlines():
            if re.search(r"warning: .*\[-W", l) and l not in seen:
                seen.append(l)
for l in seen:
    print(l[:220])
print(f"{len(seen)} warnings" if seen else "no warnings")
sys.exit(1 if seen else 0)
