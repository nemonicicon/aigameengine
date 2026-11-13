#!/usr/bin/env python3
"""make_apk.py -- (11-06) a game as an Android APK to test on a phone.

usage: make_apk.py --image IMAGE --sdk-volume VOL --sdl-volume VOL [--game sample] [--package com.example.<game>] [--abi arm64-v8a|x86_64]
                   [--out reva_native/dist/android] [--version 0.1-test] [--args "12 packs packs/v1_sample.rpack"] [--stb FOLDER]

It runs build_apk.sh in a container that has the Android SDK and NDK and SDL2 built for Android (android/README.md says what it must hold): --image is
that container's image, --sdk-volume the volume holding the Android SDK (mounted at /sdk), --sdl-volume the one holding SDL2's Android builds (mounted at
/prefix). They can also come from ANDROID_IMAGE, ANDROID_SDK_VOLUME and ANDROID_SDL_VOLUME. All are used read only. --stb (ANDROID_STB) is the folder
holding stb's headers on this machine: MSYS2's on Windows unless it's given, /usr/include/stb elsewhere. What it makes:
  <out>/<game>-<version>-<abi>.apk   the APK: the engine and the game as libmain.so on SDL2, SDL2's own Java, and the game's data as its assets
  <out>/keys/<game>-test.keystore    the game's own signing key, made the first time (never in git, never another project's)
The app's id is --package (com.example.<game> unless it's given) and its name is the game's. What the game starts with on the phone is --args, the words its
launcher says after the exe: without it, the first line of the game's own android_args.txt, or package.py's table for the games here, or "0 packs". The phone unpacks the data into the app's own storage the
first time it runs (engine/android.inc), which takes a little while; the log is files/log.txt there (adb shell run-as <package> cat files/log.txt).

⛔ The APK carries the game's data: whoever builds it must have the right to ship that data."""
import argparse
import os
import site
import subprocess
import sys
import time

site.addsitedir(site.getusersitepackages())
HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.dirname(HERE)


def icons(game_dir, out):
    """the launcher's icon at each density, from the game's own .ico when it has one (make_icon.py's), or a plain one"""
    from PIL import Image, ImageDraw
    src = None
    for f in os.listdir(game_dir):
        if f.lower().endswith(".ico"):
            src = Image.open(os.path.join(game_dir, f))
            try:
                src.size = max(src.info.get("sizes", [src.size]))
            except Exception:
                pass
            src = src.convert("RGBA")
            break
    if src is None:
        src = Image.new("RGBA", (256, 256), (30, 34, 40, 255))
        ImageDraw.Draw(src).ellipse([40, 40, 216, 216], fill=(220, 60, 50, 255))
    for name, px in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
        src.resize((px, px), Image.LANCZOS).save(os.path.join(out, "icon_%s.png" % name))


def start_args(game, game_dir):
    """what the game starts with on a phone: its own android_args.txt's first line, or package.py's table (the games in this repository), or none of the pool and its packs"""
    f = os.path.join(game_dir, "android_args.txt")
    if os.path.exists(f):
        return open(f, encoding="utf-8").read().split("\n")[0].strip()
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("package", os.path.join(NATIVE, "package.py"))
        m = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(m)
        if game in m.ARGS:
            return " ".join(m.ARGS[game])
    except Exception:
        pass
    return "0 packs"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="sample")
    ap.add_argument("--package", default=None)
    ap.add_argument("--abi", default="arm64-v8a", choices=["arm64-v8a", "x86_64"])
    ap.add_argument("--out", default=os.path.join(NATIVE, "dist", "android"))
    ap.add_argument("--version", default="0.1-test")
    ap.add_argument("--args", default=None)
    ap.add_argument("--image", default=os.environ.get("ANDROID_IMAGE"))
    ap.add_argument("--sdk-volume", default=os.environ.get("ANDROID_SDK_VOLUME"))
    ap.add_argument("--sdl-volume", default=os.environ.get("ANDROID_SDL_VOLUME"))
    ap.add_argument("--stb", default=os.environ.get("ANDROID_STB") or ("C:/msys64/mingw64/include/stb" if os.name == "nt" else "/usr/include/stb"))
    a = ap.parse_args()
    for k, env in (("image", "ANDROID_IMAGE"), ("sdk_volume", "ANDROID_SDK_VOLUME"), ("sdl_volume", "ANDROID_SDL_VOLUME")):
        if not getattr(a, k):
            sys.exit("make_apk: --%s (or %s) is needed: the build container's, as android/README.md says" % (k.replace("_", "-"), env))
    if not os.path.exists(os.path.join(a.stb, "stb_image.h")):
        sys.exit("make_apk: no stb_image.h in %s: --stb (or ANDROID_STB) says where stb's headers are" % a.stb)
    pkg = a.package or "com.example.%s" % a.game
    game_dir = os.path.join(NATIVE, "games", a.game)
    if not os.path.isdir(game_dir):
        sys.exit("no game folder %s" % game_dir)
    start = a.args or start_args(a.game, game_dir)
    out = os.path.abspath(a.out)
    os.makedirs(os.path.join(out, "keys"), exist_ok=True)
    icons(game_dir, out)
    stamp = time.strftime("%Y%m%d%H%M%S")
    sdl = "SDL2-2.32.10-android-%s" % ("x86_64" if a.abi == "x86_64" else "arm64")
    stb = os.path.abspath(a.stb).replace("\\", "/")
    cmd = ["docker", "run", "--rm",
           "-v", "%s:/sdk:ro" % a.sdk_volume, "-v", "%s:/prefix:ro" % a.sdl_volume,
           "-v", "%s:/src:ro" % NATIVE.replace("\\", "/"), "-v", "%s:/game:ro" % game_dir.replace("\\", "/"),
           "-v", "%s:/inc/stb:ro" % stb, "-v", "%s:/work" % out.replace("\\", "/"), "-v", "%s:/keys" % os.path.join(out, "keys").replace("\\", "/"),
           "-e", "GAME=%s" % a.game, "-e", "ABI=%s" % a.abi, "-e", "STAMP=%s" % stamp, "-e", "VERSION_NAME=%s" % a.version,
           "-e", "PKG=%s" % pkg, "-e", "LABEL=%s" % a.game, "-e", "ARGS=%s" % start,
           "-e", "VERSION_CODE=%d" % int(time.time() // 60 % 2000000000),
           a.image, "bash", "-c", "ln -s /prefix/%s /sdl && tr -d '\\r' < /src/android/build_apk.sh > /tmp/b.sh && bash /tmp/b.sh" % sdl]
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    t0 = time.time()
    r = subprocess.run(cmd, env=env)
    print("make_apk: exit %d in %.0f s" % (r.returncode, time.time() - t0))
    return r.returncode


if __name__ == "__main__":
    sys.exit(main())
