#!/usr/bin/env python3
"""statue2body.py -- an unrigged GLB statue grows a skeleton, no Blender (homecoming H7).

    statue2body.py strip in.glb out.glb     drop skin/anims -> a bare statue (test rig)
    statue2body.py rig   in.glb out.glb     landmarks -> 22 mixamo joints -> skin

The landmark laws carry over from the Blender-era recipe (skate_src/reva2/rig_reva.py),
re-derived in GLB space (Y up, +Z forward):
  - FACING IS ASSERTED FROM ANATOMY before anything is placed: the chest protrudes
    past the ribcage and the foot extends past the calf, both toward +Z, or the bake
    refuses (the circular toe-bone check shipped a backwards character once).
  - the arm band is the widest slab between 60-90% height; leg centres split by x sign;
    the toe is the forwardmost foot point. Nothing is hand-placed.
The skeleton is written as plain glTF nodes (translation-only, so the inverse binds are
pure translations) and a skin with zeroed weights -- autoweights.py owns the binding.
"""
import sys, json, struct
sys.path.insert(0, (__file__.rsplit("\\", 1)[0].rsplit("/", 1)[0] or ".") + "/author")
from decimate_glb import read_glb, acc_read

CHAIN = [
    ("Hips", None, "Spine"), ("Spine", "Hips", "Spine1"), ("Spine1", "Spine", "Spine2"),
    ("Spine2", "Spine1", "Neck"), ("Neck", "Spine2", "Head"), ("Head", "Neck", "HeadEnd"),
    ("LeftShoulder", "Spine2", "LeftArm"), ("LeftArm", "LeftShoulder", "LeftForeArm"),
    ("LeftForeArm", "LeftArm", "LeftHand"), ("LeftHand", "LeftForeArm", "LeftHandEnd"),
    ("RightShoulder", "Spine2", "RightArm"), ("RightArm", "RightShoulder", "RightForeArm"),
    ("RightForeArm", "RightArm", "RightHand"), ("RightHand", "RightForeArm", "RightHandEnd"),
    ("LeftUpLeg", "Hips", "LeftLeg"), ("LeftLeg", "LeftUpLeg", "LeftFoot"),
    ("LeftFoot", "LeftLeg", "LeftToeBase"), ("LeftToeBase", "LeftFoot", "LeftToeEnd"),
    ("RightUpLeg", "Hips", "RightLeg"), ("RightLeg", "RightUpLeg", "RightFoot"),
    ("RightFoot", "RightLeg", "RightToeBase"), ("RightToeBase", "RightFoot", "RightToeEnd"),
]

def write_glb(gj, bin_, dst):
    jb = json.dumps(gj, separators=(",", ":")).encode()
    while len(jb) % 4: jb += b" "
    out = bytearray()
    out += b"glTF" + struct.pack("<II", 2, 12 + 8 + len(jb) + 8 + len(bin_))
    out += struct.pack("<I", len(jb)) + b"JSON" + jb
    out += struct.pack("<I", len(bin_)) + b"BIN\x00" + bytes(bin_)
    open(dst, "wb").write(out)

def strip(src, dst):
    gj, bin_ = read_glb(src)
    # the old JOINT NODES must actually die, not linger as dead hierarchy: a ghost
    # skeleton with the same names steals every later name-lookup (the rig step added
    # 22 fresh joints and the idle then animated the corpses -- a perfect T-pose)
    dead = set()
    for sk in gj.get("skins", []): dead.update(sk["joints"])
    gj.pop("skins", None); gj.pop("animations", None)
    remap = {}
    keep = []
    for i, n in enumerate(gj.get("nodes", [])):
        if i in dead: continue
        remap[i] = len(keep); keep.append(n)
    for n in keep:
        n.pop("skin", None)
        if "children" in n:
            n["children"] = [remap[c] for c in n["children"] if c in remap]
            if not n["children"]: n.pop("children")
    gj["nodes"] = keep
    for sc in gj.get("scenes", []):
        sc["nodes"] = [remap[c] for c in sc["nodes"] if c in remap]
    for m in gj.get("meshes", []):
        for pr in m["primitives"]:
            pr["attributes"].pop("JOINTS_0", None)
            pr["attributes"].pop("WEIGHTS_0", None)
    write_glb(gj, bin_, dst)
    print("stripped statue -> %s (%d ghost joints removed)" % (dst, len(dead)))

def rig(src, dst):
    gj, bin_ = read_glb(src)
    # ⛔ EVERY PRIMITIVE OF THE BODY, NOT primitives[0] OF meshes[0]. A glTF primitive is
    # one MATERIAL's slice of a mesh, so on a Character Creator body with six materials the
    # old form measured facing, the arm band and every landmark from about one sixth of her,
    # picked by material order -- and duly refused to rig Camilia "backwards" when her own
    # chest-minus-waist is +0.041 the right way and her nose is at +Z. It survived this long
    # only because every earlier statue was a single-material scan, where primitives[0] IS
    # the whole body.
    # ⛔ And the LARGEST mesh, not every mesh: a bra sits inside the chest slab and wraps
    # the torso, adding mass at the BACK that nothing balances at the waist. Facing is
    # anatomy, and anatomy means the body.
    def _mesh_pts(m):
        out = []
        for pr in m.get("primitives", []):
            a = pr.get("attributes", {}).get("POSITION")
            if a is not None:
                out.extend(list(v) for v in acc_read(gj, bin_, a))
        return out
    _cand = [(len(p), i, p) for i, p in
             ((i, _mesh_pts(m)) for i, m in enumerate(gj["meshes"]))]
    _cand.sort(key=lambda t: -t[0])
    P = _cand[0][2]
    print("rigging from mesh %d '%s': %d verts across %d primitive(s)"
          % (_cand[0][1], gj["meshes"][_cand[0][1]].get("name", "?"), len(P),
             len(gj["meshes"][_cand[0][1]].get("primitives", []))))
    ymin = min(v[1] for v in P)
    cx = (min(v[0] for v in P) + max(v[0] for v in P)) / 2
    cz = (min(v[2] for v in P) + max(v[2] for v in P)) / 2
    pts = [(v[0] - cx, v[1] - ymin, v[2] - cz) for v in P]
    H = max(p[1] for p in pts)

    def slab(a, b, xs=None):
        o = [p for p in pts if a * H <= p[1] <= b * H]
        if xs is not None:
            o = [p for p in o if (p[0] > 0.02 * H if xs > 0 else p[0] < -0.02 * H)]
        return o
    def meanz(s): return sum(p[2] for p in s) / max(1, len(s))
    def cent(s):
        n = max(1, len(s))
        return (sum(p[0] for p in s)/n, sum(p[1] for p in s)/n, sum(p[2] for p in s)/n)

    # FACING: anatomy, never a bone (the circular-verification law)
    chest = meanz(slab(0.70, 0.78)) - meanz(slab(0.60, 0.66))
    foot = meanz(slab(0.0, 0.05, +1)) - meanz(slab(0.22, 0.30, +1))
    print("facing: chest %+.4f  foot %+.4f  (both must be positive: forward is +Z)" % (chest, foot))
    assert chest > 0 and foot > 0, "statue does not face +Z -- refusing to rig it backwards"

    # the arm band: widest slab 60-90%
    best, armY = 0.0, H * 0.75
    for f in range(60, 90):
        yc = H * f / 100.0
        sl = slab((yc - H*0.015)/H, (yc + H*0.015)/H)
        if sl:
            w = max(p[0] for p in sl) - min(p[0] for p in sl)
            if w > best: best, armY = w, yc
    band = [p for p in pts if armY - H*0.02 <= p[1] <= armY + H*0.02]
    xmax = max(p[0] for p in band)
    print("arm band at y=%.3f, halfspan %.3f" % (armY, xmax))
    def arm_at(sx, x):
        near = [p for p in band if abs(p[0] - x) < 0.05 * H]
        if not near: return (x, armY, 0.0)
        return (x, sum(p[1] for p in near)/len(near), sum(p[2] for p in near)/len(near))
    SH = 0.185 * (xmax / (0.85 * H)) * H if H > 0 else 0.16
    L = {}
    for s, sx in (("Left", +1), ("Right", -1)):
        L[s+"Shoulder"] = (sx*SH*0.55, armY + 0.01*H, arm_at(sx, sx*SH)[2])
        a = arm_at(sx, sx*SH);              L[s+"Arm"] = (a[0], a[1], a[2])
        a = arm_at(sx, sx*(SH+(xmax-SH)*0.47)); L[s+"ForeArm"] = (a[0], a[1], a[2])
        a = arm_at(sx, sx*(SH+(xmax-SH)*0.80)); L[s+"Hand"] = (a[0], a[1], a[2])
        a = arm_at(sx, sx*xmax*0.985);      L[s+"HandEnd"] = (a[0], a[1], a[2])
    def leg(sx, yf):
        sl = slab(yf-0.02, yf+0.02, sx)
        return cent(sl) if sl else (sx*0.1*H, yf*H, 0.0)
    for s, sx in (("Left", +1), ("Right", -1)):
        L[s+"UpLeg"] = leg(sx, 0.52); L[s+"Leg"] = leg(sx, 0.285); L[s+"Foot"] = leg(sx, 0.045)
        fp = slab(0.0, 0.05, sx)
        tz = max(p[2] for p in fp) if fp else 0.1*H
        fx = L[s+"Foot"][0]
        L[s+"ToeBase"] = (fx, H*0.012, tz*0.75)
        L[s+"ToeEnd"] = (fx, H*0.010, tz*0.98)
    hip = ((L["LeftUpLeg"][0]+L["RightUpLeg"][0])/2, H*0.555, (L["LeftUpLeg"][2]+L["RightUpLeg"][2])/2)
    L["Hips"] = hip
    L["Spine"] = (hip[0], H*0.62, hip[2]); L["Spine1"] = (hip[0], H*0.70, hip[2]); L["Spine2"] = (hip[0], H*0.78, hip[2])
    nk = cent(slab(0.85, 0.88)) if slab(0.85, 0.88) else (0, H*0.86, 0)
    L["Neck"] = (nk[0], H*0.865, nk[2])
    hd = cent(slab(0.88, 1.0)) if slab(0.88, 1.0) else (0, H*0.94, 0)
    L["Head"] = (nk[0], H*0.90, hd[2]); L["HeadEnd"] = (nk[0], H*1.00, hd[2])

    # ---- write: re-centre mesh positions to the grounded frame, add joint nodes ----
    # ⛔ EVERY MESH MOVES, NOT ONE PRIMITIVE. This used to write back into
    # prim["attributes"]["POSITION"] -- a single material slice of a single mesh -- which
    # both left the body's other five primitives in the old frame and abandoned the bra,
    # underwear, teeth and eyes where they were. The re-centre is a rigid shift of the WHOLE
    # statue; anything it skips is left floating relative to the skeleton.
    _seen = set()
    _shift = (cx, ymin, cz)
    for _m in gj["meshes"]:
        for _pr in _m.get("primitives", []):
            _a = _pr.get("attributes", {}).get("POSITION")
            if _a is None or _a in _seen:
                continue
            _seen.add(_a)
            _acc = gj["accessors"][_a]
            _bv = gj["bufferViews"][_acc["bufferView"]]
            _off = _bv.get("byteOffset", 0) + _acc.get("byteOffset", 0)
            _n = _acc["count"]
            _lo = [1e30] * 3
            _hi = [-1e30] * 3
            for _i in range(_n):
                _x, _y, _z = struct.unpack_from("<3f", bin_, _off + _i * 12)
                _v = (_x - _shift[0], _y - _shift[1], _z - _shift[2])
                struct.pack_into("<3f", bin_, _off + _i * 12, *_v)
                for _k in range(3):
                    if _v[_k] < _lo[_k]: _lo[_k] = _v[_k]
                    if _v[_k] > _hi[_k]: _hi[_k] = _v[_k]
            _acc["min"] = _lo
            _acc["max"] = _hi
    print("re-centred %d POSITION accessors across %d meshes" % (len(_seen), len(gj["meshes"])))

    base = len(gj["nodes"])
    idx = {}
    for i, (nm, par, tail) in enumerate(CHAIN):
        idx[nm] = base + i
    for (nm, par, tail) in CHAIN:
        world = L[nm]
        pw = L[par] if par else (0.0, 0.0, 0.0)
        node = {"name": nm, "translation": [world[0]-pw[0], world[1]-pw[1], world[2]-pw[2]]}
        gj["nodes"].append(node)
    for (nm, par, tail) in CHAIN:
        if par:
            gj["nodes"][idx[par]].setdefault("children", []).append(idx[nm])
    scene = gj["scenes"][gj.get("scene", 0)]
    scene["nodes"].append(idx["Hips"])
    # inverse binds: pure translations
    ibm_off = len(bin_)
    for (nm, par, tail) in CHAIN:
        w = L[nm]
        m = [1,0,0,0, 0,1,0,0, 0,0,1,0, -w[0], -w[1], -w[2], 1]
        bin_.extend(struct.pack("<16f", *m))
    gj["bufferViews"].append({"buffer": 0, "byteOffset": ibm_off, "byteLength": len(CHAIN)*64})
    gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1, "componentType": 5126,
                            "count": len(CHAIN), "type": "MAT4"})
    gj["skins"] = [{"joints": [idx[nm] for (nm, _p, _t) in CHAIN],
                    "inverseBindMatrices": len(gj["accessors"])-1}]
    # zeroed joints/weights: autoweights.py owns the binding
    # \u26d4 PER PRIMITIVE, SIZED TO ITS OWN VERTEX COUNT. This used to make ONE pair of
    # accessors of len(P) and hang them on primitives[0] of mesh 0 -- so a six-material body
    # had five unskinned primitives and every accessory (bra, underwear, teeth, eyes) had no
    # binding at all. A modular character is the entire point here: the body you can take the
    # clothes off is the one that needs every piece bound to the same skeleton.
    _bound = 0
    for _m in gj["meshes"]:
        for _pr in _m.get("primitives", []):
            _a = _pr.get("attributes", {}).get("POSITION")
            if _a is None:
                continue
            _n = gj["accessors"][_a]["count"]
            _joff = len(bin_)
            bin_.extend(bytes(_n * 4))
            gj["bufferViews"].append({"buffer": 0, "byteOffset": _joff, "byteLength": _n*4})
            gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1,
                                    "componentType": 5121, "count": _n, "type": "VEC4"})
            _pr["attributes"]["JOINTS_0"] = len(gj["accessors"])-1
            _woff = len(bin_)
            for _ in range(_n):
                bin_.extend(struct.pack("<4f", 1, 0, 0, 0))
            gj["bufferViews"].append({"buffer": 0, "byteOffset": _woff, "byteLength": _n*16})
            gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1,
                                    "componentType": 5126, "count": _n, "type": "VEC4"})
            _pr["attributes"]["WEIGHTS_0"] = len(gj["accessors"])-1
            _bound += 1
    # attach the skin to EVERY mesh node, not only the one holding mesh 0
    for n in gj["nodes"]:
        if "mesh" in n:
            n["skin"] = 0
    print("bound %d primitive(s) across %d mesh(es) to the new skeleton"
          % (_bound, len(gj["meshes"])))
    # a 2-frame "tpose" action: the baker measures clips to elect roles and refuses a
    # pack with none (the Blender recipe shipped the same stub for the same reason)
    toff = len(bin_)
    bin_.extend(struct.pack("<2f", 0.0, 1.0/15.0))
    gj["bufferViews"].append({"buffer": 0, "byteOffset": toff, "byteLength": 8})
    gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1, "componentType": 5126,
                            "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0/15.0]})
    tacc = len(gj["accessors"]) - 1
    hipT = gj["nodes"][idx["Hips"]]["translation"]
    voff = len(bin_)
    bin_.extend(struct.pack("<6f", *hipT, *hipT))
    gj["bufferViews"].append({"buffer": 0, "byteOffset": voff, "byteLength": 24})
    gj["accessors"].append({"bufferView": len(gj["bufferViews"])-1, "componentType": 5126,
                            "count": 2, "type": "VEC3"})
    gj["animations"] = [{"name": "tpose",
                         "samplers": [{"input": tacc, "output": len(gj["accessors"])-1,
                                       "interpolation": "LINEAR"}],
                         "channels": [{"sampler": 0,
                                       "target": {"node": idx["Hips"], "path": "translation"}}]}]
    gj["buffers"][0]["byteLength"] = len(bin_)
    write_glb(gj, bin_, dst)
    print("rigged: 22 joints -> %s" % dst)

if __name__ == "__main__":
    cmd, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
    (strip if cmd == "strip" else rig)(src, dst)
