#!/bin/sh
# Launch the skate bench. Edit the numbers, run it again -- no rebuild needed.
#
#   SCALE     how big the character is        (bigger number = bigger)
#   YAW       which way he faces, in degrees  (try 0 / 90 / 180 / 270)
#   TRICKSPD  trick playback speed            (1 = the reel's own pace, 2.6 = current)
#   FLIPAXIS  which way the DECK spins        (0=X 1=Y 2=Z)
#   HOLO      1 puts reva's hologram shader back
#   AUTOTRICK 1 fires a kickflip every 3s, hands-free, for watching it repeat

SCALE=0.62
YAW=180
TRICKSPD=2.6
FLIPAXIS=2
HOLO=0
AUTOTRICK=0

CHAR=${1:-v1_kickflip.rpack}

cd "$(dirname "$0")" || exit 1
taskkill //IM crowd.exe //F >/dev/null 2>&1
sleep 1

REVA_SKATE=1 \
REVA_SKSCALE=$SCALE \
REVA_SKYAW=$YAW \
REVA_TRICKSPD=$TRICKSPD \
REVA_FLIPAXIS=$FLIPAXIS \
REVA_HOLO=$HOLO \
REVA_AUTOTRICK=$AUTOTRICK \
REVA_DISPLAY=1 \
./crowd.exe 1 packs "$CHAR" >/dev/null 2>&1 &

echo "running: $CHAR   scale=$SCALE yaw=$YAW trickspd=$TRICKSPD flipaxis=$FLIPAXIS"
