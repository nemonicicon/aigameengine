#!/usr/bin/env python3
"""face_calibrate.py -- find the eyes and the mouth by READING THE PAINTED FACE.

    face_calibrate.py <pack>

Geometry could not tell me where a face is. The nose-bump heuristic picks the fringe on
a body with bangs; proportions from the "chin" are wrong because the measured chin is
the throat; and every guess put a camera in her hair (jacob: "i suspect you dont know
where her eyes and mouth are"). But the face is PAINTED, and paint does not lie: the
eyes are the darkest thing on a head, the lips are the reddest.

So each head vertex is sampled through its own UV into the pack's texture, and the
features are found in colour:

  eyes   the darkest texels, in two clusters left and right of the midline
  mouth  the most SATURATED RED texels below the eyes (skin is pale and desaturated;
         lips are not)

Both are then reported as rig-space points -- the same frame face_marks.py writes and
face_morphs.py bands against. Hair is excluded by weight family and by demanding the
vertex face forward, and the eye clusters must be symmetric about the head's midline,
which no fringe is.
"""
import sys, os, math, struct, io as _io
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world
from fingers import m4_trs, m4_mul, xform

def texture(p, idx=0):
    from PIL import Image
    tab = p.offTexTable
    off, ln = struct.unpack_from("<II", p.buf, tab + idx * 16 + 8)
    assert ln, "texture %d is empty" % idx
    return Image.open(_io.BytesIO(bytes(p.buf[off:off + ln]))).convert("RGB")

def calibrate(path, dump=None):
    p = Rpack(path)
    RM = rig_map(p)
    nodes = p.nodes()
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    head = RM["head"]
    fam = {head}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam:
                fam.add(i); grew = True

    # WHICH TEXTURE EACH MESH WEARS. Material records are 36 bytes: mesh index at 0,
    # texture index at 24. The engine builds meshTex the same way; without it there is no
    # way to sample a modular body, because "texture 0" belongs to whichever primitive the
    # baker happened to write first.
    mtex = {}
    for r9 in range(p.nMats):
        mesh9 = struct.unpack_from("<I", p.buf, p.offMats + r9 * 36)[0]
        tex9 = struct.unpack_from("<i", p.buf, p.offMats + r9 * 36 + 24)[0]
        if tex9 >= 0 and mesh9 not in mtex:
            mtex[mesh9] = tex9

    # EVERY MESH, NOT MESH ZERO. skinned_positions walks them all and hands back RIG-SPACE
    # points with each vertex's dominant bone, in one flat list -- so the per-mesh vertex
    # index has to be offset into it, which is precisely what the old code got wrong.
    allv = p.skinned_positions(with_bones=True)
    hx = W[head][12]
    samples = []
    imgs = {}
    used = []
    per = []
    base = 0
    for mi in range(p.nMeshes):
        rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
        verts, offUV = rec[2], rec[8]
        ti = mtex.get(mi, -1)
        if offUV and ti >= 0:
            if ti not in imgs:
                im = texture(p, ti)
                imgs[ti] = (im.size[0], im.size[1], im.load())
            TW, TH, px = imgs[ti]
            got = 0
            first = len(samples)
            for k in range(verts):
                e = allv[base + k]
                if e[3] not in fam:
                    continue
                u, v = struct.unpack_from("<2f", p.buf, offUV + k * 8)
                tx = int((u % 1.0) * (TW - 1)); ty = int((1.0 - (v % 1.0)) * (TH - 1))
                c = px[max(0, min(TW - 1, tx)), max(0, min(TH - 1, ty))]
                samples.append(((e[0], e[1], e[2]), c))
                got += 1
            if got:
                per.append((TW * TH, got, mi, ti, TW, TH, first, len(samples)))
        base += verts

    for e in per:
        used.append("mesh %d tex %d (%dx%d) %d verts" % (e[2], e[3], e[4], e[5], e[1]))
    assert samples, "no head vertices"
    # SAY WHICH MESHES THE FACE WAS READ FROM. A silent gather is how the wrong sheet
    # produced a confident wrong answer.
    for u9 in used:
        print("   read %s" % u9)
    TW, TH = (imgs[list(imgs)[0]][0], imgs[list(imgs)[0]][1]) if imgs else (0, 0)
    # skin is the bright majority; features are what departs from it
    lum = sorted(0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2] for _r, c in samples)
    skin = lum[int(len(lum) * 0.60)]
    zs = sorted(r[2] for r, _c in samples)
    zfront = zs[int(len(zs) * 0.55)]
    front = [(r, c) for (r, c) in samples if r[2] > zfront]
    assert front, "no forward-facing head vertices"

    def pct(v, q):
        v = sorted(v); return v[max(0, min(len(v) - 1, int(len(v) * q)))]

    # ---- eyes: the darkest forward texels, split by side, and they must be symmetric
    dark = sorted(front, key=lambda e: 0.299*e[1][0] + 0.587*e[1][1] + 0.114*e[1][2])
    dark = dark[:max(12, len(front) // 22)]
    L = [e for e in dark if e[0][0] - hx > 0]
    R = [e for e in dark if e[0][0] - hx < 0]
    eye = None
    if len(L) >= 4 and len(R) >= 4:
        ly = pct([e[0][1] for e in L], 0.5); ry = pct([e[0][1] for e in R], 0.5)
        lx9 = pct([e[0][0] for e in L], 0.5); rx9 = pct([e[0][0] for e in R], 0.5)
        dl9, dr9 = lx9 - hx, hx - rx9
        # ⛔ SYMMETRY IN Y IS NOT SYMMETRY. The old test only asked that the two dark
        # clusters sit at the same HEIGHT, which a fringe and a temple satisfy easily --
        # kaeli came back with her "eyes" at x 0.2466 and -11.0981, eleven units apart on
        # a head a couple of units wide, because dark hair on both sides of a face is
        # level. A pair of eyes also straddles the midline and is near MIRRORED across it.
        # With this, kaeli is refused (as the tool always did refuse her) and Anima passes
        # at an interocular of 0.115 -- a 63 mm human IOD at the club's 1.75x scale.
        sym9 = (dl9 > 0 and dr9 > 0 and abs(dl9 - dr9) < 0.35 * (dl9 + dr9))
        if sym9 and abs(ly - ry) < 0.25 * abs(pct([e[0][1] for e in front], 0.9) -
                                     pct([e[0][1] for e in front], 0.1)):
            eye = dict(y=0.5*(ly+ry),
                       lx=pct([e[0][0] for e in L], 0.5), rx=pct([e[0][0] for e in R], 0.5),
                       z=pct([e[0][2] for e in L + R], 0.6))
    # ---- mouth: the reddest texels BELOW the eye line
    def redness(c):
        mx = max(c); mn = min(c)
        sat = (mx - mn) / (mx or 1)
        return sat * (1.0 if c[0] >= mx else 0.0)
    # THE MOUTH IS NOT DIRECTLY UNDER THE EYES. "Reddest texel below the eye line" put
    # Anima's mouth 0.0129 below her eyes against an interocular of 0.115 -- about a tenth
    # of the way down a face -- because the eye geometry's own iris/sclera paint and the
    # lower lid are the reddest saturated things immediately beneath the eyes.
    # A face has proportions: the mouth sits roughly 0.6-0.9 x interocular below the eye
    # line on every human head. Refusing the band right under the eyes costs nothing and
    # removes the whole class of false positive.
    iod9 = abs(eye["lx"] - eye["rx"]) if eye else 0.0
    floor9 = (eye["y"] - 0.35 * iod9) if eye else None
    # ⛔ AND IT IS BETWEEN THE EYES. On the rebuilt reva (2026-09-10) the reddest saturated
    # texels below her eye line were a side lock of hair with pink-violet streaks: the mouth
    # came back at x -0.0515, 5 cm off a midline her own symmetric eye pair put at -0.001,
    # and 3 cm behind her lips. A mouth's centre cannot be further from the eyes' midpoint
    # than a mouth half-width (~0.34 IOD) -- found eyes fence the search to that band.
    if eye:
        mx9 = 0.5 * (eye["lx"] + eye["rx"])
        cand0 = [e for e in front if e[0][1] < floor9]
        cand = [e for e in cand0 if abs(e[0][0] - mx9) < 0.34 * iod9]
        print("   mouth search fenced to |x - %.4f| < %.4f: %d of %d candidates" % (mx9, 0.34 * iod9, len(cand), len(cand0)))
    else:
        cand = list(front)
    red = sorted(cand, key=lambda e: -redness(e[1]))
    red = red[:max(10, len(cand) // 18)]
    mouth = dict(y=pct([e[0][1] for e in red], 0.5),
                 x=pct([e[0][0] for e in red], 0.5),
                 z=pct([e[0][2] for e in red], 0.75)) if red else None
    print("face_calibrate: %s  (%d head verts, texture %dx%d, skin luma %.0f)"
          % (os.path.basename(path), len(samples), TW, TH, skin))
    hy = W[head][13]
    if eye:
        print("   EYES  y %.4f (%+.4f from the head bone)  x %.4f / %.4f  z %.4f"
              % (eye["y"], eye["y"] - hy, eye["lx"], eye["rx"], eye["z"]))
    else:
        print("   EYES  not found (no symmetric dark pair)")
    if mouth:
        print("   MOUTH y %.4f (%+.4f from the head bone)  x %.4f  z %.4f"
              % (mouth["y"], mouth["y"] - hy, mouth["x"], mouth["z"]))
    if eye and mouth:
        print("   eye-to-mouth %.4f rig" % (eye["y"] - mouth["y"]))
    return eye, mouth

if __name__ == "__main__":
    calibrate(sys.argv[1])
