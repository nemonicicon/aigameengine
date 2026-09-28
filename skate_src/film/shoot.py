#!/usr/bin/env python3
"""shoot.py -- shoot SEVENTY-FOUR with COVERAGE, and cut it.

    python skate_src/film/shoot.py                    # the film
    python skate_src/film/shoot.py --only denial      # one scene, both cameras, cut
    python skate_src/film/shoot.py --dry              # what it would do

⭐ EVERY SCENE IS SHOT TWICE. The first cut had one lens per take and it showed: six of
seven takes were the conversation camera, which frames the two HEADS, so the whole film
was two women in medium shots. Worse, act III's hinge -- mrs parsons taking the mail
carrier -- happened BEHIND them, on a timeline built for exactly that, and the picture
never once looked at it.

So each scene gets an A camera (the conversation lens, which is very good at a
conversation) and a B camera (pinned, and pointed at whatever the scene is ABOUT), and an
EDL says when to be on which:

    denial     B is behind them looking ACROSS the road at mrs parsons -- the woman they
               are arguing about, standing at her mailbox for the whole scene
    it_starts  B is the turn itself, at the camera it was verified on, cut in at t=9
               because the cue fires at 12.4
    boarding   B is the window, and the boards going over it

⛔ THE AUDIO IS IDENTICAL IN BOTH TAKES, which is what makes this safe. Both captures are
deterministic under REVA_FIXEDSTEP and play the same scene clock, so a cut at t=18 lands on
the same word in both. Segments are cut on TIME, not on keyframes, so they are re-encoded
once (crf 18) rather than stream-copied out of alignment.
"""
import argparse, glob, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
NATIVE = os.path.join(ROOT, "reva_native")
# ⛔ THE FILM AND ITS TAKES DO NOT LIVE ON C: -- a take is gigabytes of PPM before
# it is a megabyte of mp4. jacob, 2026-09-03: "start using D:\reva902".
OUTROOT = os.environ.get("REVA_OUTROOT", "out")
DRIVER = os.path.join(ROOT, ".claude", "skills", "run-reva", "driver.py")
PY = os.environ.get("CLUB_PYTHON", sys.executable)
FFMPEG = next(iter(glob.glob(os.path.join(
    os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local")),
    "Microsoft", "WinGet", "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe"))), None)

# hour of the story -> the light, off the same keyframes tod= interpolates
LIGHT = {
    # ⛔ NIGHT IS NOT sun=off -- that is the club's dark-room constant path, with no sky
    # and no direction. Night is a WEAK BLUE DIRECTIONAL over a near-black sky, and the sun
    # is never let below y=0.06 as a light because a directional from underneath lights a
    # face from the chin up. Taken from the same tod curve the day arc runs on.
    "0410": ("10,300,0.07,0.48,0.58,0.86",  "0.030,0.036,0.062"),
    # the blue hour, for the one shot that has to show a whole town before sunrise
    "PREDAWN": ("4,300,0.30,0.50,0.60,0.90", "0.085,0.105,0.165"),
    "0715": ("14,93,0.95,1.00,0.80,0.60",   "0.315,0.350,0.450"),
    # ⛔ 0800 AND 1210 WERE ADDED TO THE SHOT TABLE AND NOT TO THIS DICT (2026-09-03),
    # so the moment `tables` moved to eight and `theleash` to ten past twelve, every
    # shoot of them died on a KeyError. Interpolated off the neighbours on the same curve.
    "0800": ("22,102,1.07,1.00,0.86,0.68",  "0.358,0.402,0.510"),
    "0900": ("30,112,1.20,1.00,0.90,0.76",  "0.400,0.455,0.570"),
    "1210": ("65,180,1.37,1.00,0.98,0.94",  "0.458,0.518,0.638"),
    "1200": ("66,178,1.38,1.00,0.98,0.94",  "0.460,0.520,0.640"),
    "1330": ("56,206,1.10,1.00,0.95,0.88",  "0.400,0.450,0.560"),
    "1500": ("40,238,1.24,1.00,0.94,0.84",  "0.440,0.495,0.605"),
    # ⛔ DUSK WAS TOO DARK TO READ A FACE. At elevation 6 and intensity 0.62 over a 0.31
    # sky, act IV -- the payoff of the film -- came back as silhouettes in a grey haze.
    # Lifted the key and warmed the sky; it is still dusk, the shadows are still long,
    # and now there are faces in it.
    "1840": ("9,270,0.92,1.00,0.66,0.40",   "0.360,0.320,0.350"),
    "2200": ("18,298,0.10,0.55,0.62,0.88",  "0.038,0.044,0.075"),
}

# name, act, hero, partner, P2, set, stage, hour, camA ("" = conversation lens), camB, EDL
# P2 is who stands in slot 2 -- "" for a two-hander, which is every row that had one
# before, so nothing that already worked changes shape.
# EDL: (start, end, 'A'|'B') in seconds. Anything not covered is A.
# ⛔ ANIMA IS KAELI, IN EVERY SCENE (jacob, 2026-09-03: "MAKE SURE YOU ARE USING THE RIGHT
# ACTOR, WE ARE USING ANIMA FOR KAELI RIGHT NOW"). Only the cold open had been recast after
# the model search; the other seven rows still loaded `kmoves`, which is the cat-girl body
# in a bikini -- so six of the nine scenes were being shot with the wrong woman, wearing the
# wrong clothes, at a block party and a boarding-up. kmoves stays LOADED as a clip bank, so
# every borrowed clip (x_convo, w_look, x_chairsat, z_rm_curlsleep) still resolves through
# bankFind onto anima's rig exactly as it does in the cold open.
SHOTS = [
    # ⭐ THE COLD OPEN. Interior, before dawn, and the only scene in the film with no
    # street in it. The conversation lens does the whole thing: it is two people in a dark
    # room and there is nothing else to look at, which is the point.
    # ⛔ WAS SHOT IN `boarded` -- act V's room, with the planks up and susan sitting in
    # the corner as a set actor. The first ninety seconds of the film showed the audience
    # the last two minutes of it, in a house susan does not live in. This is kaeli's house,
    # normal, at four in the morning, and the only two people in it live here.
    # ⭐ AND THEY ARE DRESSED FOR BED. jacob: "she is a bit nude to be standing out on the
    # street at 7am" -- and it is worse at four in the morning in her own bedroom. Both
    # bodies wear the simulated garments here and ONLY here; by the porch at 07:15 they are
    # dressed and back on their normal packs.
    # ⭐ ANIMA IS KAELI. jacob, after the model search: "ooof vampire looks really bad..
    # anima looks consitenlty good lets go with anima". She is the only body in the club
    # with a sculpted face -- her own visemes, brows, per-eye blinks and a jawOpen the
    # speech envelope drives -- and this scene is ninety seconds of two faces in the dark,
    # which is the one place that difference is the whole shot.
    # ⛔ THE CONVERSATION LENS CANNOT SHOOT A BED. It frames the two HEAD positions, and
    # here one head is LYING at y 1.0 and the other is SITTING at 2.6 -- so the lens solves
    # to bed height and buries itself in the mattress. Shot at 11.0s came back as the inside
    # of the duvet with a subtitle over it, on the first scene of the film.
    # ⭐ SO THIS SCENE IS PINNED, both cameras, like dogwalk and theleash. A is the bedside
    # two-shot from the right, raised above the footboard; B is from the foot, higher again,
    # and is the establishing frame the film opens on.
    # ⛔ AND THE FOOTBOARD ATE THE BOTTOM 45% of the old B -- at y 1.55 the camera sits under
    # the rail, so half the opening image of the picture was a black slab. y 2.30 clears it.
    ("coldopen", "0", "reva_pj", "anima_fx", "", "kaeli_house", "4.6,4.55,0", "0410",
     "8.40,2.50,4.80,5.00,1.42,5.35,44", "7.60,2.30,3.40,4.90,1.30,5.60,38",
     [(0, 6, "B"), (30, 38, "B"), (52, 62, "B"), (86, 99, "B")]),

    ("porch", "I", "r2moves", "anima_fx", "", "parsons", "30,-20,0", "0715",
     "", "38.0,2.55,-9.5,30.6,1.55,-19.6,42",
     [(0, 7, "B"), (29, 35, "B")]),

    # ⛔ A FIXED A LENS LOSES A PAIR THAT WALKS -- this one shot an empty street, the two
    # of them out of the left edge in the first second. The conversation lens frames the
    # two HEAD POSITIONS and cannot lose them; that is what it is for.
    # ⭐ THE THIRD PAIRING. reva and susan, alone, which the film had never done -- and
    # where the small kind lie is. B is the tables themselves, so the scene has something
    # to be ABOUT other than two faces.
    ("tables", "I", "r2moves", "crowd_susan", "", "parsons", "33,-19,110", "0800",
     "", "38.5,2.35,-12.0,33.4,1.50,-19.2,44",
     [(0, 5, "B"), (44, 52, "B")]),

    ("dogwalk", "I", "anima_fx", "crowd_susan", "", "walkies", "26,-18,14", "0900",
     "", "12.0,2.70,-15.0,33.0,1.50,-19.0,46",
     [(0, 6, "B"), (36, 46, "B")]),

    # ⭐ B LOOKS AT WHAT THEY ARE ARGUING ABOUT: from behind the pair, across the road, at
    # mrs parsons standing at her mailbox -- which she does for this entire scene.
    ("denial", "II", "r2moves", "anima_fx", "", "parsons", "30,-20,0", "1200",
     "", "33.2,2.70,-25.5,30.4,1.55,14.0,38",
     [(15, 27, "B"), (64, 75, "B"), (92, 99, "B")]),

    ("theleash", "II", "anima_fx", "crowd_susan", "", "leash", "44,-19,22", "1210",
     "", "43.0,2.10,-15.6,44.5,1.10,-22.5,46",
     [(20, 30, "B")]),

    # ⭐ AND HERE IS THE ONE THE FIRST CUT MISSED ENTIRELY. `the_turn` fires its cues at
    # 12.4 and 16.6; B is the camera the turn was verified on. In at 9, out at 24.
    ("it_starts", "III", "r2moves", "anima_fx", "crowd_susan", "the_turn", "30,-20,0", "1500",
     "", "33.2,2.15,10.5,29.9,1.55,20.3,30",
     [(9, 24, "B"), (34, 41, "B")]),

    # ⛔ STAGED 300 UNITS OUTSIDE ITS OWN SET. "240,-240" put the pair nowhere near the
    # lawns; the set's props run x 47..53 and both lenses aim at ~(35,-22). The cameras
    # were right and the mark was wrong -- 68 seconds of correctly-lit empty grass.
    ("three_lawns", "IV", "r2moves", "anima_fx", "crowd_susan", "three_lawns", "35.5,-21,10", "1840",
     # ⛔ WAS "55,3.4,3.5,35,1.5,-22,52" -- 31 units back, a standing woman at SIX PER
     # CENT of frame height, for 112 seconds, on the payoff of the entire film.
     # framecheck's size test convicts it. In to 12 units: still a wide, still shows the
     # ground they have to cross, and they are people rather than specks.
     "40.5,2.10,-14.5,35.5,1.45,-20.5,40", "38.2,1.55,-16.2,35.5,1.30,-21.0,36",
     [(19, 33, "B"), (50, 63, "B")]),

    ("boarding", "V", "r2moves", "anima_fx", "crowd_susan", "boarded", "6.4,9.8,4", "2200",
     "", "10.2,2.30,6.2,3.6,1.95,13.6,46",
     [(23, 33, "B"), (55, 63, "B")]),
]


# THE FILM IS NOT ONLY THE TALKING. jacob: "there needs to be more scenic shots
# especially in the beginning. just dumps me in to dialouge" and "this script is very
# disjointed" -- one fault, and it is that the shot list only ever held scenes with WORDS
# in them, so seven conversations were butt-spliced with nothing between them. The
# treatment opens on a street being ordinary for forty seconds before anybody speaks.
# name -> set, hour, cam, seconds
BROLL = {
    # ⛔ EVERY ONE OF THESE LOOKED AT z ~ -6..-20 AND THE ACTORS ARE ALL AT z ~ +19..+21.
    # Twelve lenses aimed at the opposite side of the road from the entire cast, which is
    # why the b-roll measured 0.01 -- a photograph. Turned round.
    # ⭐ THE FILM'S FIRST IMAGE, and the first MOVING shot the club has ever had.
    # jacob: "a slow zoom in from an eagle eyed lens into the window reva is looking out
    # of from kaelis house while she sits on kaelis bed and kaeli wakes up from a
    # nightmare". The cold open plays INSIDE kaeli_house, which is a 14x14 room with no
    # exterior, so the approach is shot on the street and cut to -- exterior establisher
    # into interior, which is also what Mascelli says the film owes an audience before it
    # closes in (design/cinematography.md).
    # It descends onto the SAME lens `night_house` already uses, so the framing it arrives
    # at is one that was tuned to miss the trees and the near-side wall.
    # ⛔ 0410, not 2200: this is four in the morning, and at that hour the only reason we
    # can see anything is the streetlight.
    # jacob wanted it much further out: "showing the whole town early in the morning".
    # ⛔ 0410's own light (sun 0.07) renders a whole-town wide as BLACK, so this one shot
    # is the blue hour -- enough to read the roofs and the street grid, still night. The
    # descent ends in the 4am dark at the window, which is where the scene picks up.
    # ⛔ AIMED STEEPLY DOWN, NOT BACK: a shallow angle from 190 units puts the edge of the
    # world in frame (open_avenues item 7). Steep fills it with town.
    # ⛔ `actors=0`: parsons casts 27 people and block adds six SKATEBOARDERS, and at ten
    # past four in the morning an empty street is the point -- one lit window and nobody
    # else awake plants "have you been sitting there the whole time" before a word is said.
    "kaeli_window": ("kaeli_ext", "PREDAWN", "70.0,190.0,80.0,60.0,0.0,0.0,52", 18.0,
                     "actors=0 camto=32.5,2.90,-17.5,32.5,2.60,-26.0,32 "
                     "camsecs=18 camease=1"),
    "open_street":  ("parsons", "0715", "-4.0,2.05,10.5,66.0,2.10,20.0,40", 10.0),
    # the first pass of this one put the lens INSIDE a house -- the near-side lots
    # run z -26..-40 and it was sitting at -30, looking at the inside of a wall.
    "block_alive":  ("parsons", "0900", "6.0,7.20,6.0,64.0,2.20,19.5,50",  7.0),
    "mailbox_hold": ("parsons", "1200", "30.6,1.95,14.6,30.3,1.58,20.4,28", 6.5),
    "dusk_street":  ("three_lawns", "1840", "62,3.10,6.0,34,1.60,-20.0,48", 6.0),
    # and this one had a tree and a passing neighbour across the lens; tighter, and
    # angled onto the lit wall instead of straight down the lot line.
    "night_house":  ("parsons", "2200", "34.0,2.20,13.0,30.4,1.90,20.4,36", 6.0),

    # ⭐⭐ THE LULL -- act II-and-a-half, and its script IS this table. Seven held shots,
    # no moves, no cuts inside a shot. THE ORDER IS THE ARGUMENT: it runs from the widest
    # and most alive to one still person, and each shot is quieter than the one before it.
    # Nobody says a word and by shot seven the audience has done the arithmetic on its own.
    "lull1_street": ("parsons", "1200", "2.0,6.60,7.5,60.0,2.10,20.0,52",  12.0),
    "lull2_grill":  ("parsons", "1200", "70.0,2.10,14.0,74.0,1.70,20.4,40", 10.0),
    "lull3_pool":   ("parsons", "1200", "56.0,2.05,14.0,59.6,1.30,20.8,42", 10.0),
    "lull4_tables": ("parsons", "1200", "14.0,2.40,14.5,18.4,1.55,19.8,38", 12.0),
    # ⛔ shot 5 is the only time in the film the camera declines to follow something: a kid
    # does laps and leaves frame and the lens stays where it is.
    "lull5_bikes":  ("parsons", "1200", "40.0,1.95,12.0,48.0,1.35,20.0,46", 10.0),
    "lull6_gate":   ("parsons", "1200", "24.0,1.85,13.5,30.0,1.45,20.6,36", 10.0),
    # ⛔ the longest hold in the film so far, on a woman who does not move.
    "lull7_parsons":("parsons", "1200", "30.2,1.90,15.2,30.2,1.55,20.6,26", 14.0),
}
# the CUT ORDER of the whole film: ("b", broll) or ("s", scene)
FILM = [("b", "kaeli_window"), ("s", "coldopen"),
        ("b", "open_street"), ("s", "porch"), ("s", "tables"), ("s", "dogwalk"),
        ("b", "block_alive"),
        ("s", "denial"), ("s", "theleash"),
        # ⛔ THE DOG USED TO GO MISSING TWICE. `denial` ends on reva reporting the empty
        # leash from across the road -- it is what breaks kaeli and triggers "So. What do
        # you see?" -- and `theleash` is that same minute at the fence, up close, with
        # susan. The lull's seven shots used to sit BETWEEN them, so the film revealed the
        # dog, played 78 seconds of a calm street, and then discovered the dog again.
        # theleash now follows denial immediately (informational reveal, then the emotional
        # one, which is coverage rather than repetition), and act II-and-a-half lands where
        # the treatment always put it: after act II, before it starts.
        # act II-and-a-half: the street, one last time, and nobody speaks
        ("b", "lull1_street"), ("b", "lull2_grill"), ("b", "lull3_pool"),
        ("b", "lull4_tables"), ("b", "lull5_bikes"), ("b", "lull6_gate"),
        ("b", "lull7_parsons"),
        ("s", "it_starts"),
        ("b", "dusk_street"), ("s", "three_lawns"), ("b", "night_house"), ("s", "boarding")]


def scnlen(nm, default=60.0):
    """⛔ THE SCENE KNOWS HOW LONG IT IS. Typing a length in is how every row of the first
    shot list ended up asking for two seconds more than the dialogue runs -- the one thing
    that stops the engine ever reaching its frame quota, so the take never ends."""
    try:
        return float(open(os.path.join(NATIVE, "scenes", nm + ".scn")).readline().split()[3])
    except Exception:
        return default


def take(a, row, which):
    (name, act, subj, partner, p2, setname, stage, hour, camA, camB, _edl) = row
    cam = camA if which == "A" else camB
    secs = scnlen(name)
    out = os.path.join(a.takes, "%s_%s_%s.mp4" % (act.lower(), name, which))
    if a.reuse and os.path.exists(out):
        print("   %s: reusing %s" % (which, os.path.basename(out)))
        return out
    sun, sky = LIGHT[hour]
    cmd = [PY, DRIVER, "scene", out, "--name", name, "--seconds", "%.2f" % secs,
           "--fps", str(a.fps), "--new", "--subject", subj, "--partner", partner,
           "--set", setname, "--stage", stage, "--sun", sun, "--sky", sky]
    if p2:
        cmd += ["--partner2", p2]
    if cam:
        cmd += ["--cam", cam]
    if a.dry:
        print("   %s: %s" % (which, " ".join(cmd[3:])))
        return out
    t0 = time.time()
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode != 0 or not os.path.exists(out):
        print("   %s TAKE FAILED" % which)
        return None
    print("   %s: %.1f MB in %.0fs" % (which, os.path.getsize(out) / 1e6, time.time() - t0))
    return out


def read_motion(logpath):
    """The engine's `[motion] t=.. hero .. p1 .. p2 ..` lines from a take's log: (t, energy)."""
    out = []
    try:
        for l in open(logpath, encoding="utf-8", errors="replace"):
            if not l.startswith("[motion]"):
                continue
            f = l.split()
            try:
                t = float(f[1][2:]); e = float(f[3]) + float(f[5]) + float(f[7])
            except (IndexError, ValueError):
                continue
            out.append((t, e))
    except OSError:
        return []
    return out


def snap_edl(edl, motion, window=0.8, lead=0.12, verbose=True):
    """⭐ CUT ON ACTION (Mascelli; the audit's first film item). The EDL is authored in seconds
    and a cut at a second lands wherever the bodies happen to be. The engine now publishes
    each body's motion energy (summed joint angular speed) at 10 Hz during a scene, and
    every EDL boundary slides to the strongest movement within `window` seconds of it,
    landing `lead` seconds BEFORE that peak -- mid-action, so both sides of the cut carry
    the same movement. A boundary with no movement near it stays where it was written."""
    if not motion:
        return edl
    ts = [t for t, _ in motion]
    es = [e for _, e in motion]
    # smooth over ~0.3 s so a single spiky sample does not win
    sm = []
    for i in range(len(es)):
        j0 = max(0, i - 1); j1 = min(len(es), i + 2)
        sm.append(sum(es[j0:j1]) / (j1 - j0))
    # the QUIET level is the 20th percentile, not the median: a body that is talking has a
    # median energy that is already a gesture, and a real move (7 rad/s against a 1.5 floor)
    # was refused as "no movement" when the median stood in for the floor
    srt = sorted(sm)
    floor = srt[len(srt) // 5] if srt else 0.0
    thr = floor * 1.5 + 1.0

    def snap(t):
        best, bt = -1.0, t
        for tt, e in zip(ts, sm):
            if abs(tt - t) <= window and e > best:
                best, bt = e, tt
        if best <= thr:
            return t, None
        return max(0.0, bt - lead), best

    out = []
    for (s0, s1, which) in sorted(edl):
        n0, e0 = snap(float(s0)); n1, e1 = snap(float(s1))
        if n1 <= n0 + 0.25:
            n0, n1 = float(s0), float(s1)
        if verbose and (abs(n0 - s0) > 1e-3 or abs(n1 - s1) > 1e-3):
            print("   cut on action: %s %.2f-%.2f -> %.2f-%.2f (peaks %s / %s)" % (
                which, s0, s1, n0, n1, "%.1f" % e0 if e0 else "-", "%.1f" % e1 if e1 else "-"))
        out.append((n0, n1, which))
    return out


def cut(a, row, ta, tb):
    """Assemble one scene from its two takes, on the EDL."""
    (name, act, _s, _p, _p2, _st, _sg, _h, _ca, _cb, edl) = row
    secs = scnlen(name)
    if not getattr(a, "no_action_cut", False) and ta:
        edl = snap_edl(edl, read_motion(os.path.splitext(ta)[0] + ".log"))
    spans, t = [], 0.0
    for (s0, s1, which) in sorted(edl):
        s0, s1 = max(0.0, float(s0)), min(secs, float(s1))
        if s1 <= s0:
            continue
        if s0 > t:
            spans.append((t, s0, "A"))
        spans.append((s0, s1, which))
        t = s1
    if t < secs:
        spans.append((t, secs, "A"))
    parts = []
    for i, (s0, s1, which) in enumerate(spans):
        src = (ta if which == "A" else tb) or ta or tb
        if src is None:
            continue
        p = os.path.join(a.takes, "_seg_%s_%02d.mp4" % (name, i))
        cmd = [FFMPEG, "-y", "-v", "error", "-ss", "%.3f" % s0, "-to", "%.3f" % s1,
               "-i", src, "-c:v", "libx264", "-crf", "18", "-preset", "veryfast",
               "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "192k", p]
        if subprocess.run(cmd).returncode == 0 and os.path.exists(p):
            parts.append(p)
    if not parts:
        return None
    lst = os.path.join(a.takes, "_cut_%s.txt" % name)
    with open(lst, "w") as f:
        for p in parts:
            f.write("file '%s'\n" % p.replace("\\", "/"))
    out = os.path.join(a.takes, "%s_%s.mp4" % (act.lower(), name))
    subprocess.run([FFMPEG, "-y", "-v", "error", "-f", "concat", "-safe", "0",
                    "-i", lst, "-c", "copy", out])
    for p in parts:
        try:
            os.remove(p)
        except OSError:
            pass
    print("   cut: %d segments (%d on B)" % (len(spans), sum(1 for s in spans if s[2] == "B")))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--only", default="")
    ap.add_argument("--out", default=os.path.join(OUTROOT, "seventy_four.mp4"))
    ap.add_argument("--takes", default=os.path.join(OUTROOT, "takes"))
    ap.add_argument("--dry", action="store_true")
    ap.add_argument("--reuse", action="store_true", help="keep takes already on disk")
    ap.add_argument("--no-action-cut", action="store_true", help="cut on the EDL's seconds, not on the nearest action")
    ap.add_argument("--snaptest", help="print what cut-on-action would do to an EDL 'a-b:B,c-d:B' given a take LOG (--edl)")
    ap.add_argument("--edl", default="4.3-7:B")
    a = ap.parse_args()
    if a.snaptest:
        edl = []
        for tok in a.edl.split(","):
            rng, _, which = tok.partition(":")
            s0, _, s1 = rng.partition("-")
            edl.append((float(s0), float(s1), which or "B"))
        m = read_motion(a.snaptest)
        print("%d motion samples in %s" % (len(m), a.snaptest))
        out = snap_edl(edl, m)
        print("EDL in : %s" % edl)
        print("EDL out: %s" % [(round(x, 2), round(y, 2), w) for x, y, w in out])
        return 0
    os.makedirs(a.takes, exist_ok=True)

    rows = [r for r in SHOTS if not a.only or r[0] == a.only]
    total = sum(scnlen(r[0]) for r in rows)
    print("%d scene(s), %d:%02d of film, %d fps, TWO cameras each (~%d frames)"
          % (len(rows), int(total) // 60, int(total) % 60, a.fps, int(total * a.fps * 2)))
    sys.stdout.flush()

    made = {}
    for row in rows:
        print("\n=== act %-3s %-12s %5.1fs  set=%-12s %s ==="
              % (row[1], row[0], scnlen(row[0]), row[4], row[6]))
        sys.stdout.flush()
        ta = take(a, row, "A")
        tb = take(a, row, "B")
        if a.dry:
            continue
        c = cut(a, row, ta, tb)
        if c:
            made[row[0]] = c
        sys.stdout.flush()

    # ---- the silent shots, in the same pass ----
    import broll as BR
    for key in [k for (_t, k) in FILM if k in BROLL]:
        row = BROLL[key]
        setname, hour, cam, secs = row[0], row[1], row[2], row[3]
        extra = row[4] if len(row) > 4 else ""
        if a.only and a.only != key:
            continue
        sun, sky = LIGHT[hour]
        out = os.path.join(a.takes, "b_%s.mp4" % key)
        print("")
        print("=== BROLL   %-12s %5.1fs  set=%-12s %s ===" % (key, secs, setname, hour))
        sys.stdout.flush()
        if a.reuse and os.path.exists(out):
            made[key] = out
            print("   reusing")
            continue
        if BR.shoot(out, setname, cam, secs, a.fps, sun, sky, quiet=False, extra=extra):
            made[key] = out

    # ⛔ --only MUST NOT CLOBBER THE FILM. It exists to re-do one piece; writing the
    # whole-film output from a single take is how a good cut got replaced by 0.1 MB.
    if a.only:
        print("")
        print("%d piece(s) rebuilt; re-run without --only to re-cut the film" % len(made))
        return 0
    if a.dry or not made:
        print("\n%d scene(s)" % len(made))
        return 0
    # CUT IN THE FILM ORDER, not in shooting order: b-roll before the porch,
    # between the acts, and ahead of the night.
    order = [made[k] for (_t, k) in FILM if k in made]
    lst = os.path.join(a.takes, "_film.txt")
    with open(lst, "w") as f:
        for m in order:
            f.write("file '%s'\n" % m.replace("\\", "/"))
    r = subprocess.run([FFMPEG, "-y", "-f", "concat", "-safe", "0", "-i", lst,
                        "-c", "copy", a.out],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if r.returncode != 0:
        print(r.stderr[-1500:])
        return 1
    print("\nwrote %s  (%.1f MB from %d scenes)   <-- WATCH IT"
          % (a.out, os.path.getsize(a.out) / 1e6, len(order)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
