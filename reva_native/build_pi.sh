#!/bin/bash
# build_pi.sh -- THE CLUB on a Raspberry Pi 4 (64-bit OS, dtoverlay=vc4-kms-v3d). Step 18 of the engine plan (11-04) took the old
# renderer and its GBM/EGL route out: the picture is Vulkan, straight into the display card's scanout buffers and flipped through KMS,
# which is what gives the kernel's flip timestamps on the tube.
#   vk        crowd_vk        the renderer on the direct road. Console stopped, then:
#                             REVA_MODE=9 REVA_AUTOVAR=1 REVA_TIER=pi ./crowd_vk 100 packs_rvz packs/v1_Character_17_Female_Police.rpack
#   headless  crowd_headless  no display, no GPU: the sim only. Then: python3 anim_bench/hashcheck.py
#   soft      crowd_soft      the engine's own rasterizer (queue step 27): 320 x 240 on the processor, no driver. On the SDL road, so under a desktop; with no window at all
#                             it's the receipt's pixels, the same on every machine: python3 anim_bench/hashcheck.py --soft ./crowd_soft
#   dev       crowd_dev       the engine with no game linked, rebuilt only when it changes (Phase 5); then `game`
#   game      <game>/game.so  the game as a library a running crowd_dev picks up between ticks
# CC, OUTDIR, SRCDIR and EXTRA are for anim_bench/pi/cross.sh, which runs this in the revalinux container on the PC, about 45 seconds
# against 3 minutes on the Pi. GAME=games/<name>/<name>.c picks the game; the sample stands in when the zombie game isn't here.
set -e
cd "${SRCDIR:-$(dirname "$0")}"
CC=${CC:-gcc}
OUTDIR=${OUTDIR:-.}
WHICH=${1:-vk}
GAME=${GAME:-games/rvz/rvz.c}
[ -f "$GAME" ] || GAME=games/sample/sample.c
ENGINE_FILES="crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c gfx_packet.c gfx_vk.c"
if [ "$WHICH" = vk ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_VK -o "$OUTDIR/crowd_vk" $ENGINE_FILES $GAME -I/usr/include/libdrm -ldrm -lSDL2 -lvulkan -lm
  echo "built crowd_vk       (the Vulkan renderer on the direct road; console stopped: REVA_MODE=9 REVA_AUTOVAR=1 REVA_TIER=pi ./crowd_vk 100 packs_rvz packs/v1_Character_17_Female_Police.rpack)"
elif [ "$WHICH" = headless ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -DREVA_HEADLESS -o "$OUTDIR/crowd_headless" crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c $GAME gfx_packet.c -lSDL2 -lm
  echo "built crowd_headless (no display, no GPU: the sim only; python3 anim_bench/hashcheck.py)"
elif [ "$WHICH" = soft ]; then
  $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -DREVA_SOFT -o "$OUTDIR/crowd_soft" crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c $GAME gfx_packet.c gfx_soft.c -lSDL2 -lm
  echo "built crowd_soft     (the engine's own rasterizer, no driver; under a desktop: REVA_WINDOWED=1 ./crowd_soft ...; no window: REVA_NOWINDOW=1; python3 anim_bench/hashcheck.py --soft ./crowd_soft)"
elif [ "$WHICH" = dev ] || [ "$WHICH" = game ]; then
  if [ "$WHICH" = dev ]; then
    if [ -z "$(find . -maxdepth 2 \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) -not -path './games/*' -newer "$OUTDIR/crowd_dev" 2>/dev/null | head -1)" ] && [ -f "$OUTDIR/crowd_dev" ]; then
      echo "crowd_dev is up to date"
    else
      $CC -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_NODOORS -DREVA_VK -DREVA_GAMELIB -rdynamic -o "$OUTDIR/crowd_dev" $ENGINE_FILES -I/usr/include/libdrm -ldrm -lvulkan -lSDL2 -ldl -lm
      echo "built crowd_dev      (the engine alone: REVA_GAMELIB=games/sample/game.so ./crowd_dev 12 games/sample/packs games/sample/packs/v1_sample.rpack)"
    fi
  fi
  OUTSO="$OUTDIR/$(dirname "$GAME")/game.so"
  mkdir -p "$(dirname "$OUTSO")"
  $CC -O2 -std=gnu17 -ffp-contract=off -fPIC -shared -o "$OUTSO.tmp" "$GAME" && mv "$OUTSO.tmp" "$OUTSO"
  echo "built $OUTSO"
else
  echo "build_pi.sh: vk | headless | soft | dev | game (the old renderer's kms and sdl routes came out in step 18)"; exit 2
fi
