"""layers.py -- the map plan's step 6, the game layers (design/bike_lane_bash.md, "THE MAP: HOW SEATTLE GETS BUILT"): the road graph, the places food
is picked up, and the doors it's taken to, written as one text file a game reads, <prefix>_layers.txt.

    units U                                  club units a metre
    node K x y z                             a point of the road graph, in the engine's coordinates (the deck's height on a bridge)
    edge A B metres grade class oneway bridge bike | street name
                                             grade: percent, rising from A to B. oneway: 1 only A to B, -1 only B to A, 0 both, for a bicycle.
                                             bike: none, lane (painted), track (protected), path (its own way)
    pickup K x y z node food | name          a place to pick food up: where its door is, the graph node nearest it, the food, and a MADE-UP name
    door K x y z node kind | address         a door to take it to: where, the nearest node, what OpenStreetMap says the building at it is (house, apartments, other,
                                             or unknown where it says nothing or there's no building within 3 m), and its street address
    spot K x y z yaw kind group | hot spot   (11-06) a person at one of the config's hot spots: where, the way they face (radians, as the engine turns a body:
                                             atan2 of x and z), what they're doing (talk, stand, fold, crouch, kneel, goods, sit, lie, cart, tent: goods is the
                                             blanket a kneeler's things are laid out on, tent a tent and nobody), and the group they're with

⛔ Every pickup's name is made up here, from word lists, seeded by the map's own id so it's the same each cook, and checked against every name, brand and
operator in the whole downloaded file: no real business name, brand or chain reaches the file. Street names and house numbers are the real ones (real
streets, fictional storefronts, the design's rule). Nothing of a shop's or a restaurant's own tags but its cuisine (a word like "vietnamese") is read."""
import math
import os
import random

import numpy as np

import shapes as S

FOOD_OF_CUISINE = [
    ("vietnamese", "pho"), ("pho", "pho"), ("chinese", "dumplings"), ("dim_sum", "dumplings"), ("dumpling", "dumplings"), ("cantonese", "dumplings"),
    ("japanese", "sushi"), ("sushi", "sushi"), ("ramen", "noodles"), ("noodle", "noodles"), ("pizza", "pizza"), ("italian", "pizza"), ("burger", "burgers"),
    ("american", "burgers"), ("mexican", "tacos"), ("taco", "tacos"), ("thai", "curry"), ("indian", "curry"), ("korean", "bbq"), ("barbecue", "bbq"),
    ("bubble_tea", "bubble tea"), ("tea", "bubble tea"), ("sandwich", "sandwiches"), ("deli", "sandwiches"), ("chicken", "chicken"), ("teriyaki", "teriyaki"),
    ("bakery", "bakery"), ("donut", "bakery"), ("coffee", "coffee"), ("filipino", "rice"), ("hawaiian", "rice"), ("ethiopian", "rice"),
]
HOUSE = {"pho": "Pho House", "dumplings": "Dumpling Bar", "sushi": "Sushi Counter", "noodles": "Noodle Shop", "pizza": "Pizza", "burgers": "Burgers",
         "tacos": "Taqueria", "curry": "Curry Kitchen", "bbq": "BBQ", "bubble tea": "Tea Bar", "sandwiches": "Deli", "chicken": "Fried Chicken",
         "teriyaki": "Teriyaki", "bakery": "Bakery", "coffee": "Coffee", "rice": "Rice Kitchen"}
ADJ = ["Lucky", "Happy", "Little", "Golden", "Red", "Jade", "Rainy", "Uphill", "Corner", "Noisy", "Sleepy", "Tiny", "Big", "Late", "Second", "Hungry",
       "Steamy", "Rusty", "Neon", "Soggy", "Crooked", "Midnight", "Wobbly", "Loud", "Lazy", "Brave", "Damp", "Salty", "Humble", "Lonely"]
NOUN = ["Ladle", "Lantern", "Kettle", "Spoon", "Bowl", "Crane", "Tiger", "Wok", "Fox", "Ferry", "Heron", "Crow", "Otter", "Pigeon", "Moth", "Rabbit",
        "Turnip", "Comet", "Walrus", "Raccoon", "Teapot", "Gull", "Badger", "Noodle", "Pickle", "Cloud", "Puddle", "Mitten", "Slug", "Possum"]
EAT = {"restaurant", "fast_food", "cafe"}


def food_of(t, amenity):
    c = (t.get("cuisine") or "").lower()
    for k, f in FOOD_OF_CUISINE:
        if k in c:
            return f
    return "coffee" if amenity == "cafe" else "rice"


def write_layers(ck):
    st, U, floor = ck.st, ck.U, ck.floor
    prefix = ck.cfg.get("prefix", "city")
    lo_e, hi_e, lo_n, hi_n = ck.be0, ck.be1, ck.bn0, ck.bn1

    def inbox(e, n):
        return lo_e <= e <= hi_e and lo_n <= n <= hi_n

    def eng(e, n, h):
        return e * U, (h - floor) * U, -n * U
    # ---------------- the road graph: every road a bicycle may use, and the cycleways
    lift = st["lift"]["road"]
    idx, nodes, edges = {}, [], []
    for l in ck.lines:
        hw = l["hw"]
        if l["kind"] == "road":
            if hw in ("motorway", "motorway_link"):
                continue
        elif not (hw == "cycleway" or (l["kind"] == "path" and l["t"].get("bicycle") in ("yes", "designated"))):
            continue
        t = l["t"]
        ids = l["ids"]
        if l["bridge"]:
            hs = [ck.bh[n] for n in ids]
        else:
            hs = [float(ck.field.sample(ck.ne[n], ck.nn[n])) + lift for n in ids]
        bike = "path" if l["kind"] == "path" else "none"
        if l["kind"] == "road":
            for side in ("left", "right"):
                v = t.get("cycleway:" + side) or t.get("cycleway:both") or t.get("cycleway")
                if v in ("track", "opposite_track"):
                    bike = "track"
                elif v in ("lane", "buffered_lane", "opposite_lane") and bike == "none":
                    bike = "lane"
        ow = t.get("oneway")
        oneway = 1 if ow in ("yes", "1", "true") else (-1 if ow == "-1" else 0)
        if t.get("oneway:bicycle") == "no" or t.get("cycleway") in ("opposite", "opposite_lane", "opposite_track"):
            oneway = 0
        name = (t.get("name") or "").replace("|", "/").strip()
        for k in range(len(ids) - 1):
            a, b = ids[k], ids[k + 1]
            if a == b or not (inbox(ck.ne[a], ck.nn[a]) and inbox(ck.ne[b], ck.nn[b])):
                continue
            for n, h in ((a, hs[k]), (b, hs[k + 1])):
                key = (n, l["bridge"])   # a bridge's node and the ground's are told apart: a deck isn't the street under it
                if key not in idx:
                    idx[key] = len(nodes)
                    nodes.append((ck.ne[n], ck.nn[n], h))
            L = math.hypot(ck.ne[a] - ck.ne[b], ck.nn[a] - ck.nn[b])
            if L < 0.05:
                continue
            g = 100.0 * (hs[k + 1] - hs[k]) / L
            edges.append((idx[(a, l["bridge"])], idx[(b, l["bridge"])], L, g, hw, oneway, int(l["bridge"]), bike, name))
    # a bridge's end node is also the street's: join them where they're the same OSM node at the same height
    for (n, br), k in list(idx.items()):
        if br and (n, False) in idx:
            k2 = idx[(n, False)]
            if abs(nodes[k][2] - nodes[k2][2]) < 0.5:
                edges.append((k, k2, 0.05, 0.0, "join", 0, 0, "none", ""))
    NP = np.array([[e, n, h] for e, n, h in nodes]) if nodes else np.zeros((0, 3))

    def nearest_node(e, n, h):
        if not len(NP):
            return -1
        d = np.hypot(NP[:, 0] - e, NP[:, 1] - n) + 4.0 * np.maximum(np.abs(NP[:, 2] - h) - 2.0, 0.0)
        return int(np.argmin(d))
    # ---------------- buildings, for a door on the outside of a wall
    feet = [(P, P.min(axis=0), P.max(axis=0)) for _bid, _t, P in ck.buildings + ck.parts if len(P) >= 3]

    def to_door(e, n):
        """a point inside a building goes to the nearest place on its wall, and a step out from it"""
        for P, lo, hi in feet:
            if not (lo[0] <= e <= hi[0] and lo[1] <= n <= hi[1]):
                continue
            if not S.inside(np.array([e]), np.array([n]), P)[0]:
                continue
            A, B = P, np.roll(P, -1, axis=0)
            dist, tt = S.seg_dist(np.array([e]), np.array([n]), A[:, 0], A[:, 1], B[:, 0], B[:, 1])
            k = int(np.argmin(dist[0]))
            qe = A[k, 0] + (B[k, 0] - A[k, 0]) * tt[0, k]
            qn = A[k, 1] + (B[k, 1] - A[k, 1]) * tt[0, k]
            de, dn = qe - e, qn - n
            L = math.hypot(de, dn)
            if L < 1e-6:   # on the wall already: out is across the edge
                de, dn = (B[k, 1] - A[k, 1]), -(B[k, 0] - A[k, 0])
                L = math.hypot(de, dn) or 1.0
            return qe + 0.7 * de / L, qn + 0.7 * dn / L
        return e, n
    # ---------------- what the building at a door is, by its building tag: a house, apartments, something else, or unknown (building=yes, or no building at it)
    HOUSE_T = {"house", "detached", "semidetached_house", "bungalow", "terrace", "cabin", "farm"}
    FLATS_T = {"apartments", "residential", "dormitory"}
    bfeet = [(P, P.min(axis=0), P.max(axis=0), (t or {}).get("building", "yes")) for _bid, t, P in ck.buildings if len(P) >= 3]

    def kind_at(e, n):
        best, bd = "unknown", 3.0
        for P, lo, hi, b in bfeet:
            if not (lo[0] - 3 <= e <= hi[0] + 3 and lo[1] - 3 <= n <= hi[1] + 3):
                continue
            A, B = P, np.roll(P, -1, axis=0)
            dist, _tt = S.seg_dist(np.array([e]), np.array([n]), A[:, 0], A[:, 1], B[:, 0], B[:, 1])
            d = 0.0 if S.inside(np.array([e]), np.array([n]), P)[0] else float(dist[0].min())
            if d < bd:
                bd, best = d, ("house" if b in HOUSE_T else "apartments" if b in FLATS_T else "unknown" if b == "yes" else "other")
        return best
    # ---------------- every real name in the whole file, so a made-up one is never one of them
    real = set()
    for w in list(ck.ways.values()) + ck.rels:
        for k in ("name", "brand", "operator", "old_name", "alt_name", "official_name", "short_name"):
            v = (w.get("tags") or {}).get(k)
            if v:
                real.add(v.strip().lower())
    for t in ck.ntags.values():
        for k in ("name", "brand", "operator", "old_name", "alt_name", "official_name", "short_name"):
            v = t.get(k)
            if v:
                real.add(v.strip().lower())
    taken = set()
    refused = 0

    def made_up(seed, food):
        nonlocal refused
        rng = random.Random(seed)
        for _ in range(200):
            a, b = rng.choice(ADJ), rng.choice(NOUN)
            nm = "%s %s %s" % (a, b, HOUSE[food])
            if nm.lower() in real or ("%s %s" % (a, b)).lower() in real or nm in taken:
                refused += 1
                continue
            taken.add(nm)
            return nm
        raise RuntimeError("no made-up name left for %s" % food)
    # ---------------- the pickups: what OSM calls a restaurant, a fast food place or a cafe, in the box
    eats = []
    for nid in sorted(ck.ntags):
        t = ck.ntags[nid]
        if t.get("amenity") in EAT and nid in ck.ne and inbox(ck.ne[nid], ck.nn[nid]):
            eats.append((nid, ck.ne[nid], ck.nn[nid], t))
    for wid in sorted(ck.ways):
        w = ck.ways[wid]
        t = w.get("tags", {})
        if t.get("amenity") in EAT:
            ids, P = ck.pts(w)
            if len(P):
                m = P.mean(axis=0)
                if inbox(m[0], m[1]):
                    eats.append((-wid, m[0], m[1], t))
    out = ["# %s's game layers: cooked by skate_src/citymap/layers.py from OpenStreetMap (c) OpenStreetMap contributors, ODbL. Generated: don't edit, cook again" % prefix,
           "# every pickup's name is MADE UP; streets and house numbers are the map's", "units %g" % U]
    for k, (e, n, h) in enumerate(nodes):
        out.append("node %d %.3f %.3f %.3f" % ((k,) + eng(e, n, h)))
    for a, b, L, g, hw, ow, br, bike, name in edges:
        out.append("edge %d %d %.2f %.2f %s %d %d %s | %s" % (a, b, L, g, hw, ow, br, bike, name))
    foods = {}
    for k, (sid, e, n, t) in enumerate(eats):
        food = food_of(t, t.get("amenity"))
        foods[food] = foods.get(food, 0) + 1
        de, dn = to_door(e, n)
        h = float(ck.field.sample(de, dn))
        nm = made_up(abs(sid) * 7919 + 17, food)
        out.append("pickup %d %.3f %.3f %.3f %d %s | %s" % ((k,) + eng(de, dn, h) + (nearest_node(de, dn, h), food.replace(" ", "_"), nm)))
    # ---------------- the doors: entrances, address points, and buildings that carry their own address
    doors = []
    seen = []

    def addr_of(t):
        hn, stn = t.get("addr:housenumber"), t.get("addr:street")
        if not hn or not stn:
            return None
        s = "%s %s" % (hn, stn)
        if t.get("addr:unit"):
            s += " #%s" % t["addr:unit"]
        return s.replace("|", "/")
    for nid in sorted(ck.ntags):
        t = ck.ntags[nid]
        if nid not in ck.ne or not inbox(ck.ne[nid], ck.nn[nid]):
            continue
        a = addr_of(t)
        if a is None and "entrance" not in t:
            continue
        doors.append((ck.ne[nid], ck.nn[nid], a, "entrance" in t))
    for _bid, t, P in ck.buildings:
        a = addr_of(t)
        if a and len(P):
            m = P.mean(axis=0)
            if inbox(m[0], m[1]):
                doors.append((m[0], m[1], a, False))
    addr_pts = np.array([[e, n] for e, n, a, _ in doors if a]) if any(d[2] for d in doors) else np.zeros((0, 2))
    addr_txt = [a for _e, _n, a, _ in doors if a]
    nd = 0
    grid = {}
    for e, n, a, ent in doors:
        if a is None:   # an entrance with no address of its own: the nearest address point's, if one's close
            if not len(addr_pts):
                continue
            d = np.hypot(addr_pts[:, 0] - e, addr_pts[:, 1] - n)
            k = int(np.argmin(d))
            if d[k] > 40.0:
                continue
            a = addr_txt[k]
        de, dn = (e, n) if ent else to_door(e, n)
        key = (int(de // 2), int(dn // 2))
        if key in grid:
            continue
        grid[key] = 1
        seen.append(a)
        h = float(ck.field.sample(de, dn))
        out.append("door %d %.3f %.3f %.3f %d %s | %s" % ((nd,) + eng(de, dn, h) + (nearest_node(de, dn, h), kind_at(de, dn), a)))
        nd += 1
    # ---------------- (11-06) the people at a hot spot (the config's "hotspots", 11-06: a corner where people stand all day, 12th and Jackson for one). Laid
    # out by rule on the sidewalks within its radius, the same every cook: more by a bus stop, along a fence and near a shop (a corner store most), none
    # on a crossing or a kerb's ramp, fewer further out. A group stands together; someone sitting, kneeling or lying is at the back of the sidewalk,
    # away from the road
    nspot = 0
    roads = [l for l in ck.lines if l["kind"] == "road" and not l["bridge"] and len(l["C"]) >= 2]
    ra = np.concatenate([l["C"][:-1] for l in roads]) if roads else np.zeros((0, 2))
    rb = np.concatenate([l["C"][1:] for l in roads]) if roads else np.zeros((0, 2))
    # (11-06) the streets alone, not a service road: which side of a fence the street is on (a lot's parking aisle can be nearer than its street)
    streets = [l for l in roads if l["hw"] not in ("service", "track")]
    sa_ = np.concatenate([l["C"][:-1] for l in streets]) if streets else np.zeros((0, 2))
    sb_ = np.concatenate([l["C"][1:] for l in streets]) if streets else np.zeros((0, 2))
    for hs in ck.cfg.get("hotspots", []):
        hn = hs.get("node")
        if hn not in ck.ne:
            ck.say("hot spot %s: no node %s in the map" % (hs.get("name"), hn))
            continue
        he, hnn = ck.ne[hn], ck.nn[hn]
        R = float(hs.get("radius_m", 70.0))
        want = int(hs.get("people", 30))
        rnd = random.Random(int(hn) % 2147483647)

        def pts_near(pred, d):
            return np.array([[ck.ne[n], ck.nn[n]] for n, t in ck.ntags.items() if n in ck.ne and pred(t) and math.hypot(ck.ne[n] - he, ck.nn[n] - hnn) < R + d]) \
                if any(pred(t) for t in ck.ntags.values()) else np.zeros((0, 2))
        stops = pts_near(lambda t: t.get("highway") == "bus_stop", 20)
        sheltered = pts_near(lambda t: t.get("highway") == "bus_stop" and t.get("shelter") == "yes", 20)   # the cook stands a shelter there: nobody inside it
        corner_shops = pts_near(lambda t: t.get("shop") in ("convenience", "alcohol", "tobacco", "mobile_phone"), 20)
        shops = pts_near(lambda t: "shop" in t or t.get("amenity") in ("restaurant", "fast_food", "cafe", "bar", "pharmacy"), 20)
        ramps = pts_near(lambda t: t.get("barrier") == "kerb", 10)
        cross = [l["C"] for l in ck.lines if l["hw"] == "footway" and l["t"].get("footway") == "crossing"]
        cross = np.concatenate(cross) if cross else np.zeros((0, 2))
        fences = []
        for wid in sorted(ck.ways):
            t = ck.ways[wid].get("tags", {})
            if t.get("barrier") in ("fence", "wall"):
                _ids, Pf = ck.pts(ck.ways[wid])
                if len(Pf) >= 2 and np.hypot(Pf[:, 0] - he, Pf[:, 1] - hnn).min() < R + 30:
                    fences.append(Pf)
        for fz in ck.cfg.get("features", []):   # (11-06) a source's fences too: the lot's at 12th and Jackson, the bin pen's sides
            if fz.get("kind") == "fence":
                Pf = np.array([ck.place(dict(q, on="ground"))[:2] for q in fz.get("points", [])])
                if len(Pf) >= 2:
                    fences.append(Pf)
            elif fz.get("kind") == "bin_pen":
                e0, n0, _h0 = ck.place(dict(fz["at"], on="ground"))
                w_, d_ = fz.get("size_m", [3.4, 2.6])
                yw = math.radians(float(fz.get("yaw_deg", 0.0)))
                ae, an, be, bn = math.cos(yw), -math.sin(yw), math.sin(yw), math.cos(yw)
                fences.append(np.array([(e0 + ae * sa * w_ / 2 + be * sb * d_ / 2, n0 + an * sa * w_ / 2 + bn * sb * d_ / 2)
                                        for sa, sb in ((-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1))]))
        sws = [l for l in ck.lines if l["kind"] == "path" and l["t"].get("footway") == "sidewalk" and not l["bridge"] and len(l["C"]) >= 2
               and np.hypot(l["C"][:, 0] - he, l["C"][:, 1] - hnn).min() < R + 30]
        swa = np.concatenate([l["C"][:-1] for l in sws]) if sws else np.zeros((0, 2))
        swb = np.concatenate([l["C"][1:] for l in sws]) if sws else np.zeros((0, 2))
        sww = np.concatenate([np.full(len(l["C"]) - 1, float(l.get("w", 2.0)) / 2) for l in sws]) if sws else np.zeros(0)

        def on_sidewalk(e, n, behind=0.25):
            """on a mapped sidewalk, or (behind) on the paved strip between it and what's at its back: the map draws a sidewalk 2 m wide down its
            middle, and at 12th and Jackson the lot's fence stands 2 m behind that line with the sidewalk running right up to it"""
            if not len(swa):
                return False
            dist, _t = S.seg_dist(np.array([e]), np.array([n]), swa[:, 0], swa[:, 1], swb[:, 0], swb[:, 1])
            return bool((dist[0] <= sww + behind).any())
        fa = np.concatenate([f[:-1] for f in fences]) if fences else np.zeros((0, 2))
        fb = np.concatenate([f[1:] for f in fences]) if fences else np.zeros((0, 2))

        def near(xy, e, n, d):
            return len(xy) > 0 and bool((np.hypot(xy[:, 0] - e, xy[:, 1] - n) < d).any())
        cand = []
        for l in ck.lines:
            if l["kind"] != "path" or l["t"].get("footway") != "sidewalk" or l["bridge"] or len(l["C"]) < 2:
                continue
            C = l["C"]
            if np.hypot(C[:, 0] - he, C[:, 1] - hnn).min() > R:
                continue
            sarc = S.arclen(C)
            Rt = S.rights(C)
            for a in np.arange(0.75, sarc[-1], 1.5):
                e, n = float(np.interp(a, sarc, C[:, 0])), float(np.interp(a, sarc, C[:, 1]))
                d = math.hypot(e - he, n - hnn)
                if d > R or near(ramps, e, n, 2.2) or near(cross, e, n, 2.5) or near(sheltered, e, n, 4.0):
                    continue
                re_, rn_ = float(np.interp(a, sarc, Rt[:, 0])), float(np.interp(a, sarc, Rt[:, 1]))
                rl = math.hypot(re_, rn_) or 1.0
                re_, rn_ = re_ / rl, rn_ / rl
                # the back of the sidewalk: the side away from the nearest road
                back = (re_, rn_)
                if len(ra):
                    dist, tt = S.seg_dist(np.array([e]), np.array([n]), ra[:, 0], ra[:, 1], rb[:, 0], rb[:, 1])
                    q = int(np.argmin(dist[0]))
                    px, py = ra[q] + (rb[q] - ra[q]) * tt[0, q]
                    if (e - px) * re_ + (n - py) * rn_ < 0:
                        back = (-re_, -rn_)
                sc = 1.0 + (3.0 if near(stops, e, n, 14.0) else 0.0) + (2.5 if near(corner_shops, e, n, 12.0) else 0.0) + (0.7 if near(shops, e, n, 10.0) else 0.0)
                if len(fa):
                    fd, _t = S.seg_dist(np.array([e]), np.array([n]), fa[:, 0], fa[:, 1], fb[:, 0], fb[:, 1])
                    if fd.min() < 3.5:
                        sc += 2.0
                sc *= max(0.0, 1.0 - d / R) ** 0.7
                cand.append((e, n, back[0], back[1], -back[1], back[0], float(l.get("w", 2.0)), sc))
        if not cand:
            ck.say("hot spot %s: no sidewalk within %.0f m" % (hs.get("name"), R))
            continue
        w = np.array([c[7] for c in cand])
        MIX = [("deal", 0.24), ("hunch", 0.16), ("lookout", 0.08), ("fold", 0.12), ("crouch", 0.08), ("goods", 0.08), ("sit", 0.06), ("sitground", 0.05),
               ("wheel", 0.03), ("lie", 0.04), ("cart", 0.06)]
        centres, people, group, tents, kinds = [], 0, 0, 0, {}
        out_hs = []

        def put(e, n, fe, fn, kind):
            h = float(ck.field.sample(np.array([e]), np.array([n]))[0])
            x, y, z = eng(e, n, h)
            out_hs.append((x, y, z, math.atan2(fe, -fn), kind, group))
            kinds[kind] = kinds.get(kind, 0) + 1
        # (11-06, from photos of the corner: what each person is doing, and how they stand. It is a bus stop, and few of them
        # are waiting for a bus) THE BAND: along the fences on the sidewalk side, a person every step_m, nearest the corner first, in the photos' mix:
        # against the fence most sit on the ground, then hunched over their hands, standing, crouched, folded, a wheelchair, someone's goods, someone
        # lying; a second row in places, hunched, standing, talking. Neighbours are a group and turn to each other; the groups elsewhere keep off it
        band = hs.get("band") or {}
        want_band, n_band = int(band.get("people", 0)), 0
        if want_band and fences:
            step = float(band.get("step_m", 1.1))
            slots, why = [], {}
            for fi, Pf in enumerate(fences):
                for k in range(len(Pf) - 1):
                    ax, ay = float(Pf[k][0]), float(Pf[k][1])
                    bx, by = float(Pf[k + 1][0]), float(Pf[k + 1][1])
                    L = math.hypot(bx - ax, by - ay)
                    if L < 0.5:
                        continue
                    de, dn = (bx - ax) / L, (by - ay) / L
                    oe_, on_ = dn, -de
                    mx, my = (ax + bx) / 2, (ay + by) / 2
                    if len(sa_):
                        dist, tt = S.seg_dist(np.array([mx]), np.array([my]), sa_[:, 0], sa_[:, 1], sb_[:, 0], sb_[:, 1])
                        q = int(np.argmin(dist[0]))
                        px, py = sa_[q] + (sb_[q] - sa_[q]) * tt[0, q]
                        if (px - mx) * oe_ + (py - my) * on_ < 0:
                            oe_, on_ = -oe_, -on_      # out from the fence toward the street
                    for a in np.arange(step / 2, L, step):
                        e, n = ax + de * a, ay + dn * a
                        e1, n1 = e + oe_ * 0.55, n + on_ * 0.55
                        d = math.hypot(e1 - he, n1 - hnn)
                        no = ("far" if d > R else "off a sidewalk" if not on_sidewalk(e1, n1, 1.6) else "a kerb's ramp" if near(ramps, e1, n1, 2.2)
                              else "a crossing" if near(cross, e1, n1, 2.5) else "a shelter" if near(sheltered, e1, n1, 4.0)
                              else "a stop's pole" if near(stops, e1, n1, 1.3) else None)
                        if no:
                            why[no] = why.get(no, 0) + 1
                            continue
                        slots.append((d, fi, k, float(a), e, n, oe_, on_, de, dn))
            slots.sort()
            ck.say("hot spot %s: the band has %d places along %d fences (%s)" % (hs.get("name"), len(slots), len(fences),
                                                                              ", ".join("%d by %s" % (v, k) for k, v in sorted(why.items())) or "none turned down"))
            ROW1 = [("sitground", 0.30), ("hunch", 0.18), ("stand", 0.14), ("crouch", 0.12), ("fold", 0.10), ("wheel", 0.05), ("goods", 0.05), ("lie", 0.06)]
            ROW2 = [("hunch", 0.34), ("stand", 0.28), ("talk", 0.16), ("fold", 0.12), ("crouch", 0.10)]

            def pick(mix):
                u = rnd.random()
                for nm, p_ in mix:
                    if u < p_:
                        return nm
                    u -= p_
                return mix[-1][0]
            left, gc, skip = 0, (0.0, 0.0), 0
            for (_d, _fi, _k, _a, e, n, oe_, on_, de, dn) in sorted(slots[:want_band], key=lambda s_: (s_[1], s_[2], s_[3])):
                if n_band >= want_band:
                    break
                if skip:
                    skip -= 1
                    continue
                if left <= 0:
                    group += 1
                    left = rnd.choice((1, 2, 2, 3, 3, 4))
                    gc = (e + oe_ * 1.1 + de * step * (left - 1) / 2.0, n + on_ * 1.1 + dn * step * (left - 1) / 2.0)   # the group's middle
                left -= 1
                kind = pick(ROW1)
                e1, n1 = e + oe_ * 0.55, n + on_ * 0.55
                if kind == "sitground":
                    put(e + oe_ * 0.42, n + on_ * 0.42, oe_, on_, "sitground")
                elif kind in ("hunch", "stand"):
                    put(e1, n1, gc[0] - e1 + 1e-3, gc[1] - n1, kind)
                elif kind == "crouch":
                    put(e1, n1, -oe_ + 0.3 * de, -on_ + 0.3 * dn, "crouch")
                elif kind == "fold":
                    a2 = rnd.random() * 2.0 * math.pi
                    put(e1, n1, math.cos(a2), math.sin(a2), "fold")
                elif kind == "wheel":
                    put(e + oe_ * 0.75, n + on_ * 0.75, oe_, on_, "wheel")
                elif kind == "goods":
                    put(e1, n1, oe_, on_, "kneel")
                    put(e1 + oe_ * 0.85, n1 + on_ * 0.85, oe_, on_, "goods")
                    skip = 1
                else:
                    put(e + oe_ * 0.5 + de * 0.9, n + on_ * 0.5 + dn * 0.9, de, dn, "lie")
                    skip = 1
                n_band += 1
                centres.append((e1, n1))
                if kind not in ("goods", "lie", "wheel") and rnd.random() < float(band.get("second_row", 0.45)):
                    e2, n2 = e + oe_ * 1.55 + de * 0.3, n + on_ * 1.55 + dn * 0.3
                    if on_sidewalk(e2, n2, 1.6):
                        k2 = pick(ROW2)
                        if k2 == "fold":
                            a2 = rnd.random() * 2.0 * math.pi
                            put(e2, n2, math.cos(a2), math.sin(a2), "fold")
                        elif k2 == "crouch":
                            put(e2, n2, -oe_, -on_, "crouch")
                        else:
                            put(e2, n2, gc[0] - e2 + 1e-3, gc[1] - n2, k2)
                        n_band += 1
        # the few waiting for a bus: one or two by each stop's pole, facing the street
        for se, sn in (stops if len(stops) else []):
            if math.hypot(se - he, sn - hnn) > R or not len(sa_):
                continue
            dist, tt = S.seg_dist(np.array([se]), np.array([sn]), sa_[:, 0], sa_[:, 1], sb_[:, 0], sb_[:, 1])
            q = int(np.argmin(dist[0]))
            px, py = sa_[q] + (sb_[q] - sa_[q]) * tt[0, q]
            fl = math.hypot(px - se, py - sn) or 1.0
            fe_, fn_ = (px - se) / fl, (py - sn) / fl
            group += 1
            for j in range(1 if rnd.random() < 0.5 else 2):
                pe, pn = se + fn_ * (0.7 + 0.8 * j) - fe_ * 0.3, sn - fe_ * (0.7 + 0.8 * j) - fn_ * 0.3
                put(pe, pn, fe_, fn_, "stand")
                n_band += 1
        # a canopy someone sells under, by a stop the config names (12th and Jackson's on Jackson, from a photo): two by it, one behind the table
        for nid, tg in ck.ntags.items():
            if tg.get("highway") != "bus_stop" or str(tg.get("ref")) not in [str(x) for x in hs.get("canopy_stops", [])] or nid not in ck.ne:
                continue
            se, sn = ck.ne[nid], ck.nn[nid]
            if not len(sa_):
                continue
            dist, tt = S.seg_dist(np.array([se]), np.array([sn]), sa_[:, 0], sa_[:, 1], sb_[:, 0], sb_[:, 1])
            q = int(np.argmin(dist[0]))
            px, py = sa_[q] + (sb_[q] - sa_[q]) * tt[0, q]
            fl = math.hypot(px - se, py - sn) or 1.0
            fe_, fn_ = (px - se) / fl, (py - sn) / fl
            ae_, an_ = fn_, -fe_
            if (se + ae_ - he) ** 2 + (sn + an_ - hnn) ** 2 < (se - ae_ - he) ** 2 + (sn - an_ - hnn) ** 2:
                ae_, an_ = -ae_, -an_   # along the sidewalk away from the corner
            ce, cn = se + ae_ * 3.4 - fe_ * 0.6, sn + an_ * 3.4 - fn_ * 0.6
            group += 1
            put(ce, cn, fe_, fn_, "canopy")
            put(ce - fe_ * 0.9 + ae_ * 0.3, cn - fn_ * 0.9 + an_ * 0.3, fe_, fn_, "stand")
            put(ce + fe_ * 1.0, cn + fn_ * 1.0, -fe_, -fn_, "hunch")
            centres.append((ce, cn))
            n_band += 2
        guard = 0
        while people < want and w.sum() > 0 and guard < 4000:
            guard += 1
            k = int(np.searchsorted(np.cumsum(w), rnd.random() * w.sum()))
            k = min(k, len(cand) - 1)
            e, n, bke, bkn, ale, aln, sw, _sc = cand[k]
            w[k] = 0.0
            if any(math.hypot(e - ce, n - cn) < 4.0 for ce, cn in centres):
                continue
            centres.append((e, n))
            u = rnd.random()
            role = MIX[-1][0]
            for nm, p in MIX:
                if u < p:
                    role = nm
                    break
                u -= p
            if tents < int(hs.get("tents", 2)) and rnd.random() < 0.08:
                role = "tent"
            group += 1
            edge = max(0.0, sw / 2 - 0.45)   # the back of the sidewalk, from its middle
            fr = (-bke, -bkn)   # toward the road
            if role == "deal":
                m = rnd.choice((2, 2, 3, 3, 4))
                for j in range(m):
                    ang = 2.0 * math.pi * j / m + rnd.random() * 0.6
                    oe, on = 1.0 * math.cos(ang), 0.8 * math.sin(ang)
                    pe, pn = e + ale * oe + bke * on, n + aln * oe + bkn * on
                    put(pe, pn, e - pe, n - pn, ("talk", "hunch", "talk", "stand")[rnd.randrange(4)] if j else ("stand", "hunch")[rnd.randrange(2)])
                people += m
            elif role == "lookout":
                put(e, n, fr[0] + ale * (rnd.random() - 0.5), fr[1] + aln * (rnd.random() - 0.5), "stand")
                people += 1
            elif role == "fold":
                m = 1 if rnd.random() < 0.7 else 2
                for j in range(m):
                    pe, pn = e + ale * 0.9 * j + bke * edge * 0.5, n + aln * 0.9 * j + bkn * edge * 0.5
                    a2 = rnd.random() * 2.0 * math.pi
                    put(pe, pn, math.cos(a2), math.sin(a2), "fold")
                people += m
            elif role == "crouch":
                put(e + bke * edge, n + bkn * edge, fr[0], fr[1], "crouch")
                people += 1
            elif role == "goods":
                ke, kn = e + bke * edge, n + bkn * edge
                put(ke, kn, fr[0], fr[1], "kneel")
                put(ke - bke * 0.85, kn - bkn * 0.85, fr[0], fr[1], "goods")
                people += 1
                if rnd.random() < 0.5:
                    put(ke - bke * 1.9 + ale * 0.3, kn - bkn * 1.9 + aln * 0.3, bke, bkn, "stand")
                    people += 1
            elif role == "sit":
                m = 1 if rnd.random() < 0.6 else 2
                for j in range(m):
                    put(e + bke * edge + ale * 0.8 * j, n + bkn * edge + aln * 0.8 * j, fr[0], fr[1], "sit")
                people += m
            elif role == "lie":
                put(e + bke * edge, n + bkn * edge, ale, aln, "lie")
                people += 1
            elif role == "cart":
                put(e, n, ale, aln, "cart")
                people += 1
            elif role == "hunch":
                m = rnd.choice((1, 2, 2, 3))
                for j in range(m):
                    ang = 2.0 * math.pi * j / m + rnd.random() * 0.8
                    oe, on = 0.55 * math.cos(ang), 0.45 * math.sin(ang)
                    pe, pn = e + ale * oe + bke * on, n + aln * oe + bkn * on
                    put(pe, pn, e - pe + 1e-3, n - pn, "hunch")
                people += m
            elif role == "wheel":
                put(e + bke * edge * 0.6, n + bkn * edge * 0.6, fr[0], fr[1], "wheel")
                people += 1
            elif role == "sitground":
                put(e + bke * (edge + 0.1), n + bkn * (edge + 0.1), fr[0], fr[1], "sitground")
                people += 1
            elif role == "tent":
                put(e + bke * max(0.0, sw / 2 - 1.1), n + bkn * max(0.0, sw / 2 - 1.1), fr[0], fr[1], "tent")
                tents += 1
        for x, y, z, yaw, kind, grp in out_hs:
            out.append("spot %d %.3f %.3f %.3f %.4f %s %d | %s" % (nspot, x, y, z, yaw, kind, grp, (hs.get("name") or "").replace("|", "/")))
            nspot += 1
        ck.say("hot spot %s: %d people in %d groups (%d of them along the fences, at the stops and the canopy), %s (from %d places on the sidewalks within %.0f m)" % (
            hs.get("name"), people + n_band, group, n_band, ", ".join("%s %d" % kv for kv in sorted(kinds.items())), len(cand), R))
    path = os.path.join(ck.out, prefix + "_layers.txt")
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    bikes = {}
    for e in edges:
        bikes[e[7]] = bikes.get(e[7], 0) + e[2]
    ck.say("game layers: %s_layers.txt: the road graph %d nodes, %d edges, %.1f km (bike: %s); %d pickups, every name made up (%d draws refused for matching a real name), foods %s; %d doors" % (
        prefix, len(nodes), len(edges), sum(e[2] for e in edges) / 1000.0, ", ".join("%s %.1f km" % (k, v / 1000.0) for k, v in sorted(bikes.items())),
        len(eats), refused, ", ".join("%s %d" % kv for kv in sorted(foods.items())), nd))
    ck.lap("wrote the game layers")
