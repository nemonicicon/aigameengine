#!/usr/bin/env python3
"""showreel.py -- cycle the two-body repertoire on the live lab wall (the friend demo).

The lab plays one clip at a time; this driver is the playlist. It writes lab_ctl.txt
on a timer with an ever-increasing seq, walking reva2 + kaeli through the whole
authored two-body story: the five, the hug, the skip hand-in-hand, the kiss, and the
curl-up-and-purr closer. Each entry carries the EXACT staging its score was solved
for (pgap/pside/pyaw from the bake receipts -- a pair clip at the wrong gap is two
strangers acting at each other).

    python anim_bench/showreel.py [start_seq] [--proof DIR]

--proof DIR: on the FIRST cycle only, re-sends each entry at 60% of its dwell with a
shot= into DIR -- five mid-move frames, so the reel is verified on pixels without
racing the ctl file by hand. Runs until killed; the window keeps playing whatever
entry was last written.
"""
import os, sys, time

CTL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lab_ctl.txt")
COMMON = "grid=13 chains=1 chainstiff=0.06 chaindamp=0.93 once=0"

# (tag, ctl fragment, dwell seconds) -- staging from the bake receipts
REEL = [
    ("hi5",   "clip=z_hi5 pclip=z_hi5 pgap=1.063 pyaw=180 pside=0 hi5=1 aimy=1.2",              12),
    ("hug",   "clip=z_hug pclip=z_hug pgap=0.47 pyaw=180 pside=0 hi5=0 aimy=0.75",              13),
    ("skip",  "clip=z_rm_skiphand pclip=z_rm_skiphand pgap=0 pyaw=0 pside=-0.52 hi5=2 aimy=0.3", 14),
    ("kiss",  "clip=z_kiss pclip=z_kiss pgap=0.47 pyaw=180 pside=0 hi5=3 aimy=1.18 reset=1",     14),
    ("sleep", "clip=z_rm_cuddleseat pclip=z_rm_curlsleep pgap=0.55 pyaw=180 pside=0 hi5=0 aimy=0.0", 20),
]

def write(seq, frag, shot=None):
    line = "seq=%d %s %s" % (seq, COMMON, frag)
    if shot:
        line += " shot=%s" % shot
    with open(CTL, "w") as f:
        f.write(line + "\n")

def main():
    args = sys.argv[1:]
    proof = None
    if "--proof" in args:
        i = args.index("--proof"); proof = args[i + 1]; del args[i:i + 2]
    seq = int(args[0]) if args else 100
    # the partner loads ONCE -- reloading her pack every entry is churn the lab
    # does not need; g_labPartner persists across ctl lines
    write(seq, "partner=kmoves " + REEL[0][1]); seq += 1
    print("[reel] partner up; cycling %d moves" % len(REEL)); sys.stdout.flush()
    time.sleep(2.0)
    while True:
        for tag, frag, dur in REEL:
            write(seq, frag); seq += 1
            print("[reel] %s (%ds)" % (tag, dur)); sys.stdout.flush()
            if proof:
                # a mid-move proof frame, on the first cycle only
                time.sleep(dur * 0.6)
                write(seq, frag, shot="%s/reel_%s.ppm" % (proof, tag)); seq += 1
                time.sleep(dur * 0.4)
            else:
                time.sleep(dur)
        proof = None

if __name__ == "__main__":
    main()
