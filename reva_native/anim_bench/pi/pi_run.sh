#!/bin/bash
# One engine run on the tube: console off, engine for up to SECS seconds, console back.
# usage: sudo systemd-run --unit=enginetest --collect --wait bash pi_run.sh LOG SECS ENV=VAL... -- exe args...
# Lives only on the Pi, in ~/engine_test. Stops itself if memory runs low (swap-thrash locks out ssh).
# GPUAT=<s> among the ENVs samples the GPU's own busy counters for 10 s from that second: bin (vertices) and render (pixels).
# RUNDIR=<dir> among the ENVs runs from there instead of reva_native: a game's own folder.
LOG=$1; SECS=$2; shift 2
ENVS=(); GPUAT=-1; RUNDIR=/home/nemonic/engine_test/reva_native
# ⛔ (11-02) with no -- this loop never ended and took 2 GB in nine minutes
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do case "$1" in GPUAT=*) GPUAT=${1#GPUAT=};; RUNDIR=*) RUNDIR=${1#RUNDIR=};; esac; ENVS+=("$1"); shift; done
if [ "$#" -eq 0 ]; then echo "[run] no -- before the command" > "$LOG"; exit 1; fi
shift
cd "$RUNDIR" || exit 1
for u in desktop n64game n64emu; do
  if systemctl is-active --quiet "$u"; then echo "[run] $u is active, not touching the display" > "$LOG"; exit 1; fi
done
STATS=/sys/devices/platform/v3dbus/fec00000.v3d/gpu_stats
systemctl stop recompilator
sleep 1
{ echo "[run] start $(date +%T) env: ${ENVS[*]} cmd: $*"; fuser -v /dev/dri/card1 2>&1 | head -5; } > "$LOG"
T0=$(date +%s%N)
env "${ENVS[@]}" "$@" >> "$LOG" 2>&1 &
P=$!
MINAVAIL=99999999
BOOT=
for ((i=0;i<SECS;i++)); do
  sleep 1
  if ! kill -0 $P 2>/dev/null; then echo "[run] engine exited on its own after $i s" >> "$LOG"; break; fi
  A=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
  [ "$A" -lt "$MINAVAIL" ] && MINAVAIL=$A
  if [ -z "$BOOT" ] && grep -q "^fps" "$LOG"; then BOOT=$i; fi
  if [ "$i" = "$GPUAT" ]; then cp "$STATS" /tmp/gpu0; fi
  if [ "$i" = "$((GPUAT+10))" ] && [ "$GPUAT" != "-1" ]; then cp "$STATS" /tmp/gpu1
    awk 'NR==FNR{ if(FNR>1){ t0[$1]=$2; j0[$1]=$3; r0[$1]=$4 } next } FNR>1 && $1!="csd" && $1!="cpu" && $1!="cache_clean" { dt=($2-t0[$1])/1e9; printf "[gpu] %s busy %.1f%%, %.1f jobs a second, %.2f ms a job\n",$1,100*($4-r0[$1])/1e9/dt,($3-j0[$1])/dt,($3>j0[$1])?($4-r0[$1])/1e6/($3-j0[$1]):0 }' /tmp/gpu0 /tmp/gpu1 >> "$LOG"
  fi
  if [ "$A" -lt 250000 ]; then echo "[run] low memory ($A kB available), stopping the engine" >> "$LOG"; break; fi
done
{ echo "[run] first stat line at ${BOOT:-none} s (stats print every 5 s after the loop starts)"; echo "[run] min MemAvailable ${MINAVAIL} kB; temp $(vcgencmd measure_temp); throttled $(vcgencmd get_throttled); v3d $(vcgencmd measure_clock v3d); arm $(vcgencmd measure_clock arm)"; } >> "$LOG"
kill $P 2>/dev/null; sleep 1; kill -9 $P 2>/dev/null
wait $P 2>/dev/null
echo "[run] engine rc=$? ran $(( ($(date +%s%N)-T0)/1000000000 )) s" >> "$LOG"
systemctl start recompilator
echo "[run] console back $(date +%T)" >> "$LOG"
