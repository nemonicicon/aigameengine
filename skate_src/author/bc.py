#!/usr/bin/env python3
"""bc.py -- texture compression in numpy, for the cook. BC1 and BC3 are the two formats the Pi and the PC both sample (engine plan, "Packs become chunked, compressed"): a 4x4 block of pixels becomes 8 bytes (BC1) or 16 (BC3, for a texture with alpha). BC7 is the PC's: 16 bytes a block, a better picture, and the Pi can't sample it.

library: encode(rgba, want_alpha) -> bytes for one level. encode7(rgba) -> the same in BC7. mips(rgba) -> every level, largest first. decode(data, w, h, fmt) -> rgba, the check.
usage:   bc.py IMAGE [OUT.png] [--bc7]    compress an image, decode it again, say how close it came, and write the result to look at"""
import sys

import numpy as np

BC1, BC3, BC7 = 1, 3, 7
W4 = np.array([0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64], np.int32)   # BC7's own table for 4-bit indices


def blocks_of(img):
    """(h, w, c) into (n, 16, c), row by row, a block at a time. Edges repeat to fill a block"""
    h, w = img.shape[:2]
    bw, bh = (w + 3) // 4, (h + 3) // 4
    if w % 4 or h % 4:
        img = img[np.minimum(np.arange(bh * 4), h - 1)][:, np.minimum(np.arange(bw * 4), w - 1)]
    return img.reshape(bh, 4, bw, 4, -1).transpose(0, 2, 1, 3, 4).reshape(bh * bw, 16, -1), bw, bh


def to565(c):
    return (np.clip(np.round(c[..., 0] * 31 / 255), 0, 31).astype(np.uint32) << 11) | (np.clip(np.round(c[..., 1] * 63 / 255), 0, 63).astype(np.uint32) << 5) | np.clip(np.round(c[..., 2] * 31 / 255), 0, 31).astype(np.uint32)


def from565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.float32)


def colour_blocks(px):
    """(n, 16, 3) into (n, 8) bytes: two 565 endpoints along each block's main axis, and a 2-bit choice per pixel. One refit of the endpoints to the choices"""
    px = px.astype(np.float32)
    mean = px.mean(1, keepdims=True)
    d = px - mean
    cov = np.einsum("nij,nik->njk", d, d)
    axis = np.ones((len(px), 3), np.float32) / np.sqrt(3)
    for _ in range(4):
        axis = np.einsum("njk,nk->nj", cov, axis)
        axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-6)
    t = np.einsum("nij,nj->ni", d, axis)
    lo = mean[:, 0] + axis * t.min(1, keepdims=True)
    hi = mean[:, 0] + axis * t.max(1, keepdims=True)
    for _ in range(2):
        c0, c1 = to565(hi), to565(lo)
        swap = c0 < c1
        c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
        p0, p1 = from565(c0), from565(c1)
        pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)
        idx = np.argmin(((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1), axis=2)
        # least squares for the endpoints, given the choices
        w0 = np.array([1, 0, 2 / 3, 1 / 3], np.float32)[idx]
        w1 = 1 - w0
        a, b, c = (w0 * w0).sum(1), (w0 * w1).sum(1), (w1 * w1).sum(1)
        det = a * c - b * b
        ok = np.abs(det) > 1e-3
        x0 = (w0[:, :, None] * px).sum(1)
        x1 = (w1[:, :, None] * px).sum(1)
        safe = np.where(ok, det, 1.0)[:, None]
        hi = np.where(ok[:, None], (c[:, None] * x0 - b[:, None] * x1) / safe, p0)
        lo = np.where(ok[:, None], (a[:, None] * x1 - b[:, None] * x0) / safe, p1)
    c0, c1 = to565(np.clip(hi, 0, 255)), to565(np.clip(lo, 0, 255))
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    p0, p1 = from565(c0), from565(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3], 1)
    idx = np.argmin(((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1), axis=2).astype(np.uint32)
    idx[c0 == c1] = 0
    bits = (idx << (2 * np.arange(16, dtype=np.uint32))).sum(1).astype(np.uint32)
    out = np.zeros((len(px), 8), np.uint8)
    out[:, 0], out[:, 1], out[:, 2], out[:, 3] = c0 & 255, c0 >> 8, c1 & 255, c1 >> 8
    out[:, 4], out[:, 5], out[:, 6], out[:, 7] = bits & 255, (bits >> 8) & 255, (bits >> 16) & 255, bits >> 24
    return out


def alpha_blocks(a):
    """(n, 16) alpha into (n, 8) bytes: two endpoints and a 3-bit choice per pixel, eight levels between them"""
    a = a.astype(np.float32)
    a0, a1 = a.max(1), a.min(1)
    w = np.arange(8, dtype=np.float32)
    levels = np.empty((len(a), 8), np.float32)
    levels[:, 0], levels[:, 1] = a0, a1
    for k in range(1, 7):
        levels[:, k + 1] = ((7 - k) * a0 + k * a1) / 7
    idx = np.argmin(np.abs(a[:, :, None] - levels[:, None, :]), axis=2).astype(np.uint64)
    idx[a0 == a1] = 0
    bits = (idx << (3 * np.arange(16, dtype=np.uint64))).sum(1)
    out = np.zeros((len(a), 8), np.uint8)
    out[:, 0], out[:, 1] = a0, a1
    for k in range(6):
        out[:, 2 + k] = (bits >> (8 * k)) & 255
    return out


def encode(rgba, want_alpha):
    """one level, (h, w, 4) uint8, to its bytes"""
    b, bw, bh = blocks_of(rgba)
    col = colour_blocks(b[:, :, :3])
    if not want_alpha:
        return col.tobytes()
    return np.concatenate([alpha_blocks(b[:, :, 3]), col], 1).tobytes()


def _ends7(e):
    """(n, 4) floats to the nearest BC7 mode 6 end: 7 bits a channel and one low bit they all share. The ints, and the low bit"""
    best, bestp, beste = None, None, None
    for low in (0, 1):
        q = np.clip(np.round((e - low) / 2), 0, 127).astype(np.int32)
        err = (((q * 2 + low) - e) ** 2).sum(1)
        if best is None:
            best, bestp, beste = q, np.full(len(e), low, np.int32), err
        else:
            t = err < beste
            best, bestp, beste = np.where(t[:, None], q, best), np.where(t, low, bestp), np.where(t, err, beste)
    return best, bestp


def _pal7(q0, p0, q1, p1):
    c0, c1 = q0 * 2 + p0[:, None], q1 * 2 + p1[:, None]
    return ((64 - W4[None, :, None]) * c0[:, None, :] + W4[None, :, None] * c1[:, None, :] + 32) >> 6


def _fit7(px, q0, p0, q1, p1):
    pal = _pal7(q0, p0, q1, p1).astype(np.float32)
    d = ((px[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(3)
    idx = d.argmin(2)
    return idx, np.take_along_axis(d, idx[:, :, None], 2)[:, :, 0].sum(1)


def bc7_blocks(px):
    """(n, 16, 4) uint8 to (n, 16) bytes, all in BC7's mode 6: one line through the block's colour and alpha, and 4 bits a pixel along it. Then one round of least squares, kept where it's better"""
    out = np.zeros((len(px), 2), np.uint64)
    for s0 in range(0, len(px), 4096):
        p = px[s0:s0 + 4096].astype(np.float32)
        m = p.mean(1, keepdims=True)
        d = p - m
        cov = np.einsum("nij,nik->njk", d, d)
        axis = (p.max(1) - p.min(1)) + 1e-3
        for _ in range(6):
            axis = np.einsum("njk,nk->nj", cov, axis)
            axis /= np.maximum(np.linalg.norm(axis, axis=1, keepdims=True), 1e-6)
        t = (d * axis[:, None, :]).sum(2)
        e0 = m[:, 0] + t.min(1)[:, None] * axis
        e1 = m[:, 0] + t.max(1)[:, None] * axis
        q0, p0 = _ends7(e0)
        q1, p1 = _ends7(e1)
        idx, err = _fit7(p, q0, p0, q1, p1)
        # least squares on the indices just found
        w = W4[idx].astype(np.float32) / 64
        a2, b2, ab = ((1 - w) ** 2).sum(1), (w ** 2).sum(1), ((1 - w) * w).sum(1)
        ax, bx = ((1 - w)[:, :, None] * p).sum(1), (w[:, :, None] * p).sum(1)
        det = a2 * b2 - ab * ab
        ok = det > 1e-6
        safe = np.where(ok, det, 1.0)[:, None]
        n0 = np.where(ok[:, None], (b2[:, None] * ax - ab[:, None] * bx) / safe, e0)
        n1 = np.where(ok[:, None], (a2[:, None] * bx - ab[:, None] * ax) / safe, e1)
        r0, s0p = _ends7(np.clip(n0, 0, 255))
        r1, s1p = _ends7(np.clip(n1, 0, 255))
        idx2, err2 = _fit7(p, r0, s0p, r1, s1p)
        take = err2 < err
        q0, q1 = np.where(take[:, None], r0, q0), np.where(take[:, None], r1, q1)
        p0, p1 = np.where(take, s0p, p0), np.where(take, s1p, p1)
        idx = np.where(take[:, None], idx2, idx)
        # the first pixel's index has only 3 bits, so its top bit must be 0: swap the ends where it isn't
        sw = idx[:, 0] >= 8
        q0, q1 = np.where(sw[:, None], q1, q0), np.where(sw[:, None], q0, q1)
        p0, p1 = np.where(sw, p1, p0), np.where(sw, p0, p1)
        idx = np.where(sw[:, None], 15 - idx, idx).astype(np.uint64)
        q0, q1, p0, p1 = q0.astype(np.uint64), q1.astype(np.uint64), p0.astype(np.uint64), p1.astype(np.uint64)
        one = np.uint64(1)
        lo = np.full(len(p), one << np.uint64(6), np.uint64)
        for k in range(4):
            lo |= q0[:, k] << np.uint64(7 + k * 14)
            lo |= q1[:, k] << np.uint64(14 + k * 14)
        lo |= p0 << np.uint64(63)
        hi = p1 | (idx[:, 0] << np.uint64(1))
        for k in range(1, 16):
            hi |= idx[:, k] << np.uint64(k * 4)
        out[s0:s0 + 4096, 0], out[s0:s0 + 4096, 1] = lo, hi
    return out


def encode7(rgba):
    """one level, (h, w, 4) uint8, to its BC7 bytes"""
    b, bw, bh = blocks_of(rgba)
    return bc7_blocks(b).astype("<u8").tobytes()


def mips(rgba):
    """every level from this one down to 1x1, each half the last by averaging"""
    out = [rgba]
    while out[-1].shape[0] > 1 or out[-1].shape[1] > 1:
        p = out[-1].astype(np.float32)
        if p.shape[0] > 1:
            p = (p[:p.shape[0] // 2 * 2:2] + p[1:p.shape[0] // 2 * 2:2]) / 2
        if p.shape[1] > 1:
            p = (p[:, :p.shape[1] // 2 * 2:2] + p[:, 1:p.shape[1] // 2 * 2:2]) / 2
        out.append(np.round(p).astype(np.uint8))
    return out


def decode(data, w, h, fmt):
    """the bytes of one level back to (h, w, 4) uint8, as the GPU reads them"""
    bw, bh = (w + 3) // 4, (h + 3) // 4
    if fmt == BC7:   # mode 6 is all the cook writes
        raw = np.frombuffer(data, "<u8", bw * bh * 2).reshape(-1, 2)
        lo, hi = raw[:, 0], raw[:, 1]
        m7 = np.uint64(127)
        p0, p1 = (lo >> np.uint64(63)).astype(np.int32), (hi & np.uint64(1)).astype(np.int32)
        q0 = np.stack([((lo >> np.uint64(7 + k * 14)) & m7).astype(np.int32) for k in range(4)], 1)
        q1 = np.stack([((lo >> np.uint64(14 + k * 14)) & m7).astype(np.int32) for k in range(4)], 1)
        idx = np.stack([((hi >> np.uint64(1)) & np.uint64(7)).astype(np.int64)] + [((hi >> np.uint64(k * 4)) & np.uint64(15)).astype(np.int64) for k in range(1, 16)], 1)
        out = np.take_along_axis(_pal7(q0, p0, q1, p1), idx[:, :, None].repeat(4, 2), 1)
        return out.astype(np.uint8).reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)[:h, :w]
    size = 8 if fmt == BC1 else 16
    raw = np.frombuffer(data, np.uint8, bw * bh * size).reshape(-1, size)
    col = raw[:, -8:]
    c0, c1 = col[:, 0].astype(np.uint32) | (col[:, 1].astype(np.uint32) << 8), col[:, 2].astype(np.uint32) | (col[:, 3].astype(np.uint32) << 8)
    p0, p1 = from565(c0), from565(c1)
    four = (c0 > c1) | (fmt == BC3)
    pal = np.stack([p0, p1, np.where(four[:, None], (2 * p0 + p1) / 3, (p0 + p1) / 2), np.where(four[:, None], (p0 + 2 * p1) / 3, 0)], 1)
    bits = col[:, 4].astype(np.uint32) | (col[:, 5].astype(np.uint32) << 8) | (col[:, 6].astype(np.uint32) << 16) | (col[:, 7].astype(np.uint32) << 24)
    idx = (bits[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3
    rgb = np.take_along_axis(pal, idx[:, :, None].astype(np.int64).repeat(3, 2), 1)
    if fmt == BC3:
        a0, a1 = raw[:, 0].astype(np.float32), raw[:, 1].astype(np.float32)
        levels = np.empty((len(raw), 8), np.float32)
        levels[:, 0], levels[:, 1] = a0, a1
        hi = a0 > a1
        for k in range(1, 7):
            levels[:, k + 1] = ((7 - k) * a0 + k * a1) / 7
        for k in range(1, 5):
            levels[:, k + 1] = np.where(hi, levels[:, k + 1], ((5 - k) * a0 + k * a1) / 5)
        levels[:, 6] = np.where(hi, levels[:, 6], 0)
        levels[:, 7] = np.where(hi, levels[:, 7], 255)
        ab = np.zeros(len(raw), np.uint64)
        for k in range(6):
            ab |= raw[:, 2 + k].astype(np.uint64) << (8 * k)
        ai = (ab[:, None] >> (3 * np.arange(16, dtype=np.uint64))) & 7
        alpha = np.take_along_axis(levels, ai.astype(np.int64), 1)
    else:
        alpha = np.where((~four[:, None]) & (idx == 3), 0, 255).astype(np.float32)
    out = np.concatenate([rgb, alpha[:, :, None]], 2)
    return np.clip(np.round(out), 0, 255).astype(np.uint8).reshape(bh, bw, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)[:h, :w]


def main():
    from PIL import Image
    im = np.array(Image.open(sys.argv[1]).convert("RGBA"))
    want = bool((im[:, :, 3] < 255).any())
    fmt = BC7 if "--bc7" in sys.argv else BC3 if want else BC1
    data = encode7(im) if fmt == BC7 else encode(im, want)
    back = decode(data, im.shape[1], im.shape[0], fmt)
    err = (im.astype(np.float32) - back) ** 2
    psnr = lambda e: 99.0 if e <= 0 else 10 * np.log10(255 * 255 / e)
    print("%dx%d %s: %d bytes from %d, colour %.1f dB, alpha %.1f dB" % (im.shape[1], im.shape[0], "BC%d" % fmt, len(data), im.size, psnr(err[:, :, :3].mean()), psnr(err[:, :, 3].mean())))
    if len(sys.argv) > 2 and sys.argv[2] != "--bc7":
        Image.fromarray(back).save(sys.argv[2])


if __name__ == "__main__":
    main()
