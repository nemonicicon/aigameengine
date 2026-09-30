#!/bin/sh
# build_pi.sh -- THE CLUB on a Raspberry Pi 4 (64-bit Raspberry Pi OS, vc4-kms-v3d). 2026-09-30.
#
# Two ways onto the Pi's glass, from the same file:
#
#   kms   crowd_kms    the ORIGINAL path: takes DRM master, GBM/EGL on the vc4 card, page-flips at the
#                      video clock. No compositor, no X, no Wayland; the pad is /dev/input/js0. This is
#                      the composite-CRT show (interlace-aware fps, the no-hotplug fallback). No keyboard,
#                      no select screens, no REVA_SHOT capture -- those live on the SDL platform.
#   sdl   crowd_linux  the SDL platform (-DREVA_SDL), identical to the Windows and desktop-Linux build:
#                      keyboard + mouse, SDL_GameController, mode/character select, REVA_SHOT bursts.
#                      On the Pi desktop it is just a window; with no desktop at all,
#                      SDL_VIDEODRIVER=kmsdrm puts it straight on the glass through SDL's own KMS backend.
#
# Deps:  sudo apt-get install libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev libsdl2-dev libstb-dev
# ⛔ 64-bit OS. The packs are read through packed structs of floats; AArch64 loads them unaligned without
#    complaint, 32-bit VFP does not.
# ⛔ /boot/firmware/config.txt needs dtoverlay=vc4-kms-v3d (the FULL KMS overlay, not fkms). The card
#    with the connector is FOUND at boot (REVA_DRM=/dev/dri/cardN pins it) -- it was "card1" by assumption.
# ⛔ SDL2 on the Pi is built with audio (the mixer needs it) on both paths: the KMS build initialises
#    SDL for AUDIO ONLY and never opens a window.
set -e
cd "$(dirname "$0")"
WHICH=${1:-both}
if [ "$WHICH" = kms ] || [ "$WHICH" = both ]; then
  gcc -O2 -ffp-contract=off -o crowd_kms crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c -I/usr/include/libdrm -ldrm -lgbm -lSDL2 -lEGL -lGLESv2 -lm
  echo "built crowd_kms      (DRM master; run with the desktop stopped: sudo systemctl stop lightdm  /  ./crowd_kms 400 packs)"
fi
if [ "$WHICH" = sdl ] || [ "$WHICH" = both ]; then
  gcc -O2 -ffp-contract=off -DREVA_SDL -o crowd_linux crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c -lSDL2 -lEGL -lGLESv2 -lm
  echo "built crowd_linux    (desktop: REVA_WINDOWED=1 ./crowd_linux 400 packs select;  no desktop: SDL_VIDEODRIVER=kmsdrm ./crowd_linux 400 packs select)"
fi
