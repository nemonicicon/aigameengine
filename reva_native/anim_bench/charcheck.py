#!/usr/bin/env python3
"""charcheck.py -- what a character has, read off its pack, and how that stands against the character standard (queue step 23, 11-04).

The bought character has most of the features wanted in a model, and is held as the standard. So the standard is a list of things a body either has or
hasn't, each one measured from the file, and its own answers are the bar (design/character_standard.md says why each is on the list).

  charcheck.py PACK [PACK ...]         each body's answers, side by side
  charcheck.py PACK --against STANDARD the rows where PACK has less than STANDARD (a pack path; the bought character's is packs/v1_<body>.rpack)

It reads the pack as the engine does once it's loaded (chunks merged), poses nothing, and opens no window. An eye is found the way the engine finds one: by name, or a small
mesh as deep as it's wide and tall, in the head, off the middle, with a twin. Shape rules are rules of thumb: a row says how it was told.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "skate_src", "author"))
import rpack as RP
import rpackchunks as RC

FINGERS = ("thumb", "index", "middle", "ring", "pinky", "pinkie", "little", "finger")
ARKIT = ["browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight", "cheekPuff", "cheekSquintLeft", "cheekSquintRight", "eyeBlinkLeft", "eyeBlinkRight",
         "eyeLookDownLeft", "eyeLookDownRight", "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight", "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft",
         "eyeSquintRight", "eyeWideLeft", "eyeWideRight", "jawForward", "jawLeft", "jawOpen", "jawRight", "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft",
         "mouthFrownRight", "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight", "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight",
         "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper", "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft", "mouthStretchRight", "mouthUpperUpLeft",
         "mouthUpperUpRight", "noseSneerLeft", "noseSneerRight", "tongueOut"]


def png_size(b):
    if b[:8] == b"\x89PNG\r\n\x1a\n" and len(b) >= 24:
        return struct.unpack(">II", b[16:24])
    if b[:2] == b"\xff\xd8":   # a JPEG: walk to its frame header
        i = 2
        while i + 9 < len(b):
            if b[i] != 0xFF:
                break
            m, ln = b[i + 1], struct.unpack(">H", b[i + 2:i + 4])[0]
            if 0xC0 <= m <= 0xCF and m not in (0xC4, 0xC8, 0xCC):
                h, w = struct.unpack(">HH", b[i + 5:i + 9])
                return w, h
            i += 2 + ln
    return None


def census(path):
    P = RP.Rpack(path)
    img = RC.Img(P.buf)
    file_chunks = RC.split(P.file)[1]
    strs = P.strings()
    nodes = P.nodes()
    W = [None] * len(nodes)
    for i, nd in enumerate(nodes):
        L = RP._m4_trs(nd["t"], nd["q"], nd["s"])
        W[i] = L if nd["parent"] < 0 else RP._m4_mul(W[nd["parent"]], L)
    # every mesh's box at rest, through its own skin (a few hundred vertices each)
    meshes = []
    lo_all, hi_all = [1e9] * 3, [-1e9] * 3
    for mi in range(P.nMeshes):
        rec = P.MESH.unpack_from(P.buf, P.offMeshes + mi * 60)
        name = strs[rec[0]].decode("utf-8", "replace")
        verts, nidx, bc = rec[2], rec[3], rec[4]
        lo, hi = [1e9] * 3, [-1e9] * 3
        if bc:
            bm = struct.unpack_from("<%dH" % bc, P.buf, rec[12])
            bind = struct.unpack_from("<16f", P.buf, rec[14])
            pal = {}
            for v in range(0, verts, max(1, verts // 400)):
                x, y, z = struct.unpack_from("<3f", P.buf, rec[6] + v * 12)
                x, y, z = (bind[0] * x + bind[4] * y + bind[8] * z + bind[12], bind[1] * x + bind[5] * y + bind[9] * z + bind[13], bind[2] * x + bind[6] * y + bind[10] * z + bind[14])
                o = [0.0, 0.0, 0.0]
                for k in range(4):
                    w = P.buf[rec[10] + v * 4 + k] / 255.0
                    if w < 1e-3:
                        continue
                    j = P.buf[rec[9] + v * 4 + k]
                    if j >= bc:
                        continue
                    if j not in pal:
                        pal[j] = RP._m4_mul(W[bm[j]], list(struct.unpack_from("<16f", P.buf, rec[13] + j * 64)))
                    m = pal[j]
                    o[0] += w * (m[0] * x + m[4] * y + m[8] * z + m[12]); o[1] += w * (m[1] * x + m[5] * y + m[9] * z + m[13]); o[2] += w * (m[2] * x + m[6] * y + m[10] * z + m[14])
                for k in range(3):
                    lo[k] = min(lo[k], o[k]); hi[k] = max(hi[k], o[k])
            if lo[0] <= hi[0]:
                for k in range(3):
                    lo_all[k] = min(lo_all[k], lo[k]); hi_all[k] = max(hi_all[k], hi[k])
        meshes.append(dict(mi=mi, name=name, verts=verts, tris=nidx // 3, bones=bc, lo=lo, hi=hi))
    H = max(hi_all[1] - lo_all[1], 1e-9)
    cx0 = 0.5 * (lo_all[0] + hi_all[0])
    for m in meshes:
        e = [m["hi"][k] - m["lo"][k] for k in range(3)]
        m["ok"] = m["lo"][0] <= m["hi"][0]
        m["big"], m["small"] = (max(e), max(min(e), 1e-12)) if m["ok"] else (0.0, 1.0)
        m["e"] = e
        m["cx"] = (0.5 * (m["lo"][0] + m["hi"][0]) - cx0) / H if m["ok"] else 0.0
        m["cy"] = (0.5 * (m["lo"][1] + m["hi"][1]) - lo_all[1]) / H if m["ok"] else 0.0
        m["cz"] = 0.5 * (m["lo"][2] + m["hi"][2]) / H if m["ok"] else 0.0
    names_l = [m["name"].lower() for m in meshes]
    R = {}
    R["pack"] = os.path.basename(path)
    R["stands"] = "%.2f rig units" % H
    bones = [nd for nd in nodes if nd["kind"] == 1]
    R["bones"] = len(bones)
    bl = [b["name"].lower() for b in bones]
    R["finger bones"] = sum(1 for n in bl if any(f in n for f in FINGERS))
    R["toe bones"] = sum(1 for n in bl if "toe" in n)
    R["a jaw bone"] = "yes" if any("jaw" in n for n in bl) else "no"
    R["eye bones"] = sum(1 for n in bl if "eye" in n and "brow" not in n and "lid" not in n and "lash" not in n)
    R["hair or tail chain bones"] = sum(1 for n in bl if n.startswith(("hair", "tail", "pony", "braid")) or "_hair" in n or "hair_" in n)
    R["cloth chain bones"] = sum(1 for n in bl if n.startswith(("cloth", "fur", "skirt", "coat")))
    R["soft bones (jiggle)"] = sum(1 for n in bl if "breast" in n or "jiggle" in n or "belly" in n)
    R["meshes"] = len(meshes)
    R["vertices"] = sum(m["verts"] for m in meshes)
    R["triangles"] = sum(m["tris"] for m in meshes)
    R["vertices in the head"] = sum(m["verts"] for m in meshes if m["ok"] and m["cy"] > 0.86 and m["big"] < 0.25 * H)
    # eyes: by name, or by shape with a twin (the engine's own rule, engine/load.inc packEyesFind), or a person built as one mesh
    cand = [m for m in meshes if m["ok"] and 24 <= m["verts"] <= 20000 and m["big"] <= m["small"] * 1.25 and 0.008 * H <= m["big"] <= 0.045 * H and m["cy"] >= 0.80 and 0.005 <= abs(m["cx"]) <= 0.035]
    twins = [a for a in cand if any(b is not a and a["cx"] * b["cx"] < 0 and abs(abs(a["cx"]) - abs(b["cx"])) < 0.004 and abs(a["cy"] - b["cy"]) < 0.004 and a["big"] < b["big"] * 1.2 and b["big"] < a["big"] * 1.2 for b in cand)]
    named_eyes = [m for m in meshes if any(k in m["name"].lower().split("~h=")[0] for k in ("eye", "iris", "cornea")) and "brow" not in m["name"].lower() and "lash" not in m["name"].lower()]
    if twins:
        R["eyes"] = "%d meshes of their own, found by shape" % len(twins)
    elif named_eyes:
        R["eyes"] = "%d meshes of their own, by name" % len(named_eyes)
    elif any("person_body" in n for n in names_l):
        R["eyes"] = "balls in the one mesh, told by its texture's alpha"
    else:
        R["eyes"] = "none the engine can find (paint on the head)"
    R["eye meshes"] = len(twins) or len(named_eyes)
    R["lashes or brows, by name"] = sum(1 for n in names_l if "lash" in n or "brow" in n)
    # thin wide strips at eye height, in the face: what lashes and brows are when nobody named them
    eyeY = (sum(m["cy"] for m in twins) / len(twins)) if twins else None
    R["lashes or brows, by shape"] = sum(1 for m in meshes if eyeY is not None and m["ok"] and abs(m["cy"] - eyeY) < 0.02 and abs(m["cx"]) < 0.004 and 0.04 * H < m["e"][0] < 0.09 * H and m["e"][1] < 0.02 * H and m not in twins)
    R["teeth or tongue, by name"] = sum(1 for n in names_l if "teeth" in n or "tooth" in n or "tongue" in n or "mouth" in n)
    R["inside the mouth, by shape"] = sum(1 for m in meshes if eyeY is not None and m["ok"] and eyeY - 0.06 < m["cy"] < eyeY - 0.02 and abs(m["cx"]) < 0.004 and m["big"] < 0.04 * H and m not in twins)
    mnames = sorted({img.morph_name(j) for j in range(len(img.morphs))})   # a shape is counted once, by its name: a body in several meshes carries it on each
    R["face shapes (morphs)"] = len(mnames)
    low = {n.lower() for n in mnames}
    R["of ARKit's 52"] = sum(1 for a in ARKIT if a.lower() in low)
    R["a blink"] = "yes" if any("blink" in n for n in low) else "no"
    R["a jaw shape"] = "yes" if any("jaw" in n for n in low) else ("bone" if R["a jaw bone"] == "yes" else "no")
    hair = [m for m in meshes if any(k in m["name"].lower().split("~h=")[0] for k in ("hair", "pony", "braid", "scalp")) and "brow" not in m["name"].lower()]
    R["hair meshes"] = len(hair)
    R["hair vertices"] = sum(m["verts"] for m in hair)
    wear = [m for m in meshes if m["name"].startswith("wear~")]
    pieces = sorted({"~".join(m["name"].split("~")[1:3]) for m in wear})
    outfits = sorted({o for m in wear for o in (m["name"].split("~h=")[0].split("~")[3].split("+") if len(m["name"].split("~h=")[0].split("~")) > 3 else [])})
    R["wardrobe pieces"] = len(pieces)
    R["outfits"] = len(outfits)
    R["skin split by what covers it"] = sum(1 for m in meshes if m["name"].startswith(("skin~h=", "head~h=")))
    R["other garments as meshes"] = sum(1 for m in meshes if not m["name"].startswith("wear~") and any(k in m["name"].lower() for k in ("boot", "shoe", "sleeve", "bra", "pant", "top", "shirt", "coat", "jacket", "dress", "skirt", "sock", "glove", "choker", "strap", "bandana", "earring")))
    R["simulated skirts"] = sum(1 for n in names_l if "~skirt~" in n)
    R["marker nodes"] = ", ".join(sorted(nd["name"] for nd in nodes if nd["name"] in ("TrueScale", "ToonStyle", "VertexCollide"))) or "none"
    # textures: sizes, and how many are normal maps
    nTex, offTex = P.nTex, P.offTexTable
    sizes = []
    for t in range(nTex):
        o, n = struct.unpack_from("<II", P.buf, offTex + t * 16 + 8)
        sz = png_size(P.buf[o:o + min(n, 65536)])
        sizes.append(sz)
    known = [s for s in sizes if s]
    R["textures"] = nTex
    R["largest texture"] = ("%dx%d" % max(known, key=lambda s: s[0] * s[1])) if known else ("cooked" if nTex else "none")
    nmaps = set()
    for i in range(P.nMats):
        nt = struct.unpack_from("<i", P.buf, P.offMats + i * 36 + 28)[0]
        if nt > 0:
            nmaps.add(nt - 1)
    R["normal maps"] = len(nmaps)
    R["materials said in the pack"] = next((struct.unpack_from("<I", c, 0)[0] for t, _w, c in file_chunks if t == b"MATL" and len(c) >= 4), 0)
    R["face marks"] = sum(1 for nd in nodes if nd["name"].startswith("face_"))
    clips = [P.clip(i) for i in range(P.nClips)]
    R["clips of its own"] = len(clips)
    R["seconds of them"] = "%.0f" % sum(c["frames"] / max(c["fps"], 1e-6) for c in clips)
    return R


ROWS = ["stands", "bones", "finger bones", "toe bones", "a jaw bone", "eye bones", "hair or tail chain bones", "cloth chain bones", "soft bones (jiggle)", "meshes", "vertices", "triangles",
        "vertices in the head", "eyes", "lashes or brows, by name", "lashes or brows, by shape", "teeth or tongue, by name", "inside the mouth, by shape", "face shapes (morphs)", "of ARKit's 52",
        "a blink", "a jaw shape", "hair meshes", "hair vertices", "wardrobe pieces", "outfits", "skin split by what covers it", "other garments as meshes", "simulated skirts", "marker nodes",
        "textures", "largest texture", "normal maps", "materials said in the pack", "face marks", "clips of its own", "seconds of them"]
# what counts as having it, for --against: a number at least as big, or the same word
NUMERIC = {"finger bones", "eye meshes", "face shapes (morphs)", "of ARKit's 52", "hair meshes", "normal maps", "face marks", "vertices in the head", "hair or tail chain bones"}


def main(argv):
    packs = [a for a in argv[1:] if not a.startswith("--")]
    if not packs:
        sys.exit(__doc__)
    against = argv[argv.index("--against") + 1] if "--against" in argv else None
    if against:
        packs = [p for p in packs if p != against]
    cols = [census(p) for p in packs] + ([census(against)] if against else [])
    w0 = max(len(r) for r in ROWS) + 2
    ws = [max(len(c["pack"]), max(len(str(c[r])) for r in ROWS)) + 2 for c in cols]
    print(" " * w0 + "".join(c["pack"].ljust(w) for c, w in zip(cols, ws)))
    for r in ROWS:
        print(r.ljust(w0) + "".join(str(c[r]).ljust(w) for c, w in zip(cols, ws)))
    if against:
        std = cols[-1]
        for c in cols[:-1]:
            short = []
            for r in list(NUMERIC) + ["eyes", "a blink", "a jaw shape"]:
                if r in NUMERIC:
                    if isinstance(std.get(r), int) and isinstance(c.get(r), int) and c[r] < std[r]:
                        short.append("%s: %d against %d" % (r, c[r], std[r]))
                elif r == "eyes":
                    if std["eye meshes"] and not c["eye meshes"]:
                        short.append("eyes: %s" % c["eyes"])
                elif std[r] != "no" and c[r] == "no":
                    short.append("%s: no" % r)
            print("\n%s against %s: %s" % (c["pack"], std["pack"], "nothing short on the measured rows" if not short else "short on %d rows" % len(short)))
            for s in sorted(short):
                print("   " + s)


if __name__ == "__main__":
    main(sys.argv)
