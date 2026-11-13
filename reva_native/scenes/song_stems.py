#!/usr/bin/env python3
"""song_stems.py -- A RECORDING'S STEMS, with Demucs (for a music video): drums, bass, other and vocals as four wavs, so its notes can be read one instrument at a time.

    <a python 3.11 venv with demucs and soundfile>/python song_stems.py SONG.wav OUTDIR [--model htdemucs] [--threads 6] [--shifts 1]

Not the engine's python: it wants torch, so it lives in its own environment (a python of its own, beside the song). Runs on the CPU, about real time to a few times slower.
"""
import argparse, os, time
import numpy as np
import soundfile as sf
import torch
from demucs.apply import apply_model
from demucs.pretrained import get_model


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("song"); ap.add_argument("out"); ap.add_argument("--model", default="htdemucs")
    ap.add_argument("--threads", type=int, default=6); ap.add_argument("--shifts", type=int, default=1); ap.add_argument("--overlap", type=float, default=0.25)
    a = ap.parse_args()
    torch.set_num_threads(a.threads)
    os.makedirs(a.out, exist_ok=True)
    x, sr = sf.read(a.song, dtype="float32", always_2d=True)
    m = get_model(a.model); m.eval()
    assert sr == m.samplerate, "%s wants %d Hz, this is %d" % (a.model, m.samplerate, sr)
    if x.shape[1] == 1: x = np.repeat(x, 2, axis=1)
    w = torch.from_numpy(x.T)[None]
    ref = w.mean(1)
    mean, std = ref.mean(), ref.std()
    t0 = time.time()
    with torch.no_grad():
        s = apply_model(m, (w - mean) / std, device="cpu", shifts=a.shifts, overlap=a.overlap, progress=True, split=True)[0]
    s = s * std + mean
    for name, y in zip(m.sources, s):
        sf.write(os.path.join(a.out, name + ".wav"), y.numpy().T, sr, subtype="PCM_16")
        print("%-7s rms %.4f peak %.3f" % (name, float(y.pow(2).mean().sqrt()), float(y.abs().max())))
    print("%s: %.1f s of audio took %.0f s" % (a.model, x.shape[0] / sr, time.time() - t0))


if __name__ == "__main__":
    main()
