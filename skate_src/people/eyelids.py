"""eyelids.py -- THE WORLD AT THE END OF TIME's faces CLOSE THEIR EYES: a blink morph per eye on every cast member's pack.

jacob, 10-18: "make the boys' huddle in the milk and Viktor waking in the pan" -- and a boy asleep in a pan, or waking,
needs eyes that SHUT. The toon face (toonface.py) is shapes a hair above the skin: the eye's white, the iris and pupil
clipped to it, the upper lid's heavy ink line. A closed eye, drawn that way, is the upper lid's ink lying ON the lower
lid -- so the morph moves points, it adds none:

  - the eye's INTERIOR (white, iris, pupil, catchlight) squashes onto the lower lid's curve -- a sliver under the ink;
  - the UPPER LID'S INK slides down by the opening's height at that point, keeping its own width (a squashed line would
    thin to nothing) -- at the corners, where the opening is zero, nothing moves;
  - the lower lid's line, the crease, the brows, the lines of age: untouched. ⛔ The lid inks and the brows are told
    apart by their OFFSET (toonface: lid inks 0.0015 out, brows 0.0016), never by distance from the lid: a low, heavy
    brow (Billy's) lies within half an opening of the upper lid, and the first build slid its lower edge down onto the
    shut lid -- a dark wedge that read as open eyes from across the room.

Each face vertex is put back into FACE COORDINATES (a = degrees round the head, u = share of head height, off = how far
out from the skin) by inverting person.face_xyz numerically, moved there, and mapped forward again -- so a closed lid
lies on the head's own surface at its own offset, never through the skin.

    blender --background --factory-startup --python-exit-code 1 --python skate_src/people/eyelids.py -- [--only viktor,billy] [--redo 1]

`--redo 1` drops a pack's blink morphs and builds them again (morphs.drop_morphs; the old deltas stay as orphans).

Morphs `blink_L`, `blink_R` (the body's left = +a) go into reva_native/packs/v1_<name>.rpack (a pack carrying the cast's
authored clips is first put back to its clipless base -- skate_src/people/moves/bake.py re-appends them). A clip drives
them with type-3 tracks (one float a frame per morph index).
"""
import math, os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "atelier"))
sys.path.insert(0, os.path.join(REPO, "skate_src", "author"))
import numpy as np                                            # noqa: E402
from mathutils import Vector                                  # noqa: E402
import person as pp                                           # noqa: E402
import cast                                                   # noqa: E402
import toonface as tf                                         # noqa: E402
from rpack import Rpack                                       # noqa: E402
import morphs                                                 # noqa: E402

PACKS = os.path.join(REPO, "reva_native", "packs")
ENGINE = os.path.join(os.environ.get("CLUB_OUT", "out"), "engine")   # club-engine kit: machine-independent paths
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
opt = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2)}
NAMES = opt.get("--only", ",".join(cast.LINEUP)).split(",")
OURS = ("z_rm_wake", "z_rm_sleep", "z_rm_wake_up", "z_rm_wake_rub", "z_rm_huddle")


def face_inverse(person, P):
    """a Blender-space point -> (a degrees, u, off): u from its height, a by bisection on the head ring's direction, off
    the horizontal distance out from that ring point (face_xyz pushes along the horizontal radial)"""
    hh, chin, cy = person.S["head_h"], person.Z["chin"], person.head_cy
    u = (P.z - chin) / hh
    want = math.atan2(P.x, -(P.y - cy))                       # the point's polar angle round the head's axis (0 = the face)

    def polar(th):
        q = person.head_pt(th, P.z)
        return math.atan2(q.x, -(q.y - cy))
    lo, hi = want - 0.6, want + 0.6
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        if polar(mid) < want: lo = mid
        else: hi = mid
    th = 0.5 * (lo + hi)
    q = person.head_pt(th, P.z)
    o = Vector((q.x, q.y - cy, 0.0)).normalized()
    off = (P - q).dot(o)
    return math.degrees(th), u, off


def outline_fns(person, sg):
    f = person.S["face"]; mk = person.marks()
    upper, lower = tf.eye_outline(person, sg, f, mk)
    A = np.array([a for a, _ in upper]); U = np.array([u for _, u in upper]); L = np.array([u for _, u in lower])
    order = np.argsort(A)
    A, U, L = A[order], U[order], L[order]
    return (lambda a: float(np.interp(a, A, U)), lambda a: float(np.interp(a, A, L)), float(A[0]), float(A[-1]))


def main():
    for nm in NAMES:
        pack = os.path.join(PACKS, "v1_%s.rpack" % nm)
        base = os.path.join(ENGINE, nm, "v1_%s.noclips" % nm)
        p = Rpack(pack)
        if any(c in p.clip_names() for c in OURS):
            if not os.path.exists(base): sys.exit("%s carries authored clips and has no clipless base" % pack)
            shutil.copyfile(base, pack); p = Rpack(pack)
            print("[eyelids] %s: put back to its clipless base first" % nm)
        import struct
        offM = struct.unpack_from("<15I", p.buf, 4)[14]
        have = []
        if offM:
            n = struct.unpack_from("<I", p.buf, offM)[0]
            have = [p.sname(struct.unpack_from("<4I", p.buf, offM + 4 + i * 16)[1]) for i in range(n)]
        if "blink_L" in have:
            if opt.get("--redo") != "1":
                print("[eyelids] %s: already has its blink morphs -- skipped (--redo 1 rebuilds them)" % nm); continue
            print("[eyelids] %s: dropped %d old blink morphs" % (nm, morphs.drop_morphs(pack, ("blink_L", "blink_R"))))
            p = Rpack(pack)
        person = pp.Person(cast.CAST[nm])
        f = person.S["face"]
        ink_off = 0.00145                                     # the lid inks lie at 0.0015 out, the eye's shapes under 0.0013
        brow_off = 0.00155                                    # ...and the brows at 0.0016: never moved
        fns = {sg: outline_fns(person, sg) for sg in (1, -1)}
        for mi in range(p.nMeshes):
            rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
            mname = p.sname(rec[0])
            if "~face~" not in mname: continue
            V = np.frombuffer(p.buf, "<f4", rec[2] * 3, rec[6]).reshape(-1, 3).astype(float)
            for sg, tag in ((1, "blink_L"), (-1, "blink_R")):
                Uf, Lf, a0, a1 = fns[sg]
                sparse = []; nin = nink = 0
                for vi, (x, y, z) in enumerate(V):
                    Pb = Vector((x, -z, y))                   # pack (y up) -> Blender (z up)
                    if Pb.z < person.Z["chin"] or abs(x) < 1e-4 or (x > 0) != (sg > 0): continue
                    a, u, off = face_inverse(person, Pb)
                    if not (min(a0, a1) <= a <= max(a0, a1)): continue
                    up_, lo_ = Uf(a), Lf(a); h = up_ - lo_
                    if h <= 1e-5: continue
                    if off < ink_off:                         # the eye's interior
                        if not (lo_ - 0.08 * h <= u <= up_ + 0.12 * h): continue
                        u2 = lo_ + (u - lo_) * 0.06; nin += 1
                    elif off < brow_off:                      # an ink line: only the UPPER lid's, near the top edge
                        if abs(u - up_) > 0.45 * h or u < lo_ + 0.5 * h: continue
                        u2 = u - h * 0.96; nink += 1
                    else:
                        continue                              # a brow
                    Q = person.face_xyz(a, u2, off)
                    d = (Q.x - Pb.x, Q.z - Pb.z, -(Q.y - Pb.y))  # back to pack space: (x, z, -y)
                    if abs(d[0]) + abs(d[1]) + abs(d[2]) > 1e-7: sparse.append((vi, d[0], d[1], d[2]))
                if sparse:
                    morphs.add_morph(pack, mi, tag, sparse)
                    p = Rpack(pack)
                print("[eyelids] %s mesh %d '%s' %s: %d interior + %d lid-ink points move (%d deltas)"
                      % (nm, mi, mname, tag, nin, nink, len(sparse)))


main()
print("[eyelids] done")
