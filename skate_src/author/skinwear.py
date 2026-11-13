#!/usr/bin/env python3
"""skinwear.py -- tight clothes cut out of a body's own skin.

Underwear, stockings, a glove, a choker, a strap: clothes that lie on the body. Made here from the body's own surface, so they fit because they ARE its surface,
lifted a couple of millimetres, and they move as it moves because they ride the same bones by the same amounts. No fitting, no bake, any body that's a mesh.

    python skate_src/author/skinwear.py PACK --piece briefs [--name briefs] [--slot under] [--outfits under] [--colour 15151a] [--thick 1.8]
                                        [--skin "mesh@N,mesh"] [--keep-skin]
    python skate_src/author/skinwear.py --pieces                  what it can cut, and each one's numbers

What it does:
  1. A piece is a region of the body, said as a number at every point of the skin: over 0 inside, under 0 outside, 0 on its edge, and inside it's how far
     in from the edge, in metres. A piece's shape comes from the body's own measurements (bodycheck.py) and bones, so the same piece fits a different body.
  2. The skin's triangles are cut along the edge, not chosen whole: a triangle the edge crosses is split there, so a hem is a clean line, not a staircase.
  3. The cut-out is lifted off the skin by the cloth's thickness, and a rim is turned down from its edge into the skin, so no gap shows at the hem.
  4. Its texture is one strip: across it, how far in from the hem. So a piece's colours are a short list, "this colour at the hem, that one 3 cm in": a
     stocking's dark welt, a waistband.
  5. The skin under it becomes a mesh of its own, `skin~h=<piece>`, which the engine doesn't draw while the piece is worn: skin can't show through cloth.
     It stops a few millimetres inside the hem, so there's always skin under the edge. A strap is too narrow for that, and its skin stays.
  6. The piece goes in the pack as `wear~<slot>~<piece>~<outfits>`: the engine's wardrobe puts it on and takes it off (`outfit=`, `wear=+piece,-piece`).

A body is cut at rest. Limbs are found by their bones, so it doesn't matter whether it rests with its arms out or down.

⛔ It makes what lies ON the body. Anything that hangs (a skirt, a coat, a loose shirt) isn't the body's shape: that's the atelier's, or dresspack.py's.
⛔ The cut is along a level of a number given at the vertices, so an edge is as smooth as that number is: a region is written with smooth steps, never if/else
   across a surface. (An if on which leg or which arm is fine: nothing joins them.)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "reva_native", "anim_bench"))
import numpy as np

from packbody import PackBody, weld

SPAN = 0.25          # a piece's texture runs from its hem to this far in, metres


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------- cutting a surface along a level
def clip(A, f):
    """the part of a surface where f is over 0, its triangles split along f = 0. A: (pos, nrm, uv, nodes, weights, tris). Returns the same six, compacted,
    or None when nothing's inside"""
    pos, nrm, uv, nodes, w, T = A
    inside = f > 0
    k = inside[T].sum(1)
    if not (k > 0).any():
        return None
    n0 = len(pos)
    tris = [T[k == 3]]
    ea, eb, cache = [], [], {}

    def cut(a, b):                                   # the point on the edge from a (inside) to b (outside) where f is 0
        key = (int(a), int(b))
        if key not in cache:
            cache[key] = n0 + len(ea)
            ea.append(key[0])
            eb.append(key[1])
        return cache[key]
    extra = []
    for tri in T[(k == 1) | (k == 2)]:
        ins = inside[tri]
        if ins.sum() == 1:
            i = int(np.argmax(ins))
            a, b, c = tri[i], tri[(i + 1) % 3], tri[(i + 2) % 3]
            extra.append((a, cut(a, b), cut(a, c)))
        else:
            i = int(np.argmin(ins))
            c, a, b = tri[i], tri[(i + 1) % 3], tri[(i + 2) % 3]
            ca, cb = cut(a, c), cut(b, c)
            extra.append((a, b, cb))
            extra.append((a, cb, ca))
    if extra:
        tris.append(np.array(extra, dtype=np.int64))
    T2 = np.concatenate(tris)
    if ea:
        a, b = np.array(ea), np.array(eb)
        t = (f[a] / (f[a] - f[b]))[:, None]
        p2 = pos[a] + t * (pos[b] - pos[a])
        n2 = nrm[a] + t * (nrm[b] - nrm[a])
        n2 /= np.maximum(np.linalg.norm(n2, axis=1, keepdims=True), 1e-12)
        u2 = uv[a] + t * (uv[b] - uv[a])
        # the bones a new vertex rides: both ends' bones, each by its share, the four heaviest kept
        cn = np.concatenate([nodes[a], nodes[b]], axis=1)
        cw = np.concatenate([(1.0 - t) * w[a], t * w[b]], axis=1)
        for i in range(8):
            for j in range(i):
                same = (cn[:, i] == cn[:, j]) & (cw[:, i] > 0)
                cw[:, j] += np.where(same, cw[:, i], 0.0)
                cw[:, i] = np.where(same, 0.0, cw[:, i])
        top = np.argsort(-cw, axis=1)[:, :4]
        r = np.arange(len(a))[:, None]
        nd2, w2 = cn[r, top], cw[r, top]
        w2 /= np.maximum(w2.sum(1, keepdims=True), 1e-12)
        pos, nrm, uv, nodes, w = np.concatenate([pos, p2]), np.concatenate([nrm, n2]), np.concatenate([uv, u2]), np.concatenate([nodes, nd2]), np.concatenate([w, w2])
    used = np.unique(T2)
    to = np.full(len(pos), -1, dtype=np.int64)
    to[used] = np.arange(len(used))
    return pos[used], nrm[used], uv[used], nodes[used], w[used], to[T2]


def join(parts):
    """several surfaces as one"""
    parts = [p for p in parts if p is not None]
    if not parts:
        return None
    base, T = 0, []
    for p in parts:
        T.append(p[5] + base)
        base += len(p[0])
    return tuple(np.concatenate([p[i] for p in parts]) for i in range(5)) + (np.concatenate(T),)


def shell(A, thick, sink):
    """a cut-out of skin as a garment: lifted off the skin by its thickness, with a rim turned down from its edge to just under the skin so no gap shows"""
    pos, nrm, uv, nodes, w, T = A
    ids = weld(pos)
    e = np.concatenate([T[:, [0, 1]], T[:, [1, 2]], T[:, [2, 0]]])                  # every triangle's edges, the way it's wound
    key = np.sort(ids[e], axis=1)
    _u, inv, cnt = np.unique(key, axis=0, return_inverse=True, return_counts=True)
    edge = e[cnt[inv.reshape(-1)] == 1]                                             # the ones no other triangle shares: the garment's edge
    top = pos + nrm * thick
    if not len(edge):
        return top, nrm, uv, nodes, w, T
    out = np.cross(pos[edge[:, 1]] - pos[edge[:, 0]], 0.5 * (nrm[edge[:, 0]] + nrm[edge[:, 1]]))   # which way is away from the garment, at each edge
    out /= np.maximum(np.linalg.norm(out, axis=1, keepdims=True), 1e-12)
    acc = np.zeros((ids.max() + 1, 3))
    np.add.at(acc, ids[edge[:, 0]], out)
    np.add.at(acc, ids[edge[:, 1]], out)
    rv = np.unique(edge)                                                             # the vertices on the edge, each made twice more: the rim's top and its foot
    rn = acc[ids[rv]]
    rn /= np.maximum(np.linalg.norm(rn, axis=1, keepdims=True), 1e-12)
    at = np.full(len(pos), -1, dtype=np.int64)
    at[rv] = np.arange(len(rv))
    n0, m = len(pos), len(rv)
    a, b = edge[:, 0], edge[:, 1]
    ta, tb, fa, fb = n0 + at[a], n0 + at[b], n0 + m + at[a], n0 + m + at[b]
    rim = np.concatenate([np.stack([ta, fa, fb], axis=1), np.stack([ta, fb, tb], axis=1)])
    return (np.concatenate([top, top[rv], pos[rv] - nrm[rv] * sink]), np.concatenate([nrm, rn, rn]), np.concatenate([uv, uv[rv], uv[rv]]),
            np.concatenate([nodes, nodes[rv], nodes[rv]]), np.concatenate([w, w[rv], w[rv]]), np.concatenate([T, rim]))


# ---------------------------------------------------------------- a body, as a piece's shape needs it
class Fit:
    """what a piece's shape is written against: the body's measurements, its bones' places at rest, and for the mesh in hand its vertices and what they ride"""

    def __init__(self, B, M):
        self.B, self.M = B, M
        self.k = M["stature"] / 1.787              # every length below is on a body 1.787 m tall
        self.yc = M["floor"] + M["crotch"]
        self.fwd = M["faces"]
        self.P = None
        self._m = None

    def at(self, m, P):
        self._m, self.P = m, P
        return self

    def bone(self, *names):
        return self.B.node(*names)

    def side_bone(self, stem, sg):
        """a bone of her left (sg +1) or right (-1) side, by the usual ways of naming one"""
        s = "l" if sg > 0 else "r"
        S = "Left" if sg > 0 else "Right"
        return self.B.node("%s_%s" % (stem, s), S + stem[:1].upper() + stem[1:], "mixamorig:" + S + stem[:1].upper() + stem[1:])

    def rides(self, pred):
        """how much of each vertex of the mesh in hand rides the bones whose (lower-case) names pred() says yes to"""
        return self.B.weight_on(self._m, lambda n: pred(n.lower()))

    def along(self, a, b):
        """each vertex's place along the line from point a to point b: 0 at a, 1 at b, and its distance from that line"""
        d = b - a
        L = float(np.linalg.norm(d))
        t = (self.P - a) @ (d / L) / L
        r = np.linalg.norm(self.P - a - np.outer(t * L, d / L), axis=1)
        return t, r, L


# ---------------------------------------------------------------- pieces: where each one is on a body
def briefs(F, rise_front=0.075, rise_side=0.150, rise_back=0.100, strap=0.012, gusset_front=0.024, gusset_back=0.040, cut_front=0.65, cut_back=1.25):
    """briefs, cut high on the leg and low at the front, narrow over the hips, as R.E.V.A.'s sheets have them. The waistband dips at the front and the back
    and rises over the hips; each leg's opening runs from the gusset out and up to the side, quickly at the front (along the crease of the groin) and later
    at the back (under the seat). strap: how tall the side is, where front meets back"""
    k, yc, M = F.k, F.yc, F.M
    xs = 0.5 * M["hip width"] * 0.97
    x, y = np.abs(F.P[:, 0]), F.P[:, 1]
    back = smoothstep(-0.02 * k, 0.02 * k, (M["hip middle"] - F.P[:, 2]) * F.fwd)   # 0 on her front, 1 on her back, turning over at her sides
    side = np.clip(x / xs, 0.0, 1.0) ** 2
    top = yc + k * ((rise_front + (rise_side - rise_front) * side) * (1.0 - back) + (rise_back + (rise_side - rise_back) * side) * back)
    g = k * (gusset_front * (1.0 - back) + gusset_back * back)
    u = np.clip((x - g) / np.maximum(xs - g, 1e-6), 0.0, 1.0)
    leg = yc - 0.006 * k + k * (rise_side - strap + 0.006) * u ** (cut_front * (1.0 - back) + cut_back * back)
    return np.minimum(top - y, y - leg)


def stockings(F, top=0.085, rise=0.018):
    """thigh-high stockings, feet and all. top: how far under the crotch their edge is. rise: how much higher the edge is on the outside of the thigh"""
    k = F.k
    hip = F.side_bone("thigh", 1)
    out = np.clip((np.abs(F.P[:, 0]) - abs(hip[0])) / (0.07 * k), -1.0, 1.0)
    return (F.yc - k * top + k * rise * out) - F.P[:, 1]


def thigh_band(F, side=1, at=0.045, wide=0.022):
    """a band round one thigh (side: 1 her left, -1 her right), `at` under the crotch"""
    k = F.k
    f = 0.5 * k * wide - np.abs(F.P[:, 1] - (F.yc - k * at))
    return np.where(F.P[:, 0] * side > 0, f, -1.0)


def garter(F, side=1, turn=38.0, top=0.125, foot=0.085, wide=0.013):
    """a strap down the front of one thigh, from the briefs' side to the stocking's edge. turn: how far round toward the outside it runs, in degrees from
    straight ahead. top: how far above the crotch it starts; foot: how far below it ends"""
    k = F.k
    hip = F.side_bone("thigh", side)
    a = np.radians(turn)
    ray = np.array([np.sin(a) * side, 0.0, np.cos(a) * F.fwd])                  # the way out from the leg's own line that the strap lies along
    acr = np.array([np.cos(a) * side, 0.0, -np.sin(a) * F.fwd])                 # and across it
    d = F.P - np.array([hip[0], 0.0, hip[2]])
    f = 0.5 * k * wide - np.abs(d @ acr)
    f = np.minimum(f, np.minimum((F.yc + k * top) - F.P[:, 1], F.P[:, 1] - (F.yc - k * foot)))
    return np.where((d @ ray > 0) & (F.P[:, 0] * side > 0), f, -1.0)


def choker(F, at=0.50, wide=0.026):
    """a band round the neck. at: where along the neck, 0 its foot and 1 the head's joint"""
    a, b = F.bone("neck_01", "Neck", "mixamorig:Neck"), F.bone("head", "Head", "mixamorig:Head")
    t, r, L = F.along(a, b)
    f = 0.5 * F.k * wide - np.abs(t - at) * L
    return np.where(r < 0.085 * F.k, f, -1.0)                                    # the neck, and not a shoulder that happens to be level with it


def arm_band(F, side=1, at=0.40, wide=0.024):
    """a band round one upper arm. at: where along it, 0 the shoulder and 1 the elbow"""
    a, b = F.side_bone("upperarm", side), F.side_bone("lowerarm", side)
    t, r, L = F.along(a, b)
    f = 0.5 * F.k * wide - np.abs(t - at) * L
    return np.where(r < 0.10 * F.k, f, -1.0)


def glove(F, side=-1, start=0.45, fingers=False):
    """a long glove on one arm, from `start` along the forearm (0 the elbow, 1 the wrist) to the knuckles: fingerless unless fingers"""
    a, b = F.side_bone("lowerarm", side), F.side_bone("hand", side)
    t, r, L = F.along(a, b)
    f = (t - start) * L
    f = np.where(r < 0.12 * F.k + np.maximum(t - 1.0, 0.0) * L, f, -1.0)       # this arm (past the wrist the hand spreads wider than the forearm)
    if not fingers:
        tip = F.rides(lambda n: any(n.startswith(w) for w in ("thumb_02", "thumb_03", "index_02", "index_03", "middle_02", "middle_03", "ring_02", "ring_03", "pinky_02", "pinky_03")))
        first = F.rides(lambda n: any(n.startswith(w) for w in ("thumb_01", "index_01", "middle_01", "ring_01", "pinky_01")))
        f = np.minimum(f, 0.03 * F.k * (0.5 - tip - 0.55 * first) * 2.0)         # it ends across the fingers' first joints
    return f


def crop_top(F, hem=0.030, scoop=0.100, strap=0.030, back=0.055):
    """a tank top cut off under the bust. hem: how far under the under-bust line it ends. scoop: how far below the foot of the neck its neckline dips
    at the front (back: the same behind). strap: how wide each shoulder strap is"""
    k, M = F.k, F.M
    neck, sh = F.bone("neck_01", "Neck", "mixamorig:Neck"), F.side_bone("upperarm", 1)
    x, y = np.abs(F.P[:, 0]), F.P[:, 1]
    behind = smoothstep(-0.02 * k, 0.02 * k, (M["hip middle"] - F.P[:, 2]) * F.fwd)
    rx = abs(sh[0]) - k * 0.046 - k * strap                         # the neck's opening reaches out to where the strap begins
    ry = k * (scoop * (1.0 - behind) + back * behind)
    d_neck = (np.sqrt((x / rx) ** 2 + (np.minimum(y - neck[1], 0.0) / ry) ** 2 + (np.maximum(y - neck[1], 0.0) / (0.03 * k)) ** 2) - 1.0) * rx
    ax, ay = abs(sh[0]) + 0.012 * k, sh[1] - 0.018 * k              # the armhole: round the shoulder's joint, deep under the arm
    d_arm = (np.sqrt(((x - ax) / (0.058 * k)) ** 2 + ((y - ay) / (0.085 * k)) ** 2) - 1.0) * 0.058 * k
    low = y - (M["floor"] + M["underbust height"] - k * hem)
    high = (sh[1] + 0.050 * k) - y                                   # ⛔ and it has a top: without one, everything above its hem was inside it, her head too
    return np.minimum(np.minimum(np.minimum(low, high), d_neck), np.minimum(d_arm, ax - x))


def hip_band(F, at=0.130, wide=0.040):
    """a band round the hips, `at` above the crotch: a belt"""
    f = 0.5 * F.k * wide - np.abs(F.P[:, 1] - (F.yc + F.k * at))
    return np.where(np.abs(F.P[:, 0]) < 0.30 * F.k, f, -1.0)


def boots(F, top=0.20):
    """boots: everything of both legs from `top` under the knee down, feet and all"""
    knee = F.side_bone("calf", 1)
    return (knee[1] - F.k * top) - F.P[:, 1]


def leg_band(F, under=0.25, wide=0.018):
    """a band round each leg, `under` below the knee: a boot's strap"""
    knee = F.side_bone("calf", 1)
    return 0.5 * F.k * wide - np.abs(F.P[:, 1] - (knee[1] - F.k * under))


PIECES = {"crop_top": crop_top, "hip_band": hip_band, "boots": boots, "leg_band": leg_band, "briefs": briefs, "stockings": stockings, "thigh_band": thigh_band, "garter": garter, "choker": choker, "arm_band": arm_band, "glove": glove}
BLACK = [(0.0, (12, 11, 16)), (0.004, (20, 18, 26))]


def strip(stops):
    """a piece's texture: one strip, 4 across and 256 down, the colour at each distance in from the hem. stops: [(metres in, (r, g, b))], nearest the hem first"""
    from PIL import Image
    d = np.linspace(0.0, SPAN, 256)
    xs = [s[0] for s in stops]
    rgb = np.stack([np.interp(d, xs, [s[1][c] for s in stops]) for c in range(3)], axis=1)
    return Image.fromarray(np.repeat(rgb[:, None, :], 4, axis=1).round().astype(np.uint8), "RGB")


# ---------------------------------------------------------------- putting a piece on a pack
def wear(B, skin, piece, name=None, slot="under", outfits="under", colours=None, thick=0.0018, sink=0.0012, margin=0.004, hide=True, rename=None, say=print, whole=None, give=False, **shape):
    """cut a piece out of the skin meshes `skin` of PackBody B and add it as a wardrobe piece. colours: its strip's stops (see strip()). hide: the skin
    under it becomes `skin~h=<name>`. rename: {mesh index: new name} for the skin that's left, where a body's parts all carry one name and a tool after
    this one has to tell them apart. whole: every skin mesh of the body, when `skin` is only the ones still to be cut (the measurements want all of it).
    Returns the piece's mesh name (with give, that and the piece's own surface, for a tool that stands something on it). B.write() is the caller's"""
    import bodycheck as BC
    name = name or piece
    every = whole or skin
    M = BC.measure(B, every)
    S = BC.Skin(B, every)
    M["hip middle"] = float(np.mean([p[1] for p in BC.hull(S.cut(M["floor"] + M["hip height"], parts=(0, 2)))]))
    F = Fit(B, M)
    parts, cut_from = [], []
    for m in skin:
        A = B.arrays(m)
        f = PIECES[piece](F.at(m, A[0]), **shape)
        if not (f > 0).any():
            continue
        parts.append(clip((A[0], A[1], np.stack([f, f], axis=1), A[3], A[4], A[5]), f))   # how far in from the hem rides along in the UVs' place
        cut_from.append((m, A, f))
    G = join(parts)
    assert G is not None, "no skin of this body is inside a %s: its measurements don't fit the piece's shape" % piece
    G = shell((G[0], G[1], np.stack([np.full(len(G[0]), 0.5), np.clip(G[2][:, 0] / SPAN, 0.0, 1.0)], axis=1), G[3], G[4], G[5]), thick, sink)
    tex = B.add_texture(strip(colours or BLACK))
    mesh_name = "wear~%s~%s~%s" % (slot, name, outfits)
    B.add_mesh(mesh_name, skin[0], *G, tex=tex)
    under = 0
    if hide:
        for m, A, f in cut_from:
            if B.has_morphs(m):                                      # a mesh with face shapes on it stays whole: its shapes name its vertices by number
                continue
            covered, rest = clip(A, f - margin), clip(A, margin - f)
            if covered is None or rest is None:
                continue
            B.drop([m])
            B.add_mesh((rename or {}).get(m["mi"], m["name"]), m, *rest)
            B.add_mesh("skin~h=%s" % name, m, *covered)
            under += len(covered[5])
    area = 0.5 * np.linalg.norm(np.cross(G[0][G[5][:, 1]] - G[0][G[5][:, 0]], G[0][G[5][:, 2]] - G[0][G[5][:, 0]]), axis=1).sum()
    say("  %-34s %6d vertices, %5.0f square centimetres of cloth, %.1f mm thick%s"
        % (mesh_name, len(G[0]), area * 1e4, thick * 1000.0, "; %d triangles of skin under it are hidden while it's worn" % under if under else ""))
    return (mesh_name, G) if give else mesh_name


def main(argv):
    if "--pieces" in argv:
        import inspect
        for n, fn in PIECES.items():
            print("%s%s\n    %s\n" % (n, str(inspect.signature(fn)).replace("(F, ", "(").replace("(F)", "()"), " ".join(fn.__doc__.split())))
        return
    if len(argv) < 2 or "--piece" not in argv:
        sys.exit(__doc__)
    import bodycheck as BC
    import material
    opt = {argv[i]: argv[i + 1] for i in range(2, len(argv) - 1) if argv[i].startswith("--")}
    B = PackBody(argv[1])
    skin, how = BC.skin_meshes(B, opt.get("--skin"))
    col = opt.get("--colour", "15151a")
    rgb = tuple(int(col[i:i + 2], 16) for i in (0, 2, 4))
    name = wear(B, skin, opt["--piece"], name=opt.get("--name"), slot=opt.get("--slot", "under"), outfits=opt.get("--outfits", "under"),
                colours=[(0.0, rgb)], thick=float(opt.get("--thick", "1.8")) / 1000.0, hide="--keep-skin" not in argv)
    B.write()
    said = {name: (0.6, 0.0, 0.0, 0.0, material.KINDS["cloth"]), "skin~h=%s" % (opt.get("--name") or opt["--piece"]): (0.55, 0.0, 0.0, 0.0, material.KINDS["skin"])}
    material.set_materials(argv[1], said)
    print("wrote %s (skin: %s)" % (argv[1], how))


if __name__ == "__main__":
    main(sys.argv)
