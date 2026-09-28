"""sliceclip.py -- cut a frame range out of one clip into a new one, and rename the source.

jacob (09-01): "do you know kaeli's is in a crouch to standing animation as her base idle
animation?"

Yes -- and it was measured before it was believed. kaeli's `idle` (v1_kmoves, 184 frames)
is a GET-UP: frames 0-80 are a crouch (left thigh 120 deg and both knees ~100 deg off her
rest pose), 80-100 she rises, and from ~100 on she is standing (thigh 18-46 deg, knees
10-25 deg -- the same numbers as her `w_walk`). Its last frame is 111 deg away from its
first, so it does not loop, and every election that mines the word "idle" -- the pack's
idleIdx, the hero's foot idle, the donor bank's exact-name idle -- picked it and played it
on a loop: stand up, snap to the floor, stand up, for the whole of every scene.
compile_scene.py already knew ("kaeli's own idle is a GET-UP-OFF-THE-FLOOR loop") and
dodged it with role casting; the other three elections never heard.

The fix is in the asset, not the engine's opinion of it:

    python skate_src/author/sliceclip.py kmoves idle idle --from 110 --to 183 --pingpong \
        --rename-src x_getup

  * the STANDING tail (110..183) becomes the new `idle`, ping-ponged (forward then back,
    endpoints not doubled) so the loop closes on itself with no snap;
  * the get-up is RENAMED `x_getup` -- the crouch is a real performance and stays in the
    pack, under a name no idle miner will elect.

Append-and-repoint like everything in rpack.py: the new clip's track bytes go at the end,
the string table is rebuilt with the same entries in the same order (a rename changes ONE
entry's bytes and no index), so nothing that already pointed into the pack moves.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpack import Rpack, HDR, append_clips  # noqa: E402


def rename_clip(path, old, new):
    """Rename a clip by rewriting its string-table entry in place (same index)."""
    p = Rpack(path)
    strs = p.strings()
    names = p.clip_names()
    if old not in names:
        raise SystemExit("!! no clip '%s' in %s" % (old, path))
    if new in names:
        raise SystemExit("!! '%s' already exists in %s" % (new, path))
    ci = names.index(old)
    nameId = p.clip(ci)["nameId"]
    strs[nameId] = new.encode()
    out = bytearray(p.buf)
    while len(out) % 4:
        out.append(0)
    tbl = struct.pack("<I", len(strs)); so = 0
    for s in strs:
        tbl += struct.pack("<II", so, len(s)); so += len(s)
    tbl += b"".join(strs)
    newOffStr = len(out); out.extend(tbl)
    h = list(HDR.unpack_from(out, 4))
    h[11] = newOffStr
    h[13] = len(out)
    HDR.pack_into(out, 4, *h)
    open(path, "wb").write(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subject", help="packs/v1_<subject>.rpack, edited IN PLACE")
    ap.add_argument("src", help="clip to cut from")
    ap.add_argument("new", help="name of the new clip")
    ap.add_argument("--from", dest="f0", type=int, required=True, help="first frame (incl.)")
    ap.add_argument("--to", dest="f1", type=int, required=True, help="last frame (incl.)")
    ap.add_argument("--pingpong", action="store_true",
                    help="append the range reversed so the clip loops without a snap")
    ap.add_argument("--rename-src", default=None,
                    help="rename the source clip afterwards (e.g. x_getup)")
    ap.add_argument("--force", action="store_true", help="skip the name lint")
    a = ap.parse_args(argv)

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = os.path.join(root, "reva_native", "packs", "v1_%s.rpack" % a.subject)
    R = Rpack(path)
    names = R.clip_names()
    if a.src not in names:
        print("!! %s has no clip '%s'. clips: %s" % (a.subject, a.src, ", ".join(names)))
        return 1
    ci = names.index(a.src)
    c = R.clip(ci)
    F = c["frames"]
    if not (0 <= a.f0 <= a.f1 < F):
        print("!! range %d..%d outside 0..%d" % (a.f0, a.f1, F - 1))
        return 1
    n = a.f1 - a.f0 + 1
    order = list(range(a.f0, a.f1 + 1))
    if a.pingpong:
        order += list(range(a.f1 - 1, a.f0, -1))     # back down, endpoints not doubled
    tracks = {}
    for node, typ, data in R.tracks(ci):
        comp = 4 if typ == 0 else (1 if typ == 3 else 3)
        fb = comp * 4
        tracks[(node, typ)] = b"".join(data[f * fb:(f + 1) * fb] for f in order)

    # the new name may DELIBERATELY be one the miners elect ("idle" is the point here);
    # say so and go, unless the caller wants the lint enforced.
    if a.new == a.src:
        # cutting a clip into its own name: rename the original out of the way first
        if not a.rename_src:
            print("!! new name equals the source; give --rename-src so the source survives")
            return 1
        rename_clip(path, a.src, a.rename_src)
        print("%s: '%s' -> '%s'" % (a.subject, a.src, a.rename_src))
        append_clips(path, [(a.new, c["fps"], len(order), tracks)], force=True)
    else:
        append_clips(path, [(a.new, c["fps"], len(order), tracks)], force=True)
        if a.rename_src:
            rename_clip(path, a.src, a.rename_src)
            print("%s: '%s' -> '%s'" % (a.subject, a.src, a.rename_src))
    R2 = Rpack(path)
    print("%s: added '%s' = %s[%d..%d]%s, %d frames @ %g fps, %d tracks. clips now: %s"
          % (a.subject, a.new, a.src, a.f0, a.f1, " ping-pong" if a.pingpong else "",
             len(order), c["fps"], len(tracks), ", ".join(R2.clip_names())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
