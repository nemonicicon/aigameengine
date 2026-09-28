#!/usr/bin/env bash
# live.sh -- talk to a RUNNING lab. Boot once, then iterate without ever restarting.
#
#   bash anim_bench/live.sh start [subject]   # boot the lab in the background
#   bash anim_bench/live.sh state 2           # 0 idle 1 walk 2 run 3 air 4 trick 5 melee 6 gun
#   bash anim_bench/live.sh cam 3q            # side|front|3q|top
#   bash anim_bench/live.sh gait 0.42         # change the gait constant LIVE
#   bash anim_bench/live.sh trim 1.8          # multiply the DERIVED rate (sweep this)
#   bash anim_bench/live.sh speed 4.0         # drive at an exact u/s
#   bash anim_bench/live.sh shot out.png      # capture without exiting
#   bash anim_bench/live.sh grid 1            # TURNAROUND: 4 yaws in ONE live frame
#   bash anim_bench/live.sh quad sheet.png     # 4 CAMERAS on one frozen instant (adds top-down)
#   bash anim_bench/live.sh measure 6         # analyse the NEXT 6s of telemetry
#   bash anim_bench/live.sh stop
#
# `measure` is the piece that was missing: it slices the live telemetry log from now to
# N seconds from now and runs motioncheck on just that window, so a parameter can be
# changed and re-judged in one breath instead of one boot.
set -e
PY="${CLUB_PYTHON:-python}"
LAB="${TMPDIR:-/tmp}/anim_lab"; mkdir -p "$LAB"
CTL="$LAB/ctl.txt"; LOG="$LAB/lab.log"; SEQ="$LAB/seq"
cmd="${1:-}"; shift || true

send(){ n=$(( $(cat "$SEQ" 2>/dev/null || echo 0) + 1 )); echo "$n" > "$SEQ"
        printf 'seq=%d %s\n' "$n" "$*" > "$CTL"; sleep 0.5; }

case "$cmd" in
  start)
    if tasklist 2>/dev/null | grep -qi crowd_new.exe; then echo "already running"; exit 0; fi
    SUBJ="${1:-manny}"; : > "$LOG"; echo 0 > "$SEQ"; printf 'seq=0\n' > "$CTL"
    REVA_LABCTL="$CTL" REVA_ANIMSUBJ="$SUBJ" REVA_POSEDUMP=-1 REVA_WINDOWED=1 \
      REVA_AUTOMODE=11 REVA_ANIMSTATE=0 ./crowd_new.exe 400 packs "$SUBJ" >> "$LOG" 2>&1 &
    until grep -q "GAME: ANIM LAB" "$LOG" 2>/dev/null; do sleep 1; done
    echo "lab up on '$SUBJ'  (ctl=$CTL log=$LOG)" ;;
  state)  send "state=$1" ;;
  cam)    case "$1" in side) c=0;; front) c=1;; 3q) c=2;; top) c=3;; *) c=$1;; esac; send "cam=$c" ;;
  gait)   send "gait=$1" ;;
  trim)   send "trim=$1" ;;
  freeze) send "freeze=${1:-1}" ;;
  grid)   send "grid=${1:-1}" ;;
  quad)
    # MULTI-CAM: hold the pose, shoot every lens on that ONE instant, compose a 2x2.
    # Four separate runs at the same frame number are four different instants; this is
    # the same one, which is the only way angles can be compared honestly.
    out="${1:?need a path}"; base="${TMPDIR:-/tmp}/anim_lab/quad"
    send "freeze=1"; sleep 0.6
    for c in 0 1 2 3; do send "cam=$c"; sleep 0.4
      send "shot=$(cygpath -w "${base}_$c.ppm" 2>/dev/null || echo "${base}_$c.ppm")"; sleep 0.8; done
    send "freeze=0"
    "$PY" - "$out" "$base" <<'PYQ'
from PIL import Image, ImageDraw
import sys
out, base = sys.argv[1], sys.argv[2]
names = ["SIDE", "FRONT", "3/4", "TOP"]
ims = []
for i, n in enumerate(names):
    im = Image.open("%s_%d.ppm" % (base, i)).convert("RGB").resize((640, 360))
    d = ImageDraw.Draw(im); d.text((8, 6), n, fill=(120, 230, 210))
    ims.append(im)
g = Image.new("RGB", (640*2+4, 360*2+4), (12, 12, 15))
for i, im in enumerate(ims):
    g.paste(im, ((i % 2)*644, (i//2)*364))
g.save(out); print(out)
PYQ
    ;;
  speed)  send "speed=$1" ;;
  reset)  send "reset=1" ;;
  shot)   out="${1:?need a path}"; ppm="${out%.png}.ppm"
          send "shot=$(cygpath -w "$ppm" 2>/dev/null || echo "$ppm")"; sleep 1
          "$PY" -c "from PIL import Image;import sys;Image.open(sys.argv[1]).save(sys.argv[2])" "$ppm" "$out"
          echo "$out" ;;
  measure)
    secs="${1:-6}"
    start=$(wc -l < "$LOG")
    sleep "$secs"
    tail -n +$((start+1)) "$LOG" > "$LAB/slice.log"
    "$PY" anim_bench/motioncheck.py "$LAB/slice.log" ;;
  stop)   taskkill //IM crowd_new.exe //F >/dev/null 2>&1 || true; echo "lab stopped" ;;
  *) sed -n '2,20p' "$0" ;;
esac
