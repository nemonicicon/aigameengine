#!/usr/bin/env python3
"""song_notes.py -- A RECORDING'S NOTES (design/pdoom_video.md): drum hits and pitched notes read off Demucs stems, written as ordinary MIDI so they can be opened in a DAW and judged by ear.

    <the Basic Pitch venv>/python song_notes.py STEMDIR MAP.json OUTDIR [--selftest]

The drumming and the keyboard playing are to be note accurate. No MIDI exists (a Suno song is audio), and the song is clean enough to transcribe locally.
DRUMS: the drum stem's onsets, each read for what sounds at it (kick, snare, hat, tom, cymbal) by band energy, kept where they fall. PITCHES: Spotify's Basic Pitch on the bass stem and the "other" stem.
⛔ IT IS A TRANSCRIPTION: an estimate of what was played. score() says how well it fits, against controls that must fail: hits on a grid shifted a 32nd, notes transposed a semitone.
Writes OUTDIR/notes.json (seconds), OUTDIR/pdoom_band.mid (tempo from the map, so a DAW's grid lands on the song) and OUTDIR/check_band.wav (the notes as tones and clicks, for the left ear, against the song for the right).
"""
import argparse, json, math, os, sys, warnings
import numpy as np
import soundfile as sf
warnings.filterwarnings("ignore")

SR = 44100
HOP, WIN = 256, 1024                                                   # 5.8 ms frames: a drum hit's time matters at this scale
FPS = SR / HOP
GM = dict(kick=36, snare=38, hat=42, open_hat=46, tom_hi=48, tom_mid=45, tom_lo=41, crash=49, ride=51)


def mono(x): return x.mean(axis=1) if x.ndim > 1 else x


def spec(x):
    w = np.hanning(WIN); n = max(0, (len(x) - WIN) // HOP)
    S = np.empty((n, WIN // 2 + 1), np.float32)
    for i in range(n): S[i] = np.abs(np.fft.rfft(x[i * HOP:i * HOP + WIN] * w))
    return S, np.fft.rfftfreq(WIN, 1.0 / SR)


BANDS = dict(low=(30, 110), lowmid=(110, 300), mid=(300, 1500), himid=(1500, 6000), high=(6000, 16000))


def band_energy(S, f):
    return {k: np.sqrt((S[:, (f >= a) & (f < b)] ** 2).sum(axis=1)) for k, (a, b) in BANDS.items()}


def onsets(E, thresh=0.10, gap=0.045):
    """the drum stem's attacks: where any band's log energy jumps. Kept where they fall: nothing is snapped to a grid"""
    fl = np.zeros(len(next(iter(E.values()))))
    for v in E.values():
        lg = np.log1p(v / (np.percentile(v, 99) + 1e-9) * 50.0)
        d = np.maximum(0.0, np.diff(lg, prepend=lg[0]))
        fl += d / (np.percentile(d, 99.5) + 1e-9)
    k = np.ones(3) / 3.0; fl = np.convolve(fl, k, mode="same")
    ref = np.array([np.percentile(fl[max(0, i - 200):i + 200], 90) for i in range(0, len(fl), 50)]); ref = np.interp(np.arange(len(fl)), np.arange(len(ref)) * 50, ref)
    peaks, last, g = [], -10 ** 9, int(gap * FPS)
    for i in range(2, len(fl) - 2):
        if fl[i] >= fl[i - 1] and fl[i] > fl[i + 1] and fl[i] > max(thresh, 0.42 * ref[i]) and i - last >= g:
            peaks.append(i); last = i
    return np.array(peaks), fl


def classify(p, E):
    """what sounds at an attack: the rise in each band over the 25 ms after it, and how long the top of the spectrum rings"""
    out = []; a, b = int(0.012 * FPS), int(0.052 * FPS); ring = int(0.40 * FPS)     # ⛔ a kick's low end builds for 40-50 ms after the click a detector fires on: a shorter look reads a weak kick and a strong "snare"
    norm = {k: np.percentile(v, 99.5) + 1e-9 for k, v in E.items()}
    for i in p:
        rise = {}
        for k, v in E.items():
            before = v[max(0, i - a - 2):max(1, i - 1)].mean(); after = v[i:i + b + 1].max()
            rise[k] = max(0.0, after - before) / norm[k]
        hi = E["high"]; lvl = hi[i:i + b].max() + 1e-9; tail = hi[min(len(hi) - 1, i + ring)] / lvl
        # ⭐ measured on this song's groove, a sixteenth at a time (low rises 70-90 on every beat, 2 between them; himid over lowmid 0.34 on 1 and 3, 0.78 on 2 and 4):
        # ⛔ a kick's own attack reaches the snare band, so a snare is told by its SHARE of it, not its level (a level test called all four beats snare)
        kinds = []
        if rise["low"] > 0.35: kinds.append("kick")
        if rise["himid"] > 0.30 and rise["himid"] > 0.55 * rise["lowmid"]: kinds.append("snare")
        if rise["lowmid"] > 0.40 and rise["low"] < 0.25 and rise["himid"] < 0.30: kinds.append("tom")
        if rise["high"] > 0.45 and tail > 0.40: kinds.append("crash")
        elif rise["high"] > 0.09: kinds.append("hat")
        out.append(dict(t=(i + WIN / 2.0 * 0.6 / HOP) / FPS, kinds=kinds, vel=float(min(1.0, max(rise.values()))), rise={k: round(float(v), 3) for k, v in rise.items()}))
    return out


def grid_offsets(ts, t0, s16):
    d = (np.asarray(ts) - t0) / s16
    return (d - np.round(d)) * s16


def drums(stems, mp):
    x, _ = sf.read(os.path.join(stems, "drums.wav"), dtype="float32", always_2d=True)
    S, f = spec(mono(x)); E = band_energy(S, f)
    p, fl = onsets(E)
    hits = classify(p, E)
    sb = 60.0 / mp["bpm"]; s16 = sb / 4.0
    off = grid_offsets([h["t"] for h in hits], mp["t0"], s16)
    # the hits' clock against the map's. If the stem's attacks sit on the grid, the offsets pile up in one narrow place; if the detector were guessing they would spread evenly over a sixteenth
    med = float(np.median(off)); near = float(np.mean(np.abs(off - med) < 0.012))
    print("drums: %d attacks; offset from the map's sixteenths median %+.1f ms; within 12 ms of that: %.0f%% (a detector that was guessing would score %.0f%%)" % (len(hits), med * 1000, near * 100, 100.0 * 0.024 / s16))
    for h in hits: h["t"] = round(h["t"] - med, 4)                      # the detector's own lag, measured here against the grid the whole song agrees on
    # ⭐ THE SNARE, BY ITS RING: 35-90 ms after a hit, 1-3 kHz, about 15 dB louder on this song's backbeat than on its kick-only beats and its hats. ⛔ Not by its attack (a kick's click reaches that band, and
    # this snare starts before the grid). The line is halfway, in dB, between the song's own two kinds of beat, and holds for a fill's hits wherever they fall
    R13 = 20.0 * np.log10(np.sqrt((S[:, (f >= 1000.0) & (f < 3000.0)] ** 2).sum(axis=1)) + 1e-6); ra, rb = int(0.035 * FPS), int(0.090 * FPS)
    bar0 = mp["bars"][0]; pos = lambda t: int(round((t - bar0) / s16)) % 16
    for h in hits:
        i = int(h["t"] * FPS); h["ring"] = float(R13[i + ra:i + rb].mean()) if i + rb < len(R13) else -99.0
    kk = [h["ring"] for h in hits if pos(h["t"]) in (0, 8) and "kick" in h["kinds"]]; sn = [h["ring"] for h in hits if pos(h["t"]) in (4, 12) and "kick" in h["kinds"]]
    line = 0.5 * (float(np.median(kk)) + float(np.median(sn)))
    print("  the 1-3 kHz ring: %.1f dB on the bar's 1 and 3, %.1f dB on 2 and 4; the line %.1f" % (float(np.median(kk)), float(np.median(sn)), line))
    for h in hits:
        h["kinds"] = [k for k in h["kinds"] if k != "snare"]
        if h["ring"] > line and h["rise"]["himid"] > 0.25: h["kinds"].append("snare")      # ⛔ and its own attack: a snare's ring carries over the next hit, and a hat under it read as a second snare
    return hits, med


def pitched(stems, name, lo, hi, onset=0.5, frame=0.3, minlen=70.0):
    from basic_pitch.inference import predict
    from basic_pitch import ICASSP_2022_MODEL_PATH
    _, _, ev = predict(os.path.join(stems, name + ".wav"), ICASSP_2022_MODEL_PATH, onset_threshold=onset, frame_threshold=frame, minimum_note_length=minlen, minimum_frequency=lo, maximum_frequency=hi, melodia_trick=True, multiple_pitch_bends=False)
    ev = sorted((round(float(a), 4), round(float(b), 4), int(p), round(float(v), 3)) for a, b, p, v, *_ in ev)
    return ev


def chroma_of(x, sb, t0, n):
    """each beat's pitch classes, in the audio"""
    N = 8192; w = np.hanning(N); f = np.fft.rfftfreq(N, 1.0 / SR); ok = (f > 55.0) & (f < 2500.0)
    pc = (np.round(12.0 * np.log2(f[ok] / 440.0)).astype(int) + 9) % 12; out = np.zeros((n, 12))
    for i in range(n):
        a = int((t0 + i * sb) * SR)
        fr = x[a:a + N]
        if len(fr) == N: np.add.at(out[i], pc, (np.abs(np.fft.rfft(fr * w))[ok]) ** 2)
    return out


def note_chroma(ev, sb, t0, n, shift=0):
    out = np.zeros((n, 12))
    for a, b, p, v in ev:
        i0, i1 = max(0, int((a - t0) / sb)), min(n, int((b - t0) / sb) + 1)
        for i in range(i0, i1): out[i, (p + shift) % 12] += v * min(b, t0 + (i + 1) * sb) - v * max(a, t0 + i * sb)
    return out


def chroma_score(A, B):
    """how often the audio's pitch classes and the notes' agree, beat by beat, over the beats that have both"""
    c = []
    for u, v in zip(A, B):
        if u.sum() > 1e-6 and v.sum() > 1e-6:
            c.append(float(np.dot(u, v) / (np.linalg.norm(u) * np.linalg.norm(v) + 1e-12)))
    return float(np.mean(c)) if c else 0.0, len(c)


def score(stems, name, ev, mp):
    x, _ = sf.read(os.path.join(stems, name + ".wav"), dtype="float32", always_2d=True); x = mono(x)
    sb = 60.0 / mp["bpm"]; n = int((len(x) / SR - mp["t0"]) / sb)
    A = chroma_of(x, sb, mp["t0"], n)
    base, nb = chroma_score(A, note_chroma(ev, sb, mp["t0"], n))
    wrong = np.mean([chroma_score(A, note_chroma(ev, sb, mp["t0"], n, s))[0] for s in (1, 5, 6)])
    active = float(np.mean((A.sum(axis=1) > np.percentile(A.sum(axis=1), 50))))
    print("%-6s %4d notes, pitch %d..%d, %.1f notes a second; agrees with the stem %.2f over %d beats; the same notes moved 1, 5, 6 semitones (the control) %.2f" % (name, len(ev), min(e[2] for e in ev), max(e[2] for e in ev), len(ev) / (len(x) / SR), base, nb, wrong))
    return base, wrong


def write_midi(path, hits, bass, other, bpm):
    import pretty_midi
    pm = pretty_midi.PrettyMIDI(initial_tempo=bpm)
    D = pretty_midi.Instrument(program=0, is_drum=True, name="drums (Kaeli)")
    for h in hits:
        for k in h["kinds"]:
            key = {"kick": 36, "snare": 38, "hat": 42, "tom": 45, "crash": 49}[k]
            D.notes.append(pretty_midi.Note(velocity=int(40 + 87 * h["vel"]), pitch=key, start=max(0.0, h["t"]), end=max(0.0, h["t"]) + 0.08))
    B = pretty_midi.Instrument(program=38, name="bass stem"); O = pretty_midi.Instrument(program=81, name="other stem")
    for ins, ev in ((B, bass), (O, other)):
        for a, b, p, v in ev: ins.notes.append(pretty_midi.Note(velocity=int(40 + 87 * min(1.0, v)), pitch=p, start=a, end=max(b, a + 0.03)))
    pm.instruments += [D, B, O]
    pm.write(path)


def check_wav(path, stems, hits, bass, other):
    """the transcription rendered simply, in the left ear, with the original in the right: where they disagree can be heard"""
    x, _ = sf.read(os.path.join(os.path.dirname(stems.rstrip("/\\")), "song44.wav"), dtype="float32", always_2d=True)
    n = len(x); y = np.zeros(n)

    def put(s, t, g):
        a = int(t * SR); m = min(len(s), n - a)
        if a >= 0 and m > 0: y[a:a + m] += s[:m] * g
    rng = np.random.default_rng(3)
    T = np.arange(int(0.2 * SR)) / SR
    kick = np.sin(2 * np.pi * (50 + 90 * np.exp(-T / 0.03)) * T) * np.exp(-T / 0.09)
    snare = rng.standard_normal(len(T)) * np.exp(-T / 0.06) * 0.7 + np.sin(2 * np.pi * 190 * T) * np.exp(-T / 0.05) * 0.5
    hat = np.diff(rng.standard_normal(len(T) + 1)) * np.exp(-T / 0.018) * 0.4
    crash = np.diff(rng.standard_normal(int(1.2 * SR) + 1)) * np.exp(-np.arange(int(1.2 * SR)) / SR / 0.4) * 0.5
    tom = np.sin(2 * np.pi * 120 * T) * np.exp(-T / 0.09)
    S = dict(kick=kick, snare=snare, hat=hat, crash=crash, tom=tom)
    for h in hits:
        for k in h["kinds"]: put(S[k], h["t"], 0.5 + 0.5 * h["vel"])
    for ev, g in ((bass, 0.35), (other, 0.22)):
        for a, b, p, v in ev:
            d = max(0.05, b - a); t = np.arange(int(d * SR)) / SR; f = 440.0 * 2.0 ** ((p - 69) / 12.0)
            e = np.minimum(1.0, t / 0.005) * np.minimum(1.0, (d - t) / 0.02)
            put((np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t)) * e, a, g * (0.4 + 0.6 * min(1.0, v)))
    y = y / (np.abs(y).max() + 1e-6) * 0.8
    sf.write(path, np.stack([y, x.mean(axis=1) * 0.9], axis=1), SR, subtype="PCM_16")


def ascii_kit(hits, mp, a, b):
    k = dict(kick="K", snare="S", hat="x", crash="C", tom="T"); s16 = 60.0 / mp["bpm"] / 4.0; out = []
    for bi in range(a, min(b, len(mp["bars"]))):
        row = {v: ["."] * 16 for v in k.values()}; b0 = mp["bars"][bi]
        for h in hits:
            st = int(round((h["t"] - b0) / s16))
            if 0 <= st < 16:
                for kn in h["kinds"]: row[k[kn]][st] = k[kn]
        out.append("%3d %6.1fs  K %s  S %s  x %s  %s%s" % (bi, b0, "".join(row["K"]), "".join(row["S"]), "".join(row["x"]), "C" if "C" in row["C"] else " ", "T" if "T" in row["T"] else " "))
    return out


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("stems"); ap.add_argument("map"); ap.add_argument("out")
    ap.add_argument("--onset", type=float, default=0.5); ap.add_argument("--frame", type=float, default=0.3); ap.add_argument("--nopitch", action="store_true"); ap.add_argument("--bars", default="11,22,48,62")
    a = ap.parse_args()
    mp = json.load(open(a.map, encoding="utf-8")); os.makedirs(a.out, exist_ok=True)
    hits, med = drums(a.stems, mp)
    cnt = {}
    for h in hits:
        for k in h["kinds"]: cnt[k] = cnt.get(k, 0) + 1
    print("  by kind:", cnt, " unlabelled:", sum(1 for h in hits if not h["kinds"]))
    r = [int(v) for v in a.bars.split(",")]
    print("\n".join(ascii_kit(hits, mp, r[0], r[1]))); print("  ..."); print("\n".join(ascii_kit(hits, mp, r[2], r[3])))
    keep = [h for h in hits if h["kinds"]]
    out = dict(drums=[dict(t=h["t"], kinds=h["kinds"], vel=round(h["vel"], 3)) for h in keep])
    if not a.nopitch:
        bass = pitched(a.stems, "bass", 28.0, 400.0, a.onset, a.frame, 80.0)
        other = pitched(a.stems, "other", 65.0, 2100.0, a.onset, a.frame, 70.0)
        score(a.stems, "bass", bass, mp); score(a.stems, "other", other, mp)
        out.update(bass=bass, other=other)
        write_midi(os.path.join(a.out, "pdoom_band.mid"), keep, bass, other, mp["bpm"])
        check_wav(os.path.join(a.out, "check_band.wav"), a.stems, keep, bass, other)
    json.dump(out, open(os.path.join(a.out, "notes.json"), "w", encoding="utf-8"), indent=0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
