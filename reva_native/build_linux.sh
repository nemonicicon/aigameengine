#!/bin/bash
# build_linux.sh -- THE CLUB on Linux: the Vulkan renderer on SDL2's window, or no display at all. Step 18 of the engine plan (11-04)
# took the old renderer out, so there's one picture everywhere: gfx_vk.c draws the frame gfx_packet.c carries as data.
#   ./build_linux.sh            -> crowd_linux    desktop: REVA_WINDOWED=1 ./crowd_linux 400 packs select;  no desktop: SDL_VIDEODRIVER=kmsdrm ./crowd_linux 400 packs select
#   ./build_linux.sh headless   -> crowd_headless no display, no GPU: the sim only. Then: python3 anim_bench/hashcheck.py
#   ./build_linux.sh soft       -> crowd_soft     the engine's own rasterizer (queue step 27): 320 x 240 on the processor, nothing links Vulkan. REVA_WINDOWED=1 ./crowd_soft 400 packs select;
#                                                 REVA_NOWINDOW=1 draws with no window
# Deps: libsdl2-dev libvulkan-dev libstb-dev. Pixels with no GPU come from Mesa's lavapipe (mesa-vulkan-drivers), a test layer, step 19.
# GAME=games/<name>/<name>.c picks the game; the sample stands in when the zombie game isn't in this tree.
set -e
cd "$(dirname "$0")"
GAME=${GAME:-games/rvz/rvz.c}
[ -f "$GAME" ] || GAME=games/sample/sample.c
ENGINE="crowd_kms.c club_math.c club_audio.c club_save.c holly_fbo.c club_bridge.c club_net.c gfx_packet.c"
if [ "$1" = headless ]; then   # no display, no GPU: the sim only (engine plan, Phase 1)
  gcc -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -DREVA_HEADLESS -o crowd_headless $ENGINE $GAME -lSDL2 -lm
  echo "built crowd_headless"; exit 0
fi
if [ "$1" = soft ]; then   # the engine with no driver at all (queue step 27)
  gcc -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -DREVA_SOFT -o crowd_soft $ENGINE $GAME gfx_soft.c -lSDL2 -lm
  echo "built crowd_soft"; exit 0
fi
OUT=${1:-crowd_linux}
gcc -O2 -std=gnu17 -ffp-contract=off $EXTRA -DREVA_SDL -DREVA_VK -o "$OUT" $ENGINE $GAME gfx_vk.c -lSDL2 -lvulkan -lm
echo "built $OUT"
