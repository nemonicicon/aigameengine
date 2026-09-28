#!/usr/bin/env python3
"""make_crowd.py -- writes set `actor` lines for a CROWD (ROBOT NEKO ep 11: the half-pipe).

    actor <pack> <clip> x y z yaw scale [rate] [speed] [runlen] [board]

The set format is `design/the_set.md`; live examples are reva_native/sets/three_lawns.set,
parsons.set and other_side.set. Two facts from crowd_kms.c drive every choice below:

  SCALE is self-normalising. loadPack() measures each pack's own bind-pose span and sets
  P.scale = 3.15/(span*1.13) (crowd_kms.c ~line 940), and the actor draw path does
  `eff = A->sc * P->scale` (~line 5714) -- the SAME formula the prop path uses. That is why
  every humanoid actor in every shipped set carries `scale 1.000` regardless of how wild the
  pack's raw rig units are (measured: v1_Character_04 bind-pose span ~466 "units" tall,
  v1_crowd_susan ~1.7, v1_manny ~183, the curated packs ~0.03-0.07 -- and all four already
  read as adults on disk at scale 1.000). So scale stays 1.000 here too.

  YAW faces +z at 0 and turns toward +x as it increases: the static actor matrix builds
  forward = (sin(yaw), 0, cos(yaw)) (~line 5729: m[8]=sin(a), m[10]=cos(a)), and every
  "face this point" site in the engine (camera turns, mind flee/hunt, path facing, e.g.
  lines 4065, 5491, 5656, 12508) computes it the same way: yaw_deg = atan2(dx, dz) in
  degrees, dx=target.x-x, dz=target.z-z. face_yaw() below is exactly that.

CLIP RESOLUTION (actorClip(), crowd_kms.c ~4152): the actor's OWN pack is searched first;
only if the name is not found there does it fall through to bankFind() across the five
packs bankAdd() loads at boot (v1_mob1, v1_kaeli, v1_moves, v1_kmoves, v1_zmoves -- see
~line 11991-12023) -- and a bank hit only reaches the body if that body's OWN pack has
cbOK && pbOK>=10 (the canonical-skeleton census), else the engine prints
"is in a bank, but this rig has no canonical basis" and the actor is left with no clip at
all (a statue). This module never guesses that census: CHEER_OWN pairs are clips native to
their own pack (checked below with Rpack.clip_names(), so cbOK/pbOK never matters), and
CHEER_BANK_PACKS is exactly the roster already proven to receive a bank clip by shipped,
committed sets (three_lawns.set/parsons.set/twelve.set/other_side.set already run
zombie/w_cheer/m_drink/m_sway/x_fidget/w_look/w_text/z_greet on every one of them).

usage:
    make_crowd.py --preset ep11_halfpipe --seed 11 --out <path.set>
    make_crowd.py --shape line --p0 -15.5,-6.4 --p1 -15.5,6.4 --rows 2 --row-step -1.5 \
                  --target 0,0 --n 24 --seed 1
    make_crowd.py --verify        # just check every pack/clip pair on disk and exit
"""
import argparse, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
PACKS_DIR = os.path.join(REPO, "reva_native", "packs")
sys.path.insert(0, os.path.join(REPO, "skate_src", "author"))
from rpack import Rpack   # Rpack(path).clip_names() -- the P0 "does this pack/clip exist" gate

# ============================ THE CHEER ROSTER =======================================
# OWN: (pack, clip) where the clip is native to that exact pack -- no bank, no cbOK/pbOK,
# works regardless of canonical status. Confirmed with Rpack.clip_names() (see verify_roster).
CHEER_OWN = [
    ("v1_manny", "w_cheer"),                                                    # own 28-clip body; w_cheer is native here too
    ("v1_curated___Woman_Casual__jpKRgGDxhk",   "HumanArmature|Female_Clapping"),
    ("v1_curated___Woman_in_Dress__zMyPlQXBzq", "HumanArmature|Female_Clapping"),
    ("v1_curated___Woman_in_Tank_Top__XqzeZGB7iU", "HumanArmature|Female_Clapping"),
    ("v1_curated___Characters_Matt__66kQ4dBBC7",  "CharacterArmature|Wave"),
    ("v1_curated___Characters_Sam__UcLErL2W37",   "CharacterArmature|Jump"),
    ("v1_curated___Characters_Shaun__eJFT9MxzOM", "CharacterArmature|Jump_Idle"),
    # ⛔ NOT "CharacterArmature|Wave" -- the Cube_* bakes tripled the armature name in
    # their clip strings (confirmed with rpack.py; the simple name does not exist here).
    ("v1_curated___Cube_Guy_Character__K1IczhnvQ5",
        "CharacterArmature|CharacterArmature|CharacterArmature|Wave"),
    ("v1_curated___Cube_Woman_Character__75ikp7NEDx",
        "CharacterArmature|CharacterArmature|CharacterArmature|Jump"),
]

# BANK: w_cheer borrowed from v1_moves (bankAdd'd at boot). Every pack below already plays
# a bank-borrowed clip (zombie/w_cheer/m_drink/m_sway/x_fidget/w_look/w_text/z_greet) in a
# committed set on disk, which is the only real proof of cbOK/pbOK short of running the
# engine (out of scope for this task) -- so the roster is exactly that proven set, not a
# guess extended to unrelated packs.
# ⛔ parsons.set ALSO references v1_Character_{18,20,22,24,25,27,28,33,35,37,39} bare, but
# verify_roster() (below) shows none of those .rpack files exist any more -- 17-28 now only
# exist as costumed variants (_Police/_Female_Police/_Firefighter/_Doctor/_HM) and 33-39
# bare don't exist at all. Those set lines are dead today (setPack returns -1, the actor is
# silently dropped); rather than guess which costumed variant was meant, they are left out
# of this roster. A clip's name is not evidence, and neither is a pack's -- checked on disk.
CHEER_BANK_PACKS = [
    "v1_Character_01", "v1_Character_02", "v1_Character_04", "v1_Character_05",
    "v1_Character_07", "v1_Character_08", "v1_Character_09", "v1_Character_10",
    "v1_Character_12", "v1_Character_13", "v1_Character_14", "v1_Character_16",
    "v1_Character_30", "v1_Character_31",
    "v1_Character_29_Female", "v1_Character_30_Female", "v1_Character_32",
    "v1_Character_Female_02", "v1_Character_Female_04", "v1_Character_Female_05",
    "v1_Character_Killer_02", "v1_Character_Killer_04",
    "v1_crowd_susan", "v1_crowd_bgirl", "v1_crowd_soundguy",
]
BANK_DONOR_PACK = "v1_moves"
BANK_CLIP = "w_cheer"

# ============================ THE SIGHT GAG ==========================================
# ep 7 note, confirmed by reading the pack directly (rpack.py): v1_reva (the July ancestor,
# 27 nodes) carries exactly 37 clips -- p_live1-3, 8 mirrored dance-pair sets, c_kiss/insult,
# w_walk/fly/ski/run -- and NOT ONE of them is an idle. m_macarena is one of the 37, native
# to the pack, so it needs no bank/canonical transfer at all: she will macarena, forever,
# because that is the only kind of clip her bank has.
GAG_PACK, GAG_CLIP = "v1_reva", "m_macarena"


def verify_roster(packs_dir=PACKS_DIR, strict=True):
    """Confirm every pack file exists and every OWN pair's clip is really on that pack, and
    that the bank donor really carries BANK_CLIP. Returns a list of problem strings (empty
    if clean); raises SystemExit if strict and non-empty."""
    problems = []

    def clips_of(pack):
        p = os.path.join(packs_dir, pack + ".rpack")
        if not os.path.isfile(p):
            problems.append("missing pack file: %s.rpack" % pack)
            return None
        try:
            return Rpack(p).clip_names()
        except Exception as e:
            problems.append("%s: unreadable (%s)" % (pack, e))
            return None

    donor = clips_of(BANK_DONOR_PACK)
    if donor is not None and BANK_CLIP not in donor:
        problems.append("bank donor %s has no clip '%s'" % (BANK_DONOR_PACK, BANK_CLIP))
    for pack in CHEER_BANK_PACKS:
        if clips_of(pack) is None:
            continue   # missing-pack already recorded; a bank clip needs no OWN clip list
    for pack, clip in CHEER_OWN + [(GAG_PACK, GAG_CLIP)]:
        names = clips_of(pack)
        if names is not None and clip not in names:
            problems.append("%s: no clip '%s' (has %s)" % (pack, clip, ", ".join(names[:8])))
    if problems and strict:
        raise SystemExit("make_crowd.py: roster verification FAILED --\n  " +
                          "\n  ".join(problems))
    return problems


# ============================ SHAPES =================================================
def face_yaw(x, z, target):
    """Degrees, matching crowd_kms.c's own convention: atan2(dx,dz), forward=+z at yaw=0,
    turning toward +x as yaw increases (verified against actorsDraw's matrix build and
    every 'turn to face' call site in the engine)."""
    tx, tz = target
    return math.degrees(math.atan2(tx - x, tz - z))


def line_rows(p0, p1, rows, row_step, n=None, spacing=0.9, jitter=0.28, y=0.0, rng=None):
    """`rows` parallel copies of the segment p0->p1, offset along the segment's own outward
    normal by row_step per row (row_step's SIGN sets which way rows stack, e.g. -1.5 to grow
    away from a pipe on the -x side). n bodies are split as evenly as possible across the
    rows and spread evenly along each row's length (so a fixed n against a fixed length
    fixes the realised spacing); n=None instead derives n per row from length/spacing.
    Points are jittered by up to `jitter` on both axes. Returns [(x,y,z), ...]."""
    rng = rng or random.Random(0)
    (x0, z0), (x1, z1) = p0, p1
    length = math.hypot(x1 - x0, z1 - z0)
    if length < 1e-6:
        ux, uz, nx, nz = 1.0, 0.0, 0.0, 1.0
    else:
        ux, uz = (x1 - x0) / length, (z1 - z0) / length
        nx, nz = -uz, ux                      # unit normal, rotated +90 deg from the segment
    per_row_n = n if n is not None else max(1, round(length / spacing) + 1)
    pts = []
    for k in range(rows):
        cnt = (per_row_n // rows + (1 if k < per_row_n % rows else 0)) if n is not None \
              else per_row_n
        for i in range(cnt):
            t = (i + 0.5) / cnt
            bx = x0 + ux * length * t + nx * row_step * k
            bz = z0 + uz * length * t + nz * row_step * k
            bx += rng.uniform(-jitter, jitter)
            bz += rng.uniform(-jitter, jitter)
            pts.append((bx, y, bz))
    return pts


def rect_fill(xr, zr, n, spacing=0.9, jitter=0.28, y=0.0, rng=None):
    """n bodies filling the rectangle xr=(x0,x1) x zr=(z0,z1) on a jittered near-`spacing`
    grid (1-3 "rows" falls out naturally when the rectangle is shallow in one axis)."""
    rng = rng or random.Random(0)
    w, d = xr[1] - xr[0], zr[1] - zr[0]
    cols = max(1, round(math.sqrt(max(1, n) * abs(w) / max(abs(d), 1e-6))))
    nrows = max(1, math.ceil(n / cols))
    pts = []
    for r in range(nrows):
        for c in range(cols):
            if len(pts) >= n:
                break
            fx, fz = (c + 0.5) / cols, (r + 0.5) / nrows
            bx = xr[0] + fx * w + rng.uniform(-jitter, jitter)
            bz = zr[0] + fz * d + rng.uniform(-jitter, jitter)
            pts.append((bx, y, bz))
    return pts[:n]


# ============================ ACTOR LINES ============================================
def build_actors(points, roster, target, rng, rate_range=(0.85, 1.15), scale=1.0,
                  gag=None, gag_index=None):
    """One (pack, clip) per point, cycling a shuffled copy of `roster` (re-shuffled every
    pass so a repeating chorus line doesn't line up with the row spacing), each body given
    its own random `rate` in rate_range so the crowd doesn't move in lockstep. `gag`, if
    given, overrides exactly one point (gag_index, default: front-row-ish middle) with a
    single (pack, clip) pair -- the sight gag, planted where jacob's eyes will find it."""
    order = list(roster)
    rng.shuffle(order)
    n = len(points)
    gi = gag_index if gag_index is not None else n // 2
    lines = []
    for i, (x, y, z) in enumerate(points):
        if i and i % len(order) == 0:
            rng.shuffle(order)
        pack, clip = gag if (gag is not None and i == gi) else order[i % len(order)]
        yaw = face_yaw(x, z, target)
        rate = rng.uniform(*rate_range)
        lines.append("actor %-46s %-58s %7.2f %5.2f %7.2f %7.2f %5.3f %5.3f" %
                      (pack, clip, x, y, z, yaw, scale, rate))
    return lines


# ============================ THE EP11 PRESET ========================================
def ep11_halfpipe(seed=11):
    """Two crowds lining the half-pipe's long sides (the pipe: centred at 0,0,0, axis along
    z, spans x -14..14 including its decks, z -7..7) plus a short row at the +z end. Rows
    sit behind the decks at x=+-15.5 and x=+-17 (row_step 1.5, matching those two numbers
    exactly), each ~12.8 u long (z -6.4..6.4, inset off the pipe's own z-extent so the side
    rows don't collide with the end row's corner), 12 bodies per row -> spacing ~1.07 u,
    close to the nominal 0.9 (a tighter pack would have pushed the total over the 40-60
    budget once the end row is added). Every body faces the pipe centre (0,0,0). The
    macarena gag stands in the front row (x=15.5) at z~0, dead centre of that side, cheek
    by jowl with everyone actually cheering."""
    rng = random.Random(seed)
    target = (0.0, 0.0)
    roster = CHEER_OWN + [(p, BANK_CLIP) for p in CHEER_BANK_PACKS]

    # ⛔ line_rows' normal for a segment running +z is (-1,0) (left of travel) -- so
    # row_step=-1.5 walks the +x side OUTWARD to x=17.0, and +1.5 walks the -x side
    # outward to x=-17.0. Getting this backwards once put row 1 at x~14 (INSIDE row 0,
    # closer to the pipe than the deck edge) instead of behind it; caught by reading the
    # generated file's actual coordinates, not by trusting the arithmetic.
    pts_pos = line_rows((15.5, -6.4), (15.5, 6.4), rows=2, row_step=-1.5, n=24, rng=rng)  # +x side, 2x12
    pts_neg = line_rows((-15.5, -6.4), (-15.5, 6.4), rows=2, row_step=1.5, n=24, rng=rng)  # -x side, mirrored
    pts_end = line_rows((-4.0, 9.0), (4.0, 9.0), rows=1, row_step=1.5, n=8, rng=rng)        # short row, +z end

    lines = []
    lines += ["# +x side, 2 rows behind the deck (x=15.5 front / 17.0 back), facing the pipe"]
    lines += build_actors(pts_pos, roster, target, rng, gag=(GAG_PACK, GAG_CLIP), gag_index=len(pts_pos) // 4)
    lines += ["", "# -x side, mirrored"]
    lines += build_actors(pts_neg, roster, target, rng)
    lines += ["", "# short row at the +z end, looking back down the pipe"]
    lines += build_actors(pts_end, roster, target, rng)
    return lines, len(pts_pos) + len(pts_neg) + len(pts_end)


HEADER = """# crowd_example.set -- ROBOT NEKO ep 11, THE FLOOR (X-GAMES): the half-pipe crowd.
# Written by skate_src/robot_neko/make_crowd.py --preset ep11_halfpipe --seed %d
#
# The half-pipe itself (ramp geometry, deck props) is the director's -- this is only the
# crowd: %d bodies in 2 rows down each long side plus a short row at the +z end, all
# facing the pipe centre (0,0,0). One of them (front row, +x side) is v1_reva doing
# m_macarena -- her whole bank is 37 dance clips and not one idle (confirmed via
# skate_src/author/rpack.py), so she is not cheering, she is macarena-ing, forever, which
# is the joke.
#
# `include` this file from the shot set, after the ramp itself is placed, e.g.:
#     include crowd_example
"""


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", choices=["ep11_halfpipe"])
    ap.add_argument("--shape", choices=["line", "rect"])
    ap.add_argument("--p0"); ap.add_argument("--p1")
    ap.add_argument("--rows", type=int, default=2)
    ap.add_argument("--row-step", type=float, default=1.5)
    ap.add_argument("--xr"); ap.add_argument("--zr")
    ap.add_argument("--target", default="0,0")
    ap.add_argument("--n", type=int)
    ap.add_argument("--spacing", type=float, default=0.9)
    ap.add_argument("--jitter", type=float, default=0.28)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--rate-lo", type=float, default=0.85)
    ap.add_argument("--rate-hi", type=float, default=1.15)
    ap.add_argument("--gag", action="store_true", default=True)
    ap.add_argument("--no-gag", dest="gag", action="store_false")
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--out")
    ap.add_argument("--verify", action="store_true", help="just check the roster and exit")
    a = ap.parse_args(argv[1:])

    problems = verify_roster(strict=not a.verify)
    if a.verify:
        print("roster: %d OWN pairs, %d BANK pairs, 1 gag pair" %
              (len(CHEER_OWN), len(CHEER_BANK_PACKS)))
        if problems:
            print("PROBLEMS:\n  " + "\n  ".join(problems)); return 1
        print("all pack/clip pairs verified on disk (Rpack.clip_names()).")
        return 0

    if a.preset == "ep11_halfpipe":
        lines, count = ep11_halfpipe(seed=a.seed)
        text = (HEADER % (a.seed, count)) + "\n" + "\n".join(lines) + "\n"
    else:
        ok = a.n and ((a.shape == "line" and a.p0 and a.p1) or (a.shape == "rect" and a.xr and a.zr))
        if not ok:
            ap.error("need --preset, or --shape line --p0 --p1 --n, or --shape rect --xr --zr --n")
        rng = random.Random(a.seed)
        tx, tz = (float(v) for v in a.target.split(","))
        roster = CHEER_OWN + [(p, BANK_CLIP) for p in CHEER_BANK_PACKS]
        if a.shape == "line":
            x0, z0 = (float(v) for v in a.p0.split(","))
            x1, z1 = (float(v) for v in a.p1.split(","))
            pts = line_rows((x0, z0), (x1, z1), a.rows, a.row_step, n=a.n,
                             spacing=a.spacing, jitter=a.jitter, rng=rng)
        else:
            x0, x1 = (float(v) for v in a.xr.split(","))
            z0, z1 = (float(v) for v in a.zr.split(","))
            pts = rect_fill((x0, x1), (z0, z1), a.n, spacing=a.spacing, jitter=a.jitter, rng=rng)
        gag = (GAG_PACK, GAG_CLIP) if a.gag else None
        lines = build_actors(pts, roster, (tx, tz), rng,
                              rate_range=(a.rate_lo, a.rate_hi), scale=a.scale, gag=gag)
        text = "\n".join(lines) + "\n"

    if a.out:
        os.makedirs(os.path.dirname(a.out), exist_ok=True)
        with open(a.out, "w") as f:
            f.write(text)
        print("wrote %s (%d actor lines)" % (a.out, sum(1 for l in lines if l.startswith("actor "))))
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
