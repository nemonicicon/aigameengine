#!/usr/bin/env python3
"""compile_scene.py -- turn spoken lines into ONE scene the engine can play.

    compile_scene.py club_night.txt [--gap 0.30] [--out club_night]

Produces, next to the dialogue file:
  <out>.wav    every line mixed into a single track at its scene time
  <out>.scn    the timeline: who speaks when, the subtitle, a 30Hz MOUTH
               ENVELOPE per line, and the STAGING for that line

ONE MIXED TRACK, not fourteen scheduled ones. The engine then starts a single sound at
scene time zero and never schedules anything again -- subtitles and jaws are driven by
the same offsets the mix was built from, so picture and sound cannot drift apart no
matter what the frame rate does.

THE MOUTH ENVELOPE is the speech's own loudness, per 1/30s frame, normalised per line
(the p95, not the peak -- one plosive should not make every other syllable a mumble).
That is what drives the jaw morph, so the mouth opens on the sound it is actually
making rather than flapping on a timer.

STAGING (08-28). A conversation where both bodies stand in one pose for a minute is a
radio play with a camera pointed at it. The dialogue file may carry direction for the
BODIES on the same line as the direction for the voices, and it is compiled into the
timeline so the engine can act on it:

VISEMES (08-28). The `.scn` also carries a 30Hz JAW SHAPE per line, walked off the text:
closures at /m/ /b/ /p/, wide at /i/, open at /a/, and a relaxed mouth between words. The
engine MULTIPLIES it with the loudness envelope -- the envelope has the rhythm, the
viseme has the shape, and neither one alone is a mouth.

    pgap  pyaw  pside     where the partner stands, and which way she faces
    hlook plook           whether each will meet the other's eyes  <- the emotional beat
    shot                  wide | medium | two | ots | tight | solo   (widest to tightest)
                          `medium` is the hips-up two-shot: both bodies, the bottom edge at
                          hip/thigh height, so hands and held props are large in frame.
                          `solo` is the LISTENER'S POINT OF VIEW -- the lens at her eye mark,
                          she is behind it, the speaker nearly into it.
    reverse               1 = mirror the lens to the OTHER side of the pair's axis (who is on
                          screen-left swaps, a different background comes up). Sticky; 0 returns.
    jawgain               how far the PUPPET jaw opens under `mouth hinge` (1.0 = as shot so
                          far). Sticky. No effect on a scene that has a viseme track.
    hclip pclip           what each body plays
    hgt                   camera height off the eyeline: negative looks UP at the
                          speaker, positive looks DOWN on her. This is the "angle".
    hbrow pbrow           -1..1, or `auto`. The brow morph LIFTS, so a NEGATIVE weight
                          furrows it. It is sticky like the rest, which is the point:
                          the listener holds her scowl through the other one's lines.
    cam camtrack campan   the placed lens, a lens that rides with a body, one that pans after it (ep 8/10)
    cammove camsecs       (ep 11) ease this line's cam= to cammove= over the line (or camsecs=); an EVENT --
                          the move's end is carried on as the next line's cam=
    camorbit              (ep 11) circle a body, deg0 -> deg1 of world heading; sticky like camtrack
    camshake              (ep 11) amp[,hz]: a handheld on top of any placed lens; sticky, 0 = off

It is STICKY: a line inherits every value the previous line had, so the file shows only
what CHANGES. The engine eases position and facing toward the new values instead of
snapping, which is what turns "pyaw=46" into a woman turning her back on someone.
"""
import sys, os, wave, struct, math

def rms_env(frames, sr, nch, sw, fps=30.0):
    step = int(sr / fps)
    if sw != 2: raise SystemExit("expected 16-bit PCM, got %d bytes/sample" % sw)
    n = len(frames) // (2 * nch)
    out = []
    for i in range(0, n, step):
        chunk = frames[i*2*nch: min(n, i+step)*2*nch]
        if not chunk: break
        vals = struct.unpack("<%dh" % (len(chunk)//2), chunk)
        if nch == 2: vals = vals[0::2]
        acc = 0.0
        for v in vals: acc += float(v)*float(v)
        out.append(math.sqrt(acc/max(1, len(vals))))
    if not out: return []
    s = sorted(out)
    ref = s[int(len(s)*0.95)] or 1.0
    return [max(0.0, min(1.0, v/ref)) for v in out]

# what the engine is handed per line. The values here are the scene's opening state.
STAGE0 = dict(pgap="1.15", pyaw="180", pside="0.12", hlook="1", plook="1",
              # z_upright is EIGHT FRAMES -- a static pose, and it was the default
              # every body played in every scene. That is "they are just standing
              # there looking lifeless". All three principals ship a real idle.
              # ⛔ NOT "idle". kaeli's own `idle` is a GET-UP-OFF-THE-FLOOR loop -- I
              # swapped the static z_upright for it without looking at what it was on her
              # rig. `auto` casts by ROLE instead: the speaker gestures, the listener
              # listens, and it swaps on every line.
              shot="ots", hclip="auto", pclip="auto", hx="", hz="",
              # ⭐ THE REACTION SHOT. Which body the LENS is on when it is not the one
              # talking -- hero | partner | partner2. Empty = the speaker, as always, and
              # an empty key is never written. Without this the camera could only ever
              # watch whoever was speaking, which in a film of two people listening to
              # each other leaves half the performance off-screen.
              focus="",
              # ⭐ SLOT 2. The scene system takes three people now, and her blocking is
              # authored the same way slot 1's is. Defaulted EMPTY so a two-hander emits
              # none of them -- the engine keeps its own defaults and the .scn stays the
              # size it was.
              p2gap="", p2yaw="", p2side="", p2clip="", p2look="", p2brow="",
              # ⭐ AND THE FOURTH BODY (ep 7). SCN_MAXCAST went 4 -> 5 so a scene can
              # seat four bodies plus the off-screen voice; slot 3 is staged exactly
              # like slot 2. Empty by default, so a three-hander emits none of them.
              # ⛔ She is cast EXPLICITLY -- `auto` does not resolve past slot 1.
              p3gap="", p3yaw="", p3side="", p3clip="",
              # ⭐ ep 10: ONE-SHOT TRICKS. clip@sec[,clip@sec...] -- `sec` into the line the body cuts to
              # the clip at phase 0, plays it ONCE and cuts back to the line's clip at phase 0.
              # ⛔ NOT STICKY (TRICK_KEYS): a trick is an event like `beat`, and a sticky one would
              # re-fire on every line after the one that asked for it.
              htrick="", ptrick="", p2trick="", p3trick="",
              # ⭐ ep 12: EXHALES. sec[,sec...] -- `sec` into the line, that body breathes a cloud of smoke off
              # its mouth mark along its head's forward (~1.2 s). NOT sticky (PUFF_KEYS), like a trick. The
              # smoke off a cigarette's tip is the prop's own business: `@smoke` on its hprop= spec.
              hpuff="", ppuff="", p2puff="", p3puff="",
              # ⭐ THE RAGDOLL. `sec` into the line that body goes LIMP -- the skeleton becomes 18 linked
              # points falling under gravity into the floor, the furniture and the walls -- or `off` hands
              # it back to its clip. NOT sticky (DOLL_KEYS): a fall is an event, like a trick.
              hdoll="", pdoll="", p2doll="", p3doll="",
              # ⭐ ep 8: where HOLLY is, and which way up. x,y,z,yaw,pitch,w,h --
              # sticky, and on a `snap=1` line it lands on the cut, because righting a
              # telly in stop motion is the animator picking it up between exposures.
              screen="",
              # ⭐ THE FREE CAMERA (ep 8). cam=ex,ey,ez,ax,ay,az[,fov] places the lens
              # for THIS LINE; cam=off hands it back to the shot ladder. Not sticky --
              # a pin that outlives its line is how a scene loses its coverage.
              cam="",
              # ⭐ WHAT THEY ARE CARRYING. Sticky, and `off` clears it -- a woman has to
              # be able to put a coffee down. Empty by default so a scene that holds
              # nothing emits nothing.
              hprop="", pprop="", p2prop="",
              # ⭐ AND THE OTHER HAND (ep 5). A body holds TWO things now. Same spec, same
              # parser. ⛔ STAGE0 IS A WHITELIST -- an unknown key is a hard SystemExit --
              # so a scene could not have said `hprop2=` at all until these three existed.
              # With no `@where` the second prop takes the hand the first one did not.
              hprop2="", pprop2="", p2prop2="",
              # ⭐ ON A PROP. A named `mark` in the .set carries a HEIGHT, so a body sent
              # to one sits ON the chair instead of beside it. `off` hands her back to
              # the pair relation.
              # ⭐ ep 9: and slot 3. A changed mark is WALKED to now (rolled, for a body with
              # wheels or on a @deck); `snap=1` still lands it on the cut.
              pmark="", p2mark="", p3mark="",
              # ⭐ ep 10: PATHS -- `hpath=a,b,c` (ppath/p2path/p3path) rolls a body AROUND the set on a
              # centripetal Catmull-Rom from where it stands through those marks, at hspd/pspd/p2spd/
              # p3spd u/s (3.0). Sticky; a CHANGED value starts it. A path and a mark cannot both be in
              # force for one body, so a path line drops the mark (the path ends on its own last one),
              # a mark line drops the path, and `off` drops it -- see PATH_MARK below the key loop.
              hpath="", ppath="", p2path="", p3path="", hspd="", pspd="", p2spd="", p3spd="",
              # `@name` rides a set's `route <name> <mark> ...`. hloop/ploop/p2loop/p3loop=1 keeps a body
              # circling a CLOSED route (its last mark is its first) with no braking, until a line changes
              # the path or says 0. A mark line or `off` drops the loop with the path.
              hloop="", ploop="", p2loop="", p3loop="",
              # ⭐ ep 11: THE HALF-PIPE. hpipe=<name>[,<air>] (ppipe/p2pipe/p3pipe) rides a set's `halfpipe`:
              # drop in off the nearer coping and run the vert cycle, `air` u above the coping (2.0), until the
              # value changes. hair=<clip>[,<clip>...] (pair/p2air/p3air) is a one-shot fired at the START of each
              # air, cycling the list. Both sticky; `off` is said on its own line and never carried.
              hpipe="", ppipe="", p2pipe="", p3pipe="", hair="", pair="", p2air="", p3air="",
              # ⭐ ep 10: THE TRACKING LENS. camtrack=slot,dist,height,azim,fov[,aimh] follows a body off
              # its travel (azim 0 chases, 180 leads, 90 rides beside); campan=slot[,aimh] keeps the
              # cam= eye and aims at it. Sticky until camtrack=off / campan=off or a line saying cam=.
              camtrack="", campan="",
              # ⭐ ep 11: THE MOVING LENSES. cammove=ex,ey,ez,ax,ay,az[,fov] eases the line's cam= (the START)
              # to this pose (the END) -- a dolly, a crane, a push-in -- and the END is carried on as the next
              # line's cam=, so the lens stays where the move put it (with no cam= it moves from the lens it has).
              # camorbit=slot,radius,height,deg0,deg1[,fov[,aimh]] circles a body round its continuous spot, the
              # lens LOOKING along world heading deg (0 = from its -z side looking +z, 90 = from -x looking +x);
              # sticky like camtrack, and deg0 == deg1 is a world-frame follow. camsecs=<s> times the line's
              # cammove/camorbit (default: the line's whole span, to the next cut). camshake=amp[,hz] is a
              # handheld on top of any placed lens -- amp in u, hz 1.5 -- sticky, 0 = off. A camtrack/camorbit
              # height written `=h` (camtrack=0,6,=3.0,90,50) is ABSOLUTE world y: the lens tilts up after a
              # rider on a wall instead of rising with her. ONE lens mode per line.
              cammove="", camorbit="", camsecs="", camshake="",
              # ⭐ 10-22: camat=x,y,z (u) is a camorbit's CENTRE when it circles a world point -- a ship -- and not a
              # body; it rides with the orbit (carried while it is, dropped with it). set=<name> plays THIS line on
              # another set -- a CUTAWAY, the ship outside the sick bay -- with that set's own light; NOT sticky:
              # the next line without one is back on the set the scene was staged on, in the light it had.
              camat="", set="",
              # ⭐ 10-22: home=<set> -- the scene's HOME set from this line on (sticky: the boys put into his pan once
              # he is out of it). houtfit= poutfit= p2outfit= p3outfit= -- dress a body on the cut (sticky; its own
              # pack's outfit names: Viktor's are freezer and tunic).
              home="", houtfit="", poutfit="", p2outfit="", p3outfit="",
              # ⭐ 10-22: hroll= proll= p2roll= p3roll=ax,ay,az,dx,dy,dz,degps[,deg0] -- the body carried round an axis
              # (Viktor round the treadmill drum); sticky, `off` hands it back.
              hroll="", proll="", p2roll="", p3roll="",
              # ⭐ ep 10: dur=<sec> makes a line with NOTHING after `|` an ACTION line: no voice, no
              # subtitle, no mouth, that long in the timeline. NOT sticky (it is an event, like beat).
              dur="",
              # ⭐ AND THE HERO ON ONE (09-11): reva on the bed, not beside it.
              hmark="",
              # ⭐ UPPER-BODY LAYERS (09-11): a second clip on the spine and up -- carry a crate
              # while walking, wave while talking. <clip>[@weight]; sticky; `off` clears.
              hover="", pover="", p2over="",
              hbrow="auto", pbrow="auto", hgt="0",
              # ⭐ CROSSING THE LINE ON PURPOSE (ep 4), and the puppet jaw's gain. Both sticky,
              # both empty by default so a scene that never says them emits nothing.
              reverse="", jawgain="",
              # ⭐ STOP MOTION. Exposures per second from this line on -- sticky like the lens,
              # and it changes on the cut. Empty = never written = on ones.
              step="",
              # ⭐ THE LIGHT, per line and sticky: `tod=` is the hour the shot was taken (it
              # changes on the cut) and `tone=` is the highlight knee (0 = off).
              # (`snap=1` is deliberately NOT here: a jump is an event, like `beat`.)
              camhold="",
              # ⭐ ep 2: WHOSE CAMERA THIS IS. feed=1 puts the shot inside the camera reva is holding
              # (4:3, vignette, tape grain, a REC dot) and fish= is the lens on the front of it (0 = the
              # club's own glass, ~0.55 a fisheye). Both sticky, both land on the cut.
              feed="", fish="",
              tod="", tone="")
VOICE_KEYS = ("rate", "var", "pitch", "voice", "with")   # with=N: a second voice in unison (say_piper.py, 10-22)   # the voicers' keys, ignored here (voice= is say_kokoro's)
TRICK_KEYS = ("htrick", "ptrick", "p2trick", "p3trick")   # ⭐ ep 10: in STAGE0 (the whitelist) but NOT sticky
PUFF_KEYS = ("hpuff", "ppuff", "p2puff", "p3puff")         # ⭐ ep 12: in STAGE0 but NOT sticky; the engine keeps 8, 63 chars
DOLL_KEYS = ("hdoll", "pdoll", "p2doll", "p3doll")         # ⭐ the fall: in STAGE0 but NOT sticky
# ⭐ THE LENS KEYS' SHAPES (ep 10/11): how many numbers each takes, which field may be written `=h` (an ABSOLUTE
# world height; -1 = none), and what to say when it is wrong. Checked here so a bad lens stops the compile rather
# than coming back as a `[lens] ... needs` line in a take's log.
LENS_FMT = {
    "camtrack": ((5, 6), 2, "slot,dist,height,azim,fov[,aimh]  (height `=h` = absolute world y)"),
    "campan":   ((1, 2), -1, "slot[,aimh]"),
    # ⭐ THE FILMER'S OWN CAMERA (ep 2 of the skate promo): the lens rides one body's hand and watches
    # another. Everything the filmer's body does is in the picture, which is what skate footage IS.
    "camhold":  ((2, 3, 4, 5), -1, "filmer,subject[,fov[,aimh[,drop]]]  (drop = u below her hand: 0.6 is a phone by the knee)"),
    "camorbit": ((5, 6, 7), 2, "slot,radius,height,deg0,deg1[,fov[,aimh]]  (height `=h` = absolute world y)"),
    "cammove":  ((6, 7), -1, "ex,ey,ez,ax,ay,az[,fov] -- the END pose; the line's cam= is the start"),
    "camsecs":  ((1,), -1, "seconds > 0"),
    "camshake": ((1, 2), -1, "amp[,hz]  (amp in u, 0 = off; hz 1.5 if not given)"),
}


def lens_nums(v, absat=-1):
    """A lens key's comma-separated numbers; field `absat` may carry a leading `=`. ValueError if any is not one."""
    out = []
    for i, x in enumerate(v.split(",")):
        if i == absat and x.startswith("="):
            x = x[1:]
        out.append(float(x))
    return out


PIPE_KEYS = ("hpipe", "ppipe", "p2pipe", "p3pipe")        # ⭐ ep 11: sticky until `off`
PIPE_AIR_KEYS = ("hair", "pair", "p2air", "p3air")        # ⭐ ep 11: sticky until `off`
# ⛔ A SHOT NAME IS CHECKED HERE AS WELL AS IN THE ENGINE. The engine now says
# "[scene] line N: unknown shot 'x' -- using ots" instead of falling to OTS in silence --
# but that is a line in a log read after a take. This costs nothing and is read first.
SHOTS = ("wide", "medium", "two", "ots", "tight", "solo")
# beat=<seconds>: extra silence BEFORE this line, on top of the uniform gap. NOT
# sticky -- a pause is an event, not a state. It is the difference between a scene
# that is read and a scene that is played: the silence after a confession is doing
# as much work as the confession.


# ---------------------------------------------------------------- VISEMES
# per character: (jaw 0..1, relative duration). The jaw number is how far a mouth opens
# on that sound; the duration is why "sees" and "stop" do not take the same time.
VIS = {
    "a": (1.00, 1.7), "e": (0.58, 1.5), "i": (0.34, 1.4), "o": (0.70, 1.6), "u": (0.46, 1.4),
    "y": (0.36, 1.2),
    "m": (0.00, 1.0), "b": (0.00, 0.8), "p": (0.00, 0.8),   # <- the closures. everything.
    "f": (0.16, 1.0), "v": (0.18, 1.0), "w": (0.24, 1.0),
    "s": (0.18, 1.2), "z": (0.20, 1.1), "c": (0.22, 1.0), "x": (0.22, 1.0),
    "t": (0.26, 0.8), "d": (0.28, 0.8), "n": (0.24, 1.0), "l": (0.30, 1.0),
    "r": (0.34, 1.1), "g": (0.30, 0.9), "k": (0.26, 0.8), "h": (0.30, 0.8),
    "j": (0.26, 1.0), "q": (0.26, 1.0),
    " ": (0.06, 0.7),                                        # a mouth relaxes between words
}
VIS_DEFAULT = (0.26, 1.0)


def viseme_track(text, dur, fps=30.0):
    """A jaw-opening per 1/30s frame, laid across the line by rough phoneme length."""
    units = []
    prev = ""
    for ch in text.lower():
        if ch in ",.!?;:":
            units.append((0.05, 1.4))          # punctuation is a mouth at rest
            prev = ""
            continue
        if not (ch.isalpha() or ch == " "):
            continue
        # a doubled letter is one sound, not two ("stopp" is not slower than "stop")
        if ch == prev and ch != " ":
            continue
        prev = ch
        units.append(VIS.get(ch, VIS_DEFAULT))
    n = max(1, int(round(dur * fps)))
    if not units:
        return [0] * n
    total = sum(u[1] for u in units)
    out, t, ui, acc = [], 0.0, 0, 0.0
    step = total / float(n)
    for _ in range(n):
        while ui < len(units) - 1 and acc + units[ui][1] < t + step * 0.5:
            acc += units[ui][1]
            ui += 1
        out.append(units[ui][0])
        t += step
    # A JAW HAS MASS. Ramping straight between neighbouring targets chatters at 30Hz and
    # looks worse than no shaping at all; two passes of a 3-tap smooth give it weight
    # while leaving the closures deep enough to read.
    for _ in range(2):
        out = [(out[max(0, i-1)] + 2.0 * out[i] + out[min(len(out)-1, i+1)]) * 0.25
               for i in range(len(out))]
    return [max(0, min(100, int(round(v * 100)))) for v in out]


def main(script, gap=0.42, out=None):
    here = os.path.dirname(os.path.abspath(script))
    out = out or os.path.splitext(os.path.basename(script))[0]
    lines = []
    cast_names = {"0": "REVA", "1": "KAELI"}
    # ⛔ THE CAPTION FONT IS BITMAP ASCII. Anything above 0x7E draws as junk, and the
    # scripts are full of typographic punctuation because it reads better and because
    # Piper treats a dash as a real prosodic break. Fold on the way into the .scn only:
    # the script keeps its punctuation, the screen keeps its alphabet.
    SCREEN = {
        0x2014: " - ", 0x2013: "-", 0x2018: "'", 0x2019: "'",
        0x201C: '"', 0x201D: '"', 0x2026: "...", 0x00A0: " ",
    }

    def screen_text(t):
        t = t.translate(SCREEN)
        t = "".join(c if 32 <= ord(c) <= 126 else "?" for c in t)
        while "  " in t:
            t = t.replace("  ", " ")
        return t.strip()

    sfx = []
    hinge = False
    stage = dict(STAGE0)
    for raw in open(script, encoding="utf-8"):
        s = raw.strip()
        if not s or s.startswith("#"): continue
        # `cast 0=kaeli 1=susan` names who is in the scene; it is say_piper's business
        # (which VOICE each slot gets), not the timeline's, and it carries no dialogue.
        # ⭐ A SOUND EFFECT. `sfx <t> <name> [gain]`, in ABSOLUTE scene seconds -- a sound
        # is placed against the cut, not against whoever is speaking, so re-voicing a line
        # cannot drag the knock at the end of act V somewhere else.
        if s.startswith("sfx "):
            bits = s.split()
            if len(bits) >= 3:
                sfx.append((float(bits[1]), bits[2],
                            float(bits[3]) if len(bits) > 3 else 1.0))
            continue
        # ⭐ `mouth hinge` -- the puppet mouth (design/stop_motion.md): the jaw on loudness
        # alone, no viseme track, so no closures and no shapes. That IS the look.
        if s.startswith("mouth "):
            hinge = (s.split()[1:2] == ["hinge"])
            continue
        if s.startswith("cast "):
            # The cast reaches the SCREEN as well as the voices: the engine's caption was
            # hard-wired to REVA/KAELI by speaker index, so susan's dialogue came up over
            # kaeli's name.
            for bb in s.split()[1:]:
                if "=" in bb:
                    sl, who = bb.split("=", 1)
                    cast_names[sl] = who.upper()
            continue
        head, text = s.split("|", 1)
        bits = head.split()
        # the speaker may carry per-line DIRECTION for the voice (rate/var/pitch) and
        # per-line STAGING for the bodies. The timeline wants the staging; the voice
        # keys belong to say_piper.py and are skipped.
        beat = 0.0
        snap = ""
        once = {}        # ⭐ ep 10: this line's tricks, never carried to the next
        for b in bits[1:]:
            if "=" not in b: continue
            k, v = b.split("=", 1)
            if k in VOICE_KEYS: continue
            if k == "beat": beat = float(v); continue
            if k == "snap": snap = v; continue      # NOT sticky: this line's marks land on the cut
            if k not in STAGE0:
                raise SystemExit("unknown key '%s=' on: %s" % (k, s[:60]))
            if k in TRICK_KEYS:   # ⭐ ep 10: THIS line only -- and a malformed one stops the compile here
                for ev in v.split(","):
                    nm, _, at = ev.partition("@")
                    try:
                        ok = bool(nm) and float(at) >= 0.0
                    except ValueError:
                        ok = False
                    if not ok:
                        raise SystemExit("bad %s='%s' on: %s\n    it is clip@sec[,clip@sec...], sec >= 0 from the line's start"
                                         % (k, v, s[:60]))
                once[k] = v
                continue
            if k in DOLL_KEYS:    # ⭐ THIS line only: one `sec`, or `off`
                ok = v == "off"
                if not ok:
                    try:
                        ok = float(v) >= 0.0
                    except ValueError:
                        ok = False
                if not ok:
                    raise SystemExit("bad %s='%s' on: %s\n    it is one sec >= 0 from the line's start, or `off` (back on the clip)"
                                     % (k, v, s[:60]))
                once[k] = v
                continue
            if k in PUFF_KEYS:    # ⭐ ep 12: THIS line only, and checked here rather than in a take's log
                try:
                    secs = [float(x) for x in v.split(",")]
                    ok = all(x >= 0.0 for x in secs) and len(secs) <= 8 and len(v) <= 63
                except ValueError:
                    ok = False
                if not ok:
                    raise SystemExit("bad %s='%s' on: %s\n    it is sec[,sec...] (at most 8, sec >= 0 from the line's start)"
                                     % (k, v, s[:60]))
                once[k] = v
                continue
            if k == "home" and not os.path.exists(os.path.join(here, "..", "sets", v + ".set")):
                raise SystemExit("home=%s: there is no sets/%s.set, on: %s" % (v, v, s[:60]))
            if k == "set":        # ⭐ 10-22: a CUTAWAY -- this line only
                if not os.path.exists(os.path.join(here, "..", "sets", v + ".set")):
                    raise SystemExit("set=%s: there is no sets/%s.set, on: %s" % (v, v, s[:60]))
                once[k] = v
                continue
            if k == "shot" and v not in SHOTS:
                raise SystemExit("unknown shot '%s' on: %s\n    it is one of: %s"
                                 % (v, s[:60], " ".join(SHOTS)))
            stage[k] = v          # STICKY: it holds until something changes it
        # ⭐ ep 10 --------------------------------------------------------------------------------
        # WHAT THIS LINE SAID, as opposed to what it carried: the rules below are about the script.
        said = set(b.split("=", 1)[0] for b in bits[1:] if "=" in b)
        # dur= is this line's own length (never carried) and makes it an ACTION line.
        dur = stage["dur"]
        stage["dur"] = ""
        if dur:
            try:
                dur = float(dur)
            except ValueError:
                raise SystemExit("dur= wants seconds, on: %s" % s[:60])
        if text.strip() and dur:
            raise SystemExit("dur= is for an ACTION line (nothing after `|`), on: %s" % s[:60])
        if not text.strip() and not (dur and dur > 0):
            raise SystemExit("nothing after `|` is an ACTION line and needs dur=<seconds>, on: %s" % s[:60])
        # ⛔ A PATH AND A MARK CANNOT BOTH BE IN FORCE FOR ONE BODY. Both are sticky, and the engine
        # re-applies a carried mark on every line -- which would walk a body that has just rolled a
        # path straight back to the mark it started on. A path ends on its own last mark.
        PATH_MARK = (("hpath", "hmark"), ("ppath", "pmark"), ("p2path", "p2mark"), ("p3path", "p3mark"))
        for pk, mk in PATH_MARK:
            lk = pk.replace("path", "loop")
            if lk in said:
                if stage[lk] not in ("0", "1"):
                    raise SystemExit("%s= is 1 (keep circling a closed route) or 0, on: %s" % (lk, s[:60]))
                if stage[lk] == "0":
                    stage[lk] = ""        # 0 is said once, never carried
            if pk in said:
                if stage[pk] == "off":
                    stage[pk] = ""
                    stage[lk] = ""
                elif mk in said:
                    print("  !! %s= and %s= on one line: the path wins (it ends on its last mark)" % (pk, mk))
                stage[mk] = ""
            elif mk in said:
                stage[pk] = ""
                stage[lk] = ""            # the loop belongs to the path a mark just ended
            sk = pk.replace("path", "spd")
            if sk in said:
                try:
                    float(stage[sk])
                except ValueError:
                    raise SystemExit("%s= wants u/s, on: %s" % (sk, s[:60]))
        # THE LENS: camtrack / campan hold until `off` or a line that says cam=. cam= is sticky here,
        # so a tracking line drops the carried pin -- and a pan KEEPS it, because the pin is its eye.
        # ⭐ ep 11: camorbit holds the same way; cammove and camsecs are events; camshake is sticky.
        if "camshake" in said and stage["camshake"] == "off":
            stage["camshake"] = "0"
        for lk, (want, absat, fmt) in LENS_FMT.items():
            if lk in said and stage[lk] == "off" and lk in ("cammove", "camsecs"):
                raise SystemExit("%s= has no `off` -- a move ends where it lands, and cam= places a new lens, on: %s"
                                 % (lk, s[:60]))
            if lk in said and stage[lk] != "off":
                try:
                    nv = lens_nums(stage[lk], absat)
                except ValueError:
                    nv = []
                if len(nv) not in want:
                    raise SystemExit("%s= wants %s, on: %s" % (lk, fmt, s[:60]))
                if lk == "camshake" and (nv[0] < 0.0 or (len(nv) > 1 and nv[1] <= 0.0)):
                    raise SystemExit("camshake= wants amp >= 0 (u) and hz > 0, on: %s" % s[:60])
                if lk == "camsecs" and nv[0] <= 0.0:
                    raise SystemExit("camsecs= wants seconds > 0, on: %s" % s[:60])
        modes = [k for k in ("camtrack", "campan", "camorbit", "cammove", "camhold") if k in said and stage[k] != "off"]
        if len(modes) > 1:
            raise SystemExit("one lens mode per line (%s), on: %s" % (" + ".join(modes), s[:60]))
        if "camsecs" in said and not ({"cammove", "camorbit"} & said):
            raise SystemExit("camsecs= times a cammove= or camorbit= said on the SAME line, on: %s" % s[:60])
        if "camhold" in said and stage["camhold"] != "off":
            stage["cam"] = ""
            stage["camtrack"] = ""
            stage["campan"] = ""
            stage["camorbit"] = ""
        elif said & {"camtrack", "campan", "camorbit", "cammove"} or ("cam" in said and stage.get("cam") not in ("", "off")):
            stage["camhold"] = ""      # any other lens named on a line takes the camera out of her hand
        if "camtrack" in said and stage["camtrack"] != "off":
            stage["cam"] = ""
            if "campan" not in said:
                stage["campan"] = ""
            stage["camorbit"] = ""
        if "campan" in said and stage["campan"] != "off" and "camtrack" not in said:
            stage["camtrack"] = ""
            stage["camorbit"] = ""
            if "cam" not in said and stage["cam"] == "off":
                stage["cam"] = ""     # a carried cam=off would hand the pan's lens back every line
        if "camorbit" in said and stage["camorbit"] != "off":
            stage["cam"] = ""         # an orbit places its own eye
            stage["camtrack"] = ""
            stage["campan"] = ""
        if "cammove" in said:
            # ⛔ a pan carries its cam= because the pin is its EYE -- but the pan was AIMING at a body, so starting a
            # move from that carried pin would jump the aim on the cut. With no cam= of its own it starts from the
            # lens it has, which is where the pan really was looking.
            if "cam" not in said and stage["campan"]:
                stage["cam"] = ""
            stage["camtrack"] = ""    # a move is a placed lens: it ends any lens that follows a body
            stage["campan"] = ""
            stage["camorbit"] = ""
            if stage["cam"] == "off":
                stage["cam"] = ""
            if not stage["cam"] and len(stage["cammove"].split(",")) < 7:
                print("  !! cammove= with no cam= and no fov (it moves from the lens it has): the lens after it is "
                      "drawn at the scene's default fov -- give cammove= a 7th number, on: %s" % s[:60])
        if "cam" in said:
            if "camtrack" not in said:
                stage["camtrack"] = ""
            if "campan" not in said:
                stage["campan"] = ""
            if "camorbit" not in said:
                stage["camorbit"] = ""
        # ⭐ ep 11: THE HALF-PIPE -- checked here, where a typo stops the compile instead of a take
        for pk in PIPE_KEYS:
            if pk in said and stage[pk] != "off":
                nm, _, air = stage[pk].partition(",")
                try:
                    ok = bool(nm) and (air == "" or float(air) >= 0.0)
                except ValueError:
                    ok = False
                if not ok:
                    raise SystemExit("bad %s='%s' on: %s\n    it is <halfpipe name>[,<air above the coping, u>] or off"
                                     % (pk, stage[pk], s[:60]))
        for ak in PIPE_AIR_KEYS:
            if ak in said and stage[ak] != "off" and not all(stage[ak].split(",")):
                raise SystemExit("bad %s='%s' on: %s\n    it is <clip>[,<clip>...] or off" % (ak, stage[ak], s[:60]))
        # ⭐ 10-22: camat= is the orbit's centre: checked here, and never left behind when the orbit ends
        if "camat" in said:
            try:
                ok = len([float(x) for x in stage["camat"].split(",")]) == 3
            except ValueError:
                ok = False
            if not ok:
                raise SystemExit("camat= wants x,y,z (u), on: %s" % s[:60])
        if stage["camorbit"] in ("", "off"):
            if "camat" in said:
                raise SystemExit("camat= is a camorbit's centre -- say it with camorbit=, on: %s" % s[:60])
            stage["camat"] = ""
        st_ = dict(stage)
        st_.update(once)
        for lk in ("camtrack", "campan", "camorbit") + PIPE_KEYS + PIPE_AIR_KEYS:
            if stage[lk] == "off":
                stage[lk] = ""        # `off` is said on its own line; after it nothing is carried
        # ⭐ ep 11: THE EVENTS. A move's END is carried on as the next line's cam= -- with the start's fov when the end
        # names none, exactly as the engine holds it -- so the lens stays where the move put it instead of snapping
        # back to where it began. camsecs= and camshake=0 are this line's only.
        if stage["cammove"]:
            mv = stage["cammove"].split(",")
            c0 = st_.get("cam", "").split(",")
            if len(mv) == 6 and len(c0) == 7:
                mv.append(c0[6])
            stage["cam"] = ",".join(mv)
            stage["cammove"] = ""
        stage["camsecs"] = ""
        if stage["camshake"] and float(stage["camshake"].split(",")[0]) == 0.0:
            stage["camshake"] = ""
        if dur:
            st_["dur"] = "%.3f" % dur
        if snap:
            st_["snap"] = snap
        lines.append((int(bits[0]), text.strip(), st_, beat))
    wdir = os.path.join(here, "wav", out)
    if not os.path.isdir(wdir): wdir = os.path.join(here, "wav")   # scenes spoken before the split
    sr = nch = sw = None
    tracks = []
    t = 0.0
    for i, (spk, text, stg, beat) in enumerate(lines):
        t += beat
        if stg.get("dur"):
            # ⭐ ep 10: AN ACTION LINE. No wav, no envelope, no visemes: a stretch of the timeline the
            # bodies and the lens own. It keeps its number, so every later line_NN.wav still lines up.
            dd = float(stg["dur"])
            tracks.append((t, spk, text, b"", dd, [], stg, []))
            t += dd + gap
            continue
        path = os.path.join(wdir, "line_%02d.wav" % i)
        w = wave.open(path, "rb")
        if sr is None:
            sr, nch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
        assert (w.getframerate(), w.getnchannels(), w.getsampwidth()) == (sr, nch, sw), \
            "line %d has a different wav format" % i
        data = w.readframes(w.getnframes())
        w.close()
        dur = w.getnframes() / float(sr)
        tracks.append((t, spk, text, data, dur, rms_env(data, sr, nch, sw), stg,
                       viseme_track(text, dur)))
        t += dur + gap
    total = t - gap
    if sr is None:      # ⭐ ep 10: a scene of nothing but action lines has no wav to take a format from
        sr, nch, sw = 24000, 1, 2
    # mix: one buffer, every line written at its own offset
    n = int(total * sr) + sr // 2
    buf = [0] * (n * nch)
    for (start, spk, text, data, dur, env, stg, vis) in tracks:
        off = int(start * sr) * nch
        vals = struct.unpack("<%dh" % (len(data)//2), data)
        for k, v in enumerate(vals):
            if off + k < len(buf): buf[off + k] = max(-32768, min(32767, buf[off + k] + v))
    # ⭐ THE EFFECTS, mixed into the same buffer the voices are in. No engine change is
    # needed for any of this: the engine already plays one wav and muxes it against the
    # picture, so a sound effect is a source at an offset like every line is.
    sdir = os.path.join(here, "sfx")
    for (st, nm, gain) in sfx:
        sp = os.path.join(sdir, nm + ".wav")
        if not os.path.exists(sp):
            print("  !! sfx '%s' not found (%s)" % (nm, sp))
            continue
        # ⛔ NOT `sw` -- that is the SCENE's sample width, three lines of mixing above,
        # and shadowing it handed a Wave_read object to wo.setsampwidth(). The wav came out
        # zero bytes and the only symptom was an EOFError in whatever opened it next.
        sfw = wave.open(sp, "rb")
        if (sfw.getnchannels(), sfw.getsampwidth()) != (nch, sw):
            print("  !! sfx '%s' is x%d x%dB, scene is x%d x%dB -- skipped"
                  % (nm, sfw.getnchannels(), sfw.getsampwidth(), nch, sw))
            sfw.close()
            continue
        srate = sfw.getframerate()
        sd = sfw.readframes(sfw.getnframes())
        sfw.close()
        vals = struct.unpack("<%dh" % (len(sd) // 2), sd)
        # ⭐ A DIFFERENT RATE IS RESAMPLED, NOT SKIPPED. The shelf is 22050 Hz (Piper's rate)
        # and Kokoro speaks at 24000, so on every Kokoro scene each effect was dropped with one
        # log line nobody reads -- a knock that is in the script and not in the film.
        if srate != sr:
            if nch != 1:
                print("  !! sfx '%s' is %dHz stereo -- skipped" % (nm, srate))
                continue
            n_out = int(len(vals) * sr / float(srate))
            res = []   # NOT `out` -- that is the scene name, and shadowing it broke the wav path
            for i in range(n_out):
                x = i * srate / float(sr)
                i0 = int(x)
                a0 = vals[i0] if i0 < len(vals) else 0
                a1 = vals[i0 + 1] if i0 + 1 < len(vals) else a0
                res.append(int(a0 + (a1 - a0) * (x - i0)))
            vals = res
            print("  sfx  '%s' resampled %d -> %d Hz" % (nm, srate, sr))
        off = int(st * sr) * nch
        for k, v in enumerate(vals):
            if 0 <= off + k < len(buf):
                buf[off + k] = max(-32768, min(32767, buf[off + k] + int(v * gain)))
        print("  sfx  %6.2fs  %-12s x%.2f" % (st, nm, gain))
    wo = wave.open(os.path.join(here, out + ".wav"), "wb")
    wo.setnchannels(nch); wo.setsampwidth(sw); wo.setframerate(sr)
    wo.writeframes(struct.pack("<%dh" % len(buf), *buf))
    wo.close()
    with open(os.path.join(here, out + ".scn"), "w", encoding="utf-8") as f:
        f.write("scene 1 %s.wav %.3f %d\n" % (out, total, len(tracks)))
        # ⭐ UP TO FIVE NAMES (ep 7: four bodies + the voice). The engine captions by SLOT, so a third speaker's
        # dialogue no longer comes up over the second speaker's name.
        f.write("cast %s\n" % " ".join(
            cast_names.get(str(i), ("REVA", "KAELI", "SUSAN", "ARCHIVE", "-")[i]) for i in range(5)))
        for (start, spk, text, data, dur, env, stg, vis) in tracks:
            f.write("line %d %.3f %.3f %s\n" % (spk, start, dur, screen_text(text)))
            f.write("env %d %s\n" % (len(env), " ".join("%d" % round(v*100) for v in env)))
            # ⛔ AND DO NOT WRITE EMPTY KEYS. STAGE0 carries hx/hz so a line CAN move the
            # pair; written blank on every other line, the engine read atof("") = 0 and
            # walked them to world origin. Only emit a key that has a value.
            f.write("stage %s\n" % " ".join("%s=%s" % (k, stg[k]) for k in sorted(stg)
                                             if str(stg[k]) != ""))
            if not hinge:
                f.write("vis %d %s\n" % (len(vis), " ".join("%d" % v for v in vis)))
    print("%s.wav: %.1fs, %d Hz x %d ch" % (out, total, sr, nch))
    prev = {}
    for (start, spk, text, data, dur, env, stg, vis) in tracks:
        ch = " ".join("%s=%s" % (k, v) for k, v in sorted(stg.items()) if prev.get(k) != v)
        prev = stg
        print("   %5.2f +%4.2f  %s  %-52s %s" % (start, dur, cast_names.get(str(spk), "?")[:5].lower(),
                                                 (text or "(action)")[:52], ch))
    print("TOTAL %.1f seconds" % total)

if __name__ == "__main__":
    a = sys.argv[1:]
    g = float(a[a.index("--gap")+1]) if "--gap" in a else 0.42
    o = a[a.index("--out")+1] if "--out" in a else None
    main(a[0], g, o)
