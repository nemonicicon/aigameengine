#!/usr/bin/env python3
"""retarget_skate.py -- a skate move library onto ANOTHER body, baked into its own pack, board and all.

    python skate_src/author/retarget_skate.py reva_native/packs/v1_r2moves.rpack reva_native/packs/v1_clanker.rpack \
        [--clips "z_rm_sk_*"] [--exclude z_rm_sk_pickup] [--head-tilt 0.25] [--dry]

Written for Clanker (09-27, jacob: "let me see him in engine, in the skate park, skating"): a 1.72 m robot
whose hips stand 0.61 m up, against reva's 1.05. The skate library was authored on reva, and a scene's
cast plays clips from its OWN pack (the p3 ledger: a borrowed partner clip is a T-pose), so the moves are
baked onto him here:

  * the body POSE through the canonical transfer (retarget.py -- the engine's donorPoseF in Python):
    absolute semantics, so a pose means the same pose on both bodies whatever their rests;
  * the FEET and HANDS, which the 16-bone canon does not carry, by their deviation from rest (both
    bodies stand feet-flat at rest, so "the same tilt from flat" keeps his soles flat on the deck);
  * the HIPS' travel scaled by the LEG-length ratio (thigh + shin), as a delta from each body's rest
    height -- a crouch of 20 cm on reva is 11 cm on him;
  * the BOARD channel (`<clip>_bd`: the deck-top frame in the rider's model space) re-derived from HIS
    feet: each frame the board keeps the offset from the toe-joint midpoint that it had under reva's, with
    her board's rotation -- so a kickflip still turns under his feet, authored against hers.
"""
import argparse, fnmatch, math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from authorlib import fk_world, qmul, qinv, qnorm
from retarget import Basis, transfer, char_frame, NCANON

TRUNK = (0, 1, 2, 3, 4, 5)   # hips, spine x3, neck, head
LEVEL = (4, 5)               # neck, head: a robot TURNS his head, he does not tilt it
HEAD_TILT = 0.25             # how much of the donor's head TILT is kept (all of its TURN is); --head-tilt.
                             # A quarter is Clanker's (a cylinder head does not cock); a person keeps it all.


def level(q, keep):
    """swing-twist about world up: keep the twist (the turn), scale the swing (the tilt) by `keep`.
    Measured on the ride: reva's authored head rolls 37.5 degrees to look down the board -- on her, with
    hair and a face, a glance over the shoulder; on Clanker's cylinder, a broken neck (the second bench,
    and a control bench of reva showed she really does it). The turn is kept whole, the tilt a quarter."""
    x, y, z, w = q
    tw = qnorm([0.0, y, 0.0, w]) if (y * y + w * w) > 1e-12 else [0.0, 0.0, 0.0, 1.0]
    sw = qmul(q, qinv(tw))
    if sw[3] < 0: sw = [-c for c in sw]
    ang = 2.0 * math.acos(max(-1.0, min(1.0, sw[3])))
    if ang < 1e-6: return qnorm(q)
    ax = [sw[0], sw[1], sw[2]]; n = math.sqrt(sum(c * c for c in ax)) or 1.0
    h = ang * keep * 0.5
    sw2 = [ax[0] / n * math.sin(h), ax[1] / n * math.sin(h), ax[2] / n * math.sin(h), math.cos(h)]
    return qnorm(qmul(sw2, tw))


def transfer_mixed(dB, wq_d, tB):
    """retarget.transfer, with the TRUNK by deviation from rest instead of by absolute pose.
    ⛔ ABSOLUTE SEMANTICS GAVE CLANKER REVA'S POSTURE: her neck rests tilted ~25 degrees forward and her
    spine curves, and "the same pose on both bodies" laid that on a robot whose neck is a pivot under a
    cylinder -- his head came out cocked 40 degrees, a broken neck on every frame (the first bench). Both
    bodies stand upright at rest, so the trunk takes HER MOVEMENT from rest (a turn of the head, a lean);
    the limbs keep absolute semantics, because their rests really differ (her arms out, his hanging)."""
    Gd, Gt = char_frame(dB), char_frame(tB)
    GG = qmul(Gt, qinv(Gd))
    out, wt = {}, [None] * NCANON
    for c in range(NCANON):
        db, tb = dB.pb[c], tB.pb[c]
        if db < 0 or tb < 0 or dB.A[c] is None or tB.A[c] is None: continue
        D = qmul(wq_d[db], qinv(dB.W[c]))
        if c in TRUNK:
            if c in LEVEL:
                D = level(D, HEAD_TILT)
            Wp = qmul(qmul(GG, qmul(D, qinv(GG))), tB.W[c])
        else:
            Wp = qmul(qmul(GG, qmul(D, dB.A[c])), qmul(qinv(tB.A[c]), tB.W[c]))
        wt[c] = Wp
        a = tB.anc[c]
        Pw = qmul(wt[a], tB.pre[c]) if (a >= 0 and wt[a] is not None) else tB.Wpar[c]
        out[tb] = qnorm(qmul(qinv(Pw), Wp))
    return out
from rpack import Rpack, append_clips, lint_name


def main():
    global HEAD_TILT
    ap = argparse.ArgumentParser()
    ap.add_argument("donor"); ap.add_argument("target")
    ap.add_argument("--clips", default="z_rm_sk_*")
    ap.add_argument("--exclude", default="z_rm_sk_pickup")
    ap.add_argument("--head-tilt", type=float, default=HEAD_TILT)
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    HEAD_TILT = a.head_tilt
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    D, T = Rpack(a.donor), Rpack(a.target)
    dB, tB = Basis(D), Basis(T)
    dn, tn = D.nodes(), T.nodes()
    dnames, tnames = [n["name"] for n in dn], [n["name"] for n in tn]
    have = set(T.clip_names())

    def length(B, c0, c1, end):
        p = B.wp
        return math.dist(p[B.pb[c0]], p[B.pb[c1]]) + math.dist(p[B.pb[c1]], p[end])
    leg_d = length(dB, 12, 13, dB.footL); leg_t = length(tB, 12, 13, tB.footL)
    s = leg_t / leg_d
    hd, ht = dB.pb[0], tB.pb[0]
    print("legs: donor %.3f, target %.3f -> the hips travel x%.3f" % (leg_d, leg_t, s))

    # feet and hands by name (the canon has no ankle or wrist)
    EXTRA = [nm for nm in ("LeftFoot", "RightFoot", "LeftHand", "RightHand") if nm in dnames and nm in tnames]
    ex_d = {nm: dnames.index(nm) for nm in EXTRA}; ex_t = {nm: tnames.index(nm) for nm in EXTRA}
    print("feet and hands by deviation from rest:", ", ".join(EXTRA) or "none")

    names = [c for c in D.clip_names() if not c.endswith("_bd")
             and any(fnmatch.fnmatch(c, p) for p in a.clips.split(","))
             and not any(fnmatch.fnmatch(c, p) for p in a.exclude.split(",") if p)]
    out = []
    for nm in names:
        if nm in have:
            print("   %-18s already on the target -- skipped" % nm); continue
        ci = D.clip_names().index(nm); c = D.clip(ci); F = c["frames"]
        rot, tra = {}, {}
        for node, typ, data in D.tracks(ci):
            arr = struct.unpack("<%df" % (len(data) // 4), data)
            if typ == 0: rot[node] = [arr[f * 4:f * 4 + 4] for f in range(F)]
            elif typ == 1: tra[node] = [arr[f * 3:f * 3 + 3] for f in range(F)]
        bd_name = nm + "_bd"
        bd = None
        if bd_name in D.clip_names():
            bci = D.clip_names().index(bd_name); bF = D.clip(bci)["frames"]
            bT = bR = None
            for node, typ, data in D.tracks(bci):
                arr = struct.unpack("<%df" % (len(data) // 4), data)
                if node == 0 and typ == 1: bT = [arr[f * 3:f * 3 + 3] for f in range(bF)]
                if node == 0 and typ == 0: bR = [arr[f * 4:f * 4 + 4] for f in range(bF)]
            if bT and bR:
                bd = (bT, bR, bF)
        tracks = {}
        board_T, board_R = [], []
        worst_foot = 0.0
        for f in range(F):
            qo = {n: v[f] for n, v in rot.items()}
            to = {n: v[f] for n, v in tra.items()}
            wp_d, wq_d = fk_world(dn, qo, to)
            loc = transfer_mixed(dB, wq_d, tB)                # {target node: local q}
            t_over = {}
            if hd in tra:                                      # the hips' travel, scaled by the legs
                rd, rt = dn[hd]["t"], tn[ht]["t"]
                t_over[ht] = [rt[k] + (tra[hd][f][k] - rd[k]) * s for k in range(3)]
            wp_t, wq_t = fk_world(tn, loc, t_over)
            # feet and hands: the donor's deviation from its rest, laid on the target's rest (parents first)
            for enm in EXTRA:
                dq, tq = ex_d[enm], ex_t[enm]
                dev = qmul(wq_d[dq], qinv(dB.wq[dq]))
                want = qmul(dev, tB.wq[tq])
                par = tn[tq]["parent"]
                loc[tq] = qnorm(qmul(qinv(wq_t[par]), want))
                wp_t, wq_t = fk_world(tn, loc, t_over)
            for n, q in loc.items():
                tracks.setdefault((n, 0), []).extend(q)
            for n, v in t_over.items():
                tracks.setdefault((n, 1), []).extend(v)
            if bd:
                bT, bR, bF = bd
                fb = min(int(f * bF / F), bF - 1) if bF != F else f
                mid_d = [(wp_d[dB.footL][k] + wp_d[dB.footR][k]) / 2 for k in range(3)]
                mid_t = [(wp_t[tB.footL][k] + wp_t[tB.footR][k]) / 2 for k in range(3)]
                board_T.extend([mid_t[k] + (bT[fb][k] - mid_d[k]) for k in range(3)])
                board_R.extend(bR[fb])
                # how far his soles are from where hers were, relative to the board: 0 means the same contact
                worst_foot = max(worst_foot, abs((mid_t[1] - (mid_t[1] + bT[fb][1] - mid_d[1])) - (mid_d[1] - bT[fb][1])))
        # tracks must cover every frame for every node that has any
        tracks = {k: v for k, v in tracks.items() if len(v) == F * (4 if k[1] == 0 else 3)}
        out.append((nm, c["fps"], F, tracks))
        if bd:
            out.append((bd_name, c["fps"], F, {(0, 1): board_T, (0, 0): board_R}))
        print("   %-18s %3d frames: %2d bone tracks%s" % (nm, F, len(tracks), ", + its board, re-derived from the target's feet" if bd else ""))
    if a.dry or not out:
        print("dry run -- nothing written" if a.dry else "nothing to write"); return 0
    force = any(lint_name(n) for (n, _f, _F, _t) in out)
    size = append_clips(a.target, out, force=force)
    print("appended %d clips to %s (%.1f MB)" % (len(out), a.target, size / 1e6))
    return 0


if __name__ == "__main__":
    sys.exit(main())
