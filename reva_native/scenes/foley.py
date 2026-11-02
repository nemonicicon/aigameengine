"""foley.py -- THE SOUND OF WHAT THE BODIES DO, from a take (10-23, the film pass, round 4).

    python foley.py <scene.txt> <take.log> [--child viktor,billy,freddy] [--head 1.0]

The film mix had voices, a place's room tone and music, and nothing for what the bodies DO: Viktor crossed his cabin
and sat on his bunk in silence. Where a body walks is the ENGINE's business (its walking grid routes round the
furniture, so a walk's length is not the marks' distance), so the timing comes from a TAKE: the engine logs every
walk's start and its arrival with the scene time (`[walk] t <s> slot <n> -> mark ...` / `... arrived on ...`). This
writes `sfx` lines into the scene script -- footsteps through every walk (a grown-up's 1.85 a second, a child's 2.2,
lighter and higher), and a SIT where a walk ends on a mark whose line sits the body down -- in one marked block,
replaced on every run. compile_scene.py mixes them (`sfx <t> <name> [gain]`, scene seconds less the mix's head).
⛔ The times are the take's: a script re-voiced (its lines moved) wants a new take and a new run of this.
The sounds are made here, by code, into scenes/sfx/ (step_a1..4, step_c1..4, sit), once: --force remakes them.
"""
import os, re, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SFX = os.path.join(HERE, "sfx")
SR = 48000
MARK = "# ---- FOLEY"
CADENCE = {"a": 1.0 / 1.85, "c": 1.0 / 2.2}       # seconds a step


def _write(name, x):
    import wave
    x = np.clip(x / (np.abs(x).max() + 1e-9) * 0.5, -1, 1)
    w = wave.open(os.path.join(SFX, name + ".wav"), "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((x * 32767).astype("<i2").tobytes()); w.close()


def _band(x, lo, hi):
    """a band of a signal, by FFT -- the steps are short, so this is exact and cheap"""
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1.0 / SR)
    X[(f < lo) | (f > hi)] = 0
    return np.fft.irfft(X, len(x))


def _step(rng, f0, scuff):
    """a soft sole on a ship's hard floor: the HEEL (a low thump and a short tick) and, 60-80 ms on, the TOE's scuff"""
    n = int(0.26 * SR); t = np.arange(n) / SR
    ph = 2 * np.pi * f0 * t * (1 - 0.25 * t / 0.26)
    # the thump and a body overtone at 2.1x (a fundamental near 100 Hz alone is gone on a laptop's speakers)
    heel = (np.sin(ph) + 0.45 * np.sin(2.1 * ph)) * np.exp(-t / 0.035) * np.minimum(1, t / 0.002)
    tick = _band(rng.standard_normal(n), 1500, 4200) * np.exp(-t / 0.012) * 0.50
    toe_at = int(rng.uniform(0.060, 0.080) * SR)
    toe = np.zeros(n)
    k = n - toe_at
    tt = np.arange(k) / SR
    toe[toe_at:] = _band(rng.standard_normal(k), 1800, 6500) * np.exp(-tt / 0.045) * np.minimum(1, tt / 0.006) * scuff
    return heel + tick + toe


def make_sounds(force=False):
    os.makedirs(SFX, exist_ok=True)
    want = ["step_a%d" % i for i in range(1, 5)] + ["step_c%d" % i for i in range(1, 5)] + ["sit"]
    if not force and all(os.path.exists(os.path.join(SFX, w + ".wav")) for w in want): return
    rng = np.random.default_rng(1023)
    for i in range(1, 5):
        _write("step_a%d" % i, _step(rng, 92.0 * rng.uniform(0.92, 1.08), 0.30 * rng.uniform(0.8, 1.2)))
        _write("step_c%d" % i, _step(rng, 128.0 * rng.uniform(0.92, 1.08), 0.22 * rng.uniform(0.8, 1.2)))
    # a SIT: the cloth (a slow band of noise) and the bunk taking the weight (a low thud, a short creak)
    n = int(0.70 * SR); t = np.arange(n) / SR
    cloth = _band(rng.standard_normal(n), 900, 5000) * np.minimum(1, t / 0.08) * np.exp(-t / 0.22) * 0.30
    thud = np.sin(2 * np.pi * 70 * t) * np.exp(-np.maximum(0, t - 0.12) / 0.06) * (t > 0.12) * 0.9
    cr = (t > 0.16) * np.sin(2 * np.pi * (420 + 60 * np.sin(2 * np.pi * 9 * t)) * t) * np.exp(-np.maximum(0, t - 0.16) / 0.09) * 0.12
    _write("sit", cloth + thud + cr)
    print("foley: made the steps and the sit in", SFX)


def main(a):
    script, log = a[0], a[1]
    child = set(a[a.index("--child") + 1].split(",")) if "--child" in a else set()
    head = float(a[a.index("--head") + 1]) if "--head" in a else 1.0
    make_sounds("--force" in a)
    lines = open(script, encoding="utf-8").read().split("\n")
    if MARK in "\n".join(lines):
        lines = lines[:next(i for i, l in enumerate(lines) if l.startswith(MARK))]
        while lines and not lines[-1].strip(): lines.pop()
    cast = {}
    for l in lines:
        if l.startswith("cast "):
            for b in l.split()[1:]:
                if "=" in b: s, who = b.split("=", 1); cast[int(s)] = who.lower()
    # which mark changes SIT the body down: the line that sends slot s to a mark, with a sit clip on it
    MK = {0: "hmark", 1: "pmark", 2: "p2mark", 3: "p3mark"}
    CL = {0: "hclip", 1: "pclip", 2: "p2clip", 3: "p3clip"}
    sits = set()
    for l in lines:
        if "|" not in l or l.startswith("#"): continue
        keys = l.split("|", 1)[0].split()[1:]
        for s in MK:
            m = next((k.split("=", 1)[1] for k in keys if k.startswith(MK[s] + "=")), None)
            c = next((k.split("=", 1)[1] for k in keys if k.startswith(CL[s] + "=")), "")
            if m and ("sit" in c): sits.add((s, m))
    walks, open_ = [], {}
    for l in open(log, encoding="utf-8", errors="replace"):
        m = re.search(r"\[walk\] t ([\d.]+) slot (\d) -> mark '([^']+)'", l)
        if m: open_[int(m.group(2))] = (float(m.group(1)), m.group(3)); continue
        m = re.search(r"\[walk\] t ([\d.]+) slot (\d) (arrived on '([^']+)'|STUCK)", l)
        if m and int(m.group(2)) in open_:
            s = int(m.group(2)); t0, mk = open_.pop(s)
            walks.append((s, t0, float(m.group(1)), mk, m.group(3).startswith("STUCK")))
    txt = open(log, encoding="utf-8", errors="replace").read()
    if not walks and re.search(r"\[walk\] slot \d+ -> mark", txt):     # walks, but none of them timed: an old engine
        raise SystemExit("foley: %s has walks with no times -- a take from an engine before 10-23's `[walk] t` lines" % log)
    # (no walks at all is a scene where nobody walks -- 2.2's drum is run on a roll, not between marks: nothing to add)
    out = ["", MARK + " -- written by foley.py from %s: footsteps where the engine walked, a sit where it sat"
           % os.path.basename(log)]
    rng = np.random.default_rng(7)
    for s, t0, t1, mk, stuck in walks:
        kind = "c" if cast.get(s, "") in child else "a"
        step, t, k = CADENCE[kind], t0 + 0.15, 0
        while t < t1 - 0.05:
            g = (0.12 if kind == "a" else 0.09) * (0.85 if k % 2 else 1.0) * rng.uniform(0.85, 1.15)
            out.append("sfx %.2f step_%s%d %.3f" % (t - head, kind, 1 + (k % 4), g))
            t += step * rng.uniform(0.94, 1.06); k += 1
        if (s, mk) in sits and not stuck:
            out.append("sfx %.2f sit 0.140" % (t1 + 0.05 - head))
        print("  slot %d %-12s %6.2f -> %6.2f s  %d steps%s%s" % (s, mk, t0, t1, k, " + a sit" if (s, mk) in sits else "",
                                                                   "  (STUCK: placed)" if stuck else ""))
    open(script, "w", encoding="utf-8", newline="\n").write("\n".join(lines + out) + "\n")
    print("foley: %d walks -> %s" % (len(walks), script))


if __name__ == "__main__":
    if len(sys.argv) < 3: print(__doc__); sys.exit(1)
    main(sys.argv[1:])
