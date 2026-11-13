#!/usr/bin/env python3
"""songmap.py -- A MAP OF A RECORDING: its beat grid, its bars, its drum hits, its chords and every sung word's time, read from
the samples. A music video plays to this: a hit lands because it was scheduled off the same grid as the cut.

    python songmap.py SONG.wav --out DIR [--words] [--clicks] [--prompt "terms the singer uses"]
    python songmap.py --selftest

Writes DIR/map.json (seconds throughout), DIR/map.png to look at, with --clicks DIR/clicks.wav (the song with the map's beats and
hits ticking over it: the one test of a map that can be heard), with --words the words through faster-whisper (on this machine).
⛔ A SUNG MIX HAS NO STEMS: a "kick" is low-band energy on a sixteenth, and a bass note can be one. What the map claims is what a
drummer on screen could plausibly be playing, and it says how well the grid fits, with controls: see report().
"""
import argparse, json, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mixdown as M                                                    # noqa: E402

HOP, WIN = 480, 2048                                                   # 10 ms frames at 48 kHz
FPS = 100.0
BANDS = dict(kick=(30.0, 110.0), snare=(1500.0, 6000.0), body=(150.0, 350.0), hat=(7000.0, 14000.0), all=(30.0, 14000.0))


def fluxes(x):
    """a rising-energy curve per band, frames at FPS"""
    w = np.hanning(WIN); f = np.fft.rfftfreq(WIN, 1.0 / M.SR)
    n = max(0, (len(x) - WIN) // HOP)
    S = np.empty((n, len(f)), np.float32)
    for i in range(n): S[i] = np.abs(np.fft.rfft(x[i * HOP:i * HOP + WIN] * w))
    out = {}
    for k, (lo, hi) in BANDS.items():
        e = np.log1p(S[:, (f >= lo) & (f < hi)]).sum(axis=1)
        d = np.maximum(0.0, np.diff(e, prepend=e[0]))
        out[k] = d / (np.percentile(d, 99) + 1e-9)
    return out, S, f


def fold_score(env, bpm):
    """the best phase for a tempo: how much of the curve lands on its beats"""
    per = FPS * 60.0 / bpm
    bins = np.minimum((np.arange(len(env)) % per).astype(np.int32), int(per))
    tot = np.bincount(bins, env, minlength=int(per) + 1)
    k = np.array([0.5, 1.0, 0.5])
    t = np.convolve(np.concatenate([tot[-1:], tot, tot[:1]]), k, mode="valid")
    i = int(np.argmax(t))
    return float(t[i] / max(1.0, len(env) / per)), i / FPS


def find_grid(env, lo=70.0, hi=180.0):
    """the tempo and phase that put the most onsets on a beat; a coarse sweep, then finer around the winner"""
    cs = [(fold_score(env, b)[0], b) for b in np.arange(lo, hi, 0.1)]
    cs.sort(reverse=True)
    best = cs[0][1]
    fine = [(fold_score(env, b)[0], b) for b in np.arange(best - 0.3, best + 0.3, 0.005)]
    fine.sort(reverse=True)
    bpm = float(fine[0][1])
    return bpm, fold_score(env, bpm)[1], cs[:6]


def _at(env, t):
    """the curve at time t. ⛔ A frame is labelled by its window's START, and a rising edge shows while still in that window's later half, so a
    bare t * FPS reads every onset early (about 20 ms): enough to put a hit visibly before its sound. onset_bias() measures it."""
    return np.interp((np.asarray(t) + ONSET) * FPS, np.arange(len(env)), env)


ONSET = -0.028                                                         # measured by onset_bias(): where an onset at time T peaks in the flux


def drift(env, bpm, t0, dur):
    """how far the grid wanders from the music: each 8 bars' best offset, in seconds"""
    sb = 60.0 / bpm; out = []; offs = np.arange(-0.06, 0.0601, 0.002)
    for b in np.arange(0.0, dur, sb * 32):
        beats = t0 + sb * np.arange(int(b / sb), int(b / sb) + 32)
        beats = beats[beats < dur]
        if len(beats) < 8: continue
        sc = [_at(env, beats + o).sum() for o in offs]
        out.append((b, float(offs[int(np.argmax(sc))])))
    return out


def downbeat(S, f, bpm, t0, dur):
    """which beat starts a bar: a snare is on 2 and 4, so a bar starts one beat before one. Which beats have the snare is read by its RING: 35-90 ms after the beat, 1-3 kHz, where a snare's noise is
    still going and a kick's click is gone. ⛔ NOT by its attack: this song's snare starts before the grid, so the beat WITHOUT it shows the bigger rise in that band, and an attack test called the wrong beats
    the backbeat, one beat out for the whole song. That leaves 1 or 3 for the bar's start: `--bar1` settles it from a crash or the words."""
    sb = 60.0 / bpm; beats = t0 + sb * np.arange(int((dur - t0) / sb)); ok = (f >= 1000.0) & (f < 3000.0)
    e = np.sqrt((S[:, ok] ** 2).sum(axis=1)); a, b = int(0.035 * FPS), int(0.090 * FPS)
    ring = np.array([e[int(t * FPS) + a:int(t * FPS) + b].mean() if int(t * FPS) + b < len(e) else 0.0 for t in beats])
    even, odd = float(np.median(ring[0::2])), float(np.median(ring[1::2]))
    return (1 if even > odd else 0), even, odd


TH = dict(kick=0.22, snare=0.30, hat=0.15)


def kit(fx, bpm, t0, dur, shift=0.0, p=0):
    """⛔ every sixteenth, thresholded against its own neighbours, finds as many "hits" on a grid shifted a 32nd (562 against 555 kicks: a
    test that fails): the mix is too busy to read hit by hit. So: only where a kit plays this groove, and by absolute level. The body band on
    each beat is the kick (below 110 Hz is the off-beat bass), the snare band on the backbeat (a bar's 2 and 4: on this song's four-on-the-floor
    the band fires on every beat, and a snare on the 1 is the kick's attack), the hat band on each eighth."""
    sb = 60.0 / bpm
    beats = t0 + shift + sb * np.arange(int((dur - t0 - shift) / sb))
    eights = t0 + shift + sb / 2.0 * np.arange(int((dur - t0 - shift) / (sb / 2.0)))
    back = beats[(np.arange(len(beats)) - p) % 2 == 1]
    return dict(kick=beats[_at(fx["body"], beats) > TH["kick"]], snare=back[_at(fx["snare"], back) > TH["snare"]], hat=eights[_at(fx["hat"], eights) > TH["hat"]])


def chords(S, f, bpm, t0, dur):
    """each beat's best triad, by its chroma, smoothed over two beats"""
    ok = (f >= 70.0) & (f < 1400.0)
    pc = (np.round(12.0 * np.log2(f[ok] / 440.0)).astype(np.int64) + 9) % 12
    P = S[:, ok] ** 2
    names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
    tmpl = [(names[r] + q, np.roll(np.array([1, 0, 0, 1 if q == "m" else 0, 0 if q == "m" else 1, 0, 0, 1, 0, 0, 0, 0], float), r)) for r in range(12) for q in ("", "m")]
    sb = 60.0 / bpm; out = []
    beats = t0 + sb * np.arange(int((dur - t0) / sb))
    for t in beats:
        a, b = int((t - 0.2 * sb) * FPS), int((t + 1.8 * sb) * FPS)
        if a < 0 or b >= len(P): out.append((float(t), "")); continue
        c = np.zeros(12); np.add.at(c, pc, P[a:b].sum(axis=0))
        c /= c.sum() + 1e-9
        out.append((float(t), max(tmpl, key=lambda kv: float(np.dot(kv[1], c)))[0]))
    run = []
    for t, nm in out:
        if not run or run[-1][1] != nm: run.append((t, nm))
    return run


def report(fx, bpm, t0, dur, cands):
    """the numbers that could have come out badly"""
    env = fx["all"]; sb = 60.0 / bpm
    beats = t0 + sb * np.arange(int((dur - t0) / sb))
    on = _at(env, beats).mean(); off = _at(env, beats + sb / 2.0 * 0.5).mean()
    print("grid: %.3f BPM, first beat %.3f s; onsets on the beat %.3f, a quarter-beat off it %.3f" % (bpm, t0, on, off))
    print("  control: a wrong tempo scores %.3f at %.1f BPM against %.3f" % (fold_score(env, bpm * 1.03)[0], bpm * 1.03, fold_score(env, bpm)[0]))
    print("  the sweep's best tempos:", [round(float(b), 1) for _, b in cands])
    d = drift(env, bpm, t0, dur)
    print("  drift each 8 bars (ms):", " ".join("%+.0f" % (o * 1000) for _, o in d), " rms %.1f ms" % (1000.0 * math.sqrt(np.mean([o * o for _, o in d]))))
    return d


def ascii_bars(mp, cols=8):
    """the kit as text, one line a bar, on its eighths: regular means a kit, noise means the bands are hearing something else"""
    sb = 60.0 / mp["bpm"]; out = []
    has = lambda L, t: any(abs(u - t) < 0.02 for u in L)
    for i, b in enumerate(mp["bars"]):
        c = []
        for e in range(cols):
            t = b + e * sb / 2.0; k = has(mp["kick"], t); n = has(mp["snare"], t); h = has(mp["hat"], t)
            c.append("B" if k and n else "K" if k else "S" if n else "x" if h else ".")
        out.append("%3d %6.1fs  %s" % (i, b, " ".join("".join(c[j:j + 2]) for j in range(0, cols, 2))))
    return out


def words_of(path, prompt, model="large-v3"):
    os.environ.setdefault("HF_HUB_OFFLINE", "1")
    from faster_whisper import WhisperModel
    m = WhisperModel(model, device="cpu", compute_type="int8")
    seg, _ = m.transcribe(path, language="en", beam_size=5, temperature=0.0, vad_filter=False, word_timestamps=True,
                          condition_on_previous_text=True, initial_prompt=prompt or None)
    out = []
    for s in seg:
        for w in s.words or []: out.append(dict(t0=round(w.start, 3), t1=round(w.end, 3), w=w.word.strip(), p=round(w.probability, 2)))
    return out


def clicks(x, sr, mp):
    """the song with the map ticking over it: if the ticks sit in the music, a body hitting on them will"""
    y = x.copy()

    def tick(ts, f, d, g):
        n = int(d * sr); t = np.arange(n) / sr
        b = np.sin(2.0 * math.pi * f * t) * np.exp(-t / (d * 0.3)) * g
        for u in ts:
            a = int(u * sr)
            if 0 <= a and a + n < len(y): y[a:a + n] += b
    tick(mp["bars"], 2000.0, 0.03, 0.35); tick(mp["beats"], 1200.0, 0.02, 0.18); tick(mp["kick"], 90.0, 0.10, 0.5); tick(mp["snare"], 400.0, 0.05, 0.3)
    return np.clip(y, -1.0, 1.0)


def sheet(path, x, fx, mp):
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(2, 1, figsize=(20, 7), sharex=True)
    t = np.arange(len(x))[::200] / M.SR
    ax[0].plot(t, x[::200], lw=0.2, color="#7a2fd0")
    for b in mp["bars"][::4]: ax[0].axvline(b, color="#999", lw=0.3)
    for s in mp.get("sections", []): ax[0].text(s["t0"], 0.8, s["name"], fontsize=9)
    ax[1].plot(np.arange(len(fx["kick"])) / FPS, fx["kick"], lw=0.3, label="kick band")
    ax[1].plot(mp["kick"], [1.1] * len(mp["kick"]), "|", ms=6, label="kick"); ax[1].plot(mp["snare"], [1.3] * len(mp["snare"]), "|", ms=6, label="snare"); ax[1].plot(mp["hat"], [1.5] * len(mp["hat"]), "|", ms=4, label="hat")
    ax[1].legend(loc="upper right"); ax[1].set_ylim(0, 1.7)
    fig.tight_layout(); fig.savefig(path, dpi=70); plt.close(fig)


def onset_bias(fx, times):
    """how far the flux's peak sits from onsets at known times, at ONSET = 0"""
    global ONSET
    keep = ONSET; ONSET = 0.0
    offs = np.arange(-0.05, 0.0501, 0.001); sc = [_at(fx["all"], times + o).sum() for o in offs]
    ONSET = keep
    return float(offs[int(np.argmax(sc))])


def selftest():
    # a made-up drum track at a known tempo: the map must find it, and must not find one that is not there
    sr = M.SR; dur = 40.0; bpm = 128.0; sb = 60.0 / bpm; x = np.zeros(int(dur * sr)); rng = np.random.default_rng(3); t0 = 0.5
    k = np.exp(-np.arange(int(0.12 * sr)) / (0.03 * sr)) * np.sin(2.0 * math.pi * 70.0 * np.arange(int(0.12 * sr)) / sr)
    s = np.exp(-np.arange(int(0.10 * sr)) / (0.02 * sr)) * rng.standard_normal(int(0.10 * sr))
    for i in range(int((dur - t0 - 1.0) / sb)):
        a = int((t0 + i * sb) * sr)
        if i % 4 in (0, 2): x[a:a + len(k)] += k * 0.8
        if i % 4 in (1, 3): x[a:a + len(s)] += s * 0.5
    fx, S, f = fluxes(x)
    got, ph, _ = find_grid(fx["all"], 90.0, 170.0)
    bias = onset_bias(fx, t0 + sb * np.arange(int((dur - t0 - 1.0) / sb)))
    print("an onset at T peaks in the flux at T%+.0f ms (ONSET is %+.0f ms)" % (bias * 1000, ONSET * 1000))
    assert abs(bias - ONSET) < 0.006, "ONSET is out of date"
    assert abs(got - bpm) < 0.2 or abs(got - bpm * 2) < 0.4 or abs(got * 2 - bpm) < 0.4, "the tempo: %.2f" % got
    S2, f2 = S, f
    p, ev, od = downbeat(S2, f2, bpm, t0, dur)
    assert p == 0 and od > ev, "the backbeat: this track's snare is on odd beats, so its bar starts on beat 0 (%.2f %.2f)" % (ev, od)
    h = kit(fx, bpm, t0, dur); o = kit(fx, bpm, t0, dur, shift=sb / 8.0)
    assert len(h["snare"]) > 30 and len(o["snare"]) < 0.25 * len(h["snare"]), "the grid finds the hits a shifted one does not"
    noise = rng.standard_normal(int(dur * sr)) * 0.2
    assert fold_score(fluxes(noise)[0]["all"], bpm)[0] < fold_score(fx["all"], got)[0], "noise has no beat to find"
    print("songmap selftest OK")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song", nargs="?"); ap.add_argument("--out"); ap.add_argument("--words", action="store_true"); ap.add_argument("--clicks", action="store_true")
    ap.add_argument("--prompt", default=""); ap.add_argument("--selftest", action="store_true"); ap.add_argument("--bar1", type=int, default=0, help="beats to move the bar's start by, once the words show where the phrases start")
    a = ap.parse_args()
    if a.selftest: return selftest()
    x0, sr = M.read_wav(a.song)
    if sr != M.SR: raise SystemExit("48 kHz, please")
    x = M.mono(x0).astype(np.float64); dur = len(x) / sr
    os.makedirs(a.out, exist_ok=True)
    fx, S, f = fluxes(x)
    bpm, ph, cands = find_grid(fx["all"])
    if abs(bpm - round(bpm)) < 0.05: bpm = float(round(bpm))           # a machine's track is on a round tempo: the fit says so to 0.005
    sb = 60.0 / bpm
    offs = np.arange(0.0, sb, 0.001)
    t0 = float(offs[int(np.argmax([_at(fx["all"], o + sb * np.arange(int((dur - o) / sb))).sum() for o in offs]))])
    report(fx, bpm, t0, dur, cands)
    p, ev, od = downbeat(S, f, bpm, t0, dur)
    p = (p + a.bar1) % 4
    print("  a snare's ring on alternate beats: %.2f against %.2f, so the bar starts on beat index %d" % (ev, od, p))
    hs = kit(fx, bpm, t0, dur, p=p); off = kit(fx, bpm, t0, dur, shift=sb / 8.0, p=p)
    print("  control: hits on the grid %d %d %d; on a grid shifted a 32nd %d %d %d (kick snare hat)" % (len(hs["kick"]), len(hs["snare"]), len(hs["hat"]), len(off["kick"]), len(off["snare"]), len(off["hat"])))
    beats = t0 + sb * np.arange(int((dur - t0) / sb))
    bars = beats[p::4]
    mp = dict(song=os.path.basename(a.song), seconds=dur, bpm=bpm, t0=t0, beats=[round(float(b), 4) for b in beats], bars=[round(float(b), 4) for b in bars],
              kick=[round(float(u), 4) for u in hs["kick"]], snare=[round(float(u), 4) for u in hs["snare"]], hat=[round(float(u), 4) for u in hs["hat"]],
              chords=chords(S, f, bpm, t0, dur))
    print("\n".join(ascii_bars(mp)))
    print("chords (first 24):", " ".join("%s@%.1f" % (n, t) for t, n in mp["chords"][:24]))
    if a.words:
        mp["words"] = words_of(a.song, a.prompt)
        print("words: %d" % len(mp["words"]))
    path = os.path.join(a.out, "map.json")
    if not a.words and os.path.exists(path):                           # the words take minutes: a map rerun keeps them
        old = json.load(open(path, encoding="utf-8"))
        if "words" in old: mp["words"] = old["words"]
    json.dump(mp, open(path, "w", encoding="utf-8"), indent=0)
    sheet(os.path.join(a.out, "map.png"), x, fx, mp)
    if a.clicks: M.write_wav(os.path.join(a.out, "clicks.wav"), clicks(x, sr, mp))
    return 0


if __name__ == "__main__":
    sys.exit(main())
