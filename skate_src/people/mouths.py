"""mouths.py -- THE WORLD AT THE END OF TIME's faces TALK: a `jaw` morph on every cast member's pack, and `blink` (the
name the scene's blink clock drives) over both lids.

jacob, 10-18: "Start with the mouths and a rough cut of scene 2.1 on temporary voices." The scene system opens the
speaker's mouth by setting the morph NAMED `jaw` to the speech envelope (crowd_kms.c, the scene's per-line mouth; a body
with a jaw BONE takes the bone instead). The toon face (toonface.py) draws its mouth as ONE INK LINE along the lips a
hair above the skin, so an open mouth, drawn that way, is that line opened into a dark lens -- the morph moves points,
it adds none:

  - the mouth's ink line (0.0012 out): its LOWER edge drops by the opening at that point and its upper edge rises a
    fifth of it; the opening is deepest at the middle and nothing at the corners, so the line opens into a lens;
  - the lower lip's shadow stroke (0.0010 out) rides down with the lower edge;
  - filled lips (Wanda's `lip_fill`, 0.0009 out): the lower lip drops with the jaw, the upper lifts a fifth;
  - the brows, the eyes, the nose's stroke, the lines of age: untouched.

The layers are told apart by their OFFSET (eyelids.py learnt that on Billy's brow) and by where they sit in face
coordinates (a = degrees round the head, u = share of head height): each face vertex is put back into face coordinates
by inverting person.face_xyz numerically, moved there, and mapped forward again, so an open mouth lies on the head's
own surface at its own offset.

    blender --background --factory-startup --python-exit-code 1 --python skate_src/people/mouths.py -- [--only viktor] [--redo 1]

A pack carrying the cast's authored clips is first put back to its clipless base (skate_src/people/moves/bake.py
re-appends them -- run it after this). `--redo 1` drops `jaw` and `blink` and builds them again.
"""
import math, os, shutil, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "atelier"))
sys.path.insert(0, os.path.join(REPO, "skate_src", "author"))
import numpy as np                                            # noqa: E402
from mathutils import Vector                                  # noqa: E402
import person as pp                                           # noqa: E402
import cast                                                   # noqa: E402
from rpack import Rpack                                       # noqa: E402
import morphs                                                 # noqa: E402

PACKS = os.path.join(REPO, "reva_native", "packs")
ENGINE = os.path.join(os.environ.get("CLUB_OUT", "out"), "engine")   # club-engine kit: machine-independent paths
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
opt = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2)}
NAMES = opt.get("--only", ",".join(cast.LINEUP)).split(",")
OURS = ("z_rm_wake", "z_rm_sleep", "z_rm_wake_up", "z_rm_wake_rub", "z_rm_huddle")
OPEN = 0.085          # the widest opening, in head heights, for a mouth 17 degrees wide (scaled by each mouth's width)
LIFT = 0.20           # the upper edge rises this share of it


def face_inverse(person, P):
    """a Blender-space point -> (a degrees, u, off) -- as eyelids.py"""
    hh, chin, cy = person.S["head_h"], person.Z["chin"], person.head_cy
    u = (P.z - chin) / hh
    want = math.atan2(P.x, -(P.y - cy))

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


def morph_table(p):
    offM = struct.unpack_from("<15I", p.buf, 4)[14]
    if not offM: return []
    n = struct.unpack_from("<I", p.buf, offM)[0]
    return [struct.unpack_from("<4I", p.buf, offM + 4 + i * 16) for i in range(n)]   # (mesh, name, n, deltas)


def deltas(p, ent):
    m, nid, ns, od = ent
    return [struct.unpack_from("<I3f", p.buf, od + k * 16) for k in range(ns)]


def main():
    for nm in NAMES:
        pack = os.path.join(PACKS, "v1_%s.rpack" % nm)
        base = os.path.join(ENGINE, nm, "v1_%s.noclips" % nm)
        p = Rpack(pack)
        if any(c in p.clip_names() for c in OURS):
            if not os.path.exists(base): sys.exit("%s carries authored clips and has no clipless base" % pack)
            shutil.copyfile(base, pack); p = Rpack(pack)
            print("[mouths] %s: put back to its clipless base first" % nm)
        have = [p.sname(e[1]) for e in morph_table(p)]
        if "jaw" in have:
            if opt.get("--redo") != "1":
                print("[mouths] %s: already has its jaw -- skipped (--redo 1 rebuilds it)" % nm); continue
            print("[mouths] %s: dropped %d old morphs" % (nm, morphs.drop_morphs(pack, ("jaw", "blink"))))
            p = Rpack(pack)
        if "blink_L" not in have: sys.exit("%s has no blink_L -- run eyelids.py first" % nm)
        person = pp.Person(cast.CAST[nm])
        f = person.S["face"]; mk = person.marks()
        mu, mw = mk["mu"], f.get("mouth_w", 20.0)
        smile, lowlip, lf = f.get("smile", 0.0), f.get("lower_lip", 0.045), f.get("lip_fill")
        dmax = OPEN * mw / 17.0
        for mi in range(p.nMeshes):
            rec = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
            mname = p.sname(rec[0])
            if "~face~" not in mname: continue
            V = np.frombuffer(p.buf, "<f4", rec[2] * 3, rec[6]).reshape(-1, 3).astype(float)
            sparse = []; n_ink = n_shadow = n_lip = 0
            for vi, (x, y, z) in enumerate(V):
                Pb = Vector((x, -z, y))                       # pack (y up) -> Blender (z up)
                if Pb.z < person.Z["chin"]: continue
                a, u, off = face_inverse(person, Pb)
                if abs(a) > mw + 1.0: continue
                k = max(0.0, 1.0 - (a / mw) ** 2)
                d = dmax * k ** 0.6                           # deepest at the middle, nothing at the corners
                if d <= 1e-6: continue
                lu = mu + smile * (1.0 - (a / mw) ** 2)       # the line's own height there (a smile curves it)
                if 0.00115 <= off < 0.00125 and abs(u - lu) <= 0.006:                         # the mouth's ink line
                    du = -d if u < lu else LIFT * d; n_ink += 1
                elif 0.00095 <= off < 0.00105 and abs(a) <= 0.45 * mw and abs(u - (mu - lowlip)) <= 0.006:
                    du = -0.95 * d; n_shadow += 1                                              # the lower lip's shadow
                elif lf and 0.00085 <= off < 0.00095 and mu - lf[1] - 0.01 <= u <= mu + lf[0] + 0.01:
                    du = -d if u < mu else LIFT * d; n_lip += 1                                # filled lips
                else:
                    continue
                Q = person.face_xyz(a, u + du, off)
                dd = (Q.x - Pb.x, Q.z - Pb.z, -(Q.y - Pb.y))  # back to pack space: (x, z, -y)
                if abs(dd[0]) + abs(dd[1]) + abs(dd[2]) > 1e-7: sparse.append((vi, dd[0], dd[1], dd[2]))
            if sparse:
                morphs.add_morph(pack, mi, "jaw", sparse); p = Rpack(pack)
            print("[mouths] %s mesh %d '%s' jaw: %d ink + %d shadow + %d lip points (%d deltas, widest %.3f u)"
                  % (nm, mi, mname, n_ink, n_shadow, n_lip, len(sparse), dmax))
            # `blink`: the scene's blink clock shuts both eyes through one name -- each lid's own deltas again
            for ent in [e for e in morph_table(p) if e[0] == mi and p.sname(e[1]) in ("blink_L", "blink_R")]:
                morphs.add_morph(pack, mi, "blink", [(int(v), dx, dy, dz) for (v, dx, dy, dz) in deltas(p, ent)])
                p = Rpack(pack)
            print("[mouths] %s mesh %d: blink = blink_L + blink_R" % (nm, mi))


main()
print("[mouths] done")
