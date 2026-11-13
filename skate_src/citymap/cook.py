"""cook.py -- the city cook's steps, in the order the street game's map plan gives them:
true size, ground and streets, bridges from the tags, lanes and rails, buildings, cells. citymap.py is the command; README.md says what's what."""
import heapq
import json
import math
import os
import random
import re
import struct
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from rpackwrite import rigid_skin, to_png, write_pack   # noqa: E402

import geo   # noqa: E402
import layers   # noqa: E402
import shapes as S   # noqa: E402

# ---------------------------------------------------------------- the style a config starts from. Everything here can be said again in the config's "style"
STYLE = {
    "units_per_metre": 1.75,
    "cell_m": 64.0,
    "margin_m": 160.0,          # the field and the ways are read this far past the box, so what crosses its edge has heights on both sides
    "ground_step_m": 2.0,       # the ground's mesh: a vertex this often
    "far_step_m": 8.0,          # the far stand-in's ground
    "far_buildings": 40,        # the tallest this many of each cell's buildings go in its stand-in
    "streaming": [280.0, 340.0],
    "far_distance": 3000.0,
    "far_plane": 3400.0,
    "grade_smooth_m": 12.0,     # a road's height is the elevation's mean over this far either way along it
    "road_blend_m": 4.0,        # past a road's edge the ground goes back to the elevation over this far
    "path_blend_m": 1.5,
    "kerb_m": 0.5,              # the flat past a road's edge before the ground starts back
    "lane_m": 3.3,              # a lane, when a way says how many
    "widths_m": {               # curb to curb when a way says neither its width nor its lanes
        "motorway": 11.0, "motorway_link": 5.5, "trunk": 11.0, "trunk_link": 5.5, "primary": 13.0, "primary_link": 6.0,
        "secondary": 11.0, "secondary_link": 6.0, "tertiary": 10.0, "tertiary_link": 6.0, "unclassified": 8.0, "residential": 8.0,
        "living_street": 6.0, "service": 4.5, "busway": 7.0, "road": 7.0, "track": 3.5,
        "footway": 1.8, "sidewalk": 2.0, "crossing": 3.0, "path": 1.5, "steps": 2.0, "pedestrian": 5.0, "cycleway": 2.5, "bridleway": 2.0,
    },
    "sidewalk_default": ["primary", "secondary", "tertiary", "unclassified", "residential", "living_street", "primary_link", "secondary_link", "tertiary_link"],
    "sidewalk_m": 2.0,
    "curb_m": 0.15,             # a sidewalk stands this far above the road
    "bike_lane_m": 1.8,
    "post_every_m": 6.0,        # a protected lane's posts
    "post_m": [0.12, 0.9],      # their width and height
    "rail_gauge_m": 1.435,
    "rail_m": 0.08,
    "deck_m": 1.4,              # a bridge's deck, top to underside
    "parapet_m": [0.3, 1.1],    # its low wall: thickness, height
    # (11-06) the piers that hold a deck up (piers()): a bent of round columns across the deck, standing just past each way it crosses on the ground and
    # every pier_every_m between. 0 is none
    "pier_every_m": 30.0,
    "pier_m": [1.2, 1.2, 1.4],  # a column's diameter; the cap beam under the deck, its depth and its thickness along the bridge
    "pier_across_m": 7.0,       # a column for about this much of the deck's width
    "pier_clear_m": 2.6,        # no column where the deck's underside is less than this over the ground (towards a bridge's ends)
    "pier_side_m": 2.0,         # how far past a crossed way's edge (its sidewalk too) the bents stand
    "building_default_m": 8.0,  # a building that says neither its height nor its floors
    "facades": 1,               # (11-06) walls coloured by what a building is, windows, shopfronts, parapets, rooftop units, pitched roofs on houses; 0 is plain boxes
    # (11-06) planting (planting()): the map's trees, tree rows, woods and scrub; parks and lawns scattered; grass steeper than steep_grass wooded. 0 is none.
    # each: [spacing of a jittered grid in metres, the share of its points that get something]
    "trees": 1,
    "plant_wood": [5.5, 0.8], "plant_scrub": [3.0, 0.7], "plant_steep": [6.0, 0.8], "plant_park": [16.0, 0.5], "plant_lawn": [24.0, 0.35],
    "steep_grass": 0.35,
    "street": 1,                # (11-06) street furniture and markings (street()): 0 is none
    "lamp_every_m": 35.0,       # a lamp along an arterial the map gives none on, this far apart, sides taking turns
    "utility_every_m": 38.0,    # (11-06) a wooden utility pole this far apart along a street, its wires pole to pole; 0 is none
    "car_every_m": 6.4,         # (11-06) a parked car's space along a curb where parking's allowed; 0 is none
    "car_share": 0.62,          # of the spaces, the share with a car in
    "lot_car_share": 0.55,      # (11-06) of a surface lot's stalls, along its mapped parking aisles, the share with a car in; 0 is none
    # (11-06) a building that says neither its height nor its floors, by its kind: [floors, metres a floor]. Any other kind is building_default_m
    "default_levels": {"retail": [1, 4.6], "kiosk": [1, 3.2], "supermarket": [1, 5.5], "garage": [1, 3.0], "garages": [1, 3.0], "shed": [1, 2.6], "carport": [1, 2.6]},
    # (11-06) a flag on a flagpole by its flag:wikidata: its bands top to bottom, each a colour and its share of the height. Q1740429, the yellow flag
    # with three red stripes that Little Saigon flies
    "flags": {"Q1740429": [["f4c430", 5], ["da251d", 1], ["f4c430", 1], ["da251d", 1], ["f4c430", 1], ["da251d", 1], ["f4c430", 5]]},
    "joint_pole_m": 6.0,        # (11-06) a trolley span wire hangs from a pole that's already standing within this, before a new one is put up
    # (11-06) a see-through fence's texture: metres one tile of it covers along and up (8 diamonds across a chain-link tile: a real chain link's 5 cm mesh)
    "chainlink_tile_m": 0.55, "bars_tile_m": 0.24,
    "weave_blend": 1,           # (11-06) the weave drawn blended by its texture's alpha (a veil at a distance), not cut out (gone, or a checker, at a distance)
    "level_m": 3.2,
    "shop_floor_m": 4.6,        # (11-06) a shop's ground floor, when the map gives a shop building's floors and not its height
    "shopfronts": 1,            # (11-06) shopfronts on the walls that face a street, in bays with signs (shopfronts()); 0 is the old glass on every wall
    "shopfront_reach_m": 28.0,  # (11-06) a wall is a shop's front when a street runs along it within this in front of it
    "lift": {"road": 0.03, "path": 0.03, "lane": 0.05, "rail": 0.06, "crossing": 0.035},
    # what the ground is that no way covers (cover()): OpenStreetMap's areas say grass or paved, and where they say neither, the ground is paved
    # within pave_gap_m of a road's edge (its sidewalk included) in a built-up block (within built_up_m of a building), and within building_apron_m
    # of any building. The rest is grass
    "pave_gap_m": 4.0,
    "built_up_m": 40.0,
    "building_apron_m": 2.0,
    # the kinds of surface the set names (its `surface` lines, engine/spans.inc), each a kind and the words of the meshes that are it. The order is
    # the kinds' numbers, from 1
    "surfaces": [["road", ["road", "concrete"]], ["lane", ["lane"]], ["track", ["track"]], ["sidewalk", ["sidewalk", "platform"]], ["crossing", ["crossing"]],
                 ["rail", ["rail"]], ["grass", ["ground"]], ["paved", ["paved"]], ["path", ["path"]], ["deck", ["bridge", "deck"]], ["curb", ["curb"]],
                 ["wall", ["parapet", "post", "pier", "pole", "fence", "rail", "upole", "car", "tyre", "shelter", "hydrant", "flag", "steel"]], ["building", ["roof", "bldg0", "bldg1", "bldg2", "bldg3", "bldg4", "bldg5", "wall", "glass", "trim", "awning", "unit", "shingle"]]],
    "colours": {
        "ground": "6b7a4c", "paved": "8f8c85", "bridge": "8c8a85", "road": "3d3e42", "concrete": "8c8a85", "path": "a29d92", "sidewalk": "b5b0a6", "crossing": "d8d6cc",
        "curb": "9a968e", "pier": "a19c93", "lane": "3f9a55", "track": "2b8a72", "post": "e8e8e2", "rail": "6e6158", "deck": "9b968c", "parapet": "aaa59a",
        "roof": "6f6a66", "buildings": ["c9b79c", "a8b2b8", "b98f76", "d8d2c4", "8f9a8a", "b7a4b5"],
    },
    "budget": {"tris": 40000, "bytes": 3000000},
}

ROADS = {"motorway", "motorway_link", "trunk", "trunk_link", "primary", "primary_link", "secondary", "secondary_link", "tertiary", "tertiary_link",
         "unclassified", "residential", "living_street", "service", "busway", "road", "track"}
PATHS = {"footway", "path", "steps", "pedestrian", "cycleway", "bridleway"}
GRAPH = {"primary", "secondary", "tertiary", "unclassified", "residential", "living_street", "primary_link", "secondary_link", "tertiary_link", "trunk", "trunk_link"}


def merged(base, over):
    out = dict(base)
    for k, v in (over or {}).items():
        out[k] = merged(base[k], v) if isinstance(v, dict) and isinstance(base.get(k), dict) else v
    return out


def metres(s):
    """a height or a width as OSM writes them: 12, 12 m, 40', 40 ft, 12;14. None if it isn't one"""
    if s is None:
        return None
    m = re.match(r"\s*(-?\d+(?:\.\d+)?)\s*(m|ft|'|feet)?", str(s))
    if not m:
        return None
    v = float(m.group(1))
    if m.group(2) in ("ft", "'", "feet"):
        v *= 0.3048
    return v


def rgb(hexs):
    return hexs.lstrip("#").lower()


def log(msg):
    print(msg)
    sys.stdout.flush()


class Cook:
    def __init__(self, cfg, raw, out):
        self.cfg = cfg
        self.raw = raw
        self.out = out
        self.st = merged(STYLE, cfg.get("style"))
        self.U = float(self.st["units_per_metre"])
        self.times = []
        self.t0 = time.time()
        self.report = []

    def lap(self, what):
        t = time.time()
        self.times.append((what, t - self.t0))
        log("[cook] %-34s %6.2f s" % (what, t - self.t0))
        self.t0 = t

    def say(self, line):
        self.report.append(line)
        log(line)

    # ------------------------------------------------------------ 1. true size
    def read(self):
        cfg = self.cfg
        osm_path = os.path.join(self.raw, cfg["osm"])
        d = json.load(open(osm_path, encoding="utf-8"))
        els = d["elements"]
        nid = np.array([e["id"] for e in els if e["type"] == "node"], dtype=np.int64)
        lat = np.array([e["lat"] for e in els if e["type"] == "node"])
        lon = np.array([e["lon"] for e in els if e["type"] == "node"])
        self.ntags = {e["id"]: e["tags"] for e in els if e["type"] == "node" and e.get("tags")}
        self.ways = {e["id"]: e for e in els if e["type"] == "way"}
        self.rels = [e for e in els if e["type"] == "relation"]
        # (11-06) corrections to the map's tags, each with where the fact came from (the config's "tags": {"way/906291534": {"building:levels": "7",
        # "_source": "..."}}): what the map leaves out that a source says, put in before anything reads the tags
        fixed = []
        for ref, tg in sorted((cfg.get("tags") or {}).items()):
            kind, _, num = ref.partition("/")
            add = {k: str(v) for k, v in tg.items() if not k.startswith("_")}
            if kind == "way" and num.isdigit() and int(num) in self.ways:
                self.ways[int(num)].setdefault("tags", {}).update(add)
            elif kind == "node" and num.isdigit():
                self.ntags.setdefault(int(num), {}).update(add)
            else:
                continue
            fixed.append(ref)
        if fixed:
            self.say("the config corrects the map's tags on %s" % ", ".join(fixed))
        o = cfg["origin"]
        if "node" in o:
            k = int(np.nonzero(nid == o["node"])[0][0])
            olat, olon = lat[k], lon[k]
        else:
            olat, olon = o["lat"], o["lon"]
        self.zone = int(cfg.get("utm_zone", 10))
        self.E0, self.N0 = geo.utm(olat, olon, self.zone)
        E, N = geo.utm(lat, lon, self.zone)
        self.ne = dict(zip(nid.tolist(), (E - self.E0).tolist()))
        self.nn = dict(zip(nid.tolist(), (N - self.N0).tolist()))
        self.say("origin: %.7f, %.7f -> UTM %d easting %.2f northing %.2f" % (olat, olon, self.zone, self.E0, self.N0))
        # the box, in metres from the origin, snapped out to whole cells
        a = cfg["area"]
        C = float(self.st["cell_m"])
        if "south" in a:
            es, ns = geo.utm(np.array([a["south"], a["south"], a["north"], a["north"]]), np.array([a["west"], a["east"], a["west"], a["east"]]), self.zone)
            e0, e1, n0, n1 = es.min() - self.E0, es.max() - self.E0, ns.min() - self.N0, ns.max() - self.N0
        else:
            e0, e1 = a["east_m"]
            n0, n1 = a["north_m"]
        self.ni = int(math.ceil((e1 - e0) / C))
        self.nj = int(math.ceil((n1 - n0) / C))
        self.be0, self.bn0 = float(math.floor(e0)), float(math.floor(n0))
        self.be1, self.bn1 = self.be0 + self.ni * C, self.bn0 + self.nj * C
        self.say("box: east %.0f..%.0f m, north %.0f..%.0f m from the origin: %d x %d cells of %.0f m (%.0f x %.0f m)" % (
            self.be0, self.be1, self.bn0, self.bn1, self.ni, self.nj, C, self.be1 - self.be0, self.bn1 - self.bn0))
        self.lap("read the map")
        # the ground
        M = float(self.st["margin_m"])
        self.fe0, self.fn0 = self.be0 - M, self.bn0 - M
        dem = geo.Dem([os.path.join(self.raw, p) for p in cfg["dem"]])
        es, ns, z = dem.window(self.E0 + self.fe0, self.N0 + self.fn0, self.E0 + self.be1 + M, self.N0 + self.bn1 + M, 1.0)
        holes = geo.fill_holes(z)
        self.say("elevation: %d x %d at 1 m, %.2f .. %.2f m, %d pixels with no tile filled from their neighbours" % (z.shape[1], z.shape[0], np.nanmin(z), np.nanmax(z), holes))
        self.dem = S.Field(z.copy(), self.fe0, self.fn0, 1.0)
        self.field = S.Field(z, self.fe0, self.fn0, 1.0)
        self.lap("read the elevation")

    def pts(self, w):
        ids = [n for n in w["nodes"] if n in self.ne]
        return ids, np.array([[self.ne[n], self.nn[n]] for n in ids]) if ids else np.zeros((0, 2))

    # ------------------------------------------------------------ what each way is
    def classify(self):
        st = self.st
        fe1, fn1 = self.be1 + st["margin_m"], self.bn1 + st["margin_m"]
        lines, buildings, parts = [], [], []
        for wid in sorted(self.ways):
            w = self.ways[wid]
            t = w.get("tags", {})
            ids, P = self.pts(w)
            if len(P) < 2:
                continue
            if P[:, 0].max() < self.fe0 or P[:, 0].min() > fe1 or P[:, 1].max() < self.fn0 or P[:, 1].min() > fn1:
                continue
            if "building" in t or "building:part" in t:
                if ids[0] == ids[-1] and len(ids) >= 4:
                    (parts if "building:part" in t and "building" not in t else buildings).append((wid, t, S.ring_clean(P)))
                continue
            hw, rw = t.get("highway"), t.get("railway")
            if t.get("tunnel") not in (None, "no") or t.get("location") == "underground":
                continue
            kind = None
            if hw in ROADS:
                kind = "road"
            elif hw in PATHS:
                if t.get("area") == "yes" or (hw == "pedestrian" and ids[0] == ids[-1]):
                    continue
                kind = "path"
            elif rw == "tram":
                kind = "rail"
            if not kind:
                continue
            lines.append({"id": wid, "t": t, "ids": ids, "P": P, "kind": kind, "hw": hw, "bridge": t.get("bridge") not in (None, "no")})
        # buildings that are relations: their outer rings, joined, with the relation's tags
        for r in sorted(self.rels, key=lambda r: r["id"]):
            t = r.get("tags", {})
            if t.get("type") != "multipolygon" or "building" not in t:
                continue
            for ring in self.rings([m["ref"] for m in r.get("members", []) if m.get("type") == "way" and m.get("role") == "outer"]):
                P = np.array([[self.ne[n], self.nn[n]] for n in ring if n in self.ne])
                if len(P) >= 4 and P[:, 0].max() >= self.fe0 and P[:, 0].min() <= fe1 and P[:, 1].max() >= self.fn0 and P[:, 1].min() <= fn1:
                    buildings.append((-r["id"], t, S.ring_clean(P)))
        self.lines, self.buildings, self.parts = lines, buildings, parts
        self.say("ways in the box and its margin: %d roads, %d paths, %d tram tracks (%d of them bridges); %d buildings, %d building parts" % (
            sum(1 for l in lines if l["kind"] == "road"), sum(1 for l in lines if l["kind"] == "path"), sum(1 for l in lines if l["kind"] == "rail"),
            sum(1 for l in lines if l["bridge"]), len(buildings), len(parts)))
        self.lap("sorted the ways")

    def rings(self, wids):
        segs = [list(self.ways[w]["nodes"]) for w in wids if w in self.ways]
        out = []
        while segs:
            ring = segs.pop(0)
            grew = True
            while ring[0] != ring[-1] and grew:
                grew = False
                for k, s in enumerate(segs):
                    if s[0] == ring[-1]:
                        ring += s[1:]
                    elif s[-1] == ring[-1]:
                        ring += s[::-1][1:]
                    else:
                        continue
                    segs.pop(k)
                    grew = True
                    break
            if ring[0] == ring[-1]:
                out.append(ring)
        return out

    # ------------------------------------------------------------ 2. ground and streets
    def width(self, l):
        t, st = l["t"], self.st
        w = metres(t.get("width"))
        if w and w > 0.5:
            return w, "its width tag"
        hw = l["hw"]
        if l["kind"] == "rail":
            return st["rail_gauge_m"] + 0.6, "a tram's"
        if hw == "footway" and t.get("footway") == "sidewalk":
            return st["widths_m"]["sidewalk"], "a sidewalk's"
        if hw == "footway" and t.get("footway") == "crossing":
            return st["widths_m"]["crossing"], "a crossing's"
        lanes = metres(t.get("lanes"))
        if l["kind"] == "road" and lanes and lanes >= 1:
            return lanes * st["lane_m"], "its lanes"
        return st["widths_m"].get(hw, 6.0), "its class"

    def sidewalks(self, l):
        """which sides get a sidewalk drawn with the road: what OSM says, or the class's default. 'separate' means it's its own way"""
        t = l["t"]
        if l["kind"] != "road":
            return ()
        sides = []
        for side in ("left", "right"):
            v = t.get("sidewalk:" + side) or t.get("sidewalk:both")
            if v is None:
                g = t.get("sidewalk")
                v = "yes" if g in ("both", side) else ("no" if g in ("left", "right", "no", "none", "separate") else None)
            if v is None:
                v = "yes" if l["hw"] in self.st["sidewalk_default"] else "no"
            if v in ("yes", "both"):
                sides.append(side)
        return tuple(sides)

    def profiles(self):
        """every way made dense, and each that's on the ground given a height along it: the elevation's mean over grade_smooth_m either side"""
        widths = {}
        for l in self.lines:
            l["w"], why = self.width(l)
            widths[why] = widths.get(why, 0) + 1
            l["walks"] = self.sidewalks(l)
            C, at = S.densify(l["P"], 1.0)
            l["C"], l["at"] = C, at
            if not l["bridge"]:
                s = S.arclen(C)
                l["hc"] = S.smooth(self.dem.sample(C[:, 0], C[:, 1]), s, self.st["grade_smooth_m"])
        self.say("widths from: " + ", ".join("%s %d" % kv for kv in sorted(widths.items())))
        self.lap("laid the ways' grades")

    def carve(self):
        """each way on the ground pressed into the field: flat across, at its grade along, and back to the elevation past its edge. Where two meet,
        a pixel goes to the one it's furthest inside"""
        st = self.st
        z = self.field.z
        best = np.full(z.shape, np.inf)
        hgt = np.zeros(z.shape)
        blend = np.ones(z.shape)
        nseg = 0
        for l in self.lines:
            if l["bridge"] or l["kind"] == "rail":
                continue
            road = l["kind"] == "road"
            core = l["w"] / 2 + (st["kerb_m"] if road else 0.2) + (st["sidewalk_m"] if l["walks"] else 0.0)
            bl = st["road_blend_m"] if road else st["path_blend_m"]
            C, hc = l["C"], l["hc"]
            keep = np.unique(np.concatenate([np.arange(0, len(C), 4), [len(C) - 1]]))
            C, hc = C[keep], hc[keep]
            reach = core + bl
            for k in range(len(C) - 1):
                (ax, ay), (bx, by) = C[k], C[k + 1]
                c0 = int(math.floor((min(ax, bx) - reach - self.fe0))) - 1
                c1 = int(math.ceil((max(ax, bx) + reach - self.fe0))) + 1
                r0 = int(math.floor((min(ay, by) - reach - self.fn0))) - 1
                r1 = int(math.ceil((max(ay, by) + reach - self.fn0))) + 1
                c0, r0 = max(c0, 0), max(r0, 0)
                c1, r1 = min(c1, z.shape[1] - 1), min(r1, z.shape[0] - 1)
                if c1 < c0 or r1 < r0:
                    continue
                px = self.fe0 + np.arange(c0, c1 + 1)[None, :]
                py = self.fn0 + np.arange(r0, r1 + 1)[:, None]
                dx, dy = bx - ax, by - ay
                L2 = max(dx * dx + dy * dy, 1e-12)
                t = np.clip(((px - ax) * dx + (py - ay) * dy) / L2, 0, 1)
                d = np.hypot(px - (ax + t * dx), py - (ay + t * dy))
                s = d - core
                win = (s < best[r0:r1 + 1, c0:c1 + 1]) & (d < reach)
                best[r0:r1 + 1, c0:c1 + 1][win] = s[win]
                hgt[r0:r1 + 1, c0:c1 + 1][win] = (hc[k] + (hc[k + 1] - hc[k]) * t)[win]
                blend[r0:r1 + 1, c0:c1 + 1][win] = bl
                nseg += 1
        inn = best <= 0
        mid = (best > 0) & np.isfinite(best)
        w = np.clip(best[mid] / blend[mid], 0, 1)
        w = w * w * (3 - 2 * w)
        z[mid] = hgt[mid] * (1 - w) + z[mid] * w
        # where two streets meet, a pixel goes to one or the other, and their grades, each the mean along its own line, can differ by a hand's width there:
        # a crease a wheel feels as a slope on a flat corner. The streets' own pixels are smoothed among themselves (a box of street_smooth_m, twice),
        # which keeps a steady grade as it is and takes the step out
        r = int(self.st.get("street_smooth_m", 3))
        hs = hgt.copy()
        if r > 0:
            m = inn.astype(np.float64)

            def box(a):
                c = np.cumsum(np.cumsum(np.pad(a, ((r + 1, r), (r + 1, r))), axis=0), axis=1)
                return c[2 * r + 1:, 2 * r + 1:] - c[:-2 * r - 1, 2 * r + 1:] - c[2 * r + 1:, :-2 * r - 1] + c[:-2 * r - 1, :-2 * r - 1]
            for _ in range(2):
                den = box(m)
                hs = np.where(inn, box(hs * m) / np.maximum(den, 1e-9), hs)
        z[inn] = hs[inn]
        # the box's lowest ground is y 0: the walked world always has a floor there, and it mustn't poke through a hill
        c0, c1 = int(self.be0 - self.fe0), int(self.be1 - self.fe0)
        r0, r1 = int(self.bn0 - self.fn0), int(self.bn1 - self.fn0)
        self.floor = float(z[r0:r1 + 1, c0:c1 + 1].min())
        self.say("streets pressed into the ground: %d pieces; %.1f%% of the field is street or path; the lowest ground in the box is %.2f m, which is y 0" % (
            nseg, 100.0 * inn.mean(), self.floor))
        self.lap("pressed the streets in")

    # ------------------------------------------------------------ 2b. what the ground is where no way is: grass or paved
    GRASS = {"landuse": {"grass", "flowerbed", "recreation_ground", "meadow", "village_green", "allotments", "cemetery", "forest", "orchard", "greenfield"},
             "leisure": {"park", "garden", "golf_course", "dog_park", "nature_reserve"},
             "natural": {"wood", "scrub", "grassland", "shrubbery", "heath", "tree_row"}}
    PAVED = {"landuse": {"retail", "commercial", "industrial", "construction", "railway", "highway", "garages", "depot"},
             "amenity": {"parking", "parking_space", "fuel", "bicycle_parking", "marketplace", "motorcycle_parking", "car_wash"},
             "leisure": {"outdoor_seating", "playground", "track"}, "place": {"square", "plaza"}, "man_made": {"pier"}}
    SURF_GRASS = {"grass", "dirt", "ground", "earth", "mud", "artificial_turf", "woodchips", "sand"}
    SURF_PAVED = {"asphalt", "concrete", "paved", "paving_stones", "sett", "concrete:plates", "compacted", "fine_gravel", "gravel", "bricks", "rubber", "tartan"}

    def cover_class(self, t):
        """1 grass, 2 paved, 3 a surface parking lot (asphalt, 11-06), 0 says nothing. A surface tag beats what the area is"""
        s = t.get("surface")
        if t.get("amenity") in ("parking", "parking_space") and t.get("parking", "surface") == "surface" and s not in self.SURF_GRASS and s not in (
                "gravel", "fine_gravel", "compacted", "paving_stones", "concrete", "concrete:plates"):
            return 3
        if s in self.SURF_PAVED:
            return 2
        if s in self.SURF_GRASS:
            return 1
        if "area:highway" in t or (t.get("highway") in ("pedestrian", "footway", "service") and t.get("area") == "yes"):
            return 2
        if t.get("leisure") == "pitch":
            return 1
        for k, vs in self.PAVED.items():
            if t.get(k) in vs:
                return 2
        for k, vs in self.GRASS.items():
            if t.get(k) in vs:
                return 1
        return 0

    def raster(self, P):
        """the field's pixels inside ring P: (rows, cols) slices and the mask over them, or None"""
        z = self.field.z
        lo, hi = P.min(axis=0), P.max(axis=0)
        c0, c1 = max(int(math.floor(lo[0] - self.fe0)), 0), min(int(math.ceil(hi[0] - self.fe0)), z.shape[1] - 1)
        r0, r1 = max(int(math.floor(lo[1] - self.fn0)), 0), min(int(math.ceil(hi[1] - self.fn0)), z.shape[0] - 1)
        if c1 < c0 or r1 < r0:
            return None
        px = self.fe0 + np.arange(c0, c1 + 1)[None, :] + np.zeros((r1 - r0 + 1, 1))
        py = self.fn0 + np.arange(r0, r1 + 1)[:, None] + np.zeros((1, c1 - c0 + 1))
        return slice(r0, r1 + 1), slice(c0, c1 + 1), S.inside(px, py, P)

    @staticmethod
    def grow(mask, r):
        """a mask grown by r pixels, a square at a time along each axis"""
        out = mask.copy()
        for _ in range(int(r)):
            g = out.copy()
            g[1:, :] |= out[:-1, :]
            g[:-1, :] |= out[1:, :]
            g[:, 1:] |= out[:, :-1]
            g[:, :-1] |= out[:, 1:]
            out = g
        return out

    def cover(self):
        st = self.st
        z = self.field.z
        cov = np.zeros(z.shape, dtype=np.int8)
        areas = []
        fe1, fn1 = self.be1 + st["margin_m"], self.bn1 + st["margin_m"]
        for wid in sorted(self.ways):
            w = self.ways[wid]
            t = w.get("tags", {})
            if "building" in t or len(w["nodes"]) < 4 or w["nodes"][0] != w["nodes"][-1]:
                continue
            k = self.cover_class(t)
            if not k:
                continue
            ids, P = self.pts(w)
            if len(P) < 4 or P[:, 0].max() < self.fe0 or P[:, 0].min() > fe1 or P[:, 1].max() < self.fn0 or P[:, 1].min() > fn1:
                continue
            areas.append((k, S.ring_clean(P)))
        for r in sorted(self.rels, key=lambda r: r["id"]):
            t = r.get("tags", {})
            if t.get("type") != "multipolygon" or "building" in t:
                continue
            k = self.cover_class(t)
            if not k:
                continue
            for ring in self.rings([m["ref"] for m in r.get("members", []) if m.get("type") == "way" and m.get("role") == "outer"]):
                P = np.array([[self.ne[n], self.nn[n]] for n in ring if n in self.ne])
                if len(P) >= 4:
                    areas.append((k, S.ring_clean(P)))
        # the biggest first, so what's inside it (a lot in a park, a lawn in a block of shops) says what it is over it
        areas.sort(key=lambda a: -abs(S.area(a[1])) if len(a[1]) >= 3 else 0)
        for k, P in areas:
            if len(P) < 3:
                continue
            got = self.raster(P)
            if got:
                rs, cs, m = got
                cov[rs, cs][m] = k
        # buildings' footprints
        bmask = np.zeros(z.shape, dtype=bool)
        for _bid, _t, P in self.buildings + self.parts:
            if len(P) >= 3:
                got = self.raster(P)
                if got:
                    rs, cs, m = got
                    bmask[rs, cs] |= m
        # how near each pixel is to a road's edge or a sidewalk: the same distance the carve takes, out to pave_gap_m
        gap = float(st["pave_gap_m"])
        near = np.zeros(z.shape, dtype=bool)
        for l in self.lines:
            sidewalk = l["hw"] == "footway" and l["t"].get("footway") == "sidewalk"
            if l["bridge"] or not (l["kind"] == "road" or sidewalk):
                continue
            core = l["w"] / 2 + (st["sidewalk_m"] if l["walks"] else 0.0) + gap
            C = l["C"][np.unique(np.concatenate([np.arange(0, len(l["C"]), 4), [len(l["C"]) - 1]]))]
            for k in range(len(C) - 1):
                (ax, ay), (bx, by) = C[k], C[k + 1]
                c0 = max(int(math.floor(min(ax, bx) - core - self.fe0)) - 1, 0)
                c1 = min(int(math.ceil(max(ax, bx) + core - self.fe0)) + 1, z.shape[1] - 1)
                r0 = max(int(math.floor(min(ay, by) - core - self.fn0)) - 1, 0)
                r1 = min(int(math.ceil(max(ay, by) + core - self.fn0)) + 1, z.shape[0] - 1)
                if c1 < c0 or r1 < r0:
                    continue
                px = self.fe0 + np.arange(c0, c1 + 1)[None, :]
                py = self.fn0 + np.arange(r0, r1 + 1)[:, None]
                dx, dy = bx - ax, by - ay
                L2 = max(dx * dx + dy * dy, 1e-12)
                tt = np.clip(((px - ax) * dx + (py - ay) * dy) / L2, 0, 1)
                near[r0:r1 + 1, c0:c1 + 1] |= np.hypot(px - (ax + tt * dx), py - (ay + tt * dy)) < core
        built = self.grow(bmask, st["built_up_m"])
        apron = self.grow(bmask, st["building_apron_m"])
        default = np.where((near & built) | apron, 2, 1).astype(np.int8)
        self.cover_map = np.where(cov > 0, cov, default)
        c0, c1 = int(self.be0 - self.fe0), int(self.be1 - self.fe0)
        r0, r1 = int(self.bn0 - self.fn0), int(self.bn1 - self.fn0)
        box = self.cover_map[r0:r1, c0:c1]
        bc = cov[r0:r1, c0:c1]
        self.say("the ground no way covers: %d areas read (%d grass, %d paved); in the box %.1f%% says grass and %.1f%% paved by its area, and of the rest %.1f%% is paved by the default (near a road in a built-up block, or beside a building); %.1f%% of the box is grass in all" % (
            len(areas), sum(1 for a in areas if a[0] == 1), sum(1 for a in areas if a[0] == 2), 100.0 * (bc == 1).mean(), 100.0 * (bc == 2).mean(),
            100.0 * ((bc == 0) & (box == 2)).sum() / max((bc == 0).sum(), 1), 100.0 * (box == 1).mean()))
        self.lap("decided what the ground is")

    def under_decks(self):
        """NOTHING GROWS UNDER A WIDE DECK (11-10). The ground under I-5 at the camp was the map's grass, a lawn in the dark. Where the ground is grass
        and a bridge's deck is over it, between 1.5 m and bare_under_m above, the place is bare when it's deep enough under the structure for its
        height: further in from the edge of everything overhead than half the room under it (several ways side by side are one roof). So a freeway's
        underside is dirt, and the greenbelt under a narrow bridge 25 m up stays green. The cover's class 4. ⛔ OFF unless the style says bare_under_m
        (18 was tried): at the camp I-5's decks are separate ribbons with daylight between, so it bared 6,732 m2 of the slice and almost none of the
        camp's own ground, and what the ground there really is isn't something a rule can say (it takes someone who knows the place)"""
        top = float(self.st.get("bare_under_m", 0.0))
        if top <= 0:
            return
        _ins, _who, insb, _whob, hb, _roads = self.roadr()
        room = hb - self.st["deck_m"] - self.field.z
        roof = (insb > -1.5) & (room > 1.5) & (room < top)
        dark = np.zeros(roof.shape, dtype=bool)
        for r in (2, 4, 6, 8, 10, 14):
            dark |= ~self.grow(~roof, r) & (room <= 2.0 * r)
        bare = dark & (self.cover_map == 1)
        self.cover_map = np.where(bare, 4, self.cover_map).astype(np.int8)
        c0, c1 = int(self.be0 - self.fe0), int(self.be1 - self.fe0)
        r0, r1 = int(self.bn0 - self.fn0), int(self.bn1 - self.fn0)
        self.say("under the decks: %d square metres of the box's grass are bare ground (deep under a deck less than %g m up)" % (int(bare[r0:r1, c0:c1].sum()), top))
        self.lap("bared the ground under the decks")

    # ------------------------------------------------------------ 3. bridges from the tags
    def bridges(self):
        """every bridge way's nodes get heights: where a bridge meets a way on the ground, the ground there; along it, a straight run by length between
        those ends, and where bridge ways branch, each branch point the length-weighted mean of its neighbours (one solve for the lot)"""
        ground_nodes = {}
        for l in self.lines:
            if not l["bridge"]:
                for n in l["ids"]:
                    ground_nodes[n] = True
        bl = [l for l in self.lines if l["bridge"]]
        nodes = sorted({n for l in bl for n in l["ids"]})
        ix = {n: k for k, n in enumerate(nodes)}
        anchor = {}
        for l in bl:
            for n in (l["ids"][0], l["ids"][-1]):
                if n in ground_nodes:
                    anchor[n] = float(self.field.sample(self.ne[n], self.nn[n]))
        # a group of bridge ways touching nothing on the ground is held at its own ends
        parent = list(range(len(nodes)))

        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a
        ei, ej, ew = [], [], []
        for l in bl:
            for a, b in zip(l["ids"][:-1], l["ids"][1:]):
                if a == b:
                    continue
                L = math.hypot(self.ne[a] - self.ne[b], self.nn[a] - self.nn[b])
                ei.append(ix[a])
                ej.append(ix[b])
                ew.append(1.0 / max(L, 0.05))
                ra, rb = find(ix[a]), find(ix[b])
                if ra != rb:
                    parent[max(ra, rb)] = min(ra, rb)
        held = {find(ix[n]) for n in anchor}
        for l in bl:
            for n in (l["ids"][0], l["ids"][-1]):
                if find(ix[n]) not in held and n not in anchor:
                    anchor[n] = float(self.field.sample(self.ne[n], self.nn[n]))
        nN = len(nodes)
        h = np.zeros(nN)
        fixed = np.zeros(nN, dtype=bool)
        for n, v in anchor.items():
            h[ix[n]] = v
            fixed[ix[n]] = True
        ei, ej, ew = np.array(ei, dtype=int), np.array(ej, dtype=int), np.array(ew)
        free = np.nonzero(~fixed)[0]
        if len(free):
            # conjugate gradients on the graph's Laplacian, the anchors held: x is the free nodes' heights
            fpos = -np.ones(nN, dtype=int)
            fpos[free] = np.arange(len(free))
            deg = np.bincount(ei, ew, nN) + np.bincount(ej, ew, nN)

            def Lmul(x):
                full = np.zeros(nN)
                full[free] = x
                y = deg * full - np.bincount(ei, ew * full[ej], nN) - np.bincount(ej, ew * full[ei], nN)
                return y[free]
            hf = np.zeros(nN)
            hf[fixed] = h[fixed]
            b = -(deg * hf - np.bincount(ei, ew * hf[ej], nN) - np.bincount(ej, ew * hf[ei], nN))[free]
            x = np.full(len(free), float(np.mean(h[fixed])) if fixed.any() else 0.0)
            r = b - Lmul(x)
            p = r.copy()
            rr = float(r @ r)
            it = 0
            for it in range(4 * len(free) + 50):
                if rr < 1e-18:
                    break
                Ap = Lmul(p)
                al = rr / float(p @ Ap)
                x += al * p
                r -= al * Ap
                rn = float(r @ r)
                p = r + (rn / rr) * p
                rr = rn
            h[free] = x
        self.bh = {n: float(h[ix[n]]) for n in nodes}
        for l in bl:
            l["hc"] = S.along([self.bh[n] for n in l["ids"]], l["at"])
        self.say("bridges: %d ways, %d nodes, %d held where they meet the ground" % (len(bl), nN, len(anchor)))
        self.lap("lifted the bridges")

    # ------------------------------------------------------------ meshes
    def kind_name(self, k):
        c = self.st["colours"]
        if "#" in k:   # (11-06) a kind that carries its own colour: wall#9b5a44
            return k
        if k.startswith("bldg"):
            return "%s#%s" % (k, rgb(c["buildings"][int(k[4:]) % len(c["buildings"])]))
        return "%s#%s" % (k, rgb(c[k]))

    def ground(self):
        st = self.st
        g = float(st["ground_step_m"])
        C = float(st["cell_m"])
        m = int(round(C / g))
        z = self.field.z
        for i in range(self.ni):
            for j in range(self.nj):
                es = self.be0 + i * C + g * np.arange(m + 1)
                ns = self.bn0 + j * C + g * np.arange(m + 1)
                E, N = np.meshgrid(es, ns)
                H = self.field.sample(E, N)
                he = (self.field.sample(E + 1, N) - self.field.sample(E - 1, N)) / 2
                hn = (self.field.sample(E, N + 1) - self.field.sample(E, N - 1)) / 2
                X = self.cells.xyz(E, N, H).reshape(-1, 3)
                nrm = np.stack([-he, np.ones_like(he), hn], axis=-1).reshape(-1, 3)
                nrm /= np.linalg.norm(nrm, axis=1)[:, None]
                q = (np.arange(m)[None, :] + (m + 1) * np.arange(m)[:, None]).reshape(-1)
                a, b, c, d = q, q + 1, q + m + 2, q + m + 1
                # each square is grass or paved by what's at its middle (cover())
                me = (E[:-1, :-1] + g / 2).reshape(-1)
                mn = (N[:-1, :-1] + g / 2).reshape(-1)
                cc = np.clip(np.floor(me - self.fe0).astype(int), 0, self.cover_map.shape[1] - 1)
                rr = np.clip(np.floor(mn - self.fn0).astype(int), 0, self.cover_map.shape[0] - 1)
                paved = self.cover_map[rr, cc] == 2
                lot = self.cover_map[rr, cc] == 3   # (11-06) a surface lot: asphalt, a shade lighter than a road's
                bare = self.cover_map[rr, cc] == 4  # (11-10) bare ground under a wide deck (under_decks()): dirt, hard like paving to a wheel
                grass = ~paved & ~lot & ~bare
                # (11-06) patches: a slow pattern over the map picks each square's tone (the same word to E->surface: ground, paved)
                wav = np.sin(me * 0.047 + 0.3) + np.sin(mn * 0.039 + 1.7) + np.sin((me + mn) * 0.023 + 0.9) + 0.6 * np.sin((me - mn) * 0.091)
                tones = (("ground", grass & (wav > -0.6) & (wav <= 0.6)), ("ground#63714a", grass & (wav <= -0.6)), ("ground#74834f", grass & (wav > 0.6)),
                         ("paved", paved & (wav > -0.4)), ("paved#86837c", paved & (wav <= -0.4)),
                         ("paved#5f6064", lot & (wav > -0.3)), ("paved#57585c", lot & (wav <= -0.3)),
                         ("paved#8a8272", bare & (wav > -0.2)), ("paved#7e7667", bare & (wav <= -0.2))) if self.st.get("facades") else (("ground", grass), ("paved", paved | lot | bare))
                for kind, sel in tones:
                    if not sel.any():
                        continue
                    # (e, n): b is east of a, d north of it. With z south, a b c and a c d face up
                    idx = np.stack([a[sel], b[sel], c[sel], a[sel], c[sel], d[sel]], axis=1).reshape(-1)
                    used, idx = np.unique(idx, return_inverse=True)
                    self.cells.add(i, j, kind, X[used], nrm[used], idx.reshape(-1))
        self.lap("made the ground")

    def lines_geometry(self):
        st = self.st
        lift = st["lift"]
        cells = self.cells
        decks = [l for l in self.lines if l["bridge"] and l["kind"] != "rail"]
        # every deck as segments, to ask whether a point is on one (a parapet's only where its edge isn't against another deck)
        da, db, dw, dh0, dh1, downer = [], [], [], [], [], []
        for k, l in enumerate(decks):
            C, hc = l["C"], l["hc"]
            l["deck_ix"] = k
            da.append(C[:-1])
            db.append(C[1:])
            dw.append(np.full(len(C) - 1, l["w"] / 2))
            dh0.append(hc[:-1])
            dh1.append(hc[1:])
            downer.append(np.full(len(C) - 1, k))
        if decks:
            da, db, dw, dh0, dh1, downer = (np.concatenate(v) for v in (da, db, dw, dh0, dh1, downer))
        dwmax = float(dw.max()) if len(dw) else 0.0
        counts = {}

        def count(k, n):
            counts[k] = counts.get(k, 0) + n
        for l in self.lines:
            C, w, t = l["C"], l["w"], l["t"]
            if len(C) < 2:
                continue
            bridge = l["bridge"]
            hc = l["hc"] if bridge else None
            hw = l["hw"]
            if l["kind"] == "rail":
                g = st["rail_gauge_m"] / 2
                for o in (-g, g):
                    E, N, H = S.band(self.field, C, o - st["rail_m"] / 2, o + st["rail_m"] / 2, lift["rail"], hc)
                    count("rail", S.grid_quads(cells, "rail", E, N, H, (0, 1, 0)))
                continue
            if l["kind"] == "road":
                surf = "bridge" if bridge else ("concrete" if t.get("surface") == "concrete" else "road")
                up = lift["road"]
            else:
                surf = {"cycleway": "lane"}.get(hw, "path")
                up = lift["path"]
                if hw == "footway" and t.get("footway") == "sidewalk":
                    surf, up = "sidewalk", st["curb_m"]
                elif hw == "footway" and t.get("footway") == "crossing":
                    # (11-06) the road's own colour, so a marked crossing's stripes (street()) are on asphalt; it's still a crossing to E->surface
                    surf, up = ("crossing#" + self._road_colour_near(C[len(C) // 2])) if st.get("street") else "crossing", lift["crossing"]
            if bridge:
                up = 0.0
            E, N, H = S.band(self.field, C, -w / 2, w / 2, up, hc)
            count(surf, S.grid_quads(cells, surf, E, N, H, (0, 1, 0)))
            R = S.rights(C)
            if surf == "sidewalk" and not bridge:
                for side, k in ((-1, 0), (1, -1)):
                    count("curb", S.wall_along(cells, "curb", E[:, k], N[:, k], H[:, k], self.field.sample(E[:, k], N[:, k]), side * R[:, 0], side * R[:, 1]))
            # sidewalks drawn with the road, where it says so
            for side in l["walks"]:
                sg = 1 if side == "right" else -1
                a, b = sorted((sg * w / 2, sg * (w / 2 + st["sidewalk_m"])))
                Es, Ns, Hs = S.band(self.field, C, a, b, st["curb_m"] if not bridge else 0.0, None if not bridge else hc + st["curb_m"])
                count("sidewalk", S.grid_quads(cells, "sidewalk", Es, Ns, Hs, (0, 1, 0)))
                k = 0 if sg < 0 else -1
                kin = -1 if sg < 0 else 0
                Hlow = self.field.sample(Es[:, kin], Ns[:, kin]) + up if not bridge else hc
                count("curb", S.wall_along(cells, "curb", Es[:, kin], Ns[:, kin], Hs[:, kin], Hlow, -sg * R[:, 0], -sg * R[:, 1]))
            # bike lanes, from the cycleway tags: a painted lane, or a protected track with its posts
            if l["kind"] == "road":
                for side in ("left", "right"):
                    v = t.get("cycleway:" + side) or t.get("cycleway:both") or t.get("cycleway")
                    if v not in ("lane", "track", "buffered_lane", "opposite_lane", "opposite_track"):
                        continue
                    kind = "track" if "track" in v else "lane"
                    sg = 1 if side == "right" else -1
                    a, b = sorted((sg * (w / 2 - 0.1), sg * (w / 2 - 0.1 - st["bike_lane_m"])))
                    El, Nl, Hl = S.band(self.field, C, a, b, lift["lane"], None if not bridge else hc + lift["lane"] - up)
                    count(kind, S.grid_quads(cells, kind, El, Nl, Hl, (0, 1, 0)))
                    if kind == "track":
                        s = S.arclen(C)
                        at = np.arange(st["post_every_m"] / 2, s[-1], st["post_every_m"])
                        if len(at):
                            o = sg * (w / 2 - 0.1 - st["bike_lane_m"] - 0.15)
                            pe = np.interp(at, s, C[:, 0] + R[:, 0] * o)
                            pn = np.interp(at, s, C[:, 1] + R[:, 1] * o)
                            ph = np.interp(at, s, hc) if bridge else self.field.sample(pe, pn) + up
                            # (11-10) the posts stop where streets meet, as the real ones do: none within post_junction_m of a junction, and none
                            # standing in the lanes of another road than its own (the audit: 47 posts in travel lanes, across the mouths of side
                            # streets and through junctions on Dearborn, 12th, Golf Drive and 14th)
                            J = self.junctions()
                            jm = float(st.get("post_junction_m", 11.0))
                            ins_, who_, insb_, whob_, _hb, rds_ = self.roadr()
                            own = next((q_ for q_, l_ in enumerate(rds_) if l_ is l), -1)
                            for q in range(len(at)):
                                c_ = min(max(int(round(pe[q] - self.fe0)), 0), ins_.shape[1] - 1)
                                r_ = min(max(int(round(pn[q] - self.fn0)), 0), ins_.shape[0] - 1)
                                if not bridge:
                                    if len(J) and float(np.hypot(J[:, 0] - pe[q], J[:, 1] - pn[q]).min()) < jm:
                                        continue
                                    if ins_[r_, c_] > 0.3 and who_[r_, c_] != own:
                                        continue
                                elif insb_[r_, c_] > 0.3 and whob_[r_, c_] != own:
                                    continue   # on a bridge, in the lanes of another of its ways (where a ramp joins it)
                                count("post", S.box(cells, "post", pe[q], pn[q], st["post_m"][0] / 2, ph[q], ph[q] + st["post_m"][1]))
            # a bridge's deck: its sides and underside under the surface just laid. OSM draws a bridge's road and its sidewalks as ways of their own, often with
            # a gap between them where the road's width is less than the real deck's: where another deck is within reach_m of this one's edge at its height,
            # the deck is filled across to it, and where nothing is, it gets a low wall
            if bridge:
                bot = hc - st["deck_m"]
                th, ph = st["parapet_m"]
                reach = float(st.get("deck_reach_m", 6.0))
                for side in (-1, 1):
                    gap = np.zeros(len(C))
                    offs = np.arange(0.25, reach + 1e-6, 0.25)
                    # a stretch of the edge at a time, against only the segments near that stretch: every offset out to reach at once, and the first that lands on another deck
                    for c0 in range(0, len(C), 24) if len(da) else ():
                        Cs, Rs, hs = C[c0:c0 + 24], R[c0:c0 + 24], hc[c0:c0 + 24]
                        lo, hi = Cs.min(axis=0) - (w / 2 + reach + 2 + dwmax), Cs.max(axis=0) + (w / 2 + reach + 2 + dwmax)
                        near = np.nonzero((downer != l["deck_ix"]) & (np.maximum(da[:, 0], db[:, 0]) >= lo[0]) & (np.minimum(da[:, 0], db[:, 0]) <= hi[0])
                                          & (np.maximum(da[:, 1], db[:, 1]) >= lo[1]) & (np.minimum(da[:, 1], db[:, 1]) <= hi[1]))[0]
                        if not len(near):
                            continue
                        oe = (Cs[None, :, 0] + side * Rs[None, :, 0] * (w / 2 + offs[:, None])).ravel()
                        on = (Cs[None, :, 1] + side * Rs[None, :, 1] * (w / 2 + offs[:, None])).ravel()
                        dist, tt = S.seg_dist(oe, on, da[near, 0], da[near, 1], db[near, 0], db[near, 1])
                        hseg = dh0[near][None, :] + (dh1[near] - dh0[near])[None, :] * tt
                        hit = ((dist < dw[near][None, :]) & (np.abs(hseg - np.tile(hs, len(offs))[:, None]) < 1.0)).any(axis=1).reshape(len(offs), len(Cs))
                        anyhit = hit.any(axis=0)
                        gs = gap[c0:c0 + 24]
                        gs[anyhit] = offs[np.argmax(hit, axis=0)[anyhit]] + 0.1
                    k = 0 if side < 0 else -1
                    # the side wall stands at the edge where there's nothing to fill to, and at the fill's own edge where there is (it's the other deck's side)
                    Ein, Nin = E[:, k], N[:, k]
                    if gap.any():
                        Eo, No = Ein + side * R[:, 0] * gap, Nin + side * R[:, 1] * gap
                        Ef, Nf, Hf = np.stack([Ein, Eo], axis=1), np.stack([Nin, No], axis=1), np.stack([hc, hc], axis=1)
                        count("bridge", S.grid_quads(cells, "bridge", Ef, Nf, Hf, (0, 1, 0)))
                        count("deck", S.grid_quads(cells, "deck", Ef, Nf, Hf - st["deck_m"], (0, -1, 0)))
                    bare = gap == 0
                    for run in self.runs(bare):
                        sl = slice(run[0], run[1] + 1)
                        count("deck", S.wall_along(cells, "deck", Ein[sl], Nin[sl], H[sl, k], bot[sl], side * R[sl, 0], side * R[sl, 1]))
                        a, b = sorted((side * w / 2, side * (w / 2 - th)))
                        Ep, Np, Hp = S.band(self.field, C[sl], a, b, ph, hc[sl])
                        count("parapet", S.grid_quads(cells, "parapet", Ep, Np, Hp, (0, 1, 0)))
                        for kk, sgn in ((0, -1), (-1, 1)):
                            count("parapet", S.wall_along(cells, "parapet", Ep[:, kk], Np[:, kk], Hp[:, kk], hc[sl], sgn * R[sl, 0], sgn * R[sl, 1]))
                Eb, Nb, Hb = S.band(self.field, C, -w / 2, w / 2, 0.0, bot)
                count("deck", S.grid_quads(cells, "deck", Eb, Nb, Hb, (0, -1, 0)))
        self.say("laid along the ways (quads): " + ", ".join("%s %d" % kv for kv in sorted(counts.items())))
        self.lap("laid the streets, lanes, rails, decks")

    # ------------------------------------------------------------ 4b. what holds a deck up
    def piers(self):
        """(11-06) a bridge's piers, from the rule and the map's own ways (nothing here is from a photograph). Where a deck passes over a road or a rail on
        the ground, a bent stands just past that way's edge (its sidewalk too) on either side, so the span crosses it; between those, a bent every pier_every_m,
        half a spacing in from the way's start, so the ways of one long viaduct join at about the same spacing. A bent is round columns across the deck, about
        one for each pier_across_m of its width, under a cap beam the deck's width. A column isn't stood on a way on the ground (a road, a rail, a path or a
        sidewalk), inside a building, through a lower deck, or where the underside is less than pier_clear_m over the ground (towards a bridge's ends, where
        the ground comes up to meet it); a bent with no column left isn't built. pier_every_m 0 is none"""
        st = self.st
        every = float(st.get("pier_every_m", 0.0))
        if every <= 0:
            return
        dia, cap_d, cap_t = (float(v) for v in st["pier_m"])
        r = dia / 2
        across, clear, beside = float(st["pier_across_m"]), float(st["pier_clear_m"]), float(st["pier_side_m"])
        cells = self.cells
        obst, off = [], []
        for l in self.lines:
            if l["bridge"] or len(l["C"]) < 2:
                continue
            if l["kind"] == "road":
                reach = l["w"] / 2 + (st["sidewalk_m"] if l["walks"] else 0.0)
                obst.append((l["C"], reach))
                off.append((l["C"], reach + 0.3))
            elif l["kind"] == "rail":
                obst.append((l["C"], st["rail_gauge_m"] / 2 + 2.0))
                off.append((l["C"], st["rail_gauge_m"] / 2 + 2.0))
            else:   # a path or a sidewalk: a bent stands past it too, so a sidewalk mapped as its own way has the span over it, not a column in it
                obst.append((l["C"], l["w"] / 2))
                off.append((l["C"], l["w"] / 2 + 0.3))

        def flat(lst):
            if not lst:
                return np.zeros((0, 2)), np.zeros((0, 2)), np.zeros(0)
            return (np.concatenate([C[:-1] for C, _ in lst]), np.concatenate([C[1:] for C, _ in lst]),
                    np.concatenate([np.full(len(C) - 1, w) for C, w in lst]))
        oa, ob, ow = flat(obst)
        ka, kb, kw = flat(off)
        decks = [l for l in self.lines if l["bridge"] and l["kind"] != "rail" and len(l["C"]) >= 2]
        if not decks:
            return
        da = np.concatenate([l["C"][:-1] for l in decks])
        db = np.concatenate([l["C"][1:] for l in decks])
        dw = np.concatenate([np.full(len(l["C"]) - 1, l["w"] / 2 + 0.3) for l in decks])
        dh0 = np.concatenate([l["hc"][:-1] for l in decks])
        dh1 = np.concatenate([l["hc"][1:] for l in decks])
        boxes = []
        for _bid, _t, P in self.buildings + self.parts:
            if len(P) >= 3:
                boxes.append((P[:, 0].min(), P[:, 1].min(), P[:, 0].max(), P[:, 1].max(), P))
        nb = nc = dropped = 0
        # CITYMAP_PIERSAY=<mark>:<metres> says, for every bent that far from a mark, where it is and what stopped each column
        say_at = None
        sv = os.environ.get("CITYMAP_PIERSAY", "")
        if ":" in sv:
            mk, rad = sv.rsplit(":", 1)
            spec = self.cfg.get("marks", {}).get(mk)
            if spec:
                me_, mn_, _h = self.place(spec)
                say_at = (me_, mn_, float(rad))
        for l in self.lines:
            if not l["bridge"] or l["kind"] != "road" or len(l["C"]) < 2:
                continue
            C, hc, w = l["C"], l["hc"], l["w"]
            s = S.arclen(C)
            L = float(s[-1])
            if L < 2.0:
                continue
            # only what's near this way: its box, out past its sides and the widest reach
            lo, hi = C.min(axis=0) - (w / 2 + 40.0), C.max(axis=0) + (w / 2 + 40.0)

            def near(A, B):
                return np.nonzero((np.maximum(A[:, 0], B[:, 0]) >= lo[0]) & (np.minimum(A[:, 0], B[:, 0]) <= hi[0])
                                  & (np.maximum(A[:, 1], B[:, 1]) >= lo[1]) & (np.minimum(A[:, 1], B[:, 1]) <= hi[1]))[0]
            on_, kn_, dn_ = near(oa, ob), near(ka, kb), near(da, db)
            oa_, ob_, ow_ = oa[on_], ob[on_], ow[on_]
            ka_, kb_, kw_ = ka[kn_], kb[kn_], kw[kn_]
            da_, db_, dw_, dh0_, dh1_ = da[dn_], db[dn_], dw[dn_], dh0[dn_], dh1[dn_]
            # where the deck passes over a way on the ground: along the deck, that way's own band either side of the crossing, longer as they cross more
            # slantwise. A way running nearly along the deck (its own continuation on the ground, a street under a viaduct) isn't crossed: no column
            # stands on it all the same (below)
            spans = []
            if len(oa_):
                d2 = ob_ - oa_
                l2 = np.maximum(np.hypot(d2[:, 0], d2[:, 1]), 1e-9)
                for i in range(len(C) - 1):
                    p, q = C[i], C[i + 1]
                    d1 = q - p
                    seg = float(np.hypot(*d1))
                    if seg < 1e-6:
                        continue
                    den = d1[0] * d2[:, 1] - d1[1] * d2[:, 0]
                    good = np.abs(den) > 1e-9
                    den = np.where(good, den, 1.0)
                    wx, wy = oa_[:, 0] - p[0], oa_[:, 1] - p[1]
                    t = (wx * d2[:, 1] - wy * d2[:, 0]) / den
                    u = (wx * d1[1] - wy * d1[0]) / den
                    sin = np.abs(den) / (seg * l2)
                    hit = good & (t >= 0) & (t <= 1) & (u >= 0) & (u <= 1) & (sin >= 0.35)
                    for k in np.nonzero(hit)[0]:
                        half = float(ow_[k]) / float(sin[k])
                        at = float(s[i]) + float(t[k]) * seg
                        spans.append([at - half, at + half])
            spans.sort()
            merged = []
            for a, b in spans:
                if merged and a <= merged[-1][1]:
                    merged[-1][1] = max(merged[-1][1], b)
                else:
                    merged.append([a, b])
            # a bent beside each crossed band, or one in the middle of a gap too narrow for two
            fixed = []
            for k, (a, b) in enumerate(merged):
                if k == 0:
                    fixed.append(a - beside)
                if k + 1 < len(merged):
                    gap = merged[k + 1][0] - b
                    if gap >= 2.0 * beside + every / 3.0:
                        fixed += [b + beside, merged[k + 1][0] - beside]
                    elif gap >= dia + 0.6:
                        fixed.append(b + gap / 2.0)
                else:
                    fixed.append(b + beside)
            fixed = [x for x in fixed if 0.0 <= x <= L]
            fill = [x for x in np.arange(every / 2, L, every)
                    if not any(a - beside < x < b + beside for a, b in merged) and all(abs(x - f) > every / 2 for f in fixed)]
            R = S.rights(C)
            for at in sorted(fixed + fill):
                ce, cn = float(np.interp(at, s, C[:, 0])), float(np.interp(at, s, C[:, 1]))
                re, rn = float(np.interp(at, s, R[:, 0])), float(np.interp(at, s, R[:, 1]))
                rl = math.hypot(re, rn) or 1.0
                re, rn = re / rl, rn / rl
                under = float(np.interp(at, s, hc)) - st["deck_m"]
                m = max(1, int(round(w / across)))
                offs = np.zeros(1) if m == 1 else np.linspace(-(w / 2 - 1.0), w / 2 - 1.0, m)
                pe, pn = ce + re * offs, cn + rn * offs
                g = self.field.sample(pe, pn)
                why = np.array(["" for _ in offs], dtype=object)
                ok = (under - g) >= clear
                why[~ok] = "low"
                if len(ka_):
                    dist, _ = S.seg_dist(pe, pn, ka_[:, 0], ka_[:, 1], kb_[:, 0], kb_[:, 1])
                    onway = (dist < (kw_[None, :] + r)).any(axis=1)
                    why[ok & onway] = "on a way"
                    ok &= ~onway
                dist, tt = S.seg_dist(pe, pn, da_[:, 0], da_[:, 1], db_[:, 0], db_[:, 1])
                hseg = dh0_[None, :] + (dh1_ - dh0_)[None, :] * tt
                lower = ((dist < dw_[None, :]) & (hseg < under - 0.3) & (hseg > g[:, None])).any(axis=1)
                why[ok & lower] = "a deck under"
                ok &= ~lower
                for q in np.nonzero(ok)[0]:
                    for x0, y0, x1, y1, P in boxes:
                        if x0 - r <= pe[q] <= x1 + r and y0 - r <= pn[q] <= y1 + r and S.inside(np.array([pe[q]]), np.array([pn[q]]), P)[0]:
                            ok[q] = False
                            why[q] = "a building"
                            break
                if say_at and math.hypot(ce - say_at[0], cn - say_at[1]) <= say_at[2]:
                    self.say("pier near %s: way %s %s, %.1f m along of %.1f, at %.1f e %.1f n (%.1f m from the mark), underside %.1f m over the ground; columns %s" % (
                        sv, l["id"], l["t"].get("ref") or l["t"].get("name") or l["hw"], at, L, ce - say_at[0], cn - say_at[1], math.hypot(ce - say_at[0], cn - say_at[1]),
                        float(np.mean(under - g)), ", ".join("%+.1f %s" % (offs[q], why[q] or "stands") for q in range(len(offs)))))
                if not ok.any():
                    dropped += 1
                    continue
                nb += 1
                ang = 2.0 * math.pi * np.arange(13) / 12.0
                ca, sa = np.cos(ang), np.sin(ang)
                for q in np.nonzero(ok)[0]:
                    S.wall_along(cells, "pier", pe[q] + r * ca, pn[q] + r * sa, np.full(13, under + 0.05), np.full(13, float(g[q]) - 0.3), ca, sa)
                    nc += 1
                # the cap: a beam across the deck under its underside
                fe, fn = -rn, re
                a0, a1 = -(w / 2 - 0.4), w / 2 - 0.4
                Pe = np.array([ce + re * a0 - fe * cap_t / 2, ce + re * a1 - fe * cap_t / 2, ce + re * a1 + fe * cap_t / 2, ce + re * a0 + fe * cap_t / 2])
                Pn = np.array([cn + rn * a0 - fn * cap_t / 2, cn + rn * a1 - fn * cap_t / 2, cn + rn * a1 + fn * cap_t / 2, cn + rn * a0 + fn * cap_t / 2])
                outs = ((-fe, -fn), (re, rn), (fe, fn), (-re, -rn))
                for k in range(4):
                    k2 = (k + 1) % 4
                    S.wall_along(cells, "pier", Pe[[k, k2]], Pn[[k, k2]], np.full(2, under + 0.02), np.full(2, under - cap_d), outs[k][0], outs[k][1])
                X = cells.xyz(Pe, Pn, np.full(4, under - cap_d))
                cells.add_quads("pier", X[0:1], X[1:2], X[2:3], X[3:4], np.array([0.0, -1.0, 0.0]), np.array([ce]), np.array([cn]))
        self.say("piers: %d bents, %d columns; %d bents with nowhere to stand" % (nb, nc, dropped))
        self.lap("stood the piers")

    @staticmethod
    def runs(mask):
        """the stretches where mask is true, as (first, last) with last > first"""
        out, k = [], 0
        while k < len(mask):
            if not mask[k]:
                k += 1
                continue
            k2 = k
            while k2 + 1 < len(mask) and mask[k2 + 1]:
                k2 += 1
            if k2 > k:
                out.append((k, k2))
            k = k2 + 1
        return out

    # ------------------------------------------------------------ 5. buildings
    def building_height(self, t):
        h = metres(t.get("height"))
        if h and h > 0:
            return h, "height"
        lv = metres(t.get("building:levels"))
        if lv and lv > 0:
            return lv * self.st["level_m"] + (metres(t.get("roof:levels")) or 0) * self.st["level_m"] * 0.5, "levels"
        dl = (self.st.get("default_levels") or {}).get(t.get("building"))
        if dl:
            return float(dl[0]) * float(dl[1]), "its kind"
        return self.st["building_default_m"], "default"

    # ------------------------------------------------------------ 6b. what a building looks like (11-06)
    # palettes of mine, muted the way a city's are; a building's own colour tag wins. Nothing here is from a photograph
    WALLS = {
        "brick": ["8e4a3a", "9b5a44", "7a3f33", "a0644c"],
        "concrete": ["b8b4ab", "a9a69e", "c7c2b6", "8f8d88"],
        "apartments": ["c9b79c", "b6a58c", "d9cdb5", "a7b2b7", "c4a484", "9aa59a"],
        "house": ["6f8a8f", "8a9a7b", "b7a18a", "c9c1b0", "5d6d7e", "9c7b62", "d8d0bc", "a35d4a"],
        "industrial": ["9a9a94", "b0aa9c", "7f857f", "a8a291"],
        "commercial": ["c2b49a", "a88f72", "d0c7b5", "8e9289", "b49c84"],
    }
    NAMED = {"white": "e8e6e0", "black": "2b2b2b", "grey": "8f8f8f", "gray": "8f8f8f", "brown": "7a5a44", "tan": "c8b08a", "beige": "d6c9a8", "red": "8e3b32",
             "yellow": "d8c070", "blue": "5a7290", "green": "6f8a6a", "cream": "e6dcc0", "orange": "c47a3a", "pink": "d0a0a0", "maroon": "6e2f2f", "silver": "b8bcbf"}
    # each family's windows: width, height, sill over its floor, spacing along a wall
    WINDOWS = {"house": (1.0, 1.3, 0.9, 2.8), "apartments": (1.3, 1.5, 0.85, 3.0), "brick": (1.2, 1.7, 0.8, 2.6), "commercial": (1.6, 1.7, 0.8, 2.8),
               "concrete": (1.8, 1.6, 0.8, 2.4), "industrial": (2.4, 1.0, 2.6, 7.0)}
    GLASS, GLASS_SHOP, GLASS_WALL, MULLION, UNIT = "2e3a45", "3a4b58", "5b7186", "c9ced1", "a4a6a3"
    AWNINGS = ["2f6b5a", "8a2f2f", "2f4a7a", "b0822c", "4a4a4a"]
    ROOFS = ["3b3b3d", "4a3f3a", "5a5c5e", "6b4e3d", "2f3436"]
    SHOPS = {"restaurant", "cafe", "bar", "fast_food", "pub", "pharmacy", "bank", "ice_cream", "food_court", "clinic", "dentist", "post_office"}
    # (11-06) a shop's sign (shopfronts()): words for what the map says the shop is, never its name (real streets, fictional storefronts), in the colours
    # a street's signs come in (mine, by eye): [background, letters]
    SIGN_COLOURS = [("f2efe6", "b3262a"), ("c8202a", "f7d84a"), ("f4d03f", "a3161c"), ("1f4e8c", "f4f1e6"), ("2e7d4f", "f4f1e6"),
                    ("1c1c1e", "e8c35a"), ("f4f1e6", "1f4e8c"), ("e0601c", "fdf6e3")]
    CUISINE_WORDS = {"vietnamese": "PHO  BANH MI", "chinese": "NOODLE HOUSE", "japanese": "SUSHI", "sushi": "SUSHI", "thai": "THAI FOOD",
                     "korean": "KOREAN BBQ", "pizza": "PIZZA", "mexican": "TAQUERIA", "seafood": "SEAFOOD", "asian": "NOODLES", "burger": "BURGERS",
                     "sandwich": "SANDWICHES", "coffee_shop": "COFFEE", "dim_sum": "DIM SUM", "cantonese": "BBQ  NOODLES", "szechuan": "SZECHUAN",
                     "sichuan": "SZECHUAN", "filipino": "FILIPINO FOOD", "bubble_tea": "BUBBLE TEA", "bakery": "BAKERY", "indian": "INDIAN FOOD"}
    SHOP_WORDS = {"convenience": "GROCERY", "supermarket": "ASIAN MARKET", "hairdresser": "HAIR SALON", "beauty": "BEAUTY SUPPLY", "massage": "MASSAGE",
                  "mobile_phone": "PHONES", "herbalist": "HERBS  TEA", "bakery": "BAKERY", "jewelry": "JEWELRY", "clothes": "CLOTHING", "laundry": "LAUNDRY",
                  "tattoo": "TATTOO", "travel_agency": "TRAVEL", "alcohol": "LIQUOR", "tobacco": "SMOKE SHOP", "e-cigarette": "VAPE", "nails": "NAILS",
                  "florist": "FLOWERS", "butcher": "MEAT MARKET", "seafood": "SEAFOOD", "greengrocer": "PRODUCE", "furniture": "FURNITURE",
                  "electronics": "ELECTRONICS", "pawnbroker": "PAWN", "money_lender": "CASH LOANS", "optician": "EYEWEAR", "gift": "GIFTS",
                  "variety_store": "DISCOUNT", "car_repair": "AUTO REPAIR", "dry_cleaning": "CLEANERS", "deli": "DELI", "pastry": "BAKERY", "tea": "TEA"}
    AMENITY_WORDS = {"bar": "BAR", "pub": "BAR", "pharmacy": "PHARMACY", "bank": "BANK", "ice_cream": "ICE CREAM", "clinic": "CLINIC",
                     "dentist": "DENTAL", "post_office": "POST OFFICE"}
    ANY_WORDS = ["GROCERY", "PHO", "NAILS", "PHONES", "BAKERY", "JEWELRY", "TRAVEL", "HERBS  TEA", "BEAUTY SUPPLY", "TAX SERVICE", "FOR LEASE",
                 "SEAFOOD", "BANH MI", "DIM SUM", "KARAOKE", "INSURANCE", "MONEY TRANSFER", "BARBER", "FOR LEASE"]

    @staticmethod
    def shade(hexs, f):
        return "%02x%02x%02x" % tuple(max(0, min(255, int(int(hexs[i:i + 2], 16) * f))) for i in (0, 2, 4))

    def colour_of(self, v):
        if not v:
            return None
        v = v.strip().lower()
        if re.fullmatch(r"#?[0-9a-f]{6}", v):
            return v.lstrip("#")
        if re.fullmatch(r"#?[0-9a-f]{3}", v):
            return "".join(ch * 2 for ch in v.lstrip("#"))
        return self.NAMED.get(v)

    def look(self, bid, t, area, hgt, shop):
        """what a building looks like, from its tags, its size and whether there's a shop in it: the same every cook"""
        rnd = random.Random(bid)
        b = (t.get("building") or t.get("building:part") or "yes").lower()
        mat = (t.get("building:material") or "").lower()
        house = b in ("house", "detached", "semidetached_house", "terrace", "residential", "bungalow", "garage", "shed", "cabin") or (b == "yes" and area < 160 and hgt <= 10)
        if mat == "glass" or (b in ("office", "commercial", "hotel") and hgt > 30):
            fam = "glass"
        elif mat == "brick" or (b in ("apartments", "commercial", "retail") and rnd.random() < 0.3):
            fam = "brick"
        elif house:
            fam = "house"
        elif b in ("industrial", "warehouse", "garages", "service", "storage_tank", "roof", "manufacture"):
            fam = "industrial"
        elif b in ("office", "hospital", "school", "university", "college", "public", "civic", "government", "parking", "stadium", "train_station", "transportation", "hotel"):
            fam = "concrete"
        elif b in ("retail", "commercial", "supermarket", "kiosk"):
            fam = "commercial"
        else:
            fam = "apartments" if hgt > 9 or not shop else "commercial"
        wall = self.colour_of(t.get("building:colour")) or (self.GLASS_WALL if fam == "glass" else rnd.choice(self.WALLS[fam]))
        shape = (t.get("roof:shape") or "").lower()
        pitched = (shape in ("gabled", "hipped", "pyramidal", "gambrel", "half-hipped") or (house and shape in ("", "yes"))) and fam != "glass"
        return {"fam": fam, "wall": wall, "shop": shop and fam not in ("industrial", "house"), "pitched": pitched, "rnd": rnd,
                "awning": rnd.choice(self.AWNINGS) if rnd.random() < 0.65 else None, "roof": self.colour_of(t.get("roof:colour")) or rnd.choice(self.ROOFS)}

    def facade(self, bid, t, P, base, ref, top, mid, area, shop, key=None, shops=()):
        """walls, windows, a shopfront, a roof: one building's"""
        st, cells = self.st, self.cells
        hgt = top - ref
        lk = self.look(bid, t, area, hgt, shop)
        rnd, fam = lk["rnd"], lk["fam"]
        Pc = np.vstack([P, P[:1]])
        d = Pc[1:] - Pc[:-1]
        L = np.maximum(np.hypot(*d.T), 1e-9)
        de, dn = d[:, 0] / L, d[:, 1] / L
        oe, on = dn, -de   # the ring's counter-clockwise, so outward is right of each edge
        me, mn = np.full(len(L), mid[0]), np.full(len(L), mid[1])
        lo3 = cells.xyz(Pc[:, 0], Pc[:, 1], np.full(len(Pc), base))
        hi3 = cells.xyz(Pc[:, 0], Pc[:, 1], np.full(len(Pc), top))
        hint = np.stack([oe, np.zeros(len(oe)), -on], axis=1)
        cells.add_quads("wall#" + lk["wall"], lo3[:-1], lo3[1:], hi3[1:], hi3[:-1], hint, me, mn)
        quads = {}

        def quad(kind, k, c0, c1, h0, h1, out=0.04, out1=None):
            """a flat piece on wall k, from c0 to c1 along it and h0 to h1 up it, out from the wall (out1 at its top: an awning's slope)"""
            o1 = out if out1 is None else out1
            ax, ay = Pc[k, 0] + de[k] * c0, Pc[k, 1] + dn[k] * c0
            bx, by = Pc[k, 0] + de[k] * c1, Pc[k, 1] + dn[k] * c1
            quads.setdefault(kind, []).append(((ax + oe[k] * out, ay + on[k] * out, h0), (bx + oe[k] * out, by + on[k] * out, h0),
                                              (bx + oe[k] * o1, by + on[k] * o1, h1), (ax + oe[k] * o1, ay + on[k] * o1, h1), (oe[k], on[k])))
        lev = 2.9 if fam == "house" else float(st["level_m"])
        floors = max(1, int(hgt / lev + 0.3))
        if fam == "glass":
            # a curtain wall: the glass is the wall; a light band at every floor
            for k in range(len(L)):
                for f in range(1, floors):
                    hb = ref + f * lev
                    if hb + 0.2 < top - 0.3:
                        quad("trim#" + self.MULLION, k, 0.0, L[k], hb, hb + 0.2, 0.03)
        else:
            ww, wh, sill, sp = self.WINDOWS[fam]
            if t.get("facade:windows"):   # (11-06) a source's windows: width, height, sill, spacing
                try:
                    ww, wh, sill, sp = (float(v) for v in str(t["facade:windows"]).split(","))
                except ValueError:
                    pass
            mull = int(float(t.get("facade:mullions", 0) or 0))
            f0 = 1 if lk["shop"] and hgt > 4.0 else 0
            gx = max(0.0, float(st.get("shop_floor_m", 4.6)) - lev) if f0 else 0.0   # (11-06) a shop's ground floor is taller: the floors over it start higher
            for k in range(len(L)):
                if L[k] < 2.2:
                    continue
                n = int((L[k] - 1.2) // sp)
                if n < 1:
                    continue
                cs = (L[k] - n * sp) / 2.0 + sp / 2.0 + sp * np.arange(n)
                for f in range(f0, floors if fam != "industrial" else 1):
                    hb = ref + f * lev + sill + (gx if f >= 1 else 0.0)
                    if hb + wh > top - 0.35:
                        break
                    for c in cs:
                        quad("glass#" + self.GLASS, k, c - ww / 2, c + ww / 2, hb, hb + wh)
                        for mq in range(mull):   # (11-06) its mullions, and a sill
                            mx_ = c - ww / 2 + ww * (mq + 1) / (mull + 1)
                            quad("trim#3a3c40", k, mx_ - 0.035, mx_ + 0.035, hb, hb + wh, 0.06)
                        if mull:
                            quad("trim#" + self.shade(lk["wall"], 0.82), k, c - ww / 2 - 0.08, c + ww / 2 + 0.08, hb - 0.1, hb, 0.08)
                            quad("trim#2e3034", k, c - ww / 2 - 0.07, c + ww / 2 + 0.07, hb - 0.02, hb + wh + 0.07, 0.03)   # (11-06) its dark frame
            if f0 and t.get("facade:high_windows") == "yes":
                # (11-06) one tall shop storey with a row of high windows over its signs (the config's tags, from a source): 4.05 m up, 1.15 tall
                for k in range(len(L)):
                    if L[k] < 2.2:
                        continue
                    n = int((L[k] - 1.2) // sp)
                    if n < 1 or ref + 5.2 > top - 0.3:
                        continue
                    for c in (L[k] - n * sp) / 2.0 + sp / 2.0 + sp * np.arange(n):
                        quad("glass#" + self.GLASS, k, c - ww / 2, c + ww / 2, ref + 4.05, ref + 5.2)
            if f0 and st.get("shopfronts", 1) and key is not None:
                # (11-06) its shopfronts: on the walls that face a street, in bays with their signs
                self.shopfronts(bid, P, Pc, L, de, dn, oe, on, ref, top, lk, quad, key, shops, quads, tags=t)
            elif f0:
                # the shopfront: glass along the ground floor, and an awning over it on some
                for k in range(len(L)):
                    if L[k] < 4.0:
                        continue
                    quad("glass#" + self.GLASS_SHOP, k, 0.6, L[k] - 0.6, ref + 0.35, ref + 3.0)
                    if lk["awning"] and L[k] >= 5.0:
                        quad("awning#" + lk["awning"], k, 0.8, L[k] - 0.8, ref + 3.45, ref + 3.15, 0.05, 1.15)
                        quad("awning#" + lk["awning"], k, 0.8, L[k] - 0.8, ref + 3.15, ref + 2.9, 1.15, 1.15)
        # (11-06) a source's cornice: a deep moulding along the top of every wall (over the parapet's), a thin one under it, and a band under the upper
        # windows (the config's tags facade:cornice, facade:band)
        if t.get("facade:cornice") and key is not None and fam != "glass":
            cc = self.colour_of(t["facade:cornice"]) or self.shade(lk["wall"], 1.06)
            ptop = top + (0.8 if hgt >= 5.0 else 0.0)
            for k in range(len(L)):
                if L[k] < 1.0:
                    continue
                self._wall_solid(key, "trim#" + cc, Pc, k, de, dn, oe, on, [(-0.3, ptop - 0.42), (L[k] + 0.3, ptop - 0.42), (L[k] + 0.3, ptop + 0.04), (-0.3, ptop + 0.04)], -0.05, 0.34)
                self._wall_solid(key, "trim#" + cc, Pc, k, de, dn, oe, on, [(-0.12, ptop - 0.72), (L[k] + 0.12, ptop - 0.72), (L[k] + 0.12, ptop - 0.6), (-0.12, ptop - 0.6)], -0.05, 0.14)
                if t.get("facade:band") == "yes" and hgt > float(st.get("shop_floor_m", 4.6)) + 1.0:
                    hbnd = ref + float(st.get("shop_floor_m", 4.6))
                    self._wall_solid(key, "trim#" + cc, Pc, k, de, dn, oe, on, [(0.0, hbnd - 0.12), (L[k], hbnd - 0.12), (L[k], hbnd + 0.06), (0.0, hbnd + 0.06)], -0.05, 0.1)
        for kind, qs in quads.items():
            A = np.array([q[0] for q in qs])
            B = np.array([q[1] for q in qs])
            C = np.array([q[2] for q in qs])
            D = np.array([q[3] for q in qs])
            O = np.array([q[4] for q in qs])
            X = [cells.xyz(V[:, 0], V[:, 1], V[:, 2]) for V in (A, B, C, D)]
            h = np.stack([O[:, 0], np.zeros(len(O)), -O[:, 1]], axis=1)
            if kind.startswith("awning"):
                h = h + np.array([0.0, 1.5, 0.0])   # its slope faces up and out
            cells.add_quads(kind, X[0], X[1], X[2], X[3], h, np.full(len(qs), mid[0]), np.full(len(qs), mid[1]))
        # the roof
        if lk["pitched"] and self.gable(P, top, lk, mid, area):
            return lk
        tris, clean = S.earclip(P)
        i, j, _ok = cells.cell_of(np.array([mid[0]]), np.array([mid[1]]))
        cells.add_tris("roof", hi3[:-1][np.array(tris)], int(i[0]), int(j[0]))
        if fam != "house" and hgt >= 5.0:
            # a parapet round the edge, and on a big roof the plant
            pk = "trim#" + self.shade(lk["wall"], 0.8)
            H1 = top + 0.8
            inE, inN = Pc[:-1, 0] - oe * 0.3, Pc[:-1, 1] - on * 0.3
            inE2, inN2 = Pc[1:, 0] - oe * 0.3, Pc[1:, 1] - on * 0.3
            O0 = cells.xyz(Pc[:-1, 0], Pc[:-1, 1], np.full(len(L), top))
            O1 = cells.xyz(Pc[1:, 0], Pc[1:, 1], np.full(len(L), top))
            T0 = cells.xyz(Pc[:-1, 0], Pc[:-1, 1], np.full(len(L), H1))
            T1 = cells.xyz(Pc[1:, 0], Pc[1:, 1], np.full(len(L), H1))
            I0 = cells.xyz(inE, inN, np.full(len(L), H1))
            I1 = cells.xyz(inE2, inN2, np.full(len(L), H1))
            J0 = cells.xyz(inE, inN, np.full(len(L), top))
            J1 = cells.xyz(inE2, inN2, np.full(len(L), top))
            cells.add_quads(pk, O0, O1, T1, T0, hint, me, mn)
            cells.add_quads(pk, T0, T1, I1, I0, np.array([0.0, 1.0, 0.0]), me, mn)
            cells.add_quads(pk, J0, J1, I1, I0, -hint, me, mn)
            if area >= 200.0:
                lo, hi = P.min(axis=0), P.max(axis=0)
                for _ in range(min(5, int(area / 350.0) + 1)):
                    for _try in range(30):
                        x, y, hw = rnd.uniform(lo[0], hi[0]), rnd.uniform(lo[1], hi[1]), rnd.uniform(0.8, 1.5)
                        cx = np.array([x - hw - 1.0, x + hw + 1.0, x + hw + 1.0, x - hw - 1.0])
                        cy = np.array([y - hw - 1.0, y - hw - 1.0, y + hw + 1.0, y + hw + 1.0])
                        if S.inside(cx, cy, P).all():
                            S.box(cells, "unit#" + self.UNIT, x, y, hw, top, top + rnd.uniform(1.0, 2.2))
                            break
        return lk

    def gable(self, P, top, lk, mid, area):
        """a pitched roof over a house: a gable along the long side of the box round its footprint, when the footprint is near enough that box. False if not"""
        cells = self.cells
        m = P.mean(axis=0)
        Q = P - m
        w, V = np.linalg.eigh(np.cov(Q.T))
        u, v = V[:, 1], V[:, 0]   # the long way, and across it
        su, sv = Q @ u, Q @ v
        u0, u1, v0, v1 = su.min(), su.max(), sv.min(), sv.max()
        if (u1 - u0) * (v1 - v0) <= 1e-6 or area / ((u1 - u0) * (v1 - v0)) < 0.75 or v1 - v0 > 16.0:
            return False
        rh = min(3.5, 0.32 * (v1 - v0))
        vm = 0.5 * (v0 + v1)
        oh = 0.3

        def at(a, b):
            return m[0] + u[0] * a + v[0] * b, m[1] + u[1] * a + v[1] * b

        def X(a, b, h):
            e, n = at(a, b)
            return cells.xyz(np.array([e]), np.array([n]), np.array([h]))
        rk, wk = "shingle#" + lk["roof"], "wall#" + lk["wall"]
        me, mn = np.array([mid[0]]), np.array([mid[1]])
        eave, ridge = top - 0.1, top + rh
        for vs, side in ((v0 - oh, -1.0), (v1 + oh, 1.0)):
            n_up = np.array([v[0] * side, 1.2, -v[1] * side])
            cells.add_quads(rk, X(u0 - oh, vs, eave), X(u1 + oh, vs, eave), X(u1 + oh, vm, ridge), X(u0 - oh, vm, ridge), n_up, me, mn)
        for us, side in ((u0, -1.0), (u1, 1.0)):
            nn = np.array([u[0] * side, 0.0, -u[1] * side])
            cells.add_quads(wk, X(us, v0, top), X(us, v1, top), X(us, vm, ridge), X(us, vm, ridge), nn, me, mn)
        return True

    # ------------------------------------------------------------ 6c. what grows (11-06)
    LEAVES = ["4f6b3a", "5d7a3f", "3f5a35", "6b7f45"]
    NEEDLES = ["2f4a35", "36503a"]
    SHRUBS = ["4a6338", "5a7040", "3e5634"]
    BARK = "5a4636"

    @staticmethod
    def _ico():
        """a unit icosahedron: its 20 faces' corners (20, 3, 3)"""
        t = (1.0 + 5.0 ** 0.5) / 2.0
        v = np.array([[-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0], [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t], [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1]], dtype=np.float64)
        v /= np.linalg.norm(v, axis=1)[:, None]
        f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        return v[np.array(f)]

    def _faces(self, kind, T, ci, cj):
        """triangles (m, 3, 3) in map terms (e, up, n), each its own flat face facing out, into a cell"""
        if not len(T):
            return
        X = self.cells.xyz(T[..., 0], T[..., 2], T[..., 1])   # (m, 3, 3) engine coordinates
        nrm = np.cross(X[:, 1] - X[:, 0], X[:, 2] - X[:, 0])
        nrm /= np.maximum(np.linalg.norm(nrm, axis=1), 1e-12)[:, None]
        self.cells.add(ci, cj, kind, X.reshape(-1, 3), np.repeat(nrm, 3, axis=0), np.arange(len(T) * 3))

    def _wall_solid(self, key, kind, Pc, k, de, dn, oe, on, prof, o0, o1):
        """(11-06) a convex solid on wall k: its profile prof [(along, up), ...] (convex, in order round it) extruded from o0 to o1 out from the
        wall's face; a cornice, a pediment, a pilaster. Map terms (e, up, n), faces turned out (_orient), into the cell key"""
        a0 = [(Pc[k, 0] + de[k] * c + oe[k] * o0, h, Pc[k, 1] + dn[k] * c + on[k] * o0) for c, h in prof]
        a1 = [(Pc[k, 0] + de[k] * c + oe[k] * o1, h, Pc[k, 1] + dn[k] * c + on[k] * o1) for c, h in prof]
        n = len(prof)
        tris = []
        for q in range(1, n - 1):
            tris.append((a0[0], a0[q], a0[q + 1]))
            tris.append((a1[0], a1[q + 1], a1[q]))
        for q in range(n):
            q2 = (q + 1) % n
            tris.append((a0[q], a0[q2], a1[q2]))
            tris.append((a0[q], a1[q2], a1[q]))
        self._faces(kind, self._orient(np.array(tris, dtype=np.float64)), key[0], key[1])

    def _orient(self, T):
        """one convex piece's triangles (m, 3, 3) in map terms (e, up, n), each turned so it faces away from the piece's middle in the engine's
        coordinates, which flip north: a winding that faces out in map terms faces in there"""
        X = self.cells.xyz(T[..., 0], T[..., 2], T[..., 1])
        c = X.reshape(-1, 3).mean(axis=0)
        nr = np.cross(X[:, 1] - X[:, 0], X[:, 2] - X[:, 0])
        inward = (nr * (X.mean(axis=1) - c)).sum(axis=1) < 0
        if inward.any():
            T = T.copy()
            T[inward, 1], T[inward, 2] = T[inward, 2].copy(), T[inward, 1].copy()
        return T

    def planting(self):
        st = self.st
        if not st.get("trees"):
            return
        z = self.field.z
        # where things grow, as masks on the field's 1 m grid
        wood = np.zeros(z.shape, dtype=bool)
        scrub = np.zeros(z.shape, dtype=bool)
        park = np.zeros(z.shape, dtype=bool)
        WOOD = {("natural", "wood"), ("landuse", "forest")}
        SCRUB = {("natural", "scrub"), ("natural", "shrubbery"), ("natural", "heath"), ("landuse", "shrubs")}
        PARK = {("leisure", "park"), ("leisure", "garden"), ("leisure", "nature_reserve"), ("landuse", "grass"), ("landuse", "recreation_ground"),
                ("landuse", "cemetery"), ("landuse", "meadow"), ("landuse", "village_green")}

        def which(t):
            for k, v in t.items():
                if (k, v) in WOOD:
                    return wood
                if (k, v) in SCRUB:
                    return scrub
                if (k, v) in PARK:
                    return park
            return None
        rings = []
        for wid in sorted(self.ways):
            w = self.ways[wid]
            t = w.get("tags", {})
            if "building" in t or len(w["nodes"]) < 4 or w["nodes"][0] != w["nodes"][-1]:
                continue
            m = which(t)
            if m is not None:
                rings.append((m, S.ring_clean(self.pts(w)[1])))
        for r in sorted(self.rels, key=lambda r: r["id"]):
            t = r.get("tags", {})
            if t.get("type") != "multipolygon" or "building" in t:
                continue
            m = which(t)
            if m is None:
                continue
            for ring in self.rings([mm["ref"] for mm in r.get("members", []) if mm.get("type") == "way" and mm.get("role") == "outer"]):
                P = np.array([[self.ne[n], self.nn[n]] for n in ring if n in self.ne])
                if len(P) >= 4:
                    rings.append((m, S.ring_clean(P)))
        for m, P in rings:
            if len(P) >= 3:
                got = self.raster(P)
                if got:
                    rs, cs, mk = got
                    m[rs, cs] |= mk
        gy, gx = np.gradient(z)
        steep = (self.cover_map == 1) & (np.hypot(gx, gy) > float(st["steep_grass"])) & ~wood & ~scrub
        lawn = (self.cover_map == 1) & ~wood & ~scrub & ~park & ~steep
        # what can't have anything on it: every way's band, a building and a metre round it
        block = np.zeros(z.shape, dtype=bool)
        for l in self.lines:
            if l["bridge"]:
                continue
            reach = l["w"] / 2 + ((st["sidewalk_m"] if l["walks"] else 0.0) + 1.0 if l["kind"] == "road" else 0.6)
            C = l["C"]
            for k in range(len(C) - 1):
                (ax, ay), (bx, by) = C[k], C[k + 1]
                c0 = max(int(math.floor(min(ax, bx) - reach - self.fe0)), 0)
                c1 = min(int(math.ceil(max(ax, bx) + reach - self.fe0)), z.shape[1] - 1)
                r0 = max(int(math.floor(min(ay, by) - reach - self.fn0)), 0)
                r1 = min(int(math.ceil(max(ay, by) + reach - self.fn0)), z.shape[0] - 1)
                if c1 < c0 or r1 < r0:
                    continue
                px = self.fe0 + np.arange(c0, c1 + 1)[None, :]
                py = self.fn0 + np.arange(r0, r1 + 1)[:, None]
                dx, dy = bx - ax, by - ay
                L2 = max(dx * dx + dy * dy, 1e-12)
                tt = np.clip(((px - ax) * dx + (py - ay) * dy) / L2, 0, 1)
                block[r0:r1 + 1, c0:c1 + 1] |= np.hypot(px - (ax + tt * dx), py - (ay + tt * dy)) < reach
        bm = np.zeros(z.shape, dtype=bool)
        for _bid, _t, P in self.buildings + self.parts:
            if len(P) >= 3:
                got = self.raster(P)
                if got:
                    rs, cs, mk = got
                    bm[rs, cs] |= mk
        block |= self.grow(bm, 1)
        roadband = np.zeros(z.shape, dtype=bool)
        for l in self.lines:
            if l["bridge"] or l["kind"] != "road":
                continue
            reach = l["w"] / 2 + 0.3
            C = l["C"]
            for k in range(len(C) - 1):
                (ax, ay), (bx, by) = C[k], C[k + 1]
                c0 = max(int(math.floor(min(ax, bx) - reach - self.fe0)), 0)
                c1 = min(int(math.ceil(max(ax, bx) + reach - self.fe0)), z.shape[1] - 1)
                r0 = max(int(math.floor(min(ay, by) - reach - self.fn0)), 0)
                r1 = min(int(math.ceil(max(ay, by) + reach - self.fn0)), z.shape[0] - 1)
                if c1 < c0 or r1 < r0:
                    continue
                px = self.fe0 + np.arange(c0, c1 + 1)[None, :]
                py = self.fn0 + np.arange(r0, r1 + 1)[:, None]
                dx, dy = bx - ax, by - ay
                L2 = max(dx * dx + dy * dy, 1e-12)
                tt = np.clip(((px - ax) * dx + (py - ay) * dy) / L2, 0, 1)
                roadband[r0:r1 + 1, c0:c1 + 1] |= np.hypot(px - (ax + tt * dx), py - (ay + tt * dy)) < reach
        # the points: the map's own trees and rows first, then a jittered grid over each kind of ground
        spots = []   # (e, n, kind, size)
        for nid, tg in self.ntags.items():
            if tg.get("natural") == "tree" and nid in self.ne:
                spots.append((self.ne[nid], self.nn[nid], "pine" if tg.get("leaf_type") == "needleleaved" else "tree", 1.0, True))
        for wid in sorted(self.ways):
            w = self.ways[wid]
            if w.get("tags", {}).get("natural") != "tree_row":
                continue
            P = self.pts(w)[1]
            if len(P) >= 2:
                Cr, _at = S.densify(P, 7.0)
                spots += [(float(c[0]), float(c[1]), "tree", 1.0, True) for c in Cr]
        lo_e, hi_e, lo_n, hi_n = self.be0, self.be1, self.bn0, self.bn1
        # (11-06) a city's woods are brushy: KartaView's photo of Dearborn at 12th shows the mapped wood there as bushes with fewer, smaller trees
        for mask, key, kinds in ((wood, "plant_wood", (("shrub", 0.45), ("tree", 0.4), ("pine", 0.15))), (scrub, "plant_scrub", (("shrub", 0.95), ("tree", 0.05))),
                                 (steep, "plant_steep", (("shrub", 0.6), ("tree", 0.3), ("pine", 0.1))), (park, "plant_park", (("tree", 0.85), ("pine", 0.15))),
                                 (lawn, "plant_lawn", (("tree", 0.8), ("shrub", 0.2)))):
            sp, share = st[key]
            es = np.arange(lo_e + sp / 2, hi_e, sp)
            ns = np.arange(lo_n + sp / 2, hi_n, sp)
            E, N = np.meshgrid(es, ns)
            E, N = E.ravel(), N.ravel()
            h = (np.floor(E * 7.13 + N * 3.71) * 2654435761 % 4294967296).astype(np.float64)
            rnd = lambda k: ((h * (k + 1) * 0.61803398875) % 1.0)
            E = E + (rnd(1) - 0.5) * sp * 0.8
            N = N + (rnd(2) - 0.5) * sp * 0.8
            cc = np.clip((E - self.fe0).astype(int), 0, z.shape[1] - 1)
            rr = np.clip((N - self.fn0).astype(int), 0, z.shape[0] - 1)
            keep = mask[rr, cc] & (rnd(3) < share)
            pick = rnd(4)
            for q in np.nonzero(keep)[0]:
                acc, kind = 0.0, kinds[-1][0]
                for kname, kw in kinds:
                    acc += kw
                    if pick[q] < acc:
                        kind = kname
                        break
                # (11-06) the size by where it grows: trees on steep grass and in scrub smaller, scrub's bushes bigger (brambles)
                grow = {("plant_steep", False): 0.7, ("plant_scrub", False): 0.55, ("plant_scrub", True): 1.3}.get((key, kind == "shrub"), 1.0)
                spots.append((float(E[q]), float(N[q]), kind, (0.7 + 0.6 * float(rnd(5)[q])) * grow, False))
        # off every way and building, and not where a deck's too low over it
        decks = [l for l in self.lines if l["bridge"] and len(l["C"]) >= 2]
        da = np.concatenate([l["C"][:-1] for l in decks]) if decks else np.zeros((0, 2))
        db = np.concatenate([l["C"][1:] for l in decks]) if decks else np.zeros((0, 2))
        dw = np.concatenate([np.full(len(l["C"]) - 1, l["w"] / 2 + 3.0) for l in decks]) if decks else np.zeros(0)
        dh = np.concatenate([0.5 * (l["hc"][:-1] + l["hc"][1:]) for l in decks]) if decks else np.zeros(0)
        made = {"tree": 0, "pine": 0, "shrub": 0}
        T = {}
        ico = self._ico()
        _ig, _wg, insb_, _wb, hb_, _rds = self.roadr()
        for e, n, kind, size, mapped in spots:
            if not (lo_e <= e < hi_e and lo_n <= n < hi_n):
                continue
            c, r = int(e - self.fe0), int(n - self.fn0)
            if not (0 <= r < z.shape[0] and 0 <= c < z.shape[1]) or (block[r, c] and not mapped) or bm[r, c] or roadband[r, c]:
                continue
            if self.cover_map[r, c] == 4 and not mapped:
                continue   # (11-10) bare ground deep under a deck: nothing's planted there by rule
            g = float(self.field.sample(np.array([e]), np.array([n]))[0])
            # (11-10) and not in a bridge's own roadway where it comes down to the ground: its deck no higher than the ground there, so the test below
            # (a deck OVER it) let a shrub grow in I-5's lanes at the ends of its bridges
            if insb_[r, c] > -0.6 and hb_[r, c] <= g + 0.6:
                continue
            hgen = (int(e * 13.7 + n * 7.9) * 2654435761) % 4294967296
            u = [((hgen * (k + 3) * 0.61803398875) % 1.0) for k in range(6)]
            H = {"tree": 6.0 + 6.0 * u[0], "pine": 9.0 + 8.0 * u[0], "shrub": 0.9 + 0.9 * u[0]}[kind] * size
            if len(da):
                dist, _t = S.seg_dist(np.array([e]), np.array([n]), da[:, 0], da[:, 1], db[:, 0], db[:, 1])
                over = (dist[0] < dw) & (dh > g)
                if over.any():
                    room = float((dh[over] - st["deck_m"] - g).min()) - 0.5
                    if room < 1.5:
                        continue
                    H = min(H, room)
            ci, cj, ok = self.cells.cell_of(np.array([e]), np.array([n]))
            if not ok[0]:
                continue
            key = (int(ci[0]), int(cj[0]))
            ang = u[1] * 2.0 * math.pi
            ca, sa = math.cos(ang), math.sin(ang)

            def blob(cx, cy, cz, rx, ry, rz):
                V = ico * np.array([rx, ry, rz])
                V = np.stack([V[..., 0] * ca - V[..., 2] * sa, V[..., 1], V[..., 0] * sa + V[..., 2] * ca], axis=-1)
                return self._orient(V + np.array([cx, cy, cz]))

            def prism(cx, cz, y0, y1, r0, r1, sides):
                a = 2.0 * math.pi * np.arange(sides + 1) / sides + ang
                x0, z0 = cx + r0 * np.cos(a), cz + r0 * np.sin(a)
                x1, z1 = cx + r1 * np.cos(a), cz + r1 * np.sin(a)
                out = []
                for k in range(sides):
                    A, B = (x0[k], y0, z0[k]), (x0[k + 1], y0, z0[k + 1])
                    C, D = (x1[k + 1], y1, z1[k + 1]), (x1[k], y1, z1[k])
                    out += [(A, B, C), (A, C, D)] if r1 > 1e-6 else [(A, B, C)]
                return self._orient(np.array(out, dtype=np.float64))
            if kind == "tree":
                Rc = 0.32 * H
                T.setdefault((key, "bark#" + self.BARK), []).append(prism(e, n, g - 0.2, g + 0.6 * H, 0.1 + 0.012 * H, 0.07 + 0.008 * H, 6))
                lk = "leaf#" + self.LEAVES[int(u[2] * len(self.LEAVES)) % len(self.LEAVES)]
                T.setdefault((key, lk), []).append(blob(e, g + 0.66 * H, n, Rc, 0.8 * Rc, Rc))
                T.setdefault((key, lk), []).append(blob(e + (u[3] - 0.5) * Rc, g + 0.82 * H, n + (u[4] - 0.5) * Rc, 0.62 * Rc, 0.55 * Rc, 0.62 * Rc))
            elif kind == "pine":
                T.setdefault((key, "bark#" + self.BARK), []).append(prism(e, n, g - 0.2, g + 0.3 * H, 0.12 + 0.01 * H, 0.08 + 0.008 * H, 6))
                nk = "pine#" + self.NEEDLES[int(u[2] * len(self.NEEDLES)) % len(self.NEEDLES)]
                for y0, y1, r0 in ((0.2, 0.62, 0.3), (0.42, 0.82, 0.24), (0.62, 1.0, 0.17)):
                    T.setdefault((key, nk), []).append(prism(e, n, g + y0 * H, g + y1 * H, r0 * H, 0.0, 8))
            else:
                sk = "shrub#" + self.SHRUBS[int(u[2] * len(self.SHRUBS)) % len(self.SHRUBS)]
                T.setdefault((key, sk), []).append(blob(e, g + 0.35 * H, n, 0.75 * H, 0.6 * H, 0.75 * H))
            made[kind] += 1
        for (key, kind), parts in T.items():
            self._faces(kind, np.concatenate(parts), key[0], key[1])
        self.say("planting: %d trees, %d conifers, %d shrubs (%d of the map's own trees and rows' trees offered)" % (
            made["tree"], made["pine"], made["shrub"], sum(1 for sp in spots if sp[4])))
        self.lap("planted")

    # ------------------------------------------------------------ 6d. what's on the street (11-06)
    def _box(self, key, kind, e, n, h0, h1, along, across, fe, fn, out):
        """an upright box in map terms: its middle (e, n), h0 to h1, along (fe, fn) and across it; its faces into out[(key, kind)]"""
        re_, rn_ = fn, -fe
        P = [(e + fe * along / 2 + re_ * across / 2, n + fn * along / 2 + rn_ * across / 2), (e - fe * along / 2 + re_ * across / 2, n - fn * along / 2 + rn_ * across / 2),
             (e - fe * along / 2 - re_ * across / 2, n - fn * along / 2 - rn_ * across / 2), (e + fe * along / 2 - re_ * across / 2, n + fn * along / 2 - rn_ * across / 2)]
        tri = []
        for k in range(4):
            (ax, ay), (bx, by) = P[k], P[(k + 1) % 4]
            A, B, C, D = (ax, h0, ay), (bx, h0, by), (bx, h1, by), (ax, h1, ay)
            tri += [(A, C, B), (A, D, C)]
        a, b, c, d = ((x, h1, y) for x, y in P)
        tri += [(a, c, b), (a, d, c)]
        out.setdefault((key, kind), []).append(self._orient(np.array(tri, dtype=np.float64)))

    def _prism(self, key, kind, e, n, h0, h1, r0, r1, sides, out, rot=0.0):
        a = 2.0 * math.pi * np.arange(sides + 1) / sides + rot
        x0, z0, x1, z1 = e + r0 * np.cos(a), n + r0 * np.sin(a), e + r1 * np.cos(a), n + r1 * np.sin(a)
        tri = []
        for k in range(sides):
            A, B, C, D = (x0[k], h0, z0[k]), (x0[k + 1], h0, z0[k + 1]), (x1[k + 1], h1, z1[k + 1]), (x1[k], h1, z1[k])
            tri += [(A, C, B), (A, D, C)] if r1 > 1e-6 else [(A, C, B)]
        if r1 > 1e-6:
            for k in range(1, sides - 1):
                tri.append(((x1[0], h1, z1[0]), (x1[k + 1], h1, z1[k + 1]), (x1[k], h1, z1[k])))
        out.setdefault((key, kind), []).append(self._orient(np.array(tri, dtype=np.float64)))

    def _texquad(self, key, tex, corners, uvs):
        """(11-06) a quad that carries a texture (a see-through fence's weave): corners in map terms (e, up, n), each with its texture coordinate, both
        faces. Kept apart from the colour-only pieces, and written as its own mesh in the cell with the texture in the cell's pack (write())"""
        if not hasattr(self, "texq"):
            self.texq = {}
        self.texq.setdefault(key, {}).setdefault(tex, []).append((corners, uvs))

    WEAVES = ("chainlink", "bars", "razor")

    def weave_texture(self, name):
        """(11-06) a see-through fence's texture, made by code, its holes clear (alpha 0) so both renderers cut them out. chainlink: a diamond weave of
        galvanised wire, 8 diamonds across a 128-pixel tile, the wire a little thicker than life so it holds together a few metres off. bars: upright
        bars with a gap between"""
        n = 128 if name == "chainlink" else 64
        px = bytearray()
        for y in range(n):
            for x in range(n):
                if name == "chainlink":
                    p = 16.0
                    a = ((x + y + 0.5) % p) / p
                    b = ((x - y + 0.5 + 4 * n) % p) / p
                    w = 2.6 / p
                    on = min(a, 1.0 - a) < w / 2 or min(b, 1.0 - b) < w / 2
                    shade = 0.92 + 0.08 * (((x * 7 + y * 13) % 11) / 10.0)
                    c = (int(150 * shade), int(156 * shade), int(160 * shade))
                else:
                    on = (x % 16) < 3 or y >= n - 3
                    c = (58, 62, 66)
                px += bytes((c[0], c[1], c[2], 255 if on else 0))
        return to_png(bytes(px), n, n)

    def mural_texture(self):
        """(11-06) a crosswalk mural of our own design (the real one at 12th and Jackson is an artist's work, not copied): a pale blue field in a white frame,
        lotus flowers in white line along it, waves between them, dots. 512 x 128, laid once across a crossing (u along it, v across)"""
        W, H = 512, 128
        field, line = (150, 192, 214), (240, 244, 246)
        px = [list(field) for _ in range(W * H)]

        def dot(x, y, r):
            for yy in range(int(y - r - 1), int(y + r + 2)):
                for xx in range(int(x - r - 1), int(x + r + 2)):
                    if 0 <= xx < W and 0 <= yy < H and (xx - x) ** 2 + (yy - y) ** 2 <= r * r:
                        px[yy * W + xx] = list(line)
        # the frame
        for x in range(W):
            for y in list(range(5, 9)) + list(range(H - 9, H - 5)):
                px[y * W + x] = list(line)
        for y in range(5, H - 5):
            for x in list(range(5, 9)) + list(range(W - 9, W - 5)):
                px[y * W + x] = list(line)
        # lotuses: petals as arcs from a base, five a flower
        for cx in (64, 192, 320, 448):
            base_y = 92
            for p in range(-2, 3):
                ang = p * 0.42
                for tt in range(0, 61):
                    u = tt / 60.0
                    # a petal's outline: out and back along a pointed loop
                    for sgn in (-1.0, 1.0):
                        w9 = 9.0 * math.sin(math.pi * u)
                        lx, ly = sgn * w9, -46.0 * u
                        rx = lx * math.cos(ang) - ly * math.sin(ang)
                        ry = lx * math.sin(ang) + ly * math.cos(ang)
                        dot(cx + rx, base_y + ry, 1.3)
            for tt in range(0, 41):   # its stem and a leaf line
                dot(cx, base_y + tt * 0.5, 1.2)
                dot(cx - 20 + tt, base_y + 14 - 3 * math.sin(tt / 40.0 * math.pi), 1.1)
        # waves between the flowers, and dots
        for x in range(12, W - 12):
            for k, (y0, amp, ph) in enumerate(((28, 5.0, 0.0), (110, 4.0, 1.5))):
                if any(abs(x - cx) < 34 for cx in (64, 192, 320, 448)) and k == 0:
                    continue
                y = y0 + amp * math.sin(x / 11.0 + ph)
                dot(x, y, 1.2)
        for cx in (128, 256, 384):
            for r9 in (6, 12):
                for a9 in range(0, 72):
                    a = a9 / 72.0 * 2 * math.pi
                    dot(cx + r9 * math.cos(a), 64 + r9 * math.sin(a), 1.0)
        rgba = bytearray()
        for c in px:
            rgba += bytes((c[0], c[1], c[2], 255))
        return to_png(bytes(rgba), W, H)

    def _flush(self, out):
        """the faces gathered (each piece already turned outward by _orient)"""
        for (key, kind), parts in out.items():
            self._faces(kind, np.concatenate(parts), key[0], key[1])

    def _road_colour_near(self, p):
        """the colour of the road nearest a point (a crossing's middle): concrete's or the road's"""
        st = self.st
        if not hasattr(self, "_rcn"):
            rs = [l for l in self.lines if l["kind"] == "road" and len(l["C"]) >= 2]
            self._rcn = (np.concatenate([l["C"][:-1] for l in rs]), np.concatenate([l["C"][1:] for l in rs]),
                         np.concatenate([np.full(len(l["C"]) - 1, 1 if l["t"].get("surface") == "concrete" else 0) for l in rs])) if rs else None
        c = st["colours"]
        if self._rcn is None:
            return rgb(c["road"])
        a, b, k = self._rcn
        near = np.nonzero((np.minimum(a[:, 0], b[:, 0]) <= p[0] + 25) & (np.maximum(a[:, 0], b[:, 0]) >= p[0] - 25) & (np.minimum(a[:, 1], b[:, 1]) <= p[1] + 25) & (np.maximum(a[:, 1], b[:, 1]) >= p[1] - 25))[0]
        if not len(near):
            return rgb(c["road"])
        dist, _t = S.seg_dist(np.array([p[0]]), np.array([p[1]]), a[near, 0], a[near, 1], b[near, 0], b[near, 1])
        return rgb(c["concrete"] if k[near[int(np.argmin(dist[0]))]] else c["road"])

    def _beam(self, key, kind, a, b, r, out):
        """a thin square rod from a to b (map terms e, up, n), r its half-width: a wire, an arm"""
        a, b = np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64)
        d = b - a
        L = float(np.linalg.norm(d))
        if L < 1e-4:
            return
        d /= L
        up = np.array([0.0, 1.0, 0.0]) if abs(d[1]) < 0.9 else np.array([1.0, 0.0, 0.0])
        u = np.cross(d, up)
        u /= np.linalg.norm(u)
        v = np.cross(d, u)
        cs = [(u + v) * r, (u - v) * r, (-u - v) * r, (-u + v) * r]
        tri = []
        for k in range(4):
            A, B, C, D = a + cs[k], a + cs[(k + 1) % 4], b + cs[(k + 1) % 4], b + cs[k]
            tri += [(A, B, C), (A, C, D)]
        out.setdefault((key, kind), []).append(self._orient(np.array(tri)))

    # (11-06) a street's name the way the city's blades say it, and the plate under it in Little Saigon
    ABBR = {"Street": "St", "Avenue": "Ave", "Boulevard": "Blvd", "Place": "Pl", "Drive": "Dr", "Road": "Rd", "Lane": "Ln", "Court": "Ct",
            "Terrace": "Ter", "Parkway": "Pkwy", "Square": "Sq", "Highway": "Hwy"}
    DIRS = {"South": "S", "North": "N", "East": "E", "West": "W", "Southwest": "SW", "Southeast": "SE", "Northwest": "NW", "Northeast": "NE"}
    DIRS_VI = {"South": "Nam", "North": "Bắc", "East": "Đông", "West": "Tây"}
    # (11-06) a shop's sign in Little Saigon: the Vietnamese over the English (my Vietnamese, common shop words: for someone who speaks it to check)
    VI_PAIRS = {"GROCERY": ("TẠP HÓA", "GROCERY"), "ASIAN MARKET": ("CHỢ Á ĐÔNG", "ASIAN MARKET"),
                "PHO  BANH MI": ("PHỞ  BÁNH MÌ", "NOODLE SOUP  SANDWICHES"), "PHO": ("PHỞ", "NOODLE SOUP"),
                "BANH MI": ("BÁNH MÌ", "SANDWICHES"), "NOODLE HOUSE": ("MÌ HOÀNH THÁNH", "NOODLE HOUSE"),
                "NOODLES": ("MÌ  HỦ TIẾU", "NOODLES"), "RESTAURANT": ("NHÀ HÀNG", "RESTAURANT"),
                "SEAFOOD": ("HẢI SẢN", "SEAFOOD"), "HAIR SALON": ("CẮT TÓC", "HAIR SALON"), "BARBER": ("HỚT TÓC", "BARBER"),
                "NAILS": ("LÀM MÓNG", "NAILS"), "BEAUTY SUPPLY": ("MỸ PHẨM", "BEAUTY SUPPLY"), "JEWELRY": ("TIỆM VÀNG", "JEWELRY"),
                "PHONES": ("ĐIỆN THOẠI", "PHONES"), "HERBS  TEA": ("THUỐC BẮC", "HERBS  TEA"), "BAKERY": ("TIỆM BÁNH", "BAKERY"),
                "TRAVEL": ("DU LỊCH", "TRAVEL"), "BUBBLE TEA": ("TRÀ SỮA", "BUBBLE TEA"), "CAFE": ("CÀ PHÊ", "CAFE"),
                "COFFEE": ("CÀ PHÊ", "COFFEE"), "VEGAN DELI": ("ĐỒ CHAY", "VEGAN DELI"), "TOFU": ("ĐẬU HŨ", "TOFU"),
                "FOR LEASE": ("CHO THUÊ", "FOR LEASE"), "TAX SERVICE": ("KHAI THUẾ", "TAX SERVICE"), "INSURANCE": ("BẢO HIỂM", "INSURANCE"),
                "MONEY TRANSFER": ("GỬI TIỀN", "MONEY TRANSFER"), "MASSAGE": ("XOA BÓP", "MASSAGE"), "SANDWICHES": ("BÁNH MÌ", "SANDWICHES")}

    def street_short(self, name):
        """a street's name as the city's blade says it: South Jackson Street is S Jackson St, 12th Avenue South is 12th Ave S"""
        w = name.split()
        if len(w) < 2:
            return name
        out = list(w)
        i = len(w) - 1
        if w[i] in self.DIRS:
            out[i] = self.DIRS[w[i]]
            i -= 1
        if i > 0 and w[i] in self.ABBR:
            out[i] = self.ABBR[w[i]]
        if w[0] in self.DIRS and len(w) > 2:
            out[0] = self.DIRS[w[0]]
        return " ".join(out)

    def street_vi(self, name):
        """the Vietnamese plate under a street's name in Little Saigon, the way the corner's own two read (a photo of the corner, 11-06): South Jackson Street is
        Nam Đường Jackson, 12th Avenue South is Đường Số 12 Nam. Other names the same way (my reading of those two)"""
        w = name.split()
        if len(w) < 2:
            return None
        pre = w[0] if w[0] in self.DIRS_VI and len(w) > 2 else None
        suf = w[-1] if w[-1] in self.DIRS_VI and len(w) > 2 else None
        core = w[(1 if pre else 0):(len(w) - (1 if suf else 0))]
        if len(core) > 1 and (core[-1] in self.ABBR or core[-1] == "Way"):
            core = core[:-1]
        if not core:
            return None
        m = re.fullmatch(r"(\d+)(st|nd|rd|th)", core[0]) if len(core) == 1 else None
        body = ("Đường Số %s" % m.group(1)) if m else ("Đường " + " ".join(core))
        if pre:
            return self.DIRS_VI[pre] + " " + body
        if suf:
            return body + " " + self.DIRS_VI[suf]
        return body

    def districts(self):
        """(11-06) the config's `districts`: a neighbourhood as the city's map draws it (an OpenStreetMap boundary relation's outer ways), widened by
        buffer_m so both sides of a street on its edge are in it, and what's its own there: `lamps` (lantern: Chinatown-International District's red
        lantern posts) and `signs` (vi: Little Saigon's shop signs in Vietnamese and English, and its street-name plates)"""
        if hasattr(self, "_districts"):
            return self._districts
        rels = {r["id"]: r for r in self.rels}
        out = []
        for dz in self.cfg.get("districts", []):
            r = rels.get(int(dz.get("relation", 0)))
            rings = []
            for m in (r or {}).get("members", []):
                if m.get("type") == "way" and m.get("role") in ("outer", "") and m["ref"] in self.ways:
                    R = np.array([[self.ne[q], self.nn[q]] for q in self.ways[m["ref"]]["nodes"] if q in self.ne])
                    if len(R) >= 3:
                        rings.append(R)
            if not rings:
                self.say("district %s: the map has no outline for it (relation %s)" % (dz.get("name"), dz.get("relation")))
                continue
            b = float(dz.get("buffer_m", 0.0))
            allp = np.concatenate(rings)
            out.append(dict(dz, rings=rings, buffer=b, lo=allp.min(axis=0) - b, hi=allp.max(axis=0) + b))
        self._districts = out
        if out:
            self.say("(11-06) districts: " + ", ".join("%s (%s)" % (d.get("name"), ", ".join("%s %s" % (k, d[k]) for k in ("lamps", "signs") if d.get(k)))
                                                      for d in out))
        return out

    def district_of(self, e, n, what):
        """what the district a point is in says for `what` (lamps, signs), or None"""
        for d in self.districts():
            if not d.get(what) or e < d["lo"][0] or n < d["lo"][1] or e > d["hi"][0] or n > d["hi"][1]:
                continue
            for R in d["rings"]:
                if S.inside(np.array([e]), np.array([n]), R)[0]:
                    return d[what]
                Rc = np.vstack([R, R[:1]])
                dist, _t = S.seg_dist(np.array([e]), np.array([n]), Rc[:-1, 0], Rc[:-1, 1], Rc[1:, 0], Rc[1:, 1])
                if float(dist.min()) <= d["buffer"]:
                    return d[what]
        return None

    def lantern_post(self, key, e, n, h, fe, fn, out, k):
        """(11-06) a red lantern post, Chinatown-International District's (its shape read off the city's camera at 12th and Jackson and photos of the street;
        ours, made by code): an octagonal foot, a slim red pole, a lantern of pale glass under a red pagoda cap and a gold finial, and on a gold arm
        toward the street a banner (ours: a gold lantern or a lotus on red) hung square to the curb"""
        R, CAP, GLASS, GOLD = "b3262a", "9e1d22", "f2e9d3", "c9a227"
        self._prism(key, "pole#" + R, e, n, h - 0.2, h + 0.55, 0.16, 0.13, 8, out)
        self._prism(key, "pole#" + R, e, n, h + 0.55, h + 3.95, 0.06, 0.05, 8, out)
        self._prism(key, "pole#" + R, e, n, h + 3.9, h + 3.98, 0.13, 0.13, 8, out)
        self._prism(key, "lamp#" + GLASS, e, n, h + 3.98, h + 4.44, 0.15, 0.18, 8, out)
        self._prism(key, "pole#" + R, e, n, h + 4.42, h + 4.48, 0.21, 0.21, 8, out)
        self._prism(key, "pole#" + CAP, e, n, h + 4.48, h + 4.72, 0.28, 0.05, 8, out)
        self._prism(key, "pole#" + GOLD, e, n, h + 4.72, h + 4.88, 0.03, 0.01, 6, out)
        self._beam(key, "pole#" + GOLD, (e, h + 3.56, n), (e + fe * 0.8, h + 3.56, n + fn * 0.8), 0.018, out)
        ax, ay, bx, by = e + fe * 0.1, n + fn * 0.1, e + fe * 0.74, n + fn * 0.74
        self._texquad(key, "banner:%d" % (k % 2), [(ax, h + 2.56, ay), (bx, h + 2.56, by), (bx, h + 3.53, by), (ax, h + 3.53, ay)],
                      [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])

    def razor_seg(self, key, ax, ay, bx, by, g, hh, u0):
        """(11-06) coiled razor wire along a fence's top (photos of 12th and Jackson's lot): two crossed panels carrying coils made by code,
        clear between them"""
        L = math.hypot(bx - ax, by - ay)
        if L < 1e-3:
            return
        re_, rn_ = (by - ay) / L, -(bx - ax) / L
        ga, gb = g(ax, ay), g(bx, by)
        r = 0.3
        c = math.cos(math.radians(30.0))
        for s in (1.0, -1.0):
            sn = math.sin(math.radians(30.0)) * s
            pa = (ax - re_ * r * sn, ga + hh + r - r * c, ay - rn_ * r * sn)
            pb = (bx - re_ * r * sn, gb + hh + r - r * c, by - rn_ * r * sn)
            pc = (bx + re_ * r * sn, gb + hh + r + r * c, by + rn_ * r * sn)
            pd = (ax + re_ * r * sn, ga + hh + r + r * c, ay + rn_ * r * sn)
            self._texquad(key, "razor", [pa, pb, pc, pd], [(u0, 1.0), (u0 + L, 1.0), (u0 + L, 0.0), (u0, 0.0)])

    def blade_words(self, key, e, n, h0, h1, length, fd, bg, fg, words):
        """(11-06) a street-name blade's words on both its faces, each reading left to right from its own side"""
        fe, fn = fd
        re_, rn_ = fn, -fe
        name = "sign:%s:%s:%.1f:%s" % (bg, fg, max(1.0, round(length / (h1 - h0) * 2.0) / 2.0), words)
        for s in (1.0, -1.0):
            ox, oy = re_ * 0.023 * s, rn_ * 0.023 * s
            ax, ay = e - fe * length / 2 * s + ox, n - fn * length / 2 * s + oy
            bx, by = e + fe * length / 2 * s + ox, n + fn * length / 2 * s + oy
            self._texquad(key, name, [(ax, h0, ay), (bx, h0, by), (bx, h1, by), (ax, h1, ay)], [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])

    def banner_texture(self, name):
        """(11-06) a lantern post's banner, ours: a red field in a gold border, a lantern (0) or a lotus (1) in gold, a fringe. 128 x 256"""
        from PIL import Image, ImageDraw
        k = int(name.split(":")[1])
        W, H = 128, 256
        im = Image.new("RGBA", (W, H), (178, 30, 36, 255))
        d = ImageDraw.Draw(im)
        gold = (214, 168, 40, 255)
        d.rectangle([7, 7, W - 8, H - 26], outline=gold, width=5)
        cx, cy = W / 2.0, H * 0.42
        if k == 0:
            d.ellipse([cx - 32, cy - 44, cx + 32, cy + 44], outline=gold, width=5)
            for w_ in (11, 22):
                d.ellipse([cx - 32 + w_, cy - 44, cx + 32 - w_, cy + 44], outline=gold, width=2)
            d.line([(cx, cy - 44), (cx, cy + 44)], fill=gold, width=2)
            d.rectangle([cx - 14, cy - 56, cx + 14, cy - 44], fill=gold)
            d.rectangle([cx - 14, cy + 44, cx + 14, cy + 56], fill=gold)
            d.line([(cx, cy + 56), (cx, cy + 86)], fill=gold, width=4)
        else:
            for a in (-60, -30, 0, 30, 60):
                ang = math.radians(a)
                tx, ty = cx + 48 * math.sin(ang), cy + 20 - 48 * math.cos(ang)
                d.polygon([(cx - 10 * math.cos(ang), cy + 20 - 10 * math.sin(ang)), (tx, ty), (cx + 10 * math.cos(ang), cy + 20 + 10 * math.sin(ang))],
                          outline=gold, fill=(200, 50, 50, 255))
            d.line([(cx - 40, cy + 30), (cx + 40, cy + 30)], fill=gold, width=4)
        for x in range(10, W - 8, 9):
            d.line([(x, H - 26), (x, H - 6)], fill=gold, width=3)
        return to_png(im.tobytes(), W, H)

    def razor_texture(self):
        """(11-06) coiled razor wire seen from the side: loops of steel wire, three a metre, barbed, clear between. 256 x 128 is 1 x 0.6 m"""
        from PIL import Image, ImageDraw
        W, H = 256, 128
        im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        steel = (182, 188, 192, 255)
        for i in range(-2, 6):
            cx = i * W / 3.0 + 20.0
            d.ellipse([cx - 60, 6, cx + 60, H - 6], outline=steel, width=3)
            for a in range(0, 360, 24):
                ang = math.radians(a + (i % 3) * 7)
                x, y = cx + 60 * math.cos(ang), H / 2.0 + (H / 2.0 - 6) * math.sin(ang)
                d.line([(x - 4, y - 3), (x + 4, y + 3)], fill=steel, width=2)
        return to_png(im.tobytes(), W, H)

    def emblem_texture(self, name):
        """(11-06) a gable's emblem, ours (the real building's is its own): a medallion in its colour inside a gold ring, an endless knot in gold on it,
        clear round it. 256 x 256"""
        from PIL import Image, ImageDraw
        parts = name.split(":")
        if len(parts) >= 3 and parts[1] == "ji":
            return self.ji_texture("#" + parts[2])
        col = "#" + parts[1]
        GOLD = (214, 168, 40, 255)
        im = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.ellipse([6, 6, 250, 250], fill=GOLD)
        d.ellipse([20, 20, 236, 236], fill=col)
        d.ellipse([34, 34, 222, 222], outline=GOLD, width=5)
        for k in range(4):   # the knot: two squares turned and a ring of loops
            a = math.radians(45.0 * k)
            pts = [(128 + 62 * math.cos(a + j * math.pi / 2), 128 + 62 * math.sin(a + j * math.pi / 2)) for j in range(4)]
            if k % 2 == 0:
                d.polygon(pts, outline=GOLD, width=7)
        for k in range(8):
            a = math.radians(22.5 + 45.0 * k)
            cx, cy = 128 + 80 * math.cos(a), 128 + 80 * math.sin(a)
            d.ellipse([cx - 12, cy - 12, cx + 12, cy + 12], outline=GOLD, width=5)
        return to_png(im.tobytes(), 256, 256)

    def letters_texture(self, name):
        """(11-06) bare letters on a wall (a building's name band): its words in Be Vietnam Pro in their colour, clear round them, drawn at the band's
        own shape and fitted into 1024 x 128"""
        from PIL import Image, ImageDraw
        _, fg, asp, words = name.split(":", 3)
        H2 = 160
        W2 = max(H2, int(round(H2 * float(asp))))
        im = Image.new("RGBA", (W2, H2), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        size = int(H2 * 0.9)
        while True:
            f = self.sign_font(size)
            x0, y0, x1, y1 = d.textbbox((0, 0), words, font=f)
            if (x1 - x0 <= W2 - 8 and y1 - y0 <= H2 - 16) or size <= 12:
                break
            size -= 4
        d.text(((W2 - (x1 - x0)) / 2 - x0, (H2 - (y1 - y0)) / 2 - y0), words, font=f, fill="#" + fg)
        im = im.resize((1024, 128), Image.LANCZOS)
        return to_png(im.tobytes(), 1024, 128)

    def ji_texture(self, col):
        """(11-06) the character 吉 ("lucky"), drawn by code as a bold sans character's strokes in col, a darker edge round them, clear round it:
        a gable's emblem in our own choosing (the real building's is 好, from its own name). 256 x 256"""
        from PIL import Image, ImageDraw
        im = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        strokes = [(116, 14, 140, 122), (24, 50, 232, 76), (66, 102, 190, 124),     # 士: the upright, the long stroke, the short one
                   (58, 142, 82, 248), (174, 142, 198, 238), (58, 142, 198, 164), (58, 214, 198, 236)]   # 口
        r, g, b = int(col[1:3], 16), int(col[3:5], 16), int(col[5:7], 16)
        for x0, y0, x1, y1 in strokes:
            d.rectangle([x0 - 3, y0 - 3, x1 + 3, y1 + 3], fill=(int(r * 0.55), int(g * 0.55), int(b * 0.55), 255))
        for x0, y0, x1, y1 in strokes:
            d.rectangle([x0, y0, x1, y1], fill=(r, g, b, 255))
        return to_png(im.tobytes(), 256, 256)

    def school_texture(self, name):
        """(11-06) the school crossing sign, the way the city's are: a fluorescent yellow-green pentagon, black-edged, two children walking in black
        (drawn by code), clear round it; and its plate, SCHOOL. 256 x 256 and 256 x 64"""
        from PIL import Image, ImageDraw
        YG, INK = (206, 232, 30, 255), (16, 16, 16, 255)
        if name.endswith("plate"):
            im = Image.new("RGBA", (256, 64), YG)
            d = ImageDraw.Draw(im)
            d.rectangle([3, 3, 252, 60], outline=INK, width=4)
            f = self.sign_font(44)
            x0, y0, x1, y1 = d.textbbox((0, 0), "SCHOOL", font=f)
            d.text(((256 - (x1 - x0)) / 2 - x0, (64 - (y1 - y0)) / 2 - y0), "SCHOOL", font=f, fill=INK)
            return to_png(im.tobytes(), 256, 64)
        im = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        pent = [(128, 6), (250, 92), (250, 250), (6, 250), (6, 92)]
        d.polygon(pent, fill=INK)
        d.polygon([(128, 16), (240, 96), (240, 240), (16, 240), (16, 96)], fill=YG)
        for cx, s_ in ((96, 1.0), (158, 0.92)):
            hy = 108
            d.ellipse([cx - 11 * s_, hy - 11 * s_, cx + 11 * s_, hy + 11 * s_], fill=INK)
            d.polygon([(cx - 13 * s_, hy + 14 * s_), (cx + 13 * s_, hy + 14 * s_), (cx + 9 * s_, hy + 62 * s_), (cx - 9 * s_, hy + 62 * s_)], fill=INK)
            d.line([(cx - 4 * s_, hy + 60 * s_), (cx - 18 * s_, hy + 108 * s_)], fill=INK, width=int(9 * s_))
            d.line([(cx + 4 * s_, hy + 60 * s_), (cx + 16 * s_, hy + 108 * s_)], fill=INK, width=int(9 * s_))
            d.line([(cx - 12 * s_, hy + 20 * s_), (cx - 26 * s_, hy + 54 * s_)], fill=INK, width=int(7 * s_))
            d.line([(cx + 12 * s_, hy + 20 * s_), (cx + 24 * s_, hy + 50 * s_)], fill=INK, width=int(7 * s_))
        return to_png(im.tobytes(), 256, 256)

    def sign_font(self, size):
        """(11-06) the signs' letters: Be Vietnam Pro Bold (fonts/, SIL Open Font License), which has every Vietnamese letter; Pillow's own if it's gone"""
        from PIL import ImageFont
        if not hasattr(self, "_fonts"):
            self._fonts = {}
        if size not in self._fonts:
            p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "BeVietnamPro-Bold.ttf")
            self._fonts[size] = ImageFont.truetype(p, size) if os.path.exists(p) else ImageFont.load_default(size=size)
        return self._fonts[size]

    def street(self):
        st = self.st
        if not st.get("street"):
            return
        cells = self.cells
        out = {}
        made = {}

        def cell(e, n):
            ci, cj, ok = cells.cell_of(np.array([e]), np.array([n]))
            return (int(ci[0]), int(cj[0])) if ok[0] else None

        def count(k):
            made[k] = made.get(k, 0) + 1
        roads = [l for l in self.lines if l["kind"] == "road" and not l["bridge"] and len(l["C"]) >= 2]
        ra = np.concatenate([l["C"][:-1] for l in roads]) if roads else np.zeros((0, 2))
        rb = np.concatenate([l["C"][1:] for l in roads]) if roads else np.zeros((0, 2))
        rw = np.concatenate([np.full(len(l["C"]) - 1, l["w"] / 2) for l in roads]) if roads else np.zeros(0)
        rwalk = np.concatenate([np.full(len(l["C"]) - 1, 1.0 if l["walks"] else 0.0) for l in roads]) if roads else np.zeros(0)

        def nearest_road(e, n, reach=30.0):
            """the nearest road's point to (e, n), its half-width, and whether it has sidewalks drawn: None if none within reach"""
            if not len(ra):
                return None
            near = np.nonzero((np.minimum(ra[:, 0], rb[:, 0]) <= e + reach) & (np.maximum(ra[:, 0], rb[:, 0]) >= e - reach)
                              & (np.minimum(ra[:, 1], rb[:, 1]) <= n + reach) & (np.maximum(ra[:, 1], rb[:, 1]) >= n - reach))[0]
            if not len(near):
                return None
            dist, tt = S.seg_dist(np.array([e]), np.array([n]), ra[near, 0], ra[near, 1], rb[near, 0], rb[near, 1])
            k = int(np.argmin(dist[0]))
            q = near[k]
            px, py = ra[q] + (rb[q] - ra[q]) * tt[0, k]
            return float(px), float(py), float(rw[q]), bool(rwalk[q]), float(dist[0, k])
        # (11-10) THE MEDIAN OF A DIVIDED STREET. OpenStreetMap draws a divided street as two one-way ways, and everything stood "at the curb" of a way
        # was stood at both curbs of each: a red lantern post and a trolley pole in the middle of S Jackson St at 8th Ave S, ridden into on 11-10. A
        # spot is in a median when the nearest roadway is on one hand of it and another way of the same street is on the other, both within dual_m:
        # nothing is stood there by rule (what the map itself puts there stays)
        rid = np.concatenate([np.full(len(l["C"]) - 1, k) for k, l in enumerate(roads)]) if roads else np.zeros(0, dtype=int)
        _names = {}
        rname = np.array([_names.setdefault(l["t"].get("name") or l["t"].get("ref") or "", len(_names)) if (l["t"].get("name") or l["t"].get("ref")) else -1
                          for l in roads], dtype=int) if roads else np.zeros(0, dtype=int)
        dual_m = float(st.get("dual_m", 25.0))

        def in_median(e, n):
            if not len(ra):
                return False
            near = np.nonzero((np.minimum(ra[:, 0], rb[:, 0]) <= e + dual_m) & (np.maximum(ra[:, 0], rb[:, 0]) >= e - dual_m)
                              & (np.minimum(ra[:, 1], rb[:, 1]) <= n + dual_m) & (np.maximum(ra[:, 1], rb[:, 1]) >= n - dual_m))[0]
            if len(near) < 2:
                return False
            dist, tt = S.seg_dist(np.array([e]), np.array([n]), ra[near, 0], ra[near, 1], rb[near, 0], rb[near, 1])
            d, tt = dist[0], tt[0]
            k0 = int(np.argmin(d))
            a = int(rid[near[k0]])
            if d[k0] > dual_m or rname[a] < 0:
                return False
            other = (rid[near] != a) & (rname[rid[near]] == rname[a]) & (d <= dual_m)
            if not other.any():
                return False
            foot = ra[near] + (rb[near] - ra[near]) * tt[:, None] - np.array([e, n])   # from the spot to each piece's nearest point
            dots = foot[other, 0] * foot[k0, 0] + foot[other, 1] * foot[k0, 1]
            return bool((dots < -0.3 * d[k0] * d[other]).any())
        # the junctions: a node two roads cross at, or three meet at
        deg, ends = {}, {}
        for l in roads:
            for nid in l["ids"]:
                deg[nid] = deg.get(nid, 0) + 1
            for nid in (l["ids"][0], l["ids"][-1]):
                ends[nid] = ends.get(nid, 0) + 1
        junction = {nid for nid, dg in deg.items() if dg >= 3 or (dg == 2 and ends.get(nid, 0) < 2)}
        jxy = np.array([[self.ne[nid], self.nn[nid]] for nid in junction if nid in self.ne]) if junction else np.zeros((0, 2))
        g = lambda e, n: float(self.field.sample(np.array([e]), np.array([n]))[0])
        # ---- lamps: the map's, and along the arterials where it has none
        lamps = [(self.ne[nid], self.nn[nid]) for nid, tg in self.ntags.items() if tg.get("highway") == "street_lamp" and nid in self.ne]
        lamp_xy = np.array(lamps) if lamps else np.zeros((0, 2))
        mapped_n = len(lamps)
        every = float(st["lamp_every_m"])
        for l in roads:
            if l["hw"] not in ("primary", "secondary", "tertiary", "primary_link", "secondary_link", "trunk"):
                continue
            C = l["C"]
            sarc = S.arclen(C)
            R = S.rights(C)
            for k, at in enumerate(np.arange(every / 2, sarc[-1], every)):
                e0, n0 = float(np.interp(at, sarc, C[:, 0])), float(np.interp(at, sarc, C[:, 1]))
                if len(jxy) and (np.hypot(jxy[:, 0] - e0, jxy[:, 1] - n0) < 12.0).any():
                    continue
                if len(lamp_xy) and (np.hypot(lamp_xy[:, 0] - e0, lamp_xy[:, 1] - n0) < 25.0).any():
                    continue
                sd = 1.0 if k % 2 == 0 else -1.0
                re_, rn_ = float(np.interp(at, sarc, R[:, 0])), float(np.interp(at, sarc, R[:, 1]))
                off = l["w"] / 2 + (st["sidewalk_m"] - 0.5 if l["walks"] else 0.8)
                if in_median(e0 + sd * re_ * off, n0 + sd * rn_ * off):
                    sd = -sd   # (11-10) not in a divided street's median: this one stands at the outer curb
                    if in_median(e0 + sd * re_ * off, n0 + sd * rn_ * off):
                        continue
                lamps.append((e0 + sd * re_ * off, n0 + sd * rn_ * off))
        # (11-06) a district's own lamps (the config's `districts`): Chinatown-International District's red lantern posts (the red lamp posts
        # are the neighbourhood's own mark), along its streets every lantern_every_m on each side, the sides staggered, in place of the
        # city's grey lamps there; a lamp the map has there is a lantern post too
        lantern_every = float(st.get("lantern_every_m", 24.0))
        lanterns = []
        if any(d.get("lamps") == "lantern" for d in self.districts()):
            for l in roads:
                if l["hw"] not in ("primary", "secondary", "tertiary", "residential", "unclassified", "living_street", "primary_link", "secondary_link",
                                   "tertiary_link"):
                    continue
                C = l["C"]
                sarc = S.arclen(C)
                if sarc[-1] < 6.0:
                    continue
                R = S.rights(C)
                for sd in (1.0, -1.0):
                    for at in np.arange(lantern_every / 4 + (lantern_every / 2 if sd < 0 else 0.0), sarc[-1], lantern_every):
                        e0, n0 = float(np.interp(at, sarc, C[:, 0])), float(np.interp(at, sarc, C[:, 1]))
                        if len(jxy) and (np.hypot(jxy[:, 0] - e0, jxy[:, 1] - n0) < 9.0).any():
                            continue
                        re_, rn_ = float(np.interp(at, sarc, R[:, 0])), float(np.interp(at, sarc, R[:, 1]))
                        off = l["w"] / 2 + (0.6 if l["walks"] else 0.8)
                        e1, n1 = e0 + sd * re_ * off, n0 + sd * rn_ * off
                        if self.district_of(e1, n1, "lamps") == "lantern" and not in_median(e1, n1):   # (11-10) none in a divided street's median
                            lanterns.append((e1, n1))
        for q, (e, n) in enumerate(lanterns):
            key = cell(e, n)
            nr = nearest_road(e, n)
            if key is None or nr is None or nr[4] < nr[2] + 0.3 or self.in_road(e, n, 0.2):
                continue
            px, py, _w, _wk, dist = nr
            self.lantern_post(key, e, n, g(e, n), (px - e) / max(dist, 1e-6), (py - n) / max(dist, 1e-6), out, q)
            count("lantern posts")
        for q, (e, n) in enumerate(lamps):
            key = cell(e, n)
            nr = nearest_road(e, n)
            if key is None or nr is None:
                continue
            px, py, _w, _wk, dist = nr
            if dist < _w + 0.3 or self.in_road(e, n, 0.2):
                continue   # in the road: a mapped lamp on a centreline node, or (11-10) in a road that isn't the nearest one's line
            h = g(e, n)
            fe, fn = (px - e) / max(dist, 1e-6), (py - n) / max(dist, 1e-6)
            if lanterns and self.district_of(e, n, "lamps") == "lantern":
                if q < mapped_n:
                    self.lantern_post(key, e, n, h, fe, fn, out, q)
                    count("lantern posts")
                continue
            arm = min(2.2, max(1.0, dist - _w + 1.2))
            self._prism(key, "pole#707579", e, n, h - 0.2, h + 8.6, 0.1, 0.07, 6, out)
            self._box(key, "pole#707579", e + fe * arm / 2, n + fn * arm / 2, h + 8.45, h + 8.6, arm, 0.1, fe, fn, out)
            self._box(key, "lamp#4a4d50", e + fe * arm, n + fn * arm, h + 8.3, h + 8.55, 0.7, 0.32, fe, fn, out)
            count("lamps")
        sig_poles = []
        # ---- signals: at each of the map's signal nodes, a pole on the right of each road coming in, its arm over the lanes, its head to the traffic
        for nid, tg in self.ntags.items():
            if tg.get("highway") != "traffic_signals" or nid not in self.ne:
                continue
            e0, n0 = self.ne[nid], self.nn[nid]
            for l in roads:
                if nid not in l["ids"]:
                    continue
                k = l["ids"].index(nid)
                P = l["P"]
                appr = []
                if k > 0 and l["t"].get("oneway") != "-1":
                    appr.append(P[k] - P[k - 1])
                if k < len(P) - 1 and l["t"].get("oneway") not in ("yes", "true", "1"):
                    appr.append(P[k] - P[k + 1])
                for f in appr:
                    L = float(np.hypot(*f))
                    if L < 1e-6:
                        continue
                    fe, fn = f[0] / L, f[1] / L
                    re_, rn_ = fn, -fe
                    half = l["w"] / 2 if l["t"].get("oneway") in ("yes", "true", "1") else l["w"] / 4
                    back = min(9.0, 0.8 * L)
                    e, n = e0 - fe * back + re_ * (l["w"] / 2 + 0.9), n0 - fn * back + rn_ * (l["w"] / 2 + 0.9)
                    # (11-10) never in a roadway: where the way's last piece is short, or the street it meets is wide, that was inside the cross street
                    # (the audit: 8 of them, 3 to 6 m out into Jackson, Dearborn, 12th and Boren). Back along its own street until it's clear of every road
                    for _try in range(24):
                        if not self.in_road(e, n, 0.4):
                            break
                        back += 1.0
                        e, n = e0 - fe * back + re_ * (l["w"] / 2 + 0.9), n0 - fn * back + rn_ * (l["w"] / 2 + 0.9)
                    if self.in_road(e, n, 0.4):
                        continue
                    key = cell(e, n)
                    if key is None:
                        continue
                    h = g(e, n)
                    dk = self.deck_over(e, n)
                    if dk is not None and dk < h + 6.6:
                        continue   # (11-10) a bridge's deck is over it, lower than the pole: it stood up through a ramp
                    arm = l["w"] / 2 + 0.9 - half * 0.5
                    self._prism(key, "pole#6a6e70", e, n, h - 0.2, h + 6.2, 0.15, 0.12, 6, out)
                    sig_poles.append((e, n))
                    ae, an = e - re_ * arm / 2, n - rn_ * arm / 2
                    self._box(key, "pole#6a6e70", ae, an, h + 5.9, h + 6.1, 0.14, arm, fe, fn, out)
                    he, hn = e - re_ * arm, n - rn_ * arm
                    self._box(key, "signal#1c1d1f", he + fe * 0.02, hn + fn * 0.02, h + 4.7, h + 5.95, 0.06, 0.62, fe, fn, out)
                    self._box(key, "signal#2b2b2b", he + fe * 0.18, hn + fn * 0.18, h + 4.8, h + 5.85, 0.3, 0.38, fe, fn, out)
                    for y0, colour in ((5.45, "c0392b"), (5.1, "d4a017"), (4.75, "2e8b57")):
                        self._box(key, "light#" + colour, he + fe * 0.35, hn + fn * 0.35, h + y0 + 0.08, h + y0 + 0.3, 0.04, 0.22, -fe, -fn, out)
                    count("signals")
        shelters = []
        # ---- the small things at the map's nodes: stop signs, bus stops, benches, bins, bollards
        for nid, tg in self.ntags.items():
            if nid not in self.ne:
                continue
            e, n = self.ne[nid], self.nn[nid]
            what = "stop" if tg.get("highway") == "stop" else "bus" if tg.get("highway") == "bus_stop" else "bench" if tg.get("amenity") == "bench" else \
                   "bin" if tg.get("amenity") == "waste_basket" else "bollard" if tg.get("barrier") == "bollard" else None
            if not what:
                continue
            nr = nearest_road(e, n)
            if what in ("stop", "bus") and nr is not None:
                px, py, w2, wk, dist = nr
                # (11-10) a stop sign's node is on its street's own line, where a driver stops: the sign stands at the kerb on the right of the way he
                # comes, which is toward the nearer junction (the map's `direction` says it outright when it does). It was pushed straight out from
                # the NEAREST road's line, which near a corner is the cross street's: 11 signs stood 3 m out in King, Weller, Lane, Main and Washington
                on = next((l for l in roads if nid in l["ids"]), None) if what == "stop" else None
                if on is not None:
                    k = on["ids"].index(nid)
                    P = on["P"]
                    f = P[min(k + 1, len(P) - 1)] - P[max(k - 1, 0)]
                    L = float(np.hypot(*f)) or 1.0
                    fe, fn = f[0] / L, f[1] / L
                    way = tg.get("direction")
                    if way not in ("forward", "backward"):
                        J = self.junctions()
                        near = lambda s: float(np.hypot(J[:, 0] - (e + s * fe * 6.0), J[:, 1] - (n + s * fn * 6.0)).min()) if len(J) else 0.0
                        way = "forward" if near(1.0) <= near(-1.0) else "backward"
                    if way == "backward":
                        fe, fn = -fe, -fn
                    re_, rn_ = fn, -fe
                    spot = None
                    for back in range(0, 20):
                        ce, cn = e - fe * back + re_ * (on["w"] / 2 + 0.8), n - fn * back + rn_ * (on["w"] / 2 + 0.8)
                        if not self.in_road(ce, cn, 0.3):
                            spot = (ce, cn)
                            break
                    if spot is None:
                        continue
                    e, n = spot
                elif dist < w2 + 0.6:   # a node on the road's line: to its kerb, on the right
                    dx, dy = (e - px, n - py) if dist > 1e-3 else (1.0, 0.0)
                    L = math.hypot(dx, dy) or 1.0
                    e, n = px + dx / L * (w2 + 0.7), py + dy / L * (w2 + 0.7)
                    for _try in range(12):   # (11-10) and on out of any other road it's still in
                        if not self.in_road(e, n, 0.3):
                            break
                        e, n = e + dx / L, n + dy / L
            key = cell(e, n)
            if key is None:
                continue
            h = g(e, n)
            fe, fn = 1.0, 0.0
            if nr is not None:
                px, py = nr[0], nr[1]
                L = math.hypot(px - e, py - n) or 1.0
                fe, fn = (px - e) / L, (py - n) / L
            if what == "stop":
                self._prism(key, "pole#8a8f93", e, n, h - 0.2, h + 2.2, 0.04, 0.04, 4, out)
                self._box(key, "stopsign#b3261e", e, n, h + 1.95, h + 2.65, 0.05, 0.7, -fn, fe, out)
            elif what == "bus":
                self._prism(key, "pole#8a8f93", e, n, h - 0.2, h + 2.9, 0.05, 0.05, 4, out)
                self._box(key, "busplate#e8e6e0", e, n, h + 2.2, h + 2.85, 0.05, 0.45, -fn, fe, out)
                self._box(key, "busplate#2f4a7a", e, n, h + 2.05, h + 2.2, 0.06, 0.45, -fn, fe, out)
                if tg.get("shelter") == "yes":
                    shelters.append((e, n, fe, fn, tg.get("bench") == "yes", tg.get("bin") == "yes"))
            elif what == "bench":
                self._box(key, "bench#8b6a4a", e, n, h + 0.42, h + 0.5, 1.6, 0.45, -fn, fe, out)
                self._box(key, "bench#8b6a4a", e - fe * 0.2, n - fn * 0.2, h + 0.5, h + 0.95, 1.6, 0.06, -fn, fe, out)
                self._box(key, "pole#4a4d50", e, n, h - 0.1, h + 0.42, 1.4, 0.35, -fn, fe, out)
            elif what == "bin":
                self._prism(key, "bin#2f4a3a", e, n, h - 0.1, h + 0.95, 0.27, 0.25, 8, out)
            else:
                self._prism(key, "pole#e8e8e2", e, n, h - 0.1, h + 0.95, 0.1, 0.1, 6, out)
            count(what)
        # ---- (11-06) a bus stop the map says has a shelter: glass at its back and ends, a roof on four posts, a bench inside and a bin beside if it
        # says so, behind the stop's pole and a little along the kerb
        for e0, n0, fe, fn, bench, bin_ in shelters:
            ae, an = -fn, fe   # along the kerb
            be, bn = -fe, -fn  # back, away from the road
            e, n = e0 + be * 1.55 + ae * 1.2, n0 + bn * 1.55 + an * 1.2
            key = cell(e, n)
            if key is None:
                continue
            h = g(e, n)
            L, D, Hs = 3.8, 1.5, 2.45
            # (11-06) glass you see through: a sheer mesh (#rrggbbaa), drawn blended
            self._box(key, "glass#b8ccd660", e + be * D / 2, n + bn * D / 2, h + 0.12, h + Hs - 0.1, L, 0.03, ae, an, out)
            for sd in (-1.0, 1.0):
                self._box(key, "glass#b8ccd660", e + ae * sd * L / 2 + be * D * 0.2, n + an * sd * L / 2 + bn * D * 0.2, h + 0.12, h + Hs - 0.1, D * 0.6, 0.03, be, bn, out)
                for fb in (-1.0, 1.0):
                    self._prism(key, "pole#3b3f43", e + ae * sd * L / 2 + be * fb * D / 2, n + an * sd * L / 2 + bn * fb * D / 2, h - 0.1, h + Hs, 0.04, 0.04, 4, out)
            self._box(key, "shelter#3b3f43", e - be * 0.1, n - bn * 0.1, h + Hs - 0.02, h + Hs + 0.1, L + 0.3, D + 0.4, ae, an, out)
            if bench:
                self._box(key, "bench#5a5f63", e + be * (D / 2 - 0.3), n + bn * (D / 2 - 0.3), h + 0.42, h + 0.48, L * 0.6, 0.4, ae, an, out)
                self._box(key, "pole#3b3f43", e + be * (D / 2 - 0.3), n + bn * (D / 2 - 0.3), h - 0.05, h + 0.42, L * 0.5, 0.3, ae, an, out)
            if bin_:
                self._prism(key, "bin#2f4a3a", e + ae * (L / 2 + 0.55), n + an * (L / 2 + 0.55), h - 0.1, h + 0.95, 0.27, 0.25, 8, out)
            count("bus shelters")
        # ---- (11-06) a tram stop's platform the map draws as an area: a slab at kerb height, a canopy over a windscreen if it says there's a shelter,
        # a bench if it says so, and a ticket machine at one end
        for wid in sorted(self.ways):
            w = self.ways[wid]
            t = w.get("tags", {})
            if not (t.get("railway") == "platform" or (t.get("public_transport") == "platform" and t.get("tram") == "yes")) or t.get("area") != "yes":
                continue
            ids, Pp = self.pts(w)
            if len(Pp) < 4 or ids[0] != ids[-1]:
                continue
            ring = Pp[:-1]
            if S.area(ring) < 0:
                ring = ring[::-1]
            mid = ring.mean(axis=0)
            key = cell(mid[0], mid[1])
            if key is None:
                continue
            h0 = min(g(p[0], p[1]) for p in ring) + st["lift"]["road"]
            top = h0 + 0.3
            tris, _ok = S.earclip(ring)
            T = [((ring[a][0], top, ring[a][1]), (ring[b][0], top, ring[b][1]), (ring[c][0], top, ring[c][1])) for a, b, c in tris]
            for k in range(len(ring)):
                (ax, ay), (bx, by) = ring[k], ring[(k + 1) % len(ring)]
                A, B, C2, D2 = (ax, h0 - 0.1, ay), (bx, h0 - 0.1, by), (bx, top, by), (ax, top, ay)
                T += [(A, B, C2), (A, C2, D2)]
            out.setdefault((key, "platform#b5b0a6"), []).append(self._orient(np.array(T, dtype=np.float64)))
            # its long way, and its size along and across
            ed = [ring[(k + 1) % len(ring)] - ring[k] for k in range(len(ring))]
            lk = max(range(len(ed)), key=lambda k: float(np.hypot(*ed[k])))
            ae, an = ed[lk] / (float(np.hypot(*ed[lk])) or 1.0)
            pa = (ring - mid) @ np.array([ae, an])
            pc = (ring - mid) @ np.array([an, -ae])
            Lp, Wp = float(pa.max() - pa.min()), float(pc.max() - pc.min())
            if t.get("shelter") == "yes":
                Lc = min(6.0, 0.45 * Lp)
                for sd in (-1.0, 1.0):
                    self._prism(key, "pole#3b3f43", mid[0] + ae * sd * Lc * 0.4, mid[1] + an * sd * Lc * 0.4, top - 0.05, top + 2.75, 0.06, 0.06, 6, out)
                self._box(key, "shelter#3b3f43", mid[0], mid[1], top + 2.7, top + 2.85, Lc, min(1.8, Wp * 0.95), ae, an, out)
                self._box(key, "glass#b8ccd660", mid[0], mid[1], top + 0.15, top + 2.2, Lc * 0.8, 0.03, ae, an, out)
            if t.get("bench") == "yes":
                self._box(key, "bench#5a5f63", mid[0] + an * 0.35, mid[1] - ae * 0.35, top + 0.42, top + 0.48, min(2.0, Lp * 0.2), 0.4, ae, an, out)
            self._box(key, "unit#2f4a7a", mid[0] + ae * (Lp / 2 - 1.0), mid[1] + an * (Lp / 2 - 1.0), top, top + 1.7, 0.45, 0.55, ae, an, out)
            count("tram platforms")
        # ---- (11-06) fire hydrants: a pillar in the map's colour, yellow if it says none, at the kerb; flagpoles, with the flag the map names (the
        # style's flags) or a plain one in its colour
        HYD = {"yellow": "d4a62a", "red": "b03a2e", "green": "3f7a4a", "white": "e8e6e0", "silver": "b8bcbf", "blue": "3a5f9a", "black": "2b2b2b"}
        for nid in sorted(self.ntags):
            tg = self.ntags[nid]
            if nid not in self.ne:
                continue
            if tg.get("emergency") == "fire_hydrant" and tg.get("fire_hydrant:type", "pillar") == "pillar":
                e, n = self.ne[nid], self.nn[nid]
                nr = nearest_road(e, n)
                if nr is not None and nr[4] < nr[2] + 0.4:
                    px, py, w2, _wk, dist = nr
                    dx, dy = (e - px, n - py) if dist > 1e-3 else (1.0, 0.0)
                    L = math.hypot(dx, dy) or 1.0
                    e, n = px + dx / L * (w2 + 0.5), py + dy / L * (w2 + 0.5)
                key = cell(e, n)
                if key is None:
                    continue
                h = g(e, n)
                col = HYD.get((tg.get("colour") or "yellow").lower(), "d4a62a")
                self._prism(key, "hydrant#" + col, e, n, h - 0.05, h + 0.6, 0.13, 0.12, 8, out)
                self._prism(key, "hydrant#" + col, e, n, h + 0.6, h + 0.74, 0.15, 0.05, 8, out)
                self._box(key, "hydrant#" + col, e, n, h + 0.38, h + 0.5, 0.44, 0.11, 1.0, 0.0, out)
                count("hydrants")
            elif tg.get("man_made") == "flagpole":
                e, n = self.ne[nid], self.nn[nid]
                key = cell(e, n)
                if key is None:
                    continue
                h = g(e, n)
                Hf = metres(tg.get("height")) or 7.5
                self._prism(key, "pole#d6d8d8", e, n, h - 0.2, h + Hf, 0.07, 0.045, 8, out)
                self._prism(key, "pole#c9a227", e, n, h + Hf, h + Hf + 0.12, 0.08, 0.02, 6, out)
                bands = (st.get("flags") or {}).get(tg.get("flag:wikidata"))
                if not bands:
                    bands = [[self.NAMED.get((tg.get("flag:colour") or "white").lower(), "e8e6e0"), 1]]
                fw, fh = 1.5, 1.0
                tot = float(sum(b[1] for b in bands))
                top = h + Hf - 0.08
                fe, fn = 0.6, 0.8   # it flies to the north-east: Seattle's wind is mostly from the south-west
                for colr, sh in bands:
                    dh = fh * float(sh) / tot
                    self._box(key, "flag#" + colr, e + fe * (fw / 2 + 0.06), n + fn * (fw / 2 + 0.06), top - dh, top, fw, 0.02, fe, fn, out)
                    top -= dh
                count("flags")
        # ---- fences, walls, retaining walls and guard rails, from the map's barrier ways
        fence_lines = []   # (11-06) the barriers' lines, so a lot's cars don't stand across one
        BAR = {"fence": (1.8, "fence#6e7275", 0.05), "wall": (1.6, "wall#9a958c", 0.3), "retaining_wall": (1.4, "wall#8e8a82", 0.4),
               "guard_rail": (0.8, "rail#a7abae", 0.08), "jersey_barrier": (0.85, "wall#b0aca3", 0.5)}
        # (11-10) A BARRIER NEVER STANDS ACROSS A WAY THE MAP DRAWS THROUGH IT. Ridden into on S Jackson St under I-5: way 1478810952, one of the viaduct's
        # two edge walls, drawn with no layer where its twin (way 1478810961) says layer 1, so it was built on the ground, 1.6 m high, across S Jackson St
        # and S King St. Two rules. A barrier that crosses a way on the ground and runs beside one that says its layer (within twin_m for most of its
        # length) is up on that structure too, and left out like its twin (a deck gets its own low walls by rule). Any other barrier is cut where it
        # crosses a road or a path on the ground, a gap the way's own band wide: what the map draws through a fence is its gate. A short jersey barrier
        # (closure_m or less) is left whole: that one is there to close the street
        ways_g = [l for l in self.lines if l["kind"] in ("road", "path") and not l["bridge"] and len(l["P"]) >= 2]
        xa = np.concatenate([l["P"][:-1] for l in ways_g]) if ways_g else np.zeros((0, 2))
        xb = np.concatenate([l["P"][1:] for l in ways_g]) if ways_g else np.zeros((0, 2))
        xcore = np.concatenate([np.full(len(l["P"]) - 1, l["w"] / 2 + (st["kerb_m"] if l["kind"] == "road" else 0.2) + (st["sidewalk_m"] if l["walks"] else 0.0))
                                for l in ways_g]) if ways_g else np.zeros(0)
        twin_m, closure_m = float(st.get("barrier_twin_m", 4.0)), float(st.get("barrier_closure_m", 30.0))
        lay = []
        for wid in sorted(self.ways):
            t = self.ways[wid].get("tags", {})
            if t.get("barrier") in BAR and (t.get("layer") not in (None, "0") or t.get("bridge")):
                _ids, Pl = self.pts(self.ways[wid])
                if len(Pl) >= 2:
                    lay.append(S.densify(Pl, 1.0)[0])
        lay_xy = np.concatenate(lay) if lay else np.zeros((0, 2))

        def crossings(P):
            """where a line crosses a way on the ground: (metres along it, half the gap to leave: the way's band along the line's own direction)"""
            got, s0 = [], 0.0
            for k in range(len(P) - 1):
                p, r = P[k], P[k + 1] - P[k]
                L = float(np.hypot(r[0], r[1]))
                if L < 1e-6:
                    continue
                lo, hi = np.minimum(P[k], P[k + 1]), np.maximum(P[k], P[k + 1])
                near = np.nonzero((np.minimum(xa[:, 0], xb[:, 0]) <= hi[0]) & (np.maximum(xa[:, 0], xb[:, 0]) >= lo[0])
                                  & (np.minimum(xa[:, 1], xb[:, 1]) <= hi[1]) & (np.maximum(xa[:, 1], xb[:, 1]) >= lo[1]))[0]
                for q in near:
                    sx, sy = xb[q] - xa[q]
                    den = r[0] * sy - r[1] * sx
                    if abs(den) < 1e-9:
                        continue
                    qx, qy = xa[q] - p
                    tt, uu = (qx * sy - qy * sx) / den, (qx * r[1] - qy * r[0]) / den
                    if 0.0 <= tt <= 1.0 and 0.0 <= uu <= 1.0:
                        sin = abs(den) / (L * max(math.hypot(sx, sy), 1e-9))
                        got.append((s0 + tt * L, min(float(xcore[q]) / max(sin, 0.25), 14.0)))
                s0 += L
            return got

        def beside_layered(P):
            """the share of a line that runs within twin_m of a barrier that says its layer"""
            if not len(lay_xy):
                return 0.0
            Q = S.densify(P, 2.0)[0]
            near = lay_xy[(lay_xy[:, 0] >= Q[:, 0].min() - twin_m) & (lay_xy[:, 0] <= Q[:, 0].max() + twin_m)
                          & (lay_xy[:, 1] >= Q[:, 1].min() - twin_m) & (lay_xy[:, 1] <= Q[:, 1].max() + twin_m)]
            if not len(near):
                return 0.0
            return float((np.hypot(Q[:, None, 0] - near[None, :, 0], Q[:, None, 1] - near[None, :, 1]).min(axis=1) <= twin_m).mean())

        def kept(P, gaps):
            """the stretches of a line left when the gaps are taken out, each a line of its own"""
            s = S.arclen(P)
            iv = sorted((max(0.0, a - h), min(float(s[-1]), a + h)) for a, h in gaps)
            joined = []
            for a, b in iv:
                if joined and a <= joined[-1][1]:
                    joined[-1][1] = max(joined[-1][1], b)
                else:
                    joined.append([a, b])
            spans, at = [], 0.0
            for a, b in joined:
                if a - at > 0.6:
                    spans.append((at, a))
                at = b
            if float(s[-1]) - at > 0.6:
                spans.append((at, float(s[-1])))
            pt = lambda x: [float(np.interp(x, s, P[:, 0])), float(np.interp(x, s, P[:, 1]))]
            return [np.array([pt(a)] + [P[k].tolist() for k in range(len(P)) if a < s[k] < b] + [pt(b)]) for a, b in spans]
        up_twins, cut_ways, cut_gaps, cut_in, snapped, by_cls = [], 0, 0, 0, 0, {}
        self.rail_tops = []
        barrier_in_m, barrier_snap_m = float(st.get("barrier_in_m", 0.2)), float(st.get("barrier_snap_m", 3.2))
        for wid in sorted(self.ways):
            w = self.ways[wid]
            t = w.get("tags", {})
            spec = BAR.get(t.get("barrier"))
            if not spec or t.get("layer") not in (None, "0") or t.get("bridge"):
                continue
            ids, P = self.pts(w)
            if len(P) < 2:
                continue
            gaps = crossings(P) if len(xa) else []
            if gaps and beside_layered(P) >= 0.7:
                up_twins.append(wid)
                continue
            if gaps and not (t.get("barrier") == "jersey_barrier" and float(S.arclen(P)[-1]) <= closure_m):
                cut_ways += 1
                cut_gaps += len(gaps)
                spans = kept(P, gaps)
            else:
                spans = [P]
            # (11-10) and none of it stands in a road's own lanes: the audit found 98 places where a retaining wall or a fence ran along inside a
            # carriageway without ever crossing its line (the road drawn wider than the gap the map's wall leaves it: most of them I-5's own edge
            # walls). Where it's inside a road's band by no more than barrier_snap_m it's moved out to the band's edge, so the road keeps its wall;
            # deeper in than that it's left out. So is what would stand in a bridge's lanes where its deck is down at the ground. A jersey barrier
            # is left alone: in a road is where those are put
            if t.get("barrier") != "jersey_barrier":
                trimmed = []
                for Pk in spans:
                    Q = S.densify(Pk, 1.0)[0].copy()
                    ok = np.ones(len(Q), dtype=bool)
                    moved = 0
                    for k in range(len(Q)):
                        x, y = float(Q[k, 0]), float(Q[k, 1])
                        d, gx, gy = self.road_in(x, y)
                        if d > barrier_in_m:
                            cls = self.road_at(x, y)
                            if d <= barrier_snap_m and (gx or gy):
                                x, y = x - gx * (d + 0.3), y - gy * (d + 0.3)
                                if self.road_in(x, y)[0] > barrier_in_m:
                                    ok[k] = False
                                else:
                                    Q[k] = (x, y)
                                    moved += 1
                            else:
                                ok[k] = False
                            where = by_cls.setdefault((t.get("barrier"), cls), [0, 0])
                            where[0 if ok[k] else 1] += 1
                        if ok[k]:
                            dk = self.deck_over(x, y)
                            if dk is not None and self.road_in(x, y, bridge=True)[0] > barrier_in_m and dk + st["deck_m"] < g(x, y) + spec[0] + 0.5:
                                ok[k] = False   # in a bridge's lanes, its deck no higher than the barrier's top
                    if ok.all() and not moved:
                        trimmed.append(Pk)
                        continue
                    snapped += moved
                    cut_in += int((~ok).sum())
                    k0 = None
                    for k in range(len(Q) + 1):
                        if k < len(Q) and ok[k]:
                            k0 = k if k0 is None else k0
                        elif k0 is not None:
                            if k - k0 >= 2:   # every fourth point of what's left, and its last: the pieces are made 3 to 4 m long below
                                trimmed.append(Q[np.unique(np.concatenate([np.arange(k0, k, 4), [k - 1]]))])
                            k0 = None
                spans = trimmed
            hh, kind, th = spec
            # (11-06) a fence is what the map says it's made of: chain link (and wire) and bars are see-through, a wood fence a solid panel, and a fence
            # that says neither is chain link
            weave = None
            if t.get("barrier") == "fence":
                ft, mat = t.get("fence_type", ""), t.get("material", "")
                if ft in ("paling", "slatted", "wood", "panel", "split_rail", "hedge") or (not ft and mat == "wood"):
                    kind = "fence#7a5a40"
                elif ft in ("railing", "bars", "metal", "pole"):
                    weave, hh = "bars", 1.2
                else:
                    weave = "chainlink"
                hh = metres(t.get("height")) or hh
            run_u = 0.0
            if not weave and t.get("barrier") in ("wall", "retaining_wall", "guard_rail", "jersey_barrier"):
                # (11-11) its top, for a game that grinds (godot.py's sidecar): metres, east, height, north
                for Pk in spans:
                    Ct = S.densify(Pk, 2.0)[0]
                    self.rail_tops.append((t.get("barrier"), [(float(x), g(float(x), float(y)) + hh, float(y)) for x, y in Ct]))
            for Pk in spans:   # (11-10) the barrier's stretches between the ways that cross it: all of it, when none does
                Cb, _at = S.densify(Pk, 4.0 if weave != "chainlink" else 3.0)
                fence_lines.extend((Cb[k][0], Cb[k][1], Cb[k + 1][0], Cb[k + 1][1]) for k in range(len(Cb) - 1))
                for k in range(len(Cb) - 1):
                    (ax, ay), (bx, by) = Cb[k], Cb[k + 1]
                    me, mn = (ax + bx) / 2, (ay + by) / 2
                    key = cell(me, mn)
                    L = math.hypot(bx - ax, by - ay)
                    if key is None or L < 1e-3:
                        run_u += L
                        continue
                    fe, fn = (bx - ax) / L, (by - ay) / L
                    ga, gb = g(ax, ay), g(bx, by)
                    if weave:
                        tile = float(st.get(weave + "_tile_m", 0.5))
                        # the weave between the posts, its bottom on the ground at either end, its top a rail; both faces, so it's seen from either side
                        self._texquad(key, weave, [(ax, ga + 0.04, ay), (bx, gb + 0.04, by), (bx, gb + hh, by), (ax, ga + hh, ay)],
                                      [(run_u / tile, 0.0), ((run_u + L) / tile, 0.0), ((run_u + L) / tile, (hh - 0.04) / tile), (run_u / tile, (hh - 0.04) / tile)])
                        self._beam(key, "rail#9aa0a4", (ax, ga + hh, ay), (bx, gb + hh, by), 0.021, out)
                        if weave == "chainlink":
                            self._beam(key, "rail#9aa0a4", (ax, ga + 0.06, ay), (bx, gb + 0.06, by), 0.006, out)   # the tension wire along its foot
                        self._prism(key, "pole#8a9094", ax, ay, ga - 0.2, ga + hh + 0.06, 0.03, 0.03, 6, out)
                    else:
                        h0 = min(ga, gb)
                        self._box(key, kind, me, mn, h0 - 0.3, max(ga, gb) + hh, L, th, fe, fn, out)
                        if kind.startswith("fence"):
                            self._prism(key, "pole#5d6163", ax, ay, ga - 0.2, ga + hh + 0.1, 0.04, 0.04, 4, out)
                    run_u += L
                if weave and len(Cb) >= 2:
                    (ex, ey) = Cb[-1]
                    key = cell(ex, ey)
                    if key is not None:
                        self._prism(key, "pole#8a9094", ex, ey, g(ex, ey) - 0.2, g(ex, ey) + hh + 0.06, 0.03, 0.03, 6, out)
            count(t.get("barrier") + (" (%s)" % weave if weave else ""))
        if up_twins or cut_ways or cut_in:
            self.say("barriers: %d left out as up on a structure, each beside one that says its layer and across a way on the ground (%s); %d more cut where a road "
                     "or a path crosses them, %d gaps; %d m moved out to the edge of a road they ran inside, and %d m more left out for standing in its lanes" % (
                         len(up_twins), ", ".join("way %d" % x for x in up_twins[:12]), cut_ways, cut_gaps, snapped, cut_in))
            if by_cls:
                self.say("   in whose lanes (metres moved out, metres left out): " + ", ".join(
                    "%s in a %s %d, %d" % (b, c, v[0], v[1]) for (b, c), v in sorted(by_cls.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))))
        # ---- (11-06) the config's features: what a source shows that the map doesn't (the city's camera at 12th and Jackson), each with its `_source`.
        #   {"kind": "fence", "points": [{lat, lon}, ...], "height_m": 1.8}: a chain-link fence along them
        #   {"kind": "bin_pen", "at": {lat, lon}, "size_m": [w, d], "yaw_deg": 0, "bins": 7}: a chain-link pen of wheeled bins, its gate on its south side, trash
        #   heaped in front of it to the north
        def weave_seg(key, ax, ay, bx, by, hh, u0):
            """one panel of chain link from a to b, its posts, rail and tension wire; the metres along it after"""
            L = math.hypot(bx - ax, by - ay)
            if L < 1e-3:
                return u0
            ga, gb = g(ax, ay), g(bx, by)
            tile = float(st.get("chainlink_tile_m", 0.55))
            self._texquad(key, "chainlink", [(ax, ga + 0.04, ay), (bx, gb + 0.04, by), (bx, gb + hh, by), (ax, ga + hh, ay)],
                          [(u0 / tile, 0.0), ((u0 + L) / tile, 0.0), ((u0 + L) / tile, (hh - 0.04) / tile), (u0 / tile, (hh - 0.04) / tile)])
            self._beam(key, "rail#9aa0a4", (ax, ga + hh, ay), (bx, gb + hh, by), 0.021, out)
            self._beam(key, "rail#9aa0a4", (ax, ga + 0.06, ay), (bx, gb + 0.06, by), 0.006, out)
            self._prism(key, "pole#8a9094", ax, ay, ga - 0.2, ga + hh + 0.06, 0.03, 0.03, 6, out)
            self._prism(key, "pole#8a9094", bx, by, gb - 0.2, gb + hh + 0.06, 0.03, 0.03, 6, out)
            return u0 + L
        BIN = ["2f6b3a", "2f5f9a", "2a2b2d", "2f6b3a", "4a4d50", "2f5f9a", "2a2b2d", "2f6b3a"]
        # where a parked car can't stand (11-06: the lots' rule had put a car across the pen's fence,
        # the wrong spot for one): on a pen (its footprint and a car's half length past it) or across a fence, a source's or the map's (fence_lines, the barriers above)
        feature_clear, feature_lines, feature_rings = [], [], []   # rings: an area a feature says nothing stands in (its `area`)
        for fz in self.cfg.get("features", []):
            kind = fz.get("kind")
            if kind == "fence":
                pts = [self.place(dict(p, on="ground")) for p in fz.get("points", [])]
                u0 = 0.0
                for (ax, ay, _h1), (bx, by, _h2) in zip(pts[:-1], pts[1:]):
                    Cb, _at = S.densify(np.array([[ax, ay], [bx, by]]), 3.0)
                    for k in range(len(Cb) - 1):
                        key = cell(0.5 * (Cb[k][0] + Cb[k + 1][0]), 0.5 * (Cb[k][1] + Cb[k + 1][1]))
                        if key is not None:
                            if fz.get("razor"):   # (11-06) coiled razor wire along its top
                                self.razor_seg(key, Cb[k][0], Cb[k][1], Cb[k + 1][0], Cb[k + 1][1], g, float(fz.get("height_m", 1.8)), u0)
                            u0 = weave_seg(key, Cb[k][0], Cb[k][1], Cb[k + 1][0], Cb[k + 1][1], float(fz.get("height_m", 1.8)), u0)
                    feature_lines.append((ax, ay, bx, by))
                if fz.get("area"):
                    feature_rings.append(np.array([self.place(dict(p, on="ground"))[:2] for p in fz["area"]]))
                count("fences from a source")
            elif kind == "bin_pen":
                e0, n0, _h0 = self.place(dict(fz["at"], on="ground"))
                w, d = fz.get("size_m", [3.4, 2.6])
                feature_clear.append((e0, n0, float(fz.get("clear_m", math.hypot(w, d) / 2 + 2.4))))
                yaw = math.radians(float(fz.get("yaw_deg", 0.0)))
                ae, an = math.cos(yaw), -math.sin(yaw)      # along its width (east at yaw 0)
                be, bn = math.sin(yaw), math.cos(yaw)       # its depth, toward its back (north at yaw 0)
                key = cell(e0, n0)
                if key is None:
                    continue

                def corner(sa, sb):
                    return e0 + ae * sa * w / 2 + be * sb * d / 2, n0 + an * sa * w / 2 + bn * sb * d / 2
                c00, c10, c11, c01 = corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)
                u0 = 0.0
                for (pa, pb) in ((c00, c01), (c01, c11), (c11, c10)):   # three sides: its back to the north and its two ends
                    u0 = weave_seg(key, pa[0], pa[1], pb[0], pb[1], 1.8, u0)
                gate = (c00[0] + ae * (w - 1.3), c00[1] + an * (w - 1.3))   # the south side, with a gate's gap at its east end
                weave_seg(key, c00[0], c00[1], gate[0], gate[1], 1.8, 0.0)
                # the bins in a row along its back, lids down, two wheels behind
                nb = int(fz.get("bins", 7))
                for k in range(nb):
                    sa = -1.0 + (2.0 * k + 1.0) / nb
                    bxe, bxn = e0 + ae * sa * (w / 2 - 0.15) + be * (d / 2 - 0.5), n0 + an * sa * (w / 2 - 0.15) + bn * (d / 2 - 0.5)
                    h = g(bxe, bxn)
                    colr = BIN[k % len(BIN)]
                    bw = min(0.62, (w - 0.3) / nb * 0.92)
                    self._box(key, "bin#" + colr, bxe, bxn, h + 0.08, h + 1.0, bw, 0.72, ae, an, out)
                    self._box(key, "bin#" + colr, bxe - be * 0.02, bxn - bn * 0.02, h + 1.0, h + 1.06, bw + 0.04, 0.78, ae, an, out)
                    for sw in (-1.0, 1.0):
                        self._box(key, "tyre#1b1b1c", bxe + ae * sw * bw * 0.35 + be * 0.3, bxn + an * sw * bw * 0.35 + bn * 0.3, h, h + 0.2, 0.06, 0.2, be, bn, out)
                # what's heaped in front of it, toward the sidewalk: bags, a box, things spilled
                for k, (fa, fb, sz) in enumerate(((-0.3, -0.75, 0.55), (0.25, -0.95, 0.45), (0.8, -0.7, 0.5), (-0.85, -1.05, 0.4), (0.05, -1.35, 0.35), (0.55, -1.4, 0.3))):
                    te = e0 + ae * fa * w / 2 + be * (fb * d / 2 - 0.3)
                    tn = n0 + an * fa * w / 2 + bn * (fb * d / 2 - 0.3)
                    h = g(te, tn)
                    self._prism(key, ("bag#1e1f22" if k % 3 != 2 else "bag#3a3c40"), te, tn, h - 0.02, h + sz * 0.8, sz * 0.5, sz * 0.32, 7, out, rot=k * 0.7)
                self._box(key, "cardboard#a0784a", e0 + be * (-d / 2 - 0.9), n0 + bn * (-d / 2 - 0.9), g(e0, n0), g(e0, n0) + 0.35, 0.6, 0.45, ae, an, out)
                # (11-06, an aerial photo) boards and a pallet leaning in it against its back, and yellow bags among the black
                for k, fa in enumerate((-0.75, -0.45, 0.6, 0.85)):
                    bxe, bxn = e0 + ae * fa * w / 2 + be * (d / 2 - 0.15), n0 + an * fa * w / 2 + bn * (d / 2 - 0.15)
                    fte, ftn = bxe - be * 0.75, bxn - bn * 0.75
                    self._beam(key, "wood#8a6a44" if k % 2 else "wood#a08058", (fte, g(fte, ftn) + 0.02, ftn), (bxe + ae * 0.1 * k, g(bxe, bxn) + 1.5 - 0.15 * k, bxn), 0.05, out)
                for k, (fa, fb, sz) in enumerate(((-0.55, -1.2, 0.42), (0.35, -1.6, 0.38))):
                    te, tn = e0 + ae * fa * w / 2 + be * (fb * d / 2 - 0.3), n0 + an * fa * w / 2 + bn * (fb * d / 2 - 0.3)
                    self._prism(key, "bag#d8b830", te, tn, g(te, tn) - 0.02, g(te, tn) + sz * 0.8, sz * 0.5, sz * 0.3, 7, out, rot=k * 1.3)
                self._box(key, "paper#d8c8a0", e0 + ae * 0.4 + be * (-d / 2 - 1.6), n0 + an * 0.4 + bn * (-d / 2 - 1.6), g(e0, n0), g(e0, n0) + 0.02, 0.5, 0.35, be, bn, out)
                count("bin pens from a source")
            elif kind == "litter":
                # (11-06) {"kind": "litter", "way": <an area's way> or "area": [{lat, lon}, ...], "count": 140}: what's dropped all over it (an aerial
                # photo of 12th and Jackson's lot): squares of paper, cups, cans, foil, wrappers, scattered the same every cook
                if fz.get("way") and int(fz["way"]) in self.ways:
                    _ids, ringL = self.pts(self.ways[int(fz["way"])])
                    ringL = ringL[:-1]
                else:
                    ringL = np.array([self.place(dict(q, on="ground"))[:2] for q in fz.get("area", [])])
                if len(ringL) < 3:
                    continue
                lo_, hi_ = ringL.min(axis=0), ringL.max(axis=0)
                rl = random.Random(int(fz.get("way", 0)) or 7)
                LIT = [("paper#e8e2d0", 0.16, 0.11, 0.008), ("paper#d8c8a0", 0.12, 0.09, 0.008), ("cup#e8e6e0", 0.07, 0.07, 0.1), ("can#b8bcc0", 0.06, 0.06, 0.11),
                       ("foil#c8ccd0", 0.1, 0.1, 0.004), ("wrap#c83030", 0.12, 0.07, 0.006), ("bag#1e1f22", 0.3, 0.25, 0.12)]
                made_l = 0
                for _try in range(int(fz.get("count", 120)) * 4):
                    if made_l >= int(fz.get("count", 120)):
                        break
                    le, ln = rl.uniform(lo_[0], hi_[0]), rl.uniform(lo_[1], hi_[1])
                    if not S.inside(np.array([le]), np.array([ln]), ringL)[0]:
                        continue
                    key = cell(le, ln)
                    if key is None:
                        continue
                    nm, a_, b_, hh_ = LIT[rl.randrange(len(LIT))]
                    yaw_ = rl.uniform(0.0, math.pi)
                    hg = g(le, ln) + st["lift"]["road"]
                    self._box(key, nm, le, ln, hg, hg + hh_, a_, b_, math.cos(yaw_), math.sin(yaw_), out)
                    made_l += 1
                count("litter from a source")
            elif kind == "sign":
                # (11-06) {"kind": "sign", "what": "school", "at": {lat, lon}, "faces_deg": 0}: a sign on its own post, its face toward faces_deg (the school
                # crossing sign at 12th and Jackson's southwest corner: photos of it and the city's camera). The sign's face is drawn by code
                e0, n0, _h0 = self.place(dict(fz["at"], on="ground"))
                key = cell(e0, n0)
                if key is None:
                    continue
                h = g(e0, n0)
                fa_ = math.radians(float(fz.get("faces_deg", 0.0)))
                fe_, fn_ = math.sin(fa_), math.cos(fa_)
                re_, rn_ = -fn_, fe_
                self._prism(key, "pole#8a9094", e0, n0, h - 0.2, h + 3.0, 0.035, 0.035, 6, out)
                for tex, h0, h1, w_ in (("school:sign", 2.25, 3.0, 0.75), ("school:plate", 1.98, 2.2, 0.62)):
                    cx, cy = e0 + fe_ * 0.06, n0 + fn_ * 0.06
                    ax, ay, bx, by = cx - re_ * w_ / 2, cy - rn_ * w_ / 2, cx + re_ * w_ / 2, cy + rn_ * w_ / 2
                    self._texquad(key, tex, [(ax, h + h0, ay), (bx, h + h0, by), (bx, h + h1, by), (ax, h + h1, ay)], [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
                count("signs from a source")
            elif kind == "bins":
                # (11-06) {"kind": "bins", "at": {lat, lon}, "faces_deg": 0, "which": ["garbage", "recycle"]}: the city's street bins, side by side (from
                # photos: a black Garbage and a blue Recycle at the stop on Jackson, a black one at the corner), their labels toward faces_deg
                e0, n0, _h0 = self.place(dict(fz["at"], on="ground"))
                key = cell(e0, n0)
                if key is None:
                    continue
                fa_ = math.radians(float(fz.get("faces_deg", 0.0)))
                fe_, fn_ = math.sin(fa_), math.cos(fa_)
                re_, rn_ = -fn_, fe_
                which = fz.get("which", ["garbage"])
                for k, wh in enumerate(which):
                    off = (k - (len(which) - 1) / 2.0) * 0.72
                    be_, bn_ = e0 + re_ * off, n0 + rn_ * off
                    h = g(be_, bn_)
                    body = "bin#1d1e20" if wh == "garbage" else "bin#2f6fb0"
                    self._box(key, body, be_, bn_, h, h + 1.18, 0.66, 0.56, re_, rn_, out)
                    self._box(key, "bin#8a8e92" if wh == "garbage" else "bin#c8d0d8", be_, bn_, h + 1.18, h + 1.24, 0.68, 0.58, re_, rn_, out)
                    words = "Garbage" if wh == "garbage" else "Recycle"
                    cols = ("2a2b2d", "f4f4f0") if wh == "garbage" else ("2f6fb0", "f4f4f0")
                    cx, cy = be_ + fe_ * 0.285, bn_ + fn_ * 0.285
                    ax, ay, bx, by = cx - re_ * 0.3, cy - rn_ * 0.3, cx + re_ * 0.3, cy + rn_ * 0.3
                    self._texquad(key, "sign:%s:%s:3.0:%s" % (cols[0], cols[1], words), [(ax, h + 0.92, ay), (bx, h + 0.92, by), (bx, h + 1.12, by), (ax, h + 1.12, ay)],
                                  [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
                count("the city's bins from a source")
            elif kind == "billboard":
                # (11-06) {"kind": "billboard", "at": {lat, lon}, "faces_deg": 200, "size_m": [14.6, 4.3], "bottom_m": 7.2, "words": [...], "colours": [...]}:
                # a billboard on two posts, its face's bottom bottom_m over the ground, its face looking toward faces_deg (clockwise from north), its back
                # a steel box, a catwalk along its foot. Its words are ours
                e0, n0, _h0 = self.place(dict(fz["at"], on="ground"))
                key = cell(e0, n0)
                if key is None:
                    continue
                w, hh = (float(v) for v in fz.get("size_m", [14.6, 4.3]))
                h = g(e0, n0)
                bot = h + float(fz.get("bottom_m", 7.2))
                fa = math.radians(float(fz.get("faces_deg", 180.0)))
                fe, fn = math.sin(fa), math.cos(fa)          # out of its face
                re_, rn_ = -fn, fe                             # along it, left to right seen from in front
                cols = fz.get("colours", ["1c3a6e", "f4f1e6", "f4d03f"])
                tex = "billboard:%s:%s:%s:%.1f:%s" % (cols[0], cols[1], cols[2], max(1.0, round(w / hh * 2.0) / 2.0), "|".join(fz.get("words", [""])))
                cx, cy = e0 + fe * 0.17, n0 + fn * 0.17
                ax, ay, bx, by = cx - re_ * w / 2, cy - rn_ * w / 2, cx + re_ * w / 2, cy + rn_ * w / 2
                self._texquad(key, tex, [(ax, bot, ay), (bx, bot, by), (bx, bot + hh, by), (ax, bot + hh, ay)], [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
                self._box(key, "steel#5d6064", e0, n0, bot - 0.12, bot + hh + 0.12, w + 0.24, 0.3, re_, rn_, out)
                self._box(key, "steel#4a4d50", e0 + fe * 0.7, n0 + fn * 0.7, bot - 0.22, bot - 0.12, w, 0.9, re_, rn_, out)
                for s in (-0.3, 0.3):
                    self._box(key, "steel#4a4d50", e0 + re_ * s * w - fe * 0.35, n0 + rn_ * s * w - fn * 0.35, h - 0.2, bot - 0.1, 0.42, 0.42, re_, rn_, out)
                count("billboards from a source")
        # ---- markings: centre lines, lane lines, and the stripes of a marked crossing
        lift = st["lift"]
        mk = {}

        def strip(key, kind, a, b, half, hgt):
            """a flat strip from a to b (map e, n), half its width either side, at hgt"""
            (ax, ay), (bx, by) = a, b
            L = math.hypot(bx - ax, by - ay)
            if L < 1e-3:
                return
            re_, rn_ = (by - ay) / L * half, -(bx - ax) / L * half
            mk.setdefault((key, kind), []).append(((ax + re_, ay + rn_, hgt[0]), (bx + re_, by + rn_, hgt[1]), (bx - re_, by - rn_, hgt[1]), (ax - re_, ay - rn_, hgt[0])))
        for l in roads:
            if l["hw"] in ("service", "track", "motorway_link") and not l["t"].get("lanes"):
                continue
            C = l["C"]
            sarc = S.arclen(C)
            lanes = metres(l["t"].get("lanes")) or 0
            oneway = l["t"].get("oneway") in ("yes", "true", "1", "-1") or l["hw"].startswith("motorway")
            offs, dashed = [], []
            if not oneway and (lanes >= 2 or l["hw"] in ("primary", "secondary", "tertiary")):
                offs += [-0.13, 0.13]
                dashed += [False, False]
            n_l = int(lanes) if lanes >= 2 else 0
            if n_l:
                lw = l["w"] / n_l
                if oneway:
                    for k in range(1, n_l):
                        offs.append(-l["w"] / 2 + k * lw)
                        dashed.append(True)
                elif n_l >= 4:
                    for k in range(1, n_l // 2):
                        offs += [k * lw, -k * lw]
                        dashed += [True, True]
            if not offs:
                continue
            # away from the junctions
            ok_s = np.ones(len(C), dtype=bool)
            for nid in l["ids"]:
                if nid in junction and nid in self.ne:
                    d0 = np.hypot(C[:, 0] - self.ne[nid], C[:, 1] - self.nn[nid])
                    ok_s &= d0 > 9.0
            R = S.rights(C)
            H = self.field.sample(C[:, 0], C[:, 1]) + lift["road"] + 0.012
            for o, dsh in zip(offs, dashed):
                Eo, No = C[:, 0] + R[:, 0] * o, C[:, 1] + R[:, 1] * o
                for k in range(len(C) - 1):
                    if not (ok_s[k] and ok_s[k + 1]):
                        continue
                    if dsh and ((0.5 * (sarc[k] + sarc[k + 1])) % 12.0) > 3.0:
                        continue
                    key = cell(0.5 * (Eo[k] + Eo[k + 1]), 0.5 * (No[k] + No[k + 1]))
                    if key is None:
                        continue
                    strip(key, "paint#d9b23c" if not dsh else "paint#e6e4dc", (Eo[k], No[k]), (Eo[k + 1], No[k + 1]), 0.06, (H[k], H[k + 1]))
            count("marked roads")
        for l in self.lines:
            t = l["t"]
            if not (l["hw"] == "footway" and t.get("footway") == "crossing") or l["bridge"]:
                continue
            marked = t.get("crossing") in ("marked", "zebra", "traffic_signals", "uncontrolled") or t.get("crossing:markings") not in (None, "no")
            if not marked or t.get("crossing:markings") == "no":
                continue
            C = l["C"]
            sarc = S.arclen(C)
            # (11-06) as the map says it's marked: lines (its two edges), dashes, ladder (edges and bars), zebra (bars), :paired or :double (bars in
            # pairs). Nothing said, bars
            how = t.get("crossing:markings") or "zebra"
            edges = how in ("lines", "dashes", "ladder", "ladder:skewed", "ladder:paired", "lines:paired")
            bars = how not in ("lines", "dashes", "lines:paired", "surface", "pictograms")
            paired = how.endswith(":paired") or how.endswith(":double")
            if edges:
                Rx = S.rights(C)
                for sd in (-1.0, 1.0):
                    o = sd * (l["w"] / 2 - 0.15)
                    Eo, No = C[:, 0] + Rx[:, 0] * o, C[:, 1] + Rx[:, 1] * o
                    for k in range(len(C) - 1):
                        if how == "dashes" and int(sarc[k] / 0.6) % 2:
                            continue
                        key = cell(0.5 * (Eo[k] + Eo[k + 1]), 0.5 * (No[k] + No[k + 1]))
                        if key is None:
                            continue
                        strip(key, "paint#e6e4dc", (Eo[k], No[k]), (Eo[k + 1], No[k + 1]), 0.15,
                              (g(Eo[k], No[k]) + lift["crossing"] + 0.012, g(Eo[k + 1], No[k + 1]) + lift["crossing"] + 0.012))
            if t.get("crossing:mural") == "yes":
                # (11-06) a crosswalk painted as a mural: one picture laid across the whole crossing, our own design (mural_texture), a little wider
                # than the crossing's band, over the road's paint
                total = float(sarc[-1]) or 1.0
                Rx = S.rights(C)
                half = l["w"] / 2 + 0.35
                for k in range(len(C) - 1):
                    a0, a1 = sarc[k] / total, sarc[k + 1] / total
                    pa, pb = C[k], C[k + 1]
                    rva, rvb = Rx[k] * half, Rx[k + 1] * half   # (not ra, rb: nearest_road reads those)
                    key = cell(0.5 * (pa[0] + pb[0]), 0.5 * (pa[1] + pb[1]))
                    if key is None:
                        continue
                    ha, hb = g(*pa) + lift["crossing"] + 0.016, g(*pb) + lift["crossing"] + 0.016
                    self._texquad(key, "mural", [(pa[0] - rva[0], ha, pa[1] - rva[1]), (pb[0] - rvb[0], hb, pb[1] - rvb[1]), (pb[0] + rvb[0], hb, pb[1] + rvb[1]), (pa[0] + rva[0], ha, pa[1] + rva[1])],
                                  [(a0, 0.0), (a1, 0.0), (a1, 1.0), (a0, 1.0)])
                count("crosswalks painted as a mural")
            elif how == "surface":
                # a crossing marked by its surface: a band in its colour (crossing:markings:colour), else a muted brick
                col = rgb(self.NAMED.get(t.get("crossing:markings:colour", ""), t.get("crossing:markings:colour", "") if len(t.get("crossing:markings:colour", "")) == 6 else "8a5245"))
                for k in range(len(C) - 1):
                    key = cell(0.5 * (C[k][0] + C[k + 1][0]), 0.5 * (C[k][1] + C[k + 1][1]))
                    if key is None:
                        continue
                    hh = g(*C[k]) + lift["crossing"] + 0.012
                    strip(key, "paint#" + col, (C[k][0], C[k][1]), (C[k + 1][0], C[k + 1][1]), l["w"] / 2, (hh, hh))
            if bars:
                for at in np.arange(0.4, sarc[-1] - 0.4, 1.2):
                    for b0, b1 in (((0.0, 0.22), (0.38, 0.6)) if paired else ((0.0, 0.6),)):
                        a = (float(np.interp(at + b0, sarc, C[:, 0])), float(np.interp(at + b0, sarc, C[:, 1])))
                        b = (float(np.interp(at + b1, sarc, C[:, 0])), float(np.interp(at + b1, sarc, C[:, 1])))
                        key = cell(0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1]))
                        if key is None:
                            continue
                        hh = g(*a) + lift["crossing"] + 0.012
                        strip(key, "paint#e6e4dc", a, b, l["w"] / 2 - (0.3 if edges else 0.0), (hh, hh))
            count("crosswalks (%s)" % how)
        # (11-06) bike boxes (cycleway=asl): green paint on the road where the map draws one as an area
        for wid in sorted(self.ways):
            t = self.ways[wid].get("tags", {})
            if t.get("cycleway") != "asl":
                continue
            ids, Pb = self.pts(self.ways[wid])
            if len(Pb) < 4 or ids[0] != ids[-1]:
                continue
            ring = Pb[:-1]
            if S.area(ring) < 0:
                ring = ring[::-1]
            tris, _ok = S.earclip(ring)
            mid = ring.mean(axis=0)
            key = cell(mid[0], mid[1])
            if key is None:
                continue
            hh = g(mid[0], mid[1]) + lift["road"] + 0.014
            for a_, b_, c_ in tris:
                mk.setdefault((key, "paint#3f8f5a"), []).append(((ring[a_][0], ring[a_][1], hh), (ring[b_][0], ring[b_][1], hh), (ring[c_][0], ring[c_][1], hh), (ring[c_][0], ring[c_][1], hh)))
            count("bike boxes")
        for (key, kind), qs in mk.items():
            Q = np.array(qs)
            X = [cells.xyz(Q[:, c, 0], Q[:, c, 1], Q[:, c, 2]) for c in range(4)]
            cells.add_quads(kind, X[0], X[1], X[2], X[3], np.array([0.0, 1.0, 0.0]), Q[:, :, 0].mean(axis=1), Q[:, :, 1].mean(axis=1))
        # ---- (11-06) what hangs over the street and stands along it: wooden utility poles and their wires, trolley-bus wires where the map says
        # trolley_wire, the streetcar's wire, green street-name signs at the corners, cars parked along the curbs
        standing = [(e, n) for e, n in lamps] + sig_poles   # (11-06) and the signals' poles: a wire hangs from what's there
        sig_xy = np.array([[self.ne[nid], self.nn[nid]] for nid, tg in self.ntags.items() if tg.get("highway") == "traffic_signals" and nid in self.ne])
        bus_xy = np.array([[self.ne[nid], self.nn[nid]] for nid, tg in self.ntags.items() if tg.get("highway") == "bus_stop" and nid in self.ne])
        cross_xy = np.concatenate([l["C"] for l in self.lines if l["hw"] == "footway" and l["t"].get("footway") == "crossing"]) if any(
            l["hw"] == "footway" and l["t"].get("footway") == "crossing" for l in self.lines) else np.zeros((0, 2))
        drive_xy = np.array([l["C"][k] for l in self.lines if l["hw"] == "service" for k in (0, -1)]) if any(l["hw"] == "service" for l in self.lines) else np.zeros((0, 2))
        stand_xy = lambda: np.array(standing) if standing else np.zeros((0, 2))

        def near_any(xy, e, n, d):
            return len(xy) > 0 and bool((np.hypot(xy[:, 0] - e, xy[:, 1] - n) < d).any())

        def at(C, sarc, R, a):
            return (float(np.interp(a, sarc, C[:, 0])), float(np.interp(a, sarc, C[:, 1])), float(np.interp(a, sarc, R[:, 0])), float(np.interp(a, sarc, R[:, 1])))
        every = float(st.get("utility_every_m", 0.0))
        runs = []
        if every > 0:
            for l in roads:
                if l["hw"] not in ("residential", "tertiary", "secondary", "primary", "unclassified", "tertiary_link", "secondary_link", "living_street"):
                    continue
                C = l["C"]
                sarc = S.arclen(C)
                R = S.rights(C)
                side = 1.0 if abs(l["id"]) % 2 == 0 else -1.0
                off = l["w"] / 2 + 0.6
                run = []
                for k, a in enumerate(np.arange(every / 2, sarc[-1], every)):
                    e0, n0, re_, rn_ = at(C, sarc, R, a)
                    e, n = e0 + side * re_ * off, n0 + side * rn_ * off
                    if near_any(jxy, e0, n0, 11.0):
                        if len(run) >= 2:
                            runs.append(run)
                        run = []
                        continue
                    if in_median(e, n):   # (11-10) a divided street's poles run along its outer curb
                        e, n = e0 - side * re_ * off, n0 - side * rn_ * off
                        if in_median(e, n):
                            continue
                    nr = nearest_road(e, n)
                    if (nr is not None and nr[4] < nr[2] + 0.3) or self.in_road(e, n, 0.2):
                        continue   # it'd stand in another road
                    if near_any(stand_xy(), e, n, 3.0):
                        continue
                    key = cell(e, n)
                    if key is None:
                        continue
                    h = g(e, n)
                    dk = self.deck_over(e, n)
                    if dk is not None and dk < h + 11.6:
                        continue   # (11-10) a bridge's deck is over it, lower than the pole is tall: it stood up through I-5's ramp
                    self._prism(key, "upole#6b5a48", e, n, h - 0.3, h + 10.6, 0.16, 0.12, 8, out)
                    self._box(key, "upole#6b5a48", e, n, h + 10.2, h + 10.38, 2.3, 0.12, re_, rn_, out)
                    if k % 4 == 1:
                        self._prism(key, "unit#8a8d90", e, n, h + 8.4, h + 9.3, 0.28, 0.28, 8, out)   # a transformer round the pole
                    standing.append((e, n))
                    run.append((e, n, h, re_, rn_, key))
                    count("utility poles")
                if len(run) >= 2:
                    runs.append(run)
        for run in runs:
            for (e1, n1, h1, r1e, r1n, k1), (e2, n2, h2, r2e, r2n, k2) in zip(run[:-1], run[1:]):
                if math.hypot(e2 - e1, n2 - n1) > every * 1.6:
                    continue
                for oa, hh in ((-1.0, 10.42), (0.0, 10.42), (1.0, 10.42), (0.0, 8.7)):
                    ts = np.linspace(0.0, 1.0, 5)
                    P = [((e1 + r1e * oa) * (1 - t) + (e2 + r2e * oa) * t, (h1 + hh) * (1 - t) + (h2 + hh) * t - 0.45 * 4 * t * (1 - t), (n1 + r1n * oa) * (1 - t) + (n2 + r2n * oa) * t) for t in ts]
                    # (11-10) not through a bridge, or just over one: a span isn't strung where a bridge's deck is in its line, unless the deck is well
                    # over it (its underside 6 m and a deck's thickness above the wire). The audit: wires a metre over the lanes of I-5's ramps
                    if any(dk is not None and dk < p[1] + 6.0 + st["deck_m"] for p in P for dk in (self.deck_over(p[0], p[2]),)):
                        continue
                    for q in range(len(P) - 1):
                        key = cell(0.5 * (P[q][0] + P[q + 1][0]), 0.5 * (P[q][2] + P[q + 1][2]))
                        if key is not None:
                            self._beam(key, "wire#262628", P[q], P[q + 1], 0.022, out)
                count("wire spans")
        # trolley-bus wires: a pair over the right-hand lane each way, hung from span wires between poles at the curbs every 30 m
        for l in self.lines:
            if l["kind"] != "road" or l["t"].get("trolley_wire") != "yes" or len(l["C"]) < 2:
                continue
            C = l["C"]
            sarc = S.arclen(C)
            R = S.rights(C)
            oneway = l["t"].get("oneway") in ("yes", "true", "1", "-1")
            lane = min(3.3, l["w"] / 2)
            mids = [l["w"] / 2 - lane / 2] if oneway else [l["w"] / 2 - lane / 2, -(l["w"] / 2 - lane / 2)]
            H = l["hc"] + 0.0 if l["bridge"] else self.field.sample(C[:, 0], C[:, 1]) + lift["road"]
            for mid in mids:
                for o in (mid - 0.3, mid + 0.3):
                    Eo, No = C[:, 0] + R[:, 0] * o, C[:, 1] + R[:, 1] * o
                    step = max(1, len(C) // max(1, int(sarc[-1] / 6.0)))
                    idx = list(range(0, len(C), step)) + ([len(C) - 1] if (len(C) - 1) % step else [])
                    for a_, b_ in zip(idx[:-1], idx[1:]):
                        key = cell(0.5 * (Eo[a_] + Eo[b_]), 0.5 * (No[a_] + No[b_]))
                        if key is not None:
                            self._beam(key, "wire#262628", (Eo[a_], H[a_] + 5.9, No[a_]), (Eo[b_], H[b_] + 5.9, No[b_]), 0.02, out)
            for a in np.arange(10.0, sarc[-1], 30.0):
                e0, n0, re_, rn_ = at(C, sarc, R, a)
                h0 = float(np.interp(a, sarc, H))
                ends = []
                for sd in (-1.0, 1.0):
                    e, n = e0 + sd * re_ * (l["w"] / 2 + 0.7), n0 + sd * rn_ * (l["w"] / 2 + 0.7)
                    key = cell(e, n)
                    if key is None:
                        continue
                    if not l["bridge"] and (in_median(e, n) or self.in_road(e, n, 0.2)):
                        # (11-10) no pole in a divided street's median, or in another road (at a corner the curb of one street is the lane of the
                        # next: the audit found 7 in travel lanes): the span is a bracket arm from the other curb's pole, out past the wires
                        o = (min(mids) - 0.9) if sd < 0 else (max(mids) + 0.9)
                        ends.append((e0 + re_ * o, h0 + 6.5, n0 + rn_ * o))
                        continue
                    hp = h0 if l["bridge"] else g(e, n)
                    sx = stand_xy()
                    dd = np.hypot(sx[:, 0] - e, sx[:, 1] - n) if len(sx) else np.zeros(0)
                    if len(dd) and dd.min() < float(st.get("joint_pole_m", 2.5)):
                        q = int(np.argmin(dd))   # (11-06) hung from the pole that's there: a signal's, a lamp's, a utility pole
                        e, n = float(sx[q, 0]), float(sx[q, 1])
                        count("span wires hung from a pole that was there")
                    else:
                        self._prism(key, "pole#55595d", e, n, hp - 0.3, hp + 7.6, 0.13, 0.1, 8, out)
                        standing.append((e, n))
                    ends.append((e, hp + 6.5, n))
                if len(ends) == 2:
                    key = cell(e0, n0)
                    if key is not None:
                        self._beam(key, "wire#262628", ends[0], ends[1], 0.015, out)
            count("trolley wires")
        # (11-06) where two trolley-wired streets meet, the turns' wires: from each way in to each way out along a street of another name, a curve
        # over the junction for each wire of the pair, which is how the photos of 12th and Jackson show them strung across it
        arms = {}
        for l in self.lines:
            if l["kind"] != "road" or l["t"].get("trolley_wire") != "yes" or len(l["P"]) < 2 or l["bridge"]:
                continue
            ow = l["t"].get("oneway") in ("yes", "true", "1")
            lane = min(3.3, l["w"] / 2)
            m = l["w"] / 2 - lane / 2
            Pl = l["P"]
            for k, nid in enumerate(l["ids"]):
                if nid not in junction:
                    continue
                for nb, isfwd in ((k + 1, True), (k - 1, False)):
                    if nb < 0 or nb >= len(Pl):
                        continue
                    d = Pl[nb] - Pl[k]
                    L = float(np.hypot(*d))
                    if L < 1e-6:
                        continue
                    # in: vehicles come toward the junction along this side; out: they leave along it. A oneway way only one of them
                    arms.setdefault(nid, []).append((d[0] / L, d[1] / L, (not ow) or (not isfwd), (not ow) or isfwd, l["t"].get("name"), m))
        for nid, al in sorted(arms.items()):
            if nid not in self.ne or len({a[4] for a in al}) < 2:
                continue
            e0, n0 = self.ne[nid], self.nn[nid]
            h0 = g(e0, n0) + lift["road"]
            for da in al:
                if not da[2]:
                    continue
                ve, vn = -da[0], -da[1]   # the way in, toward the junction
                for db in al:
                    if not db[3] or db[4] == da[4]:
                        continue
                    we, wn = db[0], db[1]   # the way out
                    for dw in (-0.3, 0.3):
                        ma, mb = da[5] + dw, db[5] + dw
                        A = np.array([e0 + da[0] * 9.0 + vn * ma, n0 + da[1] * 9.0 - ve * ma])
                        B = np.array([e0 + db[0] * 9.0 + wn * mb, n0 + db[1] * 9.0 - we * mb])
                        # the corner: where the line in meets the line out, or the junction
                        den = ve * wn - vn * we
                        if abs(den) > 1e-3:
                            tt = ((B[0] - A[0]) * wn - (B[1] - A[1]) * we) / den
                            Cn = A + np.array([ve, vn]) * tt
                        else:
                            Cn = np.array([e0, n0])
                        ts = np.linspace(0.0, 1.0, 9)
                        Q = [(1 - u) ** 2 * A + 2 * (1 - u) * u * Cn + u * u * B for u in ts]
                        for q in range(len(Q) - 1):
                            key = cell(0.5 * (Q[q][0] + Q[q + 1][0]), 0.5 * (Q[q][1] + Q[q + 1][1]))
                            if key is not None:
                                self._beam(key, "wire#262628", (Q[q][0], h0 + 5.9, Q[q][1]), (Q[q + 1][0], h0 + 5.9, Q[q + 1][1]), 0.02, out)
                    count("trolley turns")
        # the streetcar's wire, over the middle of each track
        for l in self.lines:
            if l["kind"] != "rail" or len(l["C"]) < 2:
                continue
            C = l["C"]
            H = (l["hc"] if l["bridge"] else self.field.sample(C[:, 0], C[:, 1])) + 5.8
            step = max(1, len(C) // max(1, int(S.arclen(C)[-1] / 6.0)))
            idx = list(range(0, len(C), step)) + ([len(C) - 1] if (len(C) - 1) % step else [])
            for a_, b_ in zip(idx[:-1], idx[1:]):
                key = cell(0.5 * (C[a_, 0] + C[b_, 0]), 0.5 * (C[a_, 1] + C[b_, 1]))
                if key is not None:
                    self._beam(key, "wire#262628", (C[a_, 0], H[a_], C[a_, 1]), (C[b_, 0], H[b_], C[b_, 1]), 0.02, out)
            count("streetcar wires")
        # green street-name signs: at a corner where two named streets meet, a post with a blade along each
        names = {}
        for l in roads:
            nm = l["t"].get("name")
            if nm:
                for k, nid in enumerate(l["ids"]):
                    if nid in junction:
                        names.setdefault(nid, []).append((nm, l, k))
        for nid, ls in names.items():
            byname = {}
            for nm, l, k in ls:
                byname.setdefault(nm, (l, k))
            if len(byname) < 2 or nid not in self.ne:
                continue
            (la, ka), (lb, kb) = list(byname.values())[:2]

            def fwd(l, k):
                P = l["P"]
                d = P[k] - P[k - 1] if k > 0 else P[k + 1] - P[k]
                L = math.hypot(*d) or 1.0
                return d[0] / L, d[1] / L
            fa, fb = fwd(la, ka), fwd(lb, kb)
            e0, n0 = self.ne[nid], self.nn[nid]
            e = e0 + fa[1] * (la["w"] / 2 + 1.1) - fa[0] * (lb["w"] / 2 + 1.6)
            n = n0 - fa[0] * (la["w"] / 2 + 1.1) - fa[1] * (lb["w"] / 2 + 1.6)
            nr = nearest_road(e, n)
            if nr is not None and nr[4] < nr[2] + 0.3:
                continue
            if in_median(e, n) or self.in_road(e, n, 0.2):   # (11-10) not in a divided street's median, and not in any road
                continue
            key = cell(e, n)
            if key is None:
                continue
            h = g(e, n)
            self._prism(key, "pole#7d8288", e, n, h - 0.2, h + 3.3, 0.05, 0.05, 6, out)
            # (11-06) each blade says its street's name the city's way (12th Ave S); in Little Saigon a brown plate under each says it in Vietnamese
            # (a photo of the corner's own: Nam Đường Jackson, Đường Số 12 Nam)
            vi = self.district_of(e, n, "signs") == "vi"
            for fd, ln, hb in ((fa, la, 3.0), (fb, lb, 2.48 if vi else 2.72)):
                self._box(key, "sign#2e6b46", e, n, h + hb, h + hb + 0.22, 0.95, 0.04, fd[0], fd[1], out)
                self.blade_words(key, e, n, h + hb, h + hb + 0.22, 0.95, fd, "1f6b48", "f4f1e6", self.street_short(ln["t"]["name"]))
                vn = self.street_vi(ln["t"]["name"]) if vi else None
                if vn:
                    self._box(key, "sign#5a2a22", e, n, h + hb - 0.25, h + hb - 0.03, 0.95, 0.04, fd[0], fd[1], out)
                    self.blade_words(key, e, n, h + hb - 0.25, h + hb - 0.03, 0.95, fd, "5a2a22", "f4f1e6", vn)
                    count("street-name plates in Vietnamese")
            count("street signs")
        # cars parked along the curbs where parking's allowed (the map's parking tags; else the side streets), not across a crossing, a driveway, a
        # junction, a bus stop or a bike lane
        CAR = ["e6e6e2", "b4b7ba", "6f7378", "222326", "2a3a5c", "8e2a2a", "b9a98a", "3f5a48", "e6e6e2", "b4b7ba"]
        STYLE = [(4.6, 0.68, 0.5, 1.8), (4.1, 0.72, 0.55, 1.75), (4.8, 0.92, 0.6, 1.9)]
        cevery, share = float(st.get("car_every_m", 0.0)), float(st.get("car_share", 0.6))

        def car(key, e, n, h, fe, fn, u):
            """a parked car: body, glass, roof and tyres, its length along (fe, fn), on the ground at h"""
            Lc, bh, ch, wc = STYLE[int(u[1] * len(STYLE)) % len(STYLE)]
            col = CAR[int(u[2] * len(CAR)) % len(CAR)]
            self._box(key, "car#" + col, e, n, h + 0.3, h + 0.3 + bh, Lc, wc, fe, fn, out)
            cb = -0.15 * Lc
            self._box(key, "glass#2f3a44", e + fe * cb, n + fn * cb, h + 0.3 + bh, h + 0.3 + bh + ch, Lc * 0.5, wc - 0.2, fe, fn, out)
            self._box(key, "car#" + col, e + fe * cb, n + fn * cb, h + 0.3 + bh + ch, h + 0.36 + bh + ch, Lc * 0.46, wc - 0.26, fe, fn, out)
            for ax in (Lc / 2 - 0.75, -(Lc / 2 - 0.75)):
                for ay in (wc / 2 - 0.1, -(wc / 2 - 0.1)):
                    we, wn = e + fe * ax + fn * ay, n + fn * ax - fe * ay
                    self._box(key, "tyre#1b1b1c", we, wn, h, h + 0.64, 0.64, 0.24, fe, fn, out)
        if cevery > 0:
            for l in roads:
                t = l["t"]
                if t.get("parking:both") in ("no", "separate") or t.get("parking:lane:both") in ("no", "no_stopping", "no_parking", "fire_lane"):
                    continue
                C = l["C"]
                sarc = S.arclen(C)
                R = S.rights(C)
                oneway = t.get("oneway") in ("yes", "true", "1", "-1")
                for side, sg in (("right", 1.0), ("left", -1.0)):
                    tagged = t.get("parking:lane:" + side) or t.get("parking:" + side) or t.get("parking:lane:both") or t.get("parking:both")
                    if tagged in ("no", "no_stopping", "no_parking", "fire_lane", "separate"):
                        continue
                    if not (tagged in ("parallel", "lane", "street_side", "yes") or l["hw"] in ("residential", "tertiary", "secondary", "unclassified", "living_street")):
                        continue
                    cw = t.get("cycleway:" + side) or t.get("cycleway:both") or t.get("cycleway")
                    if cw in ("lane", "track", "buffered_lane", "opposite_lane", "opposite_track"):
                        continue
                    off = l["w"] / 2 - 1.15
                    if off < 2.4:
                        continue
                    # (11-10) nobody parks along a divided street's median: this side's kerb, at the way's middle, is between its two roadways
                    em, nm_, rem, rnm = at(C, sarc, R, sarc[-1] / 2)
                    if in_median(em + sg * rem * (l["w"] / 2 + 0.8), nm_ + sg * rnm * (l["w"] / 2 + 0.8)):
                        continue
                    for k, a in enumerate(np.arange(8.0, sarc[-1] - 6.0, cevery)):
                        e0, n0, re_, rn_ = at(C, sarc, R, a)
                        hsh = (int(e0 * 13.1 + n0 * 7.7 + k * 3.3 + sg * 5.0) * 2654435761) % 4294967296
                        u = [((hsh * (j + 2) * 0.61803398875) % 1.0) for j in range(4)]
                        if u[0] > share:
                            continue
                        e, n = e0 + sg * re_ * off, n0 + sg * rn_ * off
                        if near_any(jxy, e0, n0, 12.0) or near_any(cross_xy, e, n, 5.5) or near_any(drive_xy, e, n, 4.5) or near_any(bus_xy, e, n, 14.0) or near_any(sig_xy, e0, n0, 14.0):
                            continue
                        key = cell(e, n)
                        if key is None:
                            continue
                        h = g(e, n) + lift["road"]
                        fe, fn = -rn_, re_   # along the road
                        if sg < 0 and not oneway:
                            fe, fn = -fe, -fn
                        car(key, e, n, h, fe, fn, u)
                        count("parked cars")
        # (11-06) cars in the surface lots: along each mapped parking aisle in a lot, a stall either side square to it, a car in lot_car_share of them
        lshare = float(st.get("lot_car_share", 0.0))
        stalls = []
        if lshare > 0:
            aisles = [l for l in self.lines if l["hw"] == "service" and l["t"].get("service") == "parking_aisle" and len(l["C"]) >= 2]
            for wid in sorted(self.ways):
                w = self.ways[wid]
                t = w.get("tags", {})
                if t.get("amenity") != "parking" or t.get("parking", "surface") != "surface":
                    continue
                ids, Pl = self.pts(w)
                if len(Pl) < 4 or ids[0] != ids[-1]:
                    continue
                ring = Pl[:-1]
                lo, hi = ring.min(axis=0) - 2.0, ring.max(axis=0) + 2.0
                # (11-06) a lot a source has seen: its own share of the stalls with a car, and only the stalls on one side of its aisle (the config's
                # tags: lot:car_share, lot:car_side north/south/east/west)
                share = float(t.get("lot:car_share", lshare))
                side = {"north": (0.0, 1.0), "south": (0.0, -1.0), "east": (1.0, 0.0), "west": (-1.0, 0.0)}.get(t.get("lot:car_side", ""))
                for l in aisles:
                    C = l["C"]
                    if C[:, 0].max() < lo[0] or C[:, 0].min() > hi[0] or C[:, 1].max() < lo[1] or C[:, 1].min() > hi[1]:
                        continue
                    sarc = S.arclen(C)
                    R = S.rights(C)
                    for k, a in enumerate(np.arange(1.4, sarc[-1] - 1.0, 2.7)):
                        e0, n0, re_, rn_ = at(C, sarc, R, a)
                        # (11-06) the stalls' lines: a white line between each two stalls either side, from the aisle's edge to the stalls' far end
                        for sg in (1.0, -1.0):
                            for da in (-1.35, 1.35):
                                if da < 0 and k > 0:
                                    continue   # the line between two stalls is the one before's after
                                ls0, ln0, _r1, _r2 = at(C, sarc, R, a + da)
                                pa = (ls0 + sg * re_ * 3.25, ln0 + sg * rn_ * 3.25)
                                pb = (ls0 + sg * re_ * 8.6, ln0 + sg * rn_ * 8.6)
                                pm = ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2)
                                if not S.inside(np.array([pm[0]]), np.array([pm[1]]), ring)[0]:
                                    continue
                                key = cell(pm[0], pm[1])
                                if key is None:
                                    continue
                                L = math.hypot(pb[0] - pa[0], pb[1] - pa[1]) or 1.0
                                rx, ry = (pb[1] - pa[1]) / L * 0.055, -(pb[0] - pa[0]) / L * 0.055
                                ha, hb = g(*pa) + lift["road"] + 0.012, g(*pb) + lift["road"] + 0.012
                                stalls.append((key, ((pa[0] + rx, pa[1] + ry, ha), (pb[0] + rx, pb[1] + ry, hb), (pb[0] - rx, pb[1] - ry, hb), (pa[0] - rx, pa[1] - ry, ha))))
                        for sg in (1.0, -1.0):
                            if side and sg * (re_ * side[0] + rn_ * side[1]) < 0.5:
                                continue
                            e, n = e0 + sg * re_ * 5.9, n0 + sg * rn_ * 5.9
                            if not S.inside(np.array([e]), np.array([n]), ring)[0]:
                                continue
                            if any(math.hypot(e - ce, n - cn) < cr for ce, cn, cr in feature_clear):
                                continue   # (11-06) a source's pen keeps its corner of the lot clear
                            if any(S.inside(np.array([e]), np.array([n]), rg)[0] for rg in feature_rings):
                                continue   # nor in an area a feature keeps clear
                            fl = feature_lines + fence_lines
                            if fl and float(S.seg_dist(np.array([e]), np.array([n]), np.array([q[0] for q in fl]), np.array([q[1] for q in fl]),
                                                       np.array([q[2] for q in fl]), np.array([q[3] for q in fl]))[0].min()) < 2.6:
                                continue   # nor across a fence: a car's half length and a hand's width clear of it
                            if self.in_road(e, n, 0.3) or self.in_road(e + sg * re_ * 2.3, n + sg * rn_ * 2.3) or self.in_road(e - sg * re_ * 2.3, n - sg * rn_ * 2.3):
                                continue   # (11-10) nor with any of it out in a road (the audit: two lots' cars in the lanes of the street beside them)
                            hsh = (int(e * 13.1 + n * 7.7 + k * 3.3 + sg * 5.0 + wid % 1000) * 2654435761) % 4294967296
                            u = [((hsh * (j + 2) * 0.61803398875) % 1.0) for j in range(4)]
                            if u[0] > share:
                                continue
                            key = cell(e, n)
                            if key is None:
                                continue
                            car(key, e, n, g(e, n) + lift["road"], sg * re_, sg * rn_, u)
                            count("cars in lots")
        if stalls:
            Q = np.array([q for _k, q in stalls], dtype=np.float64)
            X = [cells.xyz(Q[:, c, 0], Q[:, c, 1], Q[:, c, 2]) for c in range(4)]
            cells.add_quads("paint#e6e4dc", X[0], X[1], X[2], X[3], np.array([0.0, 1.0, 0.0]), Q[:, :, 0].mean(axis=1), Q[:, :, 1].mean(axis=1))
            made["stall lines"] = len(stalls)
        self._flush(out)
        self.say("the street: " + ", ".join("%s %d" % kv for kv in sorted(made.items())))
        self.lap("dressed the streets")

    def street_in_front(self, mx, my, oe, on, de, dn, reach):
        """(11-06) how far in front of a wall a street runs along it: the nearest road (not a service road) ahead of the wall's middle, within 60
        degrees of straight out, and running within 40 degrees of along the wall; inf if there's none within reach"""
        a, b = self._sa, self._sb
        if not len(a):
            return np.inf
        near = np.nonzero((np.minimum(a[:, 0], b[:, 0]) <= mx + reach) & (np.maximum(a[:, 0], b[:, 0]) >= mx - reach)
                          & (np.minimum(a[:, 1], b[:, 1]) <= my + reach) & (np.maximum(a[:, 1], b[:, 1]) >= my - reach))[0]
        if not len(near):
            return np.inf
        dist, tt = S.seg_dist(np.array([mx]), np.array([my]), a[near, 0], a[near, 1], b[near, 0], b[near, 1])
        d = np.maximum(dist[0], 1e-6)
        px = a[near, 0] + (b[near, 0] - a[near, 0]) * tt[0]
        py = a[near, 1] + (b[near, 1] - a[near, 1]) * tt[0]
        ahead = ((px - mx) * oe + (py - my) * on) / d
        sx, sy = b[near, 0] - a[near, 0], b[near, 1] - a[near, 1]
        along = np.abs(sx * de + sy * dn) / np.maximum(np.hypot(sx, sy), 1e-9)
        ok = (ahead > 0.5) & (along > 0.77) & (d <= reach)
        return float(d[ok].min()) if ok.any() else np.inf

    def sign_words(self, tg):
        """(11-06) the words on a shop's sign: what the map says it is, never its name. A shop the map has as closed is for lease"""
        a, s = tg.get("amenity"), tg.get("shop")
        if not s and a not in self.SHOPS and ("disused:shop" in tg or "disused:amenity" in tg):
            return "FOR LEASE"
        cu = [c.strip().lower() for c in (tg.get("cuisine") or "").split(";") if c.strip()]
        if a in ("restaurant", "fast_food", "food_court", "cafe") and (tg.get("diet:vegan") == "only" or tg.get("diet:vegetarian") == "only"):
            return "VEGAN DELI"
        if a in ("restaurant", "fast_food", "food_court"):
            for c in cu:
                if c in self.CUISINE_WORDS:
                    return self.CUISINE_WORDS[c]
            return "TAKE OUT" if a == "fast_food" else "RESTAURANT"
        if a == "cafe":
            return "BUBBLE TEA" if "bubble_tea" in cu else "CAFE"
        if a in self.AMENITY_WORDS:
            return self.AMENITY_WORDS[a]
        if s:
            return self.SHOP_WORDS.get(s, s.replace("_", " ").replace(";", " ").upper()[:16])
        return None

    def shopfronts(self, bid, P, Pc, L, de, dn, oe, on, ref, top, lk, quad, key, shops, quads=None, tags=None):
        """(11-06) a shop building's fronts. By rule, on each wall a street runs in front of (shopfront_reach_m), or else on the wall that faces one
        best: the wall in bays about 10 m or less, the map's shops in the building each on the front nearest it, a bay its window and a sign over it
        that says what the shop is (sign_words()), an awning on some. A source's own front (the config's `shopfront` feature, on the wall nearest its
        `wall_at`) is laid as it says instead: its bays from one end, each a window or boarded and painted, its sign's words and colours, its awning"""
        st = self.st
        rnd = random.Random(abs(bid) * 7 + 3)
        reach = float(st.get("shopfront_reach_m", 28.0))
        vi_here = self.district_of(float(P[:, 0].mean()), float(P[:, 1].mean()), "signs") == "vi"   # (11-06) Little Saigon
        nw = len(L)
        hgt = top - ref
        gtop = ref + (2.9 if hgt >= 4.4 else 2.4)       # the windows' top
        made = self.made_fronts

        def tex_quad(tex, k, c0, c1, h0, h1, out, uv):
            ax, ay = Pc[k, 0] + de[k] * c0 + oe[k] * out, Pc[k, 1] + dn[k] * c0 + on[k] * out
            bx, by = Pc[k, 0] + de[k] * c1 + oe[k] * out, Pc[k, 1] + dn[k] * c1 + on[k] * out
            self._texquad(key, tex, [(ax, h0, ay), (bx, h0, by), (bx, h1, by), (ax, h1, ay)], uv)

        def bay(k, c0, c1, sign=None, awning=None, boarded=False, plain=False):
            w = c1 - c0
            if w < 1.2 or plain:
                return
            if boarded:
                # plywood over the windows, painted: three designs of ours in turn, a texture a 4.88 m run (four sheets)
                c, s = c0 + 0.1, 0
                while c < c1 - 0.1 - 1e-6:
                    cn = min(c1 - 0.1, c + 4.88)
                    f = (cn - c) / 4.88
                    tex_quad("boards:%d" % ((abs(bid) + s) % 3), k, c, cn, ref + 0.05, ref + 2.49, 0.05, [(0.0, 1.0), (f, 1.0), (f, 0.0), (0.0, 0.0)])
                    c, s = cn, s + 1
                made["boarded"] = made.get("boarded", 0) + 1
            else:
                quad("glass#" + self.GLASS_SHOP, k, c0 + 0.15, c1 - 0.15, ref + 0.35, gtop)
            if sign and sign[0] and hgt >= 3.4:
                words = sign[0]
                if vi_here and "|" not in words and words in self.VI_PAIRS:
                    words = "%s|%s" % self.VI_PAIRS[words]
                bg, fg = (sign[1], sign[2]) if len(sign) >= 3 else self.SIGN_COLOURS[rnd.randrange(len(self.SIGN_COLOURS))]
                sh = float(sign[3]) if len(sign) >= 4 else 0.75
                sw = min(w - 0.4, float(sign[4]) if len(sign) >= 5 else sh * 8.0)
                cm = 0.5 * (c0 + c1)
                s0 = min(gtop + 0.2, top - sh - 0.1)
                asp = max(1.0, round(sw / sh * 2.0) / 2.0)
                quad("trim#" + self.shade(bg, 0.55), k, cm - sw / 2 - 0.04, cm + sw / 2 + 0.04, s0 - 0.04, s0 + sh + 0.04, 0.07)
                tex_quad("sign:%s:%s:%.1f:%s" % (bg, fg, asp, words), k, cm - sw / 2, cm + sw / 2, s0, s0 + sh, 0.1, [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
                made["signs"] = made.get("signs", 0) + 1
            if awning:
                quad("awning#" + awning, k, c0 + 0.1, c1 - 0.1, gtop + 0.12, gtop - 0.18, 0.05, 1.1)
                quad("awning#" + awning, k, c0 + 0.1, c1 - 0.1, gtop - 0.18, gtop - 0.42, 1.1, 1.1)

        # a source's own fronts, each on the wall nearest its `wall_at`
        own = {}
        for fz in self.front_specs.get(bid, []):
            e0, n0, _h = self.place(dict(fz["wall_at"], on="ground"))
            dist, _tt = S.seg_dist(np.array([e0]), np.array([n0]), Pc[:-1, 0], Pc[:-1, 1], Pc[1:, 0], Pc[1:, 1])
            own[int(np.argmin(dist[0]))] = fz
        for k, fz in own.items():
            if fz.get("piers"):   # (11-06) the piers and the base under its windows in their own colour (the southwest building's are brown under tan)
                quad("trim#" + str(fz["piers"]), k, 0.0, L[k], ref + 0.0, gtop + 0.16, 0.02)
            dv = {"north": (0.0, 1.0), "south": (0.0, -1.0), "east": (1.0, 0.0), "west": (-1.0, 0.0)}.get(fz.get("from", ""), (0.0, 0.0))
            back = de[k] * dv[0] + dn[k] * dv[1] > 0      # its bays start from the wall's far end
            c = 0.0
            for b in fz.get("bays", []):
                m = float(b.get("m", 5.0))
                lo_, hi_ = (L[k] - c - m, L[k] - c) if back else (c, c + m)
                bay(k, max(0.0, lo_), min(L[k], hi_), sign=b.get("sign"), awning=b.get("awning"), boarded=bool(b.get("boarded")), plain=bool(b.get("plain")))
                c += m
            made["fronts from a source"] = made.get("fronts from a source", 0) + 1
        # (11-06) a source's crest (the config's `crest`): on the wall nearest each item's `wall_at`, a raised gable over the roofline (`gable`: [width,
        # height], in the wall's colour, peaked, as thick as the parapet, centred on the wall or at `at` of its length) with an emblem on it, or lettering
        # along the wall over its ground floor (`lettering`: [words, colour, height, width]); Ding How Shopping Center's at 12th and Jackson
        for fz in self.crest_specs.get(bid, []):
            for it in fz.get("items", []):
                e0, n0, _h = self.place(dict(it["wall_at"], on="ground"))
                dist, _tt = S.seg_dist(np.array([e0]), np.array([n0]), Pc[:-1, 0], Pc[:-1, 1], Pc[1:, 0], Pc[1:, 1])
                k = int(np.argmin(dist[0]))
                cm = float(it.get("at", 0.5)) * L[k]
                if it.get("gable"):
                    # (11-06) a pediment, solid: a raised block over the roofline (from the parapet up to its shoulders), a low triangle over it, a
                    # cornice along its shoulders and along both rakes, standing a little proud of the wall (a photo of Ding How Shopping Center)
                    gw, gh = (float(v) for v in it["gable"])
                    c0, c1 = max(-0.2, cm - gw / 2), min(L[k] + 0.2, cm + gw / 2)
                    ptop = top + (0.8 if hgt >= 5.0 else 0.0)
                    hb, hs, hp = top + 0.2, ptop + gh * 0.7, ptop + gh
                    wallk = "wall#" + lk["wall"]
                    tg_ = tags or {}
                    cornk = "trim#" + (self.colour_of(tg_.get("facade:cornice")) if tg_.get("facade:cornice") else self.shade(lk["wall"], 1.06))
                    self._wall_solid(key, wallk, Pc, k, de, dn, oe, on, [(c0, hb), (c1, hb), (c1, hs), (cm, hp), (c0, hs)], -0.4, 0.12)
                    self._wall_solid(key, cornk, Pc, k, de, dn, oe, on, [(c0 - 0.32, hs - 0.24), (c1 + 0.32, hs - 0.24), (c1 + 0.32, hs), (c0 - 0.32, hs)], -0.55, 0.42)
                    for sgn in (-1.0, 1.0):   # the rakes: from a shoulder's end to the peak, the cornice's thickness over the slope
                        ce_ = cm + sgn * (gw / 2 + 0.32)
                        ta = 0.24
                        pr = [(ce_, hs), (cm, hp + 0.02), (cm, hp + 0.02 + ta), (ce_, hs + ta)] if sgn < 0 else [(cm, hp + 0.02), (ce_, hs), (ce_, hs + ta), (cm, hp + 0.02 + ta)]
                        self._wall_solid(key, cornk, Pc, k, de, dn, oe, on, pr, -0.55, 0.42)
                    if it.get("emblem"):
                        es = min(gw * 0.46, hs - ptop - 0.34)   # over the building's cornice, the block's whole height
                        e_h0 = ptop + 0.12
                        tex_quad("emblem:%s" % it["emblem"], k, cm - es / 2, cm + es / 2, e_h0, e_h0 + es, 0.14,
                                 [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
                    made["gables from a source"] = made.get("gables from a source", 0) + 1
                if it.get("lettering"):
                    lt = it["lettering"]
                    words, col = lt[0], lt[1]
                    lh, lw = float(lt[2]) if len(lt) > 2 else 0.6, float(lt[3]) if len(lt) > 3 else 8.0
                    lw = min(lw, L[k] - 0.4)
                    h0 = min(ref + float(st.get("shop_floor_m", 4.6)) + 0.1, top - lh - 0.2)
                    tex_quad("letters:%s:%.1f:%s" % (col, max(1.0, round(lw / lh * 2.0) / 2.0), words), k, cm - lw / 2, cm + lw / 2, h0, h0 + lh, 0.06,
                             [(0.0, 1.0), (1.0, 1.0), (1.0, 0.0), (0.0, 0.0)])
                    made["lettering from a source"] = made.get("lettering from a source", 0) + 1
        # the rule's: the walls a street runs in front of
        facing = np.full(nw, np.inf)
        for k in range(nw):
            if L[k] >= 4.0:
                facing[k] = self.street_in_front(Pc[k, 0] + de[k] * L[k] / 2, Pc[k, 1] + dn[k] * L[k] / 2, oe[k], on[k], de[k], dn[k], 80.0)
        walls = [k for k in range(nw) if facing[k] <= reach]
        if not walls and not own and np.isfinite(facing).any():
            walls = [int(np.argmin(facing))]
        walls = [k for k in walls if k not in own]
        if not walls:
            return
        wa = np.array(walls)
        per = {k: [] for k in walls}
        for se, sn, tg in shops:
            dist, tt = S.seg_dist(np.array([se]), np.array([sn]), Pc[wa, 0], Pc[wa, 1], Pc[wa + 1, 0], Pc[wa + 1, 1])
            q = int(np.argmin(dist[0]))
            per[walls[q]].append((float(tt[0, q]) * L[walls[q]], self.sign_words(tg)))
        all_words = [w for k in walls for _c, w in per[k] if w]
        for k in walls:
            usable = L[k] - 0.8
            nb = max(len(per[k]), int(math.ceil(usable / 10.0)))
            bw = usable / nb
            slots = [None] * nb
            for c, w in sorted(per[k]):
                q = min(nb - 1, max(0, int((c - 0.4) / bw)))
                free = [z for z in range(nb) if slots[z] is None]
                if free:
                    q = min(free, key=lambda z: abs(z - q))
                    slots[q] = w or ""
            for q in range(nb):
                w = slots[q]
                if w is None and not per[k]:
                    w = all_words[q % len(all_words)] if all_words else self.ANY_WORDS[rnd.randrange(len(self.ANY_WORDS))]
                aw = self.AWNINGS[rnd.randrange(len(self.AWNINGS))] if (lk["awning"] and rnd.random() < 0.55) else None
                bay(k, 0.4 + q * bw, 0.4 + (q + 1) * bw, sign=[w] if w else None, awning=aw)
            made["fronts"] = made.get("fronts", 0) + 1

    def sign_image(self, name):
        """(11-06) a sign's face, 512 x 128: its words in Be Vietnam Pro (sign_font()), two lines when they say A|B (the first big, the second
        small), drawn at the sign's own shape and fitted to the texture"""
        from PIL import Image, ImageDraw
        _, bg, fg, asp, words = name.split(":", 4)
        H2 = 256
        W2 = max(H2, int(round(H2 * float(asp))))
        im = Image.new("RGBA", (W2, H2), "#" + bg)
        d = ImageDraw.Draw(im)
        d.rectangle([8, 8, W2 - 9, H2 - 9], outline="#" + fg, width=6)
        lines = words.split("|")
        shares = [0.46, 0.24] if len(lines) > 1 else [0.56]
        y = (H2 - H2 * (sum(shares) + 0.1 * (len(lines) - 1))) / 2.0
        for ln, sh in zip(lines, shares):
            size = int(H2 * sh * 1.5)
            while True:
                f = self.sign_font(size)
                x0, y0, x1, y1 = d.textbbox((0, 0), ln, font=f)
                if (x1 - x0 <= W2 - 52 and y1 - y0 <= H2 * sh) or size <= 12:
                    break
                size -= 4
            d.text(((W2 - (x1 - x0)) / 2 - x0, y + (H2 * sh - (y1 - y0)) / 2 - y0), ln, font=f, fill="#" + fg)
            y += H2 * sh + H2 * 0.1
        return im.resize((512, 128), Image.LANCZOS)

    def sign_atlas(self, items, i, j):
        """(11-06) a cell's signs in one texture (one mesh, one draw): each sign a row of 512 x 128, its quads' texture coordinates moved onto its row"""
        from PIL import Image
        signs = [(t, q) for t, q in items if t.startswith("sign:")]
        rest = [(t, q) for t, q in items if not t.startswith("sign:")]
        if not signs:
            return rest
        if not hasattr(self, "_weave_png"):
            self._weave_png = {}
        out = []
        for c0 in range(0, len(signs), 16):
            chunk = signs[c0:c0 + 16]
            rows = 1
            while rows < len(chunk):
                rows *= 2
            Ht = 128 * rows
            atlas = Image.new("RGBA", (512, Ht), (0, 0, 0, 255))
            quads = []
            for r, (t, qs) in enumerate(chunk):
                atlas.paste(self.sign_image(t), (0, r * 128))
                for corners, uvs in qs:
                    quads.append((corners, [((1.0 + u * 510.0) / 512.0, (r * 128 + 2.0 + v * 124.0) / Ht) for u, v in uvs]))
            name = "signs@%d,%d/%d" % (i, j, c0 // 16)
            self._weave_png[name] = to_png(atlas.tobytes(), 512, Ht)
            out.append((name, quads))
        return rest + out

    def boards_texture(self, name):
        """(11-06) plywood over a shop's windows, painted: a mural of our own design (the real one at 12th and Jackson is someone's work, not copied),
        bright shapes in dark outlines over four sheets, a wave, drips and scrawls, the sheets' seams. 512 x 256 is 4.88 x 2.44 m"""
        from PIL import Image, ImageDraw
        seed = int(name.split(":")[1])
        rnd = random.Random(7100 + seed)
        W, H = 512, 256
        im = Image.new("RGBA", (W, H), (176, 140, 96, 255))
        d = ImageDraw.Draw(im)
        for y in range(0, H, 3):
            d.line([(0, y), (W, y + rnd.randint(-2, 2))], fill=(166 + rnd.randint(-10, 10), 128 + rnd.randint(-8, 8), 86 + rnd.randint(-6, 6), 255))
        PAL = [(232, 64, 92), (255, 196, 40), (40, 170, 220), (120, 210, 90), (150, 80, 200), (250, 120, 40), (240, 240, 235)]
        INK = (24, 22, 26, 255)
        field = PAL[(seed * 3 + 2) % len(PAL)]
        d.rectangle([4, 14 + rnd.randint(0, 10), W - 4, H - 18 - rnd.randint(0, 12)], fill=field + (255,))
        for _ in range(10):
            r = rnd.randint(22, 64)
            x, y = rnd.randint(-10, W + 10), rnd.randint(40, H - 40)
            col = PAL[rnd.randrange(len(PAL))]
            if col == field:
                continue
            d.ellipse([x - r, y - r * 0.8, x + r, y + r * 0.8], fill=col + (255,), outline=INK, width=4)
            d.ellipse([x - r * 0.45, y - r * 0.3, x + r * 0.1, y + r * 0.1], fill=(250, 250, 245, 255))
        band = PAL[(seed * 5 + 1) % len(PAL)]
        pts = [(x, H * 0.62 + 20 * math.sin(x / 34.0 + seed * 1.7)) for x in range(-8, W + 16, 8)]
        d.line(pts, fill=band + (255,), width=16)
        d.line([(x, y - 9) for x, y in pts], fill=INK, width=3)
        d.line([(x, y + 9) for x, y in pts], fill=INK, width=3)
        for _ in range(26):
            x, y0 = rnd.randint(4, W - 4), rnd.randint(int(H * 0.45), H - 30)
            col = PAL[rnd.randrange(len(PAL))]
            d.line([(x, y0), (x, y0 + rnd.randint(10, 46))], fill=col + (255,), width=3)
        for _ in range(4):
            x, y = rnd.randint(20, W - 20), rnd.randint(30, H - 40)
            path = [(x, y)]
            for _k in range(14):
                x += rnd.randint(-6, 14)
                y += rnd.randint(-12, 12)
                path.append((x, y))
            d.line(path, fill=(INK if rnd.random() < 0.6 else (245, 245, 240, 255)), width=4)
        for x in range(0, W + 1, 128):
            d.line([(x, 0), (x, H)], fill=(70, 52, 36, 255), width=3)
        return to_png(im.tobytes(), W, H)

    def billboard_texture(self, name):
        """(11-06) a billboard's face, 512 x 128: its words on a plain field, drawn at the board's own shape and fitted to the texture"""
        from PIL import Image, ImageDraw
        _, bg, fg1, fg2, asp, text = name.split(":", 5)
        H2 = 256
        W2 = int(round(H2 * float(asp)))
        im = Image.new("RGBA", (W2, H2), "#" + bg)
        d = ImageDraw.Draw(im)
        lines = text.split("|")
        heights = [0.5, 0.26] if len(lines) > 1 else [0.62]
        y = H2 * 0.12
        for li, (ln, hs) in enumerate(zip(lines, heights)):
            size = int(H2 * hs * 1.3)
            while True:
                f = self.sign_font(size)
                x0, y0, x1, y1 = d.textbbox((0, 0), ln, font=f, stroke_width=2)
                if (x1 - x0 <= W2 * 0.9 and y1 - y0 <= H2 * hs) or size <= 12:
                    break
                size -= 4
            col = "#" + (fg1 if li == 0 else fg2)
            d.text(((W2 - (x1 - x0)) / 2 - x0, y - y0), ln, font=f, fill=col, stroke_width=2, stroke_fill=col)
            y += H2 * hs + H2 * 0.08
        im = im.resize((512, 128), Image.LANCZOS)
        return to_png(im.tobytes(), 512, 128)

    def tex_png(self, tex):
        """(11-06) a texture by its name: a fence's weave, the crosswalk's mural, boards painted over windows, a billboard's face"""
        if tex == "mural":
            return self.mural_texture()
        if tex.startswith("boards:"):
            return self.boards_texture(tex)
        if tex.startswith("billboard:"):
            return self.billboard_texture(tex)
        if tex.startswith("banner:"):
            return self.banner_texture(tex)
        if tex.startswith("school:"):
            return self.school_texture(tex)
        if tex.startswith("emblem:"):
            return self.emblem_texture(tex)
        if tex.startswith("letters:"):
            return self.letters_texture(tex)
        if tex == "razor":
            return self.razor_texture()
        return self.weave_texture(tex)

    def buildings_geometry(self):
        st = self.st
        cells = self.cells
        why = {}
        made = 0
        # a part says something about one piece of a building: its outline is left out wherever parts stand inside it
        part_mid = np.array([P.mean(axis=0) for _, _, P in self.parts]) if self.parts else np.zeros((0, 2))
        todo = []
        for bid, t, P in self.buildings:
            if len(P) < 3:
                continue
            if len(part_mid):
                lo, hi = P.min(axis=0), P.max(axis=0)
                near = np.nonzero((part_mid[:, 0] >= lo[0]) & (part_mid[:, 0] <= hi[0]) & (part_mid[:, 1] >= lo[1]) & (part_mid[:, 1] <= hi[1]))[0]
                if len(near) and S.inside(part_mid[near, 0], part_mid[near, 1], P).any():
                    why["outline left to its parts"] = why.get("outline left to its parts", 0) + 1
                    continue
            todo.append((bid, t, P, False))
        todo += [(bid, t, P, True) for bid, t, P in self.parts if len(P) >= 3]
        self.footprints = []
        looks = {}
        # (11-06) where the shops are: a building with one in it has a shopfront
        shop_ids = [n for n, tg in self.ntags.items() if n in self.ne and ("shop" in tg or tg.get("amenity") in self.SHOPS or "disused:shop" in tg
                    or tg.get("disused:amenity") in self.SHOPS)] if st.get("facades") else []
        shop_pts = np.array([[self.ne[n], self.nn[n]] for n in shop_ids]) if shop_ids else np.zeros((0, 2))
        # (11-06) the roads a shop's front can face (not service roads: an alley or a lot's aisle), and the fronts a source gives (`shopfront` features)
        rl = [l for l in self.lines if l["kind"] == "road" and not l["bridge"] and len(l["C"]) >= 2 and l["hw"] not in ("service", "track")]
        self._sa = np.concatenate([l["C"][:-1] for l in rl]) if rl else np.zeros((0, 2))
        self._sb = np.concatenate([l["C"][1:] for l in rl]) if rl else np.zeros((0, 2))
        self.front_specs, self.crest_specs = {}, {}
        for fz in self.cfg.get("features", []):
            if fz.get("kind") == "shopfront":
                self.front_specs.setdefault(int(fz["way"]), []).append(fz)
            elif fz.get("kind") == "crest":
                self.crest_specs.setdefault(int(fz["way"]), []).append(fz)
        self.made_fronts = {}
        for bid, t, P, part in todo:
            mid = P.mean(axis=0)
            i, j, ok = cells.cell_of(np.array([mid[0]]), np.array([mid[1]]))
            if not ok[0]:
                continue
            i, j = int(i[0]), int(j[0])
            hgt, src = self.building_height(t)
            why[src] = why.get(src, 0) + 1
            g = self.field.sample(P[:, 0], P[:, 1])
            ref = float(g.mean())
            base = float(g.min()) - 0.5
            mh = metres(t.get("min_height")) or 0.0
            if part and mh > 0:
                base = ref + mh
            top = ref + hgt
            if top <= base + 0.5:
                top = base + 0.5
            kind = "bldg%d" % (abs(bid) % len(st["colours"]["buildings"]))
            if st.get("facades"):
                area = abs(S.area(P))
                lo_, hi_ = P.min(axis=0), P.max(axis=0)
                near_ = np.nonzero((shop_pts[:, 0] >= lo_[0]) & (shop_pts[:, 0] <= hi_[0]) & (shop_pts[:, 1] >= lo_[1]) & (shop_pts[:, 1] <= hi_[1]))[0] if len(shop_pts) else []
                ins = [int(q) for q in np.asarray(near_)[S.inside(shop_pts[near_, 0], shop_pts[near_, 1], P)]] if len(near_) else []
                shops = [(float(shop_pts[q, 0]), float(shop_pts[q, 1]), self.ntags[shop_ids[q]]) for q in ins]
                shop = (t.get("building") in ("retail", "commercial", "supermarket", "kiosk")) or bool(shops) or bid in self.front_specs or bid in self.crest_specs
                if shop and src == "levels" and not part:
                    # (11-06) a shop's floor is taller than a home's: its ground floor shop_floor_m, the floors over it level_m
                    top += float(st.get("shop_floor_m", 4.6)) - float(st["level_m"])
                lk = self.facade(bid, t, P, base, ref, top, mid, area, shop, key=(i, j), shops=shops)
                looks[lk["fam"]] = looks.get(lk["fam"], 0) + 1
                self.footprints.append((i, j, P.min(axis=0), P.max(axis=0), base, top, "wall#" + lk["wall"]))
                made += 1
                continue
            # walls: the ring's counter-clockwise, so outward is right of each edge
            Pc = np.vstack([P, P[:1]])
            d = Pc[1:] - Pc[:-1]
            L = np.maximum(np.hypot(*d.T), 1e-9)
            oe, on = d[:, 1] / L, -d[:, 0] / L
            lo3 = cells.xyz(Pc[:, 0], Pc[:, 1], np.full(len(Pc), base))
            hi3 = cells.xyz(Pc[:, 0], Pc[:, 1], np.full(len(Pc), top))
            hint = np.stack([oe, np.zeros(len(oe)), -on], axis=1)
            mid_e = np.full(len(oe), mid[0])
            mid_n = np.full(len(oe), mid[1])
            cells.add_quads(kind, lo3[:-1], lo3[1:], hi3[1:], hi3[:-1], hint, mid_e, mid_n)
            tris, clean = S.earclip(P)
            if not clean:
                why["roof cut as a fan"] = why.get("roof cut as a fan", 0) + 1
            top3 = hi3[:-1]
            cells.add_tris("roof", top3[np.array(tris)], i, j)
            self.footprints.append((i, j, P.min(axis=0), P.max(axis=0), base, top, kind))
            made += 1
        self.say("buildings: %d made; heights from: %s" % (made, ", ".join("%s %d" % kv for kv in sorted(why.items()))))
        if looks:
            self.say("their looks: " + ", ".join("%s %d" % kv for kv in sorted(looks.items())))
        if self.made_fronts:
            self.say("(11-06) shopfronts: " + ", ".join("%s %d" % kv for kv in sorted(self.made_fronts.items())))
        # the far stand-ins' colours: the strip's own, then every wall colour a building has
        self.far_sw = list(self.FAR_SW) + sorted({f[6] for f in self.footprints if f[6].startswith("wall#")})
        self.lap("stood the buildings up")

    # ------------------------------------------------------------ 7. cells
    def surface(self, e, n):
        """the highest thing a body can stand on at a point: the ground, or a deck over it"""
        h = float(self.field.sample(e, n))
        for l in self.lines:
            if not l["bridge"] or l["kind"] == "rail":
                continue
            C = l["C"]
            dist, tt = S.seg_dist(np.array([e]), np.array([n]), C[:-1, 0], C[:-1, 1], C[1:, 0], C[1:, 1])
            k = int(np.argmin(dist[0]))
            if dist[0, k] <= l["w"] / 2:
                hh = l["hc"][k] + (l["hc"][k + 1] - l["hc"][k]) * tt[0, k]
                h = max(h, float(hh))
        return h

    def place(self, spec):
        E, N = geo.utm(spec["lat"], spec["lon"], self.zone)
        e, n = float(E - self.E0), float(N - self.N0)
        on = spec.get("on", "top")
        h = float(self.field.sample(e, n)) if on == "ground" else self.surface(e, n)
        h += float(spec.get("above", 0.0))
        return e, n, h

    # ------------------------------------------------------------ (11-10) what stands in a roadway
    AUDIT_FLAT = {"ground", "paved", "road", "concrete", "bridge", "path", "sidewalk", "crossing", "curb", "lane", "track", "rail", "deck", "parapet"}
    ROADR_REACH = 3.0

    def roadr(self):
        """the roads as pictures of the field, a metre a pixel, made once: for the roads on the ground, how far inside the nearest one's edge each
        place is (metres; under 0 is outside, down to -ROADR_REACH, and -1e9 further off) and which road that is; the same for the bridges' roads, with
        the deck's height there. (ins, who, insb, whob, hb, roads)"""
        if hasattr(self, "_roadr"):
            return self._roadr
        z = self.field.z
        nr, nc = z.shape
        ins = np.full(z.shape, -1e9)
        who = np.full(z.shape, -1, dtype=np.int32)
        insb = np.full(z.shape, -1e9)
        whob = np.full(z.shape, -1, dtype=np.int32)
        hb = np.zeros(z.shape)
        roads = [l for l in self.lines if l["kind"] == "road" and len(l["C"]) >= 2]
        for q, l in enumerate(roads):
            C = l["C"]
            keep = np.unique(np.concatenate([np.arange(0, len(C), 4), [len(C) - 1]]))
            Ck = C[keep]
            hk = np.asarray(l["hc"])[keep] if l["bridge"] else None
            half = l["w"] / 2
            reach = half + self.ROADR_REACH
            A, I = (insb, whob) if l["bridge"] else (ins, who)
            for k in range(len(Ck) - 1):
                (ax, ay), (bx, by) = Ck[k], Ck[k + 1]
                c0, c1 = max(int(math.floor(min(ax, bx) - reach - self.fe0)) - 1, 0), min(int(math.ceil(max(ax, bx) + reach - self.fe0)) + 1, nc - 1)
                r0, r1 = max(int(math.floor(min(ay, by) - reach - self.fn0)) - 1, 0), min(int(math.ceil(max(ay, by) + reach - self.fn0)) + 1, nr - 1)
                if c1 < c0 or r1 < r0:
                    continue
                px = self.fe0 + np.arange(c0, c1 + 1)[None, :]
                py = self.fn0 + np.arange(r0, r1 + 1)[:, None]
                dx, dy = bx - ax, by - ay
                t = np.clip(((px - ax) * dx + (py - ay) * dy) / max(dx * dx + dy * dy, 1e-12), 0, 1)
                v = half - np.hypot(px - (ax + t * dx), py - (ay + t * dy))
                win = (v > -self.ROADR_REACH) & (v > A[r0:r1 + 1, c0:c1 + 1])
                A[r0:r1 + 1, c0:c1 + 1][win] = v[win]
                I[r0:r1 + 1, c0:c1 + 1][win] = q
                if hk is not None:
                    hb[r0:r1 + 1, c0:c1 + 1][win] = (hk[k] + (hk[k + 1] - hk[k]) * t)[win]
        self._roadr = (ins, who, insb, whob, hb, roads)
        return self._roadr

    def in_road(self, e, n, margin=0.0):
        """whether a place is inside a road's own band on the ground (any road's, not only the nearest one's line), or within margin of its edge"""
        ins = self.roadr()[0]
        c = min(max(int(round(e - self.fe0)), 0), ins.shape[1] - 1)
        r = min(max(int(round(n - self.fn0)), 0), ins.shape[0] - 1)
        return bool(ins[r, c] > -margin)

    def road_in(self, e, n, bridge=False):
        """how far inside a road's band a place is (metres, under 0 outside), and the way toward that road's line from there (a unit vector, east
        and north; 0, 0 where there's none to tell). The roads on the ground, or the bridges' with bridge"""
        r_ = self.roadr()
        A = r_[2] if bridge else r_[0]
        c = min(max(int(round(e - self.fe0)), 1), A.shape[1] - 2)
        r = min(max(int(round(n - self.fn0)), 1), A.shape[0] - 2)
        d = float(A[r, c])
        if d < -self.ROADR_REACH:
            return d, 0.0, 0.0
        lo = -self.ROADR_REACH
        gx = max(float(A[r, c + 1]), lo) - max(float(A[r, c - 1]), lo)
        gy = max(float(A[r + 1, c]), lo) - max(float(A[r - 1, c]), lo)
        L = math.hypot(gx, gy)
        return (d, gx / L, gy / L) if L > 1e-6 else (d, 0.0, 0.0)

    def road_at(self, e, n):
        """the class of the road on the ground whose band a place is in or nearest (motorway, residential, service ...), or None"""
        _ins, who, _ib, _wb, _hb, roads = self.roadr()
        c = min(max(int(round(e - self.fe0)), 0), who.shape[1] - 1)
        r = min(max(int(round(n - self.fn0)), 0), who.shape[0] - 1)
        return roads[who[r, c]]["hw"] if who[r, c] >= 0 else None

    def deck_over(self, e, n):
        """the underside of a bridge's deck over a place (its road's band and ROADR_REACH past it), metres above the sea; None when no deck is over it"""
        _ins, _who, insb, _whob, hb, _roads = self.roadr()
        c = min(max(int(round(e - self.fe0)), 0), insb.shape[1] - 1)
        r = min(max(int(round(n - self.fn0)), 0), insb.shape[0] - 1)
        return float(hb[r, c] - self.st["deck_m"]) if insb[r, c] > -self.ROADR_REACH else None

    def junctions(self):
        """where roads meet: the places of the nodes that two roads cross at or three meet at, as (m, 2); made once"""
        if not hasattr(self, "_junctions"):
            deg, ends = {}, {}
            for l in self.lines:
                if l["kind"] != "road" or l["bridge"]:
                    continue
                for nid in l["ids"]:
                    deg[nid] = deg.get(nid, 0) + 1
                for nid in (l["ids"][0], l["ids"][-1]):
                    ends[nid] = ends.get(nid, 0) + 1
            js = [nid for nid, dg in deg.items() if (dg >= 3 or (dg == 2 and ends.get(nid, 0) < 2)) and nid in self.ne]
            self._junctions = np.array([[self.ne[nid], self.nn[nid]] for nid in js]) if js else np.zeros((0, 2))
        return self._junctions

    def audit(self):
        """WHAT STANDS IN A ROADWAY (11-10). The rider met a wall across S Jackson St and a lantern post in its median, and nothing had said so: every
        check so far looked at a place somebody thought of. This looks at all of them. Everything the cook made is held against every road's own band:
        a face between audit_low_m and audit_high_m over the road's surface (a rider's height), more than 0.2 m inside its edge, is something standing
        in the roadway. Bridges are held against their own decks. Said in the report by what it is and where, the ones in a travel lane first (more
        than audit_lane_m in from the edge; nearer the edge is the curb lane, where parked cars are meant to be). It changes nothing: it says."""
        st, U = self.st, self.U
        z = self.field.z
        nr, nc = z.shape
        lo_m, hi_m, lane_m = float(st.get("audit_low_m", 0.35)), float(st.get("audit_high_m", 2.2)), float(st.get("audit_lane_m", 3.0))
        ins, who, insb, whob, hb, roads = self.roadr()
        hits = {}   # (what, on a bridge, 4 m square) -> [faces, deepest in, e sum, n sum, road]

        def held(what, e, n, h0, h1):
            c = np.clip(np.rint(e - self.fe0).astype(int), 0, nc - 1)
            r = np.clip(np.rint(n - self.fn0).astype(int), 0, nr - 1)
            for A, I, base, br in ((ins, who, z, 0), (insb, whob, hb, 1)):
                d = A[r, c]
                top = base[r, c]
                hit = (d > 0.2) & (h1 > top + lo_m) & (h0 < top + hi_m)
                for k in np.nonzero(hit)[0]:
                    key = (what, br, int(round(e[k] / 4.0)), int(round(n[k] / 4.0)))
                    v = hits.setdefault(key, [0, 0.0, 0.0, 0.0, int(I[r[k], c[k]])])
                    v[0] += 1
                    v[1] = max(v[1], float(d[k]))
                    v[2] += float(e[k])
                    v[3] += float(n[k])
        for (i, j), kinds in sorted(self.cells.parts.items()):
            for kind, chunks in kinds.items():
                what = kind
                if kind.split("#")[0] in self.AUDIT_FLAT:
                    continue
                for pos, _nrm, idx in chunks:
                    T = np.asarray(pos)[np.asarray(idx)].reshape(-1, 3, 3)
                    if not len(T):
                        continue
                    hh = T[:, :, 1] / U + self.floor
                    held(what, T[:, :, 0].mean(axis=1) / U, -T[:, :, 2].mean(axis=1) / U, hh.min(axis=1), hh.max(axis=1))
        for (i, j), texs in sorted(getattr(self, "texq", {}).items()):
            for tex, quads in texs.items():
                Q = np.array([q[0] for q in quads], dtype=np.float64)   # (m, 4, 3): east, height, north
                if len(Q):
                    held("fence (%s)" % str(tex).split(":")[0], Q[:, :, 0].mean(axis=1), Q[:, :, 2].mean(axis=1), Q[:, :, 1].min(axis=1), Q[:, :, 1].max(axis=1))
        self.audit_hits = hits
        rows = []
        for (what, br, _a, _b), (nf, deep, se, sn, q) in hits.items():
            l = roads[q] if 0 <= q < len(roads) else None
            rows.append((deep > lane_m, what, se / nf, sn / nf, deep, nf, br, (l["t"].get("name") or l["hw"]) if l else "?", l["hw"] if l else "?"))
        lane = sorted((r for r in rows if r[0]), key=lambda r: (r[1], r[2], r[3]))
        curb = sorted((r for r in rows if not r[0]), key=lambda r: (r[1], r[2], r[3]))
        short = lambda w: w.split("#")[0]
        tally = lambda rs: ", ".join("%s %d" % kv for kv in sorted({w: sum(1 for r in rs if short(r[1]) == w) for w in {short(r[1]) for r in rs}}.items()))
        prefix = self.cfg.get("prefix", "city")
        self.say("the roadways: %d places with something standing in a travel lane (%s); %d more in a curb lane (%s). Each is in %s_roadways.txt" % (
            len(lane), tally(lane) or "nothing", len(curb), tally(curb) or "nothing", prefix))
        with open(os.path.join(self.out, prefix + "_roadways.txt"), "w", newline="\n", encoding="utf-8") as f:
            f.write("# what stands in a roadway (skate_src/citymap, audit()): where, what, how far in from the road's edge, which road. Generated: cook again\n")
            for cls, rs in (("lane", lane), ("curb", curb)):
                for r in rs:
                    f.write("%s %-22s east %8.1f north %8.1f  in %4.1f m  %s (%s)%s  %d faces\n" % (
                        cls, r[1], r[2], r[3], r[4], r[7], r[8], " on its bridge" if r[6] else "", r[5]))
        self.lap("looked at what stands in the roadways")

    def write(self):
        st = self.st
        C = float(st["cell_m"])
        U = self.U
        prefix = self.cfg.get("prefix", "city")
        props = os.path.join(self.out, "props")
        sets = os.path.join(self.out, "sets")
        os.makedirs(props, exist_ok=True)
        os.makedirs(sets, exist_ok=True)
        lines = ["# %s: cooked by skate_src/citymap from OpenStreetMap (c) OpenStreetMap contributors, ODbL, and USGS 3DEP elevation. Generated: don't edit, cook again" % prefix,
                 "# %d x %d cells of %g m; %g units a metre; y 0 is %.2f m above sea level" % (self.ni, self.nj, C, U, self.floor),
                 "climb", "streaming %g %g" % tuple(st["streaming"]), "far %g" % st["far_plane"], "fardetail %s_far %g" % (prefix, st["far_distance"])]
        lines += ["surface %s %s" % (kind, " ".join(words)) for kind, words in st["surfaces"]]
        # (11-06) the set's light and air from the config's "light": fog (density a unit [r g b], so the city ends in air and not in nothing), sky, ground,
        # haze, exposure, and sun only when a game doesn't light it by its own hour
        lt = self.cfg.get("light", {})
        for key in ("sun", "sky", "ground", "fog"):
            if key in lt:
                v = lt[key] if isinstance(lt[key], list) else [lt[key]]
                lines.append("%s %s" % (key, " ".join("%g" % x for x in v)))
        for key in ("haze", "exposure"):
            if key in lt:
                lines.append("%s %g" % (key, lt[key]))
        for name, spec in sorted(self.cfg.get("marks", {}).items()):
            e, n, h = self.place(spec)
            lines.append("mark %s %.3f %.3f %.3f %g" % (name, e * U, (h - self.floor) * U, -n * U, spec.get("yaw", 0)))
        for name, v in sorted(self.cfg.get("views", {}).items()):
            for suffix, spec in (("", v["eye"]), ("_at", v["at"])) + ((("_hero", v["hero"]),) if "hero" in v else ()):
                e, n, h = self.place(spec)
                lines.append("mark view_%s%s %.3f %.3f %.3f 0" % (name, suffix, e * U, (h - self.floor) * U, -n * U))
        far = []
        stats = []
        nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
                 {"name": "cell", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
        I16 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
        for i in range(self.ni):
            for j in range(self.nj):
                stem = "%s_%02d_%02d" % (prefix, i, j)
                ce, cn = self.be0 + (i + 0.5) * C, self.bn0 + (j + 0.5) * C
                cx, cz = ce * U, -cn * U
                meshes = []
                tris = 0
                for kind, chunks in sorted(self.cells.parts.get((i, j), {}).items()):
                    pos = np.concatenate([c[0] for c in chunks])
                    nrm = np.concatenate([c[1] for c in chunks])
                    off = np.cumsum([0] + [len(c[0]) for c in chunks[:-1]])
                    idx = np.concatenate([c[2] + o for c, o in zip(chunks, off)])
                    pos = pos - np.array([cx, 0.0, cz])
                    nv = len(pos)
                    joints, weights, bone_map = rigid_skin(nv, 0)
                    kn = self.kind_name(kind)
                    sheer = "#" in kn and len(kn.split("#", 1)[1]) == 8   # (11-06) #rrggbbaa: sheer, drawn blended (the engine reads the alpha from the name)
                    meshes.append({"name": kn, "node": 1, "pos": pos.astype(np.float32).ravel().tolist(), "nrm": nrm.astype(np.float32).ravel().tolist(),
                                   "uv": None, "idx": idx.astype(np.int64).tolist(), "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": I16, "tex": -1,
                                   "blend": sheer})
                    tris += len(idx) // 3
                # (11-06) what carries a texture (a see-through fence's weave): its own mesh, both faces, with the texture in this cell's pack
                texs = []
                for tex, quads in self.sign_atlas(sorted(getattr(self, "texq", {}).get((i, j), {}).items()), i, j):
                    Q = np.array([q[0] for q in quads], dtype=np.float64)        # (m, 4, 3) map terms (e, up, n)
                    UV = np.array([q[1] for q in quads], dtype=np.float64)       # (m, 4, 2)
                    X = self.cells.xyz(Q[..., 0], Q[..., 2], Q[..., 1]) - np.array([cx, 0.0, cz])   # (m, 4, 3) engine, the cell's own
                    nrm = np.cross(X[:, 1] - X[:, 0], X[:, 3] - X[:, 0])
                    nrm /= np.maximum(np.linalg.norm(nrm, axis=1), 1e-12)[:, None]
                    m = len(Q)
                    pos = np.concatenate([X.reshape(-1, 3), X.reshape(-1, 3)])
                    nn = np.concatenate([np.repeat(nrm, 4, axis=0), np.repeat(-nrm, 4, axis=0)])
                    uv = np.concatenate([UV.reshape(-1, 2), UV.reshape(-1, 2)])
                    base = (np.arange(m) * 4)[:, None]
                    front = (base + np.array([0, 1, 2, 0, 2, 3])[None, :]).reshape(-1)
                    back = (base + 4 * m + np.array([0, 2, 1, 0, 3, 2])[None, :]).reshape(-1)
                    idx = np.concatenate([front, back])
                    joints, weights, bone_map = rigid_skin(len(pos), 0)
                    meshes.append({"name": "fence", "node": 1, "pos": pos.astype(np.float32).ravel().tolist(), "nrm": nn.astype(np.float32).ravel().tolist(),
                                   "uv": uv.astype(np.float32).ravel().tolist(), "idx": idx.astype(np.int64).tolist(), "joints": joints, "weights": weights,
                                   "boneMap": bone_map, "ibm": I16, "tex": len(texs), "blend": bool(st.get("weave_blend", 1)) and (tex in self.WEAVES or str(tex).startswith(("letters:", "emblem:")))})
                    if not hasattr(self, "_weave_png"):
                        self._weave_png = {}
                    if tex not in self._weave_png:
                        self._weave_png[tex] = self.tex_png(tex)
                    texs.append(self._weave_png[tex])
                    tris += len(idx) // 3
                path = os.path.join(props, stem + ".rpack")
                nbytes = write_pack(path, nodes, meshes, [], texs, verbose=False, fresh=True)
                stats.append((stem, tris, nbytes))
                lines.append("cell %s %.3f 0 %.3f" % (stem, cx, cz))
                far.append(self.far_mesh(stem, i, j))
        fnodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
                  {"name": "far", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
        fbytes = write_pack(os.path.join(props, prefix + "_far.rpack"), fnodes, far, [], [self.far_texture()], verbose=False, fresh=True)
        with open(os.path.join(sets, prefix + ".set"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        tr = np.array([s[1] for s in stats])
        by = np.array([s[2] for s in stats])
        worst = sorted(stats, key=lambda s: -s[1])[:5]
        over = [s for s in stats if s[1] > st["budget"]["tris"] or s[2] > st["budget"]["bytes"]]
        self.say("cells: %d, %.1f MB in all; triangles a cell %d mean, %d most; bytes a cell %.0f KB mean, %.0f KB most; the budget is %d triangles and %.0f KB, and %d cells are over it" % (
            len(stats), by.sum() / 1048576, tr.mean(), tr.max(), by.mean() / 1024, by.max() / 1024, st["budget"]["tris"], st["budget"]["bytes"] / 1024, len(over)))
        self.say("the heaviest cells: " + ", ".join("%s %d tris %.0f KB" % (s[0], s[1], s[2] / 1024) for s in worst))
        self.say("far detail: %s_far, %d meshes, %.0f KB" % (prefix, len(far), fbytes / 1024))
        with open(os.path.join(self.out, prefix + "_cells.txt"), "w", newline="\n") as f:
            f.write("# cell triangles bytes\n" + "".join("%s %d %d\n" % s for s in stats))
        self.lap("wrote the cells")

    FAR_SW = ["ground", "road", "bldg", "roof", "paved"]

    def far_texture(self):
        c = self.st["colours"]
        cols = [c["ground"], c["road"], c["buildings"][0], c["roof"], c["paved"]] + [k[5:] for k in getattr(self, "far_sw", self.FAR_SW)[len(self.FAR_SW):]]
        px = bytearray()
        for _y in range(4):
            for x in range(4 * len(cols)):
                h = rgb(cols[x // 4])
                px += bytes((int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 255))
        return to_png(bytes(px), 4 * len(cols), 4)

    def far_mesh(self, stem, i, j):
        """what's left of a cell from a long way off, in the world's own coordinates: its ground every far_step_m, and its tallest buildings as boxes"""
        st = self.st
        C = float(st["cell_m"])
        g = float(st["far_step_m"])
        m = int(round(C / g))
        es = self.be0 + i * C + g * np.arange(m + 1)
        ns = self.bn0 + j * C + g * np.arange(m + 1)
        E, N = np.meshgrid(es, ns)
        H = self.field.sample(E, N)
        X = self.cells.xyz(E, N, H)
        pos, nrm, uv, idx = [], [], [], []

        def quad(a, b, c, d, sw, up=False):
            a, b, c, d = (np.asarray(v, dtype=np.float64) for v in (a, b, c, d))
            n = np.cross(c - a, d - b)
            n /= max(np.linalg.norm(n), 1e-12)
            if up and n[1] < 0:
                n = -n
            base = len(pos)
            for p in (a, b, c, d):
                pos.append(p)
                nrm.append(n)
                sws = getattr(self, "far_sw", self.FAR_SW)
                uv.append(((sws.index(sw) + 0.5) / float(len(sws)), 0.5))
            if np.dot(np.cross(b - a, c - a), n) >= 0:
                idx.extend([base, base + 1, base + 2, base, base + 2, base + 3])
            else:
                idx.extend([base, base + 2, base + 1, base, base + 3, base + 2])
        for r in range(m):
            for c in range(m):
                me, mn = E[r, c] + g / 2, N[r, c] + g / 2
                cc = min(max(int(me - self.fe0), 0), self.cover_map.shape[1] - 1)
                rr = min(max(int(mn - self.fn0), 0), self.cover_map.shape[0] - 1)
                quad(X[r, c], X[r, c + 1], X[r + 1, c + 1], X[r + 1, c], "paved" if self.cover_map[rr, cc] in (2, 3, 4) else "ground")
        if not hasattr(self, "_far_roads"):
            self._far_roads = {}
            for l in self.lines:
                if l["kind"] != "road" or l["hw"] in ("service", "track"):
                    continue
                Cf, at = S.densify(l["P"], g)
                hcf = S.along([self.bh[n] for n in l["ids"]], at) if l["bridge"] else None
                Ef, Nf, Hf = S.band(self.field, Cf, -l["w"] / 2, l["w"] / 2, 0.6, hcf, across=1e9)
                me, mn = 0.25 * (Ef[:-1, 0] + Ef[1:, 0] + Ef[:-1, 1] + Ef[1:, 1]), 0.25 * (Nf[:-1, 0] + Nf[1:, 0] + Nf[:-1, 1] + Nf[1:, 1])
                ci, cj, ok = self.cells.cell_of(me, mn)
                Xf = self.cells.xyz(Ef, Nf, Hf)
                for q in np.nonzero(ok)[0]:
                    self._far_roads.setdefault((int(ci[q]), int(cj[q])), []).append((Xf[q, 0], Xf[q + 1, 0], Xf[q + 1, 1], Xf[q, 1]))
        for a4, b4, c4, d4 in self._far_roads.get((i, j), []):
            quad(a4, b4, c4, d4, "road", True)
        mine = [f for f in self.footprints if f[0] == i and f[1] == j]
        for (_i, _j, lo, hi, base, top, fk) in sorted(mine, key=lambda f: -(f[5] - f[4]))[:st["far_buildings"]]:
            side = fk if fk.startswith("wall#") else "bldg"
            x0, x1 = lo[0] * self.U, hi[0] * self.U
            z0, z1 = -hi[1] * self.U, -lo[1] * self.U
            y0, y1 = (base - self.floor) * self.U, (top - self.floor) * self.U
            quad((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), "roof")
            quad((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), side)
            quad((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), side)
            quad((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), side)
            quad((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), side)
        P = np.array(pos)
        joints, weights, bone_map = rigid_skin(len(P), 0)
        return {"name": stem, "node": 1, "pos": P.astype(np.float32).ravel().tolist(), "nrm": np.array(nrm, dtype=np.float32).ravel().tolist(),
                "uv": np.array(uv, dtype=np.float32).ravel().tolist(), "idx": idx, "joints": joints, "weights": weights, "boneMap": bone_map,
                "ibm": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], "tex": 0, "nearest": True}

    # ------------------------------------------------------------ the numbers someone checks
    def checks(self):
        U = self.U
        for chk in self.cfg.get("checks", {}).get("bridges", []):
            ways = [l for l in self.lines if l["bridge"] and l["kind"] == "road" and chk in (l["t"].get("name"), l["t"].get("bridge:name"))]
            if not ways:
                self.say("bridge %r: no such bridge way in the box" % chk)
                continue
            # the longest run of them end to end is the span
            pts, hs = [], []
            best = max(ways, key=lambda l: S.arclen(l["C"])[-1])
            chain = [best]
            for _ in range(len(ways)):
                for l in ways:
                    if l in chain:
                        continue
                    if l["ids"][0] == chain[-1]["ids"][-1]:
                        chain.append(l)
                    elif l["ids"][-1] == chain[0]["ids"][0]:
                        chain.insert(0, l)
            for l in chain:
                pts.append(l["C"])
                hs.append(l["hc"])
            Cc, Hc = np.concatenate(pts), np.concatenate(hs)
            s = S.arclen(Cc)
            g = self.field.sample(Cc[:, 0], Cc[:, 1])
            k = int(np.argmin(np.abs(s - s[-1] / 2)))
            kk = int(np.argmax(Hc - g))
            self.say("bridge %r: %d ways end to end (%s), %.0f m long; deck at its ends %.1f m and %.1f m above the sea" % (
                chk, len(chain), " ".join(str(l["id"]) for l in chain), s[-1], Hc[0], Hc[-1]))
            self.say("   mid span (%.0f m along): deck %.2f m, ground under it %.2f m: %.2f m above the ravine floor (%.2f units; deck y %.2f, floor y %.2f)" % (
                s[k], Hc[k], g[k], Hc[k] - g[k], (Hc[k] - g[k]) * U, (Hc[k] - self.floor) * U, (g[k] - self.floor) * U))
            self.say("   highest above the ground: %.2f m, %.0f m along" % (Hc[kk] - g[kk], s[kk]))
        prof = self.cfg.get("checks", {}).get("profile")
        if prof:
            self.profile(prof)

    def profile(self, prof):
        """a street's grade along the way the road graph takes between two places, every step_m, from the cooked surfaces"""
        adj = {}
        for l in self.lines:
            if l["kind"] != "road" or l["hw"] not in GRAPH:
                continue
            ids = l["ids"]
            for k in range(len(ids) - 1):
                a, b = ids[k], ids[k + 1]
                L = math.hypot(self.ne[a] - self.ne[b], self.nn[a] - self.nn[b])
                adj.setdefault(a, []).append((b, L, l, k))
                adj.setdefault(b, []).append((a, L, l, k))
        start = prof.get("from_node") or self.cfg["origin"].get("node")
        E, N = geo.utm(prof["to"]["lat"], prof["to"]["lon"], self.zone)
        te, tn = float(E - self.E0), float(N - self.N0)
        goal = min(adj, key=lambda n: (math.hypot(self.ne[n] - te, self.nn[n] - tn), n))
        dist, prev = {start: 0.0}, {}
        q = [(0.0, start)]
        while q:
            d, a = heapq.heappop(q)
            if a == goal:
                break
            if d > dist.get(a, 1e18):
                continue
            for b, L, l, k in adj.get(a, []):
                if d + L < dist.get(b, 1e18):
                    dist[b] = d + L
                    prev[b] = (a, l, k)
                    heapq.heappush(q, (d + L, b))
        if goal not in prev:
            self.say("profile: no way through the road graph")
            return
        path = []
        n = goal
        while n != start:
            a, l, k = prev[n]
            path.append((a, n, l))
            n = a
        path.reverse()
        P, H = [], []
        for a, b, l in path:
            pa, pb = np.array([self.ne[a], self.nn[a]]), np.array([self.ne[b], self.nn[b]])
            seg = pa + (pb - pa) * np.linspace(0, 1, 9)[:, None]
            if l["bridge"]:
                # its own deck's heights
                hs = [S.along([self.bh[x] for x in l["ids"]], np.array([float(l["ids"].index(a)), float(l["ids"].index(b))]))]
                hh = np.linspace(hs[0][0], hs[0][1], 9)
            else:
                hh = self.field.sample(seg[:, 0], seg[:, 1])
            P.append(seg)
            H.append(hh)
        P, H = np.concatenate(P), np.concatenate(H)
        s = S.arclen(P)
        step = float(prof.get("step_m", 50.0))
        at = np.arange(0.0, s[-1] + 1e-6, step)
        if at[-1] < s[-1] - 1:
            at = np.append(at, s[-1])
        h = np.interp(at, s, H)
        names = []
        for a, b, l in path:
            nm = (l["t"].get("name") or l["hw"]) + (" (bridge)" if l["bridge"] else "")
            if not names or names[-1] != nm:
                names.append(nm)
        self.say("profile %s: %.0f m along %s" % (prof.get("name", ""), s[-1], " / ".join(names)))
        for k in range(len(at)):
            g = "" if k == 0 else "   grade %+.1f%%" % (100.0 * (h[k] - h[k - 1]) / max(at[k] - at[k - 1], 1e-9))
            self.say("   %5.0f m  %6.2f m above the sea  (y %6.2f)%s" % (at[k], h[k], (h[k] - self.floor) * self.U, g))

    def run(self):
        T = time.time()
        self.read()
        self.classify()
        self.profiles()
        self.carve()
        self.cover()
        self.bridges()
        self.under_decks()
        self.cells = S.Cells(self.be0, self.bn0, float(self.st["cell_m"]), self.ni, self.nj, self.U, self.floor)
        self.ground()
        self.lines_geometry()
        self.piers()
        self.buildings_geometry()
        self.planting()
        self.street()
        self.audit()
        self.write()
        layers.write_layers(self)
        self.checks()
        # (11-11) the same city for a Godot game: CITYMAP_GODOT=<folder> writes SM_<name>.glb and its sidecar there (godot.py)
        gd = os.environ.get("CITYMAP_GODOT")
        if gd:
            import godot
            godot.export(self, gd, os.environ.get("CITYMAP_GODOT_NAME", "Seattle"))
            self.lap("wrote the city for Godot")
        self.say("the cook took %.1f s: %s" % (time.time() - T, ", ".join("%s %.1f" % (w, t) for w, t in self.times)))
        prefix = self.cfg.get("prefix", "city")
        with open(os.path.join(self.out, prefix + "_report.txt"), "w", newline="\n", encoding="utf-8") as f:
            f.write("\n".join(self.report) + "\n")
