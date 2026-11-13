#!/usr/bin/env python3
"""godot2set.py -- a Godot scene, as skate_src/godot/engine_export.gd wrote it, made into a set and a pack for the engine (11-12, the Godot proof
of concept).

usage: godot2set.py <export folder> <name> <game folder> [--prefix P]

Reads <export folder>/<name>.glb and <name>.json and writes, in the game's folder (none of it in git: the game's .gitignore says so):
  props/<P>_park.rpack   every mesh of the scene, KEPT APART BY MATERIAL (glb2rpack.py merges a file into one mesh and one texture, which a
                         park with many materials can't be): an untextured material is a mesh named by its colour (`ride#9ea3a0`), which is
                         where the engine reads an untextured mesh's colour; a textured one carries its texture; BLEND stays blended. The
                         first word of each name says what it is to a skater: ride (ridden on), wall (in the way), rail (ground on)
  props/<P>_shaders/     (11-12, step 12) each Godot shader material the park uses, translated and compiled (godot_shader.py): the surfaces it's
                         on keep their uv and are named <kind>#<colour>#<its key>, and the set draws them with it (`shader` lines). One that
                         can't be carried is said, and its surfaces are drawn with their plain material
  sets/<P>.set           `climb` and `triangles` (its real triangles kept for rays and a moving ball), the surfaces' kinds, the prop,
                         the sun, the sky, fog and exposure as Godot's environment has them, the spawn and every Marker3D as marks,
                         and the lights
  <P>_rails.txt          each grind rail as a line (the meshes named Park_Rail, by their long axis) and the park file's own rail polylines
  <P>_gaps.txt           each gap area: its name, its points, its box
  <P>_import.txt         what came over, what didn't and why: the proof of concept's holes, in the scene's own terms
Units: Godot's metres become the engine's 1.75 units a metre; the axes are the same (glTF's). The whole scene is lifted so its lowest point
is y 0, because the engine's walked world always has a floor at y 0 and a bowl dug below it would be filled."""
import argparse
import json
import math
import os
import shutil
import site
import struct
import sys

site.addsitedir(site.getusersitepackages())   # python -I (an export is read like a download) leaves the user's numpy out
import numpy as np   # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from rpackwrite import rigid_skin, write_pack   # noqa: E402
sys.path.insert(0, HERE)
import godot_shader   # noqa: E402

U = 1.75
I16 = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def read_glb(path):
    d = open(path, "rb").read()
    if d[:4] != b"glTF":
        raise SystemExit("%s isn't a .glb" % path)
    n = struct.unpack("<I", d[12:16])[0]
    js = json.loads(d[20:20 + n])
    rest = d[20 + n:]
    bin_ = rest[8:8 + struct.unpack("<I", rest[0:4])[0]] if len(rest) >= 8 else b""
    return js, bin_


def accessor(js, bin_, i):
    a = js["accessors"][i]
    v = js["bufferViews"][a["bufferView"]]
    dt = np.dtype(CT[a["componentType"]])
    nc = NC[a["type"]]
    off = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = v.get("byteStride", 0) or dt.itemsize * nc
    raw = np.frombuffer(bin_, dtype=np.uint8, count=stride * (a["count"] - 1) + dt.itemsize * nc, offset=off)
    out = np.lib.stride_tricks.as_strided(raw, shape=(a["count"], dt.itemsize * nc), strides=(stride, 1)).copy().view(dt).reshape(a["count"], nc)
    if a.get("normalized") and dt != np.float32:
        out = out.astype(np.float64) / float(np.iinfo(dt).max)
    return out


def node_matrix(nd):
    if "matrix" in nd:
        return np.array(nd["matrix"], dtype=np.float64).reshape(4, 4).T
    t = nd.get("translation", [0, 0, 0])
    x, y, z, w = nd.get("rotation", [0, 0, 0, 1])
    s = nd.get("scale", [1, 1, 1])
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4)
    M[:3, :3] = R * np.array(s)[None, :]
    M[:3, 3] = t
    return M


def srgb(c):
    """glTF's baseColorFactor is linear; the engine's #rrggbb names are sRGB"""
    c = max(0.0, min(1.0, float(c)))
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def ambient_of(env):
    """(step 12) the scene's ambient light as a set's sky colour (the engine's ambient, which it reads as a colour as it looks and makes linear): Godot's
    ambient colour (sRGB) times its energy, when its source is a colour. None for any other source"""
    if env.get("ambient_source") != 2 or not env.get("ambient_color"):
        return None
    e = float(env.get("ambient_energy", 1.0))
    return [((godot_shader.srgb_to_linear(c) * e) ** (1.0 / 2.2)) for c in env["ambient_color"][:3]]


def fallback_hex(rec, export):
    """a shader's surface drawn plainly: the average of its first picture (the first layer of an array), as the name's colour"""
    from PIL import Image
    for v in rec.get("params", {}).values():
        f = v.get("tex") or (v.get("layers") or [None])[0]
        if f:
            try:
                c = Image.open(os.path.join(export, "shaders", f)).convert("RGB").resize((1, 1), Image.BOX).getpixel((0, 0))
                return "%02x%02x%02x" % c
            except OSError:
                pass
    return "ffffff"


def uniform_said(rec):
    p = rec.get("params", {})
    nums = ["%s %s" % (k, v["num"]) for k, v in sorted(p.items()) if "num" in v]
    return ", ".join(nums) if nums else "%d uniforms set" % len(p)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("export")
    ap.add_argument("name")
    ap.add_argument("game")
    ap.add_argument("--prefix")
    a = ap.parse_args()
    P = a.prefix or a.name
    man = json.load(open(os.path.join(a.export, a.name + ".json"), encoding="utf-8"))
    js, bin_ = read_glb(os.path.join(a.export, a.name + ".glb"))
    holes, came = [], []
    ride_of = {m["name"].replace("/", "_"): m["ride"] for m in man["meshes"]}

    # ---- every primitive, in the world, grouped by what it is and its material
    groups = {}   # (kind, material) -> [pos list, nrm list, uv list, idx list, count]
    tris_in = 0

    def walk(ni, parent):
        nonlocal tris_in
        nd = js["nodes"][ni]
        W = parent @ node_matrix(nd)
        if "mesh" in nd:
            nm = nd.get("name", "")
            kind = "rail" if "Park_Rail" in nm else ("ride" if ride_of.get(nm, False) else "wall")
            N3 = np.linalg.inv(W[:3, :3]).T
            for pr in js["meshes"][nd["mesh"]]["primitives"]:
                if pr.get("mode", 4) != 4:
                    holes.append("a primitive of %s isn't triangles (mode %d): left out" % (nm, pr.get("mode")))
                    continue
                at = pr["attributes"]
                p = accessor(js, bin_, at["POSITION"]).astype(np.float64)
                p = (W[:3, :3] @ p.T).T + W[:3, 3]
                n = accessor(js, bin_, at["NORMAL"]).astype(np.float64) if "NORMAL" in at else np.tile([0.0, 1.0, 0.0], (len(p), 1))
                n = (N3 @ n.T).T
                n /= np.maximum(np.linalg.norm(n, axis=1), 1e-12)[:, None]
                uv = accessor(js, bin_, at["TEXCOORD_0"]).astype(np.float64) if "TEXCOORD_0" in at else None
                idx = accessor(js, bin_, pr["indices"]).reshape(-1).astype(np.int64) if "indices" in pr else np.arange(len(p))
                if np.linalg.det(W[:3, :3]) < 0:
                    idx = idx.reshape(-1, 3)[:, [0, 2, 1]].reshape(-1)
                tris_in += len(idx) // 3
                G = groups.setdefault((kind, pr.get("material", -1)), [[], [], [], [], 0])
                G[0].append(p)
                G[1].append(n)
                G[2].append(uv if uv is not None else np.zeros((len(p), 2)))
                G[3].append(idx + G[4])
                G[4] += len(p)
        for c in nd.get("children", []):
            walk(c, W)
    for root_i in js["scenes"][js.get("scene", 0)]["nodes"]:
        walk(root_i, np.eye(4))
    if not groups:
        raise SystemExit("no meshes in the export")

    # ---- the static shapes Godot draws nothing for but stands things on (a ground box): drawn here, so the floor is there to ride
    extra = []
    for s in man.get("statics", []):
        leaf = s["name"].split("/")[-1]
        if not any(w in leaf.lower() for w in ("ground", "floor")):
            # (the skate game's SafetyNet under a map and SidecarWalls round it): solid and never drawn in Godot. Drawing them would wall the park
            # in or lay a floor over its low places, and the engine has no prop that's solid and not drawn: left out, and said
            holes.append("static %s: solid in Godot and never drawn (%d box%s); the engine has no solid prop that isn't drawn, so it's left out" % (
                s["name"], len(s["shapes"]), "" if len(s["shapes"]) == 1 else "es"))
            continue
        for sh in s["shapes"]:
            if sh["type"] != "BoxShape3D":
                holes.append("static %s: a %s collision shape has no mesh here (only boxes are made into one)" % (s["name"], sh["type"]))
                continue
            c = np.array(sh["xf"]["pos"])
            hx, hy, hz = (0.5 * v for v in sh["size"])
            q = sh["xf"]["quat"]
            if abs(q[0]) + abs(q[1]) + abs(q[2]) > 1e-4:
                holes.append("static %s: a turned box, made as if square to the axes" % s["name"])
            extra.append((s["name"], c, (hx, hy, hz)))
            came.append("static %s: a %.0f x %.0f x %.0f m box with nothing drawn in Godot, drawn here as ground" % (s["name"], *sh["size"]))

    # ---- lifted so the lowest point is y 0
    lows = [min(float(np.concatenate(G[0])[:, 1].min()), 1e9) for G in groups.values()] + [c[1] - h[1] for _n, c, h in extra]
    lift = -min(lows)

    # ---- the pack
    mats = js.get("materials", [])
    images = js.get("images", [])
    meshes, texs, tex_of = [], [], {}
    shd_by_key = {r["key"]: r for r in man.get("shaders", [])}
    shd_meshes = {}   # a shader's key -> the names of the meshes it's on
    for (kind, mi), (Pl, Nl, UVl, Il, _c) in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        pos = np.concatenate(Pl)
        pos = np.stack([pos[:, 0] * U, (pos[:, 1] + lift) * U, pos[:, 2] * U], axis=1)
        nrm = np.concatenate(Nl)
        uv = np.concatenate(UVl)
        idx = np.concatenate(Il)
        mat = mats[mi] if 0 <= mi < len(mats) else {}
        gkey = str(mat.get("name", "")) if str(mat.get("name", "")) in shd_by_key else ""   # (step 12) a surface with a Godot shader of its own
        pbr = mat.get("pbrMetallicRoughness", {})
        fac = pbr.get("baseColorFactor", [1, 1, 1, 1])
        tex = -1
        blend = mat.get("alphaMode") == "BLEND"
        if "baseColorTexture" in pbr:
            ti = js["textures"][pbr["baseColorTexture"]["index"]]
            src = ti.get("source")
            if src is None:
                holes.append("material %s: a texture with no source image (an extension's?): flat" % mat.get("name"))
            else:
                if src not in tex_of:
                    im = images[src]
                    if "bufferView" in im:
                        v = js["bufferViews"][im["bufferView"]]
                        data = bin_[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]]
                    else:
                        data = open(os.path.join(a.export, im["uri"]), "rb").read()
                    tex_of[src] = len(texs)
                    texs.append(data)
                tex = tex_of[src]
                if any(abs(x - 1.0) > 0.02 for x in fac[:3]):
                    holes.append("material %s: its colour factor %s over its texture is dropped (the engine draws a texture as it is)" % (
                        mat.get("name"), [round(x, 2) for x in fac[:3]]))
        name = kind if tex >= 0 else "%s#%02x%02x%02x%s" % (kind, *(int(round(srgb(x) * 255)) for x in fac[:3]), "%02x" % int(round(fac[3] * 255)) if blend and fac[3] < 0.999 else "")
        if gkey:   # its colour where it's drawn plainly is its shader's first picture's, on average; its key after a second '#', which the surface's kind reads past
            name = "%s#%s#%s" % (kind, "ffffff" if tex >= 0 else fallback_hex(shd_by_key[gkey], a.export), gkey)
            shd_meshes.setdefault(gkey, []).append(name)
        joints, weights, bone_map = rigid_skin(len(pos), 0)
        meshes.append({"name": name, "node": 1, "pos": pos.astype(np.float32).ravel().tolist(), "nrm": nrm.astype(np.float32).ravel().tolist(),
                       "uv": uv.astype(np.float32).ravel().tolist() if tex >= 0 or gkey else None, "idx": idx.tolist(), "joints": joints, "weights": weights,
                       "boneMap": bone_map, "ibm": I16, "tex": tex, "blend": blend})
        for k in ("emissiveTexture", "normalTexture", "occlusionTexture"):
            if k in mat:
                holes.append("material %s: its %s isn't carried (the pack takes a colour texture per mesh)" % (mat.get("name"), k))
    for nm, c, (hx, hy, hz) in extra:
        x0, x1, z0, z1, y1 = (c[0] - hx) * U, (c[0] + hx) * U, (c[2] - hz) * U, (c[2] + hz) * U, (c[1] + hy + lift) * U
        pos = np.array([[x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1]], dtype=np.float32)
        joints, weights, bone_map = rigid_skin(4, 0)
        meshes.append({"name": "ride#8d8e89", "node": 1, "pos": pos.ravel().tolist(), "nrm": [0, 1, 0] * 4, "uv": None, "idx": [0, 2, 1, 0, 3, 2],
                       "joints": joints, "weights": weights, "boneMap": bone_map, "ibm": I16, "tex": -1, "blend": False})
    nodes = [{"name": "root", "parent": -1, "kind": 1, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]},
             {"name": "park", "parent": 0, "kind": 2, "t": [0, 0, 0], "q": [0, 0, 0, 1], "s": [1, 1, 1]}]
    os.makedirs(os.path.join(a.game, "props"), exist_ok=True)
    os.makedirs(os.path.join(a.game, "sets"), exist_ok=True)
    nbytes = write_pack(os.path.join(a.game, "props", P + "_park.rpack"), nodes, meshes, [], texs, verbose=False, fresh=True)
    came.append("the park: %d triangles in %d meshes, %d textures, %.1f MB; lifted %.2f m so its lowest point is y 0" % (
        tris_in, len(meshes), len(texs), nbytes / 1048576.0, lift))

    def eng(p):
        return (p[0] * U, (p[1] + lift) * U, p[2] * U)

    # ---- the set
    L = ["# %s: from the Godot scene %s by skate_src/godot (engine_export.gd, godot2set.py). Generated: export and convert again" % (P, man["scene"]),
         "climb", "triangles", "surface ride ride", "surface wall wall", "surface rail rail", "prop %s_park 0 0 0 0 1" % P]
    # ---- (step 12) the park's own shaders, translated and compiled: the set draws their surfaces with them
    sdir = os.path.join(a.game, "props", P + "_shaders")
    if os.path.isdir(sdir):
        shutil.rmtree(sdir)
    for gkey in sorted(shd_meshes, key=lambda k: int(k.split("_")[-1])):
        rec = shd_by_key[gkey]
        try:
            _p, notes = godot_shader.cook(rec, a.export, sdir, lift)
            for nm in shd_meshes[gkey]:
                L.append("shader %s_park %s props/%s_shaders/%s.gshd" % (P, nm, P, gkey))
            came.append("the Godot shader %s (%s, %s): run here, translated, on %d mesh%s" % (rec["shader"], gkey, uniform_said(rec), len(shd_meshes[gkey]),
                                                                                          "" if len(shd_meshes[gkey]) == 1 else "es"))
            for n in notes:
                holes.append("the Godot shader %s (%s): %s" % (rec["shader"], gkey, n))
        except godot_shader.Unsupported as e:
            holes.append("the Godot shader %s (%s) isn't carried: %s. Its surfaces are drawn with their plain material" % (rec["shader"], gkey, e))
    env = man.get("environment") or {}
    sun = [l for l in man.get("lights", []) if l["type"] == "DirectionalLight3D"]
    if sun:
        f = np.array(sun[0]["xf"]["fwd"], dtype=np.float64)
        to = -f / max(np.linalg.norm(f), 1e-9)
        el, az = math.degrees(math.asin(max(-1.0, min(1.0, to[1])))), math.degrees(math.atan2(to[0], to[2]))
        c = sun[0]["color"]
        L.append("sun %.2f %.2f %.3f %.3f %.3f %.3f" % (el, az, sun[0]["energy"], c[0], c[1], c[2]))
        came.append("the sun: %s, elevation %.1f, azimuth %.1f, energy %.2f" % (sun[0]["name"], el, az, sun[0]["energy"]))
        if len(sun) > 1:
            holes.append("%d directional lights: the engine has one sun; the rest are left out" % (len(sun) - 1))
    # (11-12, step 12) A GODOT LIGHT ENDS AT ITS RANGE: its window there is the engine's light reach (the 9th number), and how it falls off inside
    # (energy over distance to the power of its attenuation, 1 by default) is the lamp's reach (the 8th, where it's at half), worked out as
    # where Godot's is half what it is a metre out. ⛔ Written as the 8th number, its range was where it was at half: lanterns 30 units off
    # lit one level's start as brightly as the ones beside it
    def light_line(l):
        x, y, z = eng(l["xf"]["pos"])
        c = l["color"]
        decay = max(0.25, float(l.get("attenuation", 1.0) or 1.0))
        return "light %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f" % (x, y, z, c[0], c[1], c[2], l["energy"], (2.0 ** (1.0 / decay)) * U, l.get("range", 5.0) * U)
    for l in man.get("lights", []):
        if l["type"] == "OmniLight3D":
            L.append(light_line(l))
        elif l["type"] == "SpotLight3D":
            holes.append("spot light %s: the engine's set has point lights; made a point light" % l["name"])
            L.append(light_line(l))
    if man.get("lights"):
        came.append("lights: %d (%s)" % (len(man["lights"]), ", ".join(sorted({l["type"] for l in man["lights"]}))))
    if env:
        if env.get("sky") == "ProceduralSkyMaterial" and "sky_horizon" in env:
            L.append("sky %.3f %.3f %.3f" % tuple(env["sky_horizon"]))
            came.append("the sky: Godot's procedural sky, as its horizon colour")
        elif env.get("sky_shader"):
            sky_rec = shd_by_key.get(env.get("sky_key", ""))
            try:
                if not sky_rec:
                    raise godot_shader.Unsupported("the export didn't carry it (export again)")
                _p, notes = godot_shader.cook(sky_rec, a.export, sdir, lift)
                L.append("skyshader props/%s_shaders/%s.gshd" % (P, sky_rec["key"]))
                came.append("the sky: the Godot sky shader %s, run here, translated" % env["sky_shader"])
                for n in notes:
                    holes.append("the sky shader %s: %s" % (env["sky_shader"], n))
            except godot_shader.Unsupported as e:
                holes.append("the sky is a shader (%s) that isn't carried: %s. The sky is the engine's own" % (env["sky_shader"], e))
            L.append("sky %.3f %.3f %.3f" % tuple(ambient_of(env) or env.get("background_color", [0.5, 0.6, 0.7])))
        else:
            L.append("sky %.3f %.3f %.3f" % tuple(env.get("background_color", [0.5, 0.6, 0.7])))
            came.append("the sky: Godot's background colour")
        if env.get("fog"):
            L.append("fog %g %.3f %.3f %.3f" % (env["fog_density"] / U, *env["fog_color"]))
            came.append("fog: density %.4f a metre" % env["fog_density"])
        if abs(env.get("exposure", 1.0) - 1.0) > 1e-3:
            L.append("exposure %.3f" % env["exposure"])
        for k, what, ours in (("glow", "glow", "bloom"), ("ssao", "screen-space ambient occlusion", "occlusion (`ao`)")):
            if env.get(k):
                holes.append("the environment's %s: the engine's %s is a machine's graphics setting, not a set's" % (what, ours))
    sp = man.get("spawn")
    if sp:
        x, y, z = eng(sp["pos"])
        L.append("mark start %.3f %.3f %.3f %.1f" % (x, y, z, sp["yaw_deg"]))
        came.append("the spawn: (%.1f, %.1f, %.1f) m, facing %.0f degrees" % (*sp["pos"], sp["yaw_deg"]))
    for mk in man.get("markers", []):
        x, y, z = eng(mk["xf"]["pos"])
        L.append("mark %s %.3f %.3f %.3f %.1f" % (mk["name"].split("/")[-1], x, y, z, mk["xf"]["yaw_deg"]))
    L.append("mark godot_origin %.3f %.3f %.3f 0" % eng((0.0, 0.0, 0.0)))   # where Godot's (0, 0, 0) is after the lift: a game's heights in their own terms (the void at -5.6 m)
    with open(os.path.join(a.game, "sets", P + ".set"), "w", newline="\n") as f:
        f.write("\n".join(L) + "\n")

    # ---- the rails: the Park_Rail meshes by their long axis, and the park file's polylines
    rails = []
    for nd in js["nodes"]:
        nm = nd.get("name", "")
        if "mesh" in nd and "Park_Rail" in nm:
            pts = []

            def collect(ni, parent):
                n2 = js["nodes"][ni]
                W = parent @ node_matrix(n2)
                if n2 is nd:
                    for pr in js["meshes"][nd["mesh"]]["primitives"]:
                        p = accessor(js, bin_, pr["attributes"]["POSITION"]).astype(np.float64)
                        pts.append((W[:3, :3] @ p.T).T + W[:3, 3])
                for c in n2.get("children", []):
                    collect(c, W)
            for root_i in js["scenes"][js.get("scene", 0)]["nodes"]:
                collect(root_i, np.eye(4))
            if not pts:
                continue
            p = np.concatenate(pts)
            mid = p.mean(axis=0)
            _u, _s, vt = np.linalg.svd(p - mid, full_matrices=False)
            ax = vt[0]
            t = (p - mid) @ ax
            top = p[:, 1].max()
            a0, a1 = mid + ax * t.min(), mid + ax * t.max()
            a0[1] = a1[1] = top   # the rail's top is what's ground
            if abs(ax[1]) > 0.05:
                a0[1], a1[1] = (mid + ax * t.min())[1], (mid + ax * t.max())[1]
                a0[1] += top - p[:, 1].max()
            rails.append(("mesh", nm.split("_SM_")[-1], [eng(a0), eng(a1)]))
    for r in ((man.get("sidecar") or {}).get("rails") or []):
        rails.append(("sidecar", r.get("name", "rail"), [eng(q) for q in r.get("points", [])]))
    with open(os.path.join(a.game, P + "_rails.txt"), "w", newline="\n") as f:
        f.write("# rail <name> <points...>: a grind rail's line in engine units, from %s\n" % man["scene"])
        for src, nm, pts in rails:
            f.write("rail %s %s\n" % (nm, " ".join("%.3f,%.3f,%.3f" % q for q in pts)))
    came.append("grind rails: %d (%d from the Park_Rail meshes' long axes, %d from the park file)" % (
        len(rails), sum(1 for r in rails if r[0] == "mesh"), sum(1 for r in rails if r[0] == "sidecar")))
    # (11-12) their own rail graph, as their GrindRailGraph built it in the level (engine_export.gd's _rail_graph): every piece, its links and chain
    rg = (man.get("rail_graph") or {}).get("pieces") or []
    if rg:
        with open(os.path.join(a.game, P + "_railgraph.txt"), "w", newline="\n") as f:
            f.write("# piece <a> <b> <link_a> <link_a_end> <link_b> <link_b_end> <chain> <graph> <length m> [body]: their GrindRailGraph's pieces, ends in\n"
                    "# engine units, links by piece number (-1 none), from %s\n" % man["scene"])
            for p in rg:
                aa, bb = eng(p["a"]), eng(p["b"])
                f.write("piece %.4f,%.4f,%.4f %.4f,%.4f,%.4f %d %d %d %d %d %d %.4f%s\n" % (aa[0], aa[1], aa[2], bb[0], bb[1], bb[2], p["link_a"], p["link_a_end"],
                        p["link_b"], p["link_b_end"], p["chain"], p.get("graph", 0), p["length"], (" " + p["body"]) if p.get("body") else ""))
        came.append("their rail graph: %d pieces in %d chains" % (len(rg), len({(p.get("graph", 0), p["chain"]) for p in rg})))

    # ---- the gaps
    gaps = [g for g in man.get("areas", []) if g.get("gap_name")]
    with open(os.path.join(a.game, P + "_gaps.txt"), "w", newline="\n") as f:
        f.write("# gap <points> <cx,cy,cz> <sx,sy,sz> <yaw> <name...>: a gap's box in engine units, from %s\n" % man["scene"])
        for g in gaps:
            for sh in g["shapes"]:
                if sh["type"] == "BoxShape3D":
                    f.write("gap %d %.3f,%.3f,%.3f %.3f,%.3f,%.3f %.1f %s\n" % (g.get("points", 100), *eng(sh["xf"]["pos"]), *(v * U for v in sh["size"]),
                                                                           sh["xf"]["yaw_deg"], g["gap_name"]))
    came.append("gaps: %d" % len(gaps))
    sc = (man.get("sidecar") or {})
    if sc.get("gaps") or sc.get("walls"):
        holes.append("the park file's gaps (%d) and walls (%d) aren't made yet" % (len(sc.get("gaps") or []), len(sc.get("walls") or [])))

    # ---- what didn't come over
    un = man.get("unsupported", {})
    if not man.get("shaders"):   # an export from before step 12 carried no shaders
        for sh, nn in sorted((un.get("shader_materials") or {}).items()):
            holes.append("%d surface(s) draw with the Godot shader %s: drawn with the exported material's colour instead (export it again to carry it)" % (nn, sh))
    if un.get("particles"):
        holes.append("%d particle emitter(s) (%s): the set's `emitter` line is the engine's own; not converted" % (
            len(un["particles"]), ", ".join(sorted({p["type"] for p in un["particles"]}))))
    if un.get("decals"):
        holes.append("%d Decal node(s): not converted (the set's `decal` line throws its own)" % len(un["decals"]))
    if un.get("labels"):
        holes.append("%d Label3D(s): text in the world isn't converted (%s)" % (len(un["labels"]), "; ".join(l["text"] for l in un["labels"][:4])))
    if un.get("instances_expanded"):
        came.append("%d MultiMesh instances, each made a mesh of its own (the engine has no instanced prop)" % un["instances_expanded"])
    if un.get("skinned_skipped"):
        holes.append("skinned meshes left out: %s" % ", ".join(un["skinned_skipped"][:6]))
    for path, nn in sorted((man.get("scripts") or {}).items()):
        holes.append("script %s (%d node%s): its behaviour is the game's to port; what it built when the scene ran is in the export" % (path, nn, "" if nn == 1 else "s"))
    with open(os.path.join(a.game, P + "_import.txt"), "w", newline="\n", encoding="utf-8") as f:
        f.write("# %s from %s\n\n## came over\n" % (P, man["scene"]) + "".join("- %s\n" % c for c in came) + "\n## didn't, or not as it was\n" +
                "".join("- %s\n" % h for h in holes))
    for c in came:
        print("came over:", c)
    for h in holes:
        print("hole:", h)


if __name__ == "__main__":
    main()
