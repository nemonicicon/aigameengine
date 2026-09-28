#!/usr/bin/env python3
"""makepajamas.py -- author a garment and SKIN IT, inside the pack format.

    python skate_src/author/makepajamas.py

jacob: "do you think we could put some pajams on kaeli? she is a bit nude to be standing
out on the street at 7am."

⛔ AND autoweights.py COULD NOT DO IT. The house binder is the right tool and it takes a
GLB -- Kaeli exists only as an rpack, and there is no rpack -> GLB path. So the garment is
built and skinned here, directly against the pack's own rest skeleton, which the file
already carries.

⭐ THE BODY MEASURES ITSELF. The garment is not modelled to a guess: her mesh vertices are
read, sliced into horizontal bands, and each band's actual radius about the bone axis is
taken -- so the shell follows HER silhouette, at her proportions, with a small clearance.
A pyjama on a different body needs no new numbers.

⭐ AND THE WEIGHTS ARE THE SAME IDEA AS autoweights, minus the visibility gate: each
garment vertex takes the two nearest bone SEGMENTS by distance, inverse-squared. A loose
garment does not need four influences and does not need the gate, because unlike a hand
near a hip it is never near a bone it is not on top of.

⛔ IT WRITES A NEW PACK. v1_kaeli_pj.rpack -- kmoves is 39MB and is the body every other
scene in the film uses; a bad append would take all of them down. The original is not
touched.
"""
import io, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR

# ⭐ IT WAS NEVER KAELI-SPECIFIC. The generator measures whatever body it is handed -- that
# is the whole design -- and only the two filenames were nailed down. Everyone who has to be
# dressed at four in the morning can be.
#     python skate_src/author/makepajamas.py [srcStem] [dstStem] [#rrggbb]
SRC = "reva_native/packs/v1_%s.rpack" % (sys.argv[1] if len(sys.argv) > 1 else "kmoves")
DST = "reva_native/packs/v1_%s.rpack" % (sys.argv[2] if len(sys.argv) > 2 else "kaeli_pj")
COL = sys.argv[3] if len(sys.argv) > 3 else "5A6E9E"

# ⛔ THE RIG IS NOT UE. kmoves is a MIXAMO-family skeleton -- Hips / LeftUpLeg /
# LeftLeg / Spine01 -- and a UE name list resolved exactly one bone out of ten. Each entry
# is a list of alternatives so this works on either family, which is the same problem
# canonOf() solves in the engine and for the same reason.
# ⛔ A GARMENT NEEDS EVERY JOINT IT CROSSES. This list had no FOREARM, so a sleeve
# running shoulder-to-wrist was bound entirely to the UPPER ARM -- rigid along the upper-arm
# axis, and the instant an elbow bent, the far half pointed into space and gravity slid the
# sleeve off. It read as a sleeve detaching and flying away.
# ⛔ A T-POSE CANNOT SHOW THIS: with the arm straight, an upper-arm-only sleeve sits
# perfectly. Four wrong diagnoses came before the right one, and what found it was shooting
# the same body on a clip where the elbow BENDS.
# ⛔ "spine_03"/"spine02"/"chest" matched nothing here either -- this rig calls them
# Spine1 and Spine2 -- so the whole chest was riding on `Spine`.
# ⛔ AND A THIRD FAMILY, WHERE ONE TOKEN NAMES THE OPPOSITE BONE. Hina is VRoid-ish:
# Left_arm / Left_elbow / Left_leg / Left_knee, with a numeric suffix. Note what that means:
# in the MIXAMO family `LeftLeg` is the LOWER leg and the upper is `LeftUpLeg`; in THIS family
# `Left_leg` is the UPPER leg and the lower is `Left_knee`. Normalising the punctuation away
# to "match loosely" would therefore bind the trousers' knee ring to her thigh on one body and
# her shin on the other, and nothing would report an error.
# ⭐ The UNDERSCORE is what keeps them apart -- "leftleg" cannot match "left_leg_103" and
# "left_leg" cannot match "LeftLeg" -- so each family's names go in verbatim and the substring
# test stays exact. Ugly, and safe.
WANT = [
    ["pelvis", "hips"],
    ["spine_01", "spine01", "spine"],
    ["spine_02", "spine02", "spine1"],
    ["spine_03", "spine03", "spine2", "chest"],
    ["clavicle_l", "leftshoulder", "left_shoulder"],
    ["clavicle_r", "rightshoulder", "right_shoulder"],
    ["thigh_l", "leftupleg", "leg_l", "left_leg"],
    ["calf_l", "leftleg", "left_knee"],
    ["thigh_r", "rightupleg", "leg_r", "right_leg"],
    ["calf_r", "rightleg", "right_knee"],
    ["upperarm_l", "leftarm", "left_arm"],
    ["upperarm_r", "rightarm", "right_arm"],
    # the joint whose absence flew the sleeves off. ⛔ `find()` is a SUBSTRING match, but
    # "leftarm" cannot match "leftforearm" (l-e-f-t-f-o-r-e...), so the order is safe.
    ["lowerarm_l", "leftforearm", "left_elbow"],
    ["lowerarm_r", "rightforearm", "right_elbow"],
]


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


def inv_rigid(m):
    """inverse of a rotation+uniform-scale+translation matrix"""
    sx = math.sqrt(m[0] ** 2 + m[1] ** 2 + m[2] ** 2) or 1.0
    r = [m[i] / sx for i in range(12)]
    o = [0.0] * 16
    o[0], o[4], o[8] = r[0], r[1], r[2]
    o[1], o[5], o[9] = r[4], r[5], r[6]
    o[2], o[6], o[10] = r[8], r[9], r[10]
    for i in range(12):
        o[i] /= sx
    t = (m[12], m[13], m[14])
    o[12] = -(o[0] * t[0] + o[4] * t[1] + o[8] * t[2])
    o[13] = -(o[1] * t[0] + o[5] * t[1] + o[9] * t[2])
    o[14] = -(o[2] * t[0] + o[6] * t[1] + o[10] * t[2])
    o[15] = 1.0
    return o


def seg_dist(p, a, b):
    ab = [b[i] - a[i] for i in range(3)]
    ap = [p[i] - a[i] for i in range(3)]
    L = sum(v * v for v in ab)
    t = 0.0 if L < 1e-9 else max(0.0, min(1.0, sum(ap[i] * ab[i] for i in range(3)) / L))
    c = [a[i] + ab[i] * t for i in range(3)]
    return math.sqrt(sum((p[i] - c[i]) ** 2 for i in range(3)))


def main():
    R = Rpack(SRC)
    nodes = R.nodes()
    names = [n["name"] for n in nodes]

    # ---- rest world matrices -------------------------------------------------------
    W = [None] * len(nodes)

    def world(i):
        if W[i] is None:
            loc = trs(nodes[i]["t"], nodes[i]["q"], nodes[i]["s"])
            p = nodes[i]["parent"]
            W[i] = loc if p < 0 else m4_mul(world(p), loc)
        return W[i]
    for i in range(len(nodes)):
        world(i)

    def find(frag):
        lo = [n.lower() for n in names]
        for i, n in enumerate(lo):
            if frag in n:
                return i
        return -1

    bones = []
    for alts in WANT:
        for frag in alts:
            i = find(frag)
            if i >= 0 and i not in bones:
                bones.append(i)
                break
    if len(bones) < 4:
        print("!! only %d bones resolved -- the rig's names are not what was expected" % len(bones))
        return 1
    print("skinning to %d bones: %s" % (len(bones), ", ".join(names[i] for i in bones)))

    # ---- HER OWN SILHOUETTE, MEASURED PER LIMB -------------------------------------
    # This section had three separate bugs and the four-angle bench (ridecheck.py, on
    # sets/pjcheck.set) showed all three at once the moment the garment was ISOLATED --
    # judged next to reva in a two-shot it was just "a bit off".
    #
    # 1. WRONG FRAME. It used mesh_positions(), and rpack.py says in its own docstring:
    #    "For measuring bodies use skinned_positions." A UE-family buffer can sit a metre
    #    out of rig space, so the garment was measured in one frame and worn in another.
    #
    # 2. A HORIZONTAL SLICE IS NOT A BODY. The old shell took a PERCENTILE of every vertex
    #    in a Y band, and spent three comments arguing with itself about which percentile
    #    excluded her arms -- 72nd caught wrists, 80th caught them too, and her HAIR is in
    #    those bands as well. No percentile separates a torso from a wrist, because the
    #    answer is not in the geometry. ⭐ IT IS IN THE WEIGHTS: every vertex already knows
    #    which bone owns it, so classify the bone once and a torso band contains torso and
    #    nothing else, exactly, on any rig. That is why this is 2.06 units narrower.
    #
    # 3. FAKE NORMALS -- every vertex was authored (0,1,0), which is why it rendered as a
    #    flat brown cut-out with no shading on it anywhere.
    sp = R.skinned_positions(with_bones=True)
    HEAD_K = ("head", "neck", "hair", "ear", "eye", "jaw", "tongue", "teeth", "face")
    ARM_K = ("clavicle", "shoulder", "upperarm", "forearm", "hand", "arm", "finger", "thumb")
    LEG_K = ("thigh", "calf", "upleg", "leg", "knee", "shin", "foot", "toe", "ankle")
    TOR_K = ("pelvis", "hips", "spine", "chest", "breast", "waist", "torso")

    gcache = {}

    def group(ni):
        """Walk up the chain until a name says what this is. Order matters: LeftForeArm
        is an arm before it is anything else, LeftUpLeg is a leg and not an 'arm'."""
        if ni in gcache:
            return gcache[ni]
        i, hops = ni, 0
        out = "other"
        while i >= 0 and hops < 32:
            n = names[i].lower()
            # ⛔ ARM BEFORE HEAD: "forearm" contains "ear", so LeftForeArm and
            # RightForeArm were both classifying as HEAD.
            if any(k in n for k in ARM_K):  out = "arm";  break
            if any(k in n for k in LEG_K):  out = "leg";  break
            if any(k in n for k in HEAD_K): out = "head"; break
            if any(k in n for k in TOR_K):  out = "torso"; break
            i = nodes[i]["parent"]; hops += 1
        gcache[ni] = out
        return out

    def side(ni):
        """left / right / mid. Mixamo prefixes (LeftUpLeg), UE suffixes (thigh_l)."""
        n = names[ni].lower()
        if "left" in n or n.endswith("_l") or n.endswith(".l"):  return "l"
        if "right" in n or n.endswith("_r") or n.endswith(".r"): return "r"
        p = nodes[ni]["parent"]
        return side(p) if p >= 0 else "m"

    # ⭐ THE ARMHOLES, MEASURED OFF THE ARMS THEMSELVES. Vertices the WEIGHTS assign to an
    # arm, near that arm's root, and their own spread is the radius. A thick arm gets a big
    # hole and a thin one does not, on any body, with nothing to tune -- the same rule the
    # rest of this file already follows.
    # the cap is a fraction of the BODY, so it scales to any rig
    _ys = [p[1] for p in sp]
    ARMHOLE_MAX = (max(_ys) - min(_ys)) * 0.040
    armv = [p for p in sp if p[3] >= 0 and group(p[3]) == "arm"]
    holes = []
    for sd in ("l", "r"):
        a1 = [p for p in armv if side(p[3]) == sd]
        if len(a1) < 40:
            continue
        top = sorted(a1, key=lambda p: -p[1])[: max(20, len(a1) // 8)]
        cx = sum(p[0] for p in top) / len(top)
        cy = sum(p[1] for p in top) / len(top)
        cz = sum(p[2] for p in top) / len(top)
        rs = sorted(math.sqrt((p[0]-cx)**2 + (p[1]-cy)**2 + (p[2]-cz)**2) for p in top)
        r = rs[int(len(rs) * 0.80)] * 1.45
        # ⛔ AND CAP IT, because this is the measure-from-a-POINT bug for the third time:
        # the spread of a vertex cloud around its own centroid includes how far the cloud
        # EXTENDS, not how thick it is. It returned 12.0 on a torso tube whose bands are 2.8
        # apart -- a hole eight bands tall, which does not make an armhole, it CUTS THE SHIRT
        # IN HALF. The solver then reported 14 connected pieces and pinned the top of each,
        # so a dozen loose flaps hung off her.
        r = min(r, ARMHOLE_MAX)
        holes.append((cx, cy, cz, r))
        print("armhole %s at %.1f,%.1f,%.1f r %.1f" % (sd, cx, cy, cz, r))

    tors = [p for p in sp if p[3] >= 0 and group(p[3]) == "torso"]
    legsv = [p for p in sp if p[3] >= 0 and group(p[3]) == "leg"]
    legL = [p for p in legsv if side(p[3]) == "l"]
    legR = [p for p in legsv if side(p[3]) == "r"]
    if len(tors) < 50 or len(legsv) < 50:
        print("!! torso %d / leg %d verts -- the rig's names did not classify" % (len(tors), len(legsv)))
        return 1
    body = tors + legsv
    y0, y1 = min(p[1] for p in body), max(p[1] for p in body)
    print("body spans Y %.2f .. %.2f  (torso %d verts, legs %d)" % (y0, y1, len(tors), len(legsv)))

    hip = world(bones[0])[13]
    BANDS = 18
    verts, norms, idx = [], [], []

    def shell(src, ylo, yhi, bands, seg, pad, slack=1.0, arc=None):
        """arc=(a0,a1) in radians builds a PANEL over that angular range instead of a
        closed ring -- which is what makes a front and a back instead of a tube. Returns
        (base, bands, nseg) so the caller can sew the open edges together."""
        """A tube around ONE named part of the body. `pad` is real clearance in rig units
        and `slack` a proportional one -- together they are what 'baggy' means. The shape
        barely matters now: the garment is SIMULATED, so it is built loose and gravity and
        the body decide the rest. That is the whole reason four static fits failed."""
        base = len(verts)
        a0, a1 = arc if arc else (0.0, 0.0)
        nseg = seg
        h = (yhi - ylo) / bands
        last = [0.0]
        lastring = [None]
        lastc = [sum(p[0] for p in src) / len(src), sum(p[2] for p in src) / len(src)]
        for b in range(bands + 1):
            yy = ylo + (yhi - ylo) * (b / float(bands))
            band = [p for p in src if abs(p[1] - yy) < h * 1.25]
            # ⛔ A TORSO IS NOT A CIRCLE, and one radius per band cannot describe one. At
            # chest height her median radius is 12.1 and her 97th percentile is 18.6 --
            # the same band, because she is deeper front-to-back than she is wide. Build
            # at the median and the garment sinks into her chest; build at the 97th (which
            # is what this did) and the whole thing inflates into a tent. Both were tried
            # on the bench and both are visible in the sheets.
            # ⭐ MEASURE EACH DIRECTION SEPARATELY. One radius per SEGMENT, from the
            # vertices actually lying in that direction, and the tube takes the shape of
            # the body instead of the shape of a circle drawn around it.
            ring = [0.0] * seg
            if band:
                cx = sum(p[0] for p in band) / len(band)
                cz = sum(p[2] for p in band) / len(band)
                lastc[0], lastc[1] = cx, cz
                buckets = [[] for _ in range(seg)]
                for p in band:
                    dx, dz = p[0] - cx, p[2] - cz
                    r = math.sqrt(dx * dx + dz * dz)
                    if r < 1e-6:
                        continue
                    k = int(((math.atan2(dz, dx) % (2 * math.pi)) / (2 * math.pi)) * seg) % seg
                    buckets[k].append(r)
                allr = sorted(r for bk in buckets for r in bk)
                med = allr[len(allr) // 2] if allr else last[0]
                for k in range(seg):
                    bk = sorted(buckets[k])
                    # 85th within one direction: past the skin, under a stray vertex
                    ring[k] = bk[min(len(bk) - 1, int(len(bk) * 0.85))] if bk else med
                # a garment does not have a dent in it: smooth the ring around the circle
                ring = [(ring[(k - 1) % seg] + 2.0 * ring[k] + ring[(k + 1) % seg]) / 4.0
                        for k in range(seg)]
                last[0] = med
                lastring[0] = list(ring)
            else:
                # ⛔ AN EMPTY BAND IS NOT A RADIUS OF ZERO. That collapsed the end of every
                # tube to a single point on the axis -- a cone, not a hem.
                cx, cz = lastc[0], lastc[1]
                ring = list(lastring[0]) if lastring[0] else [last[0]] * seg
            for k in range(nseg):
                a = (a0 + (a1 - a0) * k / float(nseg - 1)) if arc else (2 * math.pi * k / seg)
                ca, sa = math.cos(a), math.sin(a)
                # the measured ring is indexed by the FULL circle, so an arc samples into it
                rk = int((a % (2 * math.pi)) / (2 * math.pi) * seg) % seg
                rr = ring[rk] * slack + pad
                verts.append((cx + rr * ca, yy, cz + rr * sa))
                norms.append((ca, 0.0, sa))          # outward, not straight up
        for b in range(bands):
            for k in range(nseg if arc else seg):
                if arc and k == nseg - 1:
                    break            # an arc does not close on itself -- that is the point
                p0 = base + b * nseg + k
                p1 = base + b * nseg + ((k + 1) % nseg)
                idx.append((p0, p1, p1 + nseg))
                idx.append((p0, p1 + nseg, p0 + nseg))
        return (base, bands, nseg)

    def sew(panelA, colA, panelB, colB, b0, b1):
        """⭐ SEW two panel edges together over a band range -- and DO NOT sew the bands
        that make the armhole. This is the whole trick: the armhole is where the seam STOPS,
        not a hole cut in a tube, which is why a real shirt survives having one and my tube
        did not.
        The seam is real geometry, so the solver gets it for free: Cloth builds its distance
        constraints from TRIANGLE EDGES, and a sewn seam is a strip of triangles."""
        baseA, bandsA, segA = panelA
        baseB, bandsB, segB = panelB
        n = 0
        for b in range(b0, min(b1, bandsA, bandsB)):
            a_lo = baseA + b * segA + colA
            a_hi = baseA + (b + 1) * segA + colA
            b_lo = baseB + b * segB + colB
            b_hi = baseB + (b + 1) * segB + colB
            idx.append((a_lo, b_lo, b_hi))
            idx.append((a_lo, b_hi, a_hi))
            n += 2
        return n

    def graft(ring, pool):
        """⭐ SEW A SLEEVE INTO THE ARMHOLE.

        The armhole was a gap between two FREE EDGES with nothing closing the loop, so both
        sides splayed into rectangular flaps hanging off her hips and a hole opened in her
        lower back. A real garment has no free edge there.
        ⛔ The sleeve's shoulder ring and the armhole boundary have different vertex counts
        and no shared parameterisation, so they cannot be zipped index to index. PROXIMITY is
        what they do share: each sleeve vertex takes its nearest torso vertex, and consecutive
        pairs make the seam. Nothing has to be authored and it cannot get out of step.
        """
        if not ring or not pool:
            return 0
        near = []
        for vi in ring:
            p = verts[vi]
            best, bj = 1e18, pool[0]
            for wj in pool:
                q = verts[wj]
                d = (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 + (p[2] - q[2]) ** 2
                if d < best:
                    best, bj = d, wj
            near.append(bj)
        n = 0
        for k in range(len(ring)):
            a0, a1 = ring[k], ring[(k + 1) % len(ring)]
            b0, b1 = near[k], near[(k + 1) % len(ring)]
            if b0 == b1:
                idx.append((a0, a1, b0))
                n += 1
            else:
                idx.append((a0, a1, b1))
                idx.append((a0, b1, b0))
                n += 2
        return n

    def sew_row(panelA, panelB, band, j0, j1):
        """⭐ THE SHOULDER SEAM, which runs ALONG the top edge rather than down the side.
        Going around the body the columns read front[0..n-1] then back[0..n-1], so front's
        LAST column sits beside back's FIRST: pairing front[n-1-j] with back[j] walks
        outward across one shoulder, and front[j] with back[n-1-j] across the other.
        ⛔ And the columns this does NOT reach are not an oversight -- they are the NECK."""
        baseA, bandsA, segA = panelA
        baseB, bandsB, segB = panelB
        n = 0
        ra = baseA + band * segA
        rb = baseB + band * segB
        for j in range(j0, j1 - 1):
            # one shoulder: front's tail against back's head
            idx.append((ra + segA - 1 - j, rb + j, rb + j + 1))
            idx.append((ra + segA - 1 - j, rb + j + 1, ra + segA - 2 - j))
            # the other: front's head against back's tail
            idx.append((ra + j, rb + segB - 1 - j, rb + segB - 2 - j))
            idx.append((ra + j, rb + segB - 2 - j, ra + j + 1))
            n += 4
        return n

    # ⛔ AND THE HEIGHTS COME OFF THE PARTS, NOT OFF A FRACTION OF THE WHOLE BODY.
    # The old numbers were fractions of (max Y - min Y) tuned in a different coordinate
    # frame: in THIS one they built the "top" from y -19 to 12.8 while her torso actually
    # spans -13.2 to 41.7 -- a short tube around her hips that began six units below her
    # body, with every band under it empty. Each group knows its own extent; ask it.
    # ⛔ A GARMENT IS A TUBE WITH HOLES IN IT. The top was a CLOSED tube, so it bridged
    # straight across the armpits and the arms ended up INSIDE the shirt -- which on the
    # four-angle bench reads as bulk around the chest and a body with no arms coming out of
    # it. Named twice as "topology, not a number" and not fixed until now.
    # ⭐ AND IT IS A FACE FILTER, NOT A REMODEL. The tube is already the right shape; it
    # just has faces where holes belong. The vertices stay, and the solver takes its
    # connectivity from the EDGES of the surviving faces, so the hole is real to the
    # simulation too and not only to the renderer.
    def cut_armholes():
        if not holes:
            print("   (no armholes -- the arm groups did not resolve)")
            return
        keep, lost = [], 0
        for tri in idx:
            cx = sum(verts[i][0] for i in tri) / 3.0
            cy = sum(verts[i][1] for i in tri) / 3.0
            cz = sum(verts[i][2] for i in tri) / 3.0
            hit = any((cx-hx)**2 + (cy-hy)**2 + (cz-hz)**2 < hr*hr for (hx,hy,hz,hr) in holes)
            lost += hit
            if not hit:
                keep.append(tri)
        print("armholes: %d of %d torso faces removed" % (lost, len(idx)))
        idx[:] = keep

    tlo, thi = min(p[1] for p in tors), max(p[1] for p in tors)
    llo, lhi = min(p[1] for p in legsv), max(p[1] for p in legsv)
    tspan, lspan = thi - tlo, lhi - llo
    print("torso Y %.1f..%.1f   legs Y %.1f..%.1f" % (tlo, thi, llo, lhi))
    # a pyjama top: hangs a little below the waist, stops just under the shoulder
    # ⛔ AND "BAGGY" IS A SMALL NUMBER. slack 1.10 + a 3.5% pad on top of a per-sector
    # silhouette is not a loose shirt, it is a tent -- the bench read it as a floor-length
    # gown. Real pyjamas clear the body by a couple of centimetres, and the FOLDS come
    # from the solver, not from building it oversized.
    # ⭐ LOOSE, now that it can be. jacob: "threadbare, sheer, loose, old, comfortable".
    # The clearance went UP rather than down, which was only safe once the solver started
    # colliding with a MEASURED body instead of six guessed spheres -- loose fabric with
    # bad collision is just fabric inside a torso.
    # ⭐ AND NOW IT CAN ACTUALLY BE LOOSE. Every previous attempt at looseness became a
    # TENT, and the reason was not the clearance -- it was that the collision capsules
    # were 3-5x too big (they measured limb LENGTH, not thickness) and were already
    # inflating the garment before the clearance was added. With the radii fixed, real
    # slack is available for the first time: 1.07 was a catsuit on a T-pose.
    # ⭐ THE SHIRT IS TWO PANELS. Front over the front half, back over the back half, a gap
    # at each side -- and that gap IS the armhole. Sewn at the shoulder above it and down the
    # ribs below it; nothing is ever cut.
    # ⛔ AND THE TARGET WAS AIMED AT THE WRONG END. Driving the median standoff to 0.018
    # rig units to answer "sticking way too far off of her body" produced a CATSUIT -- at
    # close range that is skin-tight. Loose-but-not-a-tent is +0.035..0.045. Tent -> catsuit
    # is the third overshoot of this kind in one session; the fix is to aim at the BAND, not
    # at the far side of whichever complaint arrived last.
    # ⭐ THE CLEARANCE NUMBERS COME OFF fitcheck.py, NOT OFF A FEELING. It reports the
    # gap between garment and skin PER BONE, and it convicted the PAD rather than the
    # slack I had spent six rounds raising: at tspan*0.055 the pad ALONE was 0.054 rig
    # units when the whole target clearance is 0.035, so the garment could not be
    # anything but a tent whatever the slack said. Small pad, modest slack.
    # ⭐ jacob: "id say your material is still way too thick". Two causes, both real:
    # the clearance, and the RESOLUTION -- a 16-segment ring around a leg cannot fold,
    # and a tube that cannot fold reads as foam however thin you make it. A fold costs
    # two segments, so coarse fabric is stiff fabric no matter what the solver does.
    TSEG = 24                       # around the torso
    LSEG = 16                       # around each trouser leg
    GAP = 0.30                      # radians of open side, each side
    t_lo = lhi - tspan * 0.055
    t_hi = thi - tspan * 0.07
    # ⛔ THE COMPASS. shell() places at cos(a)->X, sin(a)->Z, so angle 0 is +X -- and
    # CLUB FORWARD IS +Z. Centring the "front" panel on angle 0 put it on her RIGHT SIDE,
    # the "back" panel on her left, and both ARMHOLE GAPS on her chest and her spine. She
    # rendered bare-chested with blue at the flanks, and the piece dump insisted the shirt
    # was present and correctly centred -- it was; it was just rotated 90 degrees.
    # Made twice in one session: fitcheck's first table called +X "front" too.
    front = shell(tors, t_lo, t_hi, BANDS, TSEG, tspan * 0.017, 1.20,
                  arc=(GAP, math.pi - GAP))                        # centred on +Z
    back = shell(tors, t_lo, t_hi, BANDS, TSEG, tspan * 0.017, 1.20,
                 arc=(math.pi + GAP, 2 * math.pi - GAP))           # centred on -Z
    # the armhole occupies the upper third of the side; sew above it and below it
    # ⛔ 0.62 LEFT THE TOP 38% OF EACH FLANK OPEN -- mid-ribs to shoulder. That is a
    # slit up the side of a dress, not an armhole. An armhole is a WINDOW.
    ARM_B0 = int(BANDS * 0.80)          # the armhole starts at the armpit
    SEG = front[2]
    NECK = int(SEG * 0.42)              # sewn in from each END; the rest IS the neck hole
    ns = 0
    ns += sew(front, SEG - 1, back, 0, 0, ARM_B0)          # side seam, one side
    ns += sew(back, back[2] - 1, front, 0, 0, ARM_B0)      # side seam, the other
    ns += sew_row(front, back, BANDS, 0, NECK)             # both shoulders, along the top
    print("shirt: front+back panels, %d seam faces; armhole = bands %d..%d at the sides, "
          "neck = the %d middle columns of the top row"
          % (ns, ARM_B0, BANDS, SEG - 2 * NECK))
    # ⛔ AND TWO TROUSER LEGS, NOT ONE TUBE. "the bottoms ... as one loose piece" was a
    # deliberate simplification and it read on the bench as a floor-length SKIRT -- one
    # sleeve around both legs cannot show a stride, and the solver had nothing to hold it
    # up so it pooled. Each leg is measured against its OWN vertices, so the tube follows
    # that thigh and that calf, and each is its own connected piece with its own
    # waistband pin.
    # trousers: ankle (above the sole, not around the foot) to a waistband that overlaps
    # the top, so the pinned ring sits on the hip rather than in mid-air
    # ⭐ SLEEVES. A pyjama top without them is a sack, and this generator has never made
    # them -- a torso tube, two trouser tubes and bare arms. That is a tabard.
    # ⛔ AN ARM IS NOT VERTICAL, so shell() cannot build one: it walks bands up Y, which is
    # right for a leg and useless for an arm held out from the body, where the whole limb
    # can fall inside two Y bands. A sleeve is built along the arm's OWN AXIS.
    def sleeve(src, seg, pad, slack, t0=0.06, t1=0.96):
        if len(src) < 60:
            return
        # the axis: shoulder end is the vertex cloud's top, wrist end its far extreme
        top = sorted(src, key=lambda p: -p[1])[: max(12, len(src) // 10)]
        a0 = [sum(p[i] for p in top) / len(top) for i in range(3)]
        far, fd = a0, -1.0
        for p in src:
            d = (p[0]-a0[0])**2 + (p[1]-a0[1])**2 + (p[2]-a0[2])**2
            if d > fd:
                fd, far = d, (p[0], p[1], p[2])
        ax = [far[i] - a0[i] for i in range(3)]
        L = math.sqrt(sum(v*v for v in ax)) or 1.0
        ax = [v / L for v in ax]
        # an orthonormal frame across the arm
        up = [0.0, 1.0, 0.0] if abs(ax[1]) < 0.9 else [1.0, 0.0, 0.0]
        e1 = [ax[1]*up[2]-ax[2]*up[1], ax[2]*up[0]-ax[0]*up[2], ax[0]*up[1]-ax[1]*up[0]]
        n1 = math.sqrt(sum(v*v for v in e1)) or 1.0
        e1 = [v / n1 for v in e1]
        e2 = [ax[1]*e1[2]-ax[2]*e1[1], ax[2]*e1[0]-ax[0]*e1[2], ax[0]*e1[1]-ax[1]*e1[0]]
        # project every arm vertex onto the axis once
        proj = []
        for p in src:
            d = [p[i] - a0[i] for i in range(3)]
            t = sum(d[i]*ax[i] for i in range(3))
            u = sum(d[i]*e1[i] for i in range(3))
            v = sum(d[i]*e2[i] for i in range(3))
            proj.append((t, u, v))
        base = len(verts)
        bands = 10
        h = L * (t1 - t0) / bands
        lastr = [0.0] * seg
        for b in range(bands + 1):
            t = L * (t0 + (t1 - t0) * b / float(bands))
            band = [q for q in proj if abs(q[0] - t) < h * 1.3]
            ring = list(lastr)
            if band:
                cu = sum(q[1] for q in band) / len(band)
                cv = sum(q[2] for q in band) / len(band)
                buck = [[] for _ in range(seg)]
                for q in band:
                    du, dv = q[1] - cu, q[2] - cv
                    r = math.sqrt(du*du + dv*dv)
                    if r < 1e-6:
                        continue
                    k = int(((math.atan2(dv, du) % (2*math.pi)) / (2*math.pi)) * seg) % seg
                    buck[k].append(r)
                allr = sorted(r for bk in buck for r in bk)
                med = allr[len(allr)//2] if allr else 0.0
                for k in range(seg):
                    bk = sorted(buck[k])
                    ring[k] = bk[min(len(bk)-1, int(len(bk)*0.85))] if bk else med
                ring = [(ring[(k-1) % seg] + 2.0*ring[k] + ring[(k+1) % seg]) / 4.0
                        for k in range(seg)]
                lastr = list(ring)
            for k in range(seg):
                ang = 2*math.pi*k/seg
                rr = ring[k]*slack + pad
                du, dv = math.cos(ang)*rr, math.sin(ang)*rr
                verts.append(tuple(a0[i] + ax[i]*t + e1[i]*(cu+du) + e2[i]*(cv+dv)
                                   for i in range(3)))
                nx = [e1[i]*math.cos(ang) + e2[i]*math.sin(ang) for i in range(3)]
                norms.append(tuple(nx))
        for b in range(bands):
            for k in range(seg):
                p0 = base + b*seg + k
                p1 = base + b*seg + (k+1) % seg
                idx.append((p0, p1, p1+seg))
                idx.append((p0, p1+seg, p0+seg))
        return [base + k for k in range(seg)]      # the shoulder ring, for graft()

    # ⛔ NO CUT. The armhole is the unsewn gap between the panels; cutting one out of a
    # closed tube is what fragmented the shirt into fourteen pieces.
    # ⭐ SLEEVES GO ON AFTER THE HOLES ARE CUT, so the cut cannot eat them.
    # ⛔ THE ARM GROUP IS NOT THE ARM. group() calls clavicle, shoulder, upperarm, forearm,
    # hand, fingers and thumb all "arm" -- so the cloud has the SHOULDER MASS at one end and
    # a splayed HAND at the other, and a radius measured across it is neither. The first
    # sleeves came out enormous and swallowed her. Sleeve on upperarm+forearm only.
    SLEEVE_K = ("upperarm", "forearm", "arm")
    NOT_SLEEVE = ("hand", "finger", "thumb", "clavicle", "shoulder")
    def sleevable(ni):
        n = names[ni].lower()
        if any(k in n for k in NOT_SLEEVE):
            return False
        return any(k in n for k in SLEEVE_K)
    armL = [p for p in armv if side(p[3]) == 'l' and sleevable(p[3])]
    armR = [p for p in armv if side(p[3]) == 'r' and sleevable(p[3])]
    print("sleeve verts: L %d  R %d" % (len(armL), len(armR)))
    # ⛔ NO GRAFT -- IT FLEW A SLEEVE OFF HER ARM. Sewing the sleeves into the armhole
    # is topologically right and it is what a real garment does, but clothInit pins THE TOP
    # 8% OF EACH PIECE: while a sleeve is its own piece that 8% is a strip running the whole
    # length of the sleeve, and it is the only thing holding it on the arm. Grafting merged
    # all three into one piece spanning the whole torso, so the pins moved to the shoulders
    # and the sleeves hung from nothing. graft() is kept below, unused, because the idea is
    # correct and cheap to restore -- it needs an ANATOMICAL pin rule first, not one that
    # depends on a piece's bounding box.
    for av in (armL, armR):
        sleeve(av, 12, tspan * 0.020, 1.26, t0=-0.14, t1=0.86)
        # ⛔ t0 IS MEASURED FROM THE DELTOID, NOT THE SHOULDER JOINT. The sleeve's axis
        # starts at the centroid of the arm cloud's top slice, which sits partway down the
        # upper arm -- so a t0 of 0.06 began the sleeve BELOW where the shirt ends and left
        # a ring of bare skin at each shoulder. Negative t0 runs back up the axis to meet
        # the armhole.
        # ⛔ THE RIGHT SLEEVE IS BUILT FROM A TENTH OF THE CLOUD THE LEFT IS: reva2's
        # weights are 4x asymmetric (2995 arm verts classified left, 276 right), so the
        # right ring measures small and fitcheck caught it as the only NEGATIVE gap on
        # the body. The extra slack covers her asymmetry rather than pretending it away.
    print('sleeves: %d verts total' % len(verts))
    if len(legL) > 50 and len(legR) > 50:
        for lv in (legL, legR):
            # her leg centres are 16.3 rig units apart, so a tube wider than ~8 merges
            # with its twin and the two legs read as one skirt -- which is exactly what
            # slack 1.14 produced
            shell(lv, llo + lspan * 0.07, lhi + tspan * 0.16, BANDS, LSEG, lspan * 0.016, 1.22)
    else:
        print("   (no left/right split resolved -- falling back to one piece)")
        shell(legsv, llo + lspan * 0.07, lhi + tspan * 0.05, BANDS, 16, lspan * 0.024, 1.12)
    print("garment: %d verts, %d tris" % (len(verts), len(idx)))

    # ---- weights: two nearest bone segments ---------------------------------------
    segs = []
    for bi in bones:
        wm = world(bi)
        a = (wm[12], wm[13], wm[14])
        kids = [j for j in range(len(nodes)) if nodes[j]["parent"] == bi]
        if kids:
            km = world(kids[0])
            b = (km[12], km[13], km[14])
        else:
            b = (a[0], a[1] + 1e-3, a[2])
        segs.append((a, b))

    J, WT = [], []
    for v in verts:
        d = sorted(((seg_dist(v, s[0], s[1]), k) for k, s in enumerate(segs)))[:2]
        w = [1.0 / max(1e-4, dd) ** 2 for (dd, _) in d]
        tot = sum(w) or 1.0
        w = [x / tot for x in w]
        J.append((d[0][1], d[1][1], 0, 0))
        WT.append((int(w[0] * 255), 255 - int(w[0] * 255), 0, 0))

    # ---- append as a new mesh ------------------------------------------------------
    buf = bytearray(R.buf)
    def put(b):
        o = len(buf)
        buf.extend(b)
        return o

    offPos = put(b"".join(struct.pack("<3f", *v) for v in verts))
    offNorm = put(b"".join(struct.pack("<3f", *n) for n in norms))
    offJ = put(b"".join(struct.pack("<4B", *j) for j in J))
    offW = put(b"".join(struct.pack("<4B", *w) for w in WT))
    offI = put(b"".join(struct.pack("<3I", *t) for t in idx))
    # ⛔ THE BONE MAP IS NODE INDICES, NOT SLOT INDICES. This wrote range(len(bones)) --
    # 0,1,2..8 -- while the inverse binds below are built from the REAL bones (Hips,
    # Spine01, Spine02, LeftUpLeg, ...). The engine forms palette[b] = world[boneMap[b]] x
    # inverseBind[b], so every garment vertex was driven by one bone's world matrix and a
    # different bone's inverse bind. The rest palette should be the identity; it was out by
    # up to 111 units, which is why the garment arrived as a giant shapeless mass with its
    # top flung somewhere off the body -- and why FOUR STATIC FITS before the solver failed
    # too. Not a fit problem. Never was.
    offBM = put(b"".join(struct.pack("<H", bi) for bi in bones))
    ident = struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    offIBM = put(b"".join(struct.pack("<16f", *inv_rigid(world(bi))) for bi in bones))
    offBind = put(ident)

    strs = R.strings()
    nameId = len(strs)
    # ⭐ THE NAME CARRIES THE COLOUR. The engine reads `#rrggbb` off an untextured
    # mesh name, so a garment brings its own without the pack format needing a
    # material it does not have. A soft blue, because it is four in the morning.
    strs = strs + [("pyjamas~cloth#%s" % COL).encode("ascii")]

    old = R.buf[R.offMeshes: R.offMeshes + R.nMeshes * 60]
    newmesh = struct.pack("<15I", nameId, 0, len(verts), len(idx) * 3, len(bones),
                          1 | 4, offPos, offNorm, 0, offJ, offW, offI, offBM, offIBM, offBind)
    offMeshes = put(old + newmesh)

    blob = b""
    ents = []
    for t in strs:
        ents.append((len(blob), len(t)))
        blob += t
    offStr = put(struct.pack("<I", len(strs))
                 + b"".join(struct.pack("<II", o, l) for (o, l) in ents) + blob)

    total = len(buf)
    # ⛔ FIELD 14 IS THE MORPH TABLE (rpack.py calls it reserved). This wrote 0 there, so dressing
    # any pack with a face -- kaeli's 92 shapes -- would have dropped every morph in silence. Kept.
    # (found ep 14 while writing dresspack.py, which keeps it too)
    morphs = struct.unpack_from("<I", R.buf, 4 + 14 * 4)[0]
    hdr = b"RPK1" + HDR.pack(R.ver, R.nNodes, R.nMeshes + 1, R.nMats, R.nClips, R.nTex,
                             R.offNodes, offMeshes, R.offMats, R.offClips,
                             R.offTexTable, offStr, R.offSize, total, morphs)
    buf[0:4 + HDR.size] = hdr
    io.open(DST, "wb").write(bytes(buf))
    print("wrote %s  (%d meshes, %.1f MB)" % (DST, R.nMeshes + 1, len(buf) / 1e6))

    # ⭐ VERIFY WHAT WE JUST WROTE, before anyone looks at a pixel. The bone map bug that
    # cost four static fit attempts, a whole physics solver and five rounds of "the
    # garment looks wrong" is one matrix multiply to detect: against a pack's OWN rest
    # skeleton, world[boneMap[b]] x inverseBind[b] must be the identity. packcheck --bind
    # is exactly that assertion, and it is sound HERE because we authored this binding.
    import packcheck
    # ⛔ POINT THE STRICT BIND ASSERTION AT THE MESH WE AUTHORED, AND ONLY THAT ONE. The
    # promotion to FAIL is sound for our own binding and not for the donor's: the beautiful
    # girl's pack disagrees with world[boneMap[b]] by 4729% on `mixamorig:LeftHand` BEFORE
    # this generator touches it, and an unscoped check refused to ship a perfectly good
    # garment over somebody else's bake. reva2 happened to pass, which is why it took a
    # second donor to find.
    packcheck.BIND_SEV = "FAIL"
    mine = Rpack(DST).nMeshes - 1          # the garment is appended last
    bad = [m for sev, m in packcheck.check(DST, only=mine) if sev == "FAIL"]
    for m in bad:
        print("   !! %s" % m)
    print("packcheck: %s" % ("FAILED -- do not ship this pack" if bad else "clean"))
    if bad:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
