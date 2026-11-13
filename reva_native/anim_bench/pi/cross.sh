#!/bin/bash
# cross.sh -- build for the Pi on the PC, and send it over. The ARM compiler lives in the revalinux Docker container: Debian trixie, the same release and the same gcc 14.2 as the Pi, with the Pi's libraries, and /src is reva_native, read-only.
# It goes through build_pi.sh's own lines, so there's one list of what a build is. About 30 s a build against about 3 minutes on the Pi. The hashes on the Pi are the proof: a cross-built engine prints the same ones as one built there.
#
# usage: cross.sh vk|headless [game] [data]
#   game   a folder in games/, default rvz, which keeps the old doors in as build_pi.sh does; any other game builds without them
#   data   also send what a run reads: the games, rules, voice and the receipts' references
# It lands on the Pi as crowd_vk and crowd_headless for rvz, crowd_<game>_vk and crowd_<game>_h otherwise.
# First time, in the container: dpkg --add-architecture arm64; apt-get update; apt-get install --no-install-recommends gcc-aarch64-linux-gnu libsdl2-dev:arm64 libvulkan-dev:arm64 libdrm-dev:arm64
set -e
export MSYS_NO_PATHCONV=1
WHAT=${1:-vk}; GAME=${2:-rvz}; DATA=${3:-}
PI=${PI:-pi@raspberrypi.local}; PIDIR=${PIDIR:-engine_test/reva_native}; BOX=${BOX:-revalinux}
EXTRA=-DREVA_NODOORS; [ "$GAME" = rvz ] && EXTRA=
OUT=crowd_${GAME}_vk; [ "$WHAT" = headless ] && OUT=crowd_${GAME}_h
if [ "$GAME" = rvz ]; then OUT=crowd_vk; [ "$WHAT" = headless ] && OUT=crowd_headless; fi
FROM=crowd_vk; [ "$WHAT" = headless ] && FROM=crowd_headless
t0=$(date +%s)
docker exec "$BOX" sh -c "mkdir -p /build/$GAME && tr -d '\r' < /src/build_pi.sh > /build/build_pi.sh && CC=aarch64-linux-gnu-gcc OUTDIR=/build/$GAME SRCDIR=/src GAME=games/$GAME/$GAME.c EXTRA='$EXTRA' sh /build/build_pi.sh $WHAT 2>&1 | grep -E 'error:|undefined reference' | cut -c1-200 | head -8; test -s /build/$GAME/$FROM && aarch64-linux-gnu-strip -o /build/$GAME/$OUT.stripped /build/$GAME/$FROM && test -s /build/$GAME/$OUT.stripped"
t1=$(date +%s)
[ -n "${NOCOPY:-}" ] && { echo "$OUT: built in $((t1-t0)) s, and left in the container (NOCOPY)"; exit 0; }
docker exec "$BOX" cat "/build/$GAME/$OUT.stripped" | ssh "$PI" "cat > $PIDIR/$OUT.tmp && chmod +x $PIDIR/$OUT.tmp && mv $PIDIR/$OUT.tmp $PIDIR/$OUT"
A=$(docker exec "$BOX" sha256sum "/build/$GAME/$OUT.stripped" | cut -c1-16); B=$(ssh "$PI" "sha256sum $PIDIR/$OUT" | cut -c1-16)
[ "$A" = "$B" ] || { echo "the copy on the Pi doesn't match: $A $B"; exit 1; }
echo "$OUT: built in $((t1-t0)) s, on the Pi as $PIDIR/$OUT ($A)"
if [ -n "$DATA" ]; then
  cd "$(dirname "$0")/../.."
  tar cf - --exclude='*.exe' --exclude='*.o' --exclude='stage' --exclude='cooked' --exclude='games/roam/props' games rules voice anim_bench/hashcheck.py anim_bench/hash_refs anim_bench/golden | ssh "$PI" "cd $PIDIR && tar xf -"
  echo "sent the data"
fi
