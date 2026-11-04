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
# CC, OUTDIR and SRCDIR come from outside for a build on another machine: anim_bench/pi/cross.sh, the same lines from the PC in about 30 s against about 3 minutes here
cd "${SRCDIR:-$(dirname "$0")}"
CC=${CC:-gcc}
OUTDIR=${OUTDIR:-.}
WHICH=${1:-both}
# the game's one file (GAME=games/x/x.c picks another); the kit has only the sample. EXTRA=-DREVA_NODOORS builds without the old game modes.
GAME=${GAME:-games/rvz/rvz.c}
[ -f "$GAME" ] || GAME=games/sample/sample.c
if [ "$WHICH" = kms ] || [ "$WHICH" = both ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -o "$OUTDIR/crowd_kms" crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c $GAME -I/usr/include/libdrm -ldrm -lgbm -lSDL2 -lEGL -lGLESv2 -lm
  echo "built crowd_kms      (DRM master; run with the desktop stopped: sudo systemctl stop lightdm  /  ./crowd_kms 400 packs)"
fi
if [ "$WHICH" = vk ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_VK -o "$OUTDIR/crowd_vk" crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c $GAME gl_null.c gfx_packet.c gfx_vk.c -I/usr/include/libdrm -ldrm -lSDL2 -lvulkan -lm
  echo "built crowd_vk       (the Vulkan renderer on the direct road; console stopped: REVA_MODE=9 REVA_AUTOVAR=1 REVA_TIER=pi ./crowd_vk 100 packs_rvz packs/v1_Character_17_Female_Police.rpack)"
fi
if [ "$WHICH" = headless ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -DREVA_HEADLESS -o "$OUTDIR/crowd_headless" crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c $GAME gl_null.c gfx_packet.c -lSDL2 -lm
  echo "built crowd_headless (no display, no GPU: the sim only; python3 anim_bench/hashcheck.py)"
fi
# dev: the dev console's way round. crowd_dev, the engine with no game in it, is only rebuilt when an engine file is newer. game: just the game, as a library, in a second or two, and a running crowd_dev takes it between ticks.
ENGINE_FILES="crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c gl_null.c gfx_packet.c gfx_vk.c"
if [ "$WHICH" = dev ]; then
  if [ -z "$(find . -maxdepth 2 \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) -not -path './games/*' -newer "$OUTDIR/crowd_dev" 2>/dev/null | head -1)" ] && [ -f "$OUTDIR/crowd_dev" ]; then
    echo "crowd_dev is up to date"
  else
    $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_NODOORS -DREVA_VK -DREVA_GAMELIB -rdynamic -o "$OUTDIR/crowd_dev" $ENGINE_FILES -I/usr/include/libdrm -ldrm -lvulkan -lSDL2 -ldl -lm
    echo "built crowd_dev      (the engine alone: REVA_GAMELIB=games/sample/game.so ./crowd_dev 12 games/sample/packs games/sample/packs/v1_sample.rpack)"
  fi
  WHICH=game
fi
if [ "$WHICH" = game ]; then
  OUTSO="$OUTDIR/$(dirname "$GAME")/game.so"
  mkdir -p "$(dirname "$OUTSO")"
  $CC -O2 -std=gnu17 -ffp-contract=off -fPIC -shared -o "$OUTSO.tmp" "$GAME" && mv "$OUTSO.tmp" "$OUTSO"
  echo "built $OUTSO"
fi
if [ "$WHICH" = sdl ] || [ "$WHICH" = both ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -o "$OUTDIR/crowd_linux" crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c $GAME -lSDL2 -lEGL -lGLESv2 -lm
  echo "built crowd_linux    (desktop: REVA_WINDOWED=1 ./crowd_linux 400 packs select;  no desktop: SDL_VIDEODRIVER=kmsdrm ./crowd_linux 400 packs select)"
fi
