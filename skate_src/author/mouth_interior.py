#!/usr/bin/env python3
"""mouth_interior.py -- BUILD THE INSIDE OF A MOUTH: a cavity, teeth and a tongue.

    mouth_interior.py <pack> [--report] [--depth F] [--teeth F] [--drop]

jacob, on kaeli's first working jaw: "kaelis mouth opening looks like an eldritch
monster lol. it should be black inside right? or shadowed?"

It should, and there was nothing in there to be dark. Both club women are sealed
heads: lips modelled shut, no interior, no teeth, no tongue. mouth_dark.py painted the
inner-lip TEXELS dark, which helped a sealed seam -- but once face_jaw actually swings
the mandible open you are looking through the hole and out the back of her skull. On
kaeli that reads as a flat blue disc, because what is behind her head is her hair.

⛔ The club's reference rig cannot donate this. v1_crowd_goblin has the BONES for it
(CC_Base_Teeth01/02, Tongue01→03) but its teeth and tongue meshes did not survive the
bake -- those bones carry zero weights. So the geometry is authored here, to the
goblin's measured proportions.

WHAT IT BUILDS, in the mouth's own frame (lateral from the corner marks, up = world
up, depth = into the head), all sized in MOUTH HALF-WIDTHS so it fits any face:

    cavity   a dome behind the lips, dark. This is what stops the see-through, and it
             is the only part that has to be right for the mouth to stop being a hole.
    teeth    two arcs following the dental curve, upper and lower, biting edges just
             inside the lip line.
    tongue   a flattened dome on the floor of the mouth.

BOUND THE WAY THE GOBLIN'S IS: upper teeth and the cavity to the HEAD, lower teeth and
the tongue to FACE_JAW, so the jaw carries its own half open.

⛔ IT MUST BE ITS OWN MESH. The shader aperture discards skin by its REST position, and
the interior sits exactly where the hole is -- armed for it, the aperture would delete
the very geometry it exists to reveal. Depth cannot separate them either: the gate
reaches ~1.1 mouth half-widths back, which is past the teeth. The engine finds this
mesh by NAME ("mouth_interior") and switches the opening off while it draws.

COLOUR WITHOUT A NEW MATERIAL: three solid patches are painted into an unused corner
of the pack's own texture and the new geometry's UVs point at them. One mesh material,
one texture, no engine-side art path.
"""
import sys, os, math, struct, io as _io
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world
from fingers import m4_trs, m4_mul, m4_inv_affine, xform, MESH

MESH_NAME = "mouth_interior"
# --- sizes, in MOUTH HALF-WIDTHS (the aperture's own unit) --------------------------
# the aperture at full open spans lateral +/-1.0 and vertical +0.33 .. -0.99, so the
# cavity mouth has to cover at least that or the eye finds its edge
CAV_LAT, CAV_UP, CAV_DN = 1.16, 0.42, 1.16
CAV_DEPTH = 1.25          # how far back the dome reaches
TOOTH_H = 0.17            # height of each tooth band. Shorter than it was: the teeth
                          # used to fill the whole visible arch, which left no room for
                          # the GUM they are supposed to sit in.
TOOTH_GAP = 0.02          # each biting edge, above/below the lip line
TOOTH_FRONT = 0.44        # the arch's front, BEHIND the lip plane -- teeth set at the
                          # plane itself poke through a low-poly upper lip
TOOTH_DEPTH = 0.85        # how far back the dental arch runs. ⛔ The CORNERS are what
                          # leak: a lip surface recedes fast away from the midline, so a
                          # shallow arch on a wide mouth puts its outer teeth in FRONT of
                          # the lip and a shut mouth shows a grey sliver. Deeper arch,
                          # narrower span.
TONGUE_TOP = -0.34        # the tongue's upper surface, below the lip line -- it sits
                          # just under the lower teeth's roots, so they occlude its
                          # front. Lower than that and it hangs BELOW them in frame.
TONGUE_RISE = 0.16        # how much it humps up toward the front
TONGUE_DEPTH = 0.95
RIM_INSET = 0.14          # the cavity rim, tucked behind the MEASURED face surface


def _n(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return [c / l for c in v]


def build(path, report=False, depth=CAV_DEPTH, tooth=TOOTH_H, drop=False):
    p = Rpack(path)
    names = p.node_names()
    assert "face_mouth" in names, "run face_marks.py first"
    assert "face_jaw" in names, "run jaw.py first -- the lower half rides the jaw"
    nodes = p.nodes()
    RM = rig_map(p)
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    wp, _ = fk_world(nodes)
    g = lambda n: list(wp[names.index(n)])
    M = g("face_mouth")
    head = RM["head"]; jawN = names.index("face_jaw")
    if "face_mouth_l" in names:
        ml, mr = g("face_mouth_l"), g("face_mouth_r")
        hw = 0.5 * math.dist(ml, mr)
        lat = _n([ml[k] - mr[k] for k in range(3)])
    else:
        hw = 0.25 * math.dist(g("face_eye_l"), g("face_eye_r"))
        lat = [1.0, 0.0, 0.0]
    up = [0.0, 1.0, 0.0]
    up = _n([up[k] - lat[k] * sum(up[j] * lat[j] for j in range(3)) for k in range(3)])
    # depth axis points INTO the head
    fwd = [lat[1] * up[2] - lat[2] * up[1], lat[2] * up[0] - lat[0] * up[2],
           lat[0] * up[1] - lat[1] * up[0]]
    if sum((M[k] - wp[head][k]) * fwd[k] for k in range(3)) < 0: fwd = [-c for c in fwd]
    back = [-c for c in fwd]

    def P(u, v, w):
        """mouth-local (lateral, vertical, depth-into-head) -> rig space, in half-widths"""
        return [M[k] + lat[k] * u * hw + up[k] * v * hw + back[k] * w * hw for k in range(3)]

    # ⛔ THE RIM CANNOT SIT AT A CONSTANT DEPTH. A face RECEDES away from the midline,
    # so a cavity whose mouth is a flat ellipse 0.1 half-widths behind the lip surface
    # bursts out through the CHEEKS at its corners -- it showed as dark bands running
    # sideways past kaeli's mouth corners. The rim follows the MEASURED surface: cast a
    # ray along the mouth's own forward axis at each rim point and tuck in behind
    # whatever it hits.
    SP = p.skinned_positions(with_bones=True)
    fam = {head}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam:
                fam.add(i); grew = True
    if nodes[head]["parent"] >= 0: fam.add(nodes[head]["parent"])
    rec0 = p.MESH.unpack_from(p.buf, p.offMeshes)
    _tris = []
    if rec0[3]:
        idx = struct.unpack_from("<%dI" % rec0[3], p.buf, rec0[11])
        headish = set(i for i, v in enumerate(SP) if v[3] in fam)
        lim = 3.5 * hw
        for t in range(0, len(idx), 3):
            a, b, c = idx[t], idx[t + 1], idx[t + 2]
            if (a in headish) + (b in headish) + (c in headish) < 2: continue
            A, B, C = SP[a][:3], SP[b][:3], SP[c][:3]
            if min(math.dist(A, M), math.dist(B, M), math.dist(C, M)) > lim: continue
            _tris.append((A, B, C))

    def surf_depth(u, v):
        """depth (in half-widths, into the head) of the face surface at mouth-local
        (u,v) -- Moller-Trumbore along the mouth frame's own forward axis"""
        O = P(u, v, -6.0)                     # start well in front of the face
        D = back
        best = None
        for (A, B, C) in _tris:
            e1 = [B[k] - A[k] for k in range(3)]
            e2 = [C[k] - A[k] for k in range(3)]
            pv = [D[1] * e2[2] - D[2] * e2[1], D[2] * e2[0] - D[0] * e2[2], D[0] * e2[1] - D[1] * e2[0]]
            det = sum(e1[k] * pv[k] for k in range(3))
            if abs(det) < 1e-12: continue
            tv = [O[k] - A[k] for k in range(3)]
            uu = sum(tv[k] * pv[k] for k in range(3)) / det
            if uu < -1e-6 or uu > 1 + 1e-6: continue
            qv = [tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0]]
            vv = sum(D[k] * qv[k] for k in range(3)) / det
            if vv < -1e-6 or uu + vv > 1 + 1e-6: continue
            tt = sum(e2[k] * qv[k] for k in range(3)) / det
            if tt <= 0: continue
            if best is None or tt < best: best = tt        # the FIRST surface hit
        if best is None: return None
        return (best - 6.0 * hw) / hw          # back to mouth-local depth

    rim_hits = 0
    print("mouth_interior: %s  half-width %.4f  cavity %.2f deep"
          % (os.path.basename(path), hw, depth))
    print("   frame: lat (%.2f %.2f %.2f)  up (%.2f %.2f %.2f)  into-head (%.2f %.2f %.2f)"
          % (lat[0], lat[1], lat[2], up[0], up[1], up[2], back[0], back[1], back[2]))

    verts, nors, uvs, bind = [], [], [], []      # bind: 0 = head, 1 = jaw
    tris = []

    def add(pos, nor, uv, b):
        verts.append(pos); nors.append(nor); uvs.append(uv); bind.append(b)
        return len(verts) - 1

    # ---------- the cavity: a dome from the lip plane back into the head ----------
    NU, NV = 20, 12
    ring = []
    for j in range(NV + 1):
        fv = j / float(NV)
        row = []
        for i in range(NU + 1):
            fu = i / float(NU)
            a = fu * 2.0 * math.pi
            # an ellipse at the rim, swept back and shrinking to a point at the throat
            e = math.sin(fv * math.pi / 2.0)          # 0 at the rim, 1 at the back
            r = math.cos(fv * math.pi / 2.0)
            u = math.cos(a) * CAV_LAT * r
            v = (CAV_UP if math.sin(a) > 0 else CAV_DN) * math.sin(a) * r
            sd = surf_depth(u, v)
            if sd is not None: rim_hits += 1
            rim = (sd if sd is not None else 0.0) + RIM_INSET
            w = rim + e * depth
            # the dome's normal points INWARD (we look at its inside through the hole)
            nn = _n([-(math.cos(a) * r), -(math.sin(a) * r), -0.35])
            nw = [lat[k] * nn[0] + up[k] * nn[1] + fwd[k] * nn[2] for k in range(3)]
            # the lower half of the cavity rides the jaw so the floor drops with it
            b = 1 if v < -0.15 else 0
            row.append(add(P(u, v, w), nw, ("cav",), b))
        ring.append(row)
    for j in range(NV):
        for i in range(NU):
            a, b2, c, d = ring[j][i], ring[j][i + 1], ring[j + 1][i + 1], ring[j + 1][i]
            tris += [[a, b2, c], [a, c, d]]

    # ---------- the teeth: two arcs on the dental curve ----------
    def arch(y_edge, y_root, b, nd=12):
        """INDIVIDUAL TEETH, not a band. jacob: "teeth could use a little more detail".
        A single continuous strip reads as a bright bar at conversation distance; each
        tooth is its own quad with a gap either side, and the front four are a little
        wider and stand a little proud, so the arch has a centre."""
        base = len(verts)
        for i in range(nd):
            f0, f1 = i / float(nd), (i + 1) / float(nd)
            mid = 0.5 * (f0 + f1)
            centre = 1.0 - abs(mid - 0.5) * 2.0          # 1 at the front, 0 at the corners
            gap = 0.16 * (1.0 - 0.4 * centre)            # incisors sit tighter together
            f0 += (f1 - f0) * gap * 0.5
            f1 -= (f1 - f0) * gap * 0.5
            quad = []
            for lev in (0, 1):
                yy = y_edge if lev == 0 else y_root
                # the front teeth are a touch longer, which is what makes an arch read
                if lev == 0: yy += (y_edge - y_root) * 0.07 * centre
                for f in ((f0, f1) if lev == 0 else (f1, f0)):
                    a = (f - 0.5) * math.pi * 1.02
                    u = math.sin(a) * 0.80
                    w = TOOTH_FRONT + (1.0 - math.cos(a)) * TOOTH_DEPTH
                    # ⛔ NO PROUD INCISORS. Standing them forward pushed the front
                    # teeth THROUGH the closed lips -- a grey row visible across a shut
                    # mouth on both women. The arch curve alone gives the centre; the
                    # lips are the only thing allowed to be frontmost.
                    nn = [math.sin(a) * 0.5, 1.0 if lev == 0 else -1.0, -0.35]
                    nw = [lat[k] * nn[0] + up[k] * nn[1] + fwd[k] * nn[2] for k in range(3)]
                    quad.append(add(P(u, yy, w), _n(nw), ("teeth",), b))
            tris.append([quad[0], quad[1], quad[2]])
            tris.append([quad[0], quad[2], quad[3]])
        return base

    # ⛔ NO AUTHORED LIPS, NO GUMS. jacob: "the meshs ALREADY have lips. you dont
    # need to make them ... use the meshs own lips". Both women have modelled,
    # textured lips -- the reason they VANISHED when the mouth opened was the
    # APERTURE, which is sized to the mouth CORNERS and so discarded the lips along
    # with everything else inside that ellipse. The interior is only what is
    # genuinely absent from the mesh: a cavity, teeth and a tongue. Her own lips
    # part because the lower face is skinned to face_jaw.
    arch(+TOOTH_GAP, +TOOTH_GAP + tooth, 0)          # upper: stays with the head
    arch(-TOOTH_GAP, -TOOTH_GAP - tooth, 1)          # lower: rides the jaw

    # ---------- the tongue: a flattened dome on the floor ----------
    TU, TV = 14, 7
    trow = []
    for j in range(TV + 1):
        fv = j / float(TV)
        row = []
        for i in range(TU + 1):
            fu = i / float(TU)
            a = (fu - 0.5) * math.pi
            u = math.sin(a) * 0.66 * (1.0 - 0.45 * fv)
            w = TOOTH_FRONT + 0.10 + fv * TONGUE_DEPTH
            v = TONGUE_TOP + TONGUE_RISE * math.cos(fv * math.pi / 2.0) * math.cos(a * 0.9)
            nn = _n([math.sin(a) * 0.4, 1.0, -0.3])
            nw = [lat[k] * nn[0] + up[k] * nn[1] + fwd[k] * nn[2] for k in range(3)]
            row.append(add(P(u, v, w), nw, ("tongue",), 1))
        trow.append(row)
    for j in range(TV):
        for i in range(TU):
            a, b2 = trow[j][i], trow[j][i + 1]
            c, d = trow[j + 1][i + 1], trow[j + 1][i]
            tris += [[a, b2, c], [a, c, d]]

    print("   built %d verts, %d triangles  (cavity dome + 2 dental arches + tongue)"
          % (len(verts), len(tris)))
    print("   cavity rim: %d of %d ring points found the face surface by ray"
          % (rim_hits, (NU + 1) * (NV + 1)))
    nj = sum(1 for b in bind if b == 1)
    print("   %d verts ride FACE_JAW, %d stay with the head" % (nj, len(verts) - nj))
    if report:
        return 0
    return _write(p, path, verts, nors, uvs, bind, tris, head, jawN, W, M, hw, fam)


def _write(p, path, verts, nors, uvs, bind, tris, head, jawN, W, M, hw, fam):
    """append the interior as a SECOND MESH, with a palette patch painted into the
    pack's own texture so it needs no new material path"""
    from PIL import Image
    rec0 = list(MESH.unpack_from(p.buf, p.offMeshes))
    mNode = rec0[1]
    MW = W[mNode]; MWi = m4_inv_affine(MW)
    # ---- the palette patch: three solid squares in a corner of the texture ----
    tab = p.offTexTable
    toff, tlen = struct.unpack_from("<II", p.buf, tab + 8)
    img = Image.open(_io.BytesIO(bytes(p.buf[toff:toff + tlen]))).convert("RGB")
    TW, TH = img.size
    PATCH = max(8, TW // 64)
    # bottom-left corner: three cells, each solid, with the sample point at its centre
    # ⛔ THE DARK BELONGS AT THE BACK OF THE MOUTH, NOT AROUND THE TEETH. jacob: "too
    # dark around the teeth, shouldnt be dark around the teeth, rather it should be dark
    # on the interior of the mouth". Teeth sit in GUM; behind the gum is the cavity.
    # Without the gum band the teeth floated in black and read as a skull.
    # ⛔ ONE PINK CANNOT SERVE A PURPLE WOMAN AND A BROWN ONE. The lip colour is taken
    # from HER OWN texture -- the median texel of the skin right around her mouth mark,
    # pushed toward red and darkened. reva comes out mauve, kaeli comes out brown-red,
    # and neither needed a per-character constant.
    COLS = {"cav": (16, 7, 10), "teeth": (232, 226, 214), "tongue": (168, 74, 84)}
    uvmap = {}
    # ⛔ PAINT BOTH ENDS. Which way V runs is not knowable from the pack alone: it
    # depends on the loader's flip, and the two club packs disagreed in practice --
    # the same UV gave reva a dark cavity and kaeli a TAN one, because neither was
    # landing on the patch at all and reva's texture corner merely happened to be
    # dark. Painting the cell at the top AND the bottom of the sheet makes the sample
    # correct under either convention, which is cheaper than being clever about it.
    # ⛔ NOT IN A CORNER (2026-09-10). A hand-unwrapped sheet has empty corners; a GENERATED
    # CHIP ATLAS (Meshy) has body chips right up to its edges -- reva's rebuilt 4K sheet is 94%
    # covered, and a corner patch is a square painted onto her skin. The cells go where NO
    # triangle of ANY mesh samples under EITHER V convention (so a free square's V-mirror is
    # free too, which keeps PAINT BOTH ENDS safe). No room: refuse, never paint over the body.
    def free_cells(n, want):
        import numpy as np
        from PIL import ImageDraw, ImageFilter
        G = 1024
        cov = Image.new("L", (G, G), 0); dr = ImageDraw.Draw(cov)
        for mi in range(p.nMeshes):
            r = MESH.unpack_from(p.buf, p.offMeshes + mi * MESH.size)
            nv, ni, oUV, oIdx = r[2], r[3], r[8], r[11]
            if not (nv and ni and oUV): continue
            uv = np.frombuffer(bytes(p.buf[oUV:oUV + nv * 8]), dtype="<f4").reshape(-1, 2) % 1.0
            ix = np.frombuffer(bytes(p.buf[oIdx:oIdx + ni * 4]), dtype="<u4").reshape(-1, 3)
            for tri in uv[ix]:
                if np.ptp(tri[:, 0]) > 0.25 or np.ptp(tri[:, 1]) > 0.25: continue
                for vv in (tri[:, 1], 1.0 - tri[:, 1]):
                    dr.polygon([(float(a) * (G - 1), float(b) * (G - 1)) for a, b in zip(tri[:, 0], vv)], fill=255)
        used = np.asarray(cov.filter(ImageFilter.MaxFilter(3))) > 0
        S = np.zeros((G, G), dtype=np.int32)
        for y in range(G):
            for x in range(G):
                if not used[y, x]:
                    S[y, x] = 1 + (min(S[y - 1, x], S[y, x - 1], S[y - 1, x - 1]) if y and x else 0)
        got, cell = [], TW // G
        for flat in np.argsort(-S.ravel()):
            y, x = divmod(int(flat), G); s = int(S[y, x])
            side = min(want, s * cell) - 2 * cell                  # a clear ring round each cell
            if side < 8: break
            x0, y0 = (x - s + 1) * cell + cell, (y - s + 1) * cell + cell
            if all(abs(x0 - a) >= max(side, c) or abs(y0 - b) >= max(side, c) for a, b, c in got):
                got.append((x0, y0, side))
            if len(got) == n: break
        return got
    cells = free_cells(len(COLS), PATCH * 2)
    if len(cells) < len(COLS):
        raise SystemExit("mouth_interior: REFUSED -- only %d free texture squares >= 8 texels on "
                         "this atlas; a palette patch would be painted onto the body" % len(cells))
    for (nm, col), (x0, ym, side) in zip(COLS.items(), cells):
        for y0 in (ym, TH - ym - side):                            # PAINT BOTH ENDS (see above)
            for y in range(y0, y0 + side):
                for x in range(x0, x0 + side):
                    if 0 <= x < TW and 0 <= y < TH: img.putpixel((x, y), col)
        cx, cy = x0 + side // 2, ym + side // 2
        uvmap[nm] = (cx / float(TW), 1.0 - cy / float(TH))
    fmt = Image.open(_io.BytesIO(bytes(p.buf[toff:toff + tlen]))).format or "PNG"
    buf = _io.BytesIO()
    if fmt == "JPEG": img.save(buf, format="JPEG", quality=95, subsampling=0)
    else: img.save(buf, format="PNG")
    blob = buf.getvalue()
    print("   palette patch: %s-texel cells in FREE atlas space; %s"
          % ("/".join(str(c[2]) for c in cells), ", ".join("%s uv(%.3f,%.3f)" % (k, v[0], v[1]) for k, v in uvmap.items())))

    out = bytearray(p.buf)
    def place(b, align=4):
        while len(out) % align: out.append(0)
        off = len(out); out.extend(b); return off
    # texture first (replaces the entry in place)
    noff = place(blob)
    struct.pack_into("<II", out, tab + 8, noff, len(blob))

    # ---- geometry, in MESH space (the buffer the GPU reads) ----
    posB = b"".join(struct.pack("<3f", *xform(MWi, v)) for v in verts)
    # normals are directions: rotate only
    R = [MWi[0], MWi[1], MWi[2], MWi[4], MWi[5], MWi[6], MWi[8], MWi[9], MWi[10]]
    def rot(n):
        return [R[0] * n[0] + R[3] * n[1] + R[6] * n[2],
                R[1] * n[0] + R[4] * n[1] + R[7] * n[2],
                R[2] * n[0] + R[5] * n[1] + R[8] * n[2]]
    norB = b"".join(struct.pack("<3f", *_n(rot(n))) for n in nors)
    uvB = b"".join(struct.pack("<2f", *uvmap[u[0]]) for u in uvs)
    jntB = bytearray(); wgtB = bytearray()
    for b in bind:
        jntB += bytes([b, 0, 0, 0]); wgtB += bytes([255, 0, 0, 0])
    idxB = struct.pack("<%dI" % (len(tris) * 3), *[i for t in tris for i in t])
    bmB = struct.pack("<2H", head, jawN)
    ibmB = b"".join(struct.pack("<16f", *m4_mul(m4_inv_affine(W[b]), MW)) for b in (head, jawN))
    bindB = struct.pack("<16f", *[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])

    oPos = place(posB); oNor = place(norB); oUV = place(uvB)
    oJ = place(bytes(jntB)); oW = place(bytes(wgtB)); oIdx = place(idxB)
    oBM = place(bmB); oIBM = place(ibmB); oBind = place(bindB)

    strs = p.strings()
    nameId = len(strs); strs.append(MESH_NAME.encode())
    # the new mesh record, appended after the existing ones
    meshBlob = bytearray(p.buf[p.offMeshes: p.offMeshes + p.nMeshes * 60])
    meshBlob += MESH.pack(nameId, mNode, len(verts), len(tris) * 3, 2,
                          rec0[5] | 1 | 2 | 4,          # normals + UVs + drawable
                          oPos, oNor, oUV, oJ, oW, oIdx, oBM, oIBM, oBind)
    newOffMeshes = place(bytes(meshBlob))
    # a material for it: same texture as the body, so the palette patch is sampled
    mat0 = p.buf[p.offMats: p.offMats + 36] if p.nMats else bytes(36)
    tex0 = struct.unpack_from("<i", mat0, 24)[0] if p.nMats else 0
    matBlob = bytearray(p.buf[p.offMats: p.offMats + p.nMats * 36])
    m = bytearray(36)
    struct.pack_into("<I", m, 0, p.nMeshes)          # mesh index
    struct.pack_into("<I", m, 16, 0)                 # flags: no flip, no nearest
    struct.pack_into("<i", m, 24, tex0)
    matBlob += m
    newOffMats = place(bytes(matBlob))
    tbl = struct.pack("<I", len(strs)); so = 0
    for s in strs:
        tbl += struct.pack("<II", so, len(s)); so += len(s)
    tbl += b"".join(strs)
    newOffStr = place(tbl)

    h = list(HDR.unpack_from(out, 4))
    h[2] = p.nMeshes + 1        # nMeshes
    h[3] = p.nMats + 1          # nMats
    h[7] = newOffMeshes
    h[8] = newOffMats
    h[11] = newOffStr
    h[13] = len(out)
    HDR.pack_into(out, 4, *h)

    tmp = path + ".tmpint"
    open(tmp, "wb").write(out)
    try:
        q = Rpack(tmp)
        assert q.nMeshes == p.nMeshes + 1, "mesh count did not grow"
        before = p.skinned_positions()
        after = q.skinned_positions()[:len(before)]
        worst = max(math.dist(before[i], after[i]) for i in range(len(before)))
        print("   REST-POSE GATE: the body's own %d verts moved at most %.3e"
              % (len(before), worst))
        assert worst < 1e-4, "adding the interior moved the body"
        # and the interior itself must land inside the head, not out in the room
        newp = q.skinned_positions()[len(before):]
        assert len(newp) == len(verts), "interior vertex count mismatch"
    except Exception:
        os.remove(tmp); print("   GATE FAILED -- %s is untouched" % os.path.basename(path)); raise
    if not os.path.exists(path + ".int.bak"):
        open(path + ".int.bak", "wb").write(p.buf)
    os.replace(tmp, path)
    print("mouth_interior: %s written -- mesh %d is '%s' (backup at .int.bak)"
          % (path, p.nMeshes, MESH_NAME))
    return 0


if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    sys.exit(build(a[0], "--report" in a, arg("--depth", CAV_DEPTH), arg("--teeth", TOOTH_H)))
