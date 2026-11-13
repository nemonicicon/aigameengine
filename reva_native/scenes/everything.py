#!/usr/bin/env python3
"""everything.py -- ONE SET THAT SHOWS EVERYTHING THE ENGINE CAN DO, and a camera tour of it.

One scene that tests all of the engine's capabilities at once (11-05).
A row of stations along one floor, a station a capability, and one camera a station. Each new thing the engine learns gets a station here in the step that builds it.

    python reva_native/scenes/everything.py                 writes sets/everything.set, sets/everything.trk and sets/everything.cams.txt, and says what's in it
    python reva_native/scenes/everything.py --shoot OUT.png and shoots the tour in the anim lab: OUT_<station>.png each, and OUT_sheet.png with all of them
    python reva_native/scenes/everything.py --list          the stations and what each one is there to show
    python reva_native/scenes/everything.py --film OUT.png  the same set with a film cut of its timeline (sets/everything_film.set): a lens that moves and rolls on the track,
                                                            sepia, grain, a flash and a burn from its post row, the lens focused on its aim. Five moments, one sheet
    python reva_native/scenes/everything.py --display OUT.png   one station as each display setting shows it (queue step 25): the picture held to 4:3, as a 480-line tube shows it,
                                                            as a 240-line tube does, and put together over frames. A boot each, because a display's settings are fixed at start
    python reva_native/scenes/everything.py --words OUT.png  the TV font's page of every script (queue step 26): Latin with its accents, Greek, Cyrillic, and, if a language with wide
                                                            glyphs is given (--lang ja, with its data/<name>.tvw baked), Chinese, Japanese and Korean at the large reading size
    python reva_native/scenes/everything.py --shoot OUT.png --soft   the tour through the engine's own rasterizer (queue step 27): 320 x 240 on the processor, no window opened.
                                                            What it doesn't draw (shadows, mirrors, screens, beams, fur, materials) is plain there, which is the point of looking
    python reva_native/scenes/everything.py --net OUT.png   online play (queue step 29) isn't a station either: it's two machines. Two engines play one game of rally across a
                                                            wire, on the engine's own rasterizer with no window, and each one's picture at the same ticks is put side by side:
                                                            the same world in both, and each machine's own bat the bright one (anim_bench/netcheck.py --pictures)
    ... --only fire,water   --tier high|pi|film   --env K=V (as driver.py looks takes them)

What a display setting is isn't a station: it's the whole picture. REVA_GFX="tube=1" (or aspect=4:3, taa=1, hdrout=1, width=..,height=..) on any --shoot shows the tour that way,
and REVA_INTERP=1 with REVA_BTWDBG=<folder> counts and saves the pictures between the rules' ticks on a display faster than 60.

A station whose packs aren't on this machine is left out, and said: the engine's kit has no bought or show content, and the set still loads there.
It's a place to LOOK. It says nothing is right: that's still eyes on the sheet (the stations' own lines say what to look for).
"""
import math
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NATIVE = os.path.normpath(os.path.join(HERE, ".."))
ROOT = os.path.normpath(os.path.join(NATIVE, ".."))
DRIVER = os.path.join(ROOT, ".claude", "skills", "run-reva", "driver.py")
Z = 3.5          # the line the stations stand on
STEP = 11.0      # a station's width


def has(stem):
    return any(os.path.exists(os.path.join(NATIVE, d, stem + ".rpack")) for d in ("props", "packs", "."))


class Set:
    def __init__(self):
        self.lines, self.lights, self.beams, self.movers, self.stations, self.left = [], [], [], [], [], []

    def add(self, *ln):
        self.lines += list(ln)

    def need(self, name, *stems):
        """does this machine have every pack a station wants? If not it's left out, and said"""
        miss = [s for s in stems if not has(s)]
        if miss:
            self.left.append("%s (no %s)" % (name, ", ".join(miss)))
        return not miss

    def station(self, key, cx, what, look, cam=None):
        self.stations.append((key, cx, what, look, cam or (cx, 2.3, Z + 9.5, cx, 1.25, Z, 40)))


def build():
    S = Set()
    n = 0

    def cx():
        return 5.0 + STEP * n
    # ---- bodies: skin, hair, eyes, a wardrobe, a body that walks
    if S.need("bodies", "v1_kaeli_street"):
        x = cx()
        if has("v1_reva_hi"):
            S.add("actor v1_reva_hi idle %.2f 0 %.2f 0 1.0" % (x - 2.2, Z), "actor v1_reva_hi idle %.2f 0 %.2f 0 1.0 outfit=skirt" % (x - 0.4, Z))
        S.add("actor v1_kaeli_street w_walk %.2f 0 %.2f 90 1.0 1.0 0.55" % (x + 1.0, Z + 1.6), "path %.2f 0 %.2f" % (x - 3.0, Z + 1.6), "path %.2f 0 %.2f" % (x + 3.5, Z + 1.6))
        S.station("bodies", x, "bodies' surfaces (skin, hair, eyes), a wardrobe's two outfits, and a body walking a path",
                  "skin that isn't plastic, hair with a long highlight, eyes that are wet; two outfits on one pack; feet that don't slide")
        n += 1
    # ---- materials: rough to mirror, metal, normal maps, parallax, detail
    if S.need("materials", "mat_floor", "mat_balls", "mat_panel"):
        x = cx()
        S.add("prop mat_floor %.2f 0.02 %.2f 0 1" % (x, Z - 1.0), "prop mat_balls %.2f 0.02 %.2f 0 1" % (x, Z - 1.0), "prop mat_panel %.2f 0.02 %.2f -30 1" % (x + 4.2, Z - 2.2),
              "reflect %.2f 1.4 %.2f" % (x, Z + 1.5))
        S.lights += [(x - 4.5, 2.6, Z + 2.0, 6.0, 3.3, 1.5), (x + 4.0, 1.4, Z + 2.5, 1.8, 3.0, 6.0)]
        S.station("materials", x, "what a surface is made of: ten balls from rough to mirror, plain and metal; a panel with a normal map and depth; a floor with fine grain",
                  "highlights that tighten left to right; the metal row tinted by its own colour; studs on the panel that catch the light and shift with the lens",
                  (x, 2.6, Z + 8.5, x, 1.2, Z - 1.0, 38))
        n += 1
    # ---- fur and animals
    if S.need("fur", "animal_shiba", "animal_husky", "animal_fox", "animal_alpaca"):
        x = cx()
        for k, a in enumerate(("animal_shiba", "animal_husky", "animal_fox", "animal_alpaca")):
            S.add("actor %s Idle %.2f 0 %.2f 20 1.0" % (a, x - 3.0 + 1.9 * k, Z))
        S.add("actor animal_husky Gallop %.2f 0 %.2f 90 1.0 1.0 1.83 8.0" % (x - 4.0, Z + 2.4))
        S.station("fur", x, "fur in shells on four animals at their true sizes, and one that gallops", "coats with depth at their edges, lying the way the wind and the run push them")
        n += 1
    # ---- cloth, and the toon pass
    if S.need("cloth and toon", "v1_amelia", "v1_wanda", "v1_viktor"):
        x = cx()
        S.add("actor v1_amelia idle %.2f 0 %.2f 0 1.0 outfit=robe" % (x - 2.0, Z), "actor v1_wanda idle %.2f 0 %.2f 0 1.0" % (x, Z), "actor v1_viktor idle %.2f 0 %.2f 0 1.0" % (x + 1.8, Z))
        S.station("cloth", x, "hanging cloth simulated on a body (a robe), and the toon pass: a cel ramp and an ink line, at true size",
                  "a robe that hangs and sways and isn't a sheet across the room; flat cel shading with a clean line round each body")
        n += 1
    # ---- lights and shadows
    if S.need("lights", "v1_reva902"):
        x = cx()
        S.add("actor v1_reva902 idle %.2f 0 %.2f 0 1.0" % (x, Z))
        S.add("spot %.2f 5.5 %.2f 0 -0.82 -0.57 1.0 0.92 0.75 3 34 1" % (x, Z + 4.0))
        S.lights += [(x - 3.0, 1.2, Z + 0.5, 2.4, 0.4, 0.3), (x + 3.0, 1.2, Z + 0.5, 0.3, 0.6, 2.4)]
        S.station("lights", x, "lights beyond the sun: a spot that casts a shadow, and two coloured lamps", "her shadow from the spot on the floor behind her, red from one side and blue from the other")
        n += 1
    # ---- water and a mirror. With a bed under it (skate_src/author/groundwork.py) the water has a shore (queue step 24)
    x = cx()
    if has("gw_pond") and has("gw_rock"):
        S.add("prop gw_pond %.2f 0 %.2f 0 1" % (x, Z), "water %.2f 0.36 %.2f 6.2 4.2 0.07 0.26 0.36" % (x, Z),
              "prop gw_rock %.2f 0.10 %.2f 30 1.3" % (x - 1.5, Z + 0.5), "prop gw_rock %.2f 0.08 %.2f 200 1.0" % (x + 1.7, Z - 0.3), "prop gw_rock %.2f 0.30 %.2f 110 0.8" % (x + 2.5, Z + 1.3))
    else:
        S.left.append("the pond's bed (no gw_pond, gw_rock): the water's a sheet over the floor")
        S.add("water %.2f 0.42 %.2f 6.4 4.6 0.09 0.30 0.42" % (x, Z))
    S.add("mirror %.2f 1.9 %.2f 0 3.2 2.6" % (x, Z - 4.4))
    if has("animal_fox"):
        S.add("actor animal_fox Idle %.2f 0 %.2f 200 1.0" % (x + 3.4, Z + 4.4))
    S.station("water", x, "water: the sky and the sun in it, what's under it bent by its ripples and darker as it deepens, foam where it meets its shore (step 24); and a mirror that shows the set",
              "the bed and the rocks seen through the water, wobbling; a pale broken line round the shore and round each rock; no hard edge where water meets ground; the set in the glass",
              (x, 3.1, Z + 8.0, x, 0.3, Z - 0.4, 40))
    n += 1
    # ---- particles: one of every kind (queue step 24)
    x = cx()
    kinds = (("fire", "0 1 0"), ("smoke", "0 1 0"), ("sparks", "0 1 0"), ("steam", "0 1 0"), ("dust", "0 1 0"), ("embers", "0 1 0"), ("fountain", "0 1 0"), ("snow", "0 -1 0"),
             ("exhaust", "0 1 0"), ("pyro", "0 1 0"), ("fuse", "0 1 0"))
    for k, (kind, d) in enumerate(kinds):
        px, py = x - 4.5 + 0.9 * k, (3.6 if kind == "snow" else 0.12)
        S.add("emitter %s %.2f %.2f %.2f %s" % (kind, px, py, Z, d))
    S.station("particles", x, "particles: one emitter of every kind, left to right fire smoke sparks steam dust embers fountain snow exhaust pyro fuse",
              "fire and sparks that glow (added), smoke and dust that veil (laid over, far to near); a fountain that arcs and falls; none of it popping into being")
    n += 1
    # ---- the set's timeline: beams that sweep, movers that orbit, an emitter riding one
    x = cx()
    S.beams += [("sweep", x), ("still", x)]
    if has("trk3_card"):
        S.add("mover trk3_card grid=2x2", "mover trk3_card grid=2x2")
        S.movers += [("orbit", x), ("bob", x)]
        S.add("emitter fuse 0 0.4 0 0 1 0 on=0")
    S.station("timeline", x, "the set's timeline: beams that sweep and (step 24) light what they point at, props it moves and re-pictures, an emitter riding one",
              "two shafts of light in the air; cards that circle and change their picture; sparks that stay on the moving card")
    n += 1
    # ---- the set's drawings (11-11, engine/setart.inc): a whiteboard in the world that gets drawn on, frame by frame on the timeline's clock
    x = cx()
    S.add("art sets/everything.art")
    S.art_x = x
    S.station("drawings", x, "the set's drawings (11-11): pen strokes, flat shapes and written words on a plane in the world, drawn in time on the timeline's clock",
              "a white board standing in the air with a creature drawn on it by a marker, a line of handwriting under it and a flat coloured shape; all of it growing as the clock runs")
    n += 1
    # ---- decals thrown at the world, and rain (queue step 24; the steps are skate_src/author/groundwork.py's)
    x = cx()
    if has("gw_steps"):
        S.add("prop gw_steps %.2f 0 %.2f 0 1" % (x, Z - 0.6))
    else:
        S.left.append("the decals' steps and wall (no gw_steps): they're on the floor alone")
    S.add("decal splat %.2f 0.34 %.2f 1.7 0 -1 -0.45 deep=0.7 rgb=0.72,0.06,0.05 a=0.95" % (x - 1.3, Z - 0.62),
          "decal scorch %.2f 2.0 %.2f 2.0 0 0 -1 deep=0.5" % (x + 1.2, Z - 3.36),
          "decal scorch %.2f 0 %.2f 1.5" % (x + 2.4, Z + 1.3),
          "decal wet %.2f 0 %.2f 2.6" % (x - 2.2, Z + 1.7),
          "decal stripe %.2f 0 %.2f 5.2 along=0.2 rgb=0.95,0.86,0.16" % (x, Z + 3.0),
          "decal crack %.2f 0 %.2f 2.4" % (x + 0.4, Z + 1.2),
          "decal mark %.2f 0 %.2f 0.9 rgb=0.03,0.03,0.03" % (x - 0.4, Z + 0.4))
    if has("trk3_card") and has("gw_steps"):
        S.add("decal picture %.2f 2.2 %.2f 1.3 0 0 -1 deep=0.4 pic=trk3_card" % (x - 1.4, Z - 3.36))
    S.add("emitter rain %.2f 9 %.2f 0 -1 0" % (x, Z + 1.0))
    S.station("decals", x, "decals thrown at the world: a splat over a step's edge, a scorch and a picture on the wall, a scorch, a wet patch, a crack and a mark on the floor, a painted stripe; and rain",
              "the splat folding down the step's face and onto the floor; marks that take the floor's own light and shadow; the stripe lit like paint, dark where a shadow crosses it; streaks of rain",
              (x, 2.7, Z + 8.0, x, 0.9, Z - 1.2, 40))
    n += 1
    # ---- foliage: grass and plants scattered by one line each, swaying in the set's wind (queue step 24; skate_src/author/foliage.py makes the three props)
    if S.need("foliage", "fol_grass", "fol_bush", "fol_tree"):
        x = cx()
        S.add("prop floor_01 %.2f 0.015 %.2f 0 1.6 0.17 0.22 0.11" % (x, Z - 0.5),
              "grass %.2f %.2f %.2f %.2f 0.9" % (x - 4.6, Z - 3.2, x + 4.6, Z + 2.6),
              "scatter fol_bush %.2f %.2f %.2f %.2f 5 seed=7 scale=0.7,1.15 sway=0.05 rgb=0.62,0.58,0.20,0.30" % (x - 4.2, Z - 2.8, x + 4.2, Z - 0.2),
              "scatter fol_tree %.2f %.2f %.2f %.2f 3 seed=11 scale=0.62,0.82 sway=0.03" % (x - 4.0, Z - 3.4, x + 4.0, Z - 2.2))
        S.station("foliage", x, "foliage: a field of grass from one line, bushes and trees scattered by another, each its own size and turn, leaning in the set's wind",
                  "blades dark at the foot and light at the tip, no two patches alike, bushes washed a little yellow by their own amounts; in motion, waves crossing the field",
                  (x, 2.0, Z + 8.5, x, 1.3, Z - 1.0, 40))
        n += 1
    # ---- level of detail: one prop at four distances, each drawn as a different copy (queue step 24; skate_src/author/lod.py makes the copies)
    if S.need("detail", "fol_tree", "fol_tree~lod1", "fol_tree~lod2", "fol_tree~far"):
        x = cx()
        eye = (x, 2.3, Z + 9.5)
        for k, d in enumerate((10.0, 35.0, 90.0, 200.0)):
            a = (k - 1.5) * 0.2                                     # spread across the lens, so the near one doesn't hide the far ones
            tx, tz = eye[0] + d * math.sin(a), eye[2] - d * math.cos(a)
            if k:
                S.add("prop floor_01 %.2f 0 %.2f 0 1.6 0.30 0.32 0.30" % (tx, tz))
            S.add("prop fol_tree %.2f 0 %.2f %d 0.7" % (tx, tz, 40 * k))
            if k < 3:                                              # and two bodies beside each of the first three: a body has lighter copies too, and no far picture
                if has("animal_husky") and has("animal_husky~lod1"):
                    S.add("actor animal_husky Idle %.2f 0 %.2f 30 1.0" % (tx + 1.3 + 0.02 * d, tz + 1.0))
                if has("v1_reva902") and has("v1_reva902~lod1"):
                    S.add("actor v1_reva902 idle %.2f 0 %.2f 0 1.0" % (tx - 1.3 - 0.02 * d, tz + 1.0))
        S.station("detail", x, "level of detail: one tree at 10, 35, 90 and 200 units, drawn as itself, its two lighter copies and its far picture, with a dog and a person beside the first three, on their own lighter copies from 35 on (REVA_LODDBG=1 colours every copy)",
                  "left to right, near to far: four trees that read as the same tree; with REVA_LODDBG=1 the second is washed green, the third yellow, the fourth red",
                  (eye[0], eye[1], eye[2], x, 1.9, Z - 30.0, 40))
        fa = 0.2
        S.station("detailfar", x, "the same row through a long lens: the two far trees, the third a lighter copy and the fourth two flat cards with its picture",
                  "a tree at 90 units and one at 200 that still read as the near one's shape and colour, the far one with no hard square round it",
                  (eye[0], eye[1], eye[2], eye[0] + 100.0 * math.sin(fa), 2.2, eye[2] - 100.0 * math.cos(fa), 13))
        n += 1
    return S, n


def write_art(x, fps, frames):
    """the drawings station's file: a board 4.4 units wide at the station, a creature drawn on it over the loop, a written line, a flat shape (skate_src/band)"""
    sys.path.insert(0, os.path.join(ROOT, "skate_src", "band"))
    import artfile as AF
    import doodles as D
    import strokefont as SF
    A = AF.Art(fps, frames)
    board = A.plane(origin=(x - 2.2, 3.4, Z - 1.0), across=(4.4 / 1600.0, 0, 0), down=(0, -2.75 / 1000.0, 0))
    pen = D.Pen(seed=11)
    pen.path([(0.30, 0.42), (0.26, 0.66), (0.42, 0.84), (0.66, 0.86), (0.84, 0.68), (0.82, 0.44), (0.58, 0.34)], closed=True)
    pen.circle((0.46, 0.66), 0.06); pen.circle((0.66, 0.67), 0.06); pen.dot((0.47, 0.65)); pen.dot((0.67, 0.66)); pen.arc((0.56, 0.52), 0.1, 3.5, 5.9, col="red")
    for lx in (0.38, 0.50, 0.63, 0.76): pen.line((lx, 0.37), (lx - 0.03, 0.18))
    creature = A.doodle(pen.strokes, D.INK)
    words = A.doodle(SF.Font("EMSTech.svg").write("drawn by the set's own clock", 0.06, seed=3).strokes, D.INK)
    tri = A.mesh([(0, 0), (160, 0), (80, 130)])
    for f in range(frames):
        u = f / float(frames)
        A.rect(f, board, 0, 0, 1600, 1000, (0.96, 0.96, 0.94, 1))
        A.group(f, board, creature, AF.board(120, 900, 760), upto=min(1.0, u * 2.0))
        A.group(f, board, words, AF.board(760, 860, 620), upto=min(1.0, max(0.0, u * 2.0 - 0.6)))
        A.mesh_at(f, board, tri, AF.aff(1, tx=1250, ty=160), (0.84, 0.25, 0.23, 1))
    A.write(os.path.join(NATIVE, "sets", "everything.art"))


def write(S, n):
    out = ["# everything.set -- written by scenes/everything.py: one set that shows everything the engine can do. Don't edit it: add a station there.",
           "climb", "sun 44 150 0.95 1.0 0.96 0.90", "sky 0.36 0.42 0.54", "ground 0.26 0.25 0.24", "wind 0.9 0.0 0.4 0.4"]
    for ix in range(int(math.ceil((STEP * n + 6.0) / 7.0)) + 1):
        for iz in range(-1, 3):
            out.append("prop floor_01 %.2f 0 %.2f 0 1.6 0.40 0.41 0.43" % (7.0 * ix - 3.5, 7.0 * iz))
    out += S.lines
    fps, frames = 30, 240
    # the track says every light and beam the set has, each frame: once a track has a frame, its lights and beams are the set's
    out.append("track sets/everything.trk")
    film = bytearray()
    tx = [bx for kind, bx in S.beams if kind == "sweep"]
    tx = tx[0] if tx else STEP * n / 2.0
    rows = bytearray()
    stride = 4 * (3 + len(S.lights) * 6 + len(S.beams) * 13 + len(S.movers) * 15)
    for f in range(frames):
        u = f / frames
        rows += struct.pack("<3f", 0.36, 0.42, 0.54)
        for (lx, ly, lz, r, g, b) in S.lights:
            rows += struct.pack("<6f", lx, ly, lz, r, g, b)
        for kind, bx in S.beams:
            if kind == "sweep":
                a = 0.6 * math.sin(2.0 * math.pi * u)
                d = (math.sin(a), -0.92, 0.25 * math.cos(2.0 * math.pi * u))
                l = math.sqrt(sum(c * c for c in d))
                rows += struct.pack("<13f", bx - 2.0, 6.0, Z + 0.5, d[0] / l, d[1] / l, d[2] / l, 7.0, 0.12, 1.3, 1.0, 0.35, 0.85, 0.55)
            else:
                rows += struct.pack("<13f", bx + 2.6, 6.0, Z - 0.5, -0.10, -0.99, 0.05, 7.0, 0.12, 1.1, 0.3, 0.8, 1.0, 0.45)
        for kind, mx in S.movers:
            if kind == "orbit":
                a = 2.0 * math.pi * u
                rows += struct.pack("<15f", mx + 2.2 * math.cos(a), 1.2, Z + 1.2 * math.sin(a), math.degrees(-a), 0, 0, 1, 1, 1, 1, 1, 1, 0.0, 1.0, float((f // 30) % 4))
            else:
                rows += struct.pack("<15f", mx - 3.2, 1.0 + 0.6 * math.sin(4.0 * math.pi * u), Z - 1.5, 0, 0, 0, 1, 1 + 0.25 * math.sin(4.0 * math.pi * u), 1, 1.0, 0.3, 0.2, 0.5 + 0.5 * math.sin(2.0 * math.pi * u), 1.0, 1.0)
        # (step 25) the film cut: this frame's rows again, then the lens and the post's look. The lens tracks in across the timeline station and rolls over and back;
        # sepia comes up through the middle, a flash at half way dies over a third of a second, a burn eats in over the last second and a half; grain and a gate's weave throughout
        t = f / float(fps)
        e = 0.5 - 0.5 * math.cos(math.pi * u)
        sm = lambda a, b, x: (lambda k: k * k * (3.0 - 2.0 * k))(min(1.0, max(0.0, (x - a) / (b - a))))
        whip = 6.0 * (sm(5.0, 5.6, t) - sm(6.0, 6.5, t))                 # a whip pan out and back, for the shutter to smear
        cam = (tx - 6.0 + 9.5 * e, 2.3 - 0.6 * e, Z + 9.0 - 3.6 * e, tx + whip, 1.35, Z, 42.0 - 12.0 * e, 16.0 * math.sin(2.0 * math.pi * u), 1.0)
        weave = (0.0012 * math.sin(f * 2.399), 0.0016 * math.sin(f * 1.711 + 1.0))
        post = (-1.0, min(1.0, max(0.0, (u - 0.25) * 4.0)), max(0.0, 1.0 - (t - 4.0) / 0.33) if t >= 4.0 else 0.0, max(0.0, (t - 6.5) / 1.5), weave[0], weave[1], 0.55, -1.0, 1.0, 0.5, -1.0, -1.0)
        film += rows[len(rows) - stride:] + struct.pack("<9f", *cam) + struct.pack("<12f", *post)
    with open(os.path.join(NATIVE, "sets", "everything.trk"), "wb") as o:
        o.write(("TRK3 %g %d %d %d %d 0\n" % (fps, frames, len(S.lights), len(S.beams), len(S.movers))).encode())
        o.write(bytes(rows))
    with open(os.path.join(NATIVE, "sets", "everything.set"), "w", encoding="utf-8", newline="\n") as o:
        o.write("\n".join(out) + "\n")
    with open(os.path.join(NATIVE, "sets", "everything_film.trk"), "wb") as o:
        o.write(("TRK4 %g %d %d %d %d 0 1 1\n" % (fps, frames, len(S.lights), len(S.beams), len(S.movers))).encode())
        o.write(bytes(film))
    with open(os.path.join(NATIVE, "sets", "everything_film.set"), "w", encoding="utf-8", newline="\n") as o:
        o.write("\n".join(l.replace("track sets/everything.trk", "track sets/everything_film.trk").replace("everything.set --", "everything_film.set --") for l in out) + "\n")
    if getattr(S, "art_x", None) is not None: write_art(S.art_x, fps, frames)
    wide = ("wide", STEP * n / 2.0, "the whole row", "every station at once", (STEP * n / 2.0, 9.0, Z + 30.0, STEP * n / 2.0, 1.0, Z, 50))
    cams = ";".join("%s=%s" % (k, ",".join("%g" % v for v in cam)) for k, _cx, _w, _l, cam in [wide] + S.stations)
    with open(os.path.join(NATIVE, "sets", "everything.cams.txt"), "w", encoding="utf-8", newline="\n") as o:
        o.write(cams + "\n")
    return cams


def main(argv):
    if "--net" in argv:   # two machines, no window: nothing of the set is needed
        out = argv[argv.index("--net") + 1]
        subprocess.call([sys.executable, DRIVER, "buildsoft", "--game", "rally"])
        sys.exit(subprocess.call([sys.executable, os.path.join(NATIVE, "anim_bench", "netcheck.py"), "--pictures", out]))
    S, n = build()
    if "--list" in argv:
        for key, _cx, what, look, _cam in S.stations:
            print("%-10s %s\n%-10s look for: %s" % (key, what, "", look))
        return
    cams = write(S, n)
    print("sets/everything.set: %d stations (%s), %d lights, %d beams, %d movers" % (len(S.stations), ", ".join(k for k, *_r in S.stations), len(S.lights), len(S.beams), len(S.movers)))
    for l in S.left:
        print("  left out: %s" % l)
    if "--words" in argv:
        out = argv[argv.index("--words") + 1]
        opt = {argv[i]: argv[i + 1] for i in range(1, len(argv) - 1) if argv[i].startswith("--")}
        print("the anim lab, no set: the TV font's page of every script")
        subprocess.call([sys.executable, DRIVER, "lab", out, "--new", "--live", "--settle", "2.5", "--ctl", "face=0 grid=0 dqs=0 set=off words=1 lang=" + opt.get("--lang", "off")])
        subprocess.call([sys.executable, DRIVER, "down"])
        return
    if "--display" in argv:
        out = argv[argv.index("--display") + 1]
        opt = {argv[i]: argv[i + 1] for i in range(1, len(argv) - 1) if argv[i].startswith("--")}
        from PIL import Image, ImageDraw
        base, shots = out[:-4] if out.lower().endswith(".png") else out, []
        key = opt.get("--only", "bodies").split(",")[0]
        cam = next((c for k, _cx, _w, _l, c in S.stations if k == key), S.stations[0][4])
        print("the anim lab, the set `everything`, the %s station: a window for each display setting, one after another" % key)
        for name, gfx in (("as it is", ""), ("held to 4:3", "aspect=4:3"), ("a 480-line tube", "tube=1"), ("a 240-line tube", "tube=2"), ("put together over frames", "taa=1")):
            shot = "%s_%s.png" % (base, name.replace(" ", "_").replace(":", ""))
            cmd = [sys.executable, DRIVER, "lab", shot, "--new", "--live", "--settle", "3", "--env", "REVA_TIER=" + opt.get("--tier", "high"),
                   "--ctl", "face=0 grid=0 dqs=0 set=everything cam=" + ",".join("%g" % v for v in cam)]
            if gfx:
                cmd += ["--env", "REVA_GFX=" + gfx]
            subprocess.call(cmd)
            if os.path.exists(shot):
                shots.append((name, Image.open(shot).convert("RGB")))
        subprocess.call([sys.executable, DRIVER, "down"])
        if shots:
            w, h = 640, 360
            sheet = Image.new("RGB", (w * len(shots), h + 22), (20, 30, 60))
            d = ImageDraw.Draw(sheet)
            for k, (name, im) in enumerate(shots):
                sheet.paste(im.resize((w, h)), (k * w, 22))
                d.text((k * w + 6, 5), name, fill=(255, 255, 255))
            sheet.save(base + "_sheet.png")
            print("wrote %s_sheet.png   <-- LOOK AT IT" % base)
        return
    if "--film" in argv:
        out = argv[argv.index("--film") + 1]
        opt = {argv[i]: argv[i + 1] for i in range(1, len(argv) - 1) if argv[i].startswith("--")}
        from PIL import Image, ImageDraw
        base, shots = out[:-4] if out.lower().endswith(".png") else out, []
        print("the anim lab, the set `everything_film`: one window, its timeline held at five moments")
        for k, t in enumerate((0.4, 2.0, 4.05, 5.6, 7.6)):
            shot = "%s_%d.png" % (base, k)
            cmd = [sys.executable, DRIVER, "lab", shot, "--new", "--live", "--settle", "2.5", "--env", "REVA_TIER=" + opt.get("--tier", "high"), "--ctl", "face=0 grid=0 dqs=0 set=everything_film trackt=%.2f" % t]
            for i, a in enumerate(argv):
                if a == "--env":
                    cmd += ["--env", argv[i + 1]]
            subprocess.call(cmd)
            if os.path.exists(shot):
                shots.append((t, Image.open(shot).convert("RGB")))
        subprocess.call([sys.executable, DRIVER, "down"])
        if shots:
            w, h = shots[0][1].size
            w, h = w // 2, h // 2
            sheet = Image.new("RGB", (w * len(shots), h + 22), (20, 30, 60))
            d = ImageDraw.Draw(sheet)
            for k, (t, im) in enumerate(shots):
                sheet.paste(im.resize((w, h)), (k * w, 22))
                d.text((k * w + 6, 5), "%.2f s" % t, fill=(255, 255, 255))
            sheet.save(base + "_sheet.png")
            print("wrote %s_sheet.png   <-- LOOK AT IT" % base)
        return
    if "--shoot" in argv:
        out = argv[argv.index("--shoot") + 1]
        opt = {argv[i]: argv[i + 1] for i in range(1, len(argv) - 1) if argv[i].startswith("--")}
        if "--only" in opt:
            keep = set(opt["--only"].split(","))
            cams = ";".join(c for c in cams.split(";") if c.split("=")[0] in keep)
        soft = "--soft" in argv   # (step 27) the same stations through the engine's own rasterizer: 320 x 240 on the processor, and no window opened
        cmd = [sys.executable, DRIVER, "looks", out, "--soft" if soft else "--new", "--set", "everything", "--env", "REVA_TIER=" + opt.get("--tier", "pi" if soft else "high"), "--settle", "4", "--cams", cams]
        for i, a in enumerate(argv):
            if a == "--env":
                cmd += ["--env", argv[i + 1]]
        print("the anim lab, the set `everything`: " + ("the engine's own rasterizer, no window, a camera a station" if soft else "one window, a camera a station"))
        subprocess.call(cmd)
        subprocess.call([sys.executable, DRIVER, "down"])


if __name__ == "__main__":
    main(sys.argv)
