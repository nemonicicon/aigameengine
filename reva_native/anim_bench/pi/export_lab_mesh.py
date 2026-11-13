"""export_lab_mesh.py -- one real body out of an rpack, for the GPU benches (v3d_lab2.c, vk_lab.c).

usage: export_lab_mesh.py <pack.rpack> <out.lab>

Writes LAB1: u32 verts, indices, texW, texH; then verts x {f32 pos[3], norm[3], uv[2]; u8 joints[4], weights[4]}, indices x u32, and the
mesh's base texture as RGBA8. The body is centred with its height spanning -1..1, because the benches place it in screen space.
Every mesh of the pack goes in, in order, so a pack's real index count is what gets drawn."""
import io
import struct
import sys

import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
pk = open(src, "rb").read()
assert pk[:4] == b"RPK1"
hd = struct.unpack_from("<15I", pk, 4)
n_mesh, off_mesh, n_tex, off_tex = hd[2], hd[7], hd[5], hd[10]

P, N, T, J, W, I = [], [], [], [], [], []
base = 0
for m in range(n_mesh):
    rec = struct.unpack_from("<15I", pk, off_mesh + 60 * m)
    verts, idx_count, flags = rec[2], rec[3], rec[5]
    off_pos, off_norm, off_uv, off_j, off_w, off_idx = rec[6], rec[7], rec[8], rec[9], rec[10], rec[11]
    if not verts or not idx_count:
        continue
    P.append(np.frombuffer(pk, "<f4", verts * 3, off_pos).reshape(-1, 3))
    N.append(np.frombuffer(pk, "<f4", verts * 3, off_norm).reshape(-1, 3))
    T.append(np.frombuffer(pk, "<f4", verts * 2, off_uv).reshape(-1, 2) if off_uv and (flags & 2) else np.zeros((verts, 2), "<f4"))
    J.append(np.frombuffer(pk, "u1", verts * 4, off_j).reshape(-1, 4))
    W.append(np.frombuffer(pk, "u1", verts * 4, off_w).reshape(-1, 4))
    I.append(np.frombuffer(pk, "<u4", idx_count, off_idx) + base)
    base += verts
P = np.concatenate(P); N = np.concatenate(N); T = np.concatenate(T); J = np.concatenate(J); W = np.concatenate(W); I = np.concatenate(I)

lo, hi = P.min(0), P.max(0)
centre = (lo + hi) * 0.5
scale = 2.0 / max(1e-6, hi[1] - lo[1])
P = (P - centre) * scale

off, length = struct.unpack_from("<II", pk, off_tex + 8)
img = Image.open(io.BytesIO(pk[off:off + length])).convert("RGBA")
tw, th = img.size

rec_t = np.dtype([("p", "<f4", 3), ("n", "<f4", 3), ("t", "<f4", 2), ("j", "u1", 4), ("w", "u1", 4)])
v = np.zeros(len(P), rec_t)
v["p"] = P; v["n"] = N; v["t"] = T; v["j"] = np.minimum(J, 31); v["w"] = W
with open(out, "wb") as f:
    f.write(b"LAB1")
    f.write(struct.pack("<4I", len(P), len(I), tw, th))
    f.write(v.tobytes())
    f.write(I.astype("<u4").tobytes())
    f.write(img.tobytes())
print(f"{src}: {n_mesh} meshes, {len(P)} vertices, {len(I)} indices ({len(I)//3} triangles), bones up to {int(J.max())}, texture {tw}x{th}, -> {out}")
