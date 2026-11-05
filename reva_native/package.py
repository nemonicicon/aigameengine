#!/usr/bin/env python3
"""package.py -- A GAME AS SOMETHING YOU CAN HAND TO SOMEONE (queue step 28: packaging).

    python reva_native/package.py --game sample                 dist/sample-<version>-windows-x64/ and its .zip
    python reva_native/package.py --game sample --target pi     dist/sample-<version>-pi4-arm64/ and its .tar.gz, built in the build container (docker: revalinux)
    python reva_native/package.py --game sample --target linux  the same for a Linux PC (x86-64), built there too
    --version 1.0     what it's called. With none it's the engine's commit
    --keep            leave an old folder of the same name's files and write over them (default: it's made fresh)

What goes in: the game on the Vulkan renderer, and the same game on the engine's own rasterizer (`-safe`), which needs no graphics driver at all and is what to
run when the other won't start; the libraries a Windows build asks for that Windows doesn't have; exactly the files the game reads (its own folder, or for a game with
a game.txt its staged folder, anim_bench/cookcheck.py); the engine's setup voice; LICENSE, NOTICES.md, and the licence texts of everything in it that's someone
else's, copied out of the toolchain that built it; and VERSION.txt, which says what it was built from and gives every file's sha256.

⭐ A RELEASE BUILD REPLAYS: before it's zipped, the packaged `-safe` build is run from inside the package's own folder, with nothing on the PATH but Windows, on the
game's tape, and its rules' hashes are held against the receipt's (anim_bench/hash_refs). So what's in the zip is known to start, to find everything it needs
beside itself, and to play the same game as every other build. The Pi's is run the same way under emulation.

⛔ It never packs a glyph file baked from a font (data/*.tvw: that font's licence would go with it), a save, or a source file. ⛔ Nothing here uploads anything:
a store's own tool takes the folder or the zip. ⛔ A game with bought content in it is that game's maker's to have the right to ship; this doesn't know.
"""
import argparse
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tarfile

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MINGW = r"C:\msys64\mingw64"
BOX = os.environ.get("BOX", "revalinux")
ARGS = {"sample": ["12", "packs", "packs/v1_sample.rpack"], "talk": ["4", "packs", "packs/v1_talk.rpack"], "tower": ["10", "packs", "packs/v1_tower.rpack"], "rocks": ["0", "packs"], "rally": ["0", "packs"],
        "herd": ["400", "packs", "packs/v1_herd.rpack"], "people": ["8", "packs", "packs/v1_boot.rpack"], "roam": ["0", "packs", "packs/v1_tower.rpack"], "rvz": ["100", "packs_rvz", "packs_rvz/v1_kaeli_game.rpack"]}
ENV = {"rvz": {"REVA_AUTOVAR": "1"}}
TAPE = {"sample": "sample_run.tape", "tower": "tower_run.tape", "rocks": "rocks_run.tape", "rally": "rally_run.tape", "herd": "herd_run.tape", "rvz": "zombie_run.tape"}     # the receipt's tape, where a game has one
REF = {"sample": "sample_tape.txt", "tower": "tower_tape.txt", "rocks": "rocks_tape.txt", "rally": "rally_tape.txt", "herd": "herd_tape.txt", "rvz": "zombie_tape.txt"}
SKIP = ("*.c", "*.py", "*.bat", "*.sh", "*.tvw", "*.before_bake", "*.bak", "stage", "cooked", "save", "__pycache__", "game.txt", "open.txt")
WIN_LICENCES = ["SDL2", "crt", "winpthreads", "libwinpthread", "stb", "gcc-libs"]


def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", **kw)


def git(*a):
    return run(["git", "-C", REPO] + list(a)).stdout.strip()


def driver():
    spec = importlib.util.spec_from_file_location("driver", os.path.join(REPO, ".claude", "skills", "run-reva", "driver.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def copy_game(game, dst):
    """exactly what the game reads: its staged folder if it lists its files, else its own folder without the sources"""
    src = os.path.join(HERE, "games", game)
    if os.path.exists(os.path.join(src, "game.txt")):
        stage = os.path.join(src, "stage")
        if not os.path.isdir(stage):
            sys.exit("%s lists its files (game.txt): stage it first, python anim_bench/cookcheck.py %s --cook --stage" % (game, game))
        src = stage
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(*SKIP), dirs_exist_ok=True)
    voice = os.path.join(HERE, "voice")
    os.makedirs(os.path.join(dst, "voice"), exist_ok=True)
    for f in os.listdir(voice):
        if f.endswith(".wav") and not os.path.exists(os.path.join(dst, "voice", f)):
            shutil.copy2(os.path.join(voice, f), os.path.join(dst, "voice", f))
    for f in ("LICENSE", "NOTICES.md"):
        shutil.copy2(os.path.join(REPO, f), os.path.join(dst, f))


def replay(cmd, cwd, game, env_extra, what):
    """the packaged build on the game's tape, from inside the package: its rules' hashes against the receipt's"""
    ref = os.path.join(HERE, "anim_bench", "hash_refs", REF.get(game, ""))
    tape = os.path.join(HERE, "anim_bench", "hash_refs", TAPE.get(game, ""))
    env = dict(env_extra, REVA_NOWINDOW="1", REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60", REVA_MODE="9", REVA_TIER="pi", **ENV.get(game, {}))
    have = game in TAPE and os.path.exists(tape) and os.path.exists(ref)
    if have:
        shutil.copy2(tape, os.path.join(cwd, "_receipt.tape"))
        env["REVA_REPLAY"] = "_receipt.tape"
    env["REVA_EXITTICK"] = "1800" if have else "600"
    p = run(cmd + ARGS[game], cwd=cwd, env=env, timeout=1800)
    if have:
        os.remove(os.path.join(cwd, "_receipt.tape"))
    got = [l for l in p.stdout.splitlines() if l.startswith("[simhash]") and "(end)" not in l]
    if not got or "exiting clean" not in p.stdout:
        print("⛔ %s didn't run from its own folder (exit %s):\n%s" % (what, p.returncode, "\n".join(p.stdout.splitlines()[-12:])))
        return None
    if not have:
        return "%s ran from its own folder for %d ticks (this game has no tape in the receipt to hold it to)" % (what, int(env["REVA_EXITTICK"]))
    want = [l for l in open(ref).read().splitlines() if l.startswith("[simhash]") and "(end)" not in l]
    n = min(len(got), len(want))
    if n < 10 or got[:n] != want[:n]:
        bad = next((i for i in range(n) if got[i] != want[i]), n)
        print("⛔ %s replays the tape and its rules part from the receipt at line %d:\n  %s\n  %s" % (what, bad, got[bad] if bad < len(got) else "(none)", want[bad] if bad < len(want) else "(none)"))
        return None
    return "%s replays the receipt's tape from its own folder: %d of the rules' hashes, all the same" % (what, n)


def manifest(dst, name, lines):
    """VERSION.txt: what it is, and every file's sha256"""
    rows = []
    for root, _, files in os.walk(dst):
        for f in sorted(files):
            if f == "VERSION.txt":
                continue
            p = os.path.join(root, f)
            rows.append((os.path.relpath(p, dst).replace(os.sep, "/"), hashlib.sha256(open(p, "rb").read()).hexdigest(), os.path.getsize(p)))
    rows.sort()
    with open(os.path.join(dst, "VERSION.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(name + "\n" + "\n".join(lines) + "\n\n%d files, %.1f MB. sha256, bytes, path:\n" % (len(rows), sum(r[2] for r in rows) / 1048576.0))
        for path, h, size in rows:
            f.write("%s %10d %s\n" % (h, size, path))
    return len(rows), sum(r[2] for r in rows)


def lock_section(key):
    out, on = [], False
    p = os.path.join(HERE, "toolchain.lock")
    if os.path.exists(p):
        for ln in open(p, encoding="utf-8"):
            if ln.startswith("["):
                on = ln.startswith("[" + key)
            elif on and ln.strip():
                out.append(ln.strip().replace("\t", " "))
    return out


def windows(a, name, dst):
    d = driver()
    ns = argparse.Namespace(game=a.game, sample=False)
    if d.buildvk(ns) or d.buildsoft(ns):
        sys.exit("the build failed")
    copy_game(a.game, dst)
    exes = {a.game + ".exe": "crowd_%s_vk.exe" % a.game, a.game + "-safe.exe": "crowd_%s_soft.exe" % a.game}
    dlls = set()
    for out, src in exes.items():
        shutil.copy2(os.path.join(HERE, src), os.path.join(dst, out))
        for m in re.finditer(r"DLL Name: (\S+)", run([os.path.join(MINGW, "bin", "objdump.exe"), "-p", os.path.join(dst, out)]).stdout):
            if m.group(1).lower() != "vulkan-1.dll" and os.path.exists(os.path.join(MINGW, "bin", m.group(1))):     # one the toolchain brought: Windows hasn't got it. Never the Vulkan loader: that's the graphics driver's, and a stale copy beside the game would win over it
                dlls.add(m.group(1))
    for f in sorted(dlls):
        shutil.copy2(os.path.join(MINGW, "bin", f), os.path.join(dst, f))
    lic = os.path.join(dst, "licenses")
    os.makedirs(lic, exist_ok=True)
    for pk in WIN_LICENCES:
        src = os.path.join(MINGW, "share", "licenses", pk)
        if os.path.isdir(src):
            for f in os.listdir(src):
                shutil.copy2(os.path.join(src, f), os.path.join(lic, "%s-%s%s" % (pk, f, "" if "." in f else ".txt")))
    args = " ".join(ARGS[a.game])
    sets = "".join("set %s=%s\r\n" % kv for kv in ENV.get(a.game, {}).items())
    for bat, exe, extra, what in (("play.bat", a.game + ".exe", "", "the whole screen"), ("play_window.bat", a.game + ".exe", "set REVA_WINDOWED=1\r\n", "in a window"),
                                  ("play_safe.bat", a.game + "-safe.exe", "set REVA_WINDOWED=1\r\n", "on the engine's own rasterizer: no graphics driver needed. Run this if the others won't start")):
        open(os.path.join(dst, bat), "w", newline="").write("@echo off\r\nREM %s, %s.\r\ncd /d \"%%~dp0\"\r\n%s%sstart \"\" \"%s\" %s\r\n" % (a.game, what, sets, extra, exe, args))
    env = {k: v for k, v in os.environ.items() if k.upper() not in ("PATH",)}
    env["PATH"] = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32") + os.pathsep + os.environ.get("SystemRoot", r"C:\Windows")     # nothing of the toolchain's: the package has to bring its own
    said = replay([os.path.join(dst, a.game + "-safe.exe")], dst, a.game, env, a.game + "-safe.exe")
    if not said:
        sys.exit(1)
    n, size = manifest(dst, name, ["engine %s%s, API %s" % (git("rev-parse", "--short", "HEAD"), " and uncommitted changes" if git("status", "--porcelain", "--untracked-files=no") else "", api()),
                                   "built with: " + "; ".join(lock_section("windows")[:12]), "ships beside the game: " + ", ".join(sorted(dlls)), said])
    z = shutil.make_archive(dst, "zip", os.path.dirname(dst), os.path.basename(dst))
    print("%s\n%s: %d files, %.1f MB\n%s: %.1f MB" % (said, dst, n, size / 1048576.0, z, os.path.getsize(z) / 1048576.0))


def api():
    m = re.search(r"#define ENGINE_API_VERSION (\d+)", open(os.path.join(HERE, "engine.h"), encoding="utf-8", errors="replace").read())
    return m.group(1) if m else "?"


def box(cmd, **kw):
    return run(["docker", "exec"] + cmd, env=dict(os.environ, MSYS_NO_PATHCONV="1"), **kw)


def linux(a, name, dst, pi):
    """built in the container: its own compiler for a Linux PC, the cross compiler for the Pi. /src there is this folder, read-only, so the package is read from /src/dist"""
    cc, strip, tag = ("aarch64-linux-gnu-gcc", "aarch64-linux-gnu-strip", "a64") if pi else ("gcc", "strip", "x64")
    out = "/build/pkg/%s_%s" % (a.game, tag)
    b = box([BOX, "sh", "-c", "mkdir -p %s && tr -d '\\r' < /src/build_pi.sh > /build/build_pi.sh && tr -d '\\r' < /src/build_linux.sh > /build/build_linux.sh && cd /src && "
             "CC=%s OUTDIR=%s SRCDIR=/src GAME=games/%s/%s.c EXTRA=-DREVA_NODOORS sh /build/build_pi.sh soft && " % (out, cc, out, a.game, a.game)
             + ("CC=%s OUTDIR=%s SRCDIR=/src GAME=games/%s/%s.c EXTRA=-DREVA_NODOORS sh /build/build_pi.sh vk && " % (cc, out, a.game, a.game) if pi
                else "%s -O2 -std=gnu17 -ffp-contract=off -DREVA_NODOORS -DREVA_SDL -DREVA_VK -o %s/crowd_vk crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c gfx_packet.c games/%s/%s.c gfx_vk.c -lSDL2 -lvulkan -lm && " % (cc, out, a.game, a.game))
             + "%s -o %s/%s %s/crowd_vk && %s -o %s/%s-safe %s/crowd_soft && echo BUILT" % (strip, out, a.game, out, strip, out, a.game, out)], timeout=3600)
    if "BUILT" not in b.stdout:
        sys.exit("the build in the container failed:\n" + "\n".join(b.stdout.splitlines()[-15:]))
    copy_game(a.game, dst)
    for f in (a.game, a.game + "-safe"):
        with open(os.path.join(dst, f), "wb") as o:
            o.write(subprocess.run(["docker", "exec", BOX, "cat", "%s/%s" % (out, f)], stdout=subprocess.PIPE, env=dict(os.environ, MSYS_NO_PATHCONV="1")).stdout)
    args = " ".join(ARGS[a.game])
    sets = "".join("export %s=%s\n" % kv for kv in ENV.get(a.game, {}).items())
    road = "the console, with no desktop running: it takes the screen itself" if pi else "a window"
    open(os.path.join(dst, "play.sh"), "w", newline="\n").write("#!/bin/sh\n# %s on the Vulkan renderer, on %s.\ncd \"$(dirname \"$0\")\"\n%s%sexec ./%s %s\n" % (a.game, road, sets, "" if pi else "export REVA_WINDOWED=1\n", a.game, args))
    open(os.path.join(dst, "play_safe.sh"), "w", newline="\n").write("#!/bin/sh\n# %s on the engine's own rasterizer: no graphics driver needed. Under a desktop.\ncd \"$(dirname \"$0\")\"\n%sexport REVA_WINDOWED=1\nexec ./%s-safe %s\n" % (a.game, sets, a.game, args))
    open(os.path.join(dst, "NEEDS.txt"), "w", newline="\n").write("What this build asks the machine for (Debian or Raspberry Pi OS names):\n  libsdl2-2.0-0\n  libvulkan1 and a Vulkan driver (mesa-vulkan-drivers on a Pi), for %s\n%s  nothing more for %s-safe\n"
                                                                    % (a.game, "  libdrm2\n" if pi else "", a.game))
    rel = "/src/dist/" + os.path.basename(dst)
    env = "REVA_NOWINDOW=1 REVA_NOSAVE=1 REVA_FIXEDSTEP=60 REVA_SIMHASH=60 REVA_MODE=9 REVA_TIER=pi " + " ".join("%s=%s" % kv for kv in ENV.get(a.game, {}).items())
    tape = "/src/anim_bench/hash_refs/" + TAPE.get(a.game, "none")
    have = a.game in TAPE and a.game in REF
    cmd = "cd %s && %s %s REVA_EXITTICK=%d %s%s/%s-safe %s" % (rel, env, ("REVA_REPLAY=" + tape) if have else "", 1800 if have else 600, "qemu-aarch64 " if pi else "", out, a.game, args)
    p = box([BOX, "sh", "-c", cmd], timeout=3600)
    got = [l for l in p.stdout.splitlines() if l.startswith("[simhash]") and "(end)" not in l]
    what = "%s-safe (%s)" % (a.game, "the Pi's build, under emulation" if pi else "Linux")
    if not got or "exiting clean" not in p.stdout:
        sys.exit("⛔ %s didn't run from the package's folder:\n%s" % (what, "\n".join(p.stdout.splitlines()[-12:])))
    said = "%s ran from its own folder" % what
    if have:
        want = [l for l in open(os.path.join(HERE, "anim_bench", "hash_refs", REF[a.game])).read().splitlines() if l.startswith("[simhash]") and "(end)" not in l]
        n = min(len(got), len(want))
        if n < 10 or got[:n] != want[:n]:
            sys.exit("⛔ %s replays the tape and its rules part from the receipt" % what)
        said = "%s replays the receipt's tape from its own folder: %d of the rules' hashes, all the same" % (what, n)
    n, size = manifest(dst, name, ["engine %s%s, API %s" % (git("rev-parse", "--short", "HEAD"), " and uncommitted changes" if git("status", "--porcelain", "--untracked-files=no") else "", api()),
                                   "built with: " + "; ".join(lock_section("linux")[:10]), said, "⛔ built for the Pi and run under emulation only: no Pi has run this package" if pi else ""])
    tgz = dst + ".tar.gz"
    with tarfile.open(tgz, "w:gz") as t:
        for root, _, files in os.walk(dst):
            for f in sorted(files):
                p9 = os.path.join(root, f)
                ti = t.gettarinfo(p9, os.path.join(os.path.basename(dst), os.path.relpath(p9, dst)).replace(os.sep, "/"))
                ti.mode = 0o755 if (f in (a.game, a.game + "-safe") or f.endswith(".sh")) else 0o644
                ti.uid = ti.gid = 0
                ti.uname = ti.gname = "root"
                with open(p9, "rb") as fh:
                    t.addfile(ti, fh)
    print("%s\n%s: %d files, %.1f MB\n%s: %.1f MB" % (said, dst, n, size / 1048576.0, tgz, os.path.getsize(tgz) / 1048576.0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="sample")
    ap.add_argument("--target", default="windows", choices=("windows", "pi", "linux"))
    ap.add_argument("--version")
    ap.add_argument("--out", default=os.path.join(HERE, "dist"))
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if a.game not in ARGS or not os.path.isdir(os.path.join(HERE, "games", a.game)):
        sys.exit("no game called %s (the ones this knows how to start: %s)" % (a.game, " ".join(sorted(ARGS))))
    ver = a.version or git("rev-parse", "--short", "HEAD") or "dev"
    plat = {"windows": "windows-x64", "pi": "pi4-arm64", "linux": "linux-x64"}[a.target]
    name = "%s-%s-%s" % (a.game, ver, plat)
    dst = os.path.join(a.out, name)
    if os.path.isdir(dst) and not a.keep:
        shutil.rmtree(dst)
    os.makedirs(dst, exist_ok=True)
    if a.target == "windows":
        if os.name != "nt":
            sys.exit("the Windows package is built on Windows")
        windows(a, name, dst)
    else:
        linux(a, name, dst, a.target == "pi")


if __name__ == "__main__":
    main()
