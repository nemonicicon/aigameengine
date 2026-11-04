"""hashcheck.py -- does the sim still do exactly what it did, and does it still ask for the same picture? Runs the no-display engine (crowd_headless) through a short list of scenarios
and compares each [simhash] and [packet] line against the blessed copy in hash_refs/. Seconds, not minutes, no window, and the same answer on Windows, Linux and the Pi: that's the engine's
contract (engine plan, Phases 1 and 2). A [packet] line that parts with the [simhash] line holding says the rules are the same and what's drawn changed: REVA_PACKETDUMP=<tick>:<file> gives both runs' frames as text to diff.

usage: hashcheck.py                  compare with hash_refs/
       hashcheck.py --bless          adopt this run, after you know why it changed
       hashcheck.py --only NAME      one scenario
       hashcheck.py --exe PATH       a different build
Run from reva_native. Build first: -DREVA_HEADLESS, see CLAUDE.md. packs_rvz/ must exist: the 77 packs of the zombie game, v1_Character* and v1_moves (made on request with --links).
--game sample checks the sample game instead, which is what the kit has: built with games/sample/sample.c, and run from its own folder so it sees no content but its own body. --game rocks is the 2D game, --game tower the 3D action one, --game herd the crowd one and --game roam the streaming world, the same way. Roam's cells are made by its make_assets.py, not kept in git.
A scenario marked `cadence` also runs at odd tick patterns, which must print the same lines (a packet exists only for a tick that draws, so those compare on the ticks both have).
A mismatch prints the first tick that parted; REVA_SIMDUMP=<tick>:<file> then names the value."""
import argparse
import glob
import math
import os
import struct
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
REFS = os.path.join(HERE, "hash_refs")
HERO = "packs/v1_Character_17_Female_Police.rpack"
Z = {"REVA_MODE": "9", "REVA_AUTOVAR": "1", "REVA_TIER": "pi"}
# name, env, argv after the exe, ticks, cadence checks
SCENARIOS = [
    ("zombie_noinput", dict(Z), ["100", "packs_rvz", HERO], 3600, ["1,1,1,1,2", "2,2,3"]),
    ("zombie_tape", dict(Z, REVA_REPLAY="anim_bench/hash_refs/zombie_run.tape"), ["100", "packs_rvz", HERO], 1800, ["1,1,1,1,2"]),
    ("floor_4", {"REVA_MODE": "0", "REVA_AUTOVAR": "0", "REVA_TIER": "pi"}, ["4", "packs_rvz", HERO], 1800, []),
    # the biggest letters: what's drawn has its own lines, and the rules are the game's own (SIM_AS)
    ("zombie_read", dict(Z, REVA_READING="large"), ["100", "packs_rvz", HERO], 1800, []),
]
# the sample game, from its own folder (games/sample)
BODY = "packs/v1_sample.rpack"
SG = {"REVA_MODE": "9", "REVA_TIER": "pi"}
SCENARIOS_SAMPLE = [
    ("sample_noinput", dict(SG), ["12", "packs", BODY], 3600, ["1,1,1,1,2", "2,2,3"]),
    ("sample_tape", dict(SG, REVA_REPLAY="../../anim_bench/hash_refs/sample_run.tape"), ["12", "packs", BODY], 1800, ["1,1,1,1,2"]),
    ("sample_floor", {"REVA_MODE": "0", "REVA_TIER": "pi"}, ["4", "packs", BODY], 1800, []),
    ("sample_lab", {"REVA_MODE": "11", "REVA_TIER": "pi", "REVA_LABCTL": "no_control_file"}, ["4", "packs", BODY], 900, []),
    # the same body as RPK2 (anim_bench/golden): its textures are all that differ, so it answers to sample_noinput's own lines
    ("sample_rpk2", dict(SG), ["12", "../../anim_bench/golden/rpk2", "../../anim_bench/golden/rpk2/v1_sample.rpack"], 3600, []),
    # and with its clips in a chunk and not in its body: the same again
    ("sample_chunks", dict(SG), ["12", "../../anim_bench/golden/chunks", "../../anim_bench/golden/chunks/v1_sample.rpack"], 3600, []),
    # a first boot: every step of setup by tape, then the game
    ("sample_setup", dict(SG, REVA_SETUP="1", REVA_REPLAY="../../anim_bench/hash_refs/sample_setup.tape"), ["12", "packs", BODY], 1800, []),
    ("sample_read", dict(SG, REVA_READING="large"), ["12", "packs", BODY], 1800, []),
]
SAME_AS = {"sample_rpk2": "sample_noinput", "sample_chunks": "sample_noinput"}
SIM_AS = {"zombie_read": "zombie_noinput", "sample_read": "sample_noinput"}   # the same rules, whatever's drawn
# the 2D game, from its own folder: no bodies and no packs
SCENARIOS_ROCKS = [
    ("rocks_noinput", dict(SG), ["0", "packs"], 3600, ["1,1,1,1,2", "2,2,3"]),
    ("rocks_tape", dict(SG, REVA_REPLAY="../../anim_bench/hash_refs/rocks_run.tape"), ["0", "packs"], 1800, ["1,1,1,1,2"]),
]


SCENARIOS_HERD = [
    ("herd_noinput", dict(SG), ["200", "packs", "packs/v1_herd.rpack"], 1800, ["1,1,1,1,2"]),
    ("herd_tape", dict(SG, REVA_REPLAY="../../anim_bench/hash_refs/herd_run.tape"), ["200", "packs", "packs/v1_herd.rpack"], 1200, ["1,1,1,1,2"]),
]
SCENARIOS_ROAM = [
    ("roam_noinput", dict(SG), ["0", "packs", "packs/v1_tower.rpack"], 1800, ["1,1,1,1,2"]),
]
SCENARIOS_TALK = [
    ("talk_noinput", dict(SG), ["4", "packs", "packs/v1_talk.rpack"], 1200, ["1,1,1,1,2", "2,2,3"]),
]
SCENARIOS_PEOPLE = [
    ("people_noinput", dict(SG), ["8", "packs", "packs/v1_boot.rpack"], 900, ["1,1,1,1,2", "2,2,3"]),
]
SCENARIOS_TOWER = [
    ("tower_noinput", dict(SG), ["10", "packs", "packs/v1_tower.rpack"], 3600, ["1,1,1,1,2", "2,2,3"]),
    ("tower_tape", dict(SG, REVA_REPLAY="../../anim_bench/hash_refs/tower_run.tape"), ["10", "packs", "packs/v1_tower.rpack"], 1800, ["1,1,1,1,2"]),
]


def make_tape(path, ticks=1800):
    """A session by formula, in the tape's own format: run in curves, fire in bursts, press restart now and then. The floats go in as exact bits."""
    bits = lambda v: "%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v2  seed 12345  written by hashcheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv; floats as bits)\n")
        for t in range(1, ticks + 1):
            lx = 0.85 * math.sin(t * 0.013) if t > 60 else 0.0
            ly = -0.85 * math.cos(t * 0.007) if t > 60 else 0.0
            a = 1 if t > 120 and t % 90 < 4 else 0
            x = 1 if t % 900 < 2 else 0
            f.write(f"{t} {bits(lx)} {bits(ly)} {bits(0.0)} {bits(0.0)} {bits(0.0)} {a} 0 {x} 0 0 0 0 0 0 {bits(0.0)} {bits(0.0)} 1 1 0\n")
    print("tape:", path, ticks, "ticks")


def make_setup_tape(path):
    """setup's every step, in order: yes to the picture, keep 240p, move all four edges, stretch the circle, the two level screens, the biggest letters, all the test patterns, then play"""
    bits = lambda v: "%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    presses = {60: "a", 120: "a", 180: "a", 300: "a", 380: "a", 460: "a", 540: "a", 640: "a", 700: "a", 760: "a", 810: "a", 820: "x", 880: "a", 940: "a", 1000: "a", 1060: "b", 1120: "a"}
    sticks = [(240, 260, -0.9, 0.0), (320, 340, 0.9, 0.0), (400, 420, 0.0, 0.9), (480, 500, 0.0, -0.9), (560, 600, -0.9, 0.0), (775, 785, 0.0, 0.9), (790, 800, 0.0, 0.9)]   # g_lx is the stick's right, negated
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v2  seed 12345  written by hashcheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv; floats as bits)\n")
        for t in range(1, 1801):
            btn = next((presses[k] for k in presses if k <= t < k + 2), "")
            lx = ly = 0.0
            for t0, t1, x, y in sticks:
                if t0 <= t < t1:
                    lx, ly = x, y
            f.write(f"{t} {bits(lx)} {bits(ly)} {bits(0.0)} {bits(0.0)} {bits(0.0)} {int(btn == 'a')} {int(btn == 'b')} {int(btn == 'x')} 0 0 0 0 0 0 {bits(0.0)} {bits(0.0)} 1 1 0\n")
    print("tape:", path)


def make_tower_tape(path):
    """up the ramp and across the first deck, a jump, two punches, a turn of the lens, and on up the stairs and over the bridge"""
    bits = lambda v: "%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    zero = bits(0.0)
    legs = [(60, 0.8, 0.0), (200, 0.8, -0.6), (420, -0.8, -0.3), (560, 0.0, -0.9), (760, -0.9, 0.0), (980, 0.0, 0.9), (1200, 0.0, 0.0), (1800, 0.5, 0.5)]
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v3  seed 12345  written by hashcheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv, then pads 1 to 3: on lx ly rx ry rt held; floats as bits)\n")
        for t in range(1, 1801):
            lx = ly = 0.0
            for end, x, y in legs:
                if t <= end:
                    lx, ly = -x, y   # g_lx is the stick's right, negated
                    break
            a = 1 if t in (300, 301) or t in (900, 901) else 0
            x = 1 if t % 240 in (120, 121) else 0
            rx = 0.5 if 1000 < t < 1100 else 0.0
            line = f"{t} {bits(lx)} {bits(ly)} {bits(rx)} {zero} {zero} {a} 0 {x} 0 0 0 0 0 0 {zero} {zero} 1 1 0"
            for t0, t1, rate in ((400, 1400, 0.017), (800, 99999, 0.023), (1000, 99999, 0.031)):
                on = t0 <= t < t1
                px = 0.8 * math.sin(t * rate) if on else 0.0
                py = 0.8 * math.cos(t * rate) if on else 0.0
                held = 0
                if on and t0 + 10 <= t < t0 + 12:
                    held = 1   # PAD_A: joins
                elif on and t % 200 in (50, 51):
                    held = 4   # PAD_X: a punch
                line += f" {int(on)} {bits(px)} {bits(py)} {zero} {zero} {zero} {held:x}"
            f.write(line + "\n")
    print("tape:", path)


def make_herd_tape(path):
    """wander the cursor about with the button held, and let go now and then"""
    bits = lambda v: "%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    zero = bits(0.0)
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v2  seed 12345  written by hashcheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv; floats as bits)\n")
        for t in range(1, 1201):
            lx, ly = 0.7 * math.sin(t * 0.009), -0.7 * math.cos(t * 0.013)
            a = 1 if t % 300 < 240 else 0
            f.write(f"{t} {bits(lx)} {bits(ly)} {zero} {zero} {zero} {a} 0 0 0 0 0 0 0 0 {zero} {zero} 1 1 0\n")
    print("tape:", path)


def make_rocks_tape(path):
    """sweep across the screen, fire in bursts, press play again now and then. Pad 1 is plugged in at tick 200 and joins, and pulled at 1200; pad 2 comes in at 700; pad 3 stays out of it"""
    bits = lambda v: "%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
    zero = bits(0.0)
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v3  seed 12345  written by hashcheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv, then pads 1 to 3: on lx ly rx ry rt held; floats as bits)\n")
        for t in range(1, 1801):
            lx = 0.9 * math.sin(t * 0.021) if t > 30 else 0.0
            a = 1 if (t // 25) % 3 else 0
            b = 1 if t % 300 < 2 else 0
            line = f"{t} {bits(lx)} {zero} {zero} {zero} {zero} {a} {b} 0 0 0 0 0 0 0 {zero} {zero} 1 1 0"
            for k, t0, t1, rate in ((1, 200, 1200, 0.017), (2, 700, 99999, 0.031), (3, 99999, 99999, 0.0)):
                on = t0 <= t < t1
                x = 0.8 * math.sin(t * rate) if on else 0.0
                held = 1 if on and t > t0 + 40 and (t // 20) % 2 else 0   # PAD_A
                line += f" {int(on)} {bits(x)} {zero} {zero} {zero} {zero} {held:x}"
            f.write(line + "\n")
    print("tape:", path)


def run(exe, env_extra, args, ticks, cwd=ROOT):
    env = dict(os.environ, REVA_NOSAVE="1", REVA_FIXEDSTEP="60", REVA_SIMHASH="60", REVA_EXITTICK=str(ticks), **env_extra)
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    t0 = time.time()
    p = subprocess.run([exe] + args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", timeout=900)
    lines = [l for l in p.stdout.splitlines() if l.startswith("[simhash]") or l.startswith("[packet]")]
    return lines, time.time() - t0, p


def first_part(a, b):
    for x, y in zip(a, b):
        if x != y:
            return x.split()[0] + " " + x.split()[2]
    return None if len(a) == len(b) else "the end (%d lines against %d)" % (len(a), len(b))


def cadence_part(c, base):
    """the sim's lines exactly, the packet's on the ticks both drew"""
    sim = lambda ls: [l for l in ls if l.startswith("[simhash]")]
    part = first_part(sim(c), sim(base))
    if part:
        return part
    got = {l.split()[2]: l for l in c if l.startswith("[packet]")}
    for l in base:
        if l.startswith("[packet]") and l.split()[2] in got and got[l.split()[2]] != l:
            return "[packet] " + l.split()[2]
    return None


def links():
    d = os.path.join(ROOT, "packs_rvz")
    os.makedirs(d, exist_ok=True)
    for f in glob.glob(os.path.join(ROOT, "packs", "v1_Character*.rpack")) + [os.path.join(ROOT, "packs", "v1_moves.rpack")]:
        t = os.path.join(d, os.path.basename(f))
        if not os.path.exists(t):
            os.link(f, t)
    print("packs_rvz:", len(os.listdir(d)), "packs, linked, no disk used")


ap = argparse.ArgumentParser()
ap.add_argument("--bless", action="store_true")
ap.add_argument("--only")
ap.add_argument("--links", action="store_true")
ap.add_argument("--make-tape", action="store_true")
ap.add_argument("--exe", default=os.path.join(ROOT, "crowd_headless.exe" if os.name == "nt" else "crowd_headless"))
ap.add_argument("--game", default="rvz" if os.path.exists(os.path.join(ROOT, "games", "rvz", "rvz.c")) else "sample")
a = ap.parse_args()
a.exe = os.path.abspath(a.exe)
CWD = ROOT if a.game == "rvz" else os.path.join(ROOT, "games", a.game)
if a.links:
    links()
if a.game == "roam" and not os.path.exists(os.path.join(CWD, "sets", "roam.set")):
    sys.exit("roam's cells aren't in git: run games/roam/make_assets.py first")
if not os.path.exists(a.exe):
    sys.exit("no %s: build it first, see CLAUDE.md (the no-display build)" % a.exe)
os.makedirs(REFS, exist_ok=True)
if a.make_tape:
    if a.game == "rocks":
        make_rocks_tape(os.path.join(REFS, "rocks_run.tape"))
    elif a.game == "tower":
        make_tower_tape(os.path.join(REFS, "tower_run.tape"))
    elif a.game == "herd":
        make_herd_tape(os.path.join(REFS, "herd_run.tape"))
    else:
        make_tape(os.path.join(REFS, ("sample_run" if a.game == "sample" else "zombie_run") + ".tape"))
        make_setup_tape(os.path.join(REFS, "sample_setup.tape"))
bad = 0
for name, env, args, ticks, cadences in {"rvz": SCENARIOS, "sample": SCENARIOS_SAMPLE, "rocks": SCENARIOS_ROCKS, "tower": SCENARIOS_TOWER, "talk": SCENARIOS_TALK, "people": SCENARIOS_PEOPLE, "roam": SCENARIOS_ROAM, "herd": SCENARIOS_HERD}[a.game]:
    if a.only and a.only != name:
        continue
    if "REVA_REPLAY" in env and not os.path.exists(os.path.join(CWD, env["REVA_REPLAY"])):
        print(f"{name}: SKIPPED, no tape at {env['REVA_REPLAY']}")
        continue
    lines, secs, p = run(a.exe, env, args, ticks, CWD)
    ref = os.path.join(REFS, SAME_AS.get(name, name) + ".txt")
    if not lines:
        print(f"{name}: NO HASH LINES, exit {p.returncode}\n" + "\n".join(p.stdout.splitlines()[-8:]))
        bad += 1
        continue
    if a.bless and name not in SAME_AS:
        with open(ref, "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        print(f"{name}: blessed, {len(lines)} lines to tick {ticks}, {secs:.1f} s")
    elif not os.path.exists(ref):
        print(f"{name}: no reference yet, run --bless")
        bad += 1
    else:
        want = open(ref).read().splitlines()
        part = first_part(lines, want)
        print(f"{name}: " + ("same" if part is None else f"⛔ DIFFERS, first at tick {part}") + f", {len(lines)} lines, {secs:.1f} s")
        bad += part is not None
    if name in SIM_AS:
        sim = lambda ls: [l for l in ls if l.startswith("[simhash]") and "(end)" not in l]
        mine = sim(lines)
        part = first_part(mine, sim(open(os.path.join(REFS, SIM_AS[name] + ".txt")).read().splitlines())[:len(mine)])
        print(f"  the rules against {SIM_AS[name]}: " + ("same" if part is None else f"⛔ DIFFER, first at tick {part}"))
        bad += part is not None
    for c in cadences:
        l2, s2, _ = run(a.exe, dict(env, REVA_CADENCE=c), args, ticks, CWD)
        part = cadence_part(l2, lines)
        print(f"  cadence {c}: " + ("same" if part is None else f"⛔ DIFFERS, first at tick {part}") + f", {s2:.1f} s")
        bad += part is not None
print("⛔ %d difference(s)" % bad if bad else "all same")
sys.exit(1 if bad else 0)
