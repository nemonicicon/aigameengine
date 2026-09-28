"""restclip.py -- give any pack a STATIC pose to be calibrated on.

jacob: "you need a stable test bench" ... "she is dancing while you are trying to
calibrate the clothes" ... "shes also dancing when she should be in a t or a pose"

Only reva2 shipped with a `tpose`; every other body in the club arrives with nothing but
dance clips, so the moment the bench moves to a better model the stable fixture vanishes.
A pack's nodes already carry their bind TRS, so a one-frame clip restating it is a
perfect static fixture -- exact for that body, and nothing to author.

    python skate_src/author/restclip.py just_a_beautiful_girl_animated

⭐ VERIFIED ON A MIXAMO RIG: the beautiful girl comes back in a clean T-pose, arms
level, on pixels.

⛔ AND IT FAILS ON VROID-FAMILY RIGS. On miko it folds head, torso and legs into one
blob while the arms stay correct, and FOUR hypotheses died to renders, none to reasoning:
   1. the glTF SCENE ROOTS were clobbered   -> 137 nodes -> 115 real joints: no change
   2. the TRANSLATION tracks were at fault  -> rotation-only: no change
   3. non-unit joint SCALE was dropped      -> she has none, 0 of 115
   4. the animated NODE SET was wrong       -> copying m_samba's exact 21: no change
What is left is that the engine transforms a clip on the way in -- a canonical retarget
or donor transfer -- which a rest-pose clip survives on a mixamorig skeleton and does not
survive on hers. The next attempt starts by READING the clip path in crowd_kms.c rather
than guessing a fifth time.

Original notes follow.

restclip.py -- give any pack a STATIC pose to be calibrated on.

jacob: "what i was saying is that this animation you are using on reva while trying o
calibrate the clothing is preventing you from being able to do so. you need a stable test
bench, that is reva." ... and later, watching miko dance while I measured a garment on her:
"she is dancing while you are trying to calibrate the clothes"

⭐ HE IS RIGHT TWICE AND THE SECOND TIME WAS MY REPEAT. A garment measured on a body mid-
crouch is unreadable; a T-pose is what showed the first version was a CATSUIT after six
animated benches had not. But `tpose` only existed on reva2 -- every other body in the club
arrives with nothing but dance clips, so the moment the bench moved to a better model the
stable fixture vanished and I went straight back to calibrating on a moving target.

⭐ THE REST POSE IS ALREADY IN EVERY PACK. A pack's nodes carry their bind TRS; a one-frame
clip that simply restates it is a perfect static fixture, needs no authoring, and is exact
for that body rather than a T-pose borrowed from another rig.

    python skate_src/author/restclip.py crowd_miko            # adds `tpose` in place
    python skate_src/author/restclip.py crowd_miko --name rest

⛔ ROTATION ONLY, AND THAT IS NOT AN OVERSIGHT -- IT IS THE WHOLE BUG. The first version
also wrote a TRANSLATION track per joint, on the reasoning that a fully specified local TRS
must be safer than a partial one. It folded miko into a ball: head, torso and legs collapsed
into a single white blob while her arms stayed correct.
⭐ The answer was in her own clips, and it took one line to read: `m_uprock` is
**21 tracks, ALL TYPE 0** -- rotation only, 21 joints. Mine was 230 tracks, 115 rotation and
115 translation. The club's rigs carry bone offsets in the NODE hierarchy and animate purely
by rotation; a translation track re-asserts a bone's offset in a frame the engine does not
expect it in, and the skeleton collapses.
⛔ And a first guess -- that the glTF SCENE ROOTS were being clobbered -- was wrong, tested,
and cost a round: restricting the clip from all 137 nodes to the 115 real joints changed the
render not at all. The joints restriction is kept because it is still correct, but it was
not the fault.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, append_clips                            # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subject", help="packs/v1_<subject>.rpack, edited IN PLACE")
    # ⛔ AUTHORED CLIPS LIVE IN THE `z_` NAMESPACE and rpack.py's lint enforces it --
    # "not z_ prefixed (reserved authored namespace)". The engine's clip miners scan names,
    # so an unprefixed authored clip can be picked up as if it were donor motion. Respect the
    # convention rather than routing around the check that exists to catch this.
    ap.add_argument("--name", default="z_tpose", help="clip name to add (default z_tpose)")
    ap.add_argument("--like", default=None,
                    help="copy the animated-node set from this existing clip (default: the "
                         "pack's first clip). Authoring against a known-good node set beats "
                         "inventing one.")
    ap.add_argument("--force", action="store_true", help="overwrite an existing clip")
    a = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "reva_native", "packs", "v1_%s.rpack" % a.subject)
    R = Rpack(path)
    if a.name in R.clip_names() and not a.force:
        print("%s already has a '%s' clip -- nothing to do (use --force)"
              % (a.subject, a.name))
        return 0

    nodes = R.nodes()

    # ⛔ POSE THE JOINTS, NOT THE SCENE GRAPH. The first version wrote a track for every
    # one of miko's 137 nodes -- which includes `Sketchfab_Scene`, `root` and
    # `GLTF_SceneRootNode`, the glTF scene roots carrying the Y-up conversion and the model's
    # overall SCALE. Writing rotation and translation for those (and no scale) makes the
    # engine rebuild their local matrix without the scale, and the whole body folds into a
    # ball. Her own clips target 21 nodes; mine targeted 137, and that was the difference.
    # ⭐ THE JOINTS ARE EXACTLY THE NODES SOME MESH'S BONE MAP POINTS AT -- no name
    # guessing, no hierarchy walking, and it is right on every rig family.
    # ⭐ AUTHOR AGAINST THE NODE SET THE PACK'S OWN CLIPS USE. Three guesses at why a
    # rest clip folded miko into a ball were all wrong -- the glTF scene roots (restricting
    # to real joints changed nothing), translation tracks (rotation-only changed nothing),
    # and non-unit joint scales (there are none). The one difference left in the data was
    # COUNT: her working clips animate 21 nodes and mine animated 115, the extra 94 being
    # hair chains, fingers, breast bones and cat ears -- which the engine drives by its own
    # systems. So copy a known-good clip's node set instead of inventing one.
    src_clip = a.like or (R.clip_names()[0] if R.clip_names() else None)
    joints = set()
    if src_clip and src_clip in R.clip_names():
        joints = {t[0] for t in R.tracks(R.clip_names().index(src_clip))}
        print("posing the %d nodes that '%s' animates" % (len(joints), src_clip))
    if not joints:
        import struct as _st
        for mi in range(R.nMeshes):
            rec = R.MESH.unpack_from(R.buf, R.offMeshes + mi * 60)
            nb, offBM = rec[4], rec[12]
            if nb:
                joints.update(_st.unpack_from("<%dH" % nb, R.buf, offBM))
        print("no clip to copy from; posing %d bone-map joints" % len(joints))
    if not joints:
        print("!! nothing to pose")
        return 1

    tracks = {}
    for i in sorted(joints):
        if i >= len(nodes):
            continue
        q, t = nodes[i]["q"], nodes[i]["t"]
        tracks[(i, 0)] = [q[0], q[1], q[2], q[3]]      # rotation, one frame
        _ = t                                          # translation deliberately NOT written
    append_clips(path, [(a.name, 30.0, 1, tracks)], force=a.force)
    R2 = Rpack(path)
    print("%s: added '%s' (1 frame). clips now: %s"
          % (a.subject, a.name, ", ".join(R2.clip_names())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
