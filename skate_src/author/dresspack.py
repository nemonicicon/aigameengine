#!/usr/bin/env python3
"""dresspack.py -- put PREMADE clothes on a body that is already a finished PACK. (ep 14, WARDROBE)

    python skate_src/author/dresspack.py reva_native/packs/v1_anima_outfit.rpack \
        models/Female_cargo_outfit-2e0b443f/glb/converted/female_cargo_outfit.glb \
        --wear Pants_army:pants --wear Shirt_2:top --tuck anima_boots \
        --hide anima_bra,anima_panties --out reva_native/packs/v1_kaeli_street.rpack

jacob, 09-25: "if you could possibly get some clothes on kaeli that would be aweosme". She had
skated, smoked and slammed through thirteen episodes in the bra and panties Anima ships with.

WHY NOT dressglb.py: it dresses the body's GLB, and the GLB then has to be BAKED into a new pack.
A bake starts from the source and drops everything authored on top of the pack since (the ep 6/7
ledger: 0 morphs, no face marks, no jaw, no mouth interior, a face vocabulary that no longer
resolves) -- and kaeli's pack now carries the ep 10-13 skate clips, the smoking loop, the board
channels and her mouth as well. So this tool works on the PACK: every byte of the source is kept,
and the clothes go on the end of it.

WHY NOT the cloth solver: parked at jacob's word ("taking way too long for bad results"), and an
action figure's clothes are MOLDED -- they move with the body because they are part of it. So a
garment here is SKINNED LIKE THE SKIN UNDER IT:
  1. the body is skinned to its rest pose in RIG space (the frame the engine draws in);
  2. the garment is taken to world space through its own node chain (dressglb.garment_world),
     mapped onto the body by measured landmarks (height between two joints, width from the
     body's own cross-section, each trouser leg onto each leg's bone line), and PUSHED OUT of
     the skin to a gap along the skin's own normals, the push smoothed over the garment so a
     pressed region does not crease;
  3. every garment vertex takes the WEIGHTS OF THE SKIN UNDER IT (the nearest skin vertices of
     the bones that garment may ride -- no hand ever drives a trouser pocket), smoothed over
     the garment. Skin and cloth then move by the same matrices, under LBS or DQS alike;
  4. THE SKIN THE GARMENT COVERS IS REMOVED from the body's index buffers (a triangle goes only
     when all three of its corners are under cloth), so there is nothing left under the clothes
     to poke through them in any pose -- jacob's rule, "clothing cant clip into the body, it
     must respect the mesh", kept by construction instead of by a solver;
  5. `--tuck <mesh>`: trousers go INTO the boots -- squeezed onto the skin below the boot's top
     edge and cut away inside it, blousing over the edge above;
  6. appended: the garment as `<name>~worn` (the engine: drawn and shadowed like the body, never
     simulated, never MEASURED -- the contact pass, the ragdoll and the clip audit keep the body
     they always had), its base colour and normal map, a material each; the header's morph
     table offset (field 14, which rpack.py calls "reserved" and makepajamas wrote as 0) is KEPT.
"""
import argparse, io, math, os, struct, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from decimate_glb import read_glb
from dressglb import garment_world, image_bytes
from rpack import Rpack, _m4_trs, _m4_mul

HDR = struct.Struct("<15I")


def m4np(m):
    """a column-major 16-float matrix as a numpy 4x4 (row-major, so M @ p works)"""
    return np.array(m, dtype=np.float64).reshape(4, 4).T


def rest_worlds(R):
    nd = R.nodes()
    W = [None] * len(nd)
    for i, n in enumerate(nd):
        L = _m4_trs(n["t"], n["q"], n["s"])
        W[i] = L if n["parent"] < 0 else _m4_mul(W[n["parent"]], L)
    return nd, np.stack([m4np(w) for w in W])


def mesh_rec(R, mi):
    return list(R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60))


def skin_mesh(R, mi, W):
    """rest-pose positions + normals in RIG space, node-index joints, float weights, triangles"""
    rec = mesh_rec(R, mi)
    nv, nidx, nb, flags = rec[2], rec[3], rec[4], rec[5]
    offPos, offNorm, offJ, offW, offI, offBM, offIBM, offBind = rec[6], rec[7], rec[9], rec[10], rec[11], rec[12], rec[13], rec[14]
    b = R.buf
    P = np.frombuffer(b, dtype="<f4", count=nv * 3, offset=offPos).reshape(-1, 3).astype(np.float64)
    N = (np.frombuffer(b, dtype="<f4", count=nv * 3, offset=offNorm).reshape(-1, 3).astype(np.float64)
         if flags & 1 else np.tile([0.0, 1.0, 0.0], (nv, 1)))
    T = np.frombuffer(b, dtype="<u4", count=nidx, offset=offI).reshape(-1, 3).astype(np.int64)
    if not (flags & 4) or not nb:
        return P, N, None, None, T
    bm = np.frombuffer(b, dtype="<u2", count=nb, offset=offBM).astype(np.int64)
    ibm = np.frombuffer(b, dtype="<f4", count=nb * 16, offset=offIBM).reshape(nb, 16)
    bind = m4np(struct.unpack_from("<16f", b, offBind)) if offBind else np.eye(4)
    pal = np.stack([W[bm[k]] @ m4np(ibm[k]) @ bind for k in range(nb)])
    J = np.frombuffer(b, dtype="u1", count=nv * 4, offset=offJ).reshape(-1, 4).astype(np.int64)
    Wt = np.frombuffer(b, dtype="u1", count=nv * 4, offset=offW).reshape(-1, 4).astype(np.float64) / 255.0
    P4 = np.concatenate([P, np.ones((nv, 1))], axis=1)
    outP = np.zeros((nv, 3)); outN = np.zeros((nv, 3))
    for k in range(4):
        M = pal[J[:, k]]
        outP += Wt[:, k:k + 1] * np.einsum("vij,vj->vi", M, P4)[:, :3]
        outN += Wt[:, k:k + 1] * np.einsum("vij,vj->vi", M[:, :3, :3], N)
    outN /= np.maximum(np.linalg.norm(outN, axis=1, keepdims=True), 1e-12)
    return outP, outN, bm[J], Wt, T


class Grid:
    """a uniform hash grid over points: the nearest-k query this tool needs, without scipy"""
    def __init__(self, P, h):
        self.P = P; self.h = h
        c = np.floor(P / h).astype(np.int64)
        self.key = self._k(c)
        o = np.argsort(self.key, kind="stable")
        self.order = o; self.skey = self.key[o]

    @staticmethod
    def _k(c):
        return ((c[..., 0] + 4096) * 8192 + (c[..., 1] + 4096)) * 8192 + (c[..., 2] + 4096)

    def near(self, q, rings=1):
        c = np.floor(q / self.h).astype(np.int64)
        out = []
        for dx in range(-rings, rings + 1):
            for dy in range(-rings, rings + 1):
                for dz in range(-rings, rings + 1):
                    k = self._k(c + np.array([dx, dy, dz]))
                    lo = np.searchsorted(self.skey, k, "left"); hi = np.searchsorted(self.skey, k, "right")
                    if hi > lo:
                        out.append(self.order[lo:hi])
        return np.concatenate(out) if out else np.zeros(0, np.int64)

    def knn(self, q, k, rings=1):
        idx = self.near(q, rings)
        if len(idx) == 0:
            return idx, np.zeros(0)
        d = np.linalg.norm(self.P[idx] - q, axis=1)
        s = np.argsort(d)[:k]
        return idx[s], d[s]


def adjacency(T, n):
    """vertex neighbours from triangles, WELDED BY POSITION is the caller's business"""
    nb = [set() for _ in range(n)]
    for a, b, c in T:
        nb[a].update((b, c)); nb[b].update((a, c)); nb[c].update((a, b))
    return [np.array(sorted(s), dtype=np.int64) for s in nb]


def weld(P, eps):
    """map each vertex to one representative of its position (UV seams split a garment's
    vertices; smoothing across a seam must treat both copies as one point)"""
    key = np.round(P / eps).astype(np.int64)
    _, rep, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    return rep[inv.reshape(-1)]


def smooth_field(F, nbw, iters, keep=None):
    for _ in range(iters):
        G = F.copy()
        for v, nb in enumerate(nbw):
            if len(nb):
                G[v] = 0.5 * F[v] + 0.5 * F[nb].mean(axis=0)
        if keep is not None:
            G[keep] = F[keep]
        F = G
    return F


def vert_normals(P, T, rep):
    fn = np.cross(P[T[:, 1]] - P[T[:, 0]], P[T[:, 2]] - P[T[:, 0]])
    acc = np.zeros_like(P)
    for k in range(3):
        np.add.at(acc, rep[T[:, k]], fn)
    acc = acc[rep]
    return acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-12)


def band_stats(P, y, band, mask=None):
    sel = np.abs(P[:, 1] - y) < band
    if mask is not None:
        sel &= mask
    Q = P[sel]
    if len(Q) == 0:
        return None
    c = Q[:, [0, 2]].mean(axis=0)
    r = np.hypot(Q[:, 0] - c[0], Q[:, 2] - c[1])
    return c, r.mean(), Q


def lerp_line(pts, y):
    """a point on a polyline of joints (top to bottom) at height y"""
    for a, b in zip(pts[:-1], pts[1:]):
        if b[1] <= y <= a[1] or a[1] <= y <= b[1]:
            t = (y - a[1]) / (b[1] - a[1]) if abs(b[1] - a[1]) > 1e-9 else 0.0
            return a + t * (b - a)
    return pts[0] if abs(y - pts[0][1]) < abs(y - pts[-1][1]) else pts[-1]


KIND_BONES = {
    "pants": ("pelvis", "spine_01", "spine_02", "thigh_", "calf_", "foot_"),
    "top": ("spine_", "neck_01", "clavicle_", "upperarm_l", "upperarm_r", "upperarm_twist", "breast_"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack"); ap.add_argument("glb")
    ap.add_argument("--wear", action="append", required=True, help="MeshSubstring:kind (pants|top)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--skin", default="anima_body", help="the mesh name the skin is made of")
    ap.add_argument("--hide", default="", help="meshes the clothes cover entirely (comma list)")
    ap.add_argument("--tuck", default="", help="boots: trousers go into this mesh below its top")
    ap.add_argument("--gap", type=float, default=0.006, help="cloth off the skin, rig units")
    ap.add_argument("--waist", type=float, default=None, help="pants: waistband top height")
    ap.add_argument("--hem", type=float, default=None, help="pants: cuff height")
    ap.add_argument("--xz", type=float, default=0.96, help="width scale on the measured fit (tight, then pushed out)")
    ap.add_argument("--tex", type=int, default=2048)
    ap.add_argument("--report", action="store_true", help="measure and print, write nothing")
    ap.add_argument("--dump", default="", help="write the fitted garments + masked skin as an OBJ for a look")
    a = ap.parse_args()
    # ⛔ the console is cp1252: a ⛔ in a print killed the first real run AFTER the fit (ep 11 ledger)
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    R = Rpack(a.pack)
    hdr = list(HDR.unpack_from(R.buf, 4))
    nd, W = rest_worlds(R)
    names = [n["name"] for n in nd]
    J = {n: W[i][:3, 3] for i, n in enumerate(names)}
    mnames = [R.sname(mesh_rec(R, mi)[0]) for mi in range(R.nMeshes)]
    print("pack %s: %d nodes, %d meshes, %d mats, %d tex, %d clips, morph table @%d"
          % (os.path.basename(a.pack), R.nNodes, R.nMeshes, R.nMats, R.nTex, R.nClips, hdr[14]))

    # ---------------------------------------------------------------- the skin, at rest
    skin = []                       # (mesh index, P, N, joints(node), weights, tris)
    for mi, nm in enumerate(mnames):
        if nm.split("#")[0] == a.skin:
            P, N, Jn, Wt, T = skin_mesh(R, mi, W)
            skin.append((mi, P, N, Jn, Wt, T))
    SP = np.concatenate([s[1] for s in skin]); SN = np.concatenate([s[2] for s in skin])
    SJ = np.concatenate([s[3] for s in skin]); SW = np.concatenate([s[4] for s in skin])
    base = np.cumsum([0] + [len(s[1]) for s in skin])
    dom = SJ[np.arange(len(SJ)), SW.argmax(axis=1)]
    print("skin: %d pieces, %d verts, y %.3f..%.3f" % (len(skin), len(SP), SP[:, 1].min(), SP[:, 1].max()))
    height = SP[:, 1].max() - SP[:, 1].min()
    grid = Grid(SP, height / 60.0)

    # other things on the body the clothes must go OVER (sleeves, straps, a choker) -- and the boots
    tuck = None
    over = []
    for mi, nm in enumerate(mnames):
        stem = nm.split("#")[0]
        if stem == a.skin or stem in a.hide.split(",") or "hair" in stem or "braid" in stem or "earring" in stem:
            continue
        P, N, Jn, Wt, T = skin_mesh(R, mi, W)
        if a.tuck and stem == a.tuck:
            tuck = P if tuck is None else np.concatenate([tuck, P])
        else:
            over.append(P)
    if tuck is not None:
        tops = {}
        for side, sgn in (("l", 1), ("r", -1)):
            q = tuck[(tuck[:, 0] * sgn > 0.01) & (tuck[:, 1] > 0.2)]
            tops[side] = np.percentile(q[:, 1], 97)
        print("tuck into %s: top edge y l %.3f r %.3f" % (a.tuck, tops["l"], tops["r"]))

    gj, bin_ = read_glb(a.glb)
    garments = []
    for spec in a.wear:
        # `Pants_leather:pants:Pants_army` = the leather pair's GEOMETRY in the army pair's CLOTH.
        # ⛔ A SKETCHFAB SCENE POSES ITS PIECES FOR THE THUMBNAIL: the army set stands at x +2.5
        # turned 45 degrees to show its side, the leather set square to the camera -- the same
        # mesh, the same UVs. Fitted turned, its legs split on the diagonal and the "crotch" was
        # found at knee height. Take the square one's geometry and whichever one's material.
        sub, kind = spec.split(":")[:2]
        matsub = spec.split(":")[2] if spec.count(":") >= 2 else None
        GP, GN, GUV, GI, gmat, gname = garment_world(gj, bin_, sub)
        if matsub:
            gmat = garment_world(gj, bin_, matsub)[4]
            gname += " in " + matsub
        GP = np.array(GP, np.float64); GUV = np.array(GUV, np.float64); GT = np.array(GI, np.int64).reshape(-1, 3)
        # ⛔ A THUMBNAIL GARMENT IS CAPPED. Modelled for a headless, armless, footless mannequin,
        # the tank closes its neck and both armholes with flat DISCS and the trousers close the
        # waist and both cuffs, so the Sketchfab render never shows the inside. Fitted to a body,
        # the neck cap -- a plate across the neckline -- met her bust, was pushed out ONTO it and
        # z-fought the front panel for three fits in a row: two cup-shaped patches of dark shards
        # that were not skin (hidden), not the normal map (A/B), not the sun's shadow (A/B).
        # ⭐ A cap is a COMPONENT built round a FAN HUB: one vertex shared by 16+ triangles. The
        # panels and the four cargo pockets never exceed valence 9; the caps are 24, 26, 36, 64.
        par = np.arange(len(GP))

        def root(x):
            while par[x] != x:
                par[x] = par[par[x]]; x = par[x]
            return x
        for t in GT:
            r0 = root(t[0])
            for u in t[1:]:
                ru = root(u)
                if ru != r0:
                    par[ru] = r0
        comp = np.array([root(v) for v in range(len(GP))])
        val = np.bincount(GT.ravel(), minlength=len(GP))
        caps = [c for c in np.unique(comp[GT[:, 0]]) if val[comp == c].max() >= 16]
        if caps:
            keepc = ~np.isin(comp[GT[:, 0]], caps)
            print("   dropped %d CAP(s) (fan hubs of valence %s): %d of %d triangles"
                  % (len(caps), ",".join(str(val[comp == c].max()) for c in caps), (~keepc).sum(), len(GT)))
            GT = GT[keepc]
            used = np.unique(GT)
            remap = -np.ones(len(GP), np.int64); remap[used] = np.arange(len(used))
            GP, GUV, GT = GP[used], GUV[used], remap[GT]
        print("\n== %s (%s): %d verts %d tris, source y %.3f..%.3f x %.3f..%.3f z %.3f..%.3f"
              % (gname, kind, len(GP), len(GT), GP[:, 1].min(), GP[:, 1].max(), GP[:, 0].min(), GP[:, 0].max(),
                 GP[:, 2].min(), GP[:, 2].max()))
        rep = weld(GP, 1e-5 * (GP.max() - GP.min() + 1e-9))
        g_top, g_bot = GP[:, 1].max(), GP[:, 1].min()
        # a garment turned on its axis cannot be fitted by heights and widths: say so, loudly
        yb0 = g_bot + (0.3 if kind == "pants" else 0.6) * (g_top - g_bot)
        Q = GP[np.abs(GP[:, 1] - yb0) < 0.05 * (g_top - g_bot)][:, [0, 2]]
        ev, evec = np.linalg.eigh((Q - Q.mean(0)).T @ (Q - Q.mean(0)))
        turn = abs(np.degrees(np.arctan2(evec[1, 1], evec[0, 1]))) % 180.0
        turn = min(turn, 180.0 - turn)
        print("   its widest axis is %.0f degrees off the body's left-right" % turn)
        if turn > 10.0:
            raise SystemExit("⛔ %s is TURNED %.0f degrees in its scene -- fit a copy that faces the camera "
                             "(Mesh:kind:MaterialMesh takes one mesh's geometry in another's material)" % (gname, turn))

        # -------------------------------------------------------- landmarks -> a fit
        if kind == "pants":
            y_w = a.waist if a.waist is not None else J["spine_01"][1] + 0.012
            y_h = a.hem if a.hem is not None else J["foot_l"][1] - 0.01
            sy = (y_w - y_h) / (g_top - g_bot)
            F = GP.copy(); F[:, 1] = y_h + (GP[:, 1] - g_bot) * sy
            # width off the HIPS: the garment's band at the hip joints against the skin's there
            yb = J["thigh_l"][1] - 0.04
            gs = band_stats(F, yb, 0.02); bs = band_stats(SP, yb, 0.02, np.isin(dom, [names.index(n) for n in names if n.startswith(("pelvis", "thigh", "spine"))]))
            sxz = bs[1] / gs[1] * a.xz
            F[:, 0] = bs[0][0] + (F[:, 0] - gs[0][0]) * sxz
            F[:, 2] = bs[0][1] + (F[:, 2] - gs[0][1]) * sxz
            print("   waist %.3f hem %.3f  sy %.3f  hips: garment r %.4f body r %.4f -> sxz %.3f" % (y_w, y_h, sy, gs[1], bs[1], sxz))
            # each trouser LEG onto each leg's bone line, below the crotch, blended in
            crotch = None
            ys = np.linspace(y_h, y_w, 80)
            for y in ys:                              # the crotch: the highest band with a gap at x=0
                sel = np.abs(F[:, 1] - y) < 0.01
                if sel.sum() and np.min(np.abs(F[sel, 0])) > 0.01:
                    crotch = y
            print("   crotch at y %.3f (hip joints %.3f)" % (crotch, J["thigh_l"][1]))
            for side, sgn in (("l", 1), ("r", -1)):
                line = np.array([J["thigh_" + side], J["calf_" + side], J["foot_" + side]])
                m = (F[:, 0] * sgn > 0) & (F[:, 1] < crotch + 0.02)
                shift = {}
                for y in np.linspace(y_h, crotch, 40):
                    sel = m & (np.abs(F[:, 1] - y) < 0.015)
                    if sel.sum() < 6:
                        continue
                    gc = F[sel][:, [0, 2]].mean(axis=0)
                    bl = lerp_line(line, y)
                    shift[y] = np.array([bl[0] - gc[0], bl[2] - gc[1]])
                ky = np.array(sorted(shift)); kv = np.array([shift[k] for k in ky])
                idx = np.where(m)[0]
                blend = np.clip((crotch + 0.02 - F[idx, 1]) / 0.10, 0.0, 1.0)
                for c, col in ((0, 0), (1, 2)):
                    F[idx, col] += blend * np.interp(F[idx, 1], ky, kv[:, c])
                yk = J["calf_" + side][1]
                print("   leg %s aligned on %d bands: shift at the knee x %+.4f z %+.4f"
                      % (side, len(ky), np.interp(yk, ky, kv[:, 0]), np.interp(yk, ky, kv[:, 1])))
        else:
            # a TOP hangs from the shoulders: the strap top onto the skin's shoulder line, width
            # off the chest, one uniform scale (a tank keeps its own proportions)
            shl = SP[(np.abs(SP[:, 0]) > 0.07) & (np.abs(SP[:, 0]) < 0.11) & (np.abs(SP[:, 2] - J["neck_01"][2]) < 0.07)
                     & (SP[:, 1] > J["spine_03"][1]) & (SP[:, 1] < J["neck_01"][1] + 0.02)]
            y_sh = np.percentile(shl[:, 1], 98)
            torso = np.isin(dom, [i for i, n in enumerate(names) if n.startswith(("spine", "breast", "clavicle"))])
            yc = y_sh - 0.17
            bs = band_stats(SP, yc, 0.015, torso)
            gy = g_top - (g_top - g_bot) * 0.40
            gs = band_stats(GP, gy, 0.015 * (g_top - g_bot) / 0.3)
            s = bs[1] / gs[1] * a.xz
            F = GP.copy()
            F[:, 1] = y_sh + (GP[:, 1] - g_top) * s
            # ⛔ NOT ON ITS CENTROID. The first fit centred the band's vertex MEAN on hers, and a
            # tank's bust is tessellated far finer than its back: the mean sat forward, the back
            # panel landed INSIDE her torso, and the push -- nearest surface wins -- shoved it out
            # through her CHEST. A bib in front of her and a bare back (the first bench, 90/180/270).
            # ⭐ A TOP HANGS OFF THE BACK: the flat side is where a garment and a body agree; the
            # bust is whatever the push makes of it. x on the band's middle, z off her back.
            gb = F[np.abs(F[:, 1] - yc) < 0.02]
            bb = SP[torso & (np.abs(SP[:, 1] - yc) < 0.015)]
            gx = 0.5 * (gb[:, 0].min() + gb[:, 0].max()); bx = 0.5 * (bb[:, 0].min() + bb[:, 0].max())
            F[:, 0] = bx + (F[:, 0] - gx) * s
            F[:, 2] = (bb[:, 2].min() - a.gap) + (F[:, 2] - gb[:, 2].min()) * s
            print("   hung off her back at z %.3f (her chest runs to z %.3f, the tank's front to %.3f)"
                  % (bb[:, 2].min(), bb[:, 2].max(), F[np.abs(F[:, 1] - yc) < 0.02][:, 2].max()))
            print("   shoulders y %.3f  chest band y %.3f body r %.4f garment r %.4f -> s %.3f  hem at y %.3f"
                  % (y_sh, yc, bs[1], gs[1], s, F[:, 1].min()))

        # -------------------------------------------------------- push out of the skin (and over the straps)
        nbw = adjacency(GT, len(F))
        # neighbours through the weld, so a seam moves as one
        groups = {}
        for v, r in enumerate(rep):
            groups.setdefault(r, []).append(v)
        nbw = [np.unique(np.concatenate([nbw[u] for u in groups[rep[v]]])) for v in range(len(F))]
        allowed = np.isin(dom, [i for i, n in enumerate(names) if n.startswith(KIND_BONES[kind])])
        OV = np.concatenate(over) if over else np.zeros((0, 3))

        def push(F, gap):
            D = np.zeros_like(F); n_in = 0
            for v in range(len(F)):
                idx, d = grid.knn(F[v], 8)
                if len(idx) == 0:
                    continue
                w = 1.0 / np.maximum(d, 1e-4)
                p = (SP[idx] * w[:, None]).sum(0) / w.sum()
                nn = (SN[idx] * w[:, None]).sum(0); nn /= max(np.linalg.norm(nn), 1e-9)
                dd = np.dot(F[v] - p, nn)
                if dd < gap:
                    D[v] = (gap - dd) * nn
                    if dd < 0:
                        n_in += 1
            return D, n_in

        # ⛔ A GARMENT HAS LAYERS. A waistband, a hem, a cuff is an outer skin and an inner
        # lining a few mm apart; pushed vertex by vertex, the lining (nearer the body, pushed
        # further) came out level with the outside and the two z-fought -- a silver zig-zag along
        # the first bench's waistband. ⭐ A LAYER AND THE LAYER OVER IT MOVE TOGETHER: every vertex
        # takes the LARGEST push within 1.2 cm of it (a dilation of the push field), along its own
        # skin normal, then the field is smoothed over the garment.

        def dilate(F, D):
            mag = np.linalg.norm(D, axis=1)
            dirn = np.zeros_like(D)
            nz = mag > 1e-12
            dirn[nz] = D[nz] / mag[nz, None]
            out = np.zeros_like(D)
            GGl = Grid(F, 0.012)
            for v in range(len(F)):
                if not nz[v]:
                    # a vertex that needed no push still rides with the layer under it
                    idx = GGl.near(F[v], 1)
                    idx = idx[np.linalg.norm(F[idx] - F[v], axis=1) < 0.012]
                    if len(idx) and mag[idx].max() > 1e-12:
                        k = idx[mag[idx].argmax()]
                        out[v] = mag[k] * dirn[k]
                    continue
                idx = GGl.near(F[v], 1)
                idx = idx[np.linalg.norm(F[idx] - F[v], axis=1) < 0.012]
                out[v] = mag[idx].max() * dirn[v]
            return out

        def normal_push(F):
            for it in range(4):
                D, n_in = push(F, a.gap)
                if it == 0:
                    deep = int((np.linalg.norm(D, axis=1) > a.gap + 0.025).sum())
                    print("   %d of %d garment verts were INSIDE the skin before the normal push, %d of them DEEP (>2.5 cm)%s"
                          % (n_in, len(F), deep, " -- ⛔ a deep vertex is pushed out of the NEAREST side, "
                             "which may be the wrong one: check the fit before the push" if deep > 20 else ""))
                D = dilate(F, D)
                if it < 3:
                    D = smooth_field(D, nbw, 6)
                F = F + D
            return F
        def sdist(F):
            """signed distance to the skin and the skin's normal, per garment vertex"""
            d = np.full(len(F), 9.0); nrm = np.zeros_like(F)
            for v in range(len(F)):
                idx, dd = grid.knn(F[v], 8)
                if len(idx) == 0:
                    continue
                w = 1.0 / np.maximum(dd, 1e-4)
                p = (SP[idx] * w[:, None]).sum(0) / w.sum()
                nn = (SN[idx] * w[:, None]).sum(0); nn /= max(np.linalg.norm(nn), 1e-9)
                d[v] = np.dot(F[v] - p, nn); nrm[v] = nn
            return d, nrm

        def local_min(F, x, r=0.012):
            GGl = Grid(F, r); out = x.copy()
            for v in range(len(F)):
                idx = GGl.near(F[v], 1)
                idx = idx[np.linalg.norm(F[idx] - F[v], axis=1) < r]
                out[v] = x[idx].min()
            return out

        if kind == "top":
            # ⭐ A TOP IS PUSHED RADIALLY, NOT ALONG THE SKIN. Below the armpits a tank is a tube
            # round her: each vertex goes straight OUT from the torso's axis to her own silhouette
            # in that direction at that height (+ the gap). The nearest-skin push drove the cloth
            # under her bust DOWN (the breast's underside normals) while the cloth above went OUT,
            # and the two crossed: a fold, two cup-shaped patches of dark shards through four fits
            # (not skin, not the normal map, not the shadow, not the caps -- each A/B'd away).
            # Above the armpit line the straps sit ON her shoulders, which only the normal push knows.
            cz = 0.5 * (bb[:, 2].min() + bb[:, 2].max())
            y_ap = J["upperarm_l"][1] - 0.07
            tmask = np.isin(dom, [i for i, n in enumerate(names) if n.startswith(("spine", "breast", "clavicle", "pelvis"))])
            TS = SP[tmask]
            NS, HB = 96, 0.01
            y0 = F[:, 1].min() - 0.05
            NY = int((F[:, 1].max() + 0.05 - y0) / HB) + 2
            ths = np.arctan2(TS[:, 2] - cz, TS[:, 0] - bx); rs = np.hypot(TS[:, 0] - bx, TS[:, 2] - cz)
            si = ((ths + np.pi) / (2 * np.pi) * NS).astype(int) % NS; yi = np.floor((TS[:, 1] - y0) / HB).astype(int)
            ok = (yi >= 0) & (yi < NY)
            RM = np.zeros((NS, NY)); np.maximum.at(RM, (si[ok], yi[ok]), rs[ok])
            for _ in range(4):                  # a cell the scan missed is not a hole in her
                E = RM == 0
                if not E.any():
                    break
                nbm = np.maximum.reduce([np.roll(RM, 1, 0), np.roll(RM, -1, 0), np.roll(RM, 1, 1), np.roll(RM, -1, 1)])
                RM[E] = nbm[E]
            RM = np.maximum.reduce([RM, np.roll(RM, 1, 0), np.roll(RM, -1, 0), np.roll(RM, 1, 1), np.roll(RM, -1, 1)])
            th = np.arctan2(F[:, 2] - cz, F[:, 0] - bx); rr = np.hypot(F[:, 0] - bx, F[:, 2] - cz)
            gsi = ((th + np.pi) / (2 * np.pi) * NS).astype(int) % NS
            gyi = np.clip(np.floor((F[:, 1] - y0) / HB).astype(int), 0, NY - 1)
            wr = np.clip((y_ap - F[:, 1]) / 0.03, 0.0, 1.0)
            dr0 = wr * np.maximum(0.0, RM[gsi, gyi] + a.gap - rr)
            dr0 = -local_min(F, -dr0)
            dr0 = smooth_field(dr0[:, None], nbw, 3)[:, 0]
            print("   radial push below y %.3f: %d verts out to her silhouette (max %.1f cm)" % (y_ap, (dr0 > 1e-4).sum(), dr0.max() * 100))
            F[:, 0] = bx + (F[:, 0] - bx) * (rr + dr0) / np.maximum(rr, 1e-9)
            F[:, 2] = cz + (F[:, 2] - cz) * (rr + dr0) / np.maximum(rr, 1e-9)
            # ⭐ FABRIC HANGS. Pushed along the skin's normals the tank followed the crease UNDER her
            # bust -- the normals there point down, the push folded the fabric into itself, and the
            # folded triangles read as black shards (the second close-up). A tank falls from the
            # bust apex (and from the shoulder blades) toward the waist: in each sector round the
            # torso, below any bulge the fabric may come IN by at most `slope` per unit it drops.
            slope = 0.5
            cz = 0.5 * (bb[:, 2].min() + bb[:, 2].max())
            th = np.arctan2(F[:, 2] - cz, F[:, 0] - bx); rr = np.hypot(F[:, 0] - bx, F[:, 2] - cz)
            sec = ((th + np.pi) / (2 * np.pi) * 48).astype(int) % 48
            dr = np.zeros(len(F))
            for s9 in range(48):
                idx = np.where(sec == s9)[0]
                env_r = None; env_y = None
                for v in idx[np.argsort(-F[idx, 1])]:
                    if env_r is None or rr[v] >= env_r - slope * (env_y - F[v, 1]):
                        env_r, env_y = rr[v], F[v, 1]
                    else:
                        dr[v] = env_r - slope * (env_y - F[v, 1]) - rr[v]
            # the layers go together: each vertex takes the largest drape within 1.2 cm
            dr = -local_min(F, -dr)
            dr = smooth_field(dr[:, None], nbw, 4)[:, 0]
            F[:, 0] = bx + (F[:, 0] - bx) * (rr + dr) / np.maximum(rr, 1e-9)
            F[:, 2] = cz + (F[:, 2] - cz) * (rr + dr) / np.maximum(rr, 1e-9)
            print("   draped: %d verts hang off a bulge above them (max %.1f cm out)" % ((dr > 1e-4).sum(), dr.max() * 100))
            F = normal_push(F)
        if kind == "pants":
            F = normal_push(F)
            # ⭐ THE WAISTBAND IS CINCHED. Sized off her hips, the band stood off her much narrower
            # waist and the lining showed round the top (the second close-up, and the side view).
            # The top 7 cm are pulled onto the skin; the lining lands at the gap and the outside
            # keeps its own thickness over it (the distance above the nearest lining vertex).
            ytop = F[:, 1].max()
            d, nrm = sdist(F)
            dmin = local_min(F, d)
            w = np.clip((F[:, 1] - (ytop - 0.07)) / 0.04, 0.0, 1.0)
            target = a.gap + (d - dmin)
            pull = w * (target - d)
            F = F + pull[:, None] * nrm
            print("   cinched: %d waistband verts pulled in (max %.1f cm)" % ((pull < -1e-4).sum(), -pull.min() * 100))

        # the last pass is the FULL gap, not half of it: a flat triangle across a curve sags
        # inside its own corners by L^2/8R -- 2 mm for a 3 cm triangle on her bust
        D, n_in = push(F, a.gap)
        F = F + dilate(F, D)
        print("   after the push: %d verts still inside" % push(F, 0.0)[1])

        # -------------------------------------------------------- into the boots
        keepT = np.ones(len(GT), bool)
        if kind == "pants" and tuck is not None:
            for side, sgn in (("l", 1), ("r", -1)):
                top = tops[side]
                m = F[:, 0] * sgn > 0
                idx = np.where(m & (F[:, 1] < top + 0.05))[0]
                for v in idx:
                    t = np.clip((top + 0.05 - F[v, 1]) / 0.06, 0.0, 1.0)      # 0 above the blouse, 1 at the edge and below
                    j, d = grid.knn(F[v], 8)
                    if len(j) == 0:
                        continue
                    w = 1.0 / np.maximum(d, 1e-4)
                    p = (SP[j] * w[:, None]).sum(0) / w.sum()
                    nn = (SN[j] * w[:, None]).sum(0); nn /= max(np.linalg.norm(nn), 1e-9)
                    want = p + nn * 0.003
                    dd = np.dot(F[v] - p, nn)
                    if dd > 0.003:
                        F[v] = F[v] + t * (want + (F[v] - p - dd * nn) - F[v])
                below = F[:, 1] < top - 0.035
                keepT &= ~(below[GT[:, 0]] & below[GT[:, 1]] & below[GT[:, 2]] & m[GT[:, 0]])
            print("   tucked: %d of %d triangles cut away inside the boots" % ((~keepT).sum(), len(GT)))

        # -------------------------------------------------------- the skin's weights
        GJ = np.zeros((len(F), 4), np.int64); GW = np.zeros((len(F), 4))
        SPa = SP[allowed]; SJa = SJ[allowed]; SWa = SW[allowed]
        ga = Grid(SPa, height / 60.0)
        dense = {}
        for v in range(len(F)):
            idx, d = ga.knn(F[v], 6, rings=1)
            if len(idx) == 0:
                idx, d = ga.knn(F[v], 6, rings=4)
            w = 1.0 / np.maximum(d, 1e-4) ** 2; w /= w.sum()
            acc = {}
            for k, i in enumerate(idx):
                for q in range(4):
                    if SWa[i, q] > 0:
                        acc[SJa[i, q]] = acc.get(SJa[i, q], 0.0) + w[k] * SWa[i, q]
            dense[v] = acc
        bones = sorted({b for acc in dense.values() for b in acc})
        bi = {b: k for k, b in enumerate(bones)}
        M = np.zeros((len(F), len(bones)))
        for v, acc in dense.items():
            for b, x in acc.items():
                M[v, bi[b]] = x
        M = smooth_field(M, nbw, 2)
        if kind == "pants":
            # ⛔ A TROUSER LEG RIDES ITS OWN LEG. The first bench in motion had a dark spike of cloth
            # reaching across between her knees in every angle: inner-leg vertices had taken some of
            # the OTHER leg's weights (nearest skin, then smoothing through the crotch seam), so when
            # the knees parted the fabric stretched across the gap. Below her crotch -- measured off
            # the SKIN (the lowest point between the legs), not the garment's gap, which read 0.78
            # because the source legs nearly touch at the knee -- the other leg's bones are dropped.
            midl = SP[(np.abs(SP[:, 0]) < 0.015) & (SP[:, 1] > J["calf_l"][1]) & (SP[:, 1] < J["pelvis"][1])]
            crotch_s = midl[:, 1].min() if len(midl) else J["thigh_l"][1] - 0.08
            leg_of = {}
            for b in bones:
                nb9 = names[b]
                if nb9.startswith(("thigh", "calf", "foot", "ball")):
                    leg_of[bi[b]] = 1 if nb9.endswith("_l") else -1
            # ⛔ WHICH LEG IS NOT WHICH SIDE OF ZERO. The first version took the sign of x, and a
            # baggy leg's INNER face crosses the midline by millimetres at the knee: those vertices
            # were handed to the other leg and stretched two sheets of cloth between her knees on
            # the ride (a rear close-up, full frame). A vertex's leg is its PANEL's: the connected
            # piece it belongs to, sided by that piece's centroid.
            par2 = np.arange(len(F))

            def root2(x):
                while par2[x] != x:
                    par2[x] = par2[par2[x]]; x = par2[x]
                return x
            for t in GT:
                r0 = root2(t[0])
                for u in t[1:]:
                    ru = root2(u)
                    if ru != r0:
                        par2[ru] = r0
            comp2 = np.array([root2(v) for v in range(len(F))])
            cx = {c: F[comp2 == c, 0].mean() for c in np.unique(comp2)}
            fixed = 0
            for v in range(len(F)):
                if F[v, 1] > crotch_s + 0.02:
                    continue
                side = 1 if cx[comp2[v]] > 0 else -1
                for c, sgn in leg_of.items():
                    if sgn != side and M[v, c] > 0:
                        M[v, c] = 0.0; fixed += 1
            print("   crotch (off her skin) at y %.3f: %d other-leg weights dropped below it" % (crotch_s, fixed))
        for v in range(len(F)):
            top4 = np.argsort(-M[v])[:4]
            w = M[v, top4]; w = w / max(w.sum(), 1e-9)
            GJ[v] = [bones[t] for t in top4]; GW[v] = w
        print("   weights from %d skin verts on %d bones: %s" % (allowed.sum(), len(bones), " ".join(names[b] for b in bones)))

        GN2 = vert_normals(F, GT[keepT], rep)
        # the same normals over the WHOLE garment, turned to point away from her (a garment's
        # winding is whatever its modeller left it)
        Nall = vert_normals(F, GT, rep)
        smp = np.arange(0, len(F), max(1, len(F) // 300))
        sd, sn = sdist(F[smp])
        if np.mean(np.sum(Nall[smp] * sn, axis=1)) < 0:
            Nall = -Nall
            print("   (its winding faces IN: normals turned for the cover test)")
        garments.append(dict(name=gname, kind=kind, P=F, N=GN2, UV=GUV, T=GT[keepT], J=GJ, W=GW, bones=bones,
                             tex=image_bytes(gj, bin_, gmat), ntex=image_bytes(gj, bin_, gmat, "normalTexture"),
                             Tall=GT, Nall=Nall, rep=rep))

    # ---------------------------------------------------------------- the skin the clothes cover
    # a skin vertex is COVERED when a ray along its normal meets the cloth within reach -- or
    # meets it up to 1.2 cm BEHIND it: skin poking THROUGH a sagging garment triangle is under
    # the cloth too (the first bench kept exactly that skin, and it showed through the tank as
    # shards on her bust -- only skin UNDER the cloth had been counted)
    GPc = np.concatenate([g["P"] for g in garments])
    offs = np.cumsum([0] + [len(g["P"]) for g in garments])
    GTc = np.concatenate([g["Tall"] + offs[k] for k, g in enumerate(garments)])
    A = GPc[GTc[:, 0]]; E1 = GPc[GTc[:, 1]] - A; E2 = GPc[GTc[:, 2]] - A
    reach = 0.09
    cell = 0.03
    tmin = np.minimum(np.minimum(A, A + E1), A + E2); tmax = np.maximum(np.maximum(A, A + E1), A + E2)
    tri_cells = {}
    for t in range(len(GTc)):
        lo = np.floor(tmin[t] / cell).astype(int); hi = np.floor(tmax[t] / cell).astype(int)
        for x in range(lo[0], hi[0] + 1):
            for y in range(lo[1], hi[1] + 1):
                for z in range(lo[2], hi[2] + 1):
                    tri_cells.setdefault((x, y, z), []).append(t)
    # only skin within a cell of some cloth is worth a ray (the head, hands and feet are not)
    gc = np.floor(GPc / 0.05).astype(np.int64)
    near_keys = np.unique(np.concatenate([Grid._k(gc + np.array([dx, dy, dz]))
                                          for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)]))
    cand = np.where(np.isin(Grid._k(np.floor(SP / 0.05).astype(np.int64)), near_keys))[0]
    print("\ncasting %d skin rays at the clothes" % len(cand))
    covered = np.zeros(len(SP), bool)
    for s in cand:
        o = SP[s]; d = SN[s]
        ts = set()
        for k in range(-1, int(reach / (cell * 0.5)) + 1):      # and one step BEHIND the skin
            c = np.floor((o + d * k * cell * 0.5) / cell).astype(int)
            ts.update(tri_cells.get((c[0], c[1], c[2]), ()))
        if not ts:
            continue
        tl = np.fromiter(ts, np.int64)
        e1 = E1[tl]; e2 = E2[tl]
        pv = np.cross(d, e2); det = (e1 * pv).sum(1)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = o - A[tl]
        u = (tv * pv).sum(1) * inv
        qv = np.cross(tv, e1)
        vv = (qv * d).sum(1) * inv
        tt = (qv * e2).sum(1) * inv
        hit = ok & (u >= 0) & (vv >= 0) & (u + vv <= 1) & (tt > -0.012) & (tt < reach)
        if hit.any():
            covered[s] = True
    by_ray = covered.copy()
    # ⭐ A SECOND WITNESS: BEHIND THE NEAREST CLOTH. A ray along the skin normal misses when the
    # normal is not where the cloth is (a bust's underside points DOWN; the tank hangs in FRONT
    # of it) -- three fits in a row left the same cup-shaped patch of skin under the tank. So a
    # skin vertex is also covered when it lies behind the nearest garment surface (or within
    # 4 mm in front of it) -- unless that surface is an OPENING: within 1.5 cm of a hem, an
    # armhole, a neckline or a waistband edge, the skin just past the edge stays.
    GNc = np.concatenate([g["Nall"] for g in garments])
    rep_c = np.concatenate([g["rep"] + offs[k] for k, g in enumerate(garments)])
    ecount = {}
    for t in GTc:
        for e0, e1 in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            k2 = (min(rep_c[e0], rep_c[e1]), max(rep_c[e0], rep_c[e1]))
            ecount[k2] = ecount.get(k2, 0) + 1
    bnd_rep = {r for (r0, r1), c in ecount.items() if c == 1 for r in (r0, r1)}
    is_b = np.array([rep_c[v] in bnd_rep for v in range(len(GPc))])
    gb_grid = Grid(GPc[is_b], 0.015)
    near_open = np.zeros(len(GPc), bool)
    for v in range(len(GPc)):
        idx, dd = gb_grid.knn(GPc[v], 1)
        near_open[v] = len(idx) > 0 and dd[0] < 0.015
    # ⛔ A ROLLED HEM HAS NO EDGE. The tank's hem and the waistband's top are folded, so no
    # boundary edge marks them, and the skin just past them read as "behind the cloth": holes in
    # her midriff. So an opening is also where the garment ENDS in that direction: per sector
    # round her, each garment's lowest and highest vertex, and the witness stays 1.5 cm inside.
    ax0 = np.array([0.0, 0.0, J["spine_01"][2]])
    NSW = 96
    gown = np.concatenate([np.full(len(g["P"]), k) for k, g in enumerate(garments)])
    ylo = np.full((len(garments), NSW), 9.0); yhi = np.full((len(garments), NSW), -9.0)
    for k, g in enumerate(garments):
        used = np.unique(g["T"])
        Q = g["P"][used]
        sq = ((np.arctan2(Q[:, 2] - ax0[2], Q[:, 0] - ax0[0]) + np.pi) / (2 * np.pi) * NSW).astype(int) % NSW
        np.minimum.at(ylo[k], sq, Q[:, 1]); np.maximum.at(yhi[k], sq, Q[:, 1])
    gall = Grid(GPc, 0.04)
    for s in cand:
        if covered[s]:
            continue
        idx, dd = gall.knn(SP[s], 1)
        if len(idx) == 0 or dd[0] > 0.04 or near_open[idx[0]]:
            continue
        k = gown[idx[0]]
        ss = int(((np.arctan2(SP[s, 2] - ax0[2], SP[s, 0] - ax0[0]) + np.pi) / (2 * np.pi) * NSW)) % NSW
        if not (ylo[k, ss] + 0.015 < SP[s, 1] < yhi[k, ss] - 0.015):
            continue
        if np.dot(SP[s] - GPc[idx[0]], GNc[idx[0]]) < 0.004:
            covered[s] = True
    bust = (SP[:, 1] > 1.28) & (SP[:, 1] < 1.46) & (SP[:, 2] > 0.03) & (np.abs(SP[:, 0]) < 0.15)
    print("\nthe bust (y 1.28-1.46, front): %d skin verts, %d covered by the ray, %d by the ray OR behind the cloth"
          % (bust.sum(), (bust & by_ray).sum(), (bust & covered).sum()))
    print("skin covered by the clothes: %d of %d verts (%d by the ray alone)" % (covered.sum(), len(SP), by_ray.sum()))

    newidx = {}
    for k, (mi, P, N, Jn, Wt, T) in enumerate(skin):
        cv = covered[base[k]:base[k + 1]]
        keep = ~(cv[T[:, 0]] & cv[T[:, 1]] & cv[T[:, 2]])
        if (~keep).sum():
            newidx[mi] = T[keep]
            print("   %s mesh %d: %d of %d triangles hidden" % (a.skin, mi, (~keep).sum(), len(T)))
    for mi, nm in enumerate(mnames):
        if nm.split("#")[0] in [h for h in a.hide.split(",") if h]:
            newidx[mi] = np.zeros((0, 3), np.int64)
            print("   %s mesh %d: hidden whole (under the clothes)" % (nm, mi))

    if a.dump:
        with open(a.dump, "w") as f:
            vo = 1
            for g in garments:
                f.write("o %s\n" % g["name"])
                for p in g["P"]:
                    f.write("v %.5f %.5f %.5f\n" % tuple(p))
                for t in g["T"]:
                    f.write("f %d %d %d\n" % tuple(t + vo))
                vo += len(g["P"])
            for k, (mi, P, N, Jn, Wt, T) in enumerate(skin):
                TT = newidx.get(mi, T)
                f.write("o skin_%d\n" % mi)
                for p in P:
                    f.write("v %.5f %.5f %.5f\n" % tuple(p))
                for t in TT:
                    f.write("f %d %d %d\n" % tuple(t + vo))
                vo += len(P)
        print("dumped %s" % a.dump)
    if a.report:
        return 0

    # ---------------------------------------------------------------- write: the source, then the clothes
    from PIL import Image
    buf = bytearray(R.buf)

    def put(b, align=4):
        while len(buf) % align:
            buf.append(0)
        o = len(buf); buf.extend(b); return o

    def png(blob, size, rgb=True):
        im = Image.open(io.BytesIO(blob))
        im = im.convert("RGB") if rgb else im.convert("RGB")
        if max(im.size) > size:
            im = im.resize((size, size * im.size[1] // im.size[0]), Image.LANCZOS)
        out = io.BytesIO(); im.save(out, "PNG", optimize=False); return out.getvalue()

    meshes = [mesh_rec(R, mi) for mi in range(R.nMeshes)]
    for mi, TT in newidx.items():
        meshes[mi][3] = len(TT) * 3
        meshes[mi][11] = put(TT.astype("<u4").tobytes()) if len(TT) else meshes[mi][11]
    strs = R.strings()
    tex_tab = bytearray(R.buf[R.offTexTable: R.offTexTable + R.nTex * 16])
    mats = bytearray(R.buf[R.offMats: R.offMats + R.nMats * 36])
    nTex = R.nTex
    body_node = names.index(a.skin) if a.skin in names else 0
    for g in garments:
        mi = len(meshes)
        stem = "kaeli_" + g["kind"]
        nameId = len(strs); strs.append(("%s~worn" % stem).encode())
        bm = g["bones"]; loc = {b: k for k, b in enumerate(bm)}
        J8 = np.array([[loc[b] for b in row] for row in g["J"]], np.uint8)
        W8 = np.floor(g["W"] * 255.0 + 0.5).astype(np.int64)
        W8[:, 0] += 255 - W8.sum(1)
        W8 = np.clip(W8, 0, 255).astype(np.uint8)
        offPos = put(g["P"].astype("<f4").tobytes())
        offNorm = put(g["N"].astype("<f4").tobytes())
        offUV = put(g["UV"].astype("<f4").tobytes())
        offJ = put(J8.tobytes()); offW = put(W8.tobytes())
        offI = put(g["T"].astype("<u4").tobytes())
        offBM = put(np.array(bm, "<u2").tobytes())
        offIBM = put(b"".join(np.linalg.inv(W[b]).T.astype("<f4").tobytes() for b in bm))
        offBind = put(np.eye(4).astype("<f4").tobytes())
        meshes.append([nameId, body_node, len(g["P"]), len(g["T"]) * 3, len(bm), 1 | 2 | 4,
                       offPos, offNorm, offUV, offJ, offW, offI, offBM, offIBM, offBind])
        tex = ntex = -1
        if g["tex"]:
            b = png(g["tex"][0], a.tex); o = put(b)
            tex_tab += struct.pack("<4I", 0, 0, o, len(b)); tex = nTex; nTex += 1
        if g["ntex"]:
            b = png(g["ntex"][0], a.tex); o = put(b)
            tex_tab += struct.pack("<4I", 0, 0, o, len(b)); ntex = nTex; nTex += 1
        mats += struct.pack("<6I", mi, 0, 0, 0, 0, 0) + struct.pack("<ii", tex, ntex + 1) + struct.pack("<I", 0)
        print("appended mesh %d '%s~worn': %d verts %d tris %d bones, tex %d normal %d"
              % (mi, stem, len(g["P"]), len(g["T"]), len(bm), tex, ntex))
    offMeshes = put(b"".join(struct.pack("<15I", *m) for m in meshes))
    offMats = put(bytes(mats)); offTex = put(bytes(tex_tab))
    blob = b""; ents = []
    for t in strs:
        ents.append((len(blob), len(t))); blob += t
    offStr = put(struct.pack("<I", len(strs)) + b"".join(struct.pack("<II", o, l) for (o, l) in ents) + blob)
    hdr[2] = len(meshes); hdr[3] = len(mats) // 36; hdr[5] = nTex
    hdr[7] = offMeshes; hdr[8] = offMats; hdr[10] = offTex; hdr[11] = offStr; hdr[13] = len(buf)
    buf[4:4 + HDR.size] = HDR.pack(*hdr)
    open(a.out, "wb").write(bytes(buf))
    print("wrote %s: %d meshes, %d mats, %d tex, %.1f MB (morph table @%d kept)"
          % (a.out, hdr[2], hdr[3], hdr[5], len(buf) / 1e6, hdr[14]))

    import packcheck
    packcheck.BIND_SEV = "FAIL"
    bad = []
    for k in range(len(garments)):
        bad += [m for sev, m in packcheck.check(a.out, only=len(meshes) - len(garments) + k) if sev == "FAIL"]
    for m in bad:
        print("   !! %s" % m)
    print("packcheck: %s" % ("FAILED -- do not ship this pack" if bad else "clean"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
