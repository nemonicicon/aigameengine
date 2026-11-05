#!/usr/bin/env python3
"""fuzzcheck.py -- the engine's file readers, fed broken files (queue step 28: "fuzz-tested file readers").

A file the engine reads can come from anywhere: a save a player edited, a pack somebody made, a tape somebody sent, a language file off a forum. A reader that
believes a number in a file is how a file becomes somebody else's code running on your machine. So this takes each kind of file a game reads, breaks it thousands
of ways, and runs the whole engine on each broken copy, built with the compiler's memory and undefined-behaviour checkers. The engine may refuse a broken file,
say so, draw nothing: that's right. It may not crash, hang, or touch memory that isn't its own.

It runs the real engine, on its own rasterizer with no window (gfx_soft.c), in a copy of the game's own folder with one file broken, so a broken pack is not
only read but skinned and drawn. Nothing here is a model of a reader: it's the reader, reached the way a game reaches it.

    python3 anim_bench/fuzzcheck.py --build              build the checked engines (gcc, -fsanitize=address,undefined), into --work
    python3 anim_bench/fuzzcheck.py                      every kind, a few hundred broken copies each: a few minutes on a dozen cores
    python3 anim_bench/fuzzcheck.py --runs 2000 --only pack,save     more of some
    python3 anim_bench/fuzzcheck.py --seed 7             other dice. The same seed breaks the same files the same ways, on any machine
    python3 anim_bench/fuzzcheck.py --replay FOUND.bin --kind pack   one kept file again, with everything the engine and the checkers said
    --work DIR   where the builds, the scratch folders and what's found go (default: /build/fuzz if /build is there, else a folder in the temp directory)
    --jobs N     how many at once (default: every core)

It needs gcc's sanitizers, which Windows' gcc hasn't got: from the PC, run it in the build container, where /src is reva_native:
    docker exec revalinux python3 /src/anim_bench/fuzzcheck.py --build
    docker exec revalinux python3 /src/anim_bench/fuzzcheck.py
What it finds is kept under <work>/found/<kind>/, the broken file and what was said, one of each distinct fault. It exits 1 if anything was found.
"""
import argparse
import concurrent.futures
import hashlib
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SG = {"REVA_MODE": "9", "REVA_TIER": "pi"}

SAVE_SEEDS = {
    "save/tag_auto.txt": "version 1\n# a save: key=value, one a line, yours to edit\nbest=660\nround=2\n",
    "save/display.txt": "mode 480i\nedges 480i 36 36 24 24\nedges 240p 36 36 12 12\nstretch 1.000\nok240 1\nallow50 0\nreading medium\n",
    "save/controls.txt": "# an action, then its buttons\nrun A\np2 run B\njump X Y\n",
    "save/graphics.txt": "# what this machine draws with\nmsaa 2\nhdr 0\nlights 8\nparticles 512\nwidth 640\nheight 480\naspect 4:3\nfullscreen 0\n",
    "save/language.txt": "es\n",
    "save/boot.txt": "3\n",
}


def tvw_seed():
    """a small file of wide glyphs, as skate_src/author/fontbake.py writes one"""
    cps = [0x3042, 0x3044, 0x4E00, 0x4E8C, 0x5206, 0x70B9, 0xAC00, 0xFF01]
    rows = [0x8001 if y in (0, 15) else 0x0180 for y in range(16)]
    return b"TVW1" + struct.pack("<IHH", len(cps), 16, 0) + struct.pack("<%dH" % len(cps), *cps) + b"".join(struct.pack(">16H", *rows) for _ in cps)


def wav_seed():
    """a tenth of a second of a tone, 16 bits, one channel"""
    import math
    n = 2205
    pcm = b"".join(struct.pack("<h", int(8000 * math.sin(i * 0.2))) for i in range(n))
    return b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 22050, 44100, 2, 16) + b"data" + struct.pack("<I", len(pcm)) + pcm


SET_SEED = """# a set with one of nearly every kind of line, for the fuzzer. Its one prop is the sample game's own body
climb
sun 42 150 1.25 1.0 0.96 0.90
sky 0.42 0.50 0.64
ground 0.30 0.28 0.26
exposure 1.1
haze 0.02
far 400
wind 1 0 0.5
prop v1_sample 0 0 -148 0 1
propr v1_sample 3 0 -147 20 5 0 1.2
actor v1_sample idle 4 0 -146 180 1
light -4.5 2.6 -147 1.0 0.55 0.25 6
spot 0 4 -148 0 -1 0 1 0.9 0.8 8 0.8 0.9 1
mark here 1 0 -149 0 stand
path 0 -150 2 -148
cue 1.5 idle
look 0 1.6 -148
water 0 0.1 -146 4 3 0.1 0.3 0.4
mirror 2 1 -149 0 1.5 2
gloss 0 0.2 0.1 0.6
solid -1 0 -149 1 1 -148
grass 0 -146 6 4 20
scatter v1_sample 5 -146 3 2 4
beam 0 4 -148 0 0 -148 0.2 1 1 0.8 0.6 2 0.1 0.6 1
beamlight 1
emitter fire 1 0.2 -148
decal 0 0.5 0.01 -148 0 -1 0 1.2
star 0 1 0 0.01 1 1 1 1
track fz 0
mover v1_sample 0 0 -148
lens roll=5
film sepia=0.2 grain=0.1
"""
CTL_SEED = ("seq=1 face=0 grid=0 dqs=0 set=fz clip=idle cam=3,2,-145,0,1.2,-148,40,5 sun=40,150,1.2,1.0,0.96,0.90 sky=0.44,0.50,0.62 tod=14 jaw=0.5 look=1 "
            "focus=4,1 shutter=0.5 sepia=0.2 grain=0.1 flash=0 burn=0 weave=0.001,0.001 trackt=play words=1 lang=es outfit=bare eyes=1,0.2,0.8,1.5 aimy=1.5\n")
LAB = {"REVA_MODE": "11", "REVA_TIER": "pi", "REVA_LABCTL": "lab_ctl.txt"}


def trk_seed():
    """a set's timeline, as scenes/everything.py writes one: a line of counts, then every frame's floats"""
    frames, stride = 30, 3 + 6 + 13 + 15 + 4 + 9 + 12
    rows = b"".join(struct.pack("<%df" % stride, *[0.4 + 0.01 * ((f * 7 + k) % 23) for k in range(stride)]) for f in range(frames))
    return b"TRK4 30 %d 1 1 1 1 1 1\n" % frames + rows


# kind: the game whose folder it runs in, the file that's broken (in that folder), what's put there first if the game hasn't one, the engine's argv, its env, how many ticks, text or not
TARGETS = {
    "pack":     dict(game="sample", file="packs/v1_sample.rpack", args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=90, text=False),
    "pack2":    dict(game="sample", file="packs/v1_sample.rpack", seed_from="anim_bench/golden/rpk2/v1_sample.rpack", args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=90, text=False),
    "chunks":   dict(game="sample", file="packs/v1_sample.rpack", seed_from="anim_bench/golden/chunks/v1_sample.rpack", args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=90, text=False),
    "lang":     dict(game="sample", file="lang/es.txt", args=["12", "packs", "packs/v1_sample.rpack"], env=dict(SG, REVA_LANG="es"), ticks=60, text=True),
    "wide":     dict(game="sample", file="data/fz.tvw", seed=tvw_seed, also={"lang/zz.txt": "name Z\nwide fz\n\"SCORE %d\" = \"分 %d\"\n\"PRESS A TO PLAY\" = \"あい一二！\"\n"}, args=["12", "packs", "packs/v1_sample.rpack"], env=dict(SG, REVA_LANG="zz"), ticks=60, text=False),
    "save":     dict(game="sample", file="save/tag_auto.txt", saves=True, args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=60, text=True),
    "display":  dict(game="sample", file="save/display.txt", saves=True, args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=60, text=True),
    "controls": dict(game="sample", file="save/controls.txt", saves=True, args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=60, text=True),
    "graphics": dict(game="sample", file="save/graphics.txt", saves=True, args=["12", "packs", "packs/v1_sample.rpack"], env=SG, ticks=60, text=True),
    "tape":     dict(game="sample", file="run.tape", seed_from="anim_bench/hash_refs/sample_run.tape", args=["12", "packs", "packs/v1_sample.rpack"], env=dict(SG, REVA_REPLAY="run.tape"), ticks=240, text=True),
    "talk":     dict(game="talk", file="talk/hello.talk", args=["4", "packs", "packs/v1_talk.rpack"], env=SG, ticks=420, text=True),
    "talklang": dict(game="talk", file="lang/es.txt", args=["4", "packs", "packs/v1_talk.rpack"], env=dict(SG, REVA_LANG="es"), ticks=420, text=True),
    "graph":    dict(game="talk", file="sit.anim", args=["4", "packs", "packs/v1_talk.rpack"], env=dict(SG, TALK_SIT="1"), ticks=120, text=True),
    "set":      dict(game="talk", file="sets/talk.set", args=["4", "packs", "packs/v1_talk.rpack"], env=SG, ticks=120, text=True),
    "prop":     dict(game="talk", file="props/tk_room.rpack", args=["4", "packs", "packs/v1_talk.rpack"], env=SG, ticks=120, text=False),
    "face":     dict(game="talk", file="packs/v1_talk.rpack", args=["4", "packs", "packs/v1_talk.rpack"], env=SG, ticks=300, text=False),
    "wav":      dict(game="talk", file="voice/a1.wav", args=["4", "packs", "packs/v1_talk.rpack"], env=SG, ticks=300, text=False),
    "level":    dict(game="tower", file="sets/tower.set", args=["10", "packs", "packs/v1_tower.rpack"], env=SG, ticks=120, text=True),
    "herograph": dict(game="tower", file="hero.anim", args=["10", "packs", "packs/v1_tower.rpack"], env=SG, ticks=120, text=True),
    "music":    dict(game="tower", file="music.wav", args=["10", "packs", "packs/v1_tower.rpack"], env=SG, ticks=120, text=False),
    # what film reads, in the anim lab, with the sample game's body: a set, its timeline, a compiled scene, and the line that steers the lab
    "labset":   dict(game="sample", file="sets/fz.set", seed=lambda: SET_SEED.encode(), also={"lab_ctl.txt": "seq=1 face=0 grid=0 dqs=0 set=fz\n"}, args=["4", "packs", "packs/v1_sample.rpack"], env=LAB, ticks=90, text=True),
    "track":    dict(game="sample", file="sets/fz.trk", seed=trk_seed, also={"lab_ctl.txt": "seq=1 face=0 grid=0 dqs=0 set=fz trackt=play\n", "sets/fz.set": SET_SEED}, args=["4", "packs", "packs/v1_sample.rpack"], env=LAB, ticks=120, text=False),
    "scene":    dict(game="sample", file="scenes/fz.scn", seed_from="scenes/cam_test.scn", also={"lab_ctl.txt": "seq=1 face=0 grid=0 dqs=0 scene=fz\n"}, args=["4", "packs", "packs/v1_sample.rpack"], env=LAB, ticks=240, text=True),
    "ctl":      dict(game="sample", file="lab_ctl.txt", seed=lambda: CTL_SEED.encode(), also={"sets/fz.set": SET_SEED}, args=["4", "packs", "packs/v1_sample.rpack"], env=LAB, ticks=90, text=True),
}
GAMES = sorted({t["game"] for t in TARGETS.values()})
INTS = [0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 255, 256, 1023, 1024, 4095, 4096, 32767, 32768, 65535, 65536, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 0x7FFFFFFE, 0x40000000, 0xFFFF0000]
WORDS = ["-1", "0", "1", "2147483647", "-2147483648", "4294967296", "99999999999999999999", "1e308", "-1e308", "nan", "inf", "-inf", "1e-320", "0x7fffffff", "%s%s%s%s", "%n", "%999999d",
         "../../../../etc/passwd", "A" * 300, "A" * 5000, "", " ", "\t", "\"", "\\", "=", "#", "\x00", "\xff\xfe", "日本語", "/", "..", "con", "\r"]


def mutate_bin(b, rng):
    """a binary file broken one to four ways"""
    b = bytearray(b)
    for _ in range(rng.choice((1, 1, 1, 2, 2, 3, 4))):
        if not b:
            b = bytearray(rng.randbytes(rng.randint(1, 64)))
            continue
        how = rng.randrange(12)
        at = rng.randrange(len(b))
        if how == 0:
            b[at] ^= 1 << rng.randrange(8)
        elif how == 1:
            b[at] = rng.choice((0, 1, 0x7F, 0x80, 0xFF, rng.randrange(256)))
        elif how in (2, 3, 4):       # a whole number somewhere, replaced by one that readers get wrong. Headers are tried as often as the rest
            at = rng.randrange(min(len(b), 256)) if how == 2 else at
            at &= ~3
            v = rng.choice(INTS) if rng.random() < 0.7 else (len(b) + rng.randint(-8, 8)) & 0xFFFFFFFF
            b[at:at + 4] = struct.pack("<I", v)[:max(0, min(4, len(b) - at))]
        elif how == 5 and len(b) > 4:  # a 16-bit one
            at &= ~1
            b[at:at + 2] = struct.pack("<H", rng.choice(INTS) & 0xFFFF)[:max(0, min(2, len(b) - at))]
        elif how == 6:               # cut short
            del b[rng.randrange(len(b)):]
        elif how == 7:               # a piece gone
            n = rng.randint(1, max(1, min(len(b) - at, rng.choice((1, 4, 16, 64, 4096)))))
            del b[at:at + n]
        elif how == 8:               # a piece twice
            n = rng.randint(1, max(1, min(len(b) - at, rng.choice((4, 16, 64, 4096)))))
            b[at:at] = b[at:at + n]
        elif how == 9:               # a piece of noise
            n = rng.choice((1, 4, 16, 64))
            b[at:at + n] = rng.randbytes(n)
        elif how == 10:              # a float that isn't a number
            at &= ~3
            b[at:at + 4] = rng.choice((b"\x00\x00\xc0\x7f", b"\x00\x00\x80\x7f", b"\x00\x00\x80\xff", b"\xff\xff\x7f\x7f", b"\x01\x00\x00\x00"))[:max(0, min(4, len(b) - at))]
        else:                        # more on the end
            b += rng.randbytes(rng.choice((1, 16, 4096)))
    return bytes(b)


def mutate_text(b, rng):
    """a text file broken as a person or a bad tool breaks one"""
    if rng.random() < 0.15:
        return mutate_bin(b, rng)
    try:
        lines = b.decode("utf-8", "surrogateescape").split("\n")
    except Exception:
        return mutate_bin(b, rng)
    for _ in range(rng.choice((1, 1, 2, 3))):
        how = rng.randrange(10)
        i = rng.randrange(len(lines)) if lines else 0
        if not lines:
            lines = [rng.choice(WORDS)]
        elif how == 0:
            del lines[i]
        elif how == 1:
            lines.insert(i, lines[i])
        elif how in (2, 3, 4):       # one word of a line, replaced
            toks = re.split(r"(\s+|=|,|\")", lines[i])
            idx = [k for k, t in enumerate(toks) if t and not t.isspace()]
            if idx:
                toks[rng.choice(idx)] = rng.choice(WORDS)
            lines[i] = "".join(toks)
        elif how == 5:               # a number, made another
            lines[i] = re.sub(r"-?\d+(\.\d+)?", lambda m: rng.choice(WORDS[:13]) if rng.random() < 0.5 else m.group(0), lines[i])
        elif how == 6:               # a very long line
            lines[i] = lines[i] + (" " + rng.choice(("x", "9", "\"", "="))) * rng.choice((200, 3000, 20000))
        elif how == 7:               # the same line a thousand times
            lines[i:i] = [lines[i]] * rng.choice((20, 100, 400))
        elif how == 8:               # cut mid-line
            lines = lines[:i] + [lines[i][:rng.randrange(len(lines[i]) + 1)]]
        else:                        # a line nobody wrote
            lines.insert(i, " ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 9))))
    # a megabyte at most: bigger only measures how long reading takes. (And a set of a thousand copies of a room isn't a hang: it draws a thousand rooms, and ends)
    return "\n".join(lines).encode("utf-8", "surrogateescape")[:1 << 20]


def signature(out, rc, timed_out):
    """what kind of fault, so each is kept once: the checker's own first frames, or the line it named"""
    if timed_out:
        return "hang"
    m = re.search(r"ERROR: AddressSanitizer: (\S+)", out)
    if m:
        frames = re.findall(r"#\d+ 0x[0-9a-f]+ in (\S+)", out)
        own = [f for f in frames if not f.startswith("__") and "asan" not in f][:3]
        return "asan %s in %s" % (m.group(1), " < ".join(own) or "?")
    m = re.search(r"(\S+:\d+):\d+: runtime error: (.{0,60})", out)
    if m:
        return "ub %s %s" % (m.group(1), re.sub(r"0x[0-9a-f]+|-?\d{3,}", "N", m.group(2)))
    if "LeakSanitizer" in out:
        return None
    if rc < 0:
        return "signal %d" % -rc
    return None


def one(job):
    """a worker's turn: its own copy of the game's folder, the file broken, the engine run"""
    kind, n, seed, work, exe, timeout = job
    T = TARGETS[kind]
    scratch = os.path.join(work, "w%d_%s" % (os.getpid(), T["game"]))
    src = os.path.join(ROOT, "games", T["game"])
    if not os.path.exists(os.path.join(scratch, ".ready")):
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.copytree(src, scratch, ignore=shutil.ignore_patterns("*.c", "*.py", "*.bat", "stage", "cooked", "save"))
        open(os.path.join(scratch, ".ready"), "w").close()
    path = os.path.join(scratch, T["file"])
    os.makedirs(os.path.dirname(path) or scratch, exist_ok=True)
    if "seed" in T:
        base = T["seed"]()
    elif "seed_from" in T:
        base = open(os.path.join(ROOT, T["seed_from"]), "rb").read()
    elif T["file"] in SAVE_SEEDS:
        base = SAVE_SEEDS[T["file"]].encode()
    else:
        base = open(os.path.join(src, T["file"]), "rb").read()
    shutil.rmtree(os.path.join(scratch, "save"), ignore_errors=True)
    if T.get("saves"):
        os.makedirs(os.path.join(scratch, "save"), exist_ok=True)
        for p, s in SAVE_SEEDS.items():
            open(os.path.join(scratch, p), "w", newline="\n").write(s)
    for p, s in T.get("also", {}).items():
        os.makedirs(os.path.dirname(os.path.join(scratch, p)), exist_ok=True)
        open(os.path.join(scratch, p), "w", encoding="utf-8", newline="\n").write(s)
    rng = random.Random(hashlib.sha256(("%s/%d/%d" % (kind, seed, n)).encode()).digest())
    data = base if n == 0 else (mutate_text if T["text"] else mutate_bin)(base, rng)   # the first of each kind is the file as it is: the engine must be clean on that before a broken one means anything
    open(path, "wb").write(data)
    env = dict(os.environ, REVA_FIXEDSTEP="60", REVA_NOWINDOW="1", REVA_EXITTICK=str(T["ticks"]), ASAN_OPTIONS="detect_leaks=0:abort_on_error=0:allocator_may_return_null=1:max_allocation_size_mb=2048", UBSAN_OPTIONS="print_stacktrace=0", **T["env"])
    if not T.get("saves"):
        env["REVA_NOSAVE"] = "1"
    t0 = time.time()
    # ⛔ A RUN THAT ENDS ISN'T A HANG. Twice a set of a hundred rooms on top of one another was called one: under the checkers it's a second a frame, and it ends. So a run
    # that passes the limit gets one more go with ten times as long, and only one that doesn't end in that is a hang. A slow one is said, with how long it took
    for limit in (timeout, timeout * 10.0):
        timed_out = False
        try:
            p = subprocess.run([exe[T["game"]]] + T["args"], cwd=scratch, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=limit)
            out, rc = p.stdout.decode("utf-8", "replace"), p.returncode
            break
        except subprocess.TimeoutExpired as e:
            out, rc, timed_out = (e.stdout or b"").decode("utf-8", "replace"), -999, True
    if limit != timeout and not timed_out:
        print("  %s #%d: slow, not a hang: it ended after %.0f s" % (kind, n, time.time() - t0), flush=True)
    sig = signature(out, rc, timed_out)
    open(path, "wb").write(base)   # as it was, for the next kind this worker takes
    clean = rc == 0 and "exiting clean" in out
    return kind, n, sig, data if sig else None, out[-6000:] if sig else "", clean, time.time() - t0


def build(work, jobs):
    """the checked engines, one a game, all at once"""
    os.makedirs(work, exist_ok=True)
    cc = os.environ.get("CC", "gcc")
    procs = []
    for g in GAMES:
        out = os.path.join(work, "crowd_%s_fz" % g)
        cmd = [cc, "-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined", "-fno-sanitize=alignment", "-std=gnu17", "-ffp-contract=off", "-DREVA_SDL", "-DREVA_SOFT", "-DREVA_NODOORS",
               "-o", out, "crowd_kms.c", "club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "club_bridge.c", "club_net.c", "games/%s/%s.c" % (g, g), "gfx_packet.c", "gfx_soft.c", "-lSDL2", "-lm"]
        procs.append((g, subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)))
    bad = 0
    for g, p in procs:
        o = p.communicate()[0]
        errs = [l for l in o.splitlines() if "error" in l]
        print("%s: %s" % (g, "built" if p.returncode == 0 else "FAILED\n" + "\n".join(errs[:12] or o.splitlines()[-12:])))
        bad += p.returncode != 0
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--runs", type=int, default=300, help="broken copies of each kind")
    ap.add_argument("--only", help="kinds, with commas between: " + " ".join(TARGETS))
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--work", default="/build/fuzz" if os.path.isdir("/build") else os.path.join(tempfile.gettempdir(), "reva_fuzz"))
    ap.add_argument("--timeout", type=float, default=60.0, help="seconds before a run is a hang")
    ap.add_argument("--replay")
    ap.add_argument("--kind")
    a = ap.parse_args()
    if a.build:
        sys.exit(build(a.work, a.jobs))
    exe = {g: os.path.join(a.work, "crowd_%s_fz" % g) for g in GAMES}
    for g, e in exe.items():
        if not os.path.exists(e):
            sys.exit("no %s: run with --build first (it needs gcc's sanitizers: on Windows, in the build container)" % e)
    if a.replay:
        T = TARGETS[a.kind or "pack"]
        scratch = os.path.join(a.work, "replay_" + T["game"])
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.copytree(os.path.join(ROOT, "games", T["game"]), scratch, ignore=shutil.ignore_patterns("*.c", "*.py", "*.bat", "stage", "cooked", "save"))
        if T.get("saves"):
            os.makedirs(os.path.join(scratch, "save"), exist_ok=True)
            for p, s in SAVE_SEEDS.items():
                open(os.path.join(scratch, p), "w", newline="\n").write(s)
        for p, s in T.get("also", {}).items():
            os.makedirs(os.path.dirname(os.path.join(scratch, p)), exist_ok=True)
            open(os.path.join(scratch, p), "w", encoding="utf-8", newline="\n").write(s)
        os.makedirs(os.path.dirname(os.path.join(scratch, T["file"])) or scratch, exist_ok=True)
        shutil.copyfile(a.replay, os.path.join(scratch, T["file"]))
        env = dict(os.environ, REVA_FIXEDSTEP="60", REVA_NOWINDOW="1", REVA_EXITTICK=str(T["ticks"]), ASAN_OPTIONS="detect_leaks=0:allocator_may_return_null=1:max_allocation_size_mb=2048", **T["env"])
        if not T.get("saves"):
            env["REVA_NOSAVE"] = "1"
        p = subprocess.run([exe[T["game"]]] + T["args"], cwd=scratch, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(p.stdout.decode("utf-8", "replace")[-9000:])
        print("exit %d" % p.returncode)
        return
    kinds = [k for k in (a.only.split(",") if a.only else TARGETS) if k in TARGETS]
    found = os.path.join(a.work, "found")
    jobs = [(k, n, a.seed, a.work, exe, a.timeout) for k in kinds for n in range(a.runs + 1)]
    stats = {k: dict(runs=0, clean=0, faults={}, secs=0.0) for k in kinds}
    t0 = time.time()
    with concurrent.futures.ProcessPoolExecutor(max_workers=a.jobs) as ex:
        for kind, n, sig, data, out, clean, secs in ex.map(one, jobs, chunksize=4):
            S = stats[kind]
            S["runs"] += 1
            S["clean"] += clean
            S["secs"] += secs
            if sig:
                first = sig not in S["faults"]
                S["faults"][sig] = S["faults"].get(sig, 0) + 1
                if first:
                    d = os.path.join(found, kind)
                    os.makedirs(d, exist_ok=True)
                    stem = os.path.join(d, "%04d" % n)
                    open(stem + ".bin", "wb").write(data)
                    open(stem + ".txt", "w", encoding="utf-8").write(sig + "\n\n" + out)
                    print("  %s #%d%s: %s" % (kind, n, " (the file as it is, unbroken)" if n == 0 else "", sig), flush=True)
    bad = 0
    print()
    for k in kinds:
        S = stats[k]
        nf = sum(S["faults"].values())
        bad += len(S["faults"])
        print("%-9s %5d runs, %5d ran to the end, %5d refused or stopped early, %d faulted (%d kinds)%s" % (k, S["runs"], S["clean"], S["runs"] - S["clean"] - nf, nf, len(S["faults"]), "" if not S["faults"] else "  <-- " + found + "/" + k))
    print("%.0f s on %d at once, seed %d. %s" % (time.time() - t0, a.jobs, a.seed, "⛔ %d kinds of fault" % bad if bad else "no fault found"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
