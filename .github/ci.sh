#!/bin/bash
# ci.sh -- what this repository's CI runs, as one script, so the same lines can be run on any Linux machine:
#     bash .github/ci.sh            everything, in order
#     bash .github/ci.sh build      every way the engine is built off Windows, and every game with no display
#     bash .github/ci.sh steady     each game against itself: the same hashes at odd frame pacings, and the sample from another pack format
#     bash .github/ci.sh online     two to four engines in one session on this machine's loopback
#     bash .github/ci.sh memory     the sample game twice, with fresh memory filled two different ways: the same hashes
# It needs gcc, python3 and libsdl2-dev libvulkan-dev libstb-dev libdrm-dev. No GPU, no display, no window.
# What it does NOT do: run the Vulkan build (it is compiled and linked, never started: a runner has no driver
# for it), look at a picture, touch a Raspberry Pi, or compare anything with a kept copy of what the engine printed
# before: the engine isn't finished, and that changes with every improvement. Every check here holds the engine against itself.
# JOBS=<n> is how many compiles run at once (4: each is one big file, about 45 s and half a gigabyte).
set -u
cd "$(dirname "$0")/../reva_native" || exit 1
OUT="$(cd .. && pwd)/out/ci"
mkdir -p "$OUT"
GAMES="sample rocks rally tower herd"   # the games whose files are all in git. talk, people and roam make their packs with a script first
                                       # (games/<name>/make_assets.py: numpy, Pillow), and that has only been run on Windows
JOBS=${JOBS:-4}

build() {
  local names="" n
  one() {   # one NAME COMMAND...: in the background, its output kept, at most JOBS at once
    local name=$1; shift
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done
    names="$names $name"
    ( "$@" > "$OUT/build_$name.log" 2>&1 && touch "$OUT/build_$name.ok" ) &
  }
  rm -f "$OUT"/build_*.ok
  one linux        ./build_linux.sh "$OUT/crowd_linux"                  # Vulkan on SDL's window
  one linux_soft   ./build_linux.sh soft                                # the engine's own rasterizer, the sample game in it
  one linux_bare   ./build_linux.sh headless                            # no display: what the checks below run
  one pi_road      env OUTDIR="$OUT" ./build_pi.sh vk                   # the Pi's direct road, built for this machine
  one pi_dev       env OUTDIR="$OUT" ./build_pi.sh dev                  # the engine with no game in it, and the sample as a library
  for g in $GAMES; do
    [ "$g" = sample ] && continue
    mkdir -p "$OUT/$g"
    one "$g"       env GAME="games/$g/$g.c" OUTDIR="$OUT/$g" ./build_pi.sh headless
  done
  wait
  local bad=0
  for n in $names; do
    if [ -f "$OUT/build_$n.ok" ]; then echo "built: $n"; else echo "FAILED: $n"; tail -30 "$OUT/build_$n.log"; bad=1; fi
  done
  return $bad
}

steady() {
  local bad=0 g
  for g in $GAMES; do
    echo "== $g"
    if [ "$g" = sample ]; then
      python3 anim_bench/hashcheck.py --game sample --exe ./crowd_headless || bad=1
    else
      python3 anim_bench/hashcheck.py --game "$g" --exe "$OUT/$g/crowd_headless" || bad=1
    fi
  done
  return $bad
}

online() {
  python3 anim_bench/netcheck.py --exe "$OUT/rally/crowd_headless"
}

memory() {
  # The control first. MALLOC_PERTURB_ asks the C library to fill fresh memory with a byte. If this library ignores it,
  # the two runs below would agree whatever the engine did, and the test would prove nothing.
  printf '#include <stdio.h>\n#include <stdlib.h>\nint main(void){ unsigned char *p = malloc(64); printf("%%02x\\n", p[0]); return 0; }\n' > "$OUT/fill.c"
  gcc -O0 -w -o "$OUT/fill" "$OUT/fill.c" || return 1
  local a b P
  a=$(MALLOC_PERTURB_=85 "$OUT/fill"); b=$(MALLOC_PERTURB_=170 "$OUT/fill")
  if [ "$a" = "$b" ]; then echo "this C library ignored MALLOC_PERTURB_ (fresh memory read $a both times): the test would prove nothing"; return 1; fi
  echo "fresh memory reads $a with one fill and $b with the other: the C library does as it's asked"
  # The sample game with each fill, its lines kept. Anything the rules read before writing it would change a hash, and the two files would part.
  for P in 85 170; do
    echo "== fill $P"
    MALLOC_PERTURB_=$P python3 anim_bench/hashcheck.py --game sample --exe ./crowd_headless --lines "$OUT/fill_$P.txt" || return 1
  done
  if cmp -s "$OUT/fill_85.txt" "$OUT/fill_170.txt"; then
    echo "the two fills print the same $(wc -l < "$OUT/fill_85.txt") lines"
  else
    echo "the two fills part:"; diff "$OUT/fill_85.txt" "$OUT/fill_170.txt" | head -6; return 1
  fi
}

case "${1:-all}" in
  build)    build ;;
  steady)   steady ;;
  online)   online ;;
  memory)   memory ;;
  all)      build && steady && online && memory ;;
  *)        echo "ci.sh: build | steady | online | memory | all"; exit 2 ;;
esac
