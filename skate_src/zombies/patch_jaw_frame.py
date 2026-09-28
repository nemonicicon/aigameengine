"""patch_jaw_frame.py -- jaw.py stops ASSUMING the mesh frame and RECOVERS it.

THE WALL. Susan is the first body the face tools have met that they were not written
for, and jaw.py refused her at the door:

    IBM formula check vs stored: worst 4.33e+01
    AssertionError: this pack's inverse binds do not follow the formula

The assert was right to fire and its diagnosis was wrong. Measured on the pack:

  * ALL 52 of her bones break `IBM = inv(boneWorld) . meshNodeWorld` -- the head and
    hips by 0.67, the fingers by 40.
  * But her rest pose still RECONSTRUCTS through her own inverse binds to a median
    6.7e-3, and across the whole face the drift is a near-CONSTANT 6.689e-3 (worst
    equals median) -- a uniform offset, not a deformation. Which is why she renders
    perfectly in the street.
  * Recovering the frame each bone was actually bound against (M = boneWorld . IBM)
    gives a spread of 0.46 across the whole skeleton -- but **1.3e-6 across the head
    chain**, on a frame translated (0.0034, 0.0023, -0.0067) from identity with an
    exactly unit diagonal. Her head was bound in a frame ~7mm off; her HANDS were bound
    somewhere else entirely (a re-posed Mixamo rig).

So the pack is fine and the tool's premise was too narrow: `W[meshNode]` is one way a
baker might produce inverse binds, not the only way. The jaw steals its weight from the
HEAD, so the only frame it has to agree with is the head's -- recover that, verify the
head's own chain agrees on it, and build the new bone's inverse bind there.

The absolute formula check goes; the DIFFERENTIAL rest-pose gate below it stays, and it
is the one that actually protects the body: it re-skins the rest pose after the write and
refuses if any vertex moved.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\skate_src\author\jaw.py"
s = io.open(P, encoding="utf-8", newline="").read()
CR, LF = chr(13), chr(10)
CRLF = CR + LF
WAS_CRLF = CRLF in s
if WAS_CRLF:
    s = s.replace(CRLF, LF)
orig = s

a = """    MW = W[mNode]
    bm = list(struct.unpack_from("<%dH" % boneCount, p.buf, offBM))
    worst = 0.0
    for b in range(boneCount):
        stored = list(struct.unpack_from("<16f", p.buf, offIBM + b * 64))
        mine = m4_mul(m4_inv_affine(W[bm[b]]), MW)
        worst = max(worst, max(abs(stored[k] - mine[k]) for k in range(16)))
    print("   IBM formula check vs stored: worst %.2e" % worst)
    assert worst < 1e-3, "this pack's inverse binds do not follow the formula; refusing to guess"
"""
b = '''    bm = list(struct.unpack_from("<%dH" % boneCount, p.buf, offBM))
    # ---------------- RECOVER the mesh frame; do not assume it ----------------
    # IBM[b] = inv(W[bone_b]) . M  =>  M = W[bone_b] . IBM[b]. An earlier version
    # assumed M was W[meshNode] and asserted every bone agreed, which refused
    # v1_crowd_susan outright (worst 4.33e+01). She is not broken: her rest pose
    # reconstructs through her own inverse binds to a near-constant 6.7e-3 across the
    # whole face -- a uniform offset, not a deformation -- because her head was bound in
    # a frame ~7mm off identity and her HANDS were bound somewhere else again.
    # The jaw takes its weight from the HEAD, so the head's frame is the only one it has
    # to agree with. Recover it there and require the head's own chain to concur.
    hb = [b2 for b2 in range(boneCount) if bm[b2] == head]
    assert hb, "the head is not a skinning bone in this pack; nothing to hang a jaw on"
    def ibm_of(b2):
        return list(struct.unpack_from("<16f", p.buf, offIBM + b2 * 64))
    MW = m4_mul(W[bm[hb[0]]], ibm_of(hb[0]))
    kin = set()
    n2 = head
    while n2 >= 0:
        kin.add(n2)
        n2 = nodes[n2]["parent"]
    chain = [b2 for b2 in range(boneCount) if bm[b2] in kin]
    spread = 0.0
    for b2 in chain:
        M2 = m4_mul(W[bm[b2]], ibm_of(b2))
        spread = max(spread, max(abs(M2[k] - MW[k]) for k in range(16)))
    print("   mesh frame recovered from the head chain (%d bones): spread %.2e, "
          "offset from W[meshNode] (%.4f, %.4f, %.4f)"
          % (len(chain), spread, MW[12] - W[mNode][12], MW[13] - W[mNode][13],
             MW[14] - W[mNode][14]))
    assert spread < 1e-3, ("the head's own chain disagrees about the bind frame; "
                           "refusing to guess")
'''
if s.count(a) != 1:
    print("!! anchor x%d" % s.count(a))
    sys.exit(1)
s = s.replace(a, b)

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched jaw.py")
