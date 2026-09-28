#!/usr/bin/env python3
"""face_marks.py -- put the FACE LANDMARKS into the rig, so the engine knows where a
mouth and a pair of eyes actually are.

    face_marks.py <pack> [--mouthdy D] [--mouthdx D] [--eyedy D]
                         [--mouthhw F] [--mouthtilt DEG] [--chindy D] [--report]

jacob, looking at a face-camera rig framing a curtain of hair: "i suspect you dont
know where her eyes and mouth are." Exactly right. The authoring tools measure the
face (authorlib.face_points, anchored to the nose bump because hair owns every crown),
but the ENGINE never saw those numbers -- it only had the head BONE, which sits at the
base of the skull, so every face camera was aimed at a guessed offset from it. Guessed
offsets are how you film a hairstyle.

The fix is to stop passing numbers and start passing BONES. This appends zero-weight
marker nodes parented to the head:

    face_mouth   the lip line          face_eye_l / face_eye_r   the eye centres
    face_chin    the point of the jaw  face_mouth_l / face_mouth_r  THE CORNERS

They carry no skin weights, so they deform nothing; they ride the head through normal
FK, so they are correct under every pose, every clip, every frame -- a camera, an
aperture or a viseme system just reads their world position. Same append-and-repoint
write as fingers.py: node indices never move, so every existing clip still plays.

THE CORNERS (08-28). The shader aperture used to size itself as 0.42 x the interocular
distance -- a guess on top of a guess, because the EYE marks are themselves placed by
proportion. On kaeli that cut a hole across her whole lower face; forced open it read
as a circle from her nose to her chin, wider than her mouth and see-through to the hair
behind her skull. A mouth is not derivable from an eye line, so it gets its own pair of
marks and the engine measures the aperture off them.

⛔ THE DEPTH TRAP, which cost this face a whole rung. --mouthdy moved the mark DOWN in
Y but kept the Z that was measured at the ORIGINAL height. A face RECEDES below the
lips, so the mark ended up floating in FRONT of the surface -- 0.037 rig units clear of
reva's lips, about 7cm at her world scale. The aperture's depth gate (vFace.z > -0.12)
then admitted only what was even further forward, i.e. her NOSE, and her "mouth
opening" rendered as a vertical slit beside it. Every mark here is therefore
RE-PROJECTED onto the measured front surface AT ITS FINAL HEIGHT, after the calibration
offsets are applied, never before.

Re-runnable: marks that already exist are MOVED (their node record is patched in place)
rather than skipped, so a calibration pass is just another run. Node indices never
change, so clips, morphs and weights all stay valid.
"""
import sys, os, math, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world, face_points
from fingers import (m4_trs, m4_mul, m4_inv_affine, xform, NODE, q_between, qrot, qinv)

MARKS = ("face_mouth", "face_eye_l", "face_eye_r", "face_chin",
         "face_mouth_l", "face_mouth_r")

# A MOUTH IS NOT AN EYE LINE. The default half-width is anatomy from the club's own
# reference head (v1_crowd_goblin's Character Creator rig: interocular 3.666 with a
# lip line 2.153 below the eyes), sanity-checked against the two club faces in the
# mouth camera. It is a STARTING POINT -- calibrate it per body with --mouthhw and the
# face rig's mark gizmo (REVA_FACEDBG=1), the same loop --mouthdy already uses.
MOUTH_HW_IOD = 0.34     # half-width, in interocular distances
CHIN_DROP = 0.50        # chin below the lip line, in eye-to-mouth heights


def build(path, mouth_dy=0.0, eye_dy=0.0, mouth_dx=0.0, mouth_hw=MOUTH_HW_IOD,
          eye_xf=0.32,
          chin_dy=0.0, report=False, mouth_tilt=0.0, from_file=None, only=None):
    # ⭐ ep 12: `--only face_mouth` writes just that mark -- a MOUTH for the smoke on a body whose eyes must stay
    # unmarked (kaeli: eye marks turn the shader aperture on over a morph-driven mouth). Every mark is still
    # measured and printed; only the named ones are written.
    p = Rpack(path)
    RM = rig_map(p)
    nodes = p.nodes()
    names = p.node_names()
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    F = face_points(p, RM)
    wp, wq = fk_world(nodes)
    head = RM["head"]
    faceh = abs(F["forehead"][1] - F["chin"])
    xw = F["xw"]
    mouth, nose = F["mouth"], F["nose"]

    # the front surface at a given height, measured from the head-family flesh --
    # so an eye sits ON the face, not floating inside the skull or out in the hair
    fam = {head}
    grew = True
    while grew:
        grew = False
        for i, nd in enumerate(nodes):
            if nd["parent"] in fam and i not in fam:
                fam.add(i); grew = True
    SP = p.skinned_positions(with_bones=True)
    pts = [v[:3] for v in SP if v[3] in fam]
    def front_z(y, xlo, xhi, half=0.05):
        band = [q for q in pts if abs(q[1] - y) < half * faceh
                and xlo <= abs(q[0] - wp[head][0]) <= xhi]
        if not band: return None
        band.sort(key=lambda q: q[2])
        return band[int(len(band) * 0.90)][2]

    # ---- THE FRONT SURFACE, EXACTLY: a ray cast at the face, not a percentile over a
    # slab. reva's face is a low-poly shell (TWO vertices within a lip-radius of her
    # mouth), so a band percentile reads whatever few strays fall in the window -- two
    # windows 2% apart in width disagreed by 0.02 rig units, which is most of the depth
    # gate the aperture gets. A ray along the face's forward axis through (x, y) hits a
    # TRIANGLE, and the surface is where it hits, at any mesh density.
    fwd_sign = 1.0 if mouth[2] >= wp[head][2] else -1.0
    rec0 = p.MESH.unpack_from(p.buf, p.offMeshes)
    _tris = []
    if rec0[3]:
        idx = struct.unpack_from("<%dI" % rec0[3], p.buf, rec0[11])
        # ⛔ THE LOWER FACE IS NOT HEAD-WEIGHTED. On reva the chin, jaw and lower lip
        # are dominant to the NECK, not the head -- a head-family-only filter threw
        # away exactly the triangles the lip ray needs and every cast missed. The
        # surface family therefore reaches one joint UP the chain as well as down.
        surf_fam = set(fam)
        if nodes[head]["parent"] >= 0: surf_fam.add(nodes[head]["parent"])
        headish = set(i for i, v in enumerate(SP) if v[3] in surf_fam)
        lim = 1.4 * faceh
        for t in range(0, len(idx), 3):
            a, b, c = idx[t], idx[t + 1], idx[t + 2]
            if (a in headish) + (b in headish) + (c in headish) < 2: continue
            A, B, C = SP[a][:3], SP[b][:3], SP[c][:3]
            if min(abs(A[1] - mouth[1]), abs(B[1] - mouth[1]), abs(C[1] - mouth[1])) > lim:
                continue
            _tris.append((A, B, C))

    def cast(x, y):
        """the frontmost triangle a ray along the forward axis through (x,y) hits"""
        best = None
        for (A, B, C) in _tris:
            # barycentric in the XY projection -- the ray runs along Z, so a hit is
            # just "does the flattened triangle contain the point"
            d = ((B[1] - C[1]) * (A[0] - C[0]) + (C[0] - B[0]) * (A[1] - C[1]))
            if abs(d) < 1e-12: continue
            u = ((B[1] - C[1]) * (x - C[0]) + (C[0] - B[0]) * (y - C[1])) / d
            v = ((C[1] - A[1]) * (x - C[0]) + (A[0] - C[0]) * (y - C[1])) / d
            w = 1.0 - u - v
            if u < -1e-6 or v < -1e-6 or w < -1e-6: continue
            z = u * A[2] + v * B[2] + w * C[2]
            if best is None or (z - best) * fwd_sign > 0: best = z
        return best

    def surface_z(x, y, fallback):
        """A CLUSTER, not one ray. reva's face carries coverage gaps -- a lateral scan
        at her lip line hits at x+0.015, MISSES at +0.023 and +0.031, then hits again
        at +0.047 -- so a single cast is a coin toss on a low-poly shell. Nine rays and
        the median of whatever comes back."""
        s = 0.035 * faceh
        hits = [z for dx in (-s, 0.0, s) for dy in (-s, 0.0, s)
                for z in (cast(x + dx, y + dy),) if z is not None]
        if not hits: return fallback
        hits.sort()
        return hits[len(hits) // 2]

    def midline(y):
        """THE FACE'S OWN MIDLINE: the lateral position of the frontmost ridge (nose,
        philtrum, lip centre) at height y. The head BONE is not it -- reva's skull bone
        sits 0.032 rig to one side of her face's ridge, which is why the mouth camera
        framed her cheek and the aperture opened beside her mouth. This is anatomy the
        pipeline never placed, so it is legal to trust (circular-verification law)."""
        best, bx = None, None
        n = 41
        for k in range(n):
            x = wp[head][0] + (k / (n - 1.0) - 0.5) * 1.2 * xw
            z = cast(x, y)
            if z is None: continue
            if best is None or (z - best) * fwd_sign > 0: best, bx = z, x
        return bx

    # CALIBRATED BY EYE, per body. Neither geometry nor paint could place these on a
    # purple character with bangs, so the last word is the picture: shoot the face rig,
    # read where the lips and the eye line actually land against the drawn marks, and
    # adjust. Those deltas are BAKED INTO THE RIG, so the engine never guesses again --
    # jacob: "cant you just take a screenshot and look".
    eye_y = nose[1] + 0.30 * faceh + eye_dy
    # ⛔ AND THE SPREAD WAS A HARD 0.32 WITH NO WAY TO CHANGE IT. This block says
    # "calibrated by eye, per body" and quotes jacob's "cant you just take a screenshot
    # and look" -- and then offered --eyedy for the height and NOTHING for the width, so
    # the one axis that was actually wrong on kaeli could not be corrected. Her marks sat
    # on her inner corner and her outer corner: measured off the face rig close-up, 230
    # screen px apart against pupils 125 apart.
    eye_x = eye_xf * xw
    mouth_y = mouth[1] + mouth_dy
    # ⭐ A MEASURED OR HAND-PLACED FILE FEEDS THE DERIVATION, not just the final table
    # (2026-09-10). It used to overwrite the named marks AFTER the corners, the chin and every
    # depth had been derived from the unmeasured estimates -- so calib2marks put the rebuilt
    # reva's mouth on her lips and left both CORNERS 5 cm above it, at her nose. The file's
    # mouth and eyes now replace the estimates here; everything below follows them, and the
    # depths are still RE-PROJECTED at the final height (the depth trap).
    hand = {}
    if from_file:
        for line in open(from_file, encoding='utf-8'):
            bits = line.split()
            if len(bits) == 4 and bits[0] in MARKS:
                hand[bits[0]] = [float(bits[1]), float(bits[2]), float(bits[3])]
    if "face_eye_l" in hand and "face_eye_r" in hand:
        eye_y = 0.5 * (hand["face_eye_l"][1] + hand["face_eye_r"][1]) + eye_dy
        eye_x = 0.5 * abs(hand["face_eye_l"][0] - hand["face_eye_r"][0])
    if "face_mouth" in hand:
        mouth_y = hand["face_mouth"][1] + mouth_dy
    iod = 2.0 * eye_x
    # THE MIDLINE IS MEASURED, not taken from the skull bone (see midline()).
    mid_x = hand["face_mouth"][0] if "face_mouth" in hand else midline(mouth_y)
    if mid_x is None: mid_x = wp[head][0]
    cx = mid_x + mouth_dx
    eye_z = surface_z(cx + eye_x, eye_y, None)
    if eye_z is None: eye_z = front_z(eye_y, 0.15 * xw, 0.60 * xw)
    if eye_z is None: eye_z = mouth[2]
    eye_z -= 0.10 * xw                                  # just inside the surface

    # ---- THE RE-PROJECTION. Every mark's depth is measured at the height it ENDS UP
    # at, never at the height it was found at. See the depth trap in the header.
    def on_face(y, xoff, fallback):
        """the front surface at height y and lateral offset xoff -- ray first, and the
        band percentile only if the ray misses the mesh entirely"""
        z = surface_z(cx + xoff, y, None)
        if z is not None: return z
        lo = max(0.0, abs(xoff) - 0.18 * xw)
        z = front_z(y, lo, abs(xoff) + 0.28 * xw)
        if z is None: z = front_z(y, 0.0, 0.60 * xw, half=0.09)
        return fallback if z is None else z

    mouth_z = on_face(mouth_y, 0.0, mouth[2])
    hw = mouth_hw * iod
    tilt = math.radians(mouth_tilt)
    # THE CHIN IS WHERE THE FACE ENDS. face_points' chin is the THROAT (its front-slice
    # p02 lands below the neck joint), and a fixed fraction below the lips lands in
    # empty air on a short-jawed body -- on reva the mesh face stops at y 1.631 and a
    # proportional chin sat below it, so its depth fell back to a band percentile and
    # read -0.105, BEHIND her skull. Walk down from the lips until the casts stop
    # landing: that last hit is the point of the jaw.
    # ⛔ "walk down until the mesh ends" only works on a head that IS a separate shell
    # (reva's stops at y 1.631). kaeli's face runs into her neck and chest without a
    # break, so the walk kept finding surface and slid 9 rig units down her throat to
    # z -14, behind her own head. The chin is where the face stops REACHING FORWARD:
    # below it the surface plunges under the jaw. Protrusion is measured from the skull
    # bone, which is behind every face landmark on both bodies.
    eyespan = max(1e-9, eye_y - mouth_y)
    lip_p = mouth_z - wp[head][2]
    step = 0.02 * eyespan
    chin_y = mouth_y
    for k in range(1, 51):                            # at most 1.0 eyespan below the lips
        y = mouth_y - k * step
        z = surface_z(cx, y, None)
        if z is None: break                           # the head shell ended (reva)
        if (z - wp[head][2]) < 0.45 * lip_p: break    # gone under the jaw (kaeli)
        chin_y = y
    if chin_y >= mouth_y - 1e-9:                      # never found an edge: proportion
        chin_y = mouth_y - CHIN_DROP * eyespan
    chin_y += chin_dy
    marks = {
        "face_mouth":   [cx, mouth_y, mouth_z],
        "face_eye_l":   [cx + eye_x, eye_y, eye_z],
        "face_eye_r":   [cx - eye_x, eye_y, eye_z],
        "face_chin":    [cx, chin_y, on_face(chin_y, 0.0, mouth_z)],

        # THE CORNERS sit on the face, which curves back -- their depth is measured in
        # their own lateral window, not copied from the mouth centre.
        # ⛔ AND A MOUTH IS NOT ALWAYS LEVEL. jacob on kaeli: "her model is a little off
        # kilter ... i would lower her mouth a little bit and also tilt it". --mouthtilt
        # rotates the corner pair about the mouth centre in the frontal plane; the
        # engine's aperture reads the angle back off these two marks, and
        # mouth_interior.py builds its whole frame from them, so one number tilts the
        # opening, the cavity, the teeth and the tongue together.
        "face_mouth_l": [cx + hw * math.cos(tilt), mouth_y + hw * math.sin(tilt),
                         on_face(mouth_y + hw * math.sin(tilt), hw, mouth_z)],
        "face_mouth_r": [cx - hw * math.cos(tilt), mouth_y - hw * math.sin(tilt),
                         on_face(mouth_y - hw * math.sin(tilt), hw, mouth_z)],
    }

    # ⭐ AND A HAND-PLACED FILE WINS OVER EVERY CALCULATION ABOVE. jacob: "man i could
    # place them for you eaasier" -- he can, and on a face like kaeli's he is the only
    # instrument that works: geometry picks her fringe, face_calibrate's darkest-texel rule
    # is defeated by her liner, and I converged on her eyes by bisecting render sweeps.
    # REVA_MARKEDIT=1 lets him click them straight onto her and writes this file.
    # The file's mouth and eyes already fed the derivation above, so the corners and the chin
    # follow them; here every file mark is placed AS WRITTEN (the offset flags apply only to
    # derived marks) -- except a depth that sits BEHIND THE FACE, which is re-projected, loudly.
    # face_calibrate's depth is a percentile over painted vertices: the rebuilt reva's mouth
    # read z 0.035 against a lip surface at 0.106, inside her head. A REVA_MARKEDIT click lands
    # ON the surface and is kept.
    if hand:
        for k, v in hand.items():
            v = list(v)
            if k in ("face_mouth", "face_eye_l", "face_eye_r"):
                surf = surface_z(v[0], v[1], None)
                if surf is not None and k != "face_mouth":
                    surf -= 0.10 * xw                    # eyes sit just inside the surface
                if surf is not None and (surf - v[2]) * fwd_sign > 0.25 * iod:
                    print('   %s: file depth %.4f is %.4f behind the face -- RE-PROJECTED to %.4f'
                          % (k, v[2], abs(surf - v[2]), surf))
                    v[2] = surf
            marks[k] = v
        print('   hand-placed: %d mark(s) from %s' % (len(hand), from_file))
    print("face_marks: %s  faceh %.4f  xw %.4f  IOD %.4f" % (os.path.basename(path), faceh, xw, iod))
    print("   mouth half-width %.4f rig (%.2f x IOD)   tilt %+.1f deg   chin %.4f below"
          % (hw, mouth_hw, mouth_tilt, mouth_y - chin_y))
    print("   face triangles in range: %d   forward = %+.0fZ" % (len(_tris), fwd_sign))
    surf = surface_z(cx, mouth_y, None)
    print("   lip surface at the final height (ray): %s   mark z %.4f"
          % ("MISS" if surf is None else "z %.4f" % surf, mouth_z))
    for k in MARKS:
        v = marks[k]
        print("   %-13s rig (%.4f, %.4f, %.4f)" % (k, v[0], v[1], v[2]))
    if report:
        return 0

    hw4 = W[head]
    hwi = m4_inv_affine(hw4)
    out = bytearray(p.buf)
    def place(b, align=4):
        while len(out) % align: out.append(0)
        off = len(out); out.extend(b); return off
    strs = p.strings()
    nodeBlob = bytearray(p.buf[p.offNodes: p.offNodes + p.nNodes * 52])
    moved, added = 0, 0
    for k in (only or MARKS):
        local = xform(hwi, marks[k])
        if k in names:
            # RE-RUNNABLE: patch the existing record's translation in place. The node
            # index never moves, so clips, morphs and skin weights stay valid.
            i = names.index(k)
            struct.pack_into("<3f", nodeBlob, i * 52 + 12, local[0], local[1], local[2])
            moved += 1
        else:
            nid = len(strs); strs.append(k.encode())
            # kind 0: a MARKER, not a bone -- it must never join the ground datum or
            # the bone-span sizing
            nodeBlob += NODE.pack(nid, head, 0, local[0], local[1], local[2],
                                  0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0)
            added += 1
    newOffNodes = place(bytes(nodeBlob))
    tbl = struct.pack("<I", len(strs)); so = 0
    for s in strs:
        tbl += struct.pack("<II", so, len(s)); so += len(s)
    tbl += b"".join(strs)
    newOffStr = place(tbl)
    h = list(HDR.unpack_from(out, 4))
    h[1] = p.nNodes + added; h[6] = newOffNodes; h[11] = newOffStr; h[13] = len(out)
    HDR.pack_into(out, 4, *h)
    if not os.path.exists(path + ".marks.bak"):
        open(path + ".marks.bak", "wb").write(p.buf)
    open(path, "wb").write(out)

    q = Rpack(path)
    n2 = q.nodes(); nm2 = q.node_names()
    w2, _ = fk_world(n2)
    bones = [i for i in range(len(n2)) if n2[i].get("kind") == 1]
    mny = min(w2[i][1] for i in bones)
    print("face_marks: %d -> %d nodes (%d moved, %d added); through FK (grounded rig units):"
          % (p.nNodes, q.nNodes, moved, added))
    for k in (only or MARKS):
        i = nm2.index(k)
        print("   %-13s (%.4f, %.4f, %.4f)" % (k, w2[i][0], w2[i][1] - mny, w2[i][2]))
    # THE GATE: adding marker nodes must not move a single vertex
    before = p.skinned_positions(); after = q.skinned_positions()
    worst = max(math.dist(before[i], after[i]) for i in range(len(before)))
    print("   rest-pose check: worst vertex moved %.2e rig units" % worst)
    assert worst < 1e-5, "marker nodes moved the body; something is bound to them"
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    def arg(nm, d):
        return float(a[a.index(nm) + 1]) if nm in a else d
    build(a[0], arg("--mouthdy", 0.0), arg("--eyedy", 0.0), arg("--mouthdx", 0.0),
          arg("--mouthhw", MOUTH_HW_IOD), eye_xf=arg("--eyex", 0.32),
          chin_dy=arg("--chindy", 0.0), report="--report" in a,
          mouth_tilt=arg("--mouthtilt", 0.0),
          from_file=(a[a.index("--from") + 1] if "--from" in a else None),
          only=(tuple(a[a.index("--only") + 1].split(",")) if "--only" in a else None))
