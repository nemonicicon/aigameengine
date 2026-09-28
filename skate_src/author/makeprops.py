#!/usr/bin/env python3
"""makeprops.py -- CAPABILITY H: the props the film's dialogue asks for and 143 town
meshes do not contain.

    python skate_src/author/makeprops.py

⛔ THE GAP. Two coffees, a hammer and the boards Dale never finished are all spoken about
in SEVENTY-FOUR and none of them exists. There are 143 props in the club and not
one of them is a mug. The treatment already said this would happen -- *"Must be authored
(trivial, primitives): a paddling pool, a sun lounger, and the boards"* -- and then the
film was shot four times without them.

⭐ AND THEY ARE PRIMITIVES. A mug is a cylinder with a handle. A hammer is a shaft and a
head. A board is a box. None of this needs Blender, UE, or the artery -- it needs a vertex
buffer and an index buffer, which is what an rpack already is.

⛔ FLAT-SHADED AND UNTEXTURED, deliberately. The club's shader takes a per-instance TINT
when a prop has no texture (uUse=0), which is exactly the look the town kit already has --
so these sit in frame with the ZAMNC props instead of next to them.

Writes reva_native/props/v1_x_<name>.rpack:
    mug        a coffee, for the porch and the tables
    hammer     for act V, which has been hammering nothing for four drafts
    board      a plank, for the windows
    torch      a hand torch, since town_lantern is a lamppost fitting
    cigarette  (ROBOT NEKO ep 12) paper, a filter third and a burning tip -- THREE colours in one
               prop, one mesh each: a mesh named `...#rrggbb` takes that colour, one with no colour
               takes the script's tint (the paper: `@F2EFE8`), and `#rrggbb*` is the part that GLOWS
               (the ember -- where a `@light` sits and what brightens on a drag). Authored in metres
               along +Y, filter at the origin (the grip), so its FAR TIP -- where `@smoke` leaves --
               is the burning end. Held size is WORLD length: `v1_x_cigarette@0.16` is a real one.
"""
import io, math, os, struct, sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..",
                   "reva_native", "props")
# ⭐ ep 12: --out <dir> writes somewhere else -- a checkout that must not grow a props/ can still test the bake
if "--out" in sys.argv:
    OUT = sys.argv[sys.argv.index("--out") + 1]


# ---------------------------------------------------------------- geometry helpers
def box(cx, cy, cz, sx, sy, sz):
    """axis-aligned box -> (positions, indices)"""
    hx, hy, hz = sx * .5, sy * .5, sz * .5
    c = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
         (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz)]
    c = [(x + cx, y + cy, z + cz) for (x, y, z) in c]
    f = [(0, 1, 2), (0, 2, 3), (5, 4, 7), (5, 7, 6), (4, 0, 3), (4, 3, 7),
         (1, 5, 6), (1, 6, 2), (3, 2, 6), (3, 6, 7), (4, 5, 1), (4, 1, 0)]
    return c, f


def tube(cx, cy, cz, r, h, seg=12, inner=None):
    """a cylinder along Y; `inner` hollows it (a mug)"""
    pos, idx = [], []
    rings = [r] if inner is None else [r, inner]
    base = 0
    for rr in rings:
        for k in range(seg):
            a = 2 * math.pi * k / seg
            pos.append((cx + rr * math.cos(a), cy, cz + rr * math.sin(a)))
            pos.append((cx + rr * math.cos(a), cy + h, cz + rr * math.sin(a)))
        for k in range(seg):
            a, b = base + k * 2, base + ((k + 1) % seg) * 2
            idx += [(a, b, b + 1), (a, b + 1, a + 1)]
        base += seg * 2
    # a floor for the mug
    if inner is not None:
        pos.append((cx, cy + h * .12, cz))
        c = len(pos) - 1
        for k in range(seg):
            a, b = k * 2, ((k + 1) % seg) * 2
            idx.append((c, b, a))
    return pos, idx


def merge(parts):
    pos, idx = [], []
    for (p, f) in parts:
        o = len(pos)
        pos += p
        idx += [(a + o, b + o, c + o) for (a, b, c) in f]
    return pos, idx


def cyl(y0, y1, r0, r1=None, seg=14, cap0=False, cap1=False):
    """⭐ ep 12: a cylinder (or a taper, r0 -> r1) along +Y WITH REAL NORMALS -- radial on the side,
    axial on the caps -- so a thin round thing shades round. -> (pos, idx, nrm)"""
    r1 = r0 if r1 is None else r1
    pos, nrm, idx = [], [], []
    for k in range(seg):
        a = 2 * math.pi * k / seg
        c, s = math.cos(a), math.sin(a)
        pos += [(r0 * c, y0, r0 * s), (r1 * c, y1, r1 * s)]
        nrm += [(c, 0.0, s), (c, 0.0, s)]
    for k in range(seg):
        a, b = k * 2, ((k + 1) % seg) * 2
        idx += [(a, b, b + 1), (a, b + 1, a + 1)]
    for (yc, rc, ny, on) in ((y0, r0, -1.0, cap0), (y1, r1, 1.0, cap1)):
        if not on:
            continue
        c0 = len(pos)
        pos.append((0.0, yc, 0.0))
        nrm.append((0.0, ny, 0.0))
        for k in range(seg):
            a = 2 * math.pi * k / seg
            pos.append((rc * math.cos(a), yc, rc * math.sin(a)))
            nrm.append((0.0, ny, 0.0))
        for k in range(seg):
            idx.append((c0, c0 + 1 + k, c0 + 1 + (k + 1) % seg))
    return pos, idx, nrm


# ---------------------------------------------------------------- the writer
HDR = struct.Struct("<15I")


def write(name, pos, idx):
    """One node, one mesh, no bones, no texture -- the smallest legal RPK1 v2."""
    write_meshes(name, [("mesh", pos, idx, None)])


def write_meshes(name, meshes):
    """⭐ ep 12: one node, N meshes (name, pos, idx, nrm-or-None), no bones, no texture. The mesh NAME carries
    its colour (`#rrggbb`, `#rrggbb*` = it glows) -- the engine's drawHeldOne reads it on the tint path.
    One mesh named "mesh" with no normals writes byte-for-byte what write() always wrote."""
    os.makedirs(OUT, exist_ok=True)
    strings = [name] + [m[0] for m in meshes]
    blob = b""
    stoff = []
    for t in strings:
        stoff.append((len(blob), len(t)))
        blob += t.encode()
    body = b""

    def put(b):
        nonlocal body
        o = 64 + len(body)
        body += b
        return o

    # node 0: identity root
    offNodes = put(struct.pack("<IiI10f", 0, -1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1))
    per = []
    for (mname, pos, idx, nrm) in meshes:
        nv = len(pos)
        offPos = put(b"".join(struct.pack("<3f", *p) for p in pos))
        offNorm = put(b"".join(struct.pack("<3f", *(n if nrm else (0.0, 1.0, 0.0)))
                               for n in (nrm if nrm else pos)))
        offJ = put(b"\0\0\0\0" * nv)
        offW = put(b"\xff\0\0\0" * nv)
        offI = put(b"".join(struct.pack("<3I", *t) for t in idx))
        per.append((nv, len(idx) * 3, offPos, offNorm, offJ, offW, offI))
    offBind = put(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1))
    offBM = put(struct.pack("<H", 0))
    offIBM = put(struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1))
    # flags: bit0 normals, bit1 uv (no), bit2 skinned
    offMeshes = None
    for mi, (nv, ni, offPos, offNorm, offJ, offW, offI) in enumerate(per):
        o = put(struct.pack("<15I", 1 + mi, 0, nv, ni, 1, 1 | 4,
                            offPos, offNorm, 0, offJ, offW, offI, offBM, offIBM, offBind))
        offMeshes = o if offMeshes is None else offMeshes
    offStr = put(struct.pack("<I", len(strings))
                 + b"".join(struct.pack("<II", o, l) for (o, l) in stoff) + blob)
    total = 64 + len(body)
    hdr = b"RPK1" + HDR.pack(2, 1, len(meshes), 0, 0, 0, offNodes, offMeshes, 0, 0, 0,
                             offStr, 0, total, 0)
    p = os.path.join(OUT, "v1_x_%s.rpack" % name)
    io.open(p, "wb").write(hdr + body)
    ys = [q[1] for m in meshes for q in m[1]]
    nvt = sum(len(m[1]) for m in meshes)
    ntt = sum(len(m[2]) for m in meshes)
    print("  %-8s %4d verts %4d tris   Y %.2f..%.2f%s" % (name, nvt, ntt, min(ys), max(ys),
          "" if len(meshes) == 1 else "   %d meshes: %s" % (len(meshes), " ".join(m[0] for m in meshes))))


def main():
    print("makeprops -> %s" % os.path.normpath(OUT))
    # a mug: hollow cylinder + a handle
    body = tube(0, 0, 0, 0.055, 0.10, 14, inner=0.046)
    handle = box(0.072, 0.052, 0.0, 0.016, 0.052, 0.012)
    write("mug", *merge([body, handle]))

    # a hammer: shaft along Y, head across X
    write("hammer", *merge([box(0, 0, 0, 0.026, 0.30, 0.026),
                            box(0, 0.30, 0, 0.115, 0.048, 0.042),
                            box(-0.075, 0.30, 0, 0.035, 0.036, 0.030)]))

    # a board: one plank
    write("board", *merge([box(0, 0, 0, 1.55, 0.045, 0.19)]))

    # ⭐ A SKATEBOARD. The rider has existed since August with nothing to stand on.
    # Dimensions from design/skating.md, measured off the deck the rider was tuned
    # against: 1.372 long (Z), 0.375 wide (X), 0.211 tall (Y) including wheels. The long
    # axis is Z because a kickflip spins about it -- rolling about X sends it end over end.
    # The deck top sits at SK_DECK_H (0.16); wheels reach the ground at y=0.
    DL, DW = 1.372, 0.375
    DECK_Y = 0.145                      # underside of the deck; top lands at ~0.16
    WR = 0.038                          # wheel radius -- 2*WR + deck thickness ~= 0.211
    parts = []
    # the deck: a centre plank plus a kicked nose and tail (three boxes, not a curve --
    # this is a low-poly town prop and it is seen from four feet away at speed)
    parts.append(box(0, DECK_Y, 0.0, DW, 0.022, DL * 0.62))
    for zs in (+1.0, -1.0):
        parts.append(box(0, DECK_Y + 0.030, zs * DL * 0.40, DW * 0.80, 0.020, DL * 0.20))
    # two trucks, hung under the deck at the classic inset
    for zs in (+1.0, -1.0):
        parts.append(box(0, DECK_Y - 0.030, zs * DL * 0.30, DW * 0.52, 0.030, 0.060))
    # four wheels, on their sides so they roll about X
    for zs in (+1.0, -1.0):
        for xs in (+1.0, -1.0):
            parts.append(tube(xs * DW * 0.40, WR, zs * DL * 0.30, WR, 0.052, seg=10))
    write("skatedeck", *merge(parts))

    # a hand torch -- town_lantern is a lamppost fitting
    write("torch", *merge([tube(0, 0, 0, 0.030, 0.17, 12),
                           tube(0, 0.17, 0, 0.048, 0.055, 12)]))

    # ⭐ ep 12: A CIGARETTE. 84 x 8.4 mm, in metres along +Y, the filter end at the origin (the grip) and
    # the burning end at the top -- so the engine's FAR TIP (the end of the longest side away from the
    # origin) is where the smoke leaves. From the grip: the cork filter (a third), the paper (no colour
    # of its own -- the script's tint, `@F2EFE8`), the EMBER (`*`: it glows, it is where a `@light` sits
    # and what brightens on a drag -- a hair oversized so a medium shot can see it), and a stub of grey
    # ash, tapered and capped, at the very tip.
    L, R = 0.084, 0.0042
    YF, YP, YE = L / 3.0, 0.0735, 0.0790       # filter | paper | ember | ash
    write_meshes("cigarette", [
        ("cig_filter#C8894A",  *cyl(0.0, YF, R, cap0=True)),
        ("cig_paper",          *cyl(YF, YP, R)),
        ("cig_ember#FF6A1E*",  *cyl(YP, YE, R * 1.02, R * 0.98)),
        ("cig_ash#6E6863",     *cyl(YE, L, R * 0.98, R * 0.80, cap1=True)),
    ])

    print("\nAll flat-shaded and untextured on purpose: the shader takes a per-instance")
    print("TINT when a prop has no texture, which is the look the town kit already has.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
