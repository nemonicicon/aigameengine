"""build_x3d.py -- x3d, jacob's second character: built in Blender by code from her character sheet, rigged,
skinned, painted, and rendered against the sheet.

    blender --background --factory-startup --python-exit-code 1 --python skate_src/x3d/build_x3d.py -- \
        --out <dir> [--arm 45] [--renders 1]

Writes x3d.glb (the rigged body), x3d.blend, x3d_atlas.png, cmp_front/side/back.png (sheet-scale renders in
the sheet's pose, transparent, for skate_src/x3d/compare_sheet.py) and turn_*.png (a Cycles turnaround).

jacob, 09-27: a character sheet and "next character. name her x3d". A stylised low-poly girl: magenta skin,
long straight plum hair parted in the middle, pointed elf ears, huge dark eyes with a winged lash line, a cream
crop tank with a dark star, very wide black cargo pants on a belt with a wallet chain on her left hip, chunky
black skate shoes with white soles and straps, a black cord necklace with a tag, a black band on her left wrist.

Every number is measured off the sheet's front and side views: 687 px from sole to the top of her hair is
1.66 m, so 1 px = 2.42 mm. Unlike Clanker (rigid parts on bones) she BENDS, so each surface is ONE lofted skin
whose weights blend across its joints:
  * the torso and neck are one parametric surface S(theta, z); the TANK TOP is not a second layer but a REGION
    of that surface (its edges snapped onto the neckline, armhole and hem curves), lifted 3 mm with a lip -- so
    no pose can push skin through it;
  * the arms are lofted along the bone line from a round deltoid cap that sits INSIDE the shoulder (a ball
    joint: raised or lowered, no gap opens), hands with three-bone fingers;
  * the pants are one mesh that splits at the crotch into two leg tubes (a 24-ring becomes two 13-rings);
  * the hair is one thick shell whose front opening is the face; the head and ears are closed meshes.
The rig is Mixamo-named (Hips, Spine, Spine1, Spine2, Neck, Head, Left/Right Shoulder, Arm, ForeArm, Hand, five
three-bone fingers, UpLeg, Leg, Foot, ToeBase) so the club's canon and Godot's humanoid retargeting both drive
her with the move library. Rest pose: the sheet's own A-pose (arms --arm degrees below horizontal).
HAIR PHYSICS: four Hair_* chains (three down her back rooted in Spine2, one through her right front lock rooted in
Head) that the engine's chain solver mines by name (design/engine_capabilities.md section 30); the lock is the right
curtain's front edge carried on over her shoulder onto her chest.
COLOURS: one 2048 atlas -- flat swatches, her face painted in one quadrant and the star in another -- because
the club's baker carries a material's TEXTURE, not its flat colour (glb2rpack_anim.py reads baseColorTexture
only), and her face and the print need a texture anyway.
"""
import math, os, sys, time

import bpy
import bmesh
import numpy as np
from mathutils import Vector, Matrix, Quaternion

T0 = time.time()
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
opt = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2)}
OUT = opt.get("--out", os.path.join(os.path.dirname(os.path.abspath(__file__)), "out"))
os.makedirs(OUT, exist_ok=True)
ARM_DOWN = math.radians(float(opt.get("--arm", "45")))     # rest: the arms this far below horizontal
RENDERS = opt.get("--renders", "1") == "1"

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene

# ------------------------------------------------------------------ measured off the sheet
H = 1.66                       # sole to the top of her hair
PX = H / 687.0                 # the front view: sheet rows 31 (hair top) .. 718 (sole)


def smoothstep(e0, e1, x):
    t = (x - e0) / (e1 - e0)
    t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
    return t * t * (3.0 - 2.0 * t)


def lerp_table(table, z):
    """linear interpolation of every column of a (z, ...) table at height z (clamped at the ends)"""
    zs = [r[0] for r in table]
    if z <= zs[0]: return list(table[0][1:])
    if z >= zs[-1]: return list(table[-1][1:])
    for i in range(len(zs) - 1):
        if zs[i] <= z <= zs[i + 1]:
            t = (z - zs[i]) / (zs[i + 1] - zs[i])
            return [a + (b - a) * t for a, b in zip(table[i][1:], table[i + 1][1:])]


def ring_xy(theta, hw, f, b, cy, p, cx=0.0):
    """a superellipse section: theta 0 = the front (-Y), 90 deg = her left (+X); front and back half-depths
    f and b about cy; p < 1 squares it off"""
    s, c = math.sin(theta), math.cos(theta)
    x = cx + hw * math.copysign(abs(s) ** p, s)
    y = cy - (f if c >= 0 else b) * math.copysign(abs(c) ** p, c)
    return x, y


# ------------------------------------------------------------------ the character, as parts
SWATCH = ["skin", "skin_dark", "hair", "top", "star", "pants", "belt", "buckle", "chain", "shoe", "sole",
          "band", "pendant", "lip", "white", "black"]
COLOR = {"skin": (164, 69, 113), "skin_dark": (132, 50, 90), "hair": (68, 34, 58), "top": (228, 208, 210),
         "star": (72, 50, 78), "pants": (58, 46, 60), "belt": (44, 36, 48), "buckle": (150, 140, 150),
         "chain": (206, 190, 192), "shoe": (50, 42, 52), "sole": (224, 204, 202), "band": (36, 30, 40),
         "pendant": (74, 66, 76), "lip": (136, 48, 86), "white": (236, 218, 218), "black": (22, 14, 20)}
MAT_SWATCH = {"M_Skin": "skin", "M_Hair": "hair", "M_Top": "top", "M_Pants": "pants", "M_Belt": "belt",
              "M_Metal": "buckle", "M_Chain": "chain", "M_Shoe": "shoe", "M_Sole": "sole", "M_Band": "band"}
MATS = list(MAT_SWATCH)
FACE_BOX = (-0.17, 0.17, 1.30, 1.64)       # x0, x1, z0, z1: the face projection -> atlas u 0..0.5, v 0.5..1
PRINT_BOX = (-0.20, 0.20, 0.95, 1.35)      # the tank's front, straps included -> atlas u 0.5..1, v 0.5..1


def uv_of(spec, p):
    kind = spec[0]
    if kind == "swatch":
        k = SWATCH.index(spec[1])
        return ((k % 8) * 128 + 64) / 2048.0, ((k // 8) * 128 + 64) / 2048.0
    if kind == "face":
        x0, x1, z0, z1 = FACE_BOX
        return (p[0] - x0) / (x1 - x0) * 0.5, 0.5 + (p[2] - z0) / (z1 - z0) * 0.5
    if kind == "print":
        x0, x1, z0, z1 = PRINT_BOX
        return 0.5 + (p[0] - x0) / (x1 - x0) * 0.5, 0.5 + (p[2] - z0) / (z1 - z0) * 0.5
    raise ValueError(spec)


class Part:
    def __init__(self, name, smooth=False):
        self.name, self.smooth, self.weld = name, smooth, False
        self.V, self.W, self.F, self.M, self.UV, self.S = [], [], [], [], [], []

    def v(self, p, w):
        self.V.append((float(p[0]), float(p[1]), float(p[2])))
        self.W.append(w)
        return len(self.V) - 1

    def f(self, idx, mat, uv=None, smooth=None):
        out = []
        for i in idx:                              # drop repeats: a quad that pinches is a triangle
            if i not in out: out.append(i)
        if len(out) >= 3:
            self.F.append(out); self.M.append(mat); self.UV.append(uv)
            self.S.append(self.smooth if smooth is None else smooth)


PARTS = []


def new_part(name, smooth=False):
    p = Part(name, smooth); PARTS.append(p); return p


def loft(part, rings, mat, closed=True, uv=None, cap0=None, cap1=None):
    """quads between consecutive rings of equal count (lists of vertex indices); optional fan caps given as
    vertex indices of the centre points"""
    for a, b in zip(rings[:-1], rings[1:]):
        n = len(a)
        for j in range(n if closed else n - 1):
            k = (j + 1) % n
            part.f((a[j], a[k], b[k], b[j]), mat, uv)
    if cap0 is not None:
        r = rings[0]; n = len(r)
        for j in range(n):
            part.f((cap0, r[(j + 1) % n], r[j]), mat, uv)
    if cap1 is not None:
        r = rings[-1]; n = len(r)
        for j in range(n):
            part.f((cap1, r[j], r[(j + 1) % n]), mat, uv)


# ------------------------------------------------------------------ weights (by rest position)
def norm_w(w):
    w = {k: v for k, v in w.items() if v > 0.004}
    top = sorted(w.items(), key=lambda kv: -kv[1])[:4]
    s = sum(v for _, v in top) or 1.0
    return {k: v / s for k, v in top}


def w_torso(x, y, z):
    """the spine chain by height, the clavicles on the upper outer torso, the neck into the head"""
    hips = 1.0 - smoothstep(0.935, 0.995, z)
    sp = smoothstep(0.935, 0.995, z) * (1.0 - smoothstep(1.035, 1.095, z))
    sp1 = smoothstep(1.035, 1.095, z) * (1.0 - smoothstep(1.115, 1.175, z))
    sp2 = smoothstep(1.115, 1.175, z) * (1.0 - smoothstep(1.285, 1.330, z))
    nk = smoothstep(1.285, 1.330, z) * (1.0 - smoothstep(1.370, 1.400, z))
    hd = smoothstep(1.370, 1.400, z)
    side = "Left" if x > 0 else "Right"
    sh = 0.75 * smoothstep(0.050, 0.100, abs(x)) * smoothstep(1.160, 1.230, z) * (1.0 - smoothstep(1.285, 1.310, z))
    w = {"Hips": hips, "Spine": sp, "Spine1": sp1, "Spine2": sp2 * (1.0 - sh), "Neck": nk, "Head": hd,
         side + "Shoulder": sp2 * sh}
    return norm_w(w)


# ------------------------------------------------------------------ the torso + neck: one surface S(theta, z)
T_TORSO = [  # z, half-width, front, back, centre y, squareness p
    (0.930, 0.098, 0.068, 0.080, 0.006, 0.90),
    (0.955, 0.093, 0.067, 0.076, 0.006, 0.90),
    (0.975, 0.088, 0.066, 0.072, 0.005, 0.92),
    (1.000, 0.077, 0.064, 0.068, 0.004, 0.95),
    (1.030, 0.071, 0.063, 0.066, 0.004, 0.95),
    (1.046, 0.075, 0.064, 0.066, 0.004, 0.92),
    (1.080, 0.083, 0.066, 0.067, 0.004, 0.88),
    (1.130, 0.088, 0.068, 0.069, 0.004, 0.84),
    (1.170, 0.093, 0.066, 0.070, 0.004, 0.80),
    (1.200, 0.099, 0.052, 0.067, 0.004, 0.78),
    (1.225, 0.108, 0.042, 0.061, 0.005, 0.76),
    (1.248, 0.104, 0.034, 0.053, 0.006, 0.78),
    (1.265, 0.086, 0.030, 0.047, 0.006, 0.85),
    (1.280, 0.058, 0.030, 0.043, 0.006, 0.95),
    (1.295, 0.041, 0.035, 0.039, 0.007, 1.00),
    (1.320, 0.036, 0.034, 0.036, 0.008, 1.00),
    (1.360, 0.034, 0.032, 0.034, 0.010, 1.00),
    (1.400, 0.033, 0.030, 0.033, 0.012, 1.00),
]


def torso_pt(theta, z):
    hw, f, b, cy, p = lerp_table(T_TORSO, z)
    x, y = ring_xy(theta, hw, f, b, cy, p)
    if math.cos(theta) > 0:                               # the bust, under the tank
        for bx in (0.043, -0.043):
            y -= 0.020 * math.exp(-((x - bx) / 0.030) ** 2 - ((z - 1.128) / 0.030) ** 2)
        y += 0.004 * math.exp(-(x / 0.006) ** 2 - ((z - 1.000) / 0.006) ** 2)   # the navel
    return Vector((x, y, z))


def torso_normal(theta, z):
    e = 1e-4
    a = torso_pt(theta + e, z) - torso_pt(theta - e, z)
    b = torso_pt(theta, z + e) - torso_pt(theta, z - e)
    n = a.cross(b)
    return n.normalized() if n.length > 1e-12 else Vector((0, -1, 0))


def z_top(t):
    """the tank's upper edge by |theta| in degrees: the front scoop, over the shoulders, the back neckline"""
    if t <= 32.0: return 1.222 + 0.048 * (t / 32.0) ** 1.6
    if t >= 148.0: return 1.252 + 0.030 * ((180.0 - t) / 32.0) ** 1.6
    return 1.290


def g_top(theta, z):
    """negative inside the tank top: above the hem, below the neckline, outside the armhole"""
    t = abs(math.degrees(math.atan2(math.sin(theta), math.cos(theta))))
    A = ((t - 90.0) / 34.0) ** 2 + ((z - 1.212) / 0.050) ** 2
    return max(1.046 - z, z - z_top(t), (1.0 - A) * 0.03)


def build_torso():
    P = new_part("torso", smooth=True)
    N = 32
    Z = [0.930, 0.955, 0.975, 0.990, 1.000, 1.015, 1.030, 1.046, 1.060, 1.075, 1.090, 1.105, 1.120, 1.135,
         1.150, 1.165, 1.180, 1.195, 1.210, 1.225, 1.238, 1.250, 1.260, 1.270, 1.280, 1.290, 1.300, 1.330,
         1.360, 1.400]
    TH = [2.0 * math.pi * j / N for j in range(N)]
    par = {}                                     # grid vertex -> (theta, z)
    grid = [[None] * N for _ in Z]
    for i, z in enumerate(Z):
        for j, th in enumerate(TH):
            grid[i][j] = (i, j); par[(i, j)] = [th, z]
    quads = []
    for i in range(len(Z) - 1):
        for j in range(N):
            k = (j + 1) % N
            q = [(i, j), (i, k), (i + 1, k), (i + 1, j)]
            thc = TH[j] + math.pi / N
            zc = (Z[i] + Z[i + 1]) / 2.0
            quads.append((q, g_top(thc, zc) < 0.0))
    # snap the top's boundary vertices onto its outline (Newton on g, in metres along the surface)
    use = {}
    for q, top in quads:
        for key in q: use.setdefault(key, set()).add(top)
    bnd = [key for key, s in use.items() if len(s) == 2]
    for key in bnd:
        th, z = par[key]
        for _ in range(10):
            g = g_top(th, z)
            r = max(0.03, (lerp_table(T_TORSO, z)[0] + lerp_table(T_TORSO, z)[1]) / 2.0)
            e = 1e-4
            gs = (g_top(th + e / r, z) - g_top(th - e / r, z)) / (2 * e)
            gz = (g_top(th, z + e) - g_top(th, z - e)) / (2 * e)
            n2 = gs * gs + gz * gz
            if n2 < 1e-12 or abs(g) < 1e-6: break
            th -= g * gs / n2 / r
            z -= g * gz / n2
        par[key] = [th, z]
    # vertices: skin, and for the top a lifted copy of every vertex it uses
    vid, vtop = {}, {}
    for key, (th, z) in par.items():
        p = torso_pt(th, z)
        vid[key] = P.v(p, w_torso(p.x, p.y, p.z))
    for key, s in use.items():
        if True in s:
            th, z = par[key]
            p = torso_pt(th, z) + torso_normal(th, z) * 0.003
            # the tank HANGS from the bust: below it the fabric's front falls nearly straight (the sheet's side
            # view: its front at the hem is 2 cm proud of her belly), so the hem stands off as a ledge
            front = smoothstep(0.15, 0.55, math.cos(th))
            if front > 0 and z < 1.128:
                hang = torso_pt(th, 1.128).y - 0.003 + 0.05 * (1.128 - z)
                if hang < p.y:
                    p = Vector((p.x, p.y + (hang - p.y) * front, p.z))
            vtop[key] = P.v(p, w_torso(p.x, p.y, p.z))
    edge_faces = {}
    for q, top in quads:
        if top:
            idx = [vtop[k] for k in q]
            front = math.cos(TH[q[0][1]] + math.pi / N) > 0.2
            P.f(idx, "M_Top", ("print",) if front else None)
        else:
            P.f([vid[k] for k in q], "M_Skin")
        for a, b in zip(q, q[1:] + q[:1]):
            edge_faces.setdefault(frozenset((a, b)), []).append(top)
    # the lip: the fabric's edge, from the skin's boundary up to the lifted top's
    nlip = 0
    for e, tops in edge_faces.items():
        if len(tops) == 2 and tops[0] != tops[1]:
            a, b = tuple(e)
            P.f((vid[a], vid[b], vtop[b], vtop[a]), "M_Top", smooth=False)
            nlip += 1
    print("[x3d] torso: %d verts, %d faces, the tank top %d faces with a %d-edge lip"
          % (len(P.V), len(P.F), sum(1 for m in P.M if m == "M_Top"), nlip))
    return P


# ------------------------------------------------------------------ the head
T_HEAD = [  # z, half-width, front y, back y, p
    (1.342, 0.012, -0.084, -0.066, 1.00),
    (1.350, 0.032, -0.090, -0.022, 1.00),
    (1.362, 0.050, -0.095, 0.012, 0.95),
    (1.372, 0.058, -0.097, 0.034, 0.93),
    (1.382, 0.067, -0.099, 0.058, 0.91),
    (1.392, 0.075, -0.100, 0.080, 0.90),
    (1.403, 0.082, -0.102, 0.098, 0.88),
    (1.418, 0.090, -0.103, 0.114, 0.84),
    (1.432, 0.095, -0.103, 0.124, 0.80),
    (1.450, 0.098, -0.101, 0.131, 0.80),
    (1.472, 0.100, -0.103, 0.135, 0.80),
    (1.500, 0.101, -0.106, 0.136, 0.82),
    (1.530, 0.099, -0.106, 0.134, 0.85),
    (1.560, 0.094, -0.101, 0.128, 0.88),
    (1.585, 0.086, -0.092, 0.117, 0.90),
    (1.605, 0.073, -0.072, 0.100, 0.92),
    (1.620, 0.055, -0.050, 0.076, 0.95),
    (1.632, 0.030, -0.026, 0.042, 1.00),
]


def head_pt(theta, z):
    hw, yf, yb, p = lerp_table(T_HEAD, z)
    cy = (yf + yb) / 2.0
    x, y = ring_xy(theta, hw, cy - yf, yb - cy, cy, p)
    c = math.cos(theta)
    if c > 0:
        a = math.degrees(math.atan2(math.sin(theta), c))
        if 1.392 < z < 1.403: dn = 0.014 * (z - 1.392) / 0.011            # the nose: its underside,
        elif 1.403 <= z < 1.445: dn = 0.014 * ((1.445 - z) / 0.042) ** 1.6   # its tip, a short bridge
        else: dn = 0.0
        y -= dn * math.exp(-(a / 10.0) ** 2)
        y -= 0.005 * math.exp(-(a / 12.0) ** 2 - ((z - 1.375) / 0.006) ** 2)          # the lips
        y += 0.004 * math.exp(-((abs(a) - 28.0) / 13.0) ** 2 - ((z - 1.452) / 0.016) ** 2)   # eye plane
        y -= 0.003 * math.exp(-((abs(a) - 52.0) / 12.0) ** 2 - ((z - 1.425) / 0.012) ** 2)   # cheekbones
    return Vector((x, y, z))


def build_head():
    P = new_part("head", smooth=True)
    N = 24
    ZH = [r[0] for r in T_HEAD]
    rings = []
    for z in ZH:
        rings.append([P.v(head_pt(2 * math.pi * j / N, z), {"Head": 1.0}) for j in range(N)])
    chin = P.v((0.0, -0.080, 1.339), {"Head": 1.0})
    crown = P.v((0.0, 0.006, 1.637), {"Head": 1.0})
    loft(P, rings, "M_Skin", cap0=chin, cap1=crown)
    # the painted face: every forward-facing face takes the planar projection (the paint's ground is skin)
    for fi, face in enumerate(P.F):
        a, b, c = (Vector(P.V[i]) for i in face[:3])
        n = (b - a).cross(c - a)
        cz = sum(P.V[i][2] for i in face) / len(face)
        if n.length > 0 and cz > 1.335:
            # winding is fixed later; decide on the face's position instead: in front of the head's middle
            cy = sum(P.V[i][1] for i in face) / len(face)
            if cy < -0.045:
                P.UV[fi] = ("face",)
    # the ears: pointed, out and back and up (the sheet's 3/4 close-up), the inner ear darker
    for sg in (1, -1):
        # a thick pointed leaf: its front face the concha (darker), its back and rim skin; the base in the head
        front = [(0.092, 0.002, 1.470), (0.128, 0.012, 1.490), (0.176, 0.040, 1.502), (0.140, 0.036, 1.463),
                 (0.093, 0.010, 1.426)]
        back = [(0.096, 0.030, 1.474), (0.130, 0.024, 1.492), (0.176, 0.040, 1.502), (0.141, 0.046, 1.465),
                (0.097, 0.036, 1.430)]
        F_ = [P.v((sg * x, y, z), {"Head": 1.0}) for x, y, z in front]
        B_ = [P.v((sg * x, y, z), {"Head": 1.0}) for x, y, z in back]
        inner = P.v((sg * 0.118, 0.026, 1.470), {"Head": 1.0})
        outerc = P.v((sg * 0.124, 0.040, 1.476), {"Head": 1.0})
        dark = ("swatch", "skin_dark")
        for i in range(4):
            P.f((inner, F_[i], F_[i + 1]), "M_Skin", dark, smooth=False)
            P.f((outerc, B_[i + 1], B_[i]), "M_Skin", None, smooth=False)
            P.f((F_[i], B_[i], B_[i + 1], F_[i + 1]), "M_Skin", None, smooth=False)
    print("[x3d] head: %d verts, %d faces (%d painted)" % (len(P.V), len(P.F), sum(1 for u in P.UV if u == ("face",))))
    return P


# ------------------------------------------------------------------ the arms and hands
T_ARM = [  # t along the arm from the shoulder joint, radius across (in the arm's plane), radius front-back
    (-0.045, 0.014, 0.013), (-0.030, 0.034, 0.030), (-0.010, 0.044, 0.034), (0.020, 0.045, 0.034),
    (0.060, 0.038, 0.034), (0.120, 0.033, 0.032), (0.180, 0.030, 0.030), (0.210, 0.028, 0.028),
    (0.240, 0.030, 0.031), (0.290, 0.028, 0.030), (0.360, 0.021, 0.024), (0.405, 0.0165, 0.023),
    (0.425, 0.0160, 0.024),
]
UPPER, FORE = 0.21, 0.21
JOINT = Vector((0.125, 0.012, 1.225))
FINGERS = [  # name, offset front-back along the palm (+ = front), segment lengths
    ("Index", 0.025, (0.036, 0.024, 0.021)), ("Middle", 0.008, (0.040, 0.026, 0.023)),
    ("Ring", -0.008, (0.037, 0.025, 0.021)), ("Pinky", -0.024, (0.029, 0.019, 0.018))]
ARMB = {}


def arm_frame(sg):
    """d along the arm (down and out), q the back of the hand's side (up and out), fr the front"""
    d = Vector((sg * math.cos(ARM_DOWN), 0.0, -math.sin(ARM_DOWN)))
    q = Vector((sg * math.sin(ARM_DOWN), 0.0, math.cos(ARM_DOWN)))
    return d, q, Vector((0.0, -1.0, 0.0))


def w_arm(t, side):
    sh = 0.25 * smoothstep(0.02, -0.04, t)
    fore = smoothstep(0.185, 0.235, t)
    hand = smoothstep(0.395, 0.430, t)
    return norm_w({side + "Shoulder": sh, side + "Arm": (1 - sh) * (1 - fore),
                   side + "ForeArm": fore * (1 - hand), side + "Hand": hand})


def tube(P, pts, radii, mat, weights, nseg=6, cap_end=True, uv=None, up=None):
    """a lofted tube through points (radius per point); weights per point"""
    rings = []
    for i, c in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        a = (up if up is not None else Vector((0, 0, 1)))
        if abs(a.dot(t)) > 0.9: a = Vector((1, 0, 0))
        u = t.cross(a).normalized(); v = t.cross(u).normalized()
        r = radii[i]
        rings.append([P.v(c + (u * math.cos(2 * math.pi * k / nseg) + v * math.sin(2 * math.pi * k / nseg)) * r,
                          weights[i]) for k in range(nseg)])
    c0 = P.v(pts[0] - (pts[1] - pts[0]).normalized() * radii[0] * 0.5, weights[0])
    c1 = P.v(pts[-1] + (pts[-1] - pts[-2]).normalized() * radii[-1] * 0.6, weights[-1]) if cap_end else None
    loft(P, rings, mat, uv=uv, cap0=c0, cap1=c1)


def build_arm(sg):
    side = "Left" if sg > 0 else "Right"
    P = new_part(side + "_arm")
    d, q, fr = arm_frame(sg)
    J = Vector((sg * JOINT.x, JOINT.y, JOINT.z))
    NS = 10
    rings = []
    for t, rq, rf in T_ARM:
        c = J + d * t
        w = w_arm(t, side)
        rings.append([P.v(c + q * (rq * math.cos(2 * math.pi * k / NS)) + fr * (rf * math.sin(2 * math.pi * k / NS)), w)
                      for k in range(NS)])
    tip = P.v(J + d * (-0.050), w_arm(-0.05, side))
    loft(P, rings, "M_Skin", cap0=tip, cap1=P.v(J + d * 0.432, w_arm(0.432, side)))
    wrist = J + d * (UPPER + FORE)
    ARMB[side] = {"joint": J, "elbow": J + d * UPPER, "wrist": wrist, "d": d, "q": q}
    # the palm: a flattened box from the wrist to the knuckles, its back facing q
    PR = [(0.418, 0.0145, 0.023), (0.450, 0.0135, 0.031), (0.485, 0.0125, 0.034), (0.512, 0.0115, 0.034)]
    OCT = [(1, 0.6), (0.5, 1), (-0.5, 1), (-1, 0.6), (-1, -0.6), (-0.5, -1), (0.5, -1), (1, -0.6)]
    prings = []
    for t, hq, hf in PR:
        c = J + d * t
        w = w_arm(t, side)
        prings.append([P.v(c + fr * (hf * a) + q * (hq * b), w) for a, b in OCT])
    loft(P, prings, "M_Skin", cap1=P.v(J + d * 0.515, w_arm(0.515, side)))
    knuck = J + d * 0.507
    ARMB[side]["fingers"] = {}
    for name, off, segs in FINGERS:
        base = knuck + fr * off - q * 0.002
        spread = {"Index": 0.06, "Middle": 0.0, "Ring": -0.05, "Pinky": -0.11}[name]
        dirn = (d + fr * spread).normalized()
        pts, joints = [base], [base]
        cur, dcur = base, dirn
        for s in segs:
            dcur = (dcur - q * 0.16).normalized()          # a relaxed curl toward the palm
            cur = cur + dcur * s
            pts.append(cur); joints.append(cur)
        bones = ["%sHand%s%d" % (side, name, k) for k in (1, 2, 3)]
        ws = [{side + "Hand": 0.35, bones[0]: 0.65}, {bones[0]: 0.5, bones[1]: 0.5}, {bones[1]: 0.5, bones[2]: 0.5},
              {bones[2]: 1.0}]
        tube(P, pts, [0.0088, 0.0080, 0.0071, 0.0060], "M_Skin", ws, nseg=6, up=q)
        ARMB[side]["fingers"][name] = joints
    # the thumb: from the palm's front edge near the wrist, forward and down along the palm
    tb = J + d * 0.455 + fr * 0.027 - q * 0.004
    tdir = (d * 0.62 + fr * 0.55 - q * 0.45).normalized()
    pts = [tb]
    cur, dcur = tb, tdir
    for s in (0.030, 0.025, 0.022):
        dcur = (dcur + d * 0.25 - fr * 0.10).normalized()
        cur = cur + dcur * s
        pts.append(cur)
    bones = ["%sHandThumb%d" % (side, k) for k in (1, 2, 3)]
    tube(P, pts, [0.0102, 0.0094, 0.0082, 0.0068], "M_Skin",
         [{side + "Hand": 0.5, bones[0]: 0.5}, {bones[0]: 0.5, bones[1]: 0.5}, {bones[1]: 0.5, bones[2]: 0.5},
          {bones[2]: 1.0}], nseg=6, up=q)
    ARMB[side]["fingers"]["Thumb"] = pts
    print("[x3d] %s arm + hand: %d verts" % (side, len(P.V)))
    return P


# ------------------------------------------------------------------ the pants: one mesh, one ring into two legs
T_HIP = [(0.969, 0.095, 0.080, 0.082), (0.925, 0.110, 0.092, 0.085), (0.877, 0.127, 0.099, 0.094),
         (0.805, 0.148, 0.106, 0.101), (0.745, 0.160, 0.108, 0.105)]
T_LEG = [  # z, centre x, centre y, half-width, front, back (the inner side is round: legs almost touch)
    (0.720, 0.080, -0.002, 0.083, 0.112, 0.106), (0.660, 0.083, -0.006, 0.085, 0.118, 0.112),
    (0.600, 0.085, -0.008, 0.087, 0.124, 0.118), (0.520, 0.087, -0.004, 0.090, 0.134, 0.134),
    (0.440, 0.091, 0.000, 0.094, 0.142, 0.148), (0.360, 0.098, 0.012, 0.102, 0.148, 0.156),
    (0.280, 0.108, 0.024, 0.112, 0.152, 0.160), (0.200, 0.115, 0.030, 0.120, 0.152, 0.162),
    (0.140, 0.119, 0.034, 0.124, 0.148, 0.158), (0.090, 0.119, 0.035, 0.124, 0.144, 0.154)]
KNEE, HIPJ, ANKLE = 0.44, 0.84, 0.078
LEGX = {"hip": 0.085, "knee": 0.091, "ankle": 0.119}       # the legs splay a little: the sheet's wide stance
TOE_OUT = 12.0                                              # and the feet turn out


def foot_turn(sg, p):
    """turn a point of the foot about its ankle's vertical: the toes out by TOE_OUT degrees"""
    ax, ay = sg * LEGX["ankle"], 0.060
    a = math.radians(TOE_OUT) * sg
    dx, dy = p[0] - ax, p[1] - ay
    return (ax + dx * math.cos(a) - dy * math.sin(a), ay + dx * math.sin(a) + dy * math.cos(a), p[2])


def w_leg(x, z):
    side = "Left" if x > 0 else "Right"
    leg = smoothstep(0.52, 0.38, z)
    hips = 0.35 * smoothstep(0.69, 0.76, z)
    return norm_w({"Hips": hips, side + "UpLeg": (1 - hips) * (1 - leg), side + "Leg": (1 - hips) * leg})


def w_hip(x, z):
    legs = smoothstep(0.86, 0.745, z)
    sl = min(1.0, max(0.0, 0.5 + x / 0.08))
    return norm_w({"Hips": 1 - legs, "LeftUpLeg": legs * sl, "RightUpLeg": legs * (1 - sl)})


FOLD = [0.0, 0.006, -0.004, 0.007, -0.002, 0.008, 0.0, -0.005, 0.006, -0.003, 0.005, 0.002, 0.0]


def build_pants():
    P = new_part("pants")
    N = 24
    rings = []
    for z, hw, f, b in T_HIP:
        rings.append([P.v((*ring_xy(2 * math.pi * j / N, hw, f, b, 0.004, 0.86), z), w_hip(ring_xy(2 * math.pi * j / N, hw, f, b, 0.004, 0.86)[0], z))
                      for j in range(N)])
    loft(P, rings, "M_Pants")
    crotch = rings[-1]
    legs = {}
    for sg, side in ((1, "Left"), (-1, "Right")):
        lr = []
        for li, (z0, cx, cy, hw, f, b) in enumerate(T_LEG):
            ring = []
            for k in range(13):
                phi = math.radians(-65.0 + k * 310.0 / 12.0) if sg > 0 else math.radians(245.0 - k * 310.0 / 12.0)
                s, c = math.sin(phi), math.cos(phi)
                z = z0 + (0.040 * max(0.0, c) ** 1.5 if li == len(T_LEG) - 1 else 0.0)
                fold = FOLD[k] * (0.3 + 0.7 * smoothstep(0.70, 0.30, z)) * (1 if li % 2 else -1)
                x = sg * cx + sg * (hw + fold) * math.copysign(abs(s) ** 0.9, s)
                y = cy - ((f if c >= 0 else b) + fold) * math.copysign(abs(c) ** 0.9, c)
                ring.append(P.v((x, y, z), w_leg(sg, z)))
            lr.append(ring)
        legs[side] = lr
        for a, bb in zip(lr[:-1], lr[1:]):
            for k in range(12):
                P.f((a[k], a[k + 1], bb[k + 1], bb[k]), "M_Pants")
            P.f((a[12], a[0], bb[0], bb[12]), "M_Pants")
    # the crotch: the left lobe of the last hip ring (vertices 0..12) into the left leg, 12..24 into the right
    L0, R0 = legs["Left"][0], legs["Right"][0]
    for k in range(12):
        P.f((crotch[k], crotch[k + 1], L0[k + 1], L0[k]), "M_Pants")
        P.f((crotch[(12 + k) % N], crotch[(13 + k) % N], R0[k + 1], R0[k]), "M_Pants")
    P.f((crotch[12], crotch[0], L0[0], L0[12]), "M_Pants")
    P.f((crotch[0], crotch[12], R0[0], R0[12]), "M_Pants")
    # the hems: a turned-up inner band so the inside never shows the sky
    for side, lr in legs.items():
        sg = 1 if side == "Left" else -1
        z, cx, cy, hw, f, b = T_LEG[-1]
        inner = []
        for k in range(13):
            vx, vy, vz = P.V[lr[-1][k]]
            inner.append(P.v((sg * cx + (vx - sg * cx) * 0.9, cy + (vy - cy) * 0.9, vz + 0.035), w_leg(sg, 0.125)))
        for k in range(12):
            P.f((lr[-1][k + 1], lr[-1][k], inner[k], inner[k + 1]), "M_Pants")
        P.f((lr[-1][0], lr[-1][12], inner[12], inner[0]), "M_Pants")
    # the cargo pockets: padded patches on each leg's outer side, across the knee, each with a flap
    for sg in (1, -1):
        pocket(P, sg, 64.0, 122.0, 0.405, 0.575, 0.020)
        pocket(P, sg, 60.0, 126.0, 0.548, 0.600, 0.030)
    print("[x3d] pants: %d verts, %d faces" % (len(P.V), len(P.F)))
    return P, legs


def lerp_leg(z):
    return [z] + lerp_table(sorted(T_LEG), z)


def leg_surface(sg, phi_deg, z):
    """a point on a pant leg: phi 0 = its front, 90 = its outer side"""
    _, cx, cy, hw, f, b = lerp_leg(z)
    s, c = math.sin(math.radians(phi_deg)), math.cos(math.radians(phi_deg))
    return Vector((sg * cx + sg * hw * math.copysign(abs(s) ** 0.9, s),
                   cy - (f if c >= 0 else b) * math.copysign(abs(c) ** 0.9, c), z))


def pocket(P, sg, phi0, phi1, zlo, zhi, depth, nu=4, nv=3):
    """a padded patch on the leg: a grid of the leg's own surface lifted along its normal (the border less
    than the middle, so it bulges), walled down to the fabric"""
    def n_at(phi, z):
        a = leg_surface(sg, phi + 0.5, z) - leg_surface(sg, phi - 0.5, z)
        b = leg_surface(sg, phi, z + 0.002) - leg_surface(sg, phi, z - 0.002)
        n = a.cross(b).normalized()
        return n if n.x * sg > 0 else -n
    top, bot = {}, {}
    for i in range(nu + 1):
        for j in range(nv + 1):
            phi = phi0 + (phi1 - phi0) * i / nu; z = zlo + (zhi - zlo) * j / nv
            p = leg_surface(sg, phi, z); n = n_at(phi, z)
            edge = i in (0, nu) or j in (0, nv)
            top[i, j] = P.v(p + n * depth * (0.6 if edge else 1.0), w_leg(sg, z))
            if edge:
                bot[i, j] = P.v(p + n * 0.002, w_leg(sg, z))
    for i in range(nu):
        for j in range(nv):
            P.f((top[i, j], top[i + 1, j], top[i + 1, j + 1], top[i, j + 1]), "M_Pants")
    ring = [(i, 0) for i in range(nu)] + [(nu, j) for j in range(nv)] + [(i, nv) for i in range(nu, 0, -1)] + \
           [(0, j) for j in range(nv, 0, -1)]
    for k in range(len(ring)):
        a, b = ring[k], ring[(k + 1) % len(ring)]
        P.f((bot[a], bot[b], top[b], top[a]), "M_Pants")


def box_on(P, xo, yc, zlo, zhi, half_y, depth, sg, mat, wf):
    """a box standing out of a side surface at x = xo (sg: which way is out); wf(z) -> weights, so a pocket
    across the knee bends with it"""
    xs = (xo, xo + sg * depth)
    corners = []
    for x in xs:
        for y in (yc - half_y, yc + half_y):
            for z in (zlo, zhi):
                corners.append(P.v((x, y, z), wf(z)))
    c = corners  # index = xi*4 + yi*2 + zi
    faces = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]
    for fc in faces:
        P.f([c[i] for i in fc], mat)


# ------------------------------------------------------------------ belt, chain, shoes, band, necklace
def build_belt():
    P = new_part("belt")
    N = 24
    rings = []
    for z, grow in ((0.925, 0.004), (0.931, 0.006), (0.963, 0.006), (0.969, 0.004)):
        hw, f, b = hip_at(z)
        rings.append([P.v((*ring_xy(2 * math.pi * j / N, hw + grow, f + grow, b + grow, 0.004, 0.86), z), {"Hips": 1.0})
                      for j in range(N)])
    loft(P, rings, "M_Belt")
    # the buckle: a flat plate on the front
    hw, f, b = hip_at(0.947)
    y0 = 0.004 - f - 0.007
    for (x0, x1, z0, z1, dy) in ((-0.022, 0.022, 0.933, 0.961, 0.004),):
        v = [P.v((x, y0 - dy * k, z), {"Hips": 1.0}) for k in (0, 1) for x in (x0, x1) for z in (z0, z1)]
        for fc in [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]:
            P.f([v[i] for i in fc], "M_Metal", ("swatch", "buckle"))
    # belt loops
    for ang in (-150, -100, -38, 38, 100, 150):
        th = math.radians(ang)
        x, y = ring_xy(th, hw + 0.007, f + 0.007, b + 0.007, 0.004, 0.86)
        n = Vector((x, y - 0.004, 0)).normalized()
        t = Vector((0, 0, 1)).cross(n).normalized()
        c = Vector((x, y, 0.947))
        v = [P.v(c + t * (0.006 * sx) + n * (0.003 * sn) + Vector((0, 0, 0.024 * sz)), {"Hips": 1.0})
             for sn in (-1, 1) for sx in (-1, 1) for sz in (-1, 1)]
        for fc in [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]:
            P.f([v[i] for i in fc], "M_Belt")
    return P


def hip_at(z):
    t = sorted([(r[0], r[1], r[2], r[3]) for r in T_HIP])
    return lerp_table(t, z)


def build_chain():
    """the wallet chain: a grey clip strap off the front-left belt loop, then oval links sagging round the
    left hip to a loop at the side"""
    P = new_part("chain")
    hw, f, b = hip_at(0.947)
    th0 = math.radians(38)
    x0, y0 = ring_xy(th0, hw + 0.010, f + 0.010, b + 0.010, 0.004, 0.86)
    top = Vector((x0, y0, 0.940))
    bot = Vector((x0 + 0.006, y0 - 0.004, 0.868))
    # the strap
    n = Vector((x0, y0 - 0.004, 0)).normalized(); t = Vector((0, 0, 1)).cross(n).normalized()
    v = [P.v(p + t * (0.0065 * sx) + n * (0.0018 * sn), w_chain(p)) for sn in (-1, 1) for p in (top, bot) for sx in (-1, 1)]
    for fc in [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]:
        P.f([v[i] for i in fc], "M_Chain")
    # the links along a quadratic curve: from the strap's end down round the hip to the side loop
    th1 = math.radians(106)
    x1, y1 = ring_xy(th1, hw + 0.012, f + 0.012, b + 0.012, 0.004, 0.86)
    end = Vector((x1, y1, 0.935))
    # the sag's control point: out on the front of the thigh, low (the loop's bottom ends near z 0.72)
    ctrl = Vector((0.170, -0.150, 0.500))
    pts = []
    for i in range(400):
        s = i / 399.0
        pts.append(bot * (1 - s) ** 2 + ctrl * 2 * s * (1 - s) + end * s * s)
    L = sum((pts[i + 1] - pts[i]).length for i in range(399))
    pitch = 0.036
    nlinks = int(L / pitch)
    acc, k, targets = 0.0, 0, [pitch * (i + 0.5) for i in range(nlinks)]
    for i in range(399):
        seg = (pts[i + 1] - pts[i]).length
        while k < len(targets) and acc + seg >= targets[k]:
            s = (targets[k] - acc) / seg
            c = pts[i].lerp(pts[i + 1], s)
            tan = (pts[i + 1] - pts[i]).normalized()
            link(P, c, tan, k % 2, w_chain(c))
            k += 1
        acc += seg
    print("[x3d] chain: %d links" % nlinks)
    return P


def w_chain(p):
    leg = smoothstep(0.92, 0.80, p.z) * 0.8
    return norm_w({"Hips": 1 - leg, "LeftUpLeg": leg})


def link(P, c, tan, turn, w):
    """one oval chain link: a low-poly torus, alternate links turned 90 degrees about the chain"""
    a = Vector((0, 1, 0)) if abs(tan.y) < 0.9 else Vector((1, 0, 0))
    u = tan.cross(a).normalized(); v = tan.cross(u).normalized()
    if turn: u, v = v, u
    R1, R2, r = 0.0205, 0.0125, 0.0045
    NM, Nm = 8, 4
    rings = []
    for i in range(NM):
        ang = 2 * math.pi * i / NM
        ctr = c + tan * (R1 * math.cos(ang)) + u * (R2 * math.sin(ang))
        radial = (tan * (math.cos(ang) / R1) + u * (math.sin(ang) / R2)).normalized()
        rings.append([P.v(ctr + radial * (r * math.cos(2 * math.pi * j / Nm)) + v * (r * math.sin(2 * math.pi * j / Nm)), w)
                      for j in range(Nm)])
    for i in range(NM):
        a_, b_ = rings[i], rings[(i + 1) % NM]
        for j in range(Nm):
            P.f((a_[j], a_[(j + 1) % Nm], b_[(j + 1) % Nm], b_[j]), "M_Chain")


def build_shoes():
    P = new_part("shoes")
    P_v0 = P.v
    P.v = lambda p, w: P_v0(foot_turn(1 if p[0] > 0 else -1, p), w)
    # sections along the foot, heel (+y) to toe (-y): y, half-width, top height of the upper
    # chunky skate shoes (the sheet's are big: a thick sole, a tall toe box, laces across the front)
    SEC = [(0.150, 0.058, 0.098), (0.130, 0.066, 0.108), (0.090, 0.071, 0.118), (0.040, 0.073, 0.112),
           (0.000, 0.075, 0.102), (-0.050, 0.077, 0.092), (-0.100, 0.076, 0.086), (-0.140, 0.069, 0.080),
           (-0.168, 0.057, 0.072), (-0.186, 0.038, 0.062)]
    SOLE = 0.044
    for sg, side in ((1, "Left"), (-1, "Right")):
        cx = sg * LEGX["ankle"]
        ball = -0.080
        def w(y):
            toe = smoothstep(ball + 0.03, ball - 0.03, y)
            return norm_w({side + "Foot": 1 - toe, side + "ToeBase": toe})
        up = []
        for y, hw, top in SEC:
            # a rounded section: the sole's top edge, the sides, the rounded top
            prof = [(-1.0, 0.0), (-1.0, 0.45), (-0.85, 0.85), (-0.4, 1.0), (0.4, 1.0), (0.85, 0.85), (1.0, 0.45), (1.0, 0.0)]
            up.append([P.v((cx + hw * a, y, SOLE + (top - SOLE) * b), w(y)) for a, b in prof])
        # upper: loft sections, cap the heel and the toe
        for a_, b_ in zip(up[:-1], up[1:]):
            for j in range(7):
                P.f((a_[j], a_[j + 1], b_[j + 1], b_[j]), "M_Shoe")
        heel = P.v((cx, 0.152, SOLE + 0.04), w(0.152)); toe = P.v((cx, -0.190, SOLE + 0.012), w(-0.190))
        for j in range(7):
            P.f((heel, up[0][j + 1], up[0][j]), "M_Shoe"); P.f((toe, up[-1][j], up[-1][j + 1]), "M_Shoe")
        # the sole: a flared white slab under it all
        sole_top, sole_bot = [], []
        for y, hw, top in SEC:
            sole_top.append((P.v((cx - hw - 0.006, y, SOLE), w(y)), P.v((cx + hw + 0.006, y, SOLE), w(y))))
            sole_bot.append((P.v((cx - hw - 0.004, y, 0.0), w(y)), P.v((cx + hw + 0.004, y, 0.0), w(y))))
        for i in range(len(SEC) - 1):
            (tl, tr), (tl2, tr2) = sole_top[i], sole_top[i + 1]
            (bl, br), (bl2, br2) = sole_bot[i], sole_bot[i + 1]
            P.f((tl, tr, tr2, tl2), "M_Sole"); P.f((bl, bl2, br2, br), "M_Sole")
            P.f((tl, tl2, bl2, bl), "M_Sole"); P.f((tr, br, br2, tr2), "M_Sole")
        for (tl, tr), (bl, br) in ((sole_top[0], sole_bot[0]), (sole_top[-1], sole_bot[-1])):
            P.f((tl, bl, br, tr), "M_Sole")
        # two white straps across the top of the forefoot, and a white patch on each side of the heel
        for y0 in (-0.022, -0.052, -0.082, -0.112):
            strap = []
            for y in (y0 + 0.009, y0 - 0.009):
                hw, top = sec_at(SEC, y)
                prof = [(-0.75, 0.93), (-0.55, 1.0), (-0.2, 1.03), (0.2, 1.03), (0.55, 1.0), (0.75, 0.93)]
                strap.append([P.v((cx + hw * a, y, SOLE + (top - SOLE) * b + 0.002), w(y)) for a, b in prof])
            for j in range(5):
                P.f((strap[0][j], strap[0][j + 1], strap[1][j + 1], strap[1][j]), "M_Sole", ("swatch", "sole"))
        for sx in (-1, 1):
            hw, top = sec_at(SEC, 0.075)
            x = cx + sx * (hw + 0.003)
            v = [P.v((x, y, z), w(0.07)) for y in (0.045, 0.105) for z in (SOLE + 0.010, SOLE + 0.040)]
            P.f((v[0], v[1], v[3], v[2]) if sx > 0 else (v[0], v[2], v[3], v[1]), "M_Sole", ("swatch", "sole"))
    return P


def sec_at(SEC, y):
    t = sorted([(s[0], s[1], s[2]) for s in SEC])
    return lerp_table(t, y)


def build_band():
    """the black band on her LEFT wrist (the sheet's 3/4 and side views)"""
    P = new_part("band")
    A = ARMB["Left"]
    d, q = A["d"], A["q"]
    fr = Vector((0, -1, 0))
    rings = []
    for t in (0.388, 0.392, 0.404, 0.408):
        rq, rf = lerp_table([(r[0], r[1], r[2]) for r in T_ARM], t)
        g = 0.0015 if t in (0.388, 0.408) else 0.0035
        c = A["joint"] + d * t
        rings.append([P.v(c + q * ((rq + g) * math.cos(2 * math.pi * k / 10)) + fr * ((rf + g) * math.sin(2 * math.pi * k / 10)),
                          w_arm(t, "Left")) for k in range(10)])
    loft(P, rings, "M_Band")
    return P


def build_necklace():
    """a black cord round the neck and down in a V to a dark tag"""
    P = new_part("necklace")
    pts = []
    for i in range(13):                                  # round the back of the neck
        th = math.radians(60 + i * 20)                   # 60 .. 300 degrees
        x, y = ring_xy(th, 0.046, 0.040, 0.046, 0.008, 1.0)
        pts.append(Vector((x, y, 1.292)))
    left = torso_pt(math.radians(22), 1.255) + torso_normal(math.radians(22), 1.255) * 0.004
    right = torso_pt(math.radians(-22), 1.255) + torso_normal(math.radians(-22), 1.255) * 0.004
    bottom = torso_pt(0.0, 1.198) + torso_normal(0.0, 1.198) * 0.006
    chain = [bottom.lerp(right, s) for s in (0.0, 0.5, 1.0)] + [pts[-1]] + pts[::-1][1:-1] + [pts[0], left, bottom.lerp(left, 0.5), bottom]
    ws = [norm_w({"Neck": smoothstep(1.24, 1.29, p.z), "Spine2": 1 - smoothstep(1.24, 1.29, p.z)}) for p in chain]
    tube(P, chain, [0.0016] * len(chain), "M_Band", ws, nseg=4, cap_end=True)
    # the tag, hanging at the V
    c = bottom + Vector((0, -0.003, -0.013))
    v = [P.v(c + Vector((0.0060 * sx, 0.0015 * sy, 0.011 * sz)), {"Spine2": 1.0}) for sy in (-1, 1) for sx in (-1, 1) for sz in (-1, 1)]
    for fc in [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]:
        P.f([v[i] for i in fc], "M_Metal", ("swatch", "pendant"))
    return P


# ------------------------------------------------------------------ the hair: one thick shell, the face its opening
T_HAIR = [  # z, half-width, front y, back y, opening half-angle (deg), thickness
    (1.664, 0.022, 0.030, 0.062, 0.0, 0.010),
    (1.655, 0.052, -0.005, 0.090, 0.0, 0.014),
    (1.638, 0.083, -0.036, 0.118, 0.0, 0.018),
    (1.620, 0.102, -0.066, 0.140, 0.0, 0.020),
    (1.602, 0.115, -0.089, 0.153, 0.0, 0.022),
    (1.584, 0.123, -0.100, 0.158, 0.0, 0.022),
    (1.566, 0.128, -0.106, 0.162, 0.0, 0.022),
    (1.548, 0.133, -0.108, 0.166, 24.0, 0.024),
    (1.530, 0.137, -0.109, 0.170, 40.0, 0.026),
    (1.505, 0.140, -0.110, 0.173, 50.0, 0.028),
    (1.480, 0.142, -0.106, 0.175, 55.0, 0.030),
    (1.455, 0.143, -0.100, 0.176, 57.0, 0.032),
    (1.430, 0.143, -0.096, 0.176, 57.0, 0.033),
    (1.400, 0.144, -0.090, 0.176, 55.0, 0.034),
    (1.365, 0.145, -0.080, 0.176, 56.0, 0.036),
    (1.330, 0.145, -0.062, 0.176, 62.0, 0.040),
    (1.300, 0.140, -0.040, 0.175, 80.0, 0.040),
    (1.270, 0.128, -0.012, 0.172, 90.0, 0.080),
    (1.235, 0.112, -0.016, 0.168, 92.0, 0.088),
    (1.200, 0.102, -0.012, 0.164, 92.0, 0.084),
    (1.150, 0.094, -0.003, 0.155, 92.0, 0.075),
    (1.100, 0.090, 0.005, 0.147, 92.0, 0.067),
    (1.058, 0.086, 0.010, 0.142, 92.0, 0.062),
]


def w_hair(z):
    hd = smoothstep(1.245, 1.335, z)
    nk = (1 - hd) * smoothstep(1.20, 1.27, z)
    sp1 = (1 - hd - nk) * (1 - smoothstep(1.06, 1.14, z)) * 0.4
    return norm_w({"Head": hd, "Neck": nk * 0.6, "Spine2": (1 - hd) - nk * 0.6 - sp1, "Spine1": sp1})


# ⭐ HAIR PHYSICS (jacob, 09-27: "could you give her hair physics"). The club's chain solver (crowd_kms.c H1:
# verlet, a spring toward the animated pose, measured gravity) mines any bone chain whose names say hair, so she
# carries four: three down her back -- rooted in Spine2, so the animated pose is hair LYING ON HER BACK and the
# solver adds the lag and the swing -- and one for the front lock, rooted in her head. Each back vertex rides two
# neighbouring chains across and two bones along; above her neck the weights fade back to the head's.
BACK_X = {"L": 0.058, "C": 0.0, "R": -0.058}                 # her left is +X
BACK_Z = (1.330, 1.250, 1.170, 1.100, 1.050)                 # the four bones' heads, then the tail
BACK_Y = (0.135, 0.125, 0.118, 0.112, 0.108)
LOCKJ = []                                                   # the lock's joints, filled by build_lock


def chain_vert(z, zs):
    """weights along a hanging chain whose joints are zs (descending), by height: between two bones'
    midpoints a vertex blends them"""
    mids = [(zs[k] + zs[k + 1]) / 2.0 for k in range(len(zs) - 1)]
    if z >= mids[0]: return {0: 1.0}
    if z <= mids[-1]: return {len(mids) - 1: 1.0}
    for k in range(len(mids) - 1):
        if mids[k] >= z >= mids[k + 1]:
            t = (mids[k] - z) / (mids[k] - mids[k + 1])
            return {k: 1.0 - t, k + 1: t}
    return {0: 1.0}


def w_back(x, z):
    names = ("L", "C", "R"); xs = (BACK_X["L"], BACK_X["C"], BACK_X["R"])
    if x >= xs[0]: lat = {0: 1.0}
    elif x <= xs[2]: lat = {2: 1.0}
    elif x > 0: t = (xs[0] - x) / xs[0]; lat = {0: 1.0 - t, 1: t}
    else: t = -x / -xs[2]; lat = {1: 1.0 - t, 2: t}
    w = {}
    for ci, a in lat.items():
        for k, b in chain_vert(z, BACK_Z).items():
            w["Hair_Back_%s%d" % (names[ci], k + 1)] = a * b
    return w


def w_hair_x(p, a, z):
    """the hair shell: the head's weights above her neck, the back chains below, the curtains beside her jaw
    staying with the head"""
    base = w_hair(z)
    dev = abs(math.degrees(a) - 180.0)                        # 0 at the back's centre, 180 at the part
    side = smoothstep(80.0, 55.0, dev)
    bf = smoothstep(1.37, 1.30, z) * (side + (1.0 - side) * (1.0 - smoothstep(1.26, 1.30, z)))
    if bf <= 0.0: return base
    w = {k: v * bf for k, v in w_back(p.x, z).items()}
    for k, v in base.items(): w[k] = w.get(k, 0.0) + v * (1.0 - bf)
    return norm_w(w)


def build_hair():
    P = new_part("hair")
    P.weld = True                                # the part's seam closes: the arc's two ends meet above the hairline
    M = 18                                       # points per arc: big flat planes, as the sheet's
    outer, inner = [], []
    for z, hw, yf, yb, open_deg, th in T_HAIR:
        cy = (yf + yb) / 2.0
        f, b = cy - yf, yb - cy
        o = math.radians(open_deg)
        oring, iring = [], []
        for k in range(M + 1):
            a = o + (2 * math.pi - 2 * o) * k / M
            x, y = ring_xy(a, hw, f, b, cy, 0.82)
            # the part: the crown's centre line dips a little either side of the seam
            if open_deg <= 0.0:
                z_ = z - 0.0035 * math.exp(-((min(a, 2 * math.pi - a)) / 0.12) ** 2)
            else:
                z_ = z
            # the bottom edge's centre notch
            if z == T_HAIR[-1][0]:
                back = abs(math.degrees(a) - 180.0)
                z_ = z + 0.045 * max(0.0, 1.0 - back / 8.0)
            p = Vector((x, y, z_))
            radial = Vector((x, y - cy, 0.0))
            radial = radial.normalized() if radial.length > 1e-9 else Vector((0, 1, 0))
            # round the head the thickness is radial (the curtains framing the face); down her back it is
            # FRONT-TO-BACK and fades to nothing at the sides -- radial there laid the thickness flat on her
            # shoulders as two dark pads (round 2)
            q_head = p - radial * th
            q_back = p - Vector((0.0, th * max(0.0, -math.cos(a)) ** 0.6, 0.0))
            t = smoothstep(1.335, 1.270, z)
            qi = q_head.lerp(q_back, t)
            oring.append(P.v(p, w_hair_x(p, a, z_)))
            iring.append(P.v(qi, w_hair_x(qi, a, z_)))
        outer.append(oring); inner.append(iring)
    # outer and inner skins, the opening's rims (only where it is open), the bottom edge, and the crown cap
    for i in range(len(outer) - 1):
        for k in range(M):
            P.f((outer[i][k], outer[i + 1][k], outer[i + 1][k + 1], outer[i][k + 1]), "M_Hair")
            P.f((inner[i][k], inner[i][k + 1], inner[i + 1][k + 1], inner[i + 1][k]), "M_Hair")
        if T_HAIR[i][4] > 0 or T_HAIR[i + 1][4] > 0:
            for k in (0, M):
                P.f((outer[i][k], inner[i][k], inner[i + 1][k], outer[i + 1][k]), "M_Hair")
    last = len(outer) - 1
    for k in range(M):
        P.f((outer[last][k], outer[last][k + 1], inner[last][k + 1], inner[last][k]), "M_Hair")
    top_o = P.v((0.0, 0.008, 1.665), {"Head": 1.0}); top_i = P.v((0.0, 0.008, 1.655), {"Head": 1.0})
    for k in range(M):
        P.f((top_o, outer[0][k + 1], outer[0][k]), "M_Hair")
        P.f((top_i, inner[0][k], inner[0][k + 1]), "M_Hair")
    print("[x3d] hair: %d verts, %d faces" % (len(P.V), len(P.F)))
    return P


def hair_outer(z, a):
    """a point on the hair shell's outer surface at height z and ring angle a, and its outward normal"""
    hw, yf, yb, open_deg, th = lerp_table(sorted(T_HAIR), z)
    cy = (yf + yb) / 2.0
    x, y = ring_xy(a, hw, cy - yf, yb - cy, cy, 0.82)
    return Vector((x, y, z)), Vector((x, y - cy, 0.0)).normalized(), open_deg


def build_lock():
    """her right front lock (the sheet's front and 3/4 views): not a board hung beside her face (round 5, jacob:
    'improve this bang') but the curtain's own front edge carried on -- flush on the curtain from the temple to
    the jaw, then falling forward over her shoulder to her chest, tapering to a point"""
    P = new_part("lock")
    cent, acr, nrm, wid = [], [], [], []
    for z in (1.548, 1.515, 1.478, 1.440, 1.400, 1.360):      # on the curtain
        o = lerp_table(sorted(T_HAIR), z)[3]
        a = 2.0 * math.pi - math.radians(o + 8.0)               # just behind the opening's edge, her right side
        pnt, n, _ = hair_outer(z, a)
        p2, _, _ = hair_outer(z, a - 0.01)
        cent.append(pnt + n * 0.004); nrm.append(n); acr.append((pnt - p2).normalized())
        wid.append({1.548: 0.022, 1.515: 0.040}.get(z, 0.054))
    # below the jaw it LIES ON HER: over the front of the right shoulder, then down the chest above the bust.
    # ⛔ Round 6 hung it in the air 5 cm in front of her shoulder, a loose string; and a lock placed out at her
    # chest's SIDE sits inside the arm once the arms hang. The points are the torso's own surface, lifted.
    def on_torso(x, z, lift):
        lo, hi = -math.pi / 2, 0.0                                # her front-right quarter: x runs 0 .. -hw
        for _ in range(40):
            mid = (lo + hi) / 2.0
            if torso_pt(mid, z).x > x: hi = mid
            else: lo = mid
        th = (lo + hi) / 2.0
        n = torso_normal(th, z)
        if n.y > 0: n = -n
        return torso_pt(th, z) + n * lift, n
    free = [(Vector((-0.130, -0.030, 1.300)), Vector((-0.30, -0.95, 0.20))),
            (Vector((-0.116, -0.034, 1.268)), Vector((-0.25, -0.90, 0.35)))]
    hang = free + [on_torso(-0.076, 1.215, 0.013), on_torso(-0.066, 1.168, 0.013), on_torso(-0.058, 1.128, 0.012)]
    widths = (0.052, 0.046, 0.038, 0.027, 0.010)
    for i, (c, n) in enumerate(hang):
        n = n.normalized()
        cent.append(c); nrm.append(n); wid.append(widths[i])
        a = Vector((0, 0, 1)).cross(n).normalized()
        acr.append(a if a.dot(acr[-1]) >= 0 else -a)
    LOCKJ[:] = [cent[5], cent[6], cent[7], cent[9], cent[10]]   # joints at z ~1.36, 1.305, 1.25, 1.15, the tip
    lz = [j.z for j in LOCKJ]
    LENS = [(1.0, 0.0), (0.5, 0.75), (0.0, 1.0), (-0.5, 0.75), (-1.0, 0.0), (-0.5, -0.45), (0.0, -0.6), (0.5, -0.45)]
    rings = []
    for i, c in enumerate(cent):
        hd = smoothstep(lz[0] - 0.005, lz[0] + 0.045, c.z)
        chain = {"Hair_Lock_R%d" % (k + 1): v for k, v in chain_vert(c.z, lz).items()}
        w = {k: v * (1 - hd) for k, v in chain.items()}; w["Head"] = w.get("Head", 0.0) + hd
        th = 0.020 if i < 8 else 0.012
        rings.append([P.v(c + acr[i] * (wid[i] / 2.0 * a) + nrm[i] * (th / 2.0 * b), norm_w(w)) for a, b in LENS])
    tip = P.v(cent[-1] + (cent[-1] - cent[-2]).normalized() * 0.012, {"Hair_Lock_R4": 1.0})
    top = P.v(cent[0] + (cent[0] - cent[1]).normalized() * 0.010, {"Head": 1.0})
    loft(P, rings, "M_Hair", cap0=top, cap1=tip)
    return P


# ------------------------------------------------------------------ build it
torso = build_torso()
head = build_head()
build_arm(1); build_arm(-1)
pants, LEGS = build_pants()
build_belt(); build_chain(); build_shoes(); build_band(); build_necklace()
hair = build_hair()
build_lock()


# ------------------------------------------------------------------ the atlas: swatches, the face, the star
SZ = 2048
ATL = np.zeros((SZ, SZ, 3), np.float32)
ATL[:] = np.array(COLOR["skin"]) / 255.0
for k, name in enumerate(SWATCH):
    col, row = k % 8, k // 8
    ATL[row * 128:(row + 1) * 128, col * 128:(col + 1) * 128] = np.array(COLOR[name]) / 255.0
ATL[1024:2048, 0:1024] = np.array(COLOR["skin"]) / 255.0
ATL[1024:2048, 1024:2048] = np.array(COLOR["top"]) / 255.0


def to_px(x, z, box, u0):
    x0, x1, z0, z1 = box
    return (u0 + (x - x0) / (x1 - x0) * 0.5) * SZ, (0.5 + (z - z0) / (z1 - z0) * 0.5) * SZ


def coverage(poly, pad=2, ss=3):
    xs = [p[0] for p in poly]; ys = [p[1] for p in poly]
    x0, x1 = max(0, int(min(xs)) - pad), min(SZ, int(max(xs)) + pad + 1)
    y0, y1 = max(0, int(min(ys)) - pad), min(SZ, int(max(ys)) + pad + 1)
    X, Y = np.meshgrid(x0 + (np.arange((x1 - x0) * ss) + 0.5) / ss, y0 + (np.arange((y1 - y0) * ss) + 0.5) / ss)
    inside = np.zeros(X.shape, bool)
    n = len(poly)
    for i in range(n):
        xa, ya = poly[i]; xb, yb = poly[(i + 1) % n]
        if ya == yb: continue
        cond = (ya > Y) != (yb > Y)
        xint = xa + (Y - ya) * (xb - xa) / (yb - ya)
        inside ^= cond & (X < xint)
    cov = inside.reshape(y1 - y0, ss, x1 - x0, ss).mean(axis=(1, 3))
    full = np.zeros((SZ, SZ), np.float32); full[y0:y1, x0:x1] = cov
    return full


def paint(cov, name):
    c = np.array(COLOR[name] if isinstance(name, str) else name, np.float32) / 255.0
    ATL[:] = ATL * (1 - cov[..., None]) + c * cov[..., None]


def bez(p0, c, p1, n=24):
    return [(p0[0] * (1 - t) ** 2 + c[0] * 2 * t * (1 - t) + p1[0] * t * t,
             p0[1] * (1 - t) ** 2 + c[1] * 2 * t * (1 - t) + p1[1] * t * t) for t in np.linspace(0, 1, n)]


def circle(cx, cz, r, n=48):
    return [(cx + r * math.cos(2 * math.pi * i / n), cz + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def face_poly(pts, sg=1):
    return [to_px(sg * x, z, FACE_BOX, 0.0) for x, z in pts]


def bez3(p0, c1, c2, p1, n=28):
    return [(p0[0] * (1 - t) ** 3 + 3 * c1[0] * t * (1 - t) ** 2 + 3 * c2[0] * t * t * (1 - t) + p1[0] * t ** 3,
             p0[1] * (1 - t) ** 3 + 3 * c1[1] * t * (1 - t) ** 2 + 3 * c2[1] * t * t * (1 - t) + p1[1] * t ** 3)
            for t in np.linspace(0, 1, n)]


def paint_face():
    t0 = time.time()
    for sg in (1, -1):
        I, O = (0.029, 1.440), (0.089, 1.466)
        up = bez3(I, (0.034, 1.466), (0.068, 1.486), O)
        lo = bez3(O, (0.092, 1.436), (0.052, 1.414), I)
        eye = coverage(face_poly(up + lo[1:-1], sg))
        paint(eye, "white")
        paint(coverage(face_poly(circle(0.051, 1.455, 0.0200), sg)) * eye, (66, 44, 62))
        paint(coverage(face_poly(circle(0.051, 1.455, 0.0160), sg)) * eye, (46, 30, 44))
        paint(coverage(face_poly(circle(0.051, 1.456, 0.0095), sg)) * eye, "black")
        paint(coverage(face_poly(circle(0.045, 1.463, 0.0035), sg)) * eye, (242, 232, 238))
        paint(coverage(face_poly(circle(0.058, 1.449, 0.0015), sg)) * eye, (230, 216, 224))
        # the upper lash line: thin at the inner corner, heavy outward, and out into a wing
        th = [0.0016 + 0.0040 * t ** 0.8 for t in np.linspace(0, 1, len(up))]
        top = [(x, z + t) for (x, z), t in zip(up, th)]
        paint(coverage(face_poly(up + [(0.098, 1.478)] + top[::-1], sg)), "black")
        seg = lo[:16]                                           # the lower lid: a thin darker line
        paint(coverage(face_poly(seg + [(x, z - 0.0012) for x, z in seg[::-1]], sg)), "skin_dark")
        brow = [(0.027, 1.4925), (0.029, 1.5045), (0.066, 1.5230), (0.071, 1.5185), (0.066, 1.5115),
                (0.034, 1.4955)]
        paint(coverage(face_poly(brow, sg)), "hair")
    lips = [(0.011 * math.cos(a), 1.3735 + 0.0035 * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 40, endpoint=False)]
    paint(coverage(face_poly(lips)), "lip")
    line = bez((-0.016, 1.3790), (0.0, 1.3765), (0.016, 1.3790), 16)
    paint(coverage(face_poly(line + [(x, z - 0.0014) for x, z in line[::-1]])), (104, 34, 68))
    paint(coverage(face_poly([(-0.009, 1.4015), (0.009, 1.4015), (0.0, 1.3965)])), "skin_dark")
    # the star on the tank, a point up
    star = []
    for i in range(10):
        a = math.pi / 2 + i * math.pi / 5
        r = 0.029 if i % 2 == 0 else 0.0120
        star.append(to_px(r * math.cos(a), 1.150 + r * math.sin(a), PRINT_BOX, 0.5))
    paint(coverage(star), "star")
    print("[x3d] atlas painted in %.1f s" % (time.time() - t0))


paint_face()
IMG = bpy.data.images.new("x3d_atlas", SZ, SZ, alpha=False)
IMG.pixels.foreach_set(np.concatenate([ATL.reshape(-1, 3), np.ones((SZ * SZ, 1), np.float32)], 1).ravel())
IMG.filepath_raw = os.path.join(OUT, "x3d_atlas.png"); IMG.file_format = "PNG"; IMG.save()


# ------------------------------------------------------------------ materials (one atlas, several surfaces)
def material(name, rough, metal=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes.get("Principled BSDF")
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if "Specular IOR Level" in b.inputs:
        b.inputs["Specular IOR Level"].default_value = 0.25 if metal == 0.0 else 0.5
    tn = nt.nodes.new("ShaderNodeTexImage"); tn.image = IMG; tn.interpolation = "Linear"
    nt.links.new(tn.outputs["Color"], b.inputs["Base Color"])
    c = COLOR[MAT_SWATCH[name]]
    m.diffuse_color = (*[(v / 255.0) ** 2.2 for v in c], 1.0)
    return m


MAT = {"M_Skin": material("M_Skin", 0.75), "M_Hair": material("M_Hair", 0.7), "M_Top": material("M_Top", 0.9),
       "M_Pants": material("M_Pants", 0.95), "M_Belt": material("M_Belt", 0.7),
       "M_Metal": material("M_Metal", 0.35, 0.5), "M_Chain": material("M_Chain", 0.3, 0.6),
       "M_Shoe": material("M_Shoe", 0.8), "M_Sole": material("M_Sole", 0.9), "M_Band": material("M_Band", 0.7)}


# ------------------------------------------------------------------ parts -> objects -> one body
def to_object(part):
    me = bpy.data.meshes.new(part.name)
    me.from_pydata(part.V, [], part.F)
    for name in MATS:
        me.materials.append(MAT[name])
    me.polygons.foreach_set("material_index", [MATS.index(m) for m in part.M])
    me.polygons.foreach_set("use_smooth", part.S)
    uvs = []
    for fi, face in enumerate(part.F):
        spec = part.UV[fi] or ("swatch", MAT_SWATCH[part.M[fi]])
        for vi in face:
            uvs.extend(uv_of(spec, part.V[vi]))
    uvl = me.uv_layers.new(name="UVMap")
    uvl.data.foreach_set("uv", uvs)
    me.update()
    bm = bmesh.new(); bm.from_mesh(me)
    if part.weld:
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(part.name, me)
    scene.collection.objects.link(ob)
    groups = {}
    if part.weld:                    # the weld renumbered the vertices: weights by position instead
        byp = {}
        for vi, v in enumerate(part.V):
            byp[tuple(round(c, 6) for c in v)] = part.W[vi]
        part = type("W", (), {})()
        part.W = [byp.get(tuple(round(c, 6) for c in v.co), {"Head": 1.0}) for v in me.vertices]
    for vi, w in enumerate(part.W):
        for bone, x in w.items():
            groups.setdefault(bone, {}).setdefault(round(x * 255) / 255.0, []).append(vi)
    for bone, byw in groups.items():
        vg = ob.vertex_groups.new(name=bone)
        for x, idx in byw.items():
            vg.add(idx, x, "REPLACE")
    return ob


OBJS = [to_object(p) for p in PARTS]
with bpy.context.temp_override(active_object=OBJS[0], object=OBJS[0], selected_objects=OBJS, selected_editable_objects=OBJS):
    bpy.ops.object.join()
body = OBJS[0]; body.name = "x3d_body"; body.data.name = "x3d_body"
print("[x3d] one body: %d verts, %d faces, %d materials" % (len(body.data.vertices), len(body.data.polygons), len(body.data.materials)))


# ------------------------------------------------------------------ the skeleton (Mixamo names)
arm_data = bpy.data.armatures.new("x3d_rig")
rig = bpy.data.objects.new("x3d_rig", arm_data)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
E = arm_data.edit_bones


def bone(name, head, tail, parent=None):
    b = E.new(name); b.head = Vector(head); b.tail = Vector(tail)
    if parent: b.parent = E[parent]; b.use_connect = False
    return b


bone("Hips", (0, 0.005, 0.86), (0, 0.005, 0.93))
bone("Spine", (0, 0.005, 0.93), (0, 0.004, 1.01), "Hips")
bone("Spine1", (0, 0.004, 1.01), (0, 0.004, 1.10), "Spine")
bone("Spine2", (0, 0.004, 1.10), (0, 0.006, 1.265), "Spine1")
bone("Neck", (0, 0.006, 1.265), (0, 0.010, 1.36), "Spine2")
bone("Head", (0, 0.010, 1.36), (0, 0.010, 1.62), "Neck")
for sg, side in ((1, "Left"), (-1, "Right")):
    A = ARMB[side]
    bone(side + "Shoulder", (sg * 0.020, 0.0, 1.235), tuple(A["joint"] - Vector((sg * 0.012, 0, -0.003))), "Spine2")
    bone(side + "Arm", tuple(A["joint"]), tuple(A["elbow"]), side + "Shoulder")
    bone(side + "ForeArm", tuple(A["elbow"]), tuple(A["wrist"]), side + "Arm")
    bone(side + "Hand", tuple(A["wrist"]), tuple(A["joint"] + A["d"] * 0.507), side + "ForeArm")
    for name, pts in A["fingers"].items():
        prev = side + "Hand"
        for k in (1, 2, 3):
            nm = "%sHand%s%d" % (side, name, k)
            bone(nm, tuple(pts[k - 1]), tuple(pts[k]), prev)
            prev = nm
    hx, kx, ax = sg * LEGX["hip"], sg * LEGX["knee"], sg * LEGX["ankle"]
    bone(side + "UpLeg", (hx, 0.005, HIPJ), (kx, -0.005, KNEE), "Hips")
    bone(side + "Leg", (kx, -0.005, KNEE), (ax, 0.060, ANKLE), side + "UpLeg")
    ball, toe = foot_turn(sg, (ax, -0.080, 0.032)), foot_turn(sg, (ax, -0.160, 0.032))
    bone(side + "Foot", (ax, 0.060, ANKLE), ball, side + "Leg")
    bone(side + "ToeBase", ball, toe, side + "Foot")
for nm, x in BACK_X.items():
    prev = "Spine2"
    for k in range(4):
        b = "Hair_Back_%s%d" % (nm, k + 1)
        bone(b, (x, BACK_Y[k], BACK_Z[k]), (x, BACK_Y[k + 1], BACK_Z[k + 1]), prev)
        prev = b
prev = "Head"
for k in range(4):
    bone("Hair_Lock_R%d" % (k + 1), tuple(LOCKJ[k]), tuple(LOCKJ[k + 1]), prev)
    prev = "Hair_Lock_R%d" % (k + 1)
bpy.ops.object.mode_set(mode="OBJECT")
body.parent = rig
md = body.modifiers.new("Armature", "ARMATURE"); md.object = rig
missing = [vg.name for vg in body.vertex_groups if vg.name not in arm_data.bones]
print("[x3d] %d bones; vertex groups with no bone: %s" % (len(arm_data.bones), missing or "none"))
assert not missing


# ------------------------------------------------------------------ save and export (rest pose)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "x3d.blend"))
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "x3d.glb"), export_format="GLB", use_selection=False,
                          export_skins=True, export_animations=False, export_yup=True)
print("[x3d] wrote", os.path.join(OUT, "x3d.glb"), "(%.0f s)" % (time.time() - T0))


# ------------------------------------------------------------------ renders: the sheet's pose, at the sheet's scale
def pose_world(bname, axis, deg):
    pb = rig.pose.bones[bname]
    B = pb.bone.matrix_local.to_3x3()
    R = Matrix.Rotation(math.radians(deg), 3, axis)
    pb.rotation_mode = "QUATERNION"
    pb.rotation_quaternion = (B.inverted() @ R @ B).to_quaternion()


def sheet_pose():
    down = math.degrees(ARM_DOWN)
    a = 90.0 - down - 2.0                     # the sheet's upper arms hang straight down ...
    pose_world("LeftArm", "Y", a)
    pose_world("RightArm", "Y", -a)
    pose_world("LeftForeArm", "Y", -14.0)     # ... and the forearms angle out 14 degrees
    pose_world("RightForeArm", "Y", 14.0)
    bpy.context.view_layer.update()


if RENDERS:
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.view_settings.view_transform = "Standard"
    scene.cycles.use_denoising = True
    key = bpy.data.objects.new("key", bpy.data.lights.new("key", "AREA")); scene.collection.objects.link(key)
    key.data.energy = 260; key.data.size = 3.0; key.location = (-2.5, -3.0, 3.2)
    key.rotation_mode = "QUATERNION"; key.rotation_quaternion = (Vector((0, 0, 0.9)) - key.location).to_track_quat("-Z", "Y")
    fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", "AREA")); scene.collection.objects.link(fill)
    fill.data.energy = 90; fill.data.size = 3.0; fill.location = (3.0, 2.5, 2.0)
    fill.rotation_mode = "QUATERNION"; fill.rotation_quaternion = (Vector((0, 0, 0.9)) - fill.location).to_track_quat("-Z", "Y")
    world = bpy.data.worlds.new("w"); scene.world = world; world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.85, 0.85, 0.85, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.25
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); scene.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    scene.camera = cam
    sheet_pose()

    def shoot(path, loc, look, res, ortho, transparent, samples):
        scene.render.resolution_x, scene.render.resolution_y = res
        scene.render.film_transparent = transparent
        scene.cycles.samples = samples
        cam.data.ortho_scale = ortho
        cam.location = loc
        cam.rotation_mode = "QUATERNION"
        cam.rotation_quaternion = (Vector(look) - Vector(loc)).to_track_quat("-Z", "Y")
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

    # sheet-scale: the crops compare_sheet.py cuts (front cols 20..280, side 262..470, back 640..860; rows 25..745)
    zc = (718 - 385) * PX
    shoot(os.path.join(OUT, "cmp_front.png"), ((150 - 124.5) * PX, -6, zc), ((150 - 124.5) * PX, 0, zc), (260, 720), 720 * PX, True, 16)
    shoot(os.path.join(OUT, "cmp_side.png"), (6, (366 - 341) * PX, zc), (0, (366 - 341) * PX, zc), (208, 720), 720 * PX, True, 16)
    shoot(os.path.join(OUT, "cmp_back.png"), (-(750 - 740) * PX, 6, zc), (-(750 - 740) * PX, 0, zc), (220, 720), 720 * PX, True, 16)
    # the turnaround
    for nm, loc in (("front", (0, -6, 0.86)), ("side", (6, 0, 0.86)), ("back", (0, 6, 0.86)), ("threeq", (4.2, -4.2, 1.4))):
        shoot(os.path.join(OUT, "turn_%s.png" % nm), loc, (0, 0, 0.84), (520, 900), 1.95, False, 32)
    # the face, close
    shoot(os.path.join(OUT, "face_front.png"), (0, -6, 1.47), (0, 0, 1.47), (600, 600), 0.42, False, 32)
    shoot(os.path.join(OUT, "face_threeq.png"), (3.5, -4.9, 1.50), (0, 0, 1.47), (600, 600), 0.42, False, 32)
    print("[x3d] renders done (%.0f s)" % (time.time() - T0))
