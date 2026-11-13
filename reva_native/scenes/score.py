"""score.py -- THE BED: the sound of the place a line is played in, and a little music for the scene, made by code
(numpy: oscillators, filtered noise, FM bells, a synthetic hall -- no samples, nothing downloaded) at mixdown.SR stereo.

compile_scene.py's `mix film` lays it under the dialogue, ducked (design/scene_direction.md "THE FILM MIX"):

    ambience, music = bed(seconds, places=[(t0, t1, "cabin"), ...], mood="ship", seed=...)

PLACES are rooms of sound (the ship's hum and air, consoles chirping, a sick bay's monitor, the freezer's cold, the
drum's rumble, a star's roar, the silence of space); place_of(<set>) finds one by following a set's includes. MOODS
are chord progressions voiced as slow pads in a hall, with a sub drone and, for some, a few bell notes over them. A
scene is deterministic: the same seed is the same bed, take after take.

    python score.py --demo <dir>      every place and every mood, 40 s each, to listen to
    python score.py --selftest
"""
import math, os, re, sys, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mixdown as M                                                    # noqa: E402

SR = M.SR
SETS = os.path.join(HERE, "..", "sets")


# ---------------------------------------------------------------------------------------------- where a set is
PLACE_OF = [   # (regex on a set's name, place) -- first match wins; a set's includes are followed until one matches
    (r"^rm_cabin", "cabin"), (r"^rm_corridor", "corridor"),
    (r"^rm_refectory|^rm_rec", "refectory"), (r"^rm_bridge|^rm_ops", "bridge"), (r"^rm_freezer", "freezer"),
    (r"^rm_work", "workshop"), (r"^sickbay", "sickbay"), (r"^drum", "drum"),
    (r"^star_", "star"),
    (r"^space_", "space"),
]


def place_of(setname, depth=0):
    """the place a set sounds like: its own name, else what it includes, else `space` if it says so, else `ship`"""
    for rx, pl in PLACE_OF:
        if re.search(rx, setname): return pl
    path = os.path.join(SETS, setname + ".set")
    if depth < 4 and os.path.exists(path):
        inc, space = [], False
        for ln in open(path, encoding="utf-8", errors="replace"):
            b = ln.split()
            if b[:1] == ["include"] and len(b) > 1: inc.append(b[1])
            if b[:1] == ["space"]: space = True
        for s in inc:
            p = place_of(s, depth + 1)
            if p != "ship": return p
        if space: return "space"
    return "ship"


# ---------------------------------------------------------------------------------------------- building blocks
def _rng(seed, salt):
    """a generator for (seed, salt) that is the SAME in every process: hash() of a str is salted per run, and a bed
    that changed between two takes of one scene would be no bed at all"""
    return np.random.default_rng(zlib.crc32(repr((seed, salt)).encode("utf-8")))


def noise(n, lo, hi, tilt=0.0, seed=0, salt="n"):
    """stereo noise (decorrelated L/R) between lo and hi Hz with soft edges, `tilt` dB per octave, unit RMS"""
    rng = _rng(seed, salt)
    n0, n = n, 1 << int(math.ceil(math.log2(max(n, 2))))     # a power of two: an odd length's FFT is 50x slower
    f = np.fft.rfftfreq(n, 1.0 / SR)
    fs = np.maximum(f, 1.0)
    shape = (fs / 1000.0) ** (tilt / 6.0206)
    edge = lambda x, a, w: 0.5 + 0.5 * np.tanh((np.log2(np.maximum(x, 1e-3)) - math.log2(a)) / w)
    shape = shape * edge(fs, lo, 0.18) * (1.0 - edge(fs, hi, 0.18))
    out = np.empty((n, 2), np.float32)
    for c in range(2):
        X = np.fft.rfft(rng.standard_normal(n).astype(np.float32)) * shape
        y = np.fft.irfft(X, n)
        out[:, c] = y / (np.sqrt(np.mean(y ** 2)) + 1e-12)
    return out[:n0]


def lfo(n, rate, depth, seed, salt="l"):
    """a slow wandering 1 +/- depth: three incommensurate sines"""
    rng = _rng(seed, salt); t = np.arange(n) / SR
    v = np.zeros(n)
    for k, m in enumerate((1.0, 1.618, 2.718)):
        v += np.sin(2 * math.pi * rate * m * t + rng.random() * 6.283) / (k + 1)
    return (1.0 + depth * v / 1.83).astype(np.float32)


def hum(n, f0, seed, partials=(1.0, 0.45, 0.22, 0.12)):
    """an electrical hum: f0 and its harmonics, each a pair beating slowly, wandering in level"""
    t = np.arange(n) / SR; rng = _rng(seed, "hum")
    y = np.zeros((n, 2))
    for k, a in enumerate(partials):
        f = f0 * (k + 1)
        for c in range(2):
            d = 0.06 + 0.12 * rng.random()
            y[:, c] += a * 0.5 * (np.sin(2 * math.pi * f * t + rng.random() * 6.28) + np.sin(2 * math.pi * (f + d) * t + rng.random() * 6.28))
    y *= lfo(n, 0.05, 0.25, seed, "humlfo")[:, None]
    return (y / (np.sqrt(np.mean(y ** 2)) + 1e-12)).astype(np.float32)


def events(n, rate, maker, seed, salt, spread=0.8):
    """one-shots at Poisson times, each made by maker(rng) -> mono, panned at random"""
    rng = _rng(seed, salt); out = np.zeros((n, 2), np.float32)
    t = rng.exponential(1.0 / rate) if rate > 0 else n
    while rate > 0 and t * SR < n:
        x = maker(rng)
        out = M.place(out, M.pan(x, (rng.random() * 2 - 1) * spread), int(t * SR))
        t += rng.exponential(1.0 / rate)
    return out


def chirp(rng):
    """a console's two-tone blip"""
    notes = [1318.5, 1568.0, 1760.0, 2093.0, 2349.3, 2637.0]
    k = rng.integers(1, 4); out = []
    for _ in range(k):
        f = notes[rng.integers(len(notes))]; d = 0.045 + 0.06 * rng.random()
        t = np.arange(int(d * SR)) / SR
        e = np.minimum(1.0, t / 0.004) * np.exp(-t / (d * 0.45))
        out.append(np.sin(2 * math.pi * f * t) * e); out.append(np.zeros(int(0.025 * SR)))
    return (np.concatenate(out) * (0.5 + 0.5 * rng.random())).astype(np.float32)


def monitor(rng):
    t = np.arange(int(0.11 * SR)) / SR
    return (np.sin(2 * math.pi * 987.8 * t) * np.minimum(1, t / 0.005) * np.exp(-t / 0.05)).astype(np.float32)


def clank(rng):
    """a far mechanical knock: a short noise burst through a ringing low resonance"""
    d = 0.6; t = np.arange(int(d * SR)) / SR
    f = 90 + 140 * rng.random()
    x = rng.standard_normal(len(t)) * np.exp(-t / 0.012) * 0.6 + np.sin(2 * math.pi * f * t) * np.exp(-t / 0.18)
    return (x * 0.5).astype(np.float32)


def creak(rng):
    d = 1.2 + rng.random(); t = np.arange(int(d * SR)) / SR
    f = 60 + 40 * rng.random()
    ph = 2 * math.pi * np.cumsum(f * (1 + 0.15 * np.sin(2 * math.pi * 0.7 * t))) / SR
    x = np.sin(ph + 2.5 * np.sin(ph * 2.01)) * np.sin(math.pi * t / d) ** 2
    return (x * 0.4).astype(np.float32)


def crackle(rng):
    d = 0.02 + 0.05 * rng.random(); t = np.arange(int(d * SR)) / SR
    return (rng.standard_normal(len(t)) * np.exp(-t / (d * 0.3))).astype(np.float32)


# ---------------------------------------------------------------------------------------------- the places
# each: layers of (generator, level in dB) -- the levels are relative; the whole place is normalised to its LUFS
PLACES = {
    "ship":      dict(lufs=-44.0, hum=(55.0, -9), air=((70, 900, -4.0), 0), breath=-8, chirps=(0.05, -12)),
    "cabin":     dict(lufs=-46.0, hum=(55.0, -11), air=((70, 700, -4.5), 0), breath=-9, chirps=(0.02, -16)),
    "corridor":  dict(lufs=-43.0, hum=(55.0, -8), air=((60, 1200, -3.5), 0), breath=-6, chirps=(0.04, -12)),
    "refectory": dict(lufs=-43.0, hum=(55.0, -12), air=((80, 1600, -3.0), 0), breath=-5, chirps=(0.03, -14)),
    "bridge":    dict(lufs=-42.0, hum=(55.0, -9), air=((70, 1000, -4.0), 0), breath=-7, chirps=(0.22, -8)),
    "sickbay":   dict(lufs=-44.0, hum=(55.0, -10), air=((70, 900, -4.0), 0), breath=-8, chirps=(0.06, -12), monitor=(-14,)),
    "freezer":   dict(lufs=-42.0, hum=(73.4, -6), air=((1800, 9000, -1.5), -6), breath=-4, clanks=(0.03, -6)),
    "workshop":  dict(lufs=-43.0, hum=(55.0, -9), air=((70, 1100, -3.5), 0), breath=-6, clanks=(0.04, -8)),
    "drum":      dict(lufs=-38.0, hum=(73.4, -12), air=((25, 180, -6.0), 0), roll=-4, rumble=6.67, creaks=(0.05, -4)),
    "star":      dict(lufs=-36.0, air=((20, 320, -6.0), 0), rumble=11.0, crackles=(1.2, -12)),
    "space":     dict(lufs=-58.0, air=((25, 90, -6.0), 0)),
    "insert":    dict(lufs=-60.0, air=((25, 90, -6.0), 0)),
}
# the room's acoustic, for the voices played in it (compile_scene): rt60 s, box size m, wet dB under the dry
ROOMS = {"ship": (0.45, 5.0, -21), "cabin": (0.32, 3.0, -23), "corridor": (0.55, 2.5, -20), "refectory": (0.75, 11.0, -19),
         "bridge": (0.55, 8.0, -20), "sickbay": (0.50, 7.0, -21), "freezer": (1.10, 16.0, -17), "workshop": (0.60, 7.0, -20),
         "drum": (1.80, 45.0, -15), "star": None, "space": None, "insert": None}


def place_sound(name, n, seed):
    P = PLACES.get(name, PLACES["ship"])
    y = np.zeros((n, 2), np.float32)
    if "hum" in P:
        f0, lv = P["hum"]; y += hum(n, f0, seed) * M.undb(lv)
    if "air" in P:
        (lo, hi, tilt), lv = P["air"]
        a = noise(n, lo, hi, tilt, seed, "air" + name) * M.undb(lv)
        if "rumble" in P:   # the drum's turn / the star's convection: a slow swell in the low end
            per = P["rumble"]; t = np.arange(n) / SR
            a *= (0.75 + 0.25 * np.sin(2 * math.pi * t / per))[:, None] * lfo(n, 0.07, 0.3, seed, "rum")[:, None]
        y += a
    if "breath" in P:      # broadband air, falling 4.5 dB an octave: the room heard on a small speaker
        y += noise(n, 250, 7000, -4.5, seed, "breath" + name) * M.undb(P["breath"])
    if "roll" in P:        # the drum's turn where a speaker can play it: the rumble's overtones, swelling with the turn
        tt = np.arange(n) / SR
        y += noise(n, 120, 1400, -5.0, seed, "roll") * M.undb(P["roll"]) * (0.6 + 0.4 * np.sin(2 * math.pi * tt / P["rumble"] + 1.3))[:, None]
    if "chirps" in P:
        r, lv = P["chirps"]; y += events(n, r, chirp, seed, "chirp" + name) * M.undb(lv) * 4.0
    if "monitor" in P:
        lv, = P["monitor"]; t0 = 0.3
        while t0 * SR < n:
            y = M.place(y, M.pan(monitor(None), 0.35) * M.undb(lv) * 3.0, int(t0 * SR)); t0 += 1.18
    if "clanks" in P:
        r, lv = P["clanks"]; y += events(n, r, clank, seed, "clank" + name) * M.undb(lv) * 2.0
    if "creaks" in P:
        r, lv = P["creaks"]; y += events(n, r, creak, seed, "creak" + name) * M.undb(lv) * 2.0
    if "crackles" in P:
        r, lv = P["crackles"]
        hs = noise(n, 3000, 9000, -1.0, seed, "hiss") * 0.15
        y += (events(n, r, crackle, seed, "crk") * 1.5 + hs) * M.undb(lv)
    return M.to_lufs(y, P["lufs"], excerpt=30.0) if np.abs(y).max() > 0 else y


# ---------------------------------------------------------------------------------------------- the music
def midi(m): return 440.0 * 2.0 ** ((m - 69) / 12.0)


N = {n: i for i, n in enumerate(["C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"])}


def notes(spec):
    """'F3 A3 C4 E4' -> midi numbers"""
    out = []
    for tok in spec.split():
        m = re.match(r"([A-G]b?)(-?\d)", tok)
        out.append(N[m.group(1)] + 12 * (int(m.group(2)) + 1))
    return out


MOODS = {   # chords (each held `bar` s, crossfading), the drone's root, the bells' scale (or None), level (LUFS)
    "ship":   dict(bar=12.0, lufs=-28.0, root="F2", chords=["F3 C4 E4 A4 G4", "D3 A3 C4 F4 E4", "Bb2 F3 A3 D4 C4", "C3 G3 D4 E4 G4"],
                   bells=None, bright=0.35),
    "warm":   dict(bar=11.0, lufs=-27.0, root="C3", chords=["C3 G3 D4 E4", "A2 E3 G3 C4 B3", "F2 C3 A3 E4 G4", "G2 D3 B3 E4 D4"],
                   bells="C5 D5 E5 G5 A5", bright=0.45),
    "wonder": dict(bar=14.0, lufs=-25.0, root="D2", chords=["D3 A3 C4 E4 F4", "Bb2 F3 A3 D4 E4", "G2 D3 F3 A3 C4", "A2 E3 G3 C4 D4"],
                   bells="D5 E5 F5 A5 C6 D6", bright=0.4),
    "tense":  dict(bar=8.0, lufs=-27.0, root="C2", chords=["C3 G3 C4 Eb4 D4", "Ab2 Eb3 C4 G4", "F2 C3 Ab3 Eb4", "G2 D3 F3 B3 Ab3"],
                   bells=None, bright=0.25, pulse=0.75),
    "sad":    dict(bar=13.0, lufs=-27.0, root="A2", chords=["A2 E3 C4 G4", "F2 C3 A3 E4", "C3 G3 E4 B4", "E3 B3 D4 G4"],
                   bells="A4 C5 D5 E5 G5", bright=0.3),
}


def table(H, rolloff):
    """one cycle of a band-limited wave: H harmonics at 1/h^rolloff"""
    L = 4096; ph = np.arange(L) / L * 2 * math.pi
    y = np.zeros(L)
    for h in range(1, H + 1): y += np.sin(h * ph) / h ** rolloff
    return y / np.abs(y).max()


_TAB = {}


def osc(f, n, rolloff, seed, salt, vib=0.0018):
    """a wavetable oscillator at f (with a slow vibrato), band-limited below 7 kHz"""
    H = max(1, min(40, int(7000.0 / f)))
    key = (H, rolloff)
    if key not in _TAB: _TAB[key] = table(H, rolloff)
    T = _TAB[key]
    rng = _rng(seed, salt); t = np.arange(n) / SR
    fi = f * (1.0 + vib * np.sin(2 * math.pi * (0.13 + 0.1 * rng.random()) * t + rng.random() * 6.28))
    ph = (np.cumsum(fi) / SR + rng.random()) % 1.0
    T2 = np.append(T, T[0])
    x = ph * len(T); i = x.astype(np.int64); fr = x - i                   # read by index: interp searches every sample
    return (T2[i] + (T2[i + 1] - T2[i]) * fr).astype(np.float32)


def pad_note(m, n, seed, bright):
    """one sustained note: two detuned oscillators, dark and bright, the brightness wandering"""
    f = midi(m)
    a = osc(f * 2 ** (4 / 1200.0), n, 1.9, seed, ("a", m)) + osc(f * 2 ** (-4 / 1200.0), n, 1.9, seed, ("b", m))
    b = osc(f * 2 ** (7 / 1200.0), n, 1.25, seed, ("c", m))
    o = osc(f * 2.0 * 2 ** (-3 / 1200.0), n, 2.2, seed, ("o", m))          # the octave above: air, not weight
    w = np.clip(bright * lfo(n, 0.04, 0.6, seed, ("w", m)), 0.0, 1.0)
    return (a * 0.5 * (1.0 - w) + b * w * 0.7 + o * 0.22).astype(np.float32)


def bell(f, seed, d=5.0):
    """an FM bell: an inharmonic modulator whose index dies away faster than the tone"""
    n = int(d * SR); t = np.arange(n) / SR
    I = 2.6 * np.exp(-t / 0.55)
    y = np.sin(2 * math.pi * f * t + I * np.sin(2 * math.pi * f * 3.5 * t)) * np.minimum(1.0, t / 0.006) * np.exp(-t / 1.8)
    return y.astype(np.float32)


def music(name, n, seed):
    if name not in MOODS: return np.zeros((n, 2), np.float32)
    S = MOODS[name]; bar = S["bar"]; xf = min(4.0, bar * 0.4)
    out = np.zeros((n, 2), np.float32)
    rng = _rng(seed, "mus")
    k0 = int(rng.integers(len(S["chords"])))                         # each scene starts somewhere in the turn
    k, t0 = k0, 0.0
    while t0 * SR < n:
        ch = notes(S["chords"][k % len(S["chords"])])
        at, bt = max(0.0, t0 - xf), t0 + bar + xf                      # the chord's whole span, in seconds
        a, b = int(at * SR), min(n, int(bt * SR))
        L = b - a
        if L > 0:
            tt = np.arange(L) / SR
            # the scene's first chord is IN within a second (the head is where the music speaks); the envelope is
            # shaped over the chord's WHOLE span, so the scene ending inside it is a cut, never a fade to nothing
            # (the last chords fell silent when it was shaped over the truncated part -- the mix's own fade ends it)
            att = 1.2 if t0 == 0.0 else 2 * xf
            env = np.minimum(1.0, tt / att) * np.minimum(1.0, ((bt - at) - tt) / (2 * xf))
            env = (0.5 - 0.5 * np.cos(math.pi * np.clip(env, 0, 1))).astype(np.float32)
            for i, m in enumerate(ch):
                p = -0.55 + 1.1 * i / max(1, len(ch) - 1)
                v = pad_note(m, L, seed + k, S["bright"]) * env / len(ch)
                out[a:b] += M.pan(v, p)
        t0 += bar; k += 1
    # the drone under it
    t = np.arange(n) / SR; r = midi(notes(S["root"])[0])
    dr = (np.sin(2 * math.pi * r * t) * 0.6 + np.sin(2 * math.pi * r * 2 * t) * 0.25) * lfo(n, 0.03, 0.35, seed, "drone")
    out += M.pan(dr.astype(np.float32) * 0.07, 0.0)
    if S.get("pulse"):   # a low heartbeat
        p = S["pulse"]; t1 = 0.0
        th = np.arange(int(0.35 * SR)) / SR
        thump = (np.sin(2 * math.pi * (55 * np.exp(-th / 0.12)) * th) * np.exp(-th / 0.09)).astype(np.float32)
        while t1 * SR < n:
            out = M.place(out, M.pan(thump * 0.5, 0.0), int(t1 * SR)); t1 += p
    if S.get("bells"):
        sc = [midi(m) for m in notes(S["bells"])]; t1 = 3.0 + rng.random() * 4; i = int(rng.integers(len(sc)))
        while t1 * SR < n:
            i = int(np.clip(i + rng.integers(-2, 3), 0, len(sc) - 1))
            out = M.place(out, M.pan(bell(sc[i], seed) * 0.11, rng.random() * 1.2 - 0.6), int(t1 * SR))
            t1 += 2.5 + rng.exponential(3.5)
    # the hall
    ir = M.room_ir(SR, rt60=3.4, predelay=0.03, bright=0.45, early=0.2, size=30.0, seed=seed % 997 + 3, length=4.2)
    wet = M.convolve(out, ir)[:n]
    y = out * 0.62 + wet * 0.55
    return M.to_lufs(y, S["lufs"], excerpt=60.0)


# ---------------------------------------------------------------------------------------------- the bed
def bed(seconds, places, mood, seed=1):
    """(ambience, music), stereo float32, `seconds` long. places: [(t0, t1, place)], crossfaded 0.35 s at the joins."""
    n = int(seconds * SR) + 1
    amb = np.zeros((n, 2), np.float32)
    names = sorted(set(p for _, _, p in places))
    xf = int(0.35 * SR)
    for nm in names:
        w = np.zeros(n, np.float64)
        for t0, t1, p in places:
            a, b = max(0, int(t0 * SR)), min(n, int(t1 * SR))
            if p == nm and b > a: w[a:b] = 1.0
        # the joins: a box of xf run twice (a triangle, 2 xf wide) -- by running sums, not a 33k-tap convolution
        for _ in range(2):
            c = np.concatenate([np.zeros(xf + 1), np.cumsum(w), np.full(xf, w.sum())])
            w = (c[2 * xf + 1:] - c[:n]) / (2 * xf + 1)
        on = np.where(w > 1e-4)[0]
        if not len(on): continue
        a, b = on[0], on[-1] + 1                             # the place is made only where it is heard
        amb[a:b] += place_sound(nm, b - a, seed) * w[a:b, None].astype(np.float32)
    return amb, music(mood, n, seed)


# ---------------------------------------------------------------------------------------------- demo, self-test
def demo(d):
    os.makedirs(d, exist_ok=True)
    for nm in PLACES:
        M.write_wav(os.path.join(d, "place_%s.wav" % nm), M.to_lufs(place_sound(nm, 40 * SR, 7), -23.0))
    for nm in MOODS:
        M.write_wav(os.path.join(d, "mood_%s.wav" % nm), M.to_lufs(music(nm, 40 * SR, 7), -23.0))
    print("wrote %d places and %d moods to %s" % (len(PLACES), len(MOODS), d))


def selftest():
    bad = []
    def check(what, ok, got=""):
        print("  %-62s %s  %s" % (what, "ok  " if ok else "FAIL", got)); ok or bad.append(what)
    for s, want in (("rm_cabin", "cabin"), ("rm_freezer", "freezer"), ("space_x", "space"), ("star_x", "star"),
                    ("sickbay", "sickbay"), ("drum", "drum")):
        got = place_of(s); check("place_of(%s) is %s" % (s, want), got == want, got)
    a = place_sound("cabin", 10 * SR, 3); b = place_sound("cabin", 10 * SR, 3); c = place_sound("cabin", 10 * SR, 4)
    check("a place is deterministic (same seed, same samples)", np.array_equal(a, b))
    import subprocess
    code = "import sys; sys.path.insert(0, %r); import score, zlib; print(zlib.crc32(score.place_sound('cabin', 48000, 3).tobytes()))" % HERE
    runs = {subprocess.run([sys.executable, "-c", code], capture_output=True, text=True).stdout.strip() for _ in range(2)}
    check("...in another process too (no salted hash in the seed)", len(runs) == 1 and "" not in runs, " ".join(runs))
    check("...and another seed is another take", not np.array_equal(a, c))
    L = M.lufs(a); check("the cabin sits at its level (-46 LUFS)", abs(L + 46.0) < 0.5, "%.1f" % L)
    m = music("wonder", 30 * SR, 3)
    Lm = M.lufs(m); check("the wonder mood sits at its level (-25 LUFS)", abs(Lm + 25.0) < 0.7, "%.1f" % Lm)
    Fp = np.abs(np.fft.rfft(M.mono(m))) ** 2; fp = np.fft.rfftfreq(len(m), 1.0 / SR)
    sub_ = Fp[(fp >= 25) & (fp < 90)].sum(); mid = Fp[(fp >= 125) & (fp < 1000)].sum()
    check("the music's weight is the pads (125 Hz-1 kHz), not the sub", mid > 3.0 * sub_, "mid/sub %.1f" % (mid / max(sub_, 1e-20)))
    F = np.abs(np.fft.rfft(M.mono(m)[:SR * 16])); f = np.fft.rfftfreq(SR * 16, 1.0 / SR)
    hi = F[f > 12000].sum() / F.sum()
    check("the pads stay band-limited (little above 12 kHz)", hi < 0.01, "%.4f" % hi)
    mm = M.mono(music("ship", int(41.3 * SR), 9))
    lv = [M.lufs(mm[int(s * SR):int((s + 3) * SR)], gate=False) for s in (2.0, 18.0, 37.0)]
    check("the music holds its level to the scene's last second", max(lv) - min(lv) < 4.0, " / ".join("%.1f" % v for v in lv))
    amb, mus = bed(20.0, [(0, 8, "cabin"), (8, 20, "space")], "ship", seed=5)
    l1, l2 = M.lufs(amb[:7 * SR]), M.lufs(amb[9 * SR:19 * SR])
    check("a bed crossfades from the cabin to space (the room drops away)", l1 > l2 + 6, "%.1f -> %.1f LUFS" % (l1, l2))
    check("nothing clips", max(np.abs(amb).max(), np.abs(mus).max()) < 1.0)
    print("SELFTEST", "PASS" if not bad else "FAIL: " + "; ".join(bad))
    return not bad


if __name__ == "__main__":
    if "--selftest" in sys.argv: sys.exit(0 if selftest() else 1)
    if "--demo" in sys.argv: demo(sys.argv[sys.argv.index("--demo") + 1]); sys.exit(0)
    print(__doc__)
