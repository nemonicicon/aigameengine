#!/usr/bin/env python3
"""mouth_dark.py -- paint the inside of a sealed mouth DARK, so opening it reads.

    mouth_dark.py <pack> [--amount 0.72] [--dry]

jacob, on kaeli's first working jaw: "kaelis mouth opening looks like an eldritch
monster lol. it should be black inside right? or shadowed?"

It should, and there was nothing in there to be dark. Both club mouths are SEALED --
lips modelled shut, no interior, no teeth, no tongue. When the jaw opens, the surface
that comes into view is the inner lip, and it is painted the same pink as the outer
lip, so a mouth opening looks like flesh unfolding rather than a cavity appearing.

The fix belongs in the ART, not the rig: find the texels that the inside of the mouth
uses and darken them. The inner surface is found geometrically -- vertices in the lip
band whose normals point INTO the head (away from the face's forward direction) -- and
their UV footprint is dimmed toward black, with a soft edge so the lip line does not
get a hard seam. The texture is re-encoded and appended to the pack (append-and-repoint,
like everything else here); a .tex.bak is written first.
"""
import sys, os, math, struct, io as _io
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR
from authorlib import rig_map, fk_world
from fingers import m4_trs, m4_mul, xform

def main(path, amount=0.72, dry=False):
    from PIL import Image, ImageDraw, ImageFilter
    p = Rpack(path)
    RM = rig_map(p)
    nodes = p.nodes(); names = p.node_names()
    W = []
    for i, nd in enumerate(nodes):
        loc = m4_trs(nd["t"], nd["q"], nd["s"])
        W.append(loc if nd["parent"] < 0 else m4_mul(W[nd["parent"]], loc))
    rec = p.MESH.unpack_from(p.buf, p.offMeshes)
    mNode, verts, offPos, offNorm, offUV = rec[1], rec[2], rec[6], rec[7], rec[8]
    assert offUV and offNorm, "need UVs and normals to find the inside of a mouth"
    MW = W[mNode]
    wp, _ = fk_world(nodes)
    assert "face_mouth" in names, "run face_marks.py first -- the mouth must be known"
    mk = wp[names.index("face_mouth")]
    eyel = wp[names.index("face_eye_l")]
    faceh = max(1e-6, (eyel[1] - mk[1]) / 0.34)
    head = wp[RM["head"]]
    fwd = [mk[0] - head[0], 0.0, mk[2] - head[2]]
    fl = math.hypot(fwd[0], fwd[2]) or 1.0
    fwd = [fwd[0] / fl, 0.0, fwd[2] / fl]

    tab = p.offTexTable
    toff, tlen = struct.unpack_from("<II", p.buf, tab + 8)
    img = Image.open(_io.BytesIO(bytes(p.buf[toff:toff + tlen]))).convert("RGB")
    TW, TH = img.size
    inband = bytearray(verts); far = bytearray(verts); uvs = [None] * verts
    for vi in range(verts):
        mv = list(struct.unpack_from("<3f", p.buf, offPos + vi * 12))
        r = xform(MW, mv)
        u, v = struct.unpack_from("<2f", p.buf, offUV + vi * 8)
        uvs[vi] = ((u % 1.0) * (TW - 1), (1.0 - (v % 1.0)) * (TH - 1))
        # THE SURFACE THE JAW REVEALS -- the seam band just under the lip line, inside
        # the mouth's own width. Selecting by NORMAL instead caught 9857 verts across
        # half her face and still missed the stretched seam strands, which stayed pale
        # and read as fringe hanging in the dark.
        dy9 = mk[1] - r[1]
        if -0.02 * faceh <= dy9 <= 0.20 * faceh and abs(r[0] - mk[0]) <= 0.30 * faceh:
            inband[vi] = 1
        if abs(r[1] - mk[1]) > 1.5 * faceh or abs(r[0] - mk[0]) > 1.5 * faceh:
            far[vi] = 1                                  # nowhere near the face: the leak check
    # ⛔ PAINT THE TRIANGLES, NOT DISCS ROUND THE VERTICES (2026-09-10). The first version drew a
    # TW/220 disc at every band vertex's UV and blurred it by TW/400. On a hand-unwrapped head
    # the neighbouring texels are more face; on a GENERATED CHIP ATLAS (Meshy) they are any
    # part of the body at all. Measured on reva2: of the 150 vertices that sampled a darkened
    # texel, 108 were BELOW HER FACE -- dark blots on her left shoulder, belt, hip chain, right
    # thigh and right boot, for three weeks. A triangle's own footprint cannot leave its chip.
    rec_i = p.MESH.unpack_from(p.buf, p.offMeshes)
    idx = struct.unpack_from("<%dI" % rec_i[3], p.buf, rec_i[11])
    mask = Image.new("L", (TW, TH), 0)
    dr = ImageDraw.Draw(mask)
    n_in = wrap = 0
    for t in range(0, len(idx), 3):
        a, b, c = idx[t], idx[t + 1], idx[t + 2]
        k = inband[a] + inband[b] + inband[c]
        if not k: continue
        tri = [uvs[a], uvs[b], uvs[c]]
        if max(q[0] for q in tri) - min(q[0] for q in tri) > TW * 0.25 or \
           max(q[1] for q in tri) - min(q[1] for q in tri) > TH * 0.25:
            wrap += 1; continue                          # a UV seam wrap, not a real footprint
        dr.polygon(tri, fill=255 if k == 3 else 110)     # band edge triangles fade the lip line
        n_in += 1
    print("mouth_dark: %s -- %d inner-mouth triangles (%d wrapped, skipped), texture %dx%d"
          % (os.path.basename(path), n_in, wrap, TW, TH))
    if n_in < 6:
        print("   too few to be a mouth interior; nothing painted"); return 0
    mask = mask.filter(ImageFilter.GaussianBlur(max(1.0, TW / 2048.0)))
    mpx = mask.load()
    leak = sum(1 for vi in range(verts) if far[vi] and mpx[int(uvs[vi][0]), int(uvs[vi][1])] > 8)
    print("   leak check: %d vertices away from the face sample the painted texels" % leak)
    if leak:
        print("   !! REFUSED: this paint would land on another part of the body"); return 1
    dark = Image.new("RGB", (TW, TH), (14, 8, 12))
    out = Image.composite(dark, img, mask.point(lambda a: int(a * amount)))
    if dry:
        out.save(os.path.splitext(path)[0] + "_mouthdark.png"); print("   dry run: wrote a preview png"); return 0
    # the sheet keeps its own format: append-and-repoint leaves the old blob in the file, so a
    # 4K JPEG re-encoded as PNG added ~30 MB of dead weight per run.
    fmt = Image.open(_io.BytesIO(bytes(p.buf[toff:toff + tlen]))).format or "PNG"
    buf = _io.BytesIO()
    if fmt == "JPEG": out.save(buf, format="JPEG", quality=95, subsampling=0)
    else: out.save(buf, format="PNG")
    blob = buf.getvalue()
    o = bytearray(p.buf)
    while len(o) % 4: o.append(0)
    noff = len(o); o.extend(blob)
    struct.pack_into("<II", o, tab + 8, noff, len(blob))
    h = list(HDR.unpack_from(o, 4)); h[13] = len(o); HDR.pack_into(o, 4, *h)
    if not os.path.exists(path + ".tex.bak"):
        open(path + ".tex.bak", "wb").write(p.buf)
    open(path, "wb").write(o)
    print("   painted the interior dark (%.0f%%), texture %d -> %d bytes"
          % (amount * 100, tlen, len(blob)))
    return 0

if __name__ == "__main__":
    a = sys.argv[1:]
    amt = float(a[a.index("--amount") + 1]) if "--amount" in a else 0.72
    main(a[0], amt, "--dry" in a)
