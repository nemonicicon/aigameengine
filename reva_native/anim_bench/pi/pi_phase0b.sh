#!/bin/bash
# pi_phase0b.sh <section> -- the second half of Phase 0 on the tube. Runs on the Pi, as nemonic, one run at a time. In-spec NTSC modes only.
# Sections: labs pass hash floor prio
cd ~/engine_test/reva_native || exit 1
RUN="sudo systemd-run --unit=enginetest --collect --wait --quiet bash /home/nemonic/engine_test/pi_run.sh"
SHIM=LD_PRELOAD=/home/nemonic/engine_test/gl_shim.so
Z="REVA_NOSAVE=1 REVA_MODE=9 REVA_AUTOVAR=1 REVA_TIER=pi GPUAT=30"
CIT=packs/v1_Character_17_Female_Police.rpack
show(){ echo "=== $1"; grep -E "^\[run\] (min|engine exited|low)|FATAL|Segmentation|^\[gpu\]" "$2" | grep -v 'tfu' | cut -c1-130 | head -6; grep -E "^fps" "$2" | awk '{printf "%s/%s  ", $2, $4} END {print ""}' | cut -c1-170; grep -E "^\[frames\]" "$2" | tail -1 | cut -c1-130; grep -E "^\[pass\]" "$2" | tail -1 | cut -c1-420; grep -E "^\[shim\]" "$2" | tail -1 | cut -c60-210; }

labs(){   # the benches, GL against Vulkan's direct road, with the live runs
  for m in 480i 240p; do
    $RUN /tmp/lab4_GL_$m.log 150 X=1 -- bash -c "cd ../lab && exec ./v3d_lab2 $m citizen.lab"
    $RUN /tmp/lab4_VK_$m.log 150 X=1 -- bash -c "cd ../lab && exec ./vk_lab $m citizen.lab kms"
    for a in GL VK; do echo "=== $a $m"; grep -E " work |^LIVE|FAILED|FATAL|setcrtc|dumb|addfb|prime|direct road" /tmp/lab4_${a}_$m.log | sed -E "s/ work +/ /; s/ med +/ /; s/ p95 +/ /; s/ max \| cpu +/ cpu /; s/ \| gpu bin +/ bin /; s/ render +/ ren /; s/ \| jobs +/ jobs /; s/ \| over.*//" | cut -c1-170; done
  done
}
pass(){   # the zombie game, a finish only where the framebuffer changes, and each pass's off switch
  $RUN /tmp/q_z.log 62 $Z REVA_PASSTIME=2 -- ./crowd_kms 100 packs_rvz $CIT 720x480
  show "zombie, citizen, 480i, pass clock by framebuffer" /tmp/q_z.log
  $RUN /tmp/q_zn.log 62 $Z REVA_NOHOLLY=1 $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x480
  show "zombie, citizen, 480i, real play, head pass off" /tmp/q_zn.log
  $RUN /tmp/q_zns.log 62 $Z REVA_NOHOLLY=1 REVA_SHADOW=0 $SHIM -- ./crowd_kms 100 packs_rvz $CIT 720x480
  show "zombie, citizen, 480i, real play, head pass and sun shadow off" /tmp/q_zns.log
  $RUN /tmp/q_znsp.log 62 $Z REVA_NOHOLLY=1 REVA_SHADOW=0 REVA_PASSTIME=2 -- ./crowd_kms 100 packs_rvz $CIT 720x480
  show "zombie, citizen, 480i, head and sun off, pass clock" /tmp/q_znsp.log
}
hash(){   # the same door, same packs, same seed, no input: the sim's hash for the compare with the PC
  $RUN /tmp/h_kms.log 150 REVA_NOSAVE=1 REVA_MODE=9 REVA_AUTOVAR=1 REVA_TIER=pi REVA_FIXEDSTEP=60 REVA_SIMHASH=60 -- ./crowd_kms 100 packs_rvz $CIT 720x480
  grep -a '^\[simhash\]' /tmp/h_kms.log > /tmp/hash_pi_kms.txt; wc -l /tmp/hash_pi_kms.txt; head -2 /tmp/hash_pi_kms.txt; tail -1 /tmp/hash_pi_kms.txt
}
floor(){   # the 4-body question: the same seed, game-grade bodies against film-grade ones, on the real clock
  $RUN /tmp/f_rvz.log 95 REVA_NOSAVE=1 REVA_TIER=pi REVA_SEED=12345 GPUAT=30 $SHIM -- ./crowd_kms 4 packs_rvz select 720x480
  show "floor, 4 bodies, citizens only, seed 12345" /tmp/f_rvz.log
  $RUN /tmp/f_film.log 95 REVA_NOSAVE=1 REVA_TIER=pi REVA_SEED=12345 GPUAT=30 $SHIM -- ./crowd_kms 4 packs_film select 720x480
  show "floor, 4 bodies, film meshes only, seed 12345" /tmp/f_film.log
}
prio(){   # real-time priority, on the shadow-off game (close to the line) and on the live lab, with a compile running beside it as the worst neighbour
  N="REVA_NOHOLLY=1 REVA_SHADOW=0"
  $RUN /tmp/r_base.log 62 $Z $N -- ./crowd_kms 100 packs_rvz $CIT 720x480
  show "shadow-off game, plain" /tmp/r_base.log
  $RUN /tmp/r_rt.log 62 $Z $N -- chrt -f 50 ./crowd_kms 100 packs_rvz $CIT 720x480
  show "shadow-off game, real-time priority" /tmp/r_rt.log
  # a stand-in for the performance governor that changes no setting: an idle-class spin on the last core keeps the one shared clock up under ondemand
  taskset -c 3 chrt -i 0 sh -c 'while :; do :; done' & SPIN=$!
  $RUN /tmp/r_spin.log 62 $Z $N -- chrt -f 50 ./crowd_kms 100 packs_rvz $CIT 720x480
  kill $SPIN
  show "shadow-off game, real-time priority, clock held up by a spin" /tmp/r_spin.log
  LIVE="LAB_LIVEONLY=1800"
  $RUN /tmp/r_lab.log 40 $LIVE -- bash -c "cd ../lab && exec ./v3d_lab2 480i citizen.lab"
  echo "=== lab live, plain"; grep -E "^LIVE" /tmp/r_lab.log | cut -c1-170
  ( cd /tmp && nice -n 19 gcc -O2 -c -o /tmp/noise.o ~/engine_test/reva_native/crowd_kms.c -DREVA_SDL 2>/dev/null ) &
  CC=$!; sleep 2
  $RUN /tmp/r_labc.log 40 $LIVE -- bash -c "cd ../lab && exec ./v3d_lab2 480i citizen.lab"
  echo "=== lab live, compile beside it, plain"; grep -E "^LIVE" /tmp/r_labc.log | cut -c1-170
  wait $CC
  ( cd /tmp && nice -n 19 gcc -O2 -c -o /tmp/noise.o ~/engine_test/reva_native/crowd_kms.c -DREVA_SDL 2>/dev/null ) &
  CC=$!; sleep 2
  $RUN /tmp/r_labcr.log 40 $LIVE -- bash -c "cd ../lab && exec chrt -f 50 ./v3d_lab2 480i citizen.lab"
  echo "=== lab live, compile beside it, real-time priority"; grep -E "^LIVE" /tmp/r_labcr.log | cut -c1-170
  wait $CC; rm -f /tmp/noise.o
}
gate(){   # THE GATE: 4 bodies, game-grade citizens, pinned seed, the real clock, no finish, 10 minutes at 480i, this model at stock speed
  $RUN /tmp/g_gate.log 625 REVA_NOSAVE=1 REVA_TIER=pi REVA_SEED=12345 GPUAT=300 -- chrt -f 50 ./crowd_kms 4 packs_rvz select 720x480
  show "THE GATE: 4 bodies, 10 minutes, 480i" /tmp/g_gate.log
  grep -E "^\[frames\]" /tmp/g_gate.log | awk '{print $3, $5}' | sort | uniq -c | sort -rn | head -5
}
phase1(){   # Phase 1's replay: 40 citizens, pinned seed, the real clock, no finish, 10 minutes at each mode, chosen by name through the whitelist. Reported, not a block
  for v in 480i 240p; do
    $RUN /tmp/p1_$v.log 625 REVA_NOSAVE=1 REVA_TIER=pi REVA_SEED=12345 REVA_VIDEO=$v GPUAT=300 -- ./crowd_kms 40 packs_rvz select
    show "replay: 40 citizens, 10 minutes, $v" /tmp/p1_$v.log
    grep -E "connector .*mode" /tmp/p1_$v.log | head -2 | cut -c1-80
    grep -E "^\[frames\]" /tmp/p1_$v.log | awk '{ if($4>0) n++ } END { print n+0, "windows of", NR, "held a late frame" }'
  done
}
split(){   # the labs' live runs, split screen included, at both modes
  for m in 480i 240p; do
    $RUN /tmp/s_GL_$m.log 60 LAB_LIVEONLY=600 -- bash -c "cd ../lab && exec ./v3d_lab2 $m citizen.lab"
    $RUN /tmp/s_VK_$m.log 60 LAB_LIVEONLY=600 -- bash -c "cd ../lab && exec ./vk_lab $m citizen.lab kms"
    for a in GL VK; do echo "=== $a $m"; grep -E "^LIVE|FAILED|FATAL" /tmp/s_${a}_$m.log | cut -c1-170; done
  done
}
for s in "$@"; do $s; done
systemctl is-active recompilator; vcgencmd measure_temp; vcgencmd get_throttled
