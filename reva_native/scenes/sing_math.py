#!/usr/bin/env python3
"""sing_math.py -- REVA SINGS, AND NOBODY RECORDED HER: a singing voice made from arithmetic (design/reva_voice.md).

    python sing_math.py songs/reva_bug.sing OUT.wav [--voice reva] [--hall] [--band] [--png OUT.png]
    python sing_math.py --check songs/reva_bug.sing OUT_dry.wav     the wav's own pitch and vowels against the score
    python sing_math.py --selftest

jacob's line, 11-02: no recording of a voice at any step, including training; published phonetic facts are allowed.
This file reads no audio. Her sheet: "VOICE: SYNTHETIC SULTRY / SARCASTIC". jacob: "reva has a scottish accent".

HOW THE SOUND IS MADE. Every harmonic of her pitch is summed directly (no filter runs, so nothing aliases or rings):
  * THE SOURCE is the Liljencrants-Fant glottal pulse (Fant's Rd: 0.3 tense .. 2.7 breathy), taken apart into its Fourier
    series. Harmonic k's amplitude and phase come from that table at the frame's Rd.
  * THE TRACT is a chain of resonators (the five formants, a tube's fixed run of them above, a nasal pole and zero),
    evaluated at each harmonic's frequency, with the phase kept. ⛔ Cut that run short and the top dies: a pole below f
    takes 12 dB an octave off it, and only the poles above f give any back (three of them left 2-4 kHz some 5 dB down).
  * BREATH is noise through the same tract (an STFT, frame by frame). FRICATIVES and BURSTS are shaped noise (score.noise),
    set against the level of her own vowels.
⛔ NO LUNGS: nothing sounds between phrases, on purpose. The silence is digital black.
"""
import argparse, math, os, re, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mixdown as M                                                    # noqa: E402
import score as S                                                      # noqa: E402

SR = M.SR
CTRL = 500                                                             # control frames a second
FTOP = 12000.0                                                         # no harmonic above this is worth its time
TOPF = (6050.0, 7200.0, 8350.0, 9500.0, 10650.0, 11800.0, 12950.0)     # a tube's resonances go on above the five that move
TOPB = (500.0, 600.0, 700.0, 800.0, 900.0, 1000.0, 1100.0)

# ---------------------------------------------------------------------------------------------- her vowels, in Hz
# ⛔ SCOTTISH, and level 3 of the line: `PB` is Peterson & Barney 1952's women's average, typed from memory (FLEECE and
# DRESS checked against a copy of the table; not yet the original paper). No published table of Scottish women in Hz was
# found, so `placed` is an estimate between those numbers, by the vowel's published quality.
VOWELS = {
    "IY": (310, 2790, 3310),   # FLEECE [i]                PB
    "IH": (470, 1950, 2950),   # KIT, centred              placed: PB's 430 2480 3070 toward the middle; ⛔ nearer FACE, "slid" -> "laid"
    "EY": (430, 2500, 3050),   # FACE [e], ONE vowel       placed: between FLEECE and DRESS
    "EH": (680, 2150, 2950),   # DRESS [E], open           placed: PB's 610 2330 2990 opened; ⛔ at PB's, "tell" -> "tail", "well" -> "whale"
    "AE": (860, 1550, 2830),   # TRAP = PALM [a]           placed: midway PB's ae (860 2050) and ah (850 1220)
    "AA": (860, 1550, 2830),   #
    "AO": (590, 920, 2710),    # LOT = THOUGHT [O]         PB
    "OW": (470, 900, 2690),    # GOAT [o], ONE vowel       placed: between THOUGHT and PB's oo
    "UW": (370, 1750, 2500),   # GOOSE = FOOT, fronted     placed: PB's oo F1, F2 between its oo and ee
    "UH": (370, 1750, 2500),   #
    "AH": (760, 1400, 2780),   # STRUT                     PB
    "AX": (550, 1600, 2850),   # schwa                     placed: the neutral tube
    "AL": (520, 1150, 2750),   # schwa before a dark L     placed: already darkening ("-ble" held long was heard as "go")
}
GLIDES = {"AY": ("AE", "IH"), "AY0": ("AH", "IY"), "AW": ("AH", "UW"), "OY": ("AO", "EY")}     # AY0 = PRICE before a voiceless


def _w(f1, f2, f3): return (f1 * 1.15, f2 * 1.15, f3 * 1.15)           # Klatt-style targets are a man's


# dur = its main part (a stop's closure). av = voicing kept through it. place = where the mouth closes (locus()). fric = (lo, hi,
# tilt, dB against her vowels); burst = the same + seconds (lo None = compact, around the F2 of its locus); asp/aspdur = breath
# after a release; tail = frication after a release (CH, JH). L, R have a form by position (layout()).
# ⛔ singcheck 11-02: with American vowels she scored no better, so the words were failing on these numbers: K heard as T ("call"
# -> "tall"), G as D ("glue" -> "do"), R after a stop as L ("crash" -> "clash"), L as W ("lemons" -> "women's"), S lost ("slid" -> "laid").
CONS = {
    "M":  dict(F=_w(480, 1270, 2130), place="lab", dur=.075, av=.30, fnz=1000.0),
    "N":  dict(F=_w(480, 1340, 2470), place="alv", dur=.075, av=.30, fnz=1500.0),
    "NG": dict(F=_w(480, 2000, 2900), place="vel", dur=.085, av=.30, fnz=2300.0),
    "L":  dict(F=(380, 1100, 2900), dur=.070, av=.60),                 # before a vowel
    "LL": dict(F=(450, 950, 2700), dur=.090, av=.60),                  # after one: dark, as Scots keep it
    "R":  dict(F=(400, 1150, 1650), dur=.060, av=.65),                 # before a vowel: an approximant, F3 well down
    "RR": dict(F=(450, 1350, 1700), dur=.080, av=.70),                 # after one: SOUNDED
    "RT": dict(F=(400, 1500, 2300), dur=.030, av=.22),                 # between two vowels: the tap
    "W":  dict(F=_w(290, 610, 2150), dur=.065, av=.70),
    "Y":  dict(F=_w(260, 2070, 3020), dur=.060, av=.70),
    "HH": dict(F=None, dur=.070, av=0.0, asp=.30),
    "F":  dict(F=_w(340, 1100, 2080), place="lab", dur=.100, av=0.0, fric=(1500, 10000, 0.0, -22)),
    "V":  dict(F=_w(220, 1100, 2080), place="lab", dur=.070, av=.28, fric=(1500, 9000, 0.0, -28)),
    "TH": dict(F=_w(320, 1290, 2540), place="den", dur=.095, av=0.0, fric=(1500, 10000, 0.0, -25)),
    "DH": dict(F=_w(270, 1290, 2540), place="den", dur=.055, av=.30, fric=(1500, 9000, 0.0, -30)),
    "S":  dict(F=_w(320, 1390, 2530), place="alv", dur=.110, av=0.0, fric=(4500, 11500, 0.0, -8)),
    "Z":  dict(F=_w(240, 1390, 2530), place="alv", dur=.085, av=.25, fric=(4500, 11500, 0.0, -14)),
    "SH": dict(F=_w(300, 1840, 2750), place="pal", dur=.110, av=0.0, fric=(2400, 7500, -3.0, -7)),
    "ZH": dict(F=_w(300, 1840, 2750), place="pal", dur=.080, av=.25, fric=(2400, 7500, -3.0, -13)),
    "P":  dict(F=_w(400, 1100, 2150), place="lab", dur=.060, av=0.0, burst=(300, 2500, -6.0, -18, .006), asp=.16, aspdur=.045),
    "B":  dict(F=_w(200, 1100, 2150), place="lab", dur=.050, av=.09, burst=(300, 2500, -6.0, -20, .004)),
    "T":  dict(F=_w(400, 1600, 2600), place="alv", dur=.055, av=0.0, burst=(3500, 11000, 0.0, -12, .010), asp=.16, aspdur=.050),
    "D":  dict(F=_w(200, 1600, 2600), place="alv", dur=.045, av=.09, burst=(3000, 10000, 0.0, -16, .006)),
    "K":  dict(F=_w(300, 1990, 2850), place="vel", dur=.060, av=0.0, burst=(None, None, 0.0, -10, .020), asp=.16, aspdur=.060),
    "G":  dict(F=_w(250, 1990, 2850), place="vel", dur=.050, av=.09, burst=(None, None, 0.0, -16, .012)),
    "CH": dict(F=_w(350, 1800, 2820), place="pal", dur=.055, av=0.0, burst=(2500, 8000, -3.0, -12, .006), fric=(2400, 7500, -3.0, -9), tail=.075),
    "JH": dict(F=_w(260, 1800, 2820), place="pal", dur=.045, av=.09, burst=(2500, 8000, -3.0, -18, .005), fric=(2400, 7500, -3.0, -14), tail=.060),
}


def locus(place, V):
    """F2 and F3 where a consonant meets the vowel beside it: the locus equations (Sussman et al 1991, their form; constants
    typed from memory, scaled for a woman). The same K is further forward before "key" than before "call"; a fixed target has
    every back vowel swoop down from a front K, which reads as T."""
    f2, f3 = V[1], V[2]
    if place == "lab": return 0.75 * f2 + 200.0, f3 - 250.0            # always below the vowel: a labial is heard rising into it
    if place == "alv": return 0.40 * f2 + 1300.0, 2950.0
    if place == "den": return 0.60 * f2 + 900.0, f3 - 100.0
    if place == "pal": return 0.25 * f2 + 1700.0, 2900.0
    l = 0.55 * f2 + 1100.0 if f2 > 1800.0 else f2 + 350.0              # velar: F2 and F3 pinch together
    return l, l + 400.0


# THE ONE VOICE AND ITS DIALS. rd = the source (higher is softer and breathier), breath = noise against the vowel,
# vib = cents, vibhz, drift + flutter = cents of wander, tract = formants scaled (a longer throat is lower), lag = seconds
# behind the beat, presence = dB an octave above 1 kHz: a close microphone, locus = consonants take their place from the vowel
# beside them, air = dB of high noise on her voice, effort = higher notes are louder and brighter, fw = how far her formants wander.
VOICES = {
    "reva":      dict(rd=1.25, breath=.12, vib=12.0, vibhz=5.0, drift=2.0, flutter=0.8, tract=.98, lag=.020, presence=3.0, locus=1, air=-34.0, effort=1.0, fw=.005),  # still: she never falters
    "reva_warm": dict(rd=1.15, breath=.07, vib=30.0, vibhz=5.5, drift=6.0, flutter=2.0, tract=1.00, lag=.020, presence=3.0, locus=1, air=-36.0, effort=1.0, fw=.008), # the control: nearer a person
    "reva_flat": dict(rd=1.25, breath=.12, vib=0.0, vibhz=5.0, drift=0.0, flutter=0.0, tract=.98, lag=0.0, presence=3.0, locus=1, air=-34.0, effort=0.0, fw=0.0),     # the bench: nothing moves
}


def note_hz(name):
    m = re.fullmatch(r"([A-G])([#b]?)(-?\d)", name)
    if not m: raise ValueError("not a note: " + name)
    base = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1)]
    base += {"#": 1, "b": -1, "": 0}[m.group(2)]
    return 440.0 * 2.0 ** ((12 * (int(m.group(3)) + 1) + base - 69) / 12.0)


def _code(raw):
    """a line's tokens, without its comment. ⛔ A comment's # follows a space or starts the line: C#4 and F#m have sharps in them."""
    return re.sub(r"(^|\s)#.*", "", raw).split()


def parse(path):
    """a .sing file -> notes, each with its start and length in seconds, its pitch, and its syllable split at the vowel"""
    bpm, t, notes, say = 72.0, 0.0, [], ""
    for raw in open(path, encoding="utf-8"):
        b = _code(raw)
        if not b: continue
        if b[0] == "bpm": bpm = float(b[1]); continue
        if b[0] == "say": say = " ".join(b[1:]); continue              # the words of what follows (singcheck reads them)
        if b[0] in ("meter", "chordstart", "chords"): continue         # band()'s
        d = float(b[1]) * 60.0 / bpm
        if b[0] != "-":
            v = [i for i, p in enumerate(b[2:]) if p in VOWELS or p in GLIDES]
            if len(v) != 1: raise ValueError("a syllable has ONE vowel: " + raw.strip())
            ph = b[2:]; i = v[0]
            bad = [p for p in ph[:i] + ph[i + 1:] if p not in CONS]
            if bad: raise ValueError("unknown phoneme %s in: %s" % (bad, raw.strip()))
            notes.append(dict(t0=t, dur=d, hz=note_hz(b[0]), name=b[0], onset=ph[:i], vowel=ph[i], coda=ph[i + 1:], say=say))
        t += d
    return notes


# ---------------------------------------------------------------------------------------------- the source
def lf_cycle(rd, n=4096):
    """one period of the LF glottal flow derivative (T0 = 1, Ee = 1) from Fant's Rd; returns it and the instant of closure"""
    rd = min(2.7, max(0.3, rd))
    ra = (-1.0 + 4.8 * rd) / 100.0; rk = (22.4 + 11.8 * rd) / 100.0
    rg = rk / (4.0 * (0.11 * rd / (0.5 + 1.2 * rk) - ra))
    tp = 1.0 / (2.0 * rg); te = tp * (1.0 + rk); ta = ra; wg = math.pi / tp
    eps = 1.0 / ta
    for _ in range(60): eps = (1.0 - math.exp(-eps * (1.0 - te))) / ta
    t = (np.arange(n) + 0.5) / n
    ret = -(np.exp(-eps * (t - te)) - math.exp(-eps * (1.0 - te))) / (eps * ta)

    def cycle(a):
        e0 = -1.0 / (math.exp(a * te) * math.sin(wg * te))
        return np.where(t < te, e0 * np.exp(a * t) * np.sin(wg * t), ret)
    lo, hi = -40.0, 80.0                                               # alpha: no net flow over a period
    grid = np.linspace(lo, hi, 241); area = [cycle(a).sum() for a in grid]
    for i in range(240):
        if area[i] * area[i + 1] <= 0.0: lo, hi = grid[i], grid[i + 1]; break
    flo = cycle(lo).sum()
    for _ in range(50):
        mid = 0.5 * (lo + hi); fm = cycle(mid).sum()
        if flo * fm <= 0.0: hi = mid
        else: lo, flo = mid, fm
    return cycle(0.5 * (lo + hi)), te


RD_GRID = np.linspace(0.3, 2.7, 25)
KTAB = 400
_LF = None


def lf_table():
    """complex harmonic amplitudes by Rd (rows) and k (columns), every pulse aligned on its closure so Rd can interpolate"""
    global _LF
    if _LF is None:
        tab = np.zeros((len(RD_GRID), KTAB + 1), complex)
        for r, rd in enumerate(RD_GRID):
            c, te = lf_cycle(rd)
            X = np.fft.rfft(c)[:KTAB + 1] * 2.0 / len(c)
            tab[r] = X * np.exp(2j * math.pi * np.arange(KTAB + 1) * te)
        i1 = int(np.argmin(np.abs(RD_GRID - 1.0)))
        _LF = tab / abs(tab[i1, 1])
    return _LF


def source(k, rd):
    tab = lf_table()[:, min(k, KTAB)]
    return np.interp(rd, RD_GRID, tab.real) + 1j * np.interp(rd, RD_GRID, tab.imag)


# ---------------------------------------------------------------------------------------------- the tract
def _res(f, F, B):
    """an analog resonator, 1 at 0 Hz"""
    return F * F / (F * F - f * f + 1j * f * B)


def tract(f, F, B, fnp, fnz):
    H = np.ones(np.broadcast(f, F[0]).shape, complex)
    for i in range(len(F)): H = H * _res(f, F[i], B[i])
    return H * _res(f, fnp, 110.0) / _res(f, fnz, 130.0)


# ---------------------------------------------------------------------------------------------- score -> tracks
def _smooth(x, sigma_s):
    """a gaussian across frames, ends held"""
    s = max(1e-3, sigma_s * CTRL); r = int(4 * s) + 1
    g = np.exp(-0.5 * (np.arange(-r, r + 1) / s) ** 2); g /= g.sum()
    return np.convolve(np.pad(x, r, mode="edge"), g, mode="valid")


def _ffill(x):
    """a NaN takes the last value before it (and the first value, before any)"""
    ok = np.where(~np.isnan(x))[0]
    if not len(ok): return np.zeros_like(x)
    i = np.maximum.accumulate(np.where(~np.isnan(x), np.arange(len(x)), -1))
    y = x[np.where(i < 0, ok[0], i)]
    return y


def _wander(N, hz, rng):
    """unit-RMS noise slower than about hz"""
    y = _smooth(rng.standard_normal(N), 0.35 / hz)
    return y / (np.sqrt(np.mean(y ** 2)) + 1e-12)


def _cost(p): c = CONS[p]; return c["dur"] + c.get("aspdur", 0.0) + c.get("tail", 0.0)


def layout(notes, share=0.6):
    """when each sound happens. THE VOWEL IS ON THE NOTE: a syllable's opening consonants steal from the note before; its
    closing ones end the note. Consonants shrink so they never take more than `share` of a note."""
    for i, n in enumerate(notes):
        nx = notes[i + 1] if i + 1 < len(notes) else None
        pv = notes[i - 1] if i else None
        n["first"] = pv is None or pv["t0"] + pv["dur"] < n["t0"] - 1e-6
        n["last"] = nx is None or n["t0"] + n["dur"] < nx["t0"] - 1e-6
        between = pv is not None and not n["first"] and not pv["coda"] and len(n["onset"]) == 1
        n["onset"] = ["RT" if p == "R" and between else p for p in n["onset"]]
        n["coda"] = [{"R": "RR", "L": "LL"}.get(p, p) for p in n["coda"]]
        if n["vowel"] == "AX" and n["coda"][:1] == ["LL"]: n["vowel"] = "AL"
    for i, n in enumerate(notes):
        nx = notes[i + 1] if not n["last"] else None
        lead = sum(_cost(p) for p in nx["onset"]) if nx else 0.0
        code = sum(_cost(p) for p in n["coda"])
        n["sc"] = min(1.0, share * n["dur"] / max(1e-6, lead + code))
        n["end"] = n["t0"] + n["dur"] - lead * n["sc"]
        n["v1"] = n["end"] - code * n["sc"]
    for i, n in enumerate(notes):
        n["lead_sc"] = notes[i - 1]["sc"] if not n["first"] else 1.0
    return notes


def _vowel_ends(v):
    a, b = GLIDES[v] if v in GLIDES else (v, v)
    return np.array(VOWELS[a], float), np.array(VOWELS[b], float)


def tracks(notes, voice, seed=1):
    """every control track of the song, and its noise events"""
    pre = 0.35 + max([sum(_cost(p) for p in n["onset"]) for n in notes] + [0.0])
    end = max(n["t0"] + n["dur"] for n in notes) + pre + 0.5
    N = int(end * CTRL)
    ix = lambda t: min(N, max(0, int(round(t * CTRL))))
    F3 = np.full((3, N), np.nan); bx = np.ones(N); av = np.zeros(N); asp = np.zeros(N)
    rd = np.full(N, np.nan); fnp = np.full(N, 320.0); fnz = np.full(N, 320.0)
    ftg = np.full(N, np.nan); reset = np.zeros(N, bool); vibenv = np.zeros(N); events = []
    lag = voice["lag"]

    def seg(ph, t, sc, g, nextF):
        """one consonant from time t, beside the vowel nextF; returns its end, and its formants if the vowel should move out of them"""
        c = CONS[ph]; d = c["dur"] * sc; a, b = ix(t), ix(t + d)
        F = np.array(c["F"], float) if c["F"] is not None else nextF
        if c.get("place") and voice["locus"]:
            F = F.copy(); F[1], F[2] = locus(c["place"], nextF)
        F3[:, a:b] = F[:, None]; av[a:b] = c["av"] * g
        rd[a:b] = voice["rd"] + 0.25
        if "fnz" in c: fnp[a:b], fnz[a:b] = 300.0, c["fnz"]; bx[a:b] = 2.0
        if ph == "HH": asp[a:b] = c["asp"] * g; bx[a:b] = 2.5
        t += d; hold = F if c.get("place") and ("burst" in c or "fnz" in c) else None
        if "burst" in c:
            lo, hi, tilt, db, bd = c["burst"]
            if lo is None: lo, hi = 0.75 * F[1], 1.6 * F[1]
            events.append((t, t + bd, lo, hi, tilt, db + 20.0 * math.log10(max(g, 1e-3)), 0.001, 0.006))
        if "aspdur" in c:                                              # the mouth is already moving under the breath
            d2 = c["aspdur"] * sc; a2, b2 = ix(t), ix(t + d2)
            r = np.linspace(0.0, 1.0, max(1, b2 - a2))
            F3[:, a2:b2] = np.exp(np.log(F)[:, None] * (1 - r) + np.log(nextF)[:, None] * r)
            asp[a2:b2] = c["asp"] * g; bx[a2:b2] = 2.5
            t += d2; hold = None
        if "fric" in c:
            lo, hi, tilt, db = c["fric"]
            if "tail" in c:
                d3 = c["tail"] * sc; a3, b3 = ix(t), ix(t + d3)
                F3[:, a3:b3] = F[:, None]; av[a3:b3] = c["av"] * g
                events.append((t, t + d3, lo, hi, tilt, db + 20.0 * math.log10(max(g, 1e-3)), 0.008, 0.020))
                t += d3
            else:
                events.append((t - d, t, lo, hi, tilt, db + 20.0 * math.log10(max(g, 1e-3)), 0.022, 0.022))
        return t, hold

    for n in notes:
        t0 = n["t0"] + pre + lag; v1 = n["v1"] + pre + lag; e = n["end"] + pre + lag
        g = (0.82 + 0.18 * min(1.0, n["dur"] / 0.8)) * (0.80 if n["vowel"] in ("AX", "AL") else 1.0)
        e = voice["effort"] * max(-1.0, min(1.0, math.log2(n["hz"] / 220.0)))   # up the scale: louder and brighter
        g *= 1.0 + 0.18 * e; rd_v = voice["rd"] - 0.22 * e
        fa, fb = _vowel_ends(n["vowel"])
        lead = sum(_cost(p) for p in n["onset"]) * n["lead_sc"]
        # her pitch moves as the onset begins, and arrives on the vowel
        s = ix(t0 - lead - 0.01)
        if n["first"]: reset[s] = True
        ftg[s] = math.log2(n["hz"])
        t = t0 - lead
        hold = None
        for p in n["onset"]: t, hold = seg(p, t, n["lead_sc"], g, fa)
        # the vowel, and a glide held off until its last moments, as singers do
        a, b = ix(t0), ix(v1)
        if b > a:
            if n["vowel"] in GLIDES:
                # ⛔ at a phrase's end it finishes BEFORE the fade, and takes longer on a long note, or "now" is heard as "nah"
                fin = n["last"] and not n["coda"]
                dv = (b - a) / CTRL; gl = max(0.11, min(0.30, 0.25 * dv)) if fin else 0.11
                e2 = max(a + 1, b - ix(0.12)) if fin else b
                m = max(a, e2 - max(1, ix(gl)))
                F3[:, a:m] = fa[:, None]
                r = np.linspace(0.0, 1.0, e2 - m)
                F3[:, m:e2] = np.exp(np.log(fa)[:, None] * (1 - r) + np.log(fb)[:, None] * r)
                F3[:, e2:b] = fb[:, None]
            else:
                F3[:, a:b] = fa[:, None]
            if hold is not None:                                       # the move out of a stop or nasal happens where it can be HEARD
                h = min(b - a, max(1, ix(0.016)))
                r = np.linspace(0.0, 1.0, h)
                F3[:, a:a + h] = np.exp(np.log(hold)[:, None] * (1 - r) + np.log(F3[:, a:a + h]) * r)
            tt = (np.arange(a, b) - a) / CTRL; d = (b - a) / CTRL
            swell = 0.90 + 0.10 * np.sin(math.pi * np.minimum(1.0, tt / max(1e-3, d * 0.8))) if d > 0.8 else 1.0
            av[a:b] = g * swell
            rd[a:b] = rd_v
            if d > 0.55:                                               # vibrato only once a held note has settled
                vibenv[a:b] = np.clip((tt - 0.32) / 0.35, 0.0, 1.0)
            if n["last"] and not n["coda"]:                            # a phrase ends by opening up and going out, deadpan
                f = np.clip((tt - (d - 0.18)) / 0.18, 0.0, 1.0)
                av[a:b] *= (1.0 - f) ** 1.5
                rd[a:b] = rd_v + 0.7 * f
                ftg[max(a, b - ix(0.2))] = math.log2(n["hz"]) - 45.0 / 1200.0
        t = v1
        for p in n["coda"]: t, _ = seg(p, t, n["sc"], g, fb)
        if n["last"] and n["coda"]:
            a2 = ix(v1); b2 = ix(e)
            if b2 > a2: av[a2:b2] *= np.linspace(1.0, 0.75, b2 - a2)
            ftg[max(ix(t0), ix(v1) - ix(0.12))] = math.log2(n["hz"]) - 45.0 / 1200.0

    # the tracks settle: a mouth moves in tens of milliseconds, in ratios
    F = np.empty((5 + len(TOPF), N)); B = np.empty(F.shape)
    for i in range(3):
        F[i] = np.exp(_smooth(np.log(_ffill(F3[i])), 0.010)) * voice["tract"]
    F[3] = 4100.0 * voice["tract"]; F[4] = 4950.0 * voice["tract"]
    bx = _smooth(bx, 0.006)
    B[0] = 80.0 * bx; B[1] = 100.0 * np.minimum(bx, 1.6); B[2] = 140.0 * np.minimum(bx, 1.6)
    B[3] = 250.0; B[4] = 300.0
    for i, (ff, bb) in enumerate(zip(TOPF, TOPB)): F[5 + i] = ff; B[5 + i] = bb
    fnp = _smooth(fnp, 0.008); fnz = _smooth(fnz, 0.008)
    av = np.maximum(0.0, _smooth(av, 0.004)); asp = np.maximum(0.0, _smooth(asp, 0.004))
    rd = _smooth(_ffill(rd), 0.012)

    # pitch: a light second-order lag in octaves, with what little she wanders on top
    u = _ffill(ftg); y = u[0]; v = 0.0; w = 2.0 * math.pi * 6.0; z = 0.72; dt = 1.0 / CTRL; lf = np.empty(N)
    for i in range(N):
        if reset[i]: y = u[i] - 15.0 / 1200.0; v = 0.0
        v += (w * w * (u[i] - y) - 2.0 * z * w * v) * dt
        y += v * dt
        lf[i] = y
    rng = S._rng(seed, "wander")
    for i in range(3): F[i] *= 1.0 + voice["fw"] * _wander(N, 3.0, rng)
    lf += voice["drift"] / 1200.0 * _wander(N, 1.5, rng) + voice["flutter"] / 1200.0 * _wander(N, 25.0, rng)
    ph = 2.0 * math.pi * voice["vibhz"] * np.arange(N) / CTRL
    lf += voice["vib"] / 1200.0 * vibenv * np.sin(ph)
    av *= 1.0 + 0.05 * (voice["vib"] / 30.0) * vibenv * np.sin(ph)     # a vibrato is in the level too
    f0 = 2.0 ** lf
    F[0] = np.maximum(F[0], f0 * 1.04)                                 # F1 never drops below her pitch: singers tune it up
    asp = asp + voice["breath"] * av
    return dict(N=N, f0=f0, F=F, B=B, fnp=fnp, fnz=fnz, av=av, asp=asp, rd=rd, events=events, pre=pre + lag, presence=voice["presence"])


# ---------------------------------------------------------------------------------------------- tracks -> sound
def voiced(tr):
    """every harmonic, summed: amplitude and phase from source x tract at its own frequency, frame by frame"""
    N = tr["N"]; n = int(N * SR / CTRL); out = np.zeros(n)
    on = tr["av"] > 1e-4
    edges = np.flatnonzero(np.diff(np.concatenate([[0], on.astype(np.int8), [0]])))
    for a, b in zip(edges[0::2], edges[1::2]):
        a = max(0, a - 4); b = min(N, b + 4)
        i0, i1 = int(a * SR / CTRL), min(n, int((b - 1) * SR / CTRL))
        if i1 <= i0: continue
        frames = np.arange(a, b, dtype=float); tA = np.arange(i0, i1) * CTRL / SR
        f0 = tr["f0"][a:b]; phase = 2.0 * math.pi * np.cumsum(np.interp(tA, frames, f0)) / SR
        F, B, av, rd = tr["F"][:, a:b], tr["B"][:, a:b], tr["av"][a:b], tr["rd"][a:b]
        for k in range(1, int(FTOP / f0.min()) + 1):
            fk = k * f0
            C = source(k, rd) * tract(fk, F, B, tr["fnp"][a:b], tr["fnz"][a:b])
            A = np.abs(C) * av * np.clip((FTOP - fk) / 2000.0, 0.0, 1.0) * np.maximum(1.0, fk / 1000.0) ** (tr["presence"] / 6.0206)
            if A.max() < 1e-5: continue
            out[i0:i1] += np.interp(tA, frames, A) * np.cos(k * phase + np.interp(tA, frames, np.unwrap(np.angle(C))))
    return out


def breath(tr, ref, seed=1):
    """noise through the tract, in overlapping frames: unit gain is the level of her vowel"""
    N = tr["N"]; n = int(N * SR / CTRL); out = np.zeros(n + 1024)
    win, hop = 960, 240; w = np.hanning(win)
    f = np.fft.rfftfreq(win, 1.0 / SR); rng = S._rng(seed, "breath")
    x = rng.standard_normal(n + win)
    aF = np.array(list(VOWELS["AE"]) + [4100.0, 4950.0] + list(TOPF))[:, None]
    aB = np.array([80.0, 100.0, 140.0, 250.0, 300.0] + list(TOPB))[:, None]
    unit = math.sqrt(np.mean(np.abs(tract(f, aF, aB, 320.0, 320.0)) ** 2))
    close = np.maximum(1.0, f / 1000.0) ** (tr["presence"] / 6.0206)
    for s in range(0, n - win, hop):
        j = min(N - 1, int((s + win / 2) * CTRL / SR))
        g = tr["asp"][j]
        if g < 1e-4: continue
        H = np.abs(tract(f, tr["F"][:, j:j + 1], tr["B"][:, j:j + 1], tr["fnp"][j], tr["fnz"][j])) * close
        out[s:s + win] += np.fft.irfft(np.fft.rfft(x[s:s + win] * w) * H, win) * w * g
    return out[:n] / 1.5 / unit * ref


def noises(tr, n, ref, seed=1):
    out = np.zeros(n)
    for i, (t0, t1, lo, hi, tilt, db, rise, fall) in enumerate(tr["events"]):
        a, m = int(t0 * SR), max(8, int((t1 - t0) * SR))
        if a < 0 or a + m > n: continue
        y = S.noise(m, lo, hi, tilt, seed, "ev%d" % i)[:, 0]
        t = np.arange(m) / SR
        e = np.minimum(1.0, t / max(rise, 1e-4)) * np.minimum(1.0, (m / SR - t) / max(fall, 1e-4))
        out[a:a + m] += y * e * e * (3.0 - 2.0 * e) * M.undb(db) * ref
    return out


def render(notes, voice, seed=1, share=0.6):
    """dry, mono, not yet levelled; also the tracks"""
    notes = layout([dict(n) for n in notes], share)
    tr = tracks(notes, voice, seed)
    x = voiced(tr)
    hot = np.repeat(tr["av"] > 0.6, SR // CTRL)[:len(x)]
    ref = float(np.sqrt(np.mean(x[:len(hot)][hot] ** 2))) if hot.any() else 1.0
    env = np.interp(np.arange(len(x)) * CTRL / SR, np.arange(tr["N"]), tr["av"])
    top = S.noise(len(x), 5000.0, 12000.0, 0.0, seed, "air")[:, 0] * env * M.undb(voice["air"]) * ref
    x = x + breath(tr, ref, seed) + noises(tr, len(x), ref, seed) + top
    return x.astype(np.float32), tr, notes


def _chord(name):
    """'F#m' -> its root pitch class, and the intervals above it"""
    m = re.fullmatch(r"([A-G])([#b]?)(m7|m|7)?", name)
    if not m: raise ValueError("not a chord: " + name)
    root = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[m.group(1)] + {"#": 1, "b": -1, "": 0}[m.group(2)]
    q = m.group(3) or ""
    return root % 12, [0, 3 if q.startswith("m") else 4, 7] + ([10] if q.endswith("7") else [])


def _up_to(pc, lo):
    """the first pitch of class pc at or above midi lo"""
    return lo + (pc - lo) % 12


def band(path, lead, n, seed=1):
    """a small backing from a song's `meter 3`, `chordstart <beats>`, `chords A:12 E7:6 ...` (chord:beats): the bass on each bar's
    first beat, the chord on its other beats, a quiet pad under them. Made as score.py makes its music. None if the song has no chords."""
    bpm, meter, start, seq = 72.0, 4, 0.0, []
    for raw in open(path, encoding="utf-8"):
        b = _code(raw)
        if b[:1] == ["bpm"]: bpm = float(b[1])
        elif b[:1] == ["meter"]: meter = int(b[1])
        elif b[:1] == ["chordstart"]: start = float(b[1])
        elif b[:1] == ["chords"]: seq += [(c.split(":")[0], float(c.split(":")[1])) for c in b[1:]]
    if not seq: return None
    spb = 60.0 / bpm; out = np.zeros(n); beat = 0.0

    def put(y, t):
        a = int((lead + (start + t) * spb) * SR); m = min(len(y), n - a)
        if a >= 0 and m > 0: out[a:a + m] += y[:m]
    for name, beats in seq:
        root, ivs = _chord(name)
        keys = [_up_to((root + iv) % 12, 52) for iv in ivs]
        L = int(beats * spb * SR); t = np.arange(L) / SR
        fade = np.minimum(1.0, np.minimum(t / 0.04, (L / SR - t) / 0.04))
        for k in keys: put(S.osc(S.midi(k), L, 1.9, seed, ("pad", k)) * fade * 0.14, beat)
        for i in range(int(round(beats))):
            at = beat + i
            if abs(at % meter) < 1e-6:
                f = S.midi(_up_to(root, 36)); m = int(spb * meter * SR); u = np.arange(m) / SR
                put((np.sin(2 * math.pi * f * u) + 0.3 * np.sin(4 * math.pi * f * u)) * np.minimum(1.0, u / 0.005) * np.exp(-u / 0.45) * 0.9, at)
            else:
                m = int(spb * SR); u = np.arange(m) / SR
                e = np.minimum(1.0, u / 0.004) * np.exp(-u / 0.14)
                for k in keys: put(S.osc(S.midi(k), m, 1.6, seed, ("st", k)) * e * 0.20, at)
        beat += beats
    return out.astype(np.float32)


def finish(x, hall=False, bed=None):
    y = M.to_lufs(x, -18.0)
    if bed is not None: y = y + M.to_lufs(bed, -27.0)
    if hall:
        ir = M.room_ir(rt60=1.15, predelay=0.014, bright=0.45, early=0.35, size=9.0, seed=9)
        wet = M.convolve(y, ir)
        d = np.zeros((len(wet), 2), np.float32); d[:len(y)] = M.stereo(y)
        y = d + wet * 0.30
    return M.limiter(M.to_lufs(y, -18.0), ceiling_db=-1.0)


def sheet(x, path, title):
    """a spectrogram, to look at: only pixels, not a verdict"""
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    win, hop = 1536, 240; w = np.hanning(win)
    fr = [np.abs(np.fft.rfft(x[s:s + win] * w)) for s in range(0, len(x) - win, hop)]
    D = 20.0 * np.log10(np.array(fr).T + 1e-5); top = D.max()
    fig, ax = plt.subplots(2, 1, figsize=(18, 9), height_ratios=[1, 4], sharex=True)
    ax[0].plot(np.arange(len(x)) / SR, x, lw=0.2, color="#7a2fd0"); ax[0].set_ylim(-1, 1); ax[0].set_title(title)
    ax[1].imshow(D, origin="lower", aspect="auto", cmap="magma", vmin=top - 85, vmax=top, extent=[0, len(x) / SR, 0, SR / 2000.0])
    ax[1].set_ylim(0, 12); ax[1].set_ylabel("kHz"); ax[1].set_xlabel("s")
    fig.tight_layout(); fig.savefig(path, dpi=80); plt.close(fig)


# ---------------------------------------------------------------------------------------------- measuring the wav itself
def pitch_of(x, lo=100.0, hi=900.0):
    """median pitch of a slice by YIN: from the samples, knowing nothing about the score"""
    W = int(SR * 0.03); t0, t1 = int(SR / hi), int(SR / lo); out = []
    for s in range(0, max(1, len(x) - W - t1), int(SR * 0.01)):
        a = x[s:s + W]; d = np.array([np.sum((a - x[s + t:s + t + W]) ** 2) for t in range(t0, t1)])
        c = d * np.arange(1, len(d) + 1) / (np.cumsum(d) + 1e-12)
        under = np.flatnonzero(c < 0.15)
        if not len(under): continue
        i = under[0]
        while i + 1 < len(c) and c[i + 1] < c[i]: i += 1
        if 0 < i < len(c) - 1:
            den = c[i - 1] - 2 * c[i] + c[i + 1]
            out.append(SR / (t0 + i + (0.5 * (c[i - 1] - c[i + 1]) / den if den > 0 else 0.0)))
    return float(np.median(out)) if out else 0.0


def formants_of(x):
    """F1, F2 by LPC on the samples"""
    y = np.asarray(M.resample(x, SR, 11025), np.float64)
    y = np.append(y[0], y[1:] - 0.97 * y[:-1]) * np.hanning(len(y))
    p = 12; r = np.array([np.dot(y[:len(y) - i], y[i:]) for i in range(p + 1)])
    a = np.array([1.0]); e = r[0]
    for i in range(1, p + 1):
        k = -np.dot(a, r[i:0:-1][:len(a)]) / e
        a = np.append(a, 0.0) + k * np.append(a, 0.0)[::-1]
        e *= 1.0 - k * k
    z = [q for q in np.roots(a) if q.imag > 0]
    fs = sorted((np.angle(q) * 11025 / (2 * math.pi), -math.log(abs(q)) * 11025 / math.pi) for q in z)
    return [f for f, b in fs if 180.0 < f < 5000.0 and b < 700.0]


def check(notes, x, off_cents=0.0, table=None, quiet=False):
    """the wav's pitch against the score, and held vowels' formants against the table: returns the failures"""
    notes = layout([dict(n) for n in notes]); pre = 0.35 + max([sum(_cost(p) for p in n["onset"]) for n in notes] + [0.0])
    bad = 0; table = table or VOWELS
    for n in notes:
        a, b = n["t0"] + pre, n["v1"] + pre
        if b - a < 0.18: continue
        m = x[int((a + 0.3 * (b - a)) * SR):int((a + 0.8 * (b - a)) * SR)]
        hz = pitch_of(m); c = 1200.0 * math.log2(max(hz, 1.0) / (n["hz"] * 2.0 ** (off_cents / 1200.0)))
        ok = abs(c) < 25.0; line = "%-4s %-4s %6.1f Hz  %+6.1f cents" % (n["name"], n["vowel"], hz, c)
        if n["vowel"] in VOWELS and b - a > 0.8 and n["hz"] < 240.0:   # ⛔ higher, LPC locks onto the harmonics, not the formants
            fm = formants_of(m); t = table[n["vowel"]]
            if len(fm) >= 2:
                e1, e2 = fm[0] / t[0] - 1.0, fm[1] / t[1] - 1.0
                line += "   F1 %4.0f (%4d %+4.0f%%)  F2 %4.0f (%4d %+4.0f%%)" % (fm[0], t[0], e1 * 100, fm[1], t[1], e2 * 100)
                ok = ok and abs(e1) < 0.2 and abs(e2) < 0.2
            else: ok = False; line += "   no formants found"
        bad += 0 if ok else 1
        if not quiet: print(("  ok  " if ok else "  BAD ") + line)
    return bad


def selftest():
    c, te = lf_cycle(1.2)
    assert abs(c.mean()) < 1e-3 and abs(c.min() + 1.0) < 0.05, "the LF pulse: no net flow, closure at -1"
    tab = lf_table(); r = int(np.argmin(np.abs(RD_GRID - 1.5)))
    assert abs(tab[r, 80]) < abs(tab[r, 10]) < abs(tab[r, 2]), "the source falls with frequency"
    assert abs(tab[-1, 40]) < abs(tab[6, 40]), "a breathier source has weaker highs"
    Ft = np.array([800.0, 1200.0])[:, None]; Bt = np.array([80.0, 100.0])[:, None]
    f = np.array([0.0, 800.0, 3000.0]); H = np.abs(tract(f, Ft, Bt, 320.0, 320.0))
    assert abs(H[0] - 1.0) < 1e-6 and H[1] > 5.0 > H[2], "the tract: unity at DC, up at a formant"
    assert _code("C#4 1 Z IY  # a comment") == ["C#4", "1", "Z", "IY"] and _code("# all comment") == [], "a sharp is not a comment"
    assert _chord("F#m") == (6, [0, 3, 7]) and _chord("E7") == (4, [0, 4, 7, 10]) and _up_to(9, 36) == 45, "chords"
    tune = [dict(t0=i * 1.3, dur=1.0, hz=note_hz(nm), name=nm, onset=[], vowel=v, coda=[])
            for i, (nm, v) in enumerate([("G3", "IY"), ("G3", "AO"), ("G3", "AE"), ("A3", "AH"), ("E4", "AH")])]
    x, _, _ = render(tune, VOICES["reva_flat"])
    y, _, _ = render(tune, VOICES["reva_flat"])
    assert np.array_equal(x, y), "the same score and seed is the same take"
    assert check(tune, x, quiet=True) == 0, "pitch and vowels match"
    # ⛔ a test that could not fail is not one: each control below has to fail
    assert check(tune, x, off_cents=100.0, quiet=True) >= 5, "the pitch check must catch a semitone"
    swapped = dict(VOWELS); swapped["IY"], swapped["AO"] = VOWELS["AO"], VOWELS["IY"]
    assert check(tune, x, table=swapped, quiet=True) >= 2, "the vowel check must catch a swapped vowel"
    gap = x[int((0.35 + 1.1) * SR):int((0.35 + 1.25) * SR)]
    assert np.abs(gap).max() < 1e-3 * np.abs(x).max(), "no lungs: black between phrases"
    print("sing_math selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song", nargs="?"); ap.add_argument("out", nargs="?")
    ap.add_argument("--voice", default="reva", choices=sorted(VOICES)); ap.add_argument("--hall", action="store_true")
    ap.add_argument("--band", action="store_true")
    ap.add_argument("--png"); ap.add_argument("--check", action="store_true"); ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest: return selftest()
    notes = parse(a.song)
    if a.check:
        x, sr = M.read_wav(a.out)
        bad = check(notes, M.mono(x))
        print("%d note(s) out" % bad)
        return 1 if bad else 0
    x, tr, _ = render(notes, VOICES[a.voice])
    print("%s: %.1f s, %d notes, voice %s, peak %.2f before levelling" % (a.song, len(x) / SR, len(notes), a.voice, np.abs(x).max()))
    y = finish(x, a.hall, band(a.song, tr["pre"] - VOICES[a.voice]["lag"], len(x)) if a.band else None)
    M.write_wav(a.out, y)
    if a.png: sheet(M.mono(y), a.png, os.path.basename(a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
