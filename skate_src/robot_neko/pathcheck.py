#!/usr/bin/env python3
"""pathcheck.py -- does a skating path clip anything in reva_native/sets?

Builds the CENTRIPETAL CATMULL-ROM (alpha=0.5) curve the engine's `hmark`/`pmark` walk
(and a rolling `@deck`) actually follows through an ordered mark list, samples it every
0.05u, and reports:
    - the minimum horizontal clearance to any obstacle/no-floor cell
    - the tightest turn radius on the path
    - the path length
Exits 1 if clearance < 1.0u (a rider plus a 1.4u board) or radius < 2.0u.

Obstacle data comes from grid.npz (built by build_grid.py from reva_native/props/southbank.rpack
via the room transform in reva_native/sets/southbank.set) -- world x/z, 0.10u cells, with a
precomputed exact Euclidean distance-to-nearest-blocked-cell field for the clearance query.

usage:
  pathcheck.py <set> --marks m1 m2 m3 ... [--start x,z] [--png out.png]
  pathcheck.py <set> --marks m1 m2 ... --pair --marks2 n1 n2 ... [--start2 x,z] [--speed 3.0] [--png out.png]

  <set> is a .set NAME (not a path) resolved in reva_native/sets/, `include` lines followed.
"""
import argparse, os, re, sys, math
import numpy as np

REPO = r"C:\MyVeryOwnVideoGames\reva"
SETS_DIR = os.path.join(REPO, "reva_native", "sets")
DEFAULT_GRID = os.path.join(os.environ.get("CLUB_OUT", "out"), "grid.npz")

CLEAR_MIN = 1.0     # u -- rider + 1.4u board
RADIUS_MIN = 2.0    # u
SAMPLE_DS = 0.05    # u, arc-length sample spacing
CURV_HALF = 0.30    # u, half-chord used to estimate turn radius (noise-resistant vs raw ds)

# ---------------------------------------------------------------- set / mark loading ----
def _resolve_set(name):
    if os.path.isabs(name) and os.path.exists(name):
        return name
    p = name if name.lower().endswith(".set") else name + ".set"
    cand = p if os.path.isabs(p) else os.path.join(SETS_DIR, p)
    if not os.path.exists(cand):
        raise SystemExit("pathcheck: no such set '%s' (looked at %s)" % (name, cand))
    return cand

_MARK_RE = re.compile(r'^\s*mark\s+(\S+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)')
_INC_RE = re.compile(r'^\s*include\s+(\S+)')

def load_marks(setname, _seen=None):
    """Parse `mark <name> x y z yaw` and follow `include <name>` lines, resolved in SETS_DIR.
    Later lines override earlier ones with the same name, matching file read order."""
    if _seen is None:
        _seen = set()
    path = _resolve_set(setname)
    if path in _seen:
        return {}
    _seen.add(path)
    marks = {}
    for line in open(path, encoding="utf-8"):
        line = line.split('#', 1)[0]
        mi = _INC_RE.match(line)
        if mi:
            marks.update(load_marks(mi.group(1), _seen))
            continue
        mm = _MARK_RE.match(line)
        if mm:
            name, x, _y, z, yaw = mm.groups()
            marks[name] = (float(x), float(z), float(yaw))
    return marks

# ---------------------------------------------------------------- centripetal catmull-rom ----
def _seg_eval(p0, p1, p2, p3, alpha, n):
    """n points from p1 to p2 (inclusive of p1, exclusive of p2, caller appends the final p2)."""
    def tj(ti, a, b):
        d = math.hypot(b[0] - a[0], b[1] - a[1])
        return ti + (d ** alpha if d > 1e-9 else 1e-6)
    t0 = 0.0
    t1 = tj(t0, p0, p1)
    t2 = tj(t1, p1, p2)
    t3 = tj(t2, p2, p3)
    out = []
    for k in range(n):
        t = t1 + (t2 - t1) * (k / float(n))
        def lerp(a, b, ta, tb, tt):
            if tb - ta < 1e-9:
                return a
            f = (tt - ta) / (tb - ta)
            return (a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1]))
        A1 = lerp(p0, p1, t0, t1, t)
        A2 = lerp(p1, p2, t1, t2, t)
        A3 = lerp(p2, p3, t2, t3, t)
        B1 = lerp(A1, A2, t0, t2, t)
        B2 = lerp(A2, A3, t1, t3, t)
        C = lerp(B1, B2, t1, t2, t)
        out.append(C)
    return out

def catmull_rom_path(points, alpha=0.5, sub_per_unit=30):
    """points: [(x,z), ...] control points, ALREADY deduplicated. Endpoints duplicated for
    tangents (P(-1)=P0, P(n)=P(n-1)), per spec. Returns a FINE polyline (dense, not evenly
    spaced) covering the whole path start-to-end."""
    if len(points) < 2:
        raise SystemExit("pathcheck: need at least 2 distinct points (start+marks) to path")
    pts = [points[0]] + list(points) + [points[-1]]
    fine = [points[0]]
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        chord = math.hypot(p2[0] - p1[0], p2[1] - p1[1])
        n = max(8, int(math.ceil(chord * sub_per_unit)))
        seg = _seg_eval(p0, p1, p2, p3, alpha, n)
        fine.extend(seg)
    fine.append(points[-1])
    return fine

def resample_by_arclength(fine, ds=SAMPLE_DS):
    """Walk the fine polyline accumulating arc length, emitting a point every `ds` of it.
    `next_mark` must persist ACROSS segments (a bug here once reset it every iteration and
    silently emitted only the first point of each segment -- a 9u path came back as 2 samples,
    caught by re-running the tool on a known pair of marks and seeing 'samples 2')."""
    out = [fine[0]]
    acc = 0.0
    prev = fine[0]
    next_mark = ds
    for p in fine[1:]:
        seg = math.hypot(p[0] - prev[0], p[1] - prev[1])
        if seg < 1e-12:
            prev = p
            continue
        seg_end = acc + seg
        while next_mark <= seg_end:
            f = (next_mark - acc) / seg
            out.append((prev[0] + f * (p[0] - prev[0]), prev[1] + f * (p[1] - prev[1])))
            next_mark += ds
        acc = seg_end
        prev = p
    if out[-1] != fine[-1]:
        out.append(fine[-1])
    return out, acc

def dedup(points, tol=1e-4):
    out = [points[0]]
    for p in points[1:]:
        if math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > tol:
            out.append(p)
    return out

# ---------------------------------------------------------------- turn radius ----
def circumradius(a, b, c):
    ab = math.hypot(b[0] - a[0], b[1] - a[1])
    bc = math.hypot(c[0] - b[0], c[1] - b[1])
    ca = math.hypot(a[0] - c[0], a[1] - c[1])
    area2 = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]))  # = 2*area
    if area2 < 1e-9:
        return float('inf')
    return (ab * bc * ca) / (2.0 * area2)

def min_turn_radius(path, ds=SAMPLE_DS, half=CURV_HALF):
    k = max(1, int(round(half / ds)))
    best = float('inf'); best_i = -1
    for i in range(k, len(path) - k):
        r = circumradius(path[i - k], path[i], path[i + k])
        if r < best:
            best, best_i = r, i
    return best, best_i

# ---------------------------------------------------------------- grid / clearance ----
class Grid:
    def __init__(self, path):
        g = np.load(path)
        self.ox = float(g['origin_x']); self.oz = float(g['origin_z'])
        self.cell = float(g['cell']); self.nx = int(g['nx']); self.nz = int(g['nz'])
        self.dist = g['dist']         # (nz, nx), world units, no NaN
        self.blocked = g['blocked']
        self.ref_floor = float(g['ref_floor'])

    def bilinear(self, arr, x, z):
        fx = (x - self.ox) / self.cell - 0.5
        fz = (z - self.oz) / self.cell - 0.5
        i0 = int(math.floor(fx)); j0 = int(math.floor(fz))
        tx = fx - i0; tz = fz - j0
        oob = not (0 <= i0 and i0 + 1 < self.nx and 0 <= j0 and j0 + 1 < self.nz)
        i0c = min(max(i0, 0), self.nx - 2); j0c = min(max(j0, 0), self.nz - 2)
        v00 = arr[j0c, i0c]; v10 = arr[j0c, i0c + 1]
        v01 = arr[j0c + 1, i0c]; v11 = arr[j0c + 1, i0c + 1]
        v = (v00 * (1 - tx) * (1 - tz) + v10 * tx * (1 - tz) +
             v01 * (1 - tx) * tz + v11 * tx * tz)
        return v, oob

    def clearance(self, x, z):
        return self.bilinear(self.dist, x, z)

def path_clearance(path, grid):
    best = float('inf'); best_pt = None; oob_n = 0
    for p in path:
        c, oob = grid.clearance(p[0], p[1])
        if oob:
            oob_n += 1
            c = 0.0
        if c < best:
            best, best_pt = c, p
    return best, best_pt, oob_n

# ---------------------------------------------------------------- pair check ----
def closest_approach(pathA, lenA, pathB, lenB, speed, ds=SAMPLE_DS):
    """Both riders leave their own start at t=0 and loop their own path at `speed` u/s
    (wrapping at the end -- appropriate for a closed loop). Scans one full lap of the
    longer path."""
    def at_time(path, length, t):
        s = (speed * t) % length if length > 0 else 0.0
        idx = s / ds
        i0 = int(math.floor(idx))
        i0 = min(i0, len(path) - 1)
        i1 = min(i0 + 1, len(path) - 1)
        f = idx - i0
        return (path[i0][0] + f * (path[i1][0] - path[i0][0]),
                path[i0][1] + f * (path[i1][1] - path[i0][1]))
    dur = max(lenA, lenB) / speed
    steps = max(1, int(dur / (ds / speed)))
    best = float('inf'); best_t = 0.0
    for s in range(steps + 1):
        t = dur * s / steps
        a = at_time(pathA, lenA, t)
        b = at_time(pathB, lenB, t)
        d = math.hypot(a[0] - b[0], a[1] - b[1])
        if d < best:
            best, best_t = d, t
    return best, best_t

# ---------------------------------------------------------------- drawing ----
def draw_png(out_path, grid_path, setname, paths, labels):
    from PIL import Image, ImageDraw, ImageFont
    PXU = 22
    g = np.load(grid_path)
    ox, oz, cell = float(g['origin_x']), float(g['origin_z']), float(g['cell'])
    nx, nz = int(g['nx']), int(g['nz'])
    floor, obst = g['floor'], g['obstacle']
    W, H = int(round(nx * cell * PXU)), int(round(nz * cell * PXU))
    xs = ox + (np.arange(W) + 0.5) / PXU
    zzs = oz + (nz * cell - (np.arange(H) + 0.5) / PXU)
    ii = np.clip(((xs - ox) / cell).astype(np.int64), 0, nx - 1)
    jj = np.clip(((zzs - oz) / cell).astype(np.int64), 0, nz - 1)
    JJ, II = np.meshgrid(jj, ii, indexing='ij')
    fl = floor[JJ, II]; ob = obst[JJ, II]
    has_floor = ~np.isnan(fl); has_obst = ~np.isnan(ob)
    arr = np.empty((H, W, 3), dtype=np.uint8)
    arr[:, :] = (18, 18, 20)
    arr[has_floor & ~has_obst] = (196, 196, 200)
    arr[~has_floor & ~has_obst] = (10, 10, 12)
    if has_obst.any():
        t = np.clip((ob[has_obst] - 0.05) / (3.0 - 0.05), 0, 1)
        r = (255 - t * 145).astype(np.uint8); gch = (221 - t * 201).astype(np.uint8); bch = (87 - t * -23).astype(np.uint8)
        bch = np.clip(87 + t * (110 - 87), 0, 255).astype(np.uint8)
        flat = np.where(has_obst.ravel())[0]
        arr.reshape(-1, 3)[flat, 0] = r; arr.reshape(-1, 3)[flat, 1] = gch; arr.reshape(-1, 3)[flat, 2] = bch
    img = Image.fromarray(arr, "RGB")
    draw = ImageDraw.Draw(img)
    def w2p(wx, wz):
        return ((wx - ox) * PXU, H - (wz - oz) * PXU)
    font = ImageFont.load_default()
    x0i, x1i = int(np.floor(ox)), int(np.ceil(ox + nx * cell))
    z0i, z1i = int(np.floor(oz)), int(np.ceil(oz + nz * cell))
    for xv in range(x0i, x1i + 1):
        px_, _ = w2p(xv, 0)
        col = (230, 210, 40) if xv == 0 else (60, 90, 130)
        draw.line([(px_, 0), (px_, H)], fill=col, width=1)
        if xv % 5 == 0:
            draw.text((px_ + 2, 2), str(xv), fill=(230, 230, 235), font=font)
    for zv in range(z0i, z1i + 1):
        _, py_ = w2p(0, zv)
        col = (230, 210, 40) if zv == 0 else (60, 90, 130)
        draw.line([(0, py_), (W, py_)], fill=col, width=1)
        if zv % 5 == 0:
            draw.text((2, py_ - 6), str(zv), fill=(230, 230, 235), font=font)
    if setname:
        for name, (mx, mz, yaw) in load_marks(setname).items():
            mpx, mpy = w2p(mx, mz)
            if 0 <= mpx <= W and 0 <= mpy <= H:
                draw.ellipse([mpx - 3, mpy - 3, mpx + 3, mpy + 3], fill=(40, 220, 255), outline=(0, 0, 0))
    cyc = [(255, 60, 200), (60, 255, 120), (255, 230, 40)]
    for k, (path, label) in enumerate(zip(paths, labels)):
        col = cyc[k % len(cyc)]
        pix = [w2p(*p) for p in path]
        draw.line(pix, fill=col, width=2)
        draw.ellipse([pix[0][0] - 5, pix[0][1] - 5, pix[0][0] + 5, pix[0][1] + 5], outline=col, width=2)
        draw.text((pix[0][0] + 6, pix[0][1] + 4), label, fill=col, font=font)
    img.save(out_path)
    print("[png] wrote %s" % out_path)

# ---------------------------------------------------------------- main ----
def build_path(setname, mark_names, start):
    marks = load_marks(setname)
    missing = [m for m in mark_names if m not in marks]
    if missing:
        raise SystemExit("pathcheck: marks not found in %s (or its includes): %s\navailable: %s"
                          % (setname, missing, ", ".join(sorted(marks))))
    ctrl = []
    if start is not None:
        ctrl.append(tuple(start))
    ctrl.extend((marks[m][0], marks[m][1]) for m in mark_names)
    ctrl = dedup(ctrl)
    fine = catmull_rom_path(ctrl)
    path, length = resample_by_arclength(fine)
    return path, length

def report_one(label, path, length, grid):
    clr, clr_pt, oob_n = path_clearance(path, grid)
    rad, rad_i = min_turn_radius(path)
    rad_pt = path[rad_i] if rad_i >= 0 else None
    ok = clr >= CLEAR_MIN and rad >= RADIUS_MIN and oob_n == 0
    print("[pathcheck] %s: length %.2fu  samples %d" % (label, length, len(path)))
    print("  min clearance   %.3fu  at (%.2f, %.2f)%s  [need >= %.1fu]" %
          (clr, clr_pt[0], clr_pt[1], "  *** %d samples OFF THE MAPPED GRID ***" % oob_n if oob_n else "", CLEAR_MIN))
    print("  tightest radius %.3fu  at (%.2f, %.2f)  [need >= %.1fu]" %
          (rad, rad_pt[0] if rad_pt else float('nan'), rad_pt[1] if rad_pt else float('nan'), RADIUS_MIN))
    print("  -> %s" % ("PASS" if ok else "FAIL"))
    return ok

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("set", help="set NAME (resolved in reva_native/sets/, .set optional)")
    ap.add_argument("--marks", nargs="+", required=True)
    ap.add_argument("--start", default=None, help="x,z")
    ap.add_argument("--pair", action="store_true")
    ap.add_argument("--marks2", nargs="+", default=None)
    ap.add_argument("--start2", default=None)
    ap.add_argument("--speed", type=float, default=3.0)
    ap.add_argument("--png", default=None)
    ap.add_argument("--grid", default=DEFAULT_GRID)
    args = ap.parse_args()

    if not os.path.exists(args.grid):
        raise SystemExit("pathcheck: no grid at %s -- run build_grid.py first" % args.grid)
    grid = Grid(args.grid)

    start = tuple(float(v) for v in args.start.split(",")) if args.start else None
    pathA, lenA = build_path(args.set, args.marks, start)
    okA = report_one("A (%s)" % " ".join(args.marks), pathA, lenA, grid)
    ok = okA
    paths, labels = [pathA], ["A"]

    if args.pair:
        if not args.marks2:
            raise SystemExit("pathcheck: --pair needs --marks2")
        start2 = tuple(float(v) for v in args.start2.split(",")) if args.start2 else None
        pathB, lenB = build_path(args.set, args.marks2, start2)
        okB = report_one("B (%s)" % " ".join(args.marks2), pathB, lenB, grid)
        ok = ok and okB
        paths.append(pathB); labels.append("B")
        closest, t = closest_approach(pathA, lenA, pathB, lenB, args.speed)
        print("[pathcheck] pair: closest approach %.3fu at t=%.2fs (speed %.1fu/s)  [need >= 2.0u]" %
              (closest, t, args.speed))
        if closest < 2.0:
            ok = False
            print("  -> RIDERS COME WITHIN %.2fu -- not safe run simultaneously from these starts" % closest)
        else:
            print("  -> clear")

    if args.png:
        draw_png(args.png, args.grid, args.set, paths, labels)

    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
