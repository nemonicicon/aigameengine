#!/usr/bin/env python3
"""artfile.py -- WRITES A SET'S DRAWINGS (reva_native/engine/setart.inc): the file a set's `art` line plays.

The engine draws what the file says and decides nothing: where every stroke, shape and picture is in every frame is worked out
here, by code, as a function of the song's time. The canvas is 1920 x 1080, top left
0,0, y DOWN. A thing is made once (a picture, a stroke, a group of strokes, a mesh, a plane in the world) and then placed in any
frame with six numbers, an affine (a b c d tx ty): canvas = (a*x + c*y + tx, b*x + d*y + ty).

    A = Art(fps=30, frames=300)
    board = A.plane(origin=(-1.6, 3.2, -2.0), across=(3.2 / 1600, 0, 0), down=(0, -2.0 / 1000, 0))   a layer in the world: 1600 x 1000 canvas units of it
    g = A.doodle(pen.strokes, palette)                      a drawing of strokes as a group (board units, y up: place it with board())
    for f in range(300): A.group(f, OVER, g, board(x, y, h), upto=f / 300)
    A.rect(f, UNDER, x, y, w, h, rgba); A.picture_at(f, OVER, pic, x, y, w, h, uv=(u0, v0, u1, v1), rgba=...)
    A.write("sets/show.art")                                and the set says:  art sets/show.art

LAYERS: OVER the finished picture; UNDER the world (behind every solid thing, in front of the sky); a plane's own number, in the
world. ⛔ Under and on a plane a picture gives its ALPHA only, coloured by the item's colour, and those layers pass through the
scene's exposure and post; OVER is exactly the colours given here.
    python artfile.py --selftest OUT.art            a small file with one of everything, and its numbers read back
    python artfile.py --frame FILE.art N OUT.png    frame N as a picture, by Pillow: a quick look while writing a show, NOT the engine's picture
"""
import math
import os
import struct
import sys

import numpy as np

OVER, UNDER = 0, 1
CW, CH = 1920.0, 1080.0
ITEM = 18


def aff(sx=1.0, sy=None, turn=0.0, tx=0.0, ty=0.0):
    """scale, then turn (radians, clockwise on a y-down canvas), then move"""
    sy = sx if sy is None else sy; c, s = math.cos(turn), math.sin(turn)
    return (c * sx, s * sx, -s * sy, c * sy, tx, ty)


def box(x, y, w, h, turn=0.0):
    """the six numbers that put a quad's own square (0..1) on the rectangle at x, y, w wide and h high, turned about its middle"""
    if not turn: return (w, 0.0, 0.0, h, x, y)
    c, s = math.cos(turn), math.sin(turn); cx, cy = x + w / 2, y + h / 2
    a, b, cc, d = c * w, s * w, -s * h, c * h
    return (a, b, cc, d, cx - (a + cc) / 2, cy - (b + d) / 2)


def board(x, y, h, turn=0.0):
    """for a doodle in board units (y up, 1.0 = its height): its bottom left at canvas x, y, and `h` canvas units tall"""
    c, s = math.cos(turn), math.sin(turn)
    return (c * h, s * h, s * h, -c * h, x, y)


def then(m, n):
    """the placing `m`, then `n` on top of it"""
    a, b, c, d, tx, ty = m; A, B, C, D, TX, TY = n
    return (A * a + C * b, B * a + D * b, A * c + C * d, B * c + D * d, A * tx + C * ty + TX, B * tx + D * ty + TY)


class Art:
    def __init__(self, fps=30.0, frames=1):
        self.fps = float(fps); self.frames = [[] for _ in range(int(frames))]
        self.pictures = []; self.strokes = []; self.meshes = []; self.groups = []; self.planes = []

    # ---- what a file holds, made once
    def picture(self, path):
        """an image file (a path beside the art file, or absolute). Returns its number, from 1"""
        if path in self.pictures: return self.pictures.index(path) + 1
        self.pictures.append(path); return len(self.pictures)

    def stroke(self, pts):
        p = np.asarray(pts, np.float32).reshape(-1, 2)
        if len(p) < 1: raise ValueError("a stroke has at least one point")
        self.strokes.append(p); return len(self.strokes) - 1

    def mesh(self, tris):
        p = np.asarray(tris, np.float32).reshape(-1, 2)
        if len(p) < 3 or len(p) % 3: raise ValueError("a mesh is whole triangles")
        self.meshes.append(p); return len(self.meshes) - 1

    def group_of(self, parts):
        """parts: [(stroke number, width, (r, g, b, a))] in the order a hand would draw them. A width here is in the strokes' own units:
        the engine scales it as the group is scaled when it's placed, so a drawing grows whole"""
        if not parts: raise ValueError("a group has at least one stroke")
        self.groups.append([(int(s), float(w), tuple(float(v) for v in c)) for s, w, c in parts]); return len(self.groups) - 1

    def doodle(self, strokes, palette, width=1.0):
        """a drawing of strokes as a group: its points as they are (board units), widths times `width`"""
        parts = []
        for s in strokes:
            c = tuple(float(v) for v in (palette[s["col"]] if isinstance(s["col"], str) else s["col"]))
            if max(c[:3]) > 1.0: c = tuple(v / 255.0 for v in c)          # a palette of 0..255
            if len(c) == 3: c += (1.0,)
            parts.append((self.stroke(s["pts"]), s["w"] * width, c))
        return self.group_of(parts)

    def plane(self, origin, across, down):
        """a place in the world: where canvas 0,0 is, and the world's step for one canvas unit across and one down. Returns its LAYER"""
        self.planes.append(tuple(float(v) for v in (*origin, *across, *down)))
        if len(self.planes) > 32: raise ValueError("a file has 32 planes")
        return 1 + len(self.planes)

    # ---- a frame's items, in the order they're drawn on their layer
    def _put(self, f, kind, layer, ident, m, p, rgba, p4=0.0):
        if 0 <= f < len(self.frames):
            r = tuple(rgba) + ((1.0,) if len(rgba) == 3 else ())
            self.frames[f].append((float(kind), float(layer), float(ident), *[float(v) for v in m], *[float(v) for v in p], *[float(v) for v in r], float(p4)))

    def quad(self, f, layer, m, rgba, pic=0, uv=(0.0, 0.0, 1.0, 1.0), soft=0.0): self._put(f, 1, layer, pic, m, uv, rgba, soft)
    def rect(self, f, layer, x, y, w, h, rgba, turn=0.0, soft=0.0): self.quad(f, layer, box(x, y, w, h, turn), rgba, soft=soft)
    def picture_at(self, f, layer, pic, x, y, w, h, uv=(0.0, 0.0, 1.0, 1.0), rgba=(1, 1, 1, 1), turn=0.0): self.quad(f, layer, box(x, y, w, h, turn), rgba, pic, uv)
    def stroke_at(self, f, layer, sid, m, width, rgba, frm=0.0, upto=1.0, soft=0.0): self._put(f, 2, layer, sid, m, (width, frm, upto, soft), rgba)
    def mesh_at(self, f, layer, mid, m, rgba): self._put(f, 3, layer, mid, m, (0, 0, 0, 0), rgba)
    def group(self, f, layer, gid, m, rgba=(1, 1, 1, 1), frm=0.0, upto=1.0, width=1.0, soft=0.0): self._put(f, 4, layer, gid, m, (width, frm, upto, soft), rgba)

    def group_length(self, gid): return sum(float(np.hypot(*np.diff(self.strokes[s], axis=0).T).sum()) for s, _, _ in self.groups[gid])

    def write(self, path):
        with open(path, "wb") as f:
            f.write(("ART1 %g %d %d %d %d %d %d\n" % (self.fps, len(self.frames), len(self.pictures), len(self.strokes), len(self.meshes), len(self.groups), len(self.planes))).encode())
            for p in self.pictures: f.write((p + "\n").encode("utf-8"))
            for s in self.strokes: f.write(struct.pack("<i", len(s))); f.write(s.astype("<f4").tobytes())
            for m in self.meshes: f.write(struct.pack("<i", len(m))); f.write(m.astype("<f4").tobytes())
            for g in self.groups:
                f.write(struct.pack("<i", len(g)))
                for sid, w, c in g: f.write(struct.pack("<i5f", sid, w, *c))
            for p in self.planes: f.write(struct.pack("<9f", *p))
            n = 0
            for fr in self.frames:
                f.write(struct.pack("<i", len(fr)))
                if fr: f.write(np.asarray(fr, "<f4").tobytes()); n += len(fr)
        return n


def read(path):
    """an art file back as numbers, for checking a writer: (header dict, frames as arrays of 18 floats)"""
    with open(path, "rb") as f:
        hd = f.readline().decode().split(); assert hd[0] == "ART1", hd
        fps = float(hd[1]); fr, nt, ns, nm, ng, npl = (int(v) for v in hd[2:8])
        pics = [f.readline().decode("utf-8").rstrip("\r\n") for _ in range(nt)]
        I = lambda: struct.unpack("<i", f.read(4))[0]
        strokes = [np.frombuffer(f.read(I() * 8), "<f4").reshape(-1, 2) for _ in range(ns)]
        meshes = [np.frombuffer(f.read(I() * 8), "<f4").reshape(-1, 2) for _ in range(nm)]
        groups = [[struct.unpack("<i5f", f.read(24)) for _ in range(I())] for _ in range(ng)]
        planes = [struct.unpack("<9f", f.read(36)) for _ in range(npl)]
        frames = [np.frombuffer(f.read(I() * ITEM * 4), "<f4").reshape(-1, ITEM) for _ in range(fr)]
    return dict(fps=fps, pictures=pics, strokes=strokes, meshes=meshes, groups=groups, planes=planes), frames


def preview(path, frame, out=None, size=(960, 540), ss=2, planes=None):
    """one frame of an art file as a picture, with Pillow: a QUICK LOOK while a show is being written, not the engine's picture (no world: a plane's
    items are drawn where `planes[layer]`, six numbers, puts that plane's canvas on the screen's, or left out). Returns the image"""
    from PIL import Image, ImageDraw
    hd, frames = read(path); here = os.path.dirname(os.path.abspath(path)); k = size[0] * ss / CW
    im = Image.new("RGBA", (size[0] * ss, size[1] * ss), (0, 0, 0, 255)); pics = {}
    def pic(i):
        if i not in pics:
            try: pics[i] = Image.open(os.path.join(here, hd["pictures"][i - 1])).convert("RGBA")
            except Exception: pics[i] = None
        return pics[i]
    def xy(m, x, y): return ((m[0] * x + m[2] * y + m[4]) * k, (m[1] * x + m[3] * y + m[5]) * k)
    def rgba(c): return tuple(int(max(0.0, min(1.0, v)) * 255 + 0.5) for v in c)
    def ribbon(dr, pts, m, w, s0, s1, c):
        p = np.asarray(pts, float); d = np.hypot(*np.diff(p, axis=0).T) if len(p) > 1 else np.zeros(0); cum = np.concatenate([[0.0], np.cumsum(d)])
        if s1 <= s0 and len(p) > 1: return
        q = []
        for i in range(len(p)):
            if cum[i] < s0 and (i + 1 >= len(p) or cum[i + 1] <= s0): continue
            if cum[i] > s1: break
            q.append(p[i])
            if i + 1 < len(p) and cum[i] < s1 < cum[i + 1]: q.append(p[i] + (p[i + 1] - p[i]) * (s1 - cum[i]) / (cum[i + 1] - cum[i]))
        if not q: return
        Q = [xy(m, x, y) for x, y in q]; wd = max(1, int(round(w * k)))
        if len(Q) > 1: dr.line(Q, fill=rgba(c), width=wd, joint="curve")
        for e in (Q[0], Q[-1]): dr.ellipse([e[0] - wd / 2, e[1] - wd / 2, e[0] + wd / 2, e[1] + wd / 2], fill=rgba(c))
    items = frames[frame]
    for want in ("under", "plane", "over"):
        for it in items:
            kind, layer, ident = int(it[0] + 0.5), int(it[1] + 0.5), int(it[2] + 0.5); m = tuple(it[3:9]); pr = it[9:13]; c = it[13:17]
            if (want == "under") != (layer == 1) or (want == "over") != (layer == 0): continue
            if layer >= 2:
                if not planes or layer not in planes: continue
                m = then(m, planes[layer])
            lay = Image.new("RGBA", im.size, (0, 0, 0, 0)); dr = ImageDraw.Draw(lay)
            if kind == 1:
                P = [xy(m, 0, 0), xy(m, 1, 0), xy(m, 1, 1), xy(m, 0, 1)]; src = pic(ident) if ident >= 1 else None
                if src is None: dr.polygon(P, fill=rgba(c))
                else:
                    W, H = src.size; box = (int(pr[0] * W), int(pr[1] * H), int(pr[2] * W), int(pr[3] * H)); cut = src.crop(box)
                    w = max(1, int(round(math.hypot(P[1][0] - P[0][0], P[1][1] - P[0][1])))); h = max(1, int(round(math.hypot(P[3][0] - P[0][0], P[3][1] - P[0][1]))))
                    cut = cut.resize((w, h), Image.LANCZOS); r, g, b2, al = cut.split(); tint = rgba(c)
                    cut = Image.merge("RGBA", (r.point(lambda v: v * tint[0] // 255), g.point(lambda v: v * tint[1] // 255), b2.point(lambda v: v * tint[2] // 255), al.point(lambda v: v * tint[3] // 255)))
                    ang = -math.degrees(math.atan2(P[1][1] - P[0][1], P[1][0] - P[0][0]))
                    if abs(ang) > 0.05: cut = cut.rotate(ang, expand=True, resample=Image.BICUBIC)
                    cxp = sum(q[0] for q in P) / 4.0; cyp = sum(q[1] for q in P) / 4.0
                    lay.alpha_composite(cut, (int(round(cxp - cut.size[0] / 2)), int(round(cyp - cut.size[1] / 2)))) if 0 <= cxp < im.size[0] * 2 else None
            elif kind == 2:
                S = hd["strokes"][ident]; L = float(np.hypot(*np.diff(S, axis=0).T).sum()) if len(S) > 1 else 0.0; ribbon(dr, S, m, pr[0], pr[1] * L, pr[2] * L, c)
            elif kind == 3:
                M = hd["meshes"][ident]
                for t in range(0, len(M), 3): dr.polygon([xy(m, *M[t + j]) for j in range(3)], fill=rgba(c))
            elif kind == 4:
                G = hd["groups"][ident]; sc = math.sqrt(abs(m[0] * m[3] - m[1] * m[2])) * pr[0]; at = 0.0
                Ls = [float(np.hypot(*np.diff(hd["strokes"][g[0]], axis=0).T).sum()) if len(hd["strokes"][g[0]]) > 1 else 0.0 for g in G]; tot = sum(Ls); g0, g1 = pr[1] * tot, pr[2] * tot
                for g, L in zip(G, Ls):
                    if not (at + L < g0 or (at > g1 and L > 0) or (at >= g1 and g1 < tot)):
                        ribbon(dr, hd["strokes"][g[0]], m, g[1] * sc, g0 - at, g1 - at if L > 0 else 1.0, (g[2] * c[0], g[3] * c[1], g[4] * c[2], g[5] * c[3]))
                    at += L
            im = Image.alpha_composite(im, lay)
    im = im.convert("RGB").resize(size, Image.LANCZOS)
    if out: im.save(out)
    return im


def _selftest(out):
    # four strokes made here, as a pen hands them over (points in board units, a width, a colour by name): a ring, a line and two dots. It needs no pen of anyone's
    ink = dict(ink=(28, 28, 32), red=(214, 64, 58))
    ring = np.array([(0.5 + 0.3 * math.cos(t), 0.5 + 0.3 * math.sin(t)) for t in np.linspace(0.0, 2.0 * math.pi, 33)], dtype=np.float32)
    two = lambda a, b: np.array([a, b], dtype=np.float32)
    strokes = [dict(pts=ring, w=0.012, col="ink"), dict(pts=two((0.2, 0.1), (0.8, 0.1)), w=0.012, col="red"),
               dict(pts=two((0.4, 0.6), (0.401, 0.6)), w=0.024, col="ink"), dict(pts=two((0.6, 0.6), (0.601, 0.6)), w=0.024, col="ink")]
    A = Art(30.0, 60)
    g = A.doodle(strokes, ink); pl = A.plane((0, 2, 0), (0.001, 0, 0), (0, -0.001, 0)); tri = A.mesh([(0, 0), (100, 0), (50, 80)])
    for f in range(60):
        A.rect(f, UNDER, 0, 0, CW, CH, (0.87, 0.87, 0.84, 1)); A.rect(f, UNDER, 300, 150, 900, 600, (0.17, 0.47, 0.8, 1))
        A.group(f, OVER, g, board(700, 800, 500), upto=min(1.0, f / 45.0)); A.group(f, pl, g, board(0, 1000, 1000)); A.mesh_at(f, OVER, tri, aff(1, tx=100, ty=100), (0.84, 0.25, 0.23, 1))
    n = A.write(out); hd, fr = read(out)
    assert len(fr) == 60 and all(len(x) == 5 for x in fr) and len(hd["strokes"]) == 4 and hd["groups"][0][1][2:5] == tuple(np.float32(v / 255.0) for v in ink["red"])
    print("%s: %d frames, %d items, %d strokes, 1 group %.2f board heights long; read back the same" % (out, len(fr), n, len(hd["strokes"]), A.group_length(g)))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--selftest": _selftest(sys.argv[2])
    elif len(sys.argv) >= 5 and sys.argv[1] == "--frame": preview(sys.argv[2], int(sys.argv[3]), sys.argv[4])
    else: print(__doc__)
