"""mixdown.py -- the FILM MIX's tools (numpy only): wav in/out, resampling, silence trims, loudness to ITU BS.1770,
rooms (a synthetic impulse response and FFT convolution), ducking, a lookahead limiter.

compile_scene.py builds a `mix film` scene with these (design/scene_direction.md "THE FILM MIX"); score.py makes the
bed it mixes under the dialogue. Everything works in float32 at SR (48 kHz); mono is shape (n,), stereo (n, 2).

    python mixdown.py --selftest
"""
import math, os, wave
import numpy as np

SR = 48000


# ---------------------------------------------------------------------------------------------- wav in and out
def read_wav(path):
    """float32 in -1..1, (n,) mono or (n, 2) stereo, and its rate (8/16/24/32-bit PCM)"""
    with wave.open(path, "rb") as w:
        sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if sw == 2: x = np.frombuffer(raw, "<i2").astype(np.float32) / 32768.0
    elif sw == 1: x = (np.frombuffer(raw, np.uint8).astype(np.float32) - 128.0) / 128.0
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16); v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float32) / float(1 << 23)
    else: x = np.frombuffer(raw, "<i4").astype(np.float32) / 2147483648.0
    if ch > 1: x = x.reshape(-1, ch)[:, :2]
    return x, sr


def write_wav(path, x, sr=SR, seed=0):
    """16-bit with TPDF dither (a quiet fade must not turn to grit)"""
    x = np.asarray(x, np.float64)
    ch = 1 if x.ndim == 1 else x.shape[1]
    rng = np.random.default_rng(seed)
    d = (rng.random(x.shape) - rng.random(x.shape)) / 32768.0
    q = np.clip(np.round((x + d) * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(ch); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(q.tobytes())


def mono(x): return x if x.ndim == 1 else x.mean(axis=1)


def stereo(x): return np.stack([x, x], axis=1) if x.ndim == 1 else x


def db(v): return 20.0 * math.log10(max(v, 1e-12))


def undb(d): return 10.0 ** (d / 20.0)


# ---------------------------------------------------------------------------------------------- resampling
def resample(x, sr_in, sr_out=SR, taps=24):
    """band-limited (windowed sinc, Hann, `taps` a side) to any rate; per channel"""
    if sr_in == sr_out: return np.asarray(x, np.float32)
    if x.ndim == 2: return np.stack([resample(x[:, c], sr_in, sr_out, taps) for c in range(x.shape[1])], axis=1)
    x = np.asarray(x, np.float64)
    if sr_out % sr_in == 0 and sr_out // sr_in <= 8:
        # an integer factor (Kokoro's 24 kHz to 48 kHz): zero-stuff and low-pass in one convolution -- the same
        # windowed sinc as below, sampled at the output rate, run by np.convolve instead of a pass per tap
        L = sr_out // sr_in
        up = np.zeros(len(x) * L); up[::L] = x
        k = np.arange(-taps * L, taps * L + 1) / float(L)
        cut = 0.97
        h = cut * np.sinc(cut * k) * (0.5 + 0.5 * np.cos(np.pi * np.clip(k / taps, -1.0, 1.0)))
        return np.convolve(up, h, mode="same").astype(np.float32)
    ratio = sr_in / float(sr_out)                     # input samples per output sample
    n_out = int(math.floor(len(x) / ratio))
    t = np.arange(n_out) * ratio
    i0 = np.floor(t).astype(np.int64); fr = t - i0
    cut = min(1.0, 1.0 / ratio) * 0.97                # below the lower of the two Nyquists
    out = np.zeros(n_out)
    xp = np.concatenate([np.zeros(taps), x, np.zeros(taps + 1)])
    for k in range(-taps + 1, taps + 1):
        a = fr - k
        w = cut * np.sinc(cut * a) * (0.5 + 0.5 * np.cos(np.pi * np.clip(a / taps, -1.0, 1.0)))
        out += xp[i0 + k + taps] * w
    return out.astype(np.float32)


def speedup(x, ratio, taps=24):
    """the same samples played `ratio` times faster: pitch AND formants up by ratio, length / ratio"""
    return resample(x, ratio, 1.0, taps) if ratio != 1.0 else np.asarray(x, np.float32)


# ---------------------------------------------------------------------------------------------- trims
def env_db(x, sr, win=0.010):
    """the level per `win` seconds, dBFS (mono)"""
    m = mono(x); h = max(1, int(sr * win)); n = len(m) // h
    if n == 0: return np.array([-120.0])
    p = (m[:n * h].reshape(n, h) ** 2).mean(axis=1)
    return 10.0 * np.log10(p + 1e-12)


def speech_span(x, sr, thr=-48.0, win=0.010):
    """(first, last) sample where the level is within `thr` dB of the line's own loud part"""
    e = env_db(x, sr, win); h = max(1, int(sr * win))
    ref = np.percentile(e, 95) if len(e) else -120.0
    on = np.where(e > max(thr, ref - 42.0))[0]
    if not len(on): return 0, len(x)
    return int(on[0] * h), int(min(len(x), (on[-1] + 1) * h))


def trim(x, sr, lead=0.025, tail=0.090, fade=0.008):
    """the line without its leading and trailing silence, a breath of room kept either side; returns (y, cut_at_start_s)"""
    a, b = speech_span(x, sr)
    a = max(0, a - int(lead * sr)); b = min(len(x), b + int(tail * sr))
    y = np.array(x[a:b], np.float32, copy=True)
    nf = min(len(y) // 2, int(fade * sr))
    if nf > 0:
        r = np.linspace(0.0, 1.0, nf, dtype=np.float32)
        if y.ndim == 2: r = r[:, None]
        y[:nf] *= r; y[-nf:] *= r[::-1]
    return y, a / float(sr)


# ---------------------------------------------------------------------------------------------- loudness (BS.1770-4)
def _kweight_mag(n, sr):
    """|H(f)| of the K-weighting (the standard's 48 kHz shelf + RLB high-pass, re-derived for sr), on rfft bins"""
    # the two biquads by their analog prototypes (pyloudnorm's derivation), at this rate
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / sr); Vh = 10 ** (G / 20.0); Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / sr)
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K), (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K)]
    w = np.exp(-1j * np.pi * np.arange(n // 2 + 1) / (n // 2))
    def H(b, a): return (b[0] + b[1] * w + b[2] * w * w) / (a[0] + a[1] * w + a[2] * w * w)
    return np.abs(H(b1, a1) * H(b2, a2))


def lufs(x, sr=SR, gate=True, excerpt=None):
    """integrated loudness, LUFS (400 ms blocks, 75% overlap, absolute -70 and relative -10 LU gates); `excerpt` s:
    measure only that much from the middle (a steady bed reads the same, at a fraction of the cost)"""
    if excerpt and len(x) > int(excerpt * sr):
        a = (len(x) - int(excerpt * sr)) // 2
        x = x[a:a + int(excerpt * sr)]
    x = stereo(np.asarray(x, np.float64)) if np.asarray(x).ndim == 2 else np.asarray(x, np.float64)[:, None]
    n = len(x)
    if n < int(0.4 * sr): x = np.concatenate([x, np.zeros((int(0.4 * sr) - n, x.shape[1]))]); n = len(x)
    N = 1 << int(math.ceil(math.log2(n + 1)))
    Hm = _kweight_mag(N, sr)
    y = np.stack([np.fft.irfft(np.fft.rfft(x[:, c], N) * Hm, N)[:n] for c in range(x.shape[1])], axis=1)
    blk, hop = int(0.4 * sr), int(0.1 * sr)
    cs = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y * y, axis=0)])
    starts = np.arange(0, n - blk + 1, hop)
    z = ((cs[starts + blk] - cs[starts]) / blk).sum(axis=1)     # channel weights 1, 1
    l = -0.691 + 10.0 * np.log10(z + 1e-20)
    if not gate: return float(-0.691 + 10.0 * math.log10(z.mean() + 1e-20))
    keep = l > -70.0
    if not keep.any(): return -120.0
    rel = -0.691 + 10.0 * math.log10(z[keep].mean()) - 10.0
    keep &= l > rel
    if not keep.any(): return -120.0
    return float(-0.691 + 10.0 * math.log10(z[keep].mean()))


def to_lufs(x, target, sr=SR, excerpt=None):
    """x scaled to `target` LUFS (silence comes back unchanged)"""
    L = lufs(x, sr, excerpt=excerpt)
    return x if L < -100.0 else (np.asarray(x, np.float32) * undb(target - L)).astype(np.float32)


# ---------------------------------------------------------------------------------------------- rooms
def room_ir(sr=SR, rt60=0.45, predelay=0.006, bright=0.6, early=0.5, size=4.0, seed=1, length=None):
    """a stereo impulse response: early reflections (a box `size` m across) and a diffuse tail decaying `rt60` s,
    its highs dying sooner (`bright` 0 dark .. 1 flat), unit energy"""
    n = int(sr * (length or max(0.25, rt60 * 1.15)))
    rng = np.random.default_rng(seed)
    t = np.arange(n) / sr
    f = np.fft.rfftfreq(n, 1.0 / sr)
    out = np.zeros((n, 2))
    bands = ((0.0, 300.0, 1.20), (300.0, 1500.0, 1.0), (1500.0, 5000.0, 0.55 + 0.35 * bright), (5000.0, sr / 2.0 + 1, 0.25 + 0.5 * bright))
    for c in range(2):
        F = np.fft.rfft(rng.standard_normal(n))
        y = np.zeros(n)
        for lo, hi, m in bands:
            yb = np.fft.irfft(F * ((f >= lo) & (f < hi)), n)
            y += yb * np.exp(-6.9078 * t / max(0.05, rt60 * m))
        onset = np.clip((t - predelay) / 0.012, 0.0, 1.0)            # the tail grows in after the direct sound
        y *= onset
        # early reflections: six image taps off the box's walls, per ear
        for k in range(6):
            d = predelay + (size * (0.6 + 0.9 * rng.random())) / 343.0
            i = int(d * sr)
            if i < n: y[i] += early * (0.7 ** k) * (1.0 if rng.random() > 0.5 else -1.0) * np.sqrt(np.mean(y[i:i + 400] ** 2) + 1e-9) * 6.0
        out[:, c] = y
    out /= math.sqrt((out ** 2).sum() / 2.0) + 1e-12
    return out.astype(np.float32)


def convolve(x, ir, block=1 << 16):
    """FFT overlap-add; mono x with a stereo IR gives stereo; the result is len(x) + len(ir) - 1 long"""
    x = np.asarray(x, np.float64)
    ir = np.asarray(ir, np.float64)
    xs = x[:, None] if x.ndim == 1 else x
    irs = ir[:, None] if ir.ndim == 1 else ir
    nch = max(xs.shape[1], irs.shape[1])
    n, m = len(xs), len(irs)
    N = 1 << int(math.ceil(math.log2(block + m - 1)))
    out = np.zeros((n + m - 1, nch))
    IR = [np.fft.rfft(irs[:, min(c, irs.shape[1] - 1)], N) for c in range(nch)]
    for s in range(0, n, block):
        seg = xs[s:s + block]
        for c in range(nch):
            X = np.fft.rfft(seg[:, min(c, xs.shape[1] - 1)], N)
            y = np.fft.irfft(X * IR[c], N)[:len(seg) + m - 1]
            out[s:s + len(y), c] += y
    return (out[:, 0] if nch == 1 else out).astype(np.float32)


# ---------------------------------------------------------------------------------------------- ducking, limiting
def smooth_gain(target, sr, attack=0.12, release=0.45, rate=1000):
    """a per-sample gain that follows `target` (per-sample, linear) down in `attack` s and back up in `release` s"""
    n = len(target); h = max(1, sr // rate); m = (n + h - 1) // h
    tg = np.minimum.reduceat(np.asarray(target, np.float64), np.arange(0, n, h))
    a, r = math.exp(-1.0 / (attack * rate)), math.exp(-1.0 / (release * rate))
    g = np.empty(m); v = tg[0]
    for i in range(m):
        k = a if tg[i] < v else r
        v = tg[i] + (v - tg[i]) * k
        g[i] = v
    return np.interp(np.arange(n), np.arange(m) * h, g).astype(np.float32)


def duck(n, sr, spans, depth_db=-9.0, attack=0.12, release=0.45, lead=0.10):
    """the bed's gain under dialogue: depth_db while anyone speaks (each span opened `lead` s early)"""
    tg = np.ones(n)
    k = undb(depth_db)
    for a, b in spans:
        i, j = max(0, int((a - lead) * sr)), min(n, int(b * sr))
        if j > i: tg[i:j] = k
    return smooth_gain(tg, sr, attack, release)


def limiter(x, sr=SR, ceiling_db=-1.0, release=0.08, look=0.005):
    """a lookahead peak limiter: nothing over the ceiling, the gain recovering in `release` s"""
    x = np.asarray(x, np.float32)
    pk = np.abs(x) if x.ndim == 1 else np.abs(x).max(axis=1)
    c = undb(ceiling_db)
    h = max(1, int(sr * look))
    m = (len(pk) + h - 1) // h
    blk = np.maximum.reduceat(pk, np.arange(0, len(pk), h))
    need = np.minimum(1.0, c / np.maximum(blk, 1e-9))
    need = np.minimum(need, np.concatenate([need[1:], need[-1:]]))      # look one block ahead
    r = math.exp(-1.0 / (release * sr / h)); g = np.empty(m); v = 1.0
    for i in range(m):
        v = need[i] if need[i] < v else need[i] + (v - need[i]) * r
        g[i] = v
    gs = np.interp(np.arange(len(pk)), np.arange(m) * h, g).astype(np.float32)
    y = x * (gs if x.ndim == 1 else gs[:, None])
    return np.clip(y, -c, c)


def pan(x, p):
    """mono -> stereo at p (-1 left .. 1 right), constant power"""
    a = (p + 1.0) * math.pi / 4.0
    return np.stack([x * math.cos(a), x * math.sin(a)], axis=1).astype(np.float32)


def place(buf, x, at, gain=1.0):
    """add x into buf from sample `at` (clipped at both ends)"""
    i0 = max(0, at); j = min(len(buf), at + len(x))
    if j > i0: buf[i0:j] += x[i0 - at:j - at] * gain
    return buf


# ---------------------------------------------------------------------------------------------- self-test
def selftest():
    bad = []
    def check(what, ok, got=""):
        print("  %-60s %s  %s" % (what, "ok  " if ok else "FAIL", got)); ok or bad.append(what)
    sr = SR; t = np.arange(sr * 2) / sr
    # 1. loudness: a 997 Hz sine at -20 dBFS peak is -23.0 LUFS on both channels... per channel -20 dBFS sine -> -23.01 per
    #    channel, +3.01 for two: the standard's own example (a stereo -23 dBFS 1 kHz tone reads -23.0)
    s = np.sin(2 * np.pi * 997 * t) * undb(-23.0) * math.sqrt(2)
    L1 = lufs(np.stack([s, s], 1), sr)
    check("BS.1770: a stereo 997 Hz tone at -23 dB RMS reads -20.0 LUFS", abs(L1 - (-20.0)) < 0.15, "%.2f" % L1)
    L2 = lufs(s, sr)
    check("BS.1770: the same tone in one channel reads 3 dB less", abs(L2 - (-23.0)) < 0.15, "%.2f" % L2)
    lo = np.sin(2 * np.pi * 40 * t) * undb(-23.0) * math.sqrt(2)
    check("K-weighting: 40 Hz reads quieter than 1 kHz (the RLB high-pass)", lufs(lo, sr) < L2 - 0.4, "%.2f" % lufs(lo, sr))
    # 2. resampling: a 1 kHz tone from 24 kHz to 48 kHz keeps its frequency and level, and nothing aliases
    x24 = np.sin(2 * np.pi * 1000 * np.arange(24000) / 24000.0).astype(np.float32)
    y = resample(x24, 24000, 48000)
    F = np.abs(np.fft.rfft(y[2000:2000 + 32768] * np.hanning(32768))); fpk = np.argmax(F) * 48000.0 / 32768
    check("resample 24k -> 48k: 1 kHz stays 1 kHz, level kept", abs(fpk - 1000) < 3 and abs(np.abs(y[2000:-2000]).max() - 1.0) < 0.02,
          "%.1f Hz peak %.3f" % (fpk, np.abs(y[2000:-2000]).max()))
    img = F[int(23000 * 32768 / 48000):int(25000 * 32768 / 48000)].max() / F.max()
    check("resample: no image at 23 kHz (the old Nyquist fold)", img < 1e-3, "%.1e" % img)
    # 3. a speedup by 2^(4/12) raises a tone by four semitones
    z = speedup(x24, 2 ** (4 / 12.0))
    Fz = np.abs(np.fft.rfft(z[1000:1000 + 8192] * np.hanning(8192))); fz = np.argmax(Fz) * 24000.0 / 8192
    check("speedup 2^(4/12): 1 kHz -> 1260 Hz", abs(fz - 1259.9) < 4, "%.1f Hz" % fz)
    # 4. trim keeps the speech and a breath either side
    sig = np.concatenate([np.zeros(sr // 2), np.sin(2 * np.pi * 300 * t[:sr]) * 0.3, np.zeros(sr // 2)]).astype(np.float32)
    y, cut = trim(sig, sr)
    check("trim: 0.5 s of silence cut to the 25 ms lead, 90 ms tail kept", abs(cut - 0.475) < 0.012 and abs(len(y) / sr - 1.115) < 0.02,
          "cut %.3f s, %.3f s left" % (cut, len(y) / sr))
    # 5. a room: unit energy, decays by 60 dB in about rt60
    ir = room_ir(sr, rt60=0.6)
    e = env_db(ir[:, 0], sr, 0.02); e -= e.max()
    t60 = (np.where(e > -45)[0][-1] + 1) * 0.02 * 60 / 45.0
    check("room_ir: unit energy, the tail falls ~60 dB in rt60", abs((ir ** 2).sum() / 2 - 1) < 1e-3 and 0.4 < t60 < 0.9, "t60 ~ %.2f s" % t60)
    # 6. convolution with a unit impulse is the identity
    imp = np.zeros(64, np.float32); imp[0] = 1.0
    y = convolve(x24, imp)
    check("convolve: a unit impulse returns the signal", np.abs(y[:len(x24)] - x24).max() < 1e-5)
    # 7. ducking reaches its depth inside a span and comes back after it
    g = duck(sr * 4, sr, [(1.0, 2.5)], depth_db=-9.0)
    check("duck: -9 dB inside the span, within 0.5 dB of 0 1.4 s after it", abs(db(g[int(2.0 * sr)]) + 9.0) < 0.3 and db(g[int(3.9 * sr)]) > -0.5,
          "%.2f / %.2f dB" % (db(g[int(2.0 * sr)]), db(g[int(3.9 * sr)])))
    # 8. the limiter holds the ceiling
    hot = (np.sin(2 * np.pi * 220 * t) * 2.0).astype(np.float32)
    y = limiter(hot, sr, -1.0)
    check("limiter: a +6 dB sine comes out at -1 dBFS", np.abs(y).max() <= undb(-1.0) + 1e-6, "%.2f dBFS" % db(np.abs(y).max()))
    print("SELFTEST", "PASS" if not bad else "FAIL: " + "; ".join(bad))
    return not bad


if __name__ == "__main__":
    import sys
    if "--selftest" in sys.argv: sys.exit(0 if selftest() else 1)
    print(__doc__)
