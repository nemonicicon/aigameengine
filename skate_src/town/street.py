#!/usr/bin/env python3
"""street.py -- generate a suburban street as a .set file.

    python skate_src/town/street.py reva_native/sets/block.set [--lots 5]

AssetsvilleTown is a KIT, not a set of buildings: it ships four actual houses and a
hundred wall, roof, door and window modules to make the rest out of. So a street is
generated, not placed by hand -- which is also what makes it a level format rather than
one drawing.

THE GRID, measured off the exported GLBs at club scale (1.75 units per metre):

    ground tiles   28 x 28,  origin at a corner: x 0..28,  z -28..0
    sidewalk        7 x 7,   origin at a corner: x 0..7,   z 0..7
    wall modules    7 wide along its own -Z, 5.25 tall, thin in X
    roof tiles      7 x 7,   CENTRED
    fence           7.3 long, 2.9 tall
    mailbox 2.5 tall · utility pole 14 · tree_02 21.8

Yaw follows the club's convention (forward is +z), so a wall placed at yaw=90 runs along
-X from its origin instead of -Z. That one fact is most of the house builder.

The street runs along +X. The road is one tile row centred on z=0; sidewalks, lawns and
houses go out to either side, and everything is expressed in whole modules so the pieces
actually meet.
"""
import argparse, random

TILE = 28.0     # ground tile
MOD = 7.0       # wall / roof / fence module
WALLH = 5.25    # wall height

# palette tints for the lawns, so a street is not one flat green
# ⛔ THE LAWN WAS A CHECKERBOARD FROM THE AIR. Four tints spanning +/-14% laid down as
# LAWN[(i + k) % 4] is periodic in BOTH axes by construction, which at street level reads as
# pleasant variation and from 190 units up reads as a chess board -- and the film's opening
# image is now an eagle-eye over the whole town.
# Two changes: the spread is compressed to about a third (still breaks the flatness of one
# flat green, no longer a pattern), and the choice is HASHED on the tile's position instead
# of counted, so it is aperiodic. Deterministic, so a town regenerates identically.
LAWN = [(0.288, 0.424, 0.188), (0.298, 0.434, 0.196),
        (0.282, 0.416, 0.182), (0.304, 0.440, 0.202)]


def lawn_of(ix, iz):
    """An aperiodic but deterministic tint index for a ground tile."""
    h = (ix * 73856093) ^ (iz * 19349663)
    return LAWN[(h >> 4) % len(LAWN)]


class Set:
    def __init__(self):
        self.rows = []
        self.solid = []      # (x, z, radius) of things that must not intersect

    def clear_spot(self, x, z, r):
        """⛔ NOTHING CHECKED THAT TWO PROPS WERE NOT IN THE SAME PLACE. Trees go down at
        lx+20 and driveway cars at lx+22 -- two units apart -- so the TRUNK stood inside
        the van, which is what jacob's screenshot caught. Push along X until clear; give
        up after a few tries rather than loop, because a crowded lot is better than a
        hang."""
        for _ in range(8):
            hit = None
            for (sx, sz, sr) in self.solid:
                if (x - sx) ** 2 + (z - sz) ** 2 < (r + sr) ** 2:
                    hit = (sx, sr)
                    break
            if hit is None:
                break
            x = hit[0] + (r + hit[1]) * (1.0 if x >= hit[0] else -1.0) * 1.05
        self.solid.append((x, z, r))
        return x

    def add(self, prop, x, y, z, yaw=0.0, sc=1.0, tint=None):
        r = "prop %-22s %8.2f %7.2f %9.2f %6.1f %5.3f" % (prop, x, y, z, yaw, sc)
        if tint:
            r += " %.3f %.3f %.3f" % tint
        self.rows.append(r)

    def write(self, path, header):
        with open(path, "w") as f:
            f.write(header)
            f.write("\n".join(self.rows) + "\n")
        print("%s: %d instances" % (path, len(self.rows)))


def house(S, hx, hz, wmod, dmod, face, rng, garage=True):
    """A box of wall modules with a door on the street side and a roof on top.

    `face` is +1 if the street is at -z from the house (north side of the road) and -1
    if it is at +z. The front wall gets the door; the wall behind the driveway gets the
    garage. Everything lands on the 7-unit grid, so the modules meet.
    """
    x0, x1 = hx, hx + wmod * MOD
    z0, z1 = hz, hz + dmod * MOD
    front = z0 if face > 0 else z1          # the side the street sees
    back = z1 if face > 0 else z0
    doori = wmod // 2
    gari = 0 if doori != 0 else wmod - 1

    # SIDING. One rpack carries one texture, so the colour of a house is which wall
    # rpack it is built from -- bake_town.sh bakes the module once per siding.
    sid = rng.choice(["", "_brown", "_grey", "_yellow", ""])
    W, WD, WG = "wall_01" + sid, "walldoor_01" + sid, "wallgaragedoor_01" + sid

    # front and back walls run along X -> yaw 90, each spanning x-7..x
    for i in range(wmod):
        xe = x0 + (i + 1) * MOD
        if i == doori:
            S.add(WD, xe, 0, front, 90)
        elif garage and i == gari and wmod > 1:
            S.add(WG, xe, 0, front, 90)
        else:
            # a wall with a window in it IS a wall module, same 7 x 5.25 footprint
            S.add(rng.choice(["window_01", "window_03", W]), xe, 0, front, 90)
        S.add(rng.choice(["window_01", W, W]), xe, 0, back, 90)
    # side walls run along Z -> yaw 0, each spanning z-7..z
    for j in range(dmod):
        ze = z0 + (j + 1) * MOD
        S.add(rng.choice(["window_05", W]), x0, 0, ze, 0)
        S.add(rng.choice(["window_05", W]), x1, 0, ze, 0)
    # GABLES: the attic wall is the triangle a pitched roof sits under, and even with a
    # flat deck on top it is what stops a house reading as a shoebox.
    for i in range(wmod):
        xe = x0 + (i + 1) * MOD
        S.add("wallattic_01", xe, WALLH, front, 90)
        S.add("wallattic_01", xe, WALLH, back, 90)
    for j in range(dmod):
        ze = z0 + (j + 1) * MOD
        S.add("wallattic_01", x0, WALLH, ze, 0)
        S.add("wallattic_01", x1, WALLH, ze, 0)
    # roof deck: centred tiles over every module.
    # ⛔ THE SHINGLE TINT STAYS, and the reason in the old note was wrong. It said roof_01
    # "UVs into a sheet we do not have"; ue_texmap.py says it UVs into the colour palette
    # like everything else. So the palette-flip fix was TRIED here -- and untinted the
    # roofs came back WHITE, which on white siding means a house with no roof at all.
    # Whatever swatch roof_01 lands on is not a shingle, so the tint is an art choice
    # rather than a workaround. Kept, with the right reason attached.
    shingle = rng.choice([(0.22, 0.20, 0.19), (0.26, 0.22, 0.18), (0.19, 0.19, 0.21)])
    for i in range(wmod):
        for j in range(dmod):
            S.add("roof_01", x0 + i * MOD + MOD / 2, WALLH + 3.68,
                  z0 + j * MOD + MOD / 2, 0, 1.0, shingle)
    # a chimney, and the drainpipes at the front corners
    S.add(rng.choice(["chimney_01", "chimney_02"]),
          x0 + MOD * 0.5, WALLH + 3.0, z0 + dmod * MOD * 0.5, 0, 1.0, (0.34, 0.24, 0.21))
    S.add("drainpipe", x0 + 0.2, 0, front + 0.3 * face, 0)
    S.add("drainpipe", x1 - 0.2, 0, front + 0.3 * face, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--lots", type=int, default=5)
    ap.add_argument("--seed", type=int, default=406)
    ap.add_argument("--skaters", type=int, default=6)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    S = Set()

    LOT = 3 * MOD + MOD          # 4 modules of frontage per lot = 28 = one ground tile
    length = a.lots * LOT

    # ---- ground: the road down the middle, lawn either side --------------------
    # ⛔ ONE TILE OF RUN-OFF EITHER END. The ground used to start at x=0, so a camera
    # placed a couple of units before the first lot -- which is exactly where you stand to
    # shoot down a street -- saw the void under the world.
    # ⛔ AND THE WORLD HAS TO REACH THE HORIZON. jacob: "i see an edge to this map that is
    # super close". One tile of run-off was not remotely enough -- the ground stopped a few
    # units past the last lot and the void started. Three tiles each way, four rows of lawn
    # each side, and a MOUNTAIN BACKDROP (2689 x 440 x 2510, baked and never once placed)
    # standing behind all of it so the eye never finds the seam.
    # ⛔ NOT THE MOUNTAINS. background_mountains_01 is a 2689-wide DOME, not a distant
    # ridge: dropped in at the street's centre it arches over the road like a cave roof
    # and the film is suddenly indoors. The ground itself is what has to reach the
    # horizon, so it does -- three tiles of run-off each end, four rows of lawn each side.
    ntile = int(length / TILE) + 1
    for i in range(-3, ntile + 3):
        x = i * TILE
        S.add("road_01", x, 0.00, TILE / 2)                 # z -14..14
        S.add("grass_00", x, -0.02, TILE * 1.5, 0, 1.0, lawn_of(i, 3))   # z 14..42
        S.add("grass_00", x, -0.02, -TILE * 0.5, 0, 1.0, lawn_of(i, -1))
        S.add("grass_00", x, -0.02, TILE * 2.5, 0, 1.0, lawn_of(i, 5))
        S.add("grass_00", x, -0.02, -TILE * 1.5, 0, 1.0, lawn_of(i, -3))
        S.add("grass_00", x, -0.02, TILE * 3.5, 0, 1.0, lawn_of(i, 7))
        S.add("grass_00", x, -0.02, -TILE * 2.5, 0, 1.0, lawn_of(i, -5))
    # sidewalks: 7x7, a kerb either side of the road
    for i in range(int(length / MOD) + 1):
        S.add("sidewalk_01", i * MOD, 0.01, 14.0)           # z 14..21
        S.add("sidewalk_01", i * MOD, 0.01, -21.0)          # z -21..-14

    # ---- the lots -------------------------------------------------------------
    for k in range(a.lots):
        lx = k * LOT
        for side in (+1, -1):
            hz = 26.0 if side > 0 else -26.0 - 2 * MOD
            wmod = rng.choice([3, 3, 4])
            dmod = rng.choice([2, 2, 3])
            house(S, lx + 2.0, hz, wmod, dmod, side, rng)

            # the yard, between the sidewalk and the house
            yz = 21.0 if side > 0 else -21.0
            mbx = S.clear_spot(lx + 3.0, yz + 1.5 * side, 1.1)
            S.add("mailbox_1" if k % 2 else "mailbox_2", mbx, 0, yz + 1.5 * side, 0 if side > 0 else 180)
            S.add("fence_101", lx + 1.0, 0, yz + MOD * side, 90 if side > 0 else 270)
            S.add("fence_101", lx + 1.0 + MOD, 0, yz + MOD * side, 90 if side > 0 else 270)
            # a trunk is about 1.2 across; a car is 10 long, so keep 5 clear of one
            if k % 2 == 0:
                tx = S.clear_spot(lx + 19.0, yz + 4.0 * side, 1.6)
                S.add("tree_02", tx, 0, yz + 4.0 * side, rng.uniform(0, 360), rng.uniform(0.8, 1.1))
            else:
                tx = S.clear_spot(lx + 18.0, yz + 3.0 * side, 1.6)
                S.add("tree_01", tx, 0, yz + 3.0 * side, rng.uniform(0, 360), rng.uniform(0.7, 1.0))
            # ⛔ BUSH_01 IS NOT A BUSH AT SCALE 1. Measured off its own rpack it is
            # 10.74 x 8.48 x 11.22 -- taller than a 5.25 wall module and 2.7x a person,
            # so at 1.0 every front garden grew a shrub the size of the house and ate
            # the shot it was dressing. A garden bush is knee-to-chest: ~2.0-2.8 units
            # against the mailbox's measured 2.49.
            for bx, bz in ((6.0, 2.0), (9.0, 2.2)):
                bxx = S.clear_spot(lx + bx, yz + bz * side, 1.5)
                S.add("bush_01", bxx, 0, yz + bz * side,
                      rng.uniform(0, 360), rng.uniform(0.24, 0.33))
            # a car on the driveway, parked nose-in
            if rng.random() < 0.7:
                # ⛔ ONLY THE TWO THAT RENDER. The kit's vehicles are the only SKELETAL
                # meshes in it and two of the four still land on the colour palette's
                # LABELLED SWATCH CHART -- measured, SK_veh_Sedan_01's u runs -1.538..0.955
                # where the mailbox's runs 0.217..0.782. Switching the sampler from
                # CLAMP to REPEAT fixed the van and the pickup (clamping pinned every
                # negative coordinate to u=0, which is exactly where that atlas keeps its
                # legend) and did NOT fix the sedan or the SUV, whose layout must want a
                # sheet the export never found. Until it is, a driveway gets a van or a
                # pickup; a car wearing a picture of a colour chart was parked beside the
                # porch in every act III close-up.
                v = rng.choice(["veh_pickup_01", "veh_van_01"])
                # ⛔ THE DRIVEWAY IS BETWEEN THE SIDEWALK AND THE HOUSE, NOT INSIDE IT.
                # `yz + 5*side` is z = -26 on the near side, which is EXACTLY the front
                # wall line -- every parked car has been embedded in a house since the
                # street was first generated. It never showed because no camera got close
                # to the near side until the leash scene put one on the verge.
                # ⭐ AT THE KERB, ALONG THE STREET. The road spans z -14..14, so a parked
                # car sits just outside it and points down the street rather than at the
                # house. (It was on the driveway; before that it was at z=-26, which is
                # the front WALL line -- every car in the film was inside a house.)
                # ⛔ THE KERB IS THE ROAD EDGE, NOT THE FAR SIDE OF THE PAVEMENT. Measured:
                # the road spans z -14..14 and the sidewalk 14..21, so z=16.5 parked every
                # car ON THE PAVEMENT -- reva walked straight through a van. A car at the
                # kerb sits just INSIDE the road: 11.4 puts a 4.1-wide body's outer edge at
                # ~13.5, a hand's width off the kerb.
                # ⛔ A VEHICLE IS LONG IN X; A WALL IS LONG IN Z (measured 2026-09-03:
                # pickup 9.97 x 4.12, van 9.06 x 4.50, wall_01 0.53 x 7.00). This used the
                # WALL convention -- "yaw 90 runs along X" -- on a body that already ran
                # along X, which turned every parked car ACROSS the road. jacob: "the
                # vehicles need to be parallel to the curb not perpendicular."
                dz = 11.4 * side
                vx = S.clear_spot(lx + 20.0 + rng.uniform(-2.0, 2.0), dz, 5.6)
                S.add(v, vx, 0, dz, 0 if side > 0 else 180)
            # dressing
            if rng.random() < 0.5:
                tbx = S.clear_spot(lx + 25.0, yz + 1.0 * side, 1.1)
                S.add("trash_bin_1", tbx, 0, yz + 1.0 * side, rng.uniform(0, 360))
            if rng.random() < 0.4:
                bcx = S.clear_spot(lx + 12.0, yz + 2.0 * side, 1.4)
                S.add("bicycle_01", bcx, 0, yz + 2.0 * side, rng.uniform(0, 360))

        # power runs down the north verge, and a hydrant every other lot
        px = S.clear_spot(lx + 14.0, 22.5, 1.4)
        S.add("utility_pole_5", px, 0, 22.5)
        if k % 2 == 0:
            hyx = S.clear_spot(lx + 8.0, -22.5, 0.9)
            S.add("hydrant", hyx, 0, -22.5)

    # ---- THE SKATE CREW -------------------------------------------------------
    # jacob: "add a punk influence, the gritty neighborhood kind, not the city fashion
    # punks... zombies vs neighbors is punk in its character."
    #
    # There are no punks in this roster -- it is a town pack, all uniforms and business
    # casual. So the read is built out of three things the assets CAN do: the darkest,
    # most worn bodies in the roster (measured off their own textures, not guessed at),
    # the grit that collects around where they hang out, and the fact that they are the
    # only people on this street doing anything but chores.
    CREW = ["v1_Character_30_Female", "v1_Character_Killer_02", "v1_Character_32",
            "v1_Character_Killer_04", "v1_Character_Female_04", "v1_crowd_bgirl"]
    for i in range(a.skaters):
        body = CREW[i % len(CREW)]
        lane = -8.0 + (i % 4) * 5.0            # spread across the road, not a chorus line
        back = (i % 3 == 2)                    # a couple of them rolling the other way
        yaw = 270.0 if back else 90.0
        x0 = (length - 8.0) if back else 8.0
        S.rows.append("actor %-26s %-10s %8.2f %6.2f %8.2f %6.1f %5.3f %5.2f %6.2f %7.2f %s"
                      % (body, "m_skate", x0, 0.0, lane, yaw, 1.0,
                         rng.uniform(0.85, 1.15), rng.uniform(7.0, 13.0),
                         length - 16.0, "v1_board"))

    # ---- THE GRIT. Punk is a place before it is a person. ----------------------
    gx = (a.lots - 1) * LOT + 4.0              # the last lot on the block is the rough one
    for p, dx, dz, yw in (("wreck_01", 2.0, 30.0, 24.0), ("barrel_1", 10.0, 26.0, 0.0),
                          ("barrel_2", 12.2, 27.4, 40.0), ("trash_bag", 8.4, 24.0, 0.0),
                          ("trash", 14.0, 25.0, 15.0), ("crate_1", 17.0, 26.0, 70.0),
                          ("woodenshack_01", 24.0, 34.0, 0.0), ("tireset_01", 6.0, 28.5, 0.0),
                          ("ladder_01", 20.0, 24.0, 90.0), ("box", 15.5, 28.6, 30.0)):
        S.add(p, gx + dx, 0, dz, yw)
    # boards and bottles where they sit
    for dx, dz, yw in ((3.0, -24.0, 20.0), (5.2, -25.4, 75.0)):
        S.add("bicycle_01", gx + dx, 0, dz, yw)
    for dx, dz in ((9.0, -23.4), (10.1, -23.9), (11.0, -24.4)):
        S.add("bottle_1", gx + dx, 0, dz, 0)

    S.write(a.out,
            "# %s -- generated by skate_src/town/street.py (seed %d, %d lots)\n"
            "# DO NOT HAND-EDIT: regenerate. The grid is 7-unit modules on 28-unit tiles.\n\n"
            % (a.out.split("/")[-1], a.seed, a.lots))


if __name__ == "__main__":
    main()
