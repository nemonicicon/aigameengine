#!/bin/bash
# pi_phase0.sh -- the engine half of Phase 0's measurements on the tube. Runs on the Pi, as nemonic, one run at a time.
# Each run stops the console and brings it back (pi_run.sh). In-spec NTSC modes only.
cd ~/engine_test/reva_native || exit 1
RUN="sudo systemd-run --unit=enginetest --collect --wait --quiet bash /home/nemonic/engine_test/pi_run.sh"
SHIM=LD_PRELOAD=/home/nemonic/engine_test/gl_shim.so
Z="REVA_NOSAVE=1 REVA_MODE=9 REVA_AUTOVAR=1 REVA_TIER=pi GPUAT=30"
CIT=packs/v1_Character_17_Female_Police.rpack
KAELI=packs/v1_kaeli_street.rpack
show(){ echo "=== $1"; grep -E "^\[run\] (first|min|engine exited|low)|^\[aspect\]|FATAL|Segmentation|connector .*mode|^\[gpu\]" "$2" | grep -v 'tfu' | cut -c1-130 | head -9; grep -E "^fps" "$2" | awk '{printf "%s/%s  ", $2, $4} END {print ""}' | cut -c1-170; grep -E "^\[frames\]" "$2" | tail -2 | cut -c1-130; grep -E "^\[pass\]" "$2" | tail -1 | cut -c1-260; grep -E "^\[shim\]" "$2" | tail -1 | cut -c60-210; }

# 1. The zombie game on the direct road: real play, then the pass clock, citizen hero, 100 slots
$RUN /tmp/p0_z.log 62 $Z $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x480
show "zombie, citizen hero, 480i, real play" /tmp/p0_z.log
$RUN /tmp/p0_zp.log 62 $Z REVA_PASSTIME=1 -- ./crowd_kms 100 packs_rvz $CIT 720x480
show "zombie, citizen hero, 480i, pass clock" /tmp/p0_zp.log

# 2. Kaeli as the hero, for her share in the pass clock
$RUN /tmp/p0_zk.log 75 $Z REVA_PASSTIME=1 -- ./crowd_kms 100 packs_rvz $KAELI 720x480
show "zombie, Kaeli hero, 480i, pass clock" /tmp/p0_zk.log

# 3. 240p with the aspect fixed, real play and pass clock
$RUN /tmp/p0_z240.log 62 $Z $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x240
show "zombie, citizen hero, 240p, real play" /tmp/p0_z240.log
$RUN /tmp/p0_z240p.log 62 $Z REVA_PASSTIME=1 -- ./crowd_kms 100 packs_rvz $CIT 720x240
show "zombie, citizen hero, 240p, pass clock" /tmp/p0_z240p.log

# 4. The off-switches: each pass's real cost in real play, with no finish in the way
$RUN /tmp/p0_zs0.log 62 $Z REVA_SHADOW=0 $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x480
show "zombie, citizen hero, 480i, sun shadow off" /tmp/p0_zs0.log
$RUN /tmp/p0_zp0.log 62 $Z REVA_POST=0 $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x480
show "zombie, citizen hero, 480i, post pass off" /tmp/p0_zp0.log
$RUN /tmp/p0_zsp0.log 62 $Z REVA_SHADOW=0 REVA_POST=0 $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x480
show "zombie, citizen hero, 480i, sun shadow and post off" /tmp/p0_zsp0.log

# 5. The 4-body mystery: the same floor, benchmark seed against real seed, under the shim
F="REVA_NOSAVE=1 REVA_TIER=pi GPUAT=70"
$RUN /tmp/p0_f4r.log 105 $F $SHIM -- ./crowd_kms 4 packs select 720x480
show "dance floor, 4 bodies, 480i, real play, random seed" /tmp/p0_f4r.log
$RUN /tmp/p0_f4b.log 105 $F $SHIM -- ./crowd_kms 4 packs select 720x480 bench
show "dance floor, 4 bodies, 480i, bench seed and glFinish" /tmp/p0_f4b.log
systemctl is-active recompilator; vcgencmd measure_temp; vcgencmd get_throttled
