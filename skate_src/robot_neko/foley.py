#!/usr/bin/env python3
"""foley.py NAME [...] -- procedural sound effects for the shelf (reva_native/scenes/sfx).

    python skate_src/robot_neko/foley.py clap

ROBOT NEKO needs sounds the shelf does not have, and a clapperboard is two slabs of wood
meeting: a hard broadband crack with almost no tail and a short woody ring. That is a few
lines of synthesis, not an asset hunt. Written in the shelf's own format (22050 Hz, mono,
16-bit); compile_scene.py resamples to whatever the scene speaks at.
"""
import math, os, random, struct, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
SFX = os.path.normpath(os.path.join(HERE, "..", "..", "reva_native", "scenes", "sfx"))
SR = 22050


def clap():
    rnd = random.Random(902)
    n = int(SR * 0.32)
    out, hp_prev_in, hp_prev_out = [], 0.0, 0.0
    for i in range(n):
        t = i / SR
        crack = rnd.uniform(-1, 1) * math.exp(-t / 0.018)                # the two boards meeting
        # a one-pole high-pass takes the thud out of the noise so it cracks instead of booms
        y = 0.93 * (hp_prev_out + crack - hp_prev_in)
        hp_prev_in, hp_prev_out = crack, y
        wood = (math.sin(2 * math.pi * 1650 * t) * 0.55 + math.sin(2 * math.pi * 2710 * t) * 0.3) \
            * math.exp(-t / 0.035)                                        # the slate ringing
        pre = rnd.uniform(-1, 1) * 0.25 if i < int(SR * 0.004) else 0.0  # the hinge
        out.append(y * 0.9 + wood * 0.45 + pre)
    return out


def update_chime():
    """A device's two-tone 'update' notification, and its second note STUTTERS -- a bit-crushed
    attack twice before it rings. ROBOT NEKO ep 3: reva is getting a firmware update, and the
    audience has to learn the sound before the last one arrives unannounced."""
    def tone(f, dur):
        o = []
        for i in range(int(SR * dur)):
            t = i / SR
            env = min(1.0, t / 0.006) * math.exp(-t / (dur * 0.45))
            o.append((math.sin(2 * math.pi * f * t) + 0.25 * math.sin(4 * math.pi * f * t)
                      + 0.08 * math.sin(6 * math.pi * f * t)) * env)
        return o
    lo, hi = tone(880.0, 0.11), tone(1318.5, 0.24)                       # A5 -> E6
    crush = [round(s * 6) / 6 for s in hi[:int(SR * 0.025)]]              # 6 levels: a cheap DAC
    pause = [0.0] * int(SR * 0.012)
    return lo + [0.0] * int(SR * 0.03) + crush + pause + crush + pause + hi


def click():
    """A STILLS CAMERA'S SHUTTER, for ROBOT NEKO ep 6: every stillness test is a verdict, and a
    verdict wants the sound of the thing that judged it. A shutter is not one click -- it is two
    mechanical events a few tens of milliseconds apart (the blade opening, the blade closing)
    with a small metal body ringing around them. One click reads as a switch; two read as a
    camera, and the gap is what does it.

    Deliberately DRY: the show is action figures on a table, and a reverby shutter would put the
    camera in a hall. Almost no tail."""
    rnd = random.Random(1206)
    n = int(SR * 0.15)
    gap = int(SR * 0.032)                                                 # blade open -> blade shut
    out, hp_in, hp_out = [], 0.0, 0.0
    for i in range(n):
        t = i / SR
        # the two blade events: a hard, very short noise burst each
        snap = 0.0
        for k, (at, amp) in enumerate(((0, 1.0), (gap, 0.72))):
            if i >= at:
                snap += rnd.uniform(-1, 1) * amp * math.exp(-(i - at) / SR / 0.0035)
        # high-passed so it cracks rather than thumps -- a shutter has no low end
        y = 0.95 * (hp_out + snap - hp_in)
        hp_in, hp_out = snap, y
        # the body ringing: two close metal partials, gone almost immediately
        ring = (math.sin(2 * math.pi * 3150 * t) * 0.5 + math.sin(2 * math.pi * 4820 * t) * 0.26) \
            * math.exp(-t / 0.012)
        out.append(y * 1.0 + ring * 0.3)
    return out


def _clapburst(rnd, out, t0, dur_c, amp):
    """One hand-clap in a crowd: a short noise burst, high-passed so it cracks instead of
    thuds (same shape as clap()'s single clapperboard hit, just much shorter and softer --
    a crowd claps with open palms, not two boards). Adds into `out` starting at t0 seconds;
    silently clips at the buffer end so callers don't have to bounds-check per event."""
    i0 = int(t0 * SR); n = int(SR * dur_c); hp_prev = 0.0
    for k in range(n):
        if i0 + k >= len(out):
            return
        t = k / SR
        burst = rnd.uniform(-1, 1) * math.exp(-t / (dur_c * 0.35))
        y = burst - hp_prev * 0.4
        hp_prev = burst
        out[i0 + k] += y * amp


def _whoop(rnd, out, t0, wl, amp, f0, f1):
    """One pitched voice rising into the crowd noise and fading -- a glide from f0 toward
    f1 and back down, windowed by a half-sine so it has no click at either end."""
    i0 = int(t0 * SR); n = int(SR * wl)
    for k in range(n):
        if i0 + k >= len(out):
            return
        t = k / SR; ph = t / wl
        env = math.sin(math.pi * min(1.0, ph)) ** 1.1
        freq = f0 + (f1 - f0) * math.sin(math.pi * ph * 0.5)
        out[i0 + k] += math.sin(2 * math.pi * freq * t) * env * amp


def crowd():
    """A STADIUM CROWD for ROBOT NEKO ep 11 (the half-pipe): ~8s, LOOPABLE. Three layers --
    band-limited noise with slow swells (the murmur/roar bed), hundreds of short random
    claps (applause, the same crack-not-thud shape as clap() but tiny), and a handful of
    pitched whoops rising out of it. Looped by rendering an extra crossfade tail and
    blending it back over the head (a synthesised loop has no natural seam to hide, so one
    is built on purpose) rather than just cutting at 8s and hoping the boundary is quiet."""
    rnd = random.Random(1102)
    dur, xfade = 8.0, 0.35
    n = int(SR * (dur + xfade))
    out = [0.0] * n

    lp = 0.0                                                          # the murmur/roar bed
    for i in range(n):
        t = i / SR
        lp += 0.06 * (rnd.uniform(-1, 1) - lp)                        # one-pole low-pass: hiss -> murmur
        swell = (0.55 + 0.25 * math.sin(2 * math.pi * t / 3.1 + 0.4)
                 + 0.15 * math.sin(2 * math.pi * t / 1.7 + 1.1)
                 + 0.10 * math.sin(2 * math.pi * t / 5.3))            # three slow, unsynced swells
        out[i] += lp * 0.38 * max(0.15, swell)

    for _ in range(420):                                              # hundreds of short random claps
        _clapburst(rnd, out, rnd.uniform(0, dur), rnd.uniform(0.012, 0.03), rnd.uniform(0.22, 0.8))

    for _ in range(5):                                                # a few pitched whoops
        f0 = rnd.uniform(320, 420)
        _whoop(rnd, out, rnd.uniform(0.3, dur - 1.3), rnd.uniform(0.8, 1.3), 0.16,
               f0, f0 * rnd.uniform(1.5, 1.9))

    xn, loopN = int(SR * xfade), int(SR * dur)                        # fold the tail over the head
    for i in range(xn):
        f = i / xn
        out[i] = out[i] * f + out[loopN + i] * (1.0 - f)
    return out[:loopN]


def crowdpop():
    """A ~2.5s SWELL for a landed trick: the same murmur-plus-claps-plus-whoop recipe as
    crowd(), but ONE SHOT -- it rises fast (claps build in probability and loudness toward
    the front rather than scattering evenly) and decays away rather than looping."""
    rnd = random.Random(2606)
    dur = 2.5
    n = int(SR * dur)
    out = [0.0] * n

    lp = 0.0
    for i in range(n):
        t = i / SR
        lp += 0.08 * (rnd.uniform(-1, 1) - lp)
        env = math.sin(math.pi * min(1.0, t / dur)) ** 0.6 * math.exp(-max(0.0, t - dur * 0.55) / 0.9)
        out[i] += lp * 0.6 * env

    for _ in range(150):                                              # the burst builds through the take
        t0 = rnd.uniform(0, dur * 0.85)
        amp = (0.3 + 0.7 * (t0 / dur)) * rnd.uniform(0.35, 1.0)
        _clapburst(rnd, out, t0, rnd.uniform(0.012, 0.026), amp * 0.55)

    for _ in range(2):                                                # one or two whoops right on the peak
        f0 = rnd.uniform(340, 420)
        _whoop(rnd, out, rnd.uniform(0.15, 0.5), rnd.uniform(1.1, 1.6), 0.22,
               f0, f0 * rnd.uniform(1.4, 1.8))
    return out


# ============================ EP 12: 1AM BEDROOM ====================================
# jacob: "up our quality" -- sound is half of it. Every maker below stays under the
# clap/click/crowd house style (filtered noise + envelopes) and deliberately AVOIDS a bare
# oscillator standing alone in the mix: even the clock's knock and the lighter's whoomph are
# a sine UNDER a noise burst, never a tone by itself, because a lone sine reads as a test
# tone and a night bedroom should not.

def _onepole_hp(rnd, n, coeff=0.9):
    """n samples of noise through a one-pole high-pass (the same first-difference-with-
    feedback idiom clap()/click() already use) -- a hiss/crackle/paper texture rather than
    a thud. Returns a plain list."""
    out = []; hp_in = hp_out = 0.0
    for _ in range(n):
        w = rnd.uniform(-1, 1)
        hp_out = coeff * (hp_out + w - hp_in)
        hp_in = w
        out.append(hp_out)
    return out


def roomtone():
    """A QUIET NIGHT-ROOM BED for ep 12 (1am, the bedroom): ~10s, LOOPABLE. This sits under
    dialogue at roughly -40dB, so nothing here is meant to be noticed on its own -- three
    very restrained layers: a low fridge/HVAC motor (mostly a narrow band of filtered noise
    -- the difference of two low-pass stages -- with the faintest steady tone under it, not
    a pure hum), very distant traffic (heavily low-passed noise, slow random swells, much
    quieter and slower than the ep11 stadium crowd), and a thin steady hiss of 'air'. Looped
    like crowd(): rendered with an extra crossfade tail folded back over the head."""
    rnd = random.Random(100112)
    dur, xfade = 10.0, 0.6
    n = int(SR * (dur + xfade))
    out = [0.0] * n

    lpA = lpB = 0.0                                                   # the motor: band = lpA - lpB
    for i in range(n):
        t = i / SR
        wob = 1.0 + 0.004 * math.sin(2 * math.pi * t / 11.0)          # a real motor drifts a hair
        w = rnd.uniform(-1, 1)
        lpA += 0.05 * wob * (w - lpA)
        lpB += 0.012 * wob * (w - lpB)
        band = lpA - lpB
        tone = math.sin(2 * math.pi * 58.0 * wob * t) * 0.05          # barely-there electrical pitch
        out[i] += band * 0.30 + tone

    lp1 = lp2 = 0.0                                                   # distant traffic
    for i in range(n):
        t = i / SR
        w = rnd.uniform(-1, 1)
        lp1 += 0.04 * (w - lp1)
        lp2 += 0.04 * (lp1 - lp2)
        swell = 0.5 + 0.3 * math.sin(2 * math.pi * t / 9.7 + 0.8) + 0.2 * math.sin(2 * math.pi * t / 17.3)
        out[i] += lp2 * 0.55 * max(0.08, swell)

    air = _onepole_hp(rnd, n, 0.85)                                    # a thin, steady hiss
    for i in range(n):
        out[i] += air[i] * 0.028

    xn, loopN = int(SR * xfade), int(SR * dur)
    for i in range(xn):
        f = i / xn
        out[i] = out[i] * f + out[loopN + i] * (1.0 - f)
    return out[:loopN]


def clock():
    """A SOFT ANALOGUE WALL-CLOCK TICK for ep 12: 1 Hz, LOOPABLE, ~8s (8 beats). Alternates
    'tick'/'tock' a little (a real escapement's two strokes are not identical) -- a short
    high-passed click plus a soft wooden knock underneath, both fully decayed well inside
    the 1s gap, so the loop needs no crossfade: it is silence at both ends by construction."""
    rnd = random.Random(120100)
    dur = 8.0
    n = int(SR * dur)
    out = [0.0] * n
    for beat in range(8):
        i0 = int(beat * SR)
        f = 1450.0 if beat % 2 == 0 else 1180.0
        hp_in = hp_out = 0.0
        nlen = int(SR * 0.05)
        for k in range(nlen):
            if i0 + k >= n:
                break
            t = k / SR
            w = rnd.uniform(-1, 1) * math.exp(-t / 0.012)
            hp_out = 0.6 * (hp_out + w - hp_in)
            hp_in = w
            knock = math.sin(2 * math.pi * f * t) * math.exp(-t / 0.018)
            out[i0 + k] += hp_out * 0.55 + knock * 0.30
    return out


def sheets():
    """~1.5s of BEDSHEET RUSTLE (someone rolling over) for ep 12. Real fabric noise is
    hundreds of tiny friction grains, not one texture, so this sums ~260 short high-passed
    noise grains at random times/amplitudes inside one overall rise-and-settle envelope --
    a single shoulder of movement, not a loop."""
    rnd = random.Random(150100)
    dur = 1.5
    n = int(SR * dur)
    out = [0.0] * n
    for _ in range(260):
        t0 = rnd.uniform(0, dur - 0.02)
        shoulder = math.sin(math.pi * min(1.0, t0 / dur)) ** 0.7      # rises, settles
        amp = shoulder * rnd.uniform(0.18, 0.6)
        dur_g = rnd.uniform(0.006, 0.022)
        i0 = int(t0 * SR); nlen = int(SR * dur_g)
        hp_in = hp_out = 0.0
        for k in range(nlen):
            if i0 + k >= n:
                break
            tt = k / SR
            w = rnd.uniform(-1, 1) * math.exp(-tt / (dur_g * 0.6))
            hp_out = 0.9 * (hp_out + w - hp_in)
            hp_in = w
            out[i0 + k] += hp_out * amp
    return out


def lighter():
    """A ZIPPO-ISH FLICK AND CATCH, ~0.8s, for ep 12 (kaeli at the window). Three small
    events, not one 'fire' sound: the flint wheel's dry scrape (noise through a high-pass
    whose cutoff creeps up as the wheel spins faster), a couple of individual flint sparks
    inside it, then -- a beat later -- the catch: a soft low-passed whoosh with the
    faintest low tone as the flame steadies."""
    rnd = random.Random(160100)
    dur = 0.8
    n = int(SR * dur)
    out = [0.0] * n

    scrape_len = int(SR * 0.09)                                       # 1. the scrape
    hp_in = hp_out = 0.0
    for k in range(scrape_len):
        t = k / SR
        cutoff = 0.25 + 0.35 * (t / 0.09)
        w = rnd.uniform(-1, 1)
        hp_out = cutoff * (hp_out + w - hp_in)
        hp_in = w
        env = 1.0 if t < 0.05 else math.exp(-(t - 0.05) / 0.02)
        out[k] += hp_out * 0.55 * env
    for i0s in (int(SR * 0.02), int(SR * 0.05), int(SR * 0.075)):      # a few flint sparks
        for k in range(int(SR * 0.006)):
            if i0s + k >= n:
                break
            t = k / SR
            out[i0s + k] += rnd.uniform(-1, 1) * math.exp(-t / 0.003) * 0.5

    i0 = int(SR * 0.15); lp = 0.0                                      # 2. the catch
    for k in range(n - i0):
        t = k / SR
        w = rnd.uniform(-1, 1)
        lp += 0.09 * (w - lp)
        env = min(1.0, t / 0.02) * math.exp(-t / 0.35)
        out[i0 + k] += lp * 0.5 * env
        if t < 0.25:
            out[i0 + k] += math.sin(2 * math.pi * 140 * t) * math.exp(-t / 0.09) * 0.12
    return out


def drag():
    """~1.0s of a CIGARETTE being drawn on for ep 12: a thin hiss of air under sparse,
    tiny paper/tobacco crackle-pops that thicken toward the middle of the draw (when it's
    burning hottest) and thin out again as it eases off."""
    rnd = random.Random(170100)
    dur = 1.0
    n = int(SR * dur)
    out = [0.0] * n

    def swell(t):
        return math.sin(math.pi * min(1.0, t / dur)) ** 0.8

    air = _onepole_hp(rnd, n, 0.9)
    for i in range(n):
        out[i] += air[i] * 0.07 * swell(i / SR)

    for _ in range(90):
        t0 = rnd.uniform(0, dur)
        if rnd.random() > swell(t0) + 0.15:                            # thin out off-peak
            continue
        amp = rnd.uniform(0.15, 0.45) * (0.4 + 0.6 * swell(t0))
        i0 = int(t0 * SR); nlen = int(SR * rnd.uniform(0.002, 0.006))
        for k in range(nlen):
            if i0 + k >= n:
                break
            tt = k / SR
            out[i0 + k] += rnd.uniform(-1, 1) * math.exp(-tt / 0.0015) * amp
    return out


def exhale():
    """~1.6s SOFT BREATHY EXHALE for ep 12: band-passed noise (the difference of two
    low-pass stages, so it has the mouth/throat's body rather than a full hiss) under one
    smooth swell-and-fade envelope -- no attack transient, no click, just air leaving."""
    rnd = random.Random(180100)
    dur = 1.6
    n = int(SR * dur)
    out = [0.0] * n
    lp1 = lp2 = 0.0
    for i in range(n):
        t = i / SR
        w = rnd.uniform(-1, 1)
        lp1 += 0.28 * (w - lp1)
        lp2 += 0.11 * (lp1 - lp2)
        band = lp1 - lp2
        env = math.sin(math.pi * min(1.0, t / dur)) ** 1.4            # smooth in, smooth out
        out[i] = band * 0.55 * env
    return out


def steps():
    """TWO OR THREE BARE-FOOT STEPS on a wooden floor, ~1.5s, for ep 12 (kaeli crossing to
    the window). Soft and low -- skin, not a heel -- so each step is low-passed noise plus
    a couple of quiet wood-resonance partials under a fast, gentle decay, never a hard
    click."""
    rnd = random.Random(190100)
    dur = 1.5
    n = int(SR * dur)
    out = [0.0] * n
    for t0 in (0.10, 0.62, 1.12):                                      # an unhurried walk
        amp = rnd.uniform(0.5, 0.75)
        i0 = int(t0 * SR); lp = 0.0
        nlen = int(SR * 0.09)
        for k in range(nlen):
            if i0 + k >= n:
                break
            t = k / SR
            w = rnd.uniform(-1, 1)
            lp += 0.14 * (w - lp)
            env = math.exp(-t / 0.035)
            wood = (math.sin(2 * math.pi * 210 * t) * 0.5 + math.sin(2 * math.pi * 145 * t) * 0.3) \
                * math.exp(-t / 0.05)
            out[i0 + k] += (lp * 0.6 + wood * 0.35) * amp * env
    return out


def crickets():
    """A DISTANT CRICKET CHORUS through an open door, for ep 12 (ONE AM -- jacob: "low energy,
    relaxed, dreamy"): ~8s, LOOPABLE by construction. A field cricket's chirp is a short train of
    pulses (3-4 here, ~14 ms apart) on a carrier near 4.5 kHz, repeated at its own steady period;
    five of them at different pitches, periods and distances. Every chirp is placed MODULO the
    loop length, so a chirp that runs off the end wraps onto the head and the loop has no seam.
    Distance is a one-pole low-pass and a quieter level; a faint smear (two short echoes) keeps
    them from sounding like they are in the room. This sits far under dialogue (~-45 dB)."""
    rnd = random.Random(120419)
    dur = 8.0
    n = int(SR * dur)
    out = [0.0] * n
    bugs = [(4480, 0.40, 1.00), (4720, 0.52, 0.55), (4200, 0.63, 0.40), (4950, 0.47, 0.30), (4350, 0.73, 0.22)]
    for f0, per, amp in bugs:
        k = max(1, round(dur / per)); per = dur / k                   # a whole number of chirps per loop
        ph0 = rnd.uniform(0, per)
        npulse = rnd.choice((3, 4))
        for c in range(k):
            t0 = ph0 + c * per + rnd.uniform(-0.006, 0.006)
            a = amp * rnd.uniform(0.8, 1.0)
            for p in range(npulse):
                ts = t0 + p * 0.014
                pl = int(SR * 0.009)
                for j in range(pl):
                    env = math.sin(math.pi * j / pl) ** 2
                    v = math.sin(2 * math.pi * f0 * (j / SR)) * env * a
                    idx = (int(ts * SR) + j) % n
                    out[idx] += v
    lp = 0.0                                                           # distance: soften the top
    for i in range(n):
        lp += 0.55 * (out[i] - lp)
        out[i] = lp
    sm = [0.0] * n                                                     # a little air around them
    for i in range(n):
        sm[i] = out[i] + 0.35 * out[(i - int(SR * 0.031)) % n] + 0.18 * out[(i - int(SR * 0.067)) % n]
    return sm




# ---- THE SKATE FOLEY (ROBOT NEKO ep 13, GOT IT) -------------------------------------------------------------------
# A skate clip is half sound: the wheels never stop, a pop is the tail hitting concrete, a landing is two trucks a
# few tens of milliseconds apart, and a slam is a body, a slide and a board getting away on its own. None of it
# was on the shelf. Four makers, same format as the rest (22050 Hz mono), placed by `sfx` against the compiled
# line times: `roll` every 4.0 s under a run, `pop` on the trick's cut-in, `land` ~0.5 s after, `slam` on the
# `pdoll=` cue.

def sk_noise(n, seed):
    rnd = random.Random(seed)
    return [rnd.uniform(-1.0, 1.0) for _ in range(n)]


def sk_lp(x, cutoff):
    """one-pole low-pass"""
    a = math.exp(-2.0 * math.pi * cutoff / SR)
    y, out = 0.0, []
    for s in x:
        y = a * y + (1.0 - a) * s
        out.append(y)
    return out


def sk_hp(x, cutoff):
    """one-pole high-pass"""
    a = math.exp(-2.0 * math.pi * cutoff / SR)
    y, prev, out = 0.0, 0.0, []
    for s in x:
        y = a * (y + s - prev)
        prev = s
        out.append(y)
    return out


def roll():
    """Urethane wheels on concrete, 4.0 s, laid end to end under a run. Band-limited noise (a rumble under a hiss),
    modulated by the bearings' wobble and the rider's pump, with the four wheels ticking over a seam every 0.62 s
    (front truck, then back, 11 ms apart per wheel)."""
    n = int(SR * 4.0)
    rnd = random.Random(1313)
    base = sk_lp(sk_hp(sk_noise(n, 13), 180.0), 2200.0)
    out = []
    for i in range(n):
        t = i / SR
        wob = 0.78 + 0.22 * math.sin(2 * math.pi * 7.3 * t) * math.sin(2 * math.pi * 1.9 * t)
        out.append(base[i] * wob)
    for k in range(6):
        t0 = 0.31 + k * 0.62
        for w in range(4):
            i0 = int((t0 + w * 0.011) * SR)
            for j in range(int(SR * 0.006)):
                if i0 + j < n:
                    out[i0 + j] += rnd.uniform(-1, 1) * 0.9 * math.exp(-j / (SR * 0.0015))
    return out


def pop():
    """The tail smacking concrete: a broadband crack, high-passed so it cracks rather than booms, the deck ringing
    lower than a clapperboard (a 7-ply maple deck, not two slates), and a short thump under it."""
    n = int(SR * 0.22)
    rnd = random.Random(77)
    out, hp_in, hp_out = [], 0.0, 0.0
    for i in range(n):
        t = i / SR
        crack = rnd.uniform(-1, 1) * math.exp(-t / 0.009)
        y = 0.9 * (hp_out + crack - hp_in)
        hp_in, hp_out = crack, y
        wood = (math.sin(2 * math.pi * 430 * t) * 0.7 + math.sin(2 * math.pi * 1210 * t) * 0.35) * math.exp(-t / 0.045)
        thump = math.sin(2 * math.pi * 95 * t) * math.exp(-t / 0.03) * 0.8
        out.append(y * 0.8 + wood * 0.5 + thump)
    return out


def land():
    """Four wheels landing: the front truck, then the back 35 ms later, each a pitch-dropping thud with a short
    rattle -- and the wheels already rolling out under it."""
    n = int(SR * 0.42)
    out = [0.0] * n
    rnd = random.Random(5)
    for (t0, g) in ((0.0, 1.0), (0.035, 0.85)):
        i0 = int(t0 * SR)
        for j in range(n - i0):
            t = j / SR
            thud = math.sin(2 * math.pi * (130.0 - 60.0 * min(t / 0.05, 1.0)) * t) * math.exp(-t / 0.05)
            rattle = rnd.uniform(-1, 1) * math.exp(-t / 0.02) * 0.5
            out[i0 + j] += g * (thud + rattle)
    tail = sk_lp(sk_hp(sk_noise(n, 9), 200.0), 1800.0)
    for i in range(n):
        out[i] += tail[i] * 0.25 * min(1.0, i / (SR * 0.05))
    return out


def slam():
    """A body hitting concrete at speed: a heavy pitch-dropping thud with a slap of skin and cloth on it, a slide
    on the concrete dying over ~0.9 s, and the board getting away -- four wooden knocks as it flips tail over
    nose, each a little quieter."""
    n = int(SR * 1.6)
    out = [0.0] * n
    rnd = random.Random(902)
    for i in range(n):
        t = i / SR
        body = math.sin(2 * math.pi * (70.0 + 40.0 * math.exp(-t / 0.04)) * t) * math.exp(-t / 0.11)
        slap = rnd.uniform(-1, 1) * math.exp(-t / 0.012) * 0.7
        out[i] += body + slap
    sc = sk_lp(sk_hp(sk_noise(n, 3), 400.0), 3000.0)
    for i in range(n):
        t = i / SR
        env = 0.0 if t < 0.05 else 0.55 * math.exp(-(t - 0.05) / 0.35)
        out[i] += sc[i] * env
    for k, (t0, f) in enumerate(((0.12, 380.0), (0.31, 520.0), (0.47, 440.0), (0.66, 610.0))):
        i0 = int(t0 * SR)
        for j in range(int(SR * 0.12)):
            if i0 + j >= n:
                break
            t = j / SR
            out[i0 + j] += (math.sin(2 * math.pi * f * t) * 0.6 + rnd.uniform(-1, 1) * math.exp(-t / 0.004) * 0.5) \
                * math.exp(-t / 0.035) * (0.8 - 0.12 * k)
    return out

MAKERS = {"clap": clap, "update_chime": update_chime, "click": click,
          "crowd": crowd, "crowdpop": crowdpop,
          "roomtone": roomtone, "clock": clock, "sheets": sheets, "lighter": lighter,
          "drag": drag, "exhale": exhale, "steps": steps, "crickets": crickets,
          "roll": roll, "pop": pop, "land": land, "slam": slam}   # ep 13: the skate foley


def write(name, samples):
    peak = max(1e-6, max(abs(s) for s in samples))
    g = 0.89 / peak                                                       # -1 dBFS
    os.makedirs(SFX, exist_ok=True)
    p = os.path.join(SFX, name + ".wav")
    with wave.open(p, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * g)) * 32767)) for s in samples))
    print("wrote %s  %.2fs" % (p, len(samples) / SR))


if __name__ == "__main__":
    for nm in (sys.argv[1:] or list(MAKERS)):
        write(nm, MAKERS[nm]())
