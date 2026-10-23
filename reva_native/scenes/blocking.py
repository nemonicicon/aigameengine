"""blocking.py -- STAGE A SCENE IN A SET: where the people stand, and the lens on every line -- measured, and TESTED.

A scene PLAN -- its set, a cast on marks, extras, and per line a lens or staging keys -- becomes the engine's scene
script (scenes/<name>.txt, for compile_scene.py) and its scene set (sets/<setname>.set: the set, every mark the plan
named, the extras who do not speak). Split out of THE WORLD AT THE END OF TIME's chapter 2 generator (wateot_ch2.py,
29 scenes, 10-22/23), whose first takes taught it everything below:

 - a SINGLE is the speaker's face from the first lens that SEES it: over the listener's shoulder (two people facing),
   from in front swung 35 degrees toward the listener (side by side: a row of consoles), from above (lying down). Each
   candidate is TESTED -- the face toward the lens; no head, torso or furniture across the line (the set's own
   triangles, glass included); a listener's back may edge the frame, never fill it; the eye inside nobody, and not
   pressed into a face by a wall. When none passes it says so (WARN) and the plan gives the line a two-shot or a wide.
 - a TWO-SHOT square to the pair (looking down over the screens of a row of consoles), a WIDE from the farthest corner
   that sees every head, an OVER-the-shoulder at a point.
 - a standing body on a room mark is TURNED to face the others (a body on a mark faces the mark; only its head turns),
   and MOVED out of furniture to free floor; a seat's mark is left facing its table. Each is said in the log.
 - a mark change is staged where the lenses measure it (a walk the engine cannot route is the plan's `snap=1`).

THE PLAN: {"set": room, "title", "axis": +1/-1 (the side of the line), "cast": {slot: (person, outfit, mark, clip)} --
a mark is a room mark's name or (x, y, z, yaw) in metres -- "extras": [(person, clip, mark, outfit)], "lenses": {name:
"cam=..."}, "lines": {slot id: {"keys": "hmark=... pclip=...", "lens": "wide|two[:a:b]|single[:a[:b]]|over:s:x,y,z|
<named>|cam=...", "to": listener, "dur": s, "what": note, "pre": [extra lines]}}, "homes", "marks", "pair",
"keep_facing", "bounds" (when the set has no `# BOUNDS` line)}.
THE CALLER gives: HEIGHTS {person: metres} (else 1.70), ROOMS (a folder of <room>_{shell,fix,glow,glass}.glb in
metres -- skate_src/setkit.py's builders write them; without them only the walls are known), the slots, the cue map,
a header. Positions in metres; the engine's units are 1.75 a metre (U).

    python blocking.py --selftest     a synthetic room: a lens must go round a table, never through it
"""
import json, math, os, re, struct, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SETS = os.path.join(HERE, "..", "sets")
ROOMS = os.environ.get("CLUB_ROOMS", os.path.join("out", "rooms"))   # where a room's geometry glbs are
U = 1.75                                    # club units a metre
HEIGHTS = {}                                # person -> standing height, metres (the caller fills it)
DEFAULT_H = 1.70
SLOTKEY = {0: "h", 1: "p", 2: "p2", 3: "p3"}
WARN = []
FURNITURE = ("seat", "station", "table", "bunk", "bed", "chair", "bench")


def height(person):
    return HEIGHTS.get(person, DEFAULT_H)


# ---------------------------------------------------------------------------------------------- the rooms
def read_glb_tris(path):
    """(v0, e1, e2, lo, hi) of every triangle in one of build_rooms.py's glbs (one mesh: POSITION, indices)"""
    b = open(path, "rb").read()
    jl = struct.unpack_from("<I", b, 12)[0]
    js = json.loads(b[20:20 + jl]); binb = b[20 + jl + 8:]
    def acc(i, dt, n):
        a = js["accessors"][i]; v = js["bufferViews"][a["bufferView"]]
        return np.frombuffer(binb, dt, a["count"] * n, v.get("byteOffset", 0) + a.get("byteOffset", 0))
    pr = js["meshes"][0]["primitives"][0]
    Pp = acc(pr["attributes"]["POSITION"], "<f4", 3).reshape(-1, 3).astype(np.float64)
    I = acc(pr["indices"], "<u4", 1).reshape(-1, 3)
    A, B, C = Pp[I[:, 0]], Pp[I[:, 1]], Pp[I[:, 2]]
    return A, B - A, C - A, np.minimum(np.minimum(A, B), C), np.maximum(np.maximum(A, B), C)


FURN = {}


def furniture(room):
    """the room's triangles a lens cannot see through -- walls, fittings, screens, and GLASS (2.24's workshop: nine lines
    of Viktor behind a dark band of it); never the mirror's backing, which is drawn only in a reflection (None: not a
    room with geometry in ROOMS)"""
    if room not in FURN:
        parts = [read_glb_tris(os.path.join(ROOMS, "%s_%s.glb" % (room, k))) for k in ("shell", "fix", "glow", "glass")
                 if os.path.exists(os.path.join(ROOMS, "%s_%s.glb" % (room, k)))]
        FURN[room] = tuple(np.concatenate([p[i] for p in parts]) for i in range(5)) if parts else None
    return FURN[room]


def seg_blocked(F, a, b, t0=0.03, t1=0.96):
    """does the segment a -> b cross a triangle of F between t0 and t1 of its length (Moller-Trumbore)"""
    if F is None: return False
    a, b = np.asarray(a, float), np.asarray(b, float)
    lo, hi = np.minimum(a, b) - 0.02, np.maximum(a, b) + 0.02
    m = np.all(F[3] <= hi, 1) & np.all(F[4] >= lo, 1)
    if not m.any(): return False
    v0, e1, e2 = F[0][m], F[1][m], F[2][m]
    d = b - a
    p = np.cross(d, e2); det = np.einsum("ij,ij->i", e1, p)
    ok = np.abs(det) > 1e-12; inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    sv = a - v0
    u = np.einsum("ij,ij->i", sv, p) * inv
    q = np.cross(sv, e1)
    v = (q @ d) * inv
    t = np.einsum("ij,ij->i", q, e2) * inv
    return bool(np.any(ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > t0) & (t < t1)))


def read_set(name, seen=None):
    """marks (metres) and bounds from a set file and what it includes"""
    seen = seen or set()
    marks, bounds = {}, None
    p = os.path.join(SETS, name + ".set")
    if name in seen or not os.path.exists(p): return marks, bounds
    seen.add(name)
    for ln in open(p, encoding="utf-8"):
        t = ln.split()
        if not t: continue
        if t[0] == "include":
            m2, b2 = read_set(t[1], seen); marks.update(m2); bounds = bounds or b2
        elif t[0] == "mark" and len(t) >= 6:
            marks[t[1]] = (float(t[2]) / U, float(t[3]) / U, float(t[4]) / U, float(t[5]))
        elif t[0] == "#" and len(t) > 6 and t[1] == "BOUNDS":
            bounds = tuple(float(v) for v in t[2:7])
    return marks, bounds


def fmt(v): return "%.2f" % (v * U)


def cam(e, a, fov):
    return "cam=%s,%s,%s,%s,%s,%s,%d" % (fmt(e[0]), fmt(e[1]), fmt(e[2]), fmt(a[0]), fmt(a[1]), fmt(a[2]), round(fov))


class Stage:
    def __init__(self, plan):
        self.plan = plan
        self.marks, self.bounds = read_set(plan["set"])
        self.bounds = self.bounds or plan.get("bounds")    # a room built before BOUNDS lines (the drum, the sick bay)
        assert self.bounds, "no BOUNDS in %s (rebuild the room)" % plan["set"]
        self.marks.update({k: v for k, v in plan.get("marks", {}).items()})
        self.pos = {}           # slot -> (x, y, z, yaw) where it stands NOW
        self.who = {s: c[0] for s, c in plan["cast"].items()}
        for s, c in plan["cast"].items():
            self.pos[s] = self.mark(c[2])
        self.axis = plan.get("axis", 1)
        self.room = plan["set"]

    def go_home(self, home):
        """home=<set> on a line: the scene's room changes (sticky) -- re-read its marks and bounds"""
        if home == self.plan.get("setname", "_s%s" % self.plan["code"].replace(".", "_")): room = self.plan["set"]
        else:
            assert home in self.plan.get("homes", {}), "home %r is not one of the scene's homes" % home
            room = self.plan["homes"][home]["set"]
        marks, bounds = read_set(room)
        self.marks = dict(marks); self.marks.update(self.plan.get("marks", {}))
        self.bounds = bounds or self.plan.get("bounds")
        self.room = room

    def mark(self, m):
        if isinstance(m, tuple): return m
        assert m in self.marks, "mark %r not in %s (have %s)" % (m, self.plan["set"], ", ".join(sorted(self.marks)))
        return self.marks[m]

    def inside(self, e, pad=0.30):
        x0, x1, z0, z1, h = self.bounds
        return [min(max(e[0], x0 + pad), x1 - pad), min(max(e[1], 0.45), h - 0.25), min(max(e[2], z0 + pad), z1 - pad)]

    def lying(self, s):
        c = self.plan["cast"][s][3]
        return c.startswith("z_rm_") and not c.startswith("z_rm_sit")

    def head(self, s, f=0.92):
        x, y, z, yaw = self.pos[s]
        if self.lying(s):      # (10-22) along the body, toward the mark's -forward, just over the bed
            a = math.radians(yaw); r = 0.38 * height(self.who[s])
            return [x - math.sin(a) * r, y + 0.16, z - math.cos(a) * r]
        hh = height(self.who[s]) * f
        if self.plan["cast"][s][3].startswith(("z_sit", "z_rm_sit")): hh *= 0.70
        return [x, y + hh, z]

    def facing(self, s):
        yaw = math.radians(self.pos[s][3])
        return math.sin(yaw), math.cos(yaw)

    def side_by_side(self, a, b):
        """both face the same way and b is BESIDE a (a row of stations facing a screen, two on one bench)"""
        fa, fb = self.facing(a), self.facing(b)
        dx, dz = self.pos[b][0] - self.pos[a][0], self.pos[b][2] - self.pos[a][2]; d = math.hypot(dx, dz) or 1.0
        return fa[0] * fb[0] + fa[1] * fb[1] > 0.5 and abs(fa[0] * dx / d + fa[1] * dz / d) < 0.6

    def heads(self, skip=()):
        """every head in the room, (slot, point) -- the cast where they stand now, and the extras (slot None) on their
        marks"""
        out = [(s, self.head(s)) for s in self.pos if s not in skip]
        self._lying = {s for s in self.pos if self.lying(s)}
        for pers, clip, m, _ in self.plan.get("extras", []):
            x, y, z, _ = m if isinstance(m, tuple) else self.marks.get(m, (None,) * 4)
            if x is None: continue
            out.append((None, [x, y + height(pers) * 0.92 * (0.70 if clip.startswith(("z_sit", "z_rm_sit")) else 1.0), z]))
        return out

    def clear(self, e, S, skip, edge=()):
        """the eye is not inside anybody, and nobody's head is on the line from it to S -- but for `edge`, the listener an
        over-the-shoulder single keeps at the edge of the frame, who need only stay OFF the line"""
        if seg_blocked(furniture(self.room), e, S): return False      # a bunk, a console screen, a locker
        vx, vy, vz = S[0] - e[0], S[1] - e[1], S[2] - e[2]; n2 = vx * vx + vy * vy + vz * vz or 1.0
        for s, P in self.heads(skip):
            if math.dist(e, P) < 0.50: return False
            t = ((P[0] - e[0]) * vx + (P[1] - e[1]) * vy + (P[2] - e[2]) * vz) / n2
            r = 0.22 if s in edge else 0.42
            if 0.0 < t < 1.0 and math.dist(P, [e[0] + vx * t, e[1] + vy * t, e[2] + vz * t]) < r: return False
            # (10-22) and nobody's BODY: 2.6's single of Billy at a boy's eye height ran straight through Viktor's chest
            # -- his head was well above the line. The torso, shoulders to hips, a column about a body's half-width.
            if s in getattr(self, "_lying", ()): continue          # lying: no standing torso to cross
            y0, y1, rr = P[1] * 0.50, P[1] - 0.12, 0.16 * max(0.6, P[1] / 1.55)
            if s in edge: rr *= 0.45      # (10-22) the shoulder may edge the frame; a grown-up's chest may not fill a boy's single (2.23)
            for k in range(1, 24):
                u = k / 24.0
                Q = (e[0] + vx * u, e[1] + vy * u, e[2] + vz * u)
                if y0 <= Q[1] <= y1 and math.hypot(Q[0] - P[0], Q[2] - P[2]) < rr + 0.04: return False
        return True

    def fills(self, e, S, s, fov, k=0.5):
        """slot s's body, PROJECTED into the frame of a lens at e on S (vertical fov `fov`, 16:9), reaches into its
        central band |u| < k (u = -1..1 across the frame) while it is in the frame's height: the trunk (hips to
        shoulders, a torso and an arm wide) and the head. (10-22: 2.23 had a grown-up's back across a boy's single; a
        test on the horizontal alone refused singles whose near body was BELOW the frame.)"""
        if self.lying(s): return False
        x, y, z, _ = self.pos[s]
        fx, fy, fz = S[0] - e[0], S[1] - e[1], S[2] - e[2]; n = math.sqrt(fx * fx + fy * fy + fz * fz) or 1.0
        fx, fy, fz = fx / n, fy / n, fz / n
        rx, rz = fz, -fx; rn = math.hypot(rx, rz) or 1.0; rx, rz = rx / rn, rz / rn       # right, horizontal
        ux, uy, uz = -rz * fy, rz * fx - rx * fz, rx * fy                                 # up' = right x forward
        tv = math.tan(math.radians(fov) / 2.0); th = 16.0 / 9.0 * tv
        sc = min(1.0, height(self.who[s]) / 1.70)
        hd = self.head(s)
        sit = self.plan["cast"][s][3].startswith(("z_sit", "z_rm_sit"))
        y0 = y + (0.50 if sit else 0.53 * height(self.who[s])); y1 = hd[1] - 0.20 * sc
        pts = [(x + rx * d, yy, z + rz * d) for yy in (y0, y1) for d in (-0.30 * sc, 0.30 * sc)]
        pts += [(hd[0] + rx * d, hd[1] + dy, hd[2] + rz * d) for d, dy in ((-0.12 * sc, 0), (0.12 * sc, 0), (0, 0.12 * sc), (0, -0.12 * sc))]
        us, vs = [], []
        for px, py, pz in pts:
            wx, wy, wz = px - e[0], py - e[1], pz - e[2]
            al = wx * fx + wy * fy + wz * fz
            if al <= 0.05: continue
            if al > n + 0.30: return False                  # well behind the speaker: background (BESIDE him counts: 2.28)
            us.append((wx * rx + wz * rz) / al / th); vs.append((wx * ux + wy * uy + wz * uz) / al / tv)
        if not us: return False
        return max(us) > -k and min(us) < k and max(vs) > -0.9 and min(vs) < 0.9

    def sees_face(self, s, e, slack=75.0):
        """a lens at e sees slot s's face: it is within `slack` degrees of the way the body faces"""
        fx, fz = self.facing(s)
        dx, dz = e[0] - self.pos[s][0], e[2] - self.pos[s][2]; d = math.hypot(dx, dz) or 1.0
        return (fx * dx + fz * dz) / d > math.cos(math.radians(slack))

    def single(self, spk, lst, fov=32):
        """the speaker's face, from the first of these that SEES it -- the face toward the lens, nobody's head on the
        line, the eye inside nobody: over the listener's shoulder (the axis's side, then the other), then from in front
        of the speaker swung 35 degrees toward the listener (then away), 1.9 m out and then 1.5.

        ⭐ (10-22) 2.4/2.6/2.8's first cuts: over-the-shoulder is right for two people FACING each other, and wrong for
        two side by side (a row of stations: Pal from behind past the navigator's back, the navigator past Pal's typing
        hand), for a speaker whose back is to the listener (Viktor on his cabin door's mark), and over a listener whose
        head is on the line (a boy at the table hid Viktor whole). The swing is 35 degrees so a seated face clears its
        own console screen, which stands 0.95 m in front of a station's chair from 1.14 to 1.60 m up. The eye stays at
        about the SPEAKER's eye height (at the listener's own head height a single of Viktor was shot from a
        five-year-old's head, from behind it)."""
        S, L = self.head(spk), self.head(lst, 0.80)
        dx, dz = S[0] - L[0], S[2] - L[2]; d = math.hypot(dx, dz) or 1.0
        aim = [S[0], S[1] - 0.02, S[2]]
        ux, uz = dx / d, dz / d
        back = 0.80 if d < 3.0 else -0.25 * d
        if L[1] > S[1] + 0.30:  ey = L[1] - 0.22          # a grown-up hears a child: over the grown-up's SHOULDER, down at him
        else:                   ey = max(L[1] + 0.12, S[1] - 0.04)      # (10-22: from the child's height it was behind her hips, 2.23)
        side = 0.55 if L[1] > S[1] - 0.30 else 0.30       # a short listener barely edges the frame
        if L[1] > S[1] + 0.30: side = 0.42
        ots = []
        for ax in (self.axis, -self.axis):
            vx, vz = -uz * ax, ux * ax
            ots.append((self.inside([L[0] - ux * back + vx * side, ey, L[2] - uz * back + vz * side]),
                        fov if d < 3.0 else 24, (lst,)))
        front = []
        fx, fz = self.facing(spk)
        for lift in (0.05, 0.35, 0.60):                    # level, then over the heads of a crowded table (2.12)
            for D in (1.9, 1.5):
                sw = []
                for sg in (1, -1):
                    a = math.radians(35) * sg
                    vx, vz = fx * math.cos(a) - fz * math.sin(a), fx * math.sin(a) + fz * math.cos(a)
                    sw.append((-(vx * -dx + vz * -dz), [S[0] + vx * D, S[1] + lift, S[2] + vz * D]))
                for _, e in sorted(sw, key=lambda q: q[0]):    # toward the listener first
                    front.append((self.inside(e), 30, ()))
        if self.lying(spk):
            # (10-22) A LYING FACE LOOKS UP: from above it, toward the feet; else from either side at bed height
            # (under a bunk over it) -- whichever the furniture and the heads leave clear
            a = math.radians(self.pos[spk][3]); fx, fz = math.sin(a), math.cos(a)
            cands = [[S[0] + fx * 0.35, S[1] + 0.85, S[2] + fz * 0.35]]
            for sg in (1, -1):
                for D in (1.3, 1.0):
                    cands.append([S[0] + sg * fz * D + fx * 0.25, S[1] + 0.32, S[2] - sg * fx * D + fz * 0.25])
            # above first; then the sides, the OPEN side first (the least pulled in by the walls -- not the
            # wall's side of a bunk against it)
            cands = cands[:1] + sorted(cands[1:], key=lambda e: math.dist(e, self.inside(e)))
            for e in cands:
                e = self.inside(e)
                if math.dist(e, S) < 0.6: continue
                if self.clear(e, S, (spk,)): return cam(e, [S[0], S[1] - 0.04, S[2]], 34)
            WARN.append("%s: no lens sees lying slot %d's face" % (self.plan["code"], spk))
        for e, f, edge in (front + ots) if self.side_by_side(spk, lst) else (ots + front):
            if math.dist(e, S) < 0.9: continue             # pressed against a wall, into the face
            # (10-22) a back across the frame is not a single -- the listener's, or ANYBODY's between the lens and the
            # speaker (2.23: Marie-Claude across Billy's single from in front of him)
            # (the listener's shoulder stays in the frame's outer quarters; anybody else only may not reach its centre)
            if any(self.fills(e, S, o, f, 0.5 if (edge and o == lst) else 0.2) for o in self.pos if o != spk): continue
            if self.sees_face(spk, e) and self.clear(e, S, (spk,), edge): return cam(e, aim, f)
        WARN.append("%s: no lens sees slot %d's face (to %d) -- over the shoulder, unchecked" % (self.plan["code"], spk, lst))
        e, f, _ = ots[0]
        return cam(e, aim, f)

    def two(self, a, b, fov=44):
        A, B = self.head(a), self.head(b)
        mx, mz = (A[0] + B[0]) / 2, (A[2] + B[2]) / 2
        dx, dz = B[0] - A[0], B[2] - A[2]; d = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / d * self.axis, dx / d * self.axis
        if self.side_by_side(a, b):
            # (10-22) a row facing one way is shot from the side its FACES are on (2.4's went behind the row and the
            # back wall clamped it into the chair backs)
            fx, fz = [p + q for p, q in zip(self.facing(a), self.facing(b))]
            if nx * fx + nz * fz < 0: nx, nz = -nx, -nz
            if "z_sit_type" in (self.plan["cast"][a][3], self.plan["cast"][b][3]):
                # at consoles, whose screens stand 0.95 m in front of the chairs from 1.14 to 1.60 m up: look OVER them
                y = min(self.bounds[4] - 0.25, 2.4); hy = (A[1] + B[1]) / 2
                D = 0.95 * (y - hy) / 0.55
                e = self.inside([mx + nx * D, y, mz + nz * D])
                return cam(e, [mx, hy - 0.10, mz], min(70, 2 * math.degrees(math.atan((d + 0.8) / 2 / D))))
        dist = max(1.8, d * 1.15 + 1.0)
        aim = [mx, (A[1] + B[1]) / 2 - 0.25, mz]
        F = furniture(self.room)
        first = None
        for sg, y in ((1, 1.45), (1, 1.95), (-1, 1.45), (-1, 1.95)):          # (10-22) the side, then higher, then across
            e = self.inside([mx + sg * nx * dist, y, mz + sg * nz * dist])
            first = first or e
            if math.hypot(e[0] - mx, e[2] - mz) < 0.9: continue                # a wall pressed it into them
            if not seg_blocked(F, e, A) and not seg_blocked(F, e, B): return cam(e, aim, fov)
        WARN.append("%s: no two-shot of %d and %d clears the furniture -- the first, unchecked" % (self.plan["code"], a, b))
        return cam(first, aim, fov)

    def wide(self, fov=64, slots=None):
        sl = slots or list(self.pos)
        cx = sum(self.pos[s][0] for s in sl) / len(sl); cz = sum(self.pos[s][2] for s in sl) / len(sl)
        x0, x1, z0, z1, h = self.bounds
        F = furniture(self.room)
        heads = [self.head(s) for s in sl]
        corners = sorted(((math.hypot(ex - cx, ez - cz), ex, ez) for ex in (x0 + 0.35, x1 - 0.35)
                          for ez in (z0 + 0.35, z1 - 0.35)), reverse=True)
        for dd, ex, ez in corners:                       # (10-22) the farthest corner whose eye sees every head
            e = [ex, min(h - 0.35, 2.15), ez]
            if not any(seg_blocked(F, e, P) for P in heads): return cam(e, [cx, 1.05, cz], fov)
        e = [corners[0][1], min(h - 0.35, 2.15), corners[0][2]]
        return cam(e, [cx, 1.05, cz], fov)

    def over(self, s, target, fov=40):
        """over slot s's shoulder at a world point (a screen, the door)"""
        S = self.head(s, 0.85)
        dx, dz = target[0] - S[0], target[2] - S[2]; d = math.hypot(dx, dz) or 1.0
        ux, uz = dx / d, dz / d
        e = self.inside([S[0] - ux * 0.6 - uz * 0.30 * self.axis, S[1] + 0.12, S[2] - uz * 0.6 + ux * 0.30 * self.axis])
        return cam(e, target, fov)


# ---------------------------------------------------------------------------------------------- the script
def action_dur(text):
    return round(min(11.0, max(2.5, 2.0 + 0.085 * len(text.split()))), 1)


def name_marks(code, plan):
    """a position given as (x, y, z, yaw) metres becomes a NAMED mark in the scene's set (the engine finds marks by
    name): the cast's own, and every Xmark=(x,y,z,yaw) in a line's keys"""
    tag = code.replace(".", "_")
    new = {}
    cast = {}
    for s, c in plan["cast"].items():
        if isinstance(c[2], tuple):
            nm = "m%s_c%d" % (tag, s); new[nm] = c[2]; c = c[:2] + (nm,) + c[3:]
        cast[s] = c
    plan["cast"] = cast
    k = 0
    for slot, o in list(plan.get("lines", {}).items()):
        if isinstance(o, str): o = {"keys": o}
        ks = []
        for kv in o.get("keys", "").split():
            m = re.match(r"^(\w*mark)=\(([-0-9.,\s]+)\)$", kv)
            if m:
                v = tuple(float(x) for x in m.group(2).split(","))
                nm = "m%s_%d" % (tag, k); k += 1; new[nm] = v; kv = "%s=%s" % (m.group(1), nm)
            ks.append(kv)
        o["keys"] = " ".join(ks)
        plan["lines"][slot] = o
    plan.setdefault("marks", {}).update(new)
    return new


def face_the_room(code, plan, st):
    """⭐ (10-22) PEOPLE TALKING FACE EACH OTHER. A standing member of the cast on a ROOM mark faces the way the room's
    mark faces (a door, a screen), and a body on a mark turns only its head (35 degrees, and only the hero and the
    partner) -- so 2.6's Viktor stood on his cabin door's mark facing the door while the boys fought behind him, and
    every single of him was the back of his head. Whoever stands on a room mark facing more than 50 degrees away from
    the others is turned to face them, on a mark of the scene's own. A position the plan gave as (x, y, z, yaw) is the
    plan's choice and is left alone; so is a slot in plan["keep_facing"]."""
    for s, (pers, outfit, mark, clip) in list(plan["cast"].items()):
        nm = turn_to_others(code, plan, st, s, mark, clip)
        if nm: plan["cast"][s] = (pers, outfit, nm, clip)


def in_furniture(st, x, y, z):
    """a body standing at (x, z) has furniture through it: a vertical line 0.3-1.0 m up, at its centre or 0.12 m out,
    crosses the room's triangles"""
    F = furniture(st.room)
    return any(seg_blocked(F, (x + dx, y + 0.30, z + dz), (x + dx, y + 1.00, z + dz), 0.0, 1.0)
               for dx, dz in ((0, 0), (0.12, 0), (-0.12, 0), (0, 0.12), (0, -0.12)))


def free_floor(code, plan, st, s, why):
    """⭐ (10-22) a STANDING body's mark inside a piece of furniture is moved to the nearest free floor (rings 0.1 m apart,
    out to 1 m, clear of the others by 0.45 m), on a mark of the scene's own, and SAID: the engine's contact pass would
    shove it out sideways every frame, off its mark and across the lenses aimed past it (2.28: Freddy stood inside the
    cabin's table and was slid 0.34 m across Billy's singles). Returns the new mark's name, or None."""
    if plan["cast"][s][3].startswith(("z_sit", "z_rm_")): return None
    x, y, z, yaw = st.pos[s]
    if not in_furniture(st, x, y, z): return None
    x0, x1, z0, z1, _ = st.bounds
    for k in range(1, 11):
        r = 0.1 * k
        for a in range(0, 360, 30):
            px, pz = x + r * math.sin(math.radians(a)), z + r * math.cos(math.radians(a))
            if not (x0 + 0.25 < px < x1 - 0.25 and z0 + 0.25 < pz < z1 - 0.25): continue
            if any(math.hypot(px - st.pos[o][0], pz - st.pos[o][2]) < 0.45 for o in st.pos if o != s): continue
            if in_furniture(st, px, y, pz): continue
            nm = "m%s_free%d" % (code.replace(".", "_"), sum(1 for m in plan["marks"] if "_free" in m))
            plan["marks"][nm] = (round(px, 3), y, round(pz, 3), yaw)
            st.marks[nm] = plan["marks"][nm]; st.pos[s] = plan["marks"][nm]
            print("  %s: %s stood INSIDE furniture at (%.2f, %.2f) (%s) -- moved %.1f m to free floor (%s)"
                  % (code, st.who[s], x, z, why, r, nm))
            return nm
    WARN.append("%s: %s stands INSIDE furniture at (%.2f, %.2f) (%s) and no free floor within 1 m" % (code, st.who[s], x, z, why))
    return None


def turn_to_others(code, plan, st, s, mark, clip):
    """slot s, standing on room mark `mark`, faces the others within 5 m -- a mark of the scene's own if the room's
    turns it more than 50 degrees away from them (None: it already faces them, sits, lies, or the plan chose)"""
    tag = "m%s_" % code.replace(".", "_")
    if clip.startswith(("z_sit", "z_rm_", "z_carry")) or mark.startswith(tag) or s in plan.get("keep_facing", ()):  # (z_rm_: lying, sitting)
        return None
    if mark.startswith(FURNITURE): return None          # a seat's mark faces its table: she sits on the next line (2.9)
    x, y, z, yaw = st.pos[s]
    oth = [st.pos[o] for o in st.pos if o != s and 0.05 < math.hypot(st.pos[o][0] - x, st.pos[o][2] - z) < 5.0]
    if not oth: return None
    cx, cz = sum(p[0] for p in oth) / len(oth) - x, sum(p[2] for p in oth) / len(oth) - z
    want = round(math.degrees(math.atan2(cx, cz)) % 360.0)
    off = abs((want - yaw + 180.0) % 360.0 - 180.0)
    if off <= 50.0: return None
    nm = next((k for k, v in plan["marks"].items() if k.startswith(tag + "f") and v == (x, y, z, want)), None)
    if nm is None:
        nm = "%sf%d_%d" % (tag, s, sum(1 for k in plan["marks"] if k.startswith(tag + "f")))
        plan["marks"][nm] = (x, y, z, want)
    st.marks[nm] = plan["marks"][nm]; st.pos[s] = plan["marks"][nm]
    print("  %s: %s turned %.0f deg on %s to face the others (%s)" % (code, st.who[s], off, mark, nm))
    return nm


def write_scene(code, plan, slots, cues, name, header, say=lambda t: t, setname=None, writer="blocking.py",
                narrator="narrator", out_dir=None):
    """a PLAN -> <out_dir>/<name>.txt (the scene script) and sets/<setname>.set (+ one per other home): see the
    module's docstring. slots: [(slot, kind, cue, text)], kind speech|narr|action; cues: {CUE: person}; header:
    the script's comment lines; say: the text a line speaks (a cleaner); the narrator is slot 4."""
    plan["code"] = code
    setname = setname or "_s%s" % code.replace(".", "_"); plan["setname"] = setname
    newmarks = name_marks(code, plan)
    st = Stage(plan)
    face_the_room(code, plan, st)
    for s in list(st.pos):
        nm = free_floor(code, plan, st, s, "the scene's start")
        if nm: plan["cast"][s] = plan["cast"][s][:2] + (nm,) + plan["cast"][s][3:]
    cast = plan["cast"]
    L = list(header) + ["cast %s%s" % (" ".join("%d=%s" % (s, cast[s][0]) for s in sorted(cast)),
                                        (" 4=%s" % narrator) if narrator else ""), ""]
    first = True
    last_spk = None
    for slot, kind, cue, text in slots:
        o = plan.get("lines", {}).get(slot, {})
        if isinstance(o, str): o = {"keys": o}
        if kind == "speech":
            c0 = cue.replace(" (CONT'D)", "").strip()
            who = cues[c0]
            spk = o.get("spk", next(s for s in cast if cast[s][0] == who))
        elif kind == "narr":
            spk = 4
        else:
            spk = o.get("spk", 4)
        keys = []
        if first:
            keys.append("snap=1")
            for s in sorted(cast):
                k = SLOTKEY[s]
                pers, outfit, mark, clip = cast[s]
                keys += ["%smark=%s" % (k, mark), "%sclip=%s" % (k, clip)]
                if outfit: keys.append("%soutfit=%s" % (k, outfit))
                if s in (0, 1, 2): keys.append("%slook=%d" % (k, 1))
            first = False
        keys += o.get("keys", "").split()
        for kv in o.get("keys", "").split():
            if kv.startswith("home="): st.go_home(kv[5:])
        # a mark changed on this line moves the stand-in the lenses measure from
        moved = []
        for kv in o.get("keys", "").split():
            if "mark=" in kv:
                kk, mv = kv.split("=", 1)
                s = {"hmark": 0, "pmark": 1, "p2mark": 2, "p3mark": 3}.get(kk)
                if s is not None and s in cast and mv != "off": st.pos[s] = st.mark(mv); moved.append((s, kk, mv))
            if "clip=" in kv:
                kk, cv = kv.split("=", 1)
                s = {"hclip": 0, "pclip": 1, "p2clip": 2, "p3clip": 3}.get(kk)
                if s is not None and s in cast: cast[s] = cast[s][:3] + (cv,)
        # ... and whoever it moved onto a room mark turns to the others there (face_the_room's reason, on a line: 2.24's
        # Viktor hurried back to the doors and stopped with his back to Wanda, who had called him from them)
        for s, kk, mv in moved:
            nm0 = free_floor(code, plan, st, s, slot)
            if nm0: keys = ["%s=%s" % (kk, nm0) if k == "%s=%s" % (kk, mv) else k for k in keys]; mv = nm0
            nm = turn_to_others(code, plan, st, s, mv, cast[s][3])
            if nm: keys = ["%s=%s" % (kk, nm) if k == "%s=%s" % (kk, mv) else k for k in keys]
        lens = o.get("lens")
        if lens is None and kind == "speech" and spk in plan.get("pair", ()):
            lens = "two:%d:%d" % tuple(plan["pair"])     # (10-22) the boys are a unit: their lines are the pair's shot
        if lens is None:
            if kind == "speech":
                lst = o.get("to", last_spk if (last_spk is not None and last_spk != spk and last_spk in cast) else
                            next((s for s in cast if s != spk), spk))
                lens = "single:%d:%d" % (spk, lst)
            else:
                lens = "wide"
        if lens.startswith("cam=") or lens.startswith("camorbit") or lens.startswith("set="):
            keys.append(lens)
        elif lens in plan.get("lenses", {}):
            keys.append(plan["lenses"][lens])
        elif lens == "wide":
            keys.append(st.wide())
        elif lens.startswith("two"):
            bits = lens.split(":")
            a, b = (int(bits[1]), int(bits[2])) if len(bits) == 3 else (spk if spk in cast else 0, next(s for s in cast if s != (spk if spk in cast else 0)))
            keys.append(st.two(a, b))
        elif lens.startswith("single"):
            bits = lens.split(":")
            a = int(bits[1]) if len(bits) > 1 else spk
            b = int(bits[2]) if len(bits) > 2 else (last_spk if last_spk not in (None, a) else next(s for s in cast if s != a))
            keys.append(st.single(a, b))
        elif lens.startswith("over:"):
            _, s, tgt = lens.split(":", 2)
            keys.append(st.over(int(s), [float(v) / U for v in tgt.split(",")]))
        else:
            raise SystemExit("%s: unknown lens %r" % (slot, lens))
        if kind == "action" and "dur=" not in " ".join(keys):
            keys.append("dur=%s" % o.get("dur", action_dur(text)))
        if kind == "speech" and spk in cast: last_spk = spk
        tag = {"speech": cue, "narr": "NARRATOR", "action": "ACTION"}[kind]
        L.append("# ---- %s (%s)%s" % (slot, tag, (" " + o["what"]) if o.get("what") else ""))
        for extra in o.get("pre", []):              # extra lines before this slot's own (a beat, a cutaway)
            L.append(extra)
        L.append("%d %s|%s" % (spk, " ".join(keys), "" if kind == "action" else say(text)))
    open(os.path.join(out_dir or HERE, name + ".txt"), "w", encoding="utf-8", newline="\n").write("\n".join(L) + "\n")
    # the scene's sets: its room (and any other home it moves to), its extras, and every mark it named
    homes = {setname: dict(set=plan["set"], extras=plan.get("extras", []),
                                                    setlines=plan.get("setlines", []))}
    homes.update(plan.get("homes", {}))
    allmarks = dict(plan.get("marks", {}))
    for hname, h in homes.items():
        S = ["# %s.set -- WRITTEN BY %s: scene %s's room and the people in it who do not speak" % (hname, writer, code),
             "include %s" % h["set"]]
        for nm, (x, y, z, yaw) in sorted(allmarks.items()):
            S.append("mark %-14s %s %s %s %.0f" % (nm, fmt(x), fmt(y), fmt(z), yaw))
        rm_marks, _ = read_set(h["set"])
        rm_marks.update(allmarks)
        for (pers, clip, m, outfit) in h.get("extras", []):
            x, y, z, yaw = m if isinstance(m, tuple) else rm_marks[m]
            S.append("actor v1_%-12s %-12s %s %s %s %.0f 1%s" % (pers, clip, fmt(x), fmt(y), fmt(z), yaw,
                                                                (" outfit=%s" % outfit) if outfit else ""))   # (10-23)
        S += h.get("setlines", [])
        open(os.path.join(SETS, hname + ".set"), "w", newline="\n").write("\n".join(S) + "\n")
    return name

# ---------------------------------------------------------------------------------------------- the self-test
def _box(lo, hi):
    """the triangles of an axis-aligned box, in the shape furniture() returns"""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    P = np.array([[x, y, z] for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)], float)
    T = [(0, 1, 3), (0, 3, 2), (4, 6, 7), (4, 7, 5), (0, 4, 5), (0, 5, 1), (2, 3, 7), (2, 7, 6), (0, 2, 6), (0, 6, 4),
         (1, 5, 7), (1, 7, 3)]
    A, B, C = (np.array([P[t[k]] for t in T]) for k in range(3))
    return A, B - A, C - A, np.minimum(np.minimum(A, B), C), np.maximum(np.maximum(A, B), C)


def selftest():
    """two people facing across a table with a console SCREEN on it (1.0-1.9 m, between their faces) in a 6 x 6 m room,
    a third standing INSIDE the table and a fourth facing away: every single must go round the screen, the one inside
    must be moved out to free floor, the one facing away turned to the others"""
    HEIGHTS.update({"a": 1.75, "b": 1.65, "c": 1.70, "d": 1.70})
    parts = [_box((-0.6, 0.0, -0.4), (0.6, 0.75, 0.4)), _box((-0.3, 1.0, -0.05), (0.3, 1.9, 0.05))]   # table, screen
    FURN["_selftest"] = tuple(np.concatenate([p[i] for p in parts]) for i in range(5))
    plan = {"set": "_selftest", "code": "0.0", "axis": 1, "bounds": (-3.0, 3.0, -3.0, 3.0, 2.8),
            "cast": {0: ("a", None, (0.0, 0.0, -1.2, 0.0), "z_idle"), 1: ("b", None, (0.0, 0.0, 1.2, 180.0), "z_idle"),
                     2: ("c", None, (0.1, 0.0, 0.0, 90.0), "z_idle"), 3: ("d", None, (1.5, 0.0, -1.0, 270.0), "z_idle")},
            "marks": {"far": (1.5, 0.0, -1.0, 90.0)}}
    st = Stage(plan); st.room = "_selftest"
    ok = True
    for spk, lst in ((0, 1), (1, 0)):
        c = [float(v) / U for v in single_cam(st.single(spk, lst))]
        e, S = c[:3], st.head(spk)
        through = seg_blocked(FURN["_selftest"], e, S)
        print("single of %s (to %s): eye (%.2f, %.2f, %.2f) -- %s" % (st.who[spk], st.who[lst], e[0], e[1], e[2],
              "THROUGH the screen: FAIL" if through else "goes round the screen"))
        ok &= not through
    nm = free_floor("0.0", plan, st, 2, "selftest")
    print("c stood inside the table -> %s" % (("moved to %s %s" % (nm, plan["marks"][nm][:3])) if nm else "NOT moved: FAIL"))
    ok &= bool(nm) and not in_furniture(st, *plan["marks"][nm][:3])
    plan["cast"][3] = ("d", None, "far", "z_idle"); st.pos[3] = plan["marks"]["far"]
    nm2 = turn_to_others("0.0", plan, st, 3, "far", "z_idle")
    print("d faced away -> %s" % (("turned to yaw %.0f (%s)" % (plan["marks"][nm2][3], nm2)) if nm2 else "NOT turned: FAIL"))
    ok &= bool(nm2)
    print("SELFTEST %s" % ("PASS" if ok else "FAIL"))
    return ok


def single_cam(c):
    return c.split("=", 1)[1].split(",")[:6]


if __name__ == "__main__":
    if "--selftest" in sys.argv: sys.exit(0 if selftest() else 1)
    print(__doc__)
