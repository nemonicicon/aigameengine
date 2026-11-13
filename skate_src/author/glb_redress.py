"""glb_redress.py -- A CHARACTER'S MESHES REPLACED INSIDE ITS OWN GLB, nothing else touched: its skeleton, its bind matrices and every clip stay byte for byte what they were, new bones go
on the end of its skin, and the clothes come in as meshes named by the wardrobe's tags (design/atelier.md). A round trip through Blender's exporter rewrites the skeleton's rolls and
resamples the clips; a character that came from somewhere else with 81 clips on it doesn't get to find out what that does.

11-02, the alien: he is taken without his clothing, and his clothes are made again by the engine's tools.

    redress(base_glb, out_glb, drop=[node names], rename={node: new name}, meshes=[Mesh...], bones=[(name, head, parent)], materials=[glTF materials])

  Mesh(name, prims)   prims: [(material name, positions, normals, uvs or None, joints as bone names, weights, indices[, second uvs])], in glTF's axes (metres, Y up, +Z the body's front).
                      The second UV carries the worn cloth's numbers for each point (reva_native/clubcloth): how far it may go from the skinned pose, how near the skin it may come
  bones               (name, head in glTF's axes, parent bone): rotated like the world at rest, a child of that bone, on the end of the skin
  materials           new fabrics, as glTF writes them: {"name", "pbrMetallicRoughness", "doubleSided"}. The meshes name them like the ones already there

No bpy: it reads and writes the glb itself.
"""
import json
import struct

import numpy as np

COMPONENT = {5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}


class Mesh:
    def __init__(self, name, prims):
        self.name, self.prims = name, prims


def read_glb(path):
    with open(path, "rb") as f:
        magic, version, length = struct.unpack("<III", f.read(12))
        assert magic == 0x46546C67, path + " is not a glb"
        size, kind = struct.unpack("<II", f.read(8))
        gltf = json.loads(f.read(size))
        size, kind = struct.unpack("<II", f.read(8))
        blob = f.read(size)
    return gltf, blob


def write_glb(path, gltf, blob):
    text = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    text += b" " * ((4 - len(text) % 4) % 4)
    blob = bytes(blob) + b"\0" * ((4 - len(blob) % 4) % 4)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob)))
        f.write(struct.pack("<II", len(text), 0x4E4F534A))
        f.write(text)
        f.write(struct.pack("<II", len(blob), 0x004E4942))
        f.write(blob)


def _node_matrix(n):
    if "matrix" in n:
        return np.array(n["matrix"], float).reshape(4, 4).T
    t = n.get("translation", [0, 0, 0])
    x, y, z, w = n.get("rotation", [0, 0, 0, 1])
    s = n.get("scale", [1, 1, 1])
    R = np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                  [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                  [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])
    M = np.eye(4)
    M[:3, :3] = R * np.array(s, float)
    M[:3, 3] = t
    return M


def _quat_of(R):
    """a rotation matrix as glTF's [x, y, z, w]"""
    t = R[0, 0] + R[1, 1] + R[2, 2]
    if t > 0:
        s = 0.5 / np.sqrt(t + 1.0)
        q = [(R[2, 1] - R[1, 2]) * s, (R[0, 2] - R[2, 0]) * s, (R[1, 0] - R[0, 1]) * s, 0.25 / s]
    else:
        i = int(np.argmax([R[0, 0], R[1, 1], R[2, 2]]))
        j, k = (i + 1) % 3, (i + 2) % 3
        s = 2.0 * np.sqrt(1.0 + R[i, i] - R[j, j] - R[k, k])
        q = [0.0, 0.0, 0.0, (R[k, j] - R[j, k]) / s]
        q[i] = 0.25 * s
        q[j] = (R[j, i] + R[i, j]) / s
        q[k] = (R[k, i] + R[i, k]) / s
    q = np.array(q, float)
    return [float(c) for c in q / np.linalg.norm(q)]


def redress(base, out, drop=(), rename=None, meshes=(), bones=(), materials=()):
    rename = rename or {}
    g, blob = read_glb(base)
    g.setdefault("materials", []).extend(materials)
    nodes = g["nodes"]
    index_of = {n.get("name"): i for i, n in enumerate(nodes)}
    assert len(g.get("skins", [])) == 1, "one skin expected"
    skin = g["skins"][0]
    # The mesh nodes dropped or kept.
    drop = set(drop)
    parent_of = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent_of[c] = i
    holder = None
    keep_meshes = []
    remove_nodes = set()
    for i, n in enumerate(nodes):
        if "mesh" not in n:
            continue
        if holder is None and i in parent_of:
            holder = parent_of[i]
        if n.get("name") in drop:
            remove_nodes.add(i)
        else:
            keep_meshes.append(n["mesh"])
    for name in drop:
        assert name in index_of and "mesh" in nodes[index_of[name]], "no mesh node named %s" % name
    for name, to in rename.items():
        nodes[index_of[name]]["name"] = to
    # Global rests, for the new bones.
    def global_of(i):
        M = _node_matrix(nodes[i])
        p = parent_of.get(i)
        return global_of(p) @ M if p is not None else M
    # What of the old file stays: the accessors the clips, the skin and the kept meshes read, the buffer views those and the images read.
    used_accessors = set()
    for a in g.get("animations", []):
        for s in a["samplers"]:
            used_accessors.add(s["input"])
            used_accessors.add(s["output"])
    used_accessors.add(skin["inverseBindMatrices"])
    mesh_map = {}
    new_meshes = []
    for m in sorted(set(keep_meshes)):
        mesh_map[m] = len(new_meshes)
        new_meshes.append(g["meshes"][m])
        for p in g["meshes"][m]["primitives"]:
            used_accessors.update(p["attributes"].values())
            if "indices" in p:
                used_accessors.add(p["indices"])
            for t in p.get("targets", []):
                used_accessors.update(t.values())
    used_views = {g["accessors"][a]["bufferView"] for a in used_accessors}
    for im in g.get("images", []):
        if "bufferView" in im:
            used_views.add(im["bufferView"])
    # Pack again.
    out_blob = bytearray()
    views, view_map = [], {}

    def add_view(data, target=None):
        while len(out_blob) % 4:
            out_blob.append(0)
        v = {"buffer": 0, "byteOffset": len(out_blob), "byteLength": len(data)}
        if target is not None:
            v["target"] = target
        out_blob.extend(data)
        views.append(v)
        return len(views) - 1

    for i in sorted(used_views):
        v = g["bufferViews"][i]
        start = v.get("byteOffset", 0)
        nv = add_view(blob[start:start + v["byteLength"]], v.get("target"))
        if "byteStride" in v:
            views[nv]["byteStride"] = v["byteStride"]
        view_map[i] = nv
    accessors, accessor_map = [], {}
    for a in sorted(used_accessors):
        d = dict(g["accessors"][a])
        d["bufferView"] = view_map[d["bufferView"]]
        accessor_map[a] = len(accessors)
        accessors.append(d)
    for an in g.get("animations", []):
        for s in an["samplers"]:
            s["input"] = accessor_map[s["input"]]
            s["output"] = accessor_map[s["output"]]
    for m in new_meshes:
        for p in m["primitives"]:
            p["attributes"] = {k: accessor_map[v] for k, v in p["attributes"].items()}
            if "indices" in p:
                p["indices"] = accessor_map[p["indices"]]
    for im in g.get("images", []):
        if "bufferView" in im:
            im["bufferView"] = view_map[im["bufferView"]]
    for i in range(len(nodes)):
        if "mesh" in nodes[i] and i not in remove_nodes:
            nodes[i]["mesh"] = mesh_map[nodes[i]["mesh"]]
    # New bones: on the end of the skin, the old joints keep their numbers.
    joints = list(skin["joints"])
    a = g["accessors"][skin["inverseBindMatrices"]]
    v = g["bufferViews"][a["bufferView"]]
    ibm = np.frombuffer(blob, np.float32, a["count"] * 16, v.get("byteOffset", 0) + a.get("byteOffset", 0)).reshape(-1, 4, 4).copy()
    new_ibm = []
    # Bones may name each other as parents, in order.
    added = {}
    for name, head, parent in bones:
        p = added.get(parent, index_of.get(parent))
        assert p is not None, "no bone %s for %s" % (parent, name)
        G = global_of(p)
        # A child rotated like the world at rest: its own local rotation undoes its parent's.
        R = G[:3, :3]
        scale = np.linalg.norm(R, axis=0)
        Rn = R / scale
        local_r = Rn.T
        local_t = np.linalg.inv(G) @ np.array([head[0], head[1], head[2], 1.0])
        node = {"name": name, "translation": [float(c) for c in local_t[:3]], "rotation": _quat_of(local_r)}
        nodes.append(node)
        i = len(nodes) - 1
        nodes[p].setdefault("children", []).append(i)
        parent_of[i] = p
        index_of[name] = i
        added[name] = i
        joints.append(i)
        M = np.eye(4)
        M[:3, 3] = [-head[0], -head[1], -head[2]]
        new_ibm.append(M.T)                    # glTF's matrices are column-major
    if new_ibm:
        all_ibm = np.concatenate([ibm, np.array(new_ibm, np.float32)])
        nv = add_view(all_ibm.astype(np.float32).tobytes())
        accessors.append({"bufferView": nv, "componentType": 5126, "count": len(all_ibm), "type": "MAT4"})
        skin["inverseBindMatrices"] = len(accessors) - 1
    else:
        skin["inverseBindMatrices"] = accessor_map[skin["inverseBindMatrices"]]
    skin["joints"] = joints
    joint_of = {nodes[j]["name"]: k for k, j in enumerate(joints)}
    # New meshes.
    mat_of = {m["name"]: i for i, m in enumerate(g.get("materials", []))}

    def add_accessor(arr, comp, kind, target, minmax=False):
        nv = add_view(arr.tobytes(), target)
        d = {"bufferView": nv, "componentType": comp, "count": len(arr), "type": kind}
        if minmax:
            d["min"] = [float(c) for c in arr.min(0)]
            d["max"] = [float(c) for c in arr.max(0)]
        accessors.append(d)
        return len(accessors) - 1

    new_nodes = []
    for mesh in meshes:
        # A glTF importer keeps node names unique: a mesh named like a bone has the bone renamed (Godot: head_2), which the character's own clips follow and every clip made for it elsewhere doesn't.
        assert mesh.name not in index_of, "%s is already a node's name in %s" % (mesh.name, base)
        prims = []
        for mat, pos, nor, uvs, joint_names, wts, idx, *more in mesh.prims:
            J = np.array([[joint_of.get(b, 0) if b else 0 for b in row] for row in joint_names], np.uint16)
            W = np.array(wts, np.float32)
            W = W / np.maximum(W.sum(1, keepdims=True), 1e-9)
            attrs = {"POSITION": add_accessor(np.array(pos, np.float32), 5126, "VEC3", 34962, True),
                     "NORMAL": add_accessor(np.array(nor, np.float32), 5126, "VEC3", 34962),
                     "JOINTS_0": add_accessor(J, 5123, "VEC4", 34962),
                     "WEIGHTS_0": add_accessor(W, 5126, "VEC4", 34962)}
            if uvs is not None:
                attrs["TEXCOORD_0"] = add_accessor(np.array(uvs, np.float32), 5126, "VEC2", 34962)
            if more and more[0] is not None:
                attrs["TEXCOORD_1"] = add_accessor(np.array(more[0], np.float32), 5126, "VEC2", 34962)
            I = np.array(idx, np.uint32)
            prims.append({"attributes": attrs, "indices": add_accessor(I, 5125, "SCALAR", 34963), "material": mat_of[mat]})
        new_meshes.append({"name": mesh.name, "primitives": prims})
        node = {"name": mesh.name, "mesh": len(new_meshes) - 1, "skin": 0}
        nodes.append(node)
        new_nodes.append(len(nodes) - 1)
    # The removed nodes go, every number after them moves down.
    nodes_new, node_map = [], {}
    for i, n in enumerate(nodes):
        if i not in remove_nodes:
            node_map[i] = len(nodes_new)
            nodes_new.append(n)
    for n in nodes_new:
        if "children" in n:
            n["children"] = [node_map[c] for c in n["children"] if c not in remove_nodes]
    if holder is not None:
        nodes_new[node_map[holder]].setdefault("children", []).extend(node_map[c] for c in new_nodes)
    skin["joints"] = [node_map[j] for j in skin["joints"]]
    if "skeleton" in skin:
        skin["skeleton"] = node_map[skin["skeleton"]]
    for s in g.get("scenes", []):
        s["nodes"] = [node_map[i] for i in s["nodes"] if i not in remove_nodes]
    for an in g.get("animations", []):
        for c in an["channels"]:
            c["target"]["node"] = node_map[c["target"]["node"]]
    g["nodes"], g["meshes"], g["accessors"], g["bufferViews"] = nodes_new, new_meshes, accessors, views
    g["buffers"] = [{"byteLength": len(out_blob)}]
    write_glb(out, g, out_blob)
    print("[redress] %s: %d nodes, %d meshes (%d new), %d joints (%d new), %.1f MB" % (out, len(nodes_new), len(new_meshes), len(meshes), len(skin["joints"]), len(bones), len(out_blob) / 1e6))
