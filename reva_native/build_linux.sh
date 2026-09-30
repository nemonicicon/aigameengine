#!/bin/sh
# build_linux.sh -- THE CLUB on Linux, through SDL2 + Mesa (EGL/GLES). 2026-09-30.
#
# The Windows build was never Windows: SDL2 supplies the window, the GLES context and the pad,
# and only four calls (_execv/_putenv/_chdir, the PHOSPHOR handoff) are Win32. -DREVA_SDL builds
# that same platform on Linux; without it the file is the Pi's KMS/DRM renderer.
#
# Deps (Ubuntu/Debian):
#   apt-get install libsdl2-dev libgles-dev libegl-dev libstb-dev
# Headless (no GPU, no desktop): Mesa's llvmpipe under Xvfb renders every pass the club has --
#   apt-get install xvfb libgl1-mesa-dri
#   REVA_WINDOWED=1 xvfb-run -a -s "-screen 0 1280x720x24" ./crowd_linux 400 packs select
# ⛔ stdout is block-buffered into a pipe: wrap with `stdbuf -o0` or the log ends at the kill.
# ⛔ No `-Dmain=SDL_main` here -- that is the MinGW SDL2main shim, and Linux has no shim.
set -e
cd "$(dirname "$0")"
OUT=${1:-crowd_linux}
gcc -O2 -DREVA_SDL -o "$OUT" crowd_kms.c holly_fbo.c -lSDL2 -lEGL -lGLESv2 -lm
echo "built $OUT"
