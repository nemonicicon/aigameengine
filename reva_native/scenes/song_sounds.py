#!/usr/bin/env python3
"""song_sounds.py -- HOW MANY KEYBOARD SOUNDS, AND WHEN (a music video: with two keyboards in front of her, she plays both where their sounds differ).
Each note on the keyboard stem gets the shape of its spectrum, which does not move with its pitch (MFCC 2-12, at its attack and in its body, with what pitch explains regressed out); those split into groups, each tested
against two controls that must lose: one Gaussian blob of the same shape (does it split better than a thing with no groups?) and the same labels shuffled in time (does it come in runs, like a player's phrase?).
  song_sounds.py <stems dir> <map.json> <notes.json> <out sounds.json>
Writes {"upper": [[t0, t1], ...]}: when her right hand plays the upper keyboard, on whole bars."""
import json, os, sys, wave

import numpy as np


def read_stem(path):
    w = wave.open(path); sr = w.getframerate(); ch = w.getnchannels(); bw = w.getsampwidth(); raw = w.readframes(w.getnframes())
    x = np.frombuffer(raw, {2: np.int16, 4: np.int32}[bw]).astype(np.float32).reshape(-1, ch).mean(1) / (2.0 ** (8 * bw - 1))
    return x, sr


class Timbre:
    def __init__(self, x, sr, N=2048):
        self.x, self.sr, self.N = x, sr, N; self.hann = np.hanning(N); fr = np.fft.rfftfreq(N, 1.0 / sr)
        mel = lambda f: 2595.0 * np.log10(1.0 + f / 700.0); imel = lambda m: 700.0 * (10.0 ** (m / 2595.0) - 1.0)
        pts = imel(np.linspace(mel(80.0), mel(12000.0), 42)); self.FB = np.zeros((40, len(fr)))
        for i in range(40):
            a, b, c = pts[i], pts[i + 1], pts[i + 2]
            self.FB[i] = np.clip(np.minimum((fr - a) / (b - a), (c - fr) / (c - b)), 0.0, None)
        self.DCT = np.cos(np.pi * np.outer(np.arange(13), (np.arange(40) + 0.5)) / 40.0)

    def at(self, t):
        i = int(t * self.sr); s = self.x[i:i + self.N]
        if len(s) < self.N: s = np.r_[s, np.zeros(self.N - len(s))]
        return self.DCT @ np.log(self.FB @ (np.abs(np.fft.rfft(s * self.hann)) ** 2) + 1e-9)

    def note(self, e):
        return np.r_[self.at(e[0] + 0.005)[2:13], self.at(e[0] + 0.09)[2:13], np.log(max(0.03, e[1] - e[0]))]


def directions(F, P, k=6):
    V = np.vander((P - P.mean()) / P.std(), 4); R = F - V @ np.linalg.lstsq(V, F, rcond=None)[0]
    Z = (R - R.mean(0)) / (R.std(0) + 1e-9); U, s, _ = np.linalg.svd(Z, full_matrices=False)
    return U[:, :k] * s[:k] / np.sqrt(len(Z))


def kmeans(Y, seed):
    r = np.random.RandomState(seed); C = Y[r.choice(len(Y), 2, replace=False)]
    for _ in range(50):
        D = ((Y[:, None] - C[None]) ** 2).sum(2); L = D.argmin(1)
        C = np.array([Y[L == j].mean(0) if (L == j).any() else C[j] for j in range(2)])
    return L, D[np.arange(len(Y)), L].sum()


def split(Y): return min((kmeans(Y, s) for s in range(10)), key=lambda r: r[1])[0]


def sep(Y, L):
    a, b = Y[L == 0], Y[L == 1]
    return float(np.linalg.norm(a.mean(0) - b.mean(0)) / (0.5 * (a.std(0).mean() + b.std(0).mean())))


def mean_run(L): return float(np.mean(np.diff(np.r_[0, np.where(np.diff(L))[0] + 1, len(L)])))


def test(Y, L, label):
    blob = [sep(G, split(G)) for G in (np.random.RandomState(s).multivariate_normal(np.zeros(Y.shape[1]), np.cov(Y.T), size=len(Y)) for s in range(4))]
    shuf = [mean_run(np.random.RandomState(s).permutation(L)) for s in range(50)]
    print("  %s: split %.2f against one blob %.2f +- %.2f; runs of %.2f notes against shuffled %.2f +- %.2f; groups of %d and %d notes" % (label, sep(Y, L), np.mean(blob), np.std(blob), mean_run(L), np.mean(shuf), np.std(shuf), (L == 0).sum(), (L == 1).sum()))


def runs_of(flags, least):
    """[(first bar, last bar + 1)] of each run of True at least `least` long"""
    out = []; i = 0
    while i < len(flags):
        if flags[i]:
            j = i
            while j < len(flags) and flags[j]: j += 1
            if j - i >= least: out.append((i, j))
            i = j
        else: i += 1
    return out


def main(argv):
    stems, mapf, notesf, outf = argv[1:5]
    bars = np.array(json.load(open(mapf, encoding="utf-8"))["bars"]); notes = json.load(open(notesf, encoding="utf-8"))
    x, sr = read_stem(os.path.join(stems, "other.wav")); T = Timbre(x, sr)
    K = np.array(sorted(n[:4] for n in notes["other"])); F = np.array([T.note(e) for e in K]); print("%d notes on the keyboard stem" % len(K))
    Y = directions(F, K[:, 2]); L = split(Y)
    if (L == 1).sum() > (L == 0).sum(): L = 1 - L                     # 1 is the rarer sound
    test(Y, L, "first split")
    m = L == 0; Y0 = directions(F[m], K[m, 2]); L0 = split(Y0); test(Y0, L0, "inside the larger group")
    first = K[m, 0] < 50.0
    if (L0[first] == 1).sum() > (L0[first] == 0).sum(): L0 = 1 - L0   # 0 leads the song's first 50 s
    who = np.zeros(len(K), int); who[~m] = 2; who[np.where(m)[0][L0 == 1]] = 1; ends = np.r_[bars, bars[-1] + (bars[-1] - bars[-2])]
    n3 = np.array([[((K[:, 0] >= ends[b]) & (K[:, 0] < ends[b + 1]) & (who == s)).sum() for s in range(3)] for b in range(len(bars))])
    # ⭐ THE RARE SOUND is well evidenced: where it leads, in a run of 4 bars or more, her right hand goes up. ⛔ The later change inside the larger group splits no better than a blob, though it comes in runs: 8 bars at 70% and over to count at all
    rare = (n3[:, 2] >= 3) & (n3[:, 2] > n3[:, 0] + n3[:, 1])
    win = np.array([n3[b:b + 8].sum(0) for b in range(len(bars))]); later = win[:, 1] / np.maximum(1, win[:, 0] + win[:, 1]) >= 0.70
    a = runs_of(list(rare), 4); b = runs_of(list(later), 8)
    print("  the rare sound leads:", " ".join("%.1f-%.1f" % (ends[i], ends[j]) for i, j in a) or "nowhere")
    print("  the later change leads:", " ".join("%.1f-%.1f" % (ends[i], ends[j]) for i, j in b) or "nowhere")
    up = np.zeros(len(bars), bool)
    for i, j in a + b: up[i:j] = True
    windows = [[float(ends[i]), float(ends[j])] for i, j in runs_of(list(up), 4)]
    print("  her right hand on the upper keyboard: " + " ".join("%.1f-%.1f" % tuple(w) for w in windows))
    json.dump(dict(upper=windows, rare=[[float(ends[i]), float(ends[j])] for i, j in a], later=[[float(ends[i]), float(ends[j])] for i, j in b]), open(outf, "w", encoding="utf-8"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
