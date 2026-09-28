"""fitcheck.py -- HOW FAR IS THE FABRIC FROM HER SKIN, per bone, in rig units.

jacob: "from what i can see the fabric is wayyy ofar away from her skin"

That is a DISTANCE, and both meshes are in the same pack, so it can be measured instead of
admired. For every bone this reports the body's radius, the garment's radius, and the gap --
so "too loose", "too tight" and "clipping" stop being impressions and become a column.

⭐ PER BONE, NOT PER HEIGHT BAND, for three reasons that the height-band version got wrong
on its first run:
  * ⛔ A T-POSE PUTS THE ARMS AT CHEST HEIGHT, so a chest band measured a "body radius" of
    0.934 -- her ARM SPAN -- and duly reported the shirt as 0.11 INSIDE her body. A bone
    owns its own vertices no matter what pose the body is in.
  * ⛔ The band's compass was wrong too: it called +X "front" when club forward is +Z, so
    every direction in the table was 90 degrees from its label.
  * ⭐ And a bone capsule is EXACTLY WHAT THE ENGINE COLLIDES AGAINST, so this number
    predicts what the solver will do rather than merely describing the mesh.

⛔ RADIUS IS MEASURED TO THE BONE'S SEGMENT, NEVER TO ITS ORIGIN. Measuring from the origin
returns the limb's LENGTH: it gave the engine LeftForeArm r 24.1 on a body 140 units tall,
and the garment was shoved out by balloons. That mistake has now been made three times in
this codebase, so it is written at the top of the function that could make it a fourth.

⛔ AND IT MEASURES THE BUILD, NOT THE DRAPE. The solver moves this garment by 0.46% of the
frame, so the built shape is very nearly the final shape -- but that is a fact about THIS
garment, not a law. Length-preserving fabric cannot shrink inward: gravity pulls a too-wide
tube DOWN, never IN. If the build is wrong no simulation rescues it, which is why the build
is what this measures.

    python skate_src/author/fitcheck.py reva_pj
    python skate_src/author/fitcheck.py reva_pj --want 0.035
"""
import argparse
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack                                          # noqa: E402


def seg_dist(p, a, b):
    """Distance from p to the SEGMENT ab -- the one measurement this file exists to get
    right. A bone is a line, and its origin is not its axis."""
    ab = [b[i] - a[i] for i in range(3)]
    ap = [p[i] - a[i] for i in range(3)]
    L = sum(v * v for v in ab)
    t = 0.0 if L < 1e-9 else max(0.0, min(1.0, sum(ap[i] * ab[i] for i in range(3)) / L))
    return math.sqrt(sum((p[i] - (a[i] + ab[i] * t)) ** 2 for i in range(3)))


def pct(xs, q):
    if not xs:
        return None
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(len(xs) * q))]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subject", help="packs/v1_<subject>.rpack")
    ap.add_argument("--want", type=float, default=0.035,
                    help="the clearance a loose garment should sit at, in rig units")
    ap.add_argument("--min-verts", type=int, default=12)
    a = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "reva_native", "packs", "v1_%s.rpack" % a.subject)
    R = Rpack(path)
    nodes = R.nodes()
    names = [n["name"] for n in nodes]

    # rest world matrices, so a bone can be a segment from itself to its first child
    W = [None] * len(nodes)

    def world(i):
        if W[i] is None:
            from rpack import _m4_mul, _m4_trs
            L = _m4_trs(nodes[i]["t"], nodes[i]["q"], nodes[i]["s"])
            p = nodes[i]["parent"]
            W[i] = L if p < 0 else _m4_mul(world(p), L)
        return W[i]

    kid = [-1] * len(nodes)
    for i, nd in enumerate(nodes):
        p = nd["parent"]
        if 0 <= p < len(nodes) and kid[p] < 0:
            kid[p] = i

    def seg_of(bi):
        m = world(bi)
        a0 = (m[12], m[13], m[14])
        if kid[bi] < 0:
            return a0, a0
        k = world(kid[bi])
        return a0, (k[12], k[13], k[14])

    # ---- split the pack: the garment, and the body it is worn over ---------------------
    cloth, spans, at = set(), [], 0
    for mi in range(R.nMeshes):
        rec = R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60)
        if "~cloth" in R.sname(rec[0]):
            cloth.add(mi)
        spans.append((at, at + rec[2]))     # ⛔ skinned_positions() returns ONE FLAT list
        at += rec[2]                        # across every mesh, not a list per mesh
    if not cloth:
        print("!! %s has no ~cloth mesh -- nothing to fit" % a.subject)
        return 1

    pts = R.skinned_positions(with_bones=True)
    gv, bv = [], []
    for mi in range(R.nMeshes):
        s0, s1 = spans[mi]
        (gv if mi in cloth else bv).extend(pts[s0:s1])
    print("%s: %d garment verts over %d body verts" % (a.subject, len(gv), len(bv)))

    # ---- the body's radius per bone, each measured to that bone's own segment ----------
    body_r = {}
    by = {}
    for p in bv:
        bi = p[3]
        if bi is not None and bi >= 0:
            by.setdefault(bi, []).append(p)
    for bi, ps in by.items():
        if len(ps) < a.min_verts:
            continue
        s0, s1 = seg_of(bi)
        # 85th percentile: past the skin, under a stray vertex
        body_r[bi] = (pct([seg_dist(p, s0, s1) for p in ps], 0.85), len(ps))

    # ---- the garment goes to its NEAREST bone, not to the one it is skinned to ---------
    # ⛔ THE GARMENT'S BINDING IS COARSER THAN THE BODY'S and measuring against it lies.
    # This garment is skinned to eight bones with NO FOREARM, so every sleeve vertex over
    # the wrist belongs to LeftArm and its distance to that segment is measured to the
    # ELBOW -- a whole forearm of counterfeit looseness, which duly reported the sleeve at
    # x2.56. Nearest-bone removes the bias completely and needs nothing from the binding.
    segs = [(bi, seg_of(bi)) for bi in body_r]
    cloth_r = {}
    for p in gv:
        best, bbi = 1e9, -1
        for bi, (s0, s1) in segs:
            d = seg_dist(p, s0, s1)
            if d < best:
                best, bbi = d, bi
        if bbi >= 0:
            cloth_r.setdefault(bbi, []).append(best)
    cloth_r = {bi: (pct(ds, 0.85), len(ds)) for bi, ds in cloth_r.items()
               if len(ds) >= a.min_verts}

    shared = sorted(set(body_r) & set(cloth_r), key=lambda bi: names[bi])
    if not shared:
        print("!! no bone carries both body and garment vertices")
        return 1

    print()
    print("  %-18s %8s %8s %9s %7s   %s"
          % ("bone", "body r", "cloth r", "gap", "x", "verdict"))
    bad = 0
    for bi in shared:
        rb, nb = body_r[bi]
        rg, ng = cloth_r[bi]
        gap = rg - rb
        if gap < 0.0:
            v, worse = "CLIPPING -- fabric inside her", True
        elif gap > a.want * 2.2:
            v, worse = "too loose", True
        elif gap < a.want * 0.35:
            v, worse = "too tight", True
        else:
            v, worse = "ok", False
        bad += worse
        print("  %-18s %8.4f %8.4f %+9.4f %7.2f   %s"
              % (names[bi][:18], rb, rg, gap, rg / rb if rb > 1e-6 else 0.0, v))

    gaps = [cloth_r[bi][0] - body_r[bi][0] for bi in shared]
    print()
    print("clearance: median %+.4f  p10 %+.4f  p90 %+.4f   (target %.4f)"
          % (pct(gaps, 0.5), pct(gaps, 0.1), pct(gaps, 0.9), a.want))
    print("%d of %d bones outside the target band" % (bad, len(shared)))
    print()
    print("A garment sits a little PROUD of the skin. A NEGATIVE gap is fabric inside the")
    print("body -- it reads as clipping no matter how good the solver is, because the solver")
    print("is only asked to push cloth OUT of a capsule, never to find it there at rest.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
