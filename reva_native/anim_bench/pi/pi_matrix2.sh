#!/bin/bash
# Real play conditions (no bench, no glFinish), with the shim: the frame split, and the zombie game.
# Runs on the Pi, as nemonic. Every run stops the console and brings it back (pi_run.sh).
SHIM=LD_PRELOAD=/home/nemonic/engine_test/gl_shim.so
RUN="sudo systemd-run --unit=enginetest --collect --wait --quiet bash /home/nemonic/engine_test/pi_run.sh"
show(){ echo "=== $1"; grep -E "^\[run\] (first|min|engine exited|low)|FATAL|Segmentation|No available|\[rvz\]|\[set\] REVA_SET|\[set\] collision" "$2" | cut -c1-130 | head -8; grep -E "^fps" "$2" | awk '{printf "%s/%s  ", $2, $4} END {print ""}'; grep -E "^\[shim\]" "$2" | tail -5 | cut -c1-190; }
perfrun(){ # $1 = process name, $2 = seconds to wait for boot, $3 = out
  ( sleep "$2"; P=$(pidof "$1"); [ -n "$P" ] && sudo perf record -q -F 250 -g -p "$P" -o /tmp/perf_$3.data -- sleep 15 >/dev/null 2>&1 ) &
}

$RUN /tmp/m2_k4.log 100 REVA_NOSAVE=1 REVA_TIER=pi $SHIM -- ./crowd_kms 4 packs select 720x480
show "KMS dance floor, 4 bodies, 480i, pi rung" /tmp/m2_k4.log

perfrun crowd_kms 80 k40
$RUN /tmp/m2_k40.log 110 REVA_NOSAVE=1 REVA_TIER=pi $SHIM -- ./crowd_kms 40 packs select 720x480
wait
show "KMS dance floor, 40 bodies, 480i, pi rung (perf from 80 s)" /tmp/m2_k40.log

$RUN /tmp/m2_k40h.log 100 REVA_NOSAVE=1 REVA_TIER=high $SHIM -- ./crowd_kms 40 packs select 720x480
show "KMS dance floor, 40 bodies, 480i, high rung" /tmp/m2_k40h.log

echo on | sudo tee /sys/class/drm/card1-Composite-1/status >/dev/null
perfrun crowd_linux 100 z
$RUN /tmp/m2_z.log 130 REVA_NOSAVE=1 REVA_TIER=pi SDL_VIDEODRIVER=kmsdrm REVA_AUTOMODE=9 REVA_AUTOVAR=1 REVA_AUTOPICK=0 $SHIM -- ./crowd_linux 400 packs packs/v1_kaeli_street.rpack
wait
show "ZOMBIE GAME on SDL kmsdrm, 480i, pi rung, the demo playing itself (perf from 100 s)" /tmp/m2_z.log

$RUN /tmp/m2_zh.log 110 REVA_NOSAVE=1 REVA_TIER=high SDL_VIDEODRIVER=kmsdrm REVA_AUTOMODE=9 REVA_AUTOVAR=1 REVA_AUTOPICK=0 $SHIM -- ./crowd_linux 400 packs packs/v1_kaeli_street.rpack
show "ZOMBIE GAME on SDL kmsdrm, 480i, high rung, the demo playing itself" /tmp/m2_zh.log
echo detect | sudo tee /sys/class/drm/card1-Composite-1/status >/dev/null

for r in k40 z; do
  echo "=== perf hot spots: $r"
  sudo perf report -i /tmp/perf_$r.data --stdio --no-children --percent-limit 1.2 -g none --sort dso 2>/dev/null | grep -vE "^#|^$" | head -10 | cut -c1-90
  sudo perf report -i /tmp/perf_$r.data --stdio --no-children --percent-limit 1.2 -g none --sort symbol 2>/dev/null | grep -vE "^#|^$" | head -24 | cut -c1-90
done
systemctl is-active recompilator
