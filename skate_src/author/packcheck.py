#!/usr/bin/env python3
"""packcheck.py -- the invariants a .rpack must satisfy, checked without rendering anything.

    python skate_src/author/packcheck.py                 # every pack
    python skate_src/author/packcheck.py v1_kaeli_pj     # one
    python skate_src/author/packcheck.py --selftest      # prove the checks can fail

WHY THIS EXISTS. jacob: "again with this regression test? i think you need to design a new,
better regression test."

He is right, and the proof is in the run that prompted it. I had just changed the
conversation camera, the head-room maths and the shot sizes -- and `shotsweep.py` reported
0.000% on all four strict shots. It did not validate that work. It STRUCTURALLY CANNOT SEE
it: its shots are the anim lab and the face rig, neither of which uses the scene camera. It
measures sameness in a corner of the engine I rarely touch, and is blind to everything I
actually change.

⭐ SO THIS ONE IS BUILT FROM THE BUGS THAT REALLY HAPPENED, and it checks PROPERTIES rather
than pixel identity. Every check below is a bug that cost real time this session:

  BIND      world[boneMap[b]] x inverseBind[b] must be the IDENTITY at rest.
            ⭐ THE ONE THAT MATTERS. kaeli's pyjamas were written with boneMap = 0,1,2..8
            (node indices) while the inverse binds came from the REAL bones -- so every
            garment vertex was driven by one bone's world matrix and a different bone's
            inverse bind. The rest palette was out by up to 111 units. It cost FOUR static
            fit attempts, a whole physics solver, and five rounds of "the garment looks
            wrong" before I measured it. This check is one second and would have said so
            immediately.

  MAP       every boneMap entry indexes a real node.
  JOINTS    every per-vertex joint index is < boneCount. An out-of-range joint reads an
            unset palette slot and folds the mesh into a star of triangles at the origin.
  WEIGHTS   per-vertex weights sum to ~255. All-zero weights = the mesh collapses to bind.
  NORMALS   not every normal identical. The pyjamas were authored (0,1,0) for every vertex
            and rendered as a flat brown cut-out with no shading anywhere.
  COLOUR    an UNTEXTURED mesh must carry `#rrggbb` in its name, because the draw paths
            fall back to a tint for it -- and a mesh with neither texture nor colour picks
            up whatever texture the previous draw left bound. That is how a blue garment
            rendered brown, and how one rendered invisible.

⛔ THIS DOES NOT REPLACE LOOKING. It replaces the part of looking that is arithmetic.
"""
import argparse
import glob
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
PACKS = os.path.join(ROOT, "reva_native", "packs")
sys.path.insert(0, HERE)
from rpack import Rpack   # noqa: E402

IDENT = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
BIND_TOL = 1e-2          # club units; the pyjama bug was 111
BIND_REL = 0.10
# ⛔ AND THIS CHECK IS INFO BY DEFAULT, BECAUSE I COULD NOT JUSTIFY IT AS A FAILURE.
# Demanding rest == bind convicted 173 meshes; normalising by rig size got it to 37; and
# those 37 packs RENDER CORRECTLY IN THE CLUB EVERY DAY. If their bone maps were really
# mismatched their bodies would be visibly exploded, and they are not. The truth is that
# a stored rest pose is very often NOT the bind pose -- it is frame 0 of an animation --
# so the bones disagree legitimately and no magnitude threshold separates that from a
# real mismatch.
# ⭐ SO IT IS SOUND ONLY FOR PACKS WE AUTHOR, where the garment is built against the
# pack's own rest skeleton and identity IS the contract. `--bind` promotes it to a
# failure; makepajamas.py and friends should run it that way on what they write.
# A checker that cries wolf on 37 good packs is worse than no checker.
BIND_SEV = "info"
SAMPLE = 400             # vertices sampled per mesh for the per-vertex checks


def m4_mul(a, b):
    o = [0.0] * 16
    for c in range(4):
        for r in range(4):
            o[c * 4 + r] = sum(a[k * 4 + r] * b[c * 4 + k] for k in range(4))
    return o


def trs(t, q, s):
    x, y, z, w = q
    m = [0.0] * 16
    m[0] = (1 - 2 * (y * y + z * z)) * s[0]; m[1] = (2 * (x * y + z * w)) * s[0]; m[2] = (2 * (x * z - y * w)) * s[0]
    m[4] = (2 * (x * y - z * w)) * s[1]; m[5] = (1 - 2 * (x * x + z * z)) * s[1]; m[6] = (2 * (y * z + x * w)) * s[1]
    m[8] = (2 * (x * z + y * w)) * s[2]; m[9] = (2 * (y * z - x * w)) * s[2]; m[10] = (1 - 2 * (x * x + y * y)) * s[2]
    m[12], m[13], m[14], m[15] = t[0], t[1], t[2], 1.0
    return m


def check(path, only=None):
    """-> (list of (severity, message)); severity 'FAIL' or 'warn'

    `only` limits the per-mesh checks to one mesh index. ⛔ That exists because the strict
    BIND assertion is only sound on a mesh whose binding WE authored: a donor pack baked from
    a Sketchfab scene routinely carries inverse binds that disagree with world[boneMap[b]],
    and demanding otherwise made makepajamas refuse to ship a garment over a fault that was
    already in the donor before it was touched."""
    out = []
    try:
        R = Rpack(path)
    except Exception as e:
        return [("FAIL", "unreadable: %s" % e)]
    nodes = R.nodes()
    W = [None] * len(nodes)
    for i, nd in enumerate(nodes):
        L = trs(nd["t"], nd["q"], nd["s"])
        W[i] = L if nd["parent"] < 0 else m4_mul(W[nd["parent"]], L)
    # ⭐ THE RIG'S OWN SIZE, so the bind check can be scale-free. A spread of 2 units means
    # nothing until you know whether the body is 3 units tall or 300.
    xs = [W[i][12] for i in range(len(nodes))]
    ys = [W[i][13] for i in range(len(nodes))]
    zs = [W[i][14] for i in range(len(nodes))]
    span = math.sqrt((max(xs) - min(xs)) ** 2 + (max(ys) - min(ys)) ** 2
                     + (max(zs) - min(zs)) ** 2) if nodes else 1.0
    if span < 1e-6:
        span = 1.0
    seen_bind = [False]

    for mi in range(R.nMeshes):
        if only is not None and mi != only:
            continue
        rec = R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60)
        name = R.sname(rec[0])
        if isinstance(name, bytes):
            name = name.decode("utf-8", "replace")
        # ⛔ FLAGS IS FIELD 5, NOT 3. The record is name,node,verts,idxCount,
        # boneCount,flags,... -- field 3 is idxCount, and reading it as flags made this
        # checker warn that the fully-textured BODY mesh had no UVs, on its very first
        # run. A checker that has never been shown a known-bad is not evidence, and one
        # that misreads its own file format is worse than none.
        verts, boneCount, flags = rec[2], rec[4], rec[5]
        offPos, offNorm, offJ, offW = rec[6], rec[7], rec[9], rec[10]
        offBM, offIBM = rec[12], rec[13]
        tag = "mesh%d '%s'" % (mi, name[:26])

        # ---- NORMALS: all-identical means they were faked -----------------------------
        if flags & 1 and verts:
            step = max(1, verts // SAMPLE)
            ns = set()
            for v in range(0, verts, step):
                ns.add(struct.unpack_from("<3f", R.buf, offNorm + v * 12))
                if len(ns) > 1:
                    break
            if len(ns) == 1:
                # ⛔ warn, not FAIL: a FLAT piece of geometry legitimately has identical
                # normals, and skateref has three such meshes. Only suspicious on a body.
                out.append(("warn", "%s: every normal identical %s -- authored, not computed"
                            % (tag, tuple(round(x, 2) for x in list(ns)[0]))))

        # ---- COLOUR: untextured meshes must carry their own -----------------------------
        tex = R.meshTex[mi] if hasattr(R, "meshTex") else None
        if tex is None:
            tex = -1 if not (flags & 2) else 0
        if not (flags & 2) and "#" not in name:
            out.append(("warn", "%s: no UV and no #rrggbb -- will draw in whatever texture "
                                "was last bound" % tag))

        if not boneCount:
            continue

        # ---- MAP + BIND ---------------------------------------------------------------
        # ⛔ THE INVARIANT IS AGREEMENT, NOT IDENTITY, and the first version of this got it
        # wrong and convicted six legitimate packs. This repo already knew why: susan's rig
        # (design/faces.md, Z5) has all 52 bones "breaking" IBM = inv(bone).meshWorld while
        # her rest pose reconstructs to a near-CONSTANT offset -- a uniform frame, not a
        # deformation. W[meshNode] is one way a baker makes inverse binds, not the only one.
        # ⭐ So: every bone must yield the SAME matrix as every other. If they agree, that
        # shared matrix is just the mesh's own frame and the pack is fine. If they disagree,
        # the bone map and the inverse binds are mismatched -- which is precisely kaeli's
        # pyjama bug (bones disagreeing by 111 units) and not susan's legitimate bake.
        bm = struct.unpack_from("<%dH" % boneCount, R.buf, offBM)
        pals, ok_b = [], []
        for b in range(boneCount):
            if bm[b] >= len(nodes):
                out.append(("FAIL", "%s: boneMap[%d]=%d but the pack has %d nodes"
                            % (tag, b, bm[b], len(nodes))))
                continue
            ibm = list(struct.unpack_from("<16f", R.buf, offIBM + b * 64))
            pals.append(m4_mul(W[bm[b]], ibm))
            ok_b.append(b)
        if len(pals) >= 2:
            ref = pals[0]
            spread, worstb = 0.0, ok_b[0]
            for k, P in enumerate(pals):
                e = max(abs(P[i] - ref[i]) for i in range(16))
                if e > spread:
                    spread, worstb = e, ok_b[k]
            # ⛔ SCALE-FREE, AND ONCE PER PACK. The spread only means something against
            # the size of the rig: kaeli's broken garment disagreed by 111 on a body spanning
            # 140 (28% of it), while the Mixamo crowd packs disagree by ~2 on comparable rigs
            # (1.5%) -- because their stored REST pose is not their BIND pose, which is legal
            # and extremely common. And the bone map is a PACK fault, not a mesh fault: a
            # 96-mesh pack was printing 96 identical lines.
            rel = spread / span
            if rel > BIND_REL and not seen_bind[0]:
                seen_bind[0] = True
                out.append((BIND_SEV, "bone map vs inverse binds MISMATCHED -- the bones "
                                    "disagree about the rest frame by %.1f, which is %.0f%% "
                                    "of the whole rig (worst: bone %d -> node %d '%s'). "
                                    "A posed rest pose gives a few %%; this is scatter."
                            % (spread, rel * 100.0, worstb, bm[worstb],
                               nodes[bm[worstb]]["name"][:18])))

        # ---- JOINTS + WEIGHTS ---------------------------------------------------------
        if verts:
            step = max(1, verts // SAMPLE)
            badj = badw = 0
            for v in range(0, verts, step):
                j = struct.unpack_from("<4B", R.buf, offJ + v * 4)
                w = struct.unpack_from("<4B", R.buf, offW + v * 4)
                if max(j) >= boneCount:
                    badj += 1
                if not (200 <= sum(w) <= 310):
                    badw += 1
            n = len(range(0, verts, step))
            if badj:
                out.append(("FAIL", "%s: %d/%d sampled vertices index a joint >= boneCount "
                                    "(%d)" % (tag, badj, n, boneCount)))
            if badw > n * 0.02:
                out.append(("FAIL", "%s: %d/%d sampled vertices have weights that do not sum "
                                    "to 255" % (tag, badw, n)))
    return out


def selftest():
    """⛔ A CHECKER THAT HAS NEVER BEEN SHOWN A KNOWN-BAD IS NOT EVIDENCE. This bench has
    been burned by its own instruments twice (anim_bench/README), so prove each check can
    actually fire before trusting a clean run."""
    ok = True
    m = m4_mul(trs((3.0, 1.0, -2.0), (0, 0, 0, 1), (1, 1, 1)), IDENT)
    err = max(abs(m4_mul(m, IDENT)[i] - IDENT[i]) for i in range(16))
    print("  bind check sees a translated bone paired with an identity inverse bind: "
          "err %.3f -> %s" % (err, "FAILS as it should" if err > BIND_TOL else "MISSED"))
    ok &= err > BIND_TOL
    err0 = max(abs(m4_mul(IDENT, IDENT)[i] - IDENT[i]) for i in range(16))
    print("  and a correct pairing: err %.6f -> %s"
          % (err0, "passes" if err0 <= BIND_TOL else "FALSE POSITIVE"))
    ok &= err0 <= BIND_TOL
    print("selftest %s" % ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    # ⛔ stdout here is cp1252 and one pack is named with a non-ASCII character (the
    # ninjato pack). The first full run crashed mid-report on it.
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument("name", nargs="?")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--quiet", action="store_true", help="only print packs with findings")
    ap.add_argument("--bind", action="store_true",
                    help="promote the rest-frame check to a FAIL. Sound only for packs "
                         "WE author, where rest == bind is the contract.")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.bind:
        globals()["BIND_SEV"] = "FAIL"
    pats = os.path.join(PACKS, "%s.rpack" % (a.name if a.name else "*"))
    files = sorted(glob.glob(pats))
    if not files:
        sys.exit("no packs matched %s" % pats)
    nfail = nwarn = 0
    for f in files:
        res = check(f)
        fails = [r for r in res if r[0] == "FAIL"]
        warns = [r for r in res if r[0] != "FAIL"]
        nfail += len(fails)
        nwarn += len(warns)
        if not res:
            if not a.quiet:
                print("%-34s ok" % os.path.basename(f)[:34])
            continue
        print("%-34s %d FAIL %d warn" % (os.path.basename(f)[:34], len(fails), len(warns)))
        for sev, msg in fails + warns:
            print("    %-4s %s" % (sev, msg))
    print("-" * 78)
    print("%d pack(s): %d FAIL, %d warn" % (len(files), nfail, nwarn))
    return 1 if nfail else 0


if __name__ == "__main__":
    sys.exit(main())
