#!/usr/bin/env python3
"""gltf_read.py -- a .glb's meshes as numpy arrays: positions, normals, indices and morph targets (dense or sparse), with no library.

    g = Glb(path); g.json, g.prims() -> [(mesh index, primitive index, mesh name)]
    g.attr(acc) -> array        g.prim(mi, pi) -> dict(P, N, I, targets=[{POSITION: deltas}], names=[...])
"""
import json, struct

import numpy as np

_CT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_NC = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


class Glb:
    def __init__(self, path):
        b = open(path, "rb").read()
        assert b[:4] == b"glTF", path + " isn't a .glb"
        n = struct.unpack_from("<I", b, 12)[0]
        self.json = json.loads(b[20:20 + n])
        o = 20 + n
        ln = struct.unpack_from("<I", b, o)[0]
        self.bin = b[o + 8:o + 8 + ln]

    def _view(self, vi, count, nc, dt):
        v = self.json["bufferViews"][vi]
        off = v.get("byteOffset", 0); item = nc * np.dtype(dt).itemsize; stride = v.get("byteStride", item)
        if stride == item:
            return off, None
        return off, stride

    def attr(self, ai):
        a = self.json["accessors"][ai]
        dt, nc, count = _CT[a["componentType"]], _NC[a["type"]], a["count"]
        if "bufferView" in a:
            off0, stride = self._view(a["bufferView"], count, nc, dt)
            off = off0 + a.get("byteOffset", 0)
            if stride is None:
                out = np.frombuffer(self.bin, dt, count * nc, off).reshape(count, nc).astype(np.float64 if dt == np.float32 else np.int64)
            else:
                out = np.stack([np.frombuffer(self.bin, dt, nc, off + i * stride) for i in range(count)]).astype(np.float64 if dt == np.float32 else np.int64)
        else:
            out = np.zeros((count, nc))
        sp = a.get("sparse")
        if sp:
            si, sv = sp["indices"], sp["values"]
            idt = _CT[si["componentType"]]
            iv = self.json["bufferViews"][si["bufferView"]]
            idx = np.frombuffer(self.bin, idt, sp["count"], iv.get("byteOffset", 0) + si.get("byteOffset", 0)).astype(np.int64)
            vv = self.json["bufferViews"][sv["bufferView"]]
            val = np.frombuffer(self.bin, dt, sp["count"] * nc, vv.get("byteOffset", 0) + sv.get("byteOffset", 0)).reshape(-1, nc)
            out = out.astype(np.float64); out[idx] = val
        if a.get("normalized") and dt != np.float32:
            out = out / float(np.iinfo(dt).max)
        return out

    def prims(self):
        return [(mi, pi, m.get("name", "")) for mi, m in enumerate(self.json["meshes"]) for pi in range(len(m["primitives"]))]

    def prim(self, mi, pi):
        m = self.json["meshes"][mi]; p = m["primitives"][pi]
        d = dict(P=self.attr(p["attributes"]["POSITION"]),
                 N=self.attr(p["attributes"]["NORMAL"]) if "NORMAL" in p["attributes"] else None,
                 I=self.attr(p["indices"]).reshape(-1, 3) if "indices" in p else None,
                 targets=[{k: self.attr(v) for k, v in t.items()} for t in p.get("targets", [])],
                 names=(m.get("extras") or {}).get("targetNames") or [])
        return d
