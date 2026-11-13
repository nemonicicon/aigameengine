"""glb_stubclip.py -- add a 2-frame rest clip to a GLB that has none.

glb2rpack_anim measures body height from clip motion and dies with
"max() iterable argument is empty" on a pack with no animations. Most rigged exports carry
one; some do not -- Selina's .blend has eight actions and none assigned to her armature, and
the Daz vampire's 405-bone rig CRASHES Blender's glTF animation exporter outright
(KeyError: None in sampling_cache.py resolving parent_bone_uuid, in the animation path, so
it only bites once there is a clip to sample).

⭐ SO WRITE THE CLIP AFTER THE EXPORT, NOT DURING IT. A two-frame rotation track holding one
joint's rest orientation is all the baker needs, and it sidesteps Blender's animation path
entirely. statue2body already does exactly this for the same reason.

    python skate_src/author/glb_stubclip.py models/_out/vampire.glb
"""
import json
import struct
import sys


def read_glb(path):
    b = open(path, "rb").read()
    assert b[:4] == b"glTF", "not a GLB"
    ln, ty = struct.unpack_from("<II", b, 12)
    gj = json.loads(b[20:20 + ln].decode("utf-8"))
    p = 20 + ln
    blen, bty = struct.unpack_from("<II", b, p)
    return gj, bytearray(b[p + 8:p + 8 + blen])


def write_glb(gj, bin_, path):
    jb = json.dumps(gj, separators=(",", ":")).encode()
    while len(jb) % 4:
        jb += b" "
    while len(bin_) % 4:
        bin_.append(0)
    out = bytearray()
    out += b"glTF" + struct.pack("<II", 2, 12 + 8 + len(jb) + 8 + len(bin_))
    out += struct.pack("<I", len(jb)) + b"JSON" + jb
    out += struct.pack("<I", len(bin_)) + b"BIN\x00" + bytes(bin_)
    open(path, "wb").write(bytes(out))


def main(path, name="z_rest"):
    gj, bin_ = read_glb(path)
    if gj.get("animations"):
        print("%s already has %d animation(s) -- nothing to do"
              % (path, len(gj["animations"])))
        return 0
    skins = gj.get("skins") or []
    if not skins or not skins[0].get("joints"):
        print("!! no skin/joints to key")
        return 1
    joint = skins[0]["joints"][0]
    nd = gj["nodes"][joint]
    # \u26d4 KEY THE JOINT'S OWN REST ROTATION, not identity: a node with a rotation in its
    # TRS would be snapped to identity by an identity-valued track, which is a pose change
    # dressed up as a no-op.
    q = nd.get("rotation", [0.0, 0.0, 0.0, 1.0])

    toff = len(bin_)
    bin_.extend(struct.pack("<2f", 0.0, 1.0 / 15.0))
    gj.setdefault("bufferViews", []).append(
        {"buffer": 0, "byteOffset": toff, "byteLength": 8})
    gj.setdefault("accessors", []).append(
        {"bufferView": len(gj["bufferViews"]) - 1, "componentType": 5126,
         "count": 2, "type": "SCALAR", "min": [0.0], "max": [1.0 / 15.0]})
    tacc = len(gj["accessors"]) - 1

    roff = len(bin_)
    for _ in range(2):
        bin_.extend(struct.pack("<4f", *q))
    gj["bufferViews"].append({"buffer": 0, "byteOffset": roff, "byteLength": 32})
    gj["accessors"].append(
        {"bufferView": len(gj["bufferViews"]) - 1, "componentType": 5126,
         "count": 2, "type": "VEC4"})
    racc = len(gj["accessors"]) - 1

    gj["animations"] = [{
        "name": name,
        "samplers": [{"input": tacc, "output": racc, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": joint, "path": "rotation"}}],
    }]
    gj["buffers"][0]["byteLength"] = len(bin_)
    write_glb(gj, bin_, path)
    print("added '%s' (2 frames) on joint %d '%s' -> %s"
          % (name, joint, nd.get("name", "?"), path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "z_rest"))
