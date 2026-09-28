#!/usr/bin/env bash
# one capture of sets/<1> with the engine's stdout kept, so the bank report is readable
set -u
SET="${1:-ztest}"
CAM="${2:--4.2,2.2,-5.2,0,1.5,0,32}"
SP="${CLUB_OUT:-out}/shoot_set"; mkdir -p "$SP"
N="/c/MyVeryOwnVideoGames/reva/reva_native"
cd "$N" || exit 1
rm -rf "$SP/zf"; mkdir -p "$SP/zf"
printf 'seq=%s face=0 grid=0 dqs=0 clip=z_upright set=%s sun=50,210,1.3,1.0,0.96,0.90 sky=0.44,0.50,0.62 cam=%s\n' \
    "$$" "$SET" "$CAM" > anim_bench/lab_ctl.txt
REVA_WINDOWED=1 REVA_LABCTL=anim_bench/lab_ctl.txt REVA_AUTOMODE=11 \
REVA_ANIMSUBJ=r2moves REVA_ANIMSTATE=0 REVA_FIXEDSTEP=30 \
REVA_SHOT=420 REVA_SHOTN=3 REVA_SHOTEVERY=8 \
REVA_SHOT_PATH="$(cygpath -w "$SP/zf/f.ppm")" \
    ./crowd_new.exe 400 packs r2moves > "$SP/zlog.txt" 2>&1 &
PID=$!
for i in $(seq 1 40); do
    n=$(ls "$SP/zf"/*.ppm 2>/dev/null | wc -l)
    [ "$n" -ge 3 ] && break
    kill -0 $PID 2>/dev/null || break
    sleep 1
done
sleep 1
taskkill //F //IM crowd_new.exe >/dev/null 2>&1
echo "frames: $(ls "$SP/zf"/*.ppm 2>/dev/null | wc -l)"
echo "=== bank / set / donor ==="
grep -i "zombie bank\|\[set\]\|DONOR BANK" "$SP/zlog.txt" | head -20
