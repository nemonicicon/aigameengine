#!/usr/bin/env python3
"""face_morphs.py -- give a face MORPHS, so a body with no face bones can talk.

    face_morphs.py <pack>

Neither club body has a single bone below the head: reva2's rig ends at Head, kaeli's
at Head/head_end. So a face cannot be posed -- it has to be SHAPED, which is exactly
what rpack v3 morphs (homecoming H6) exist for.

Three targets, all built from the same measured face frame (authorlib.face_points --
mouth/nose/forehead anchored to the nose bump, because hair owns every crown):

  jaw    the lower face swings open about a hinge set at ear height, behind the teeth.
         A jaw is a ROTATION, not a downward slide: sliding the chin stretches the
         cheeks and reads as a puppet. Weight ramps from zero at the mouth line to one
         at the chin, so the upper lip stays put.
  smile  the mouth CORNERS (the outer thirds of the mouth band) lift and draw back.
  brow   the brow band lifts -- the accent that makes a line land as a question.

Deltas are written in MESH space (the buffer the GPU blends), while every measurement
is taken in RIG space, so each vertex is carried through the mesh node's world matrix
and back. Skipping that puts a perfect jaw on a face that is somewhere else entirely.
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack
from authorlib import rig_map, fk_world, face_points
from fingers import m4_trs, m4_mul, m4_inv_affine, xform
from morphs import add_morph

JAW_OPEN_W = 0.135   # world units the LOWER LIP travels at weight 1 (solved per body)
SMILE_A = 0.055      # of face height
BROW_A = 0.022
LIP_PART = 0.42      # how much of the jaw the LOWER LIP takes (the rest is chin)
BLINK_F = 0.42       # of the eye band's height, the lid travels down

# THE ANGLE IS SOLVED, NOT PICKED. A fixed 6 degrees moved reva's chin 1cm and kaeli's
# half that -- invisible at conversation distance, which is exactly what jacob saw
# ("mouths are not moving"). Two bodies with different face geometry and wildly
# different rig scales (1.81 vs 0.018) need different angles to open the same MOUTH,
# so the score solves each one against a world-space target.

def build(path, scale=1.0):
    p = Rpack(path)
    RM = rig_map(p)
    nodes = p.nodes()
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    rec = p.MESH.unpack_from(p.buf, p.offMeshes)
    mNode, verts, offPos = rec[1], rec[2], rec[6]
    MW = W[mNode]                      # mesh space -> rig space
    MWi = m4_inv_affine(MW)
    F = face_points(p, RM)
    wp, _ = fk_world(nodes)
    head = wp[RM["head"]]
    xw = F["xw"]
    faceh = abs(F["forehead"][1] - F["chin"])
    mouth = F["mouth"]
    nose = F["nose"]
    # THE MARKS WIN. face_points is a heuristic that this session caught being a whole
    # feature off on a body with bangs (its "nose" was the fringe), and every band here
    # is measured FROM the mouth -- so a wrong mouth silently moves the jaw morph onto
    # her nose. Once face_marks.py has calibrated a face against the render, those
    # nodes are the truth and the heuristic steps aside.
    nm_all = p.node_names()
    if "face_mouth" in nm_all and "face_eye_l" in nm_all:
        mk = wp[nm_all.index("face_mouth")]
        eyel = wp[nm_all.index("face_eye_l")]
        eyer = wp[nm_all.index("face_eye_r")]
        eye_y = 0.5 * (eyel[1] + eyer[1])
        mouth = [mk[0], mk[1], mk[2]]
        # a face's own scale, from two landmarks that are now known to be right
        faceh = max(1e-6, (eye_y - mouth[1]) / 0.34)
        nose = [mk[0], mouth[1] + 0.42 * (eye_y - mouth[1]), mk[2]]
        print("face_morphs: using the CALIBRATED marks (mouth y %.4f, eye y %.4f, "
              "derived faceh %.4f)" % (mouth[1], eye_y, faceh))
    # the head family owns the flesh that may move at all
    fam = {RM["head"]}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam:
                fam.add(i); grew = True
    dom = {}
    for v in p.skinned_positions(with_bones=True):
        dom[len(dom)] = v[3]
    # the jaw hinge: ear height, well behind the face front
    # THE HINGE IS AT THE EAR, mid-skull. Anatomy, because the measured landmarks
    # cannot supply it: a jaw pivots just in front of the ear canal, roughly nose
    # height, about half a face-height behind the lips.
    hinge = [head[0], nose[1], mouth[2] - 0.55 * faceh]
    # AND THE "CHIN" FROM face_points IS THE THROAT. Its p02 front-slice reading lands
    # BELOW the neck joint on reva (0.52 face-heights under the mouth, where a real
    # chin is ~0.18), so the mandible is defined from the mouth instead: down 0.20 of
    # a face height, then released over the next 0.10 so the throat stays put.
    jaw_bot = mouth[1] - 0.20 * faceh
    # solve the jaw angle: displacement is linear in the angle for small angles, so
    # measure the chin's travel at one degree and scale to the world target
    chin_r = [mouth[0], mouth[1] - 0.04 * faceh, mouth[2]]   # the LOWER LIP
    a1 = math.radians(1.0)
    dy1 = chin_r[1] - hinge[1]; dz1 = chin_r[2] - hinge[2]
    m1 = math.hypot(hinge[1] + dy1*math.cos(a1) - dz1*math.sin(a1) - chin_r[1],
                    hinge[2] + dy1*math.sin(a1) + dz1*math.cos(a1) - chin_r[2])
    deg = max(4.0, min(34.0, (JAW_OPEN_W / scale) / (m1 or 1e-9)))
    ang = math.radians(deg)
    print("face_morphs: jaw solved to %.1f deg for a %.3fu world opening (scale %.4f)"
          % (deg, JAW_OPEN_W, scale))
    jaw, smile, brow, blink = [], [], [], []
    for vi in range(verts):
        mv = list(struct.unpack_from("<3f", p.buf, offPos + vi * 12))
        if dom.get(vi, -1) not in fam: continue
        r = xform(MW, mv)                                  # rig space
        if r[2] <= head[2] - 0.15 * xw: continue           # front of the head only
        # ---- jaw ----
        # THE LIP SEAM STAYS SHUT. Both club mouths are SEALED geometry -- there is no
        # modelled interior, no teeth, no tongue -- so rotating the lip surface itself
        # does not open a mouth, it tears the seam and stretches the inner-lip texture
        # into a pale mass hanging out of her face (jacob: "kaelis mouth opening looks
        # like an eldritch monster lol... it should be black inside right?"). It should,
        # and there is nothing there to be black. So the jaw carries the CHIN: zero
        # weight at the lip line, full weight at the chin, released before the throat.
        # The face reads as talking because the jaw moves, which is what a jaw does.
        lip_y = mouth[1]
        chin_y = mouth[1] - 0.36 * faceh
        rel_y = mouth[1] - 0.62 * faceh
        # the lower lip gets a SHARE of the motion (LIP_PART) so the mouth actually
        # parts and casts a seam shadow; the chin takes the rest. All of it and the
        # seam tears; none of it and she talks with a welded mouth.
        # A MOUTH IS AN ELLIPSE, NOT A LETTERBOX. The lip share used to depend only on
        # height, so the entire lip line -- corners included -- dropped by the same
        # amount and opened a dark RECTANGLE across her face. Real lips are anchored at
        # the corners and part in the middle, so the share tapers to zero by the corner.
        mhw = 0.30 * faceh                      # mouth half-width
        latm = abs(r[0] - mouth[0])
        corner = max(0.0, math.cos(min(1.0, latm / mhw) * (math.pi / 2.0)))
        if r[1] <= lip_y:
            if r[1] >= chin_y:
                t9 = (lip_y - r[1]) / (lip_y - chin_y)
                w = LIP_PART * corner + (1.0 - LIP_PART) * t9
            else: w = max(0.0, 1.0 - (chin_y - r[1]) / (chin_y - rel_y))
        else: w = 0.0
        if w > 0.02:
            a = ang * w
            dy = r[1] - hinge[1]; dz = r[2] - hinge[2]
            ry = hinge[1] + dy * math.cos(a) - dz * math.sin(a)
            rz = hinge[2] + dy * math.sin(a) + dz * math.cos(a)
            nm = xform(MWi, [r[0], ry, rz])
            jaw.append((vi, nm[0] - mv[0], nm[1] - mv[1], nm[2] - mv[2]))
        # ---- smile: the outer thirds of the mouth band ----
        dmy = abs(r[1] - mouth[1])
        lat = abs(r[0] - head[0])
        if dmy < 0.16 * faceh and 0.20 * xw < lat < 0.85 * xw:
            f2 = (1.0 - dmy / (0.16 * faceh)) * min(1.0, (lat - 0.20 * xw) / (0.30 * xw))
            d = SMILE_A * faceh * f2
            nm = xform(MWi, [r[0] + d * 0.35 * (1 if r[0] > head[0] else -1),
                             r[1] + d, r[2] - d * 0.25])
            smile.append((vi, nm[0] - mv[0], nm[1] - mv[1], nm[2] - mv[2]))
        # ---- blink: the LID, a narrow band sitting on the eye line ----
        e = (r[1] - (nose[1] + 0.02 * faceh)) / (0.18 * faceh)
        if 0.0 < e < 1.0 and 0.12 * xw < lat < 0.80 * xw:
            f4 = math.sin(math.pi * e)          # strongest mid-lid, zero at both rims
            d = BLINK_F * 0.18 * faceh * f4
            nm = xform(MWi, [r[0], r[1] - d, r[2] + d * 0.15])
            blink.append((vi, nm[0] - mv[0], nm[1] - mv[1], nm[2] - mv[2]))
        # ---- brow: BETWEEN the eyes and the MEASURED forehead, nowhere near the
        # scalp. The first band ran nose+0.22 to nose+0.78 face-heights, which on
        # reva reached y 1.899 -- past her forehead (1.814) and into the HAIRLINE --
        # so "raising an eyebrow" lifted the top of her skull. That is the movement
        # jacob saw where a mouth should have been ("revas eyebrows are moving when
        # it should be her mouth opening instead"). A brow is a ridge, not a scalp.
        bl = nose[1] + 0.16 * faceh
        bh = F["forehead"][1]
        if bh > bl and bl < r[1] < bh and lat < 0.85 * xw:
            f3 = math.sin(math.pi * (r[1] - bl) / (bh - bl))
            d = BROW_A * faceh * f3
            nm = xform(MWi, [r[0], r[1] + d, r[2] + d * 0.2])
            brow.append((vi, nm[0] - mv[0], nm[1] - mv[1], nm[2] - mv[2]))
    print("face_morphs: %s -- face height %.4f, hinge (%.3f,%.3f,%.3f)"
          % (os.path.basename(path), faceh, hinge[0], hinge[1], hinge[2]))
    # what the LIPS actually do is the number that matters
    lipv = [(e, math.sqrt(e[1]**2 + e[2]**2 + e[3]**2)) for e in jaw
            if abs(struct.unpack_from("<3f", p.buf, offPos + e[0]*12)[1]) >= 0]
    lipd = []
    for e in jaw:
        mvv = struct.unpack_from("<3f", p.buf, offPos + e[0]*12)
        rr = xform(MW, list(mvv))
        if abs(rr[1] - mouth[1]) < 0.06 * faceh:
            lipd.append(math.sqrt(e[1]**2 + e[2]**2 + e[3]**2))
    if lipd:
        lipd.sort()
        print("   LIP BAND: %d verts, median travel %.4f rig-u = %.3f world u (%.1f cm)"
              % (len(lipd), lipd[len(lipd)//2], lipd[len(lipd)//2]*scale,
                 lipd[len(lipd)//2]*scale*50))
    for nm2, s in (("jaw", jaw), ("smile", smile), ("brow", brow), ("blink", blink)):
        assert s, "no vertices found for %s" % nm2
        lo = min(e[0] for e in s); hi = max(e[0] for e in s)
        mx = max(math.sqrt(e[1]**2 + e[2]**2 + e[3]**2) for e in s)
        print("   %-6s %6d verts, vertex range %d..%d, max delta %.4f rig-u"
              % (nm2, len(s), lo, hi, mx))
    have = []
    from rpack import HDR
    offM = HDR.unpack_from(p.buf, 4)[14]
    if offM:
        n = struct.unpack_from("<I", p.buf, offM)[0]
        have = [p.sname(struct.unpack_from("<4I", p.buf, offM + 4 + i * 16)[1]) for i in range(n)]
    for nm2, s in (("jaw", jaw), ("smile", smile), ("brow", brow), ("blink", blink)):
        if nm2 in have:
            print("   %s already present, skipped" % nm2); continue
        add_morph(path, 0, nm2, s)
    q = Rpack(path)
    offM = HDR.unpack_from(q.buf, 4)[14]
    n = struct.unpack_from("<I", q.buf, offM)[0]
    print("   pack now carries %d morphs: %s"
          % (n, ", ".join(q.sname(struct.unpack_from("<4I", q.buf, offM + 4 + i * 16)[1])
                          for i in range(n))))

if __name__ == "__main__":
    a = sys.argv[1:]
    sc = float(a[a.index("--scale") + 1]) if "--scale" in a else 1.0
    build(a[0], sc)
