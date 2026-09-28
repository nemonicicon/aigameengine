"""patch_partnerdraw.py -- A BORROWED CLIP IS STILL A CLIP.

⛔ THE PARTNER WAS NOT DRAWN IN ANY SCENE, and it hid behind a line of ctl that was
accidentally load-bearing.

The per-line staging resolves her clip in two ways. If her own pack has it, it sets
`g_labPClip`. If it does not -- which is the NORMAL case, because `x_convo1`/`x_convo2`
live in the moves bank and not in kmoves -- it borrows through the canonical transfer and
sets `g_labPDnP`/`g_labPDnC` instead, leaving `g_labPClip` at -1.

And both the call gate and labDrawPartner's own guard test ONLY `clip>=0`:

    if(g_mode==MODE_ANIM && g_labPartner && g_labPClip>=0 && skSolid)   <-- fails
    if(!P||clip<0|| ...) return 0;                                       <-- fails

So a borrowed clip meant no partner. It was invisible until now because driver.py wrote
`pclip=idle` into every scene ctl: that put kmoves' own idle index into `g_labPClip`, the
gate passed, and the donor override inside then did the real posing. The moment that ctl
line went away -- for the good reason that a scene owns its own clips -- the partner
vanished from every take.

⭐ AND IT CASCADES, which is why the film looked broken in a way that had nothing to do
with clips: her head mark is published BY THAT DRAW, so `g_partHeadOK` stayed 0, and the
conversation camera is gated on BOTH marks. Every scene therefore fell through to a static
wide and not one over-the-shoulder, tight or two-shot in the entire film ever fired.

Both tests now accept a borrowed clip.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


sub("""    if(!P||clip<0||(unsigned)clip>=P->nClips||P->nNodes>1024) return 0;""",
    """    /* ⛔ A BORROWED CLIP IS STILL A CLIP. clip is -1 whenever the staging resolved her
     * line out of the BANK rather than her own pack -- which is the normal case -- and
     * refusing to draw on that basis made the partner absent from every scene in the
     * film. Her head mark is published below, so her absence also silently disabled the
     * conversation camera, which is gated on both marks. */
    int borrowed9=(g_labPDnP&&g_labPDnC>=0&&(unsigned)g_labPDnC<g_labPDnP->nClips);
    if(!P||P->nNodes>1024) return 0;
    if(!borrowed9&&(clip<0||(unsigned)clip>=P->nClips)) return 0;""",
    "labDrawPartner accepts a borrowed clip")

sub("""            if(g_mode==MODE_ANIM&&g_labPartner&&g_labPClip>=0&&skSolid)
                labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,mModel,mPal,mBind,pal,labDq,mDqS);""",
    """            if(g_mode==MODE_ANIM&&g_labPartner&&(g_labPClip>=0||g_labPDnP)&&skSolid)
                labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,mModel,mPal,mBind,pal,labDq,mDqS);""",
    "the call gate accepts a borrowed clip")

sub("""                        labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,mModel,mPal,mBind,pal,labDq,mDqS); }""",
    """                        labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,mModel,mPal,mBind,pal,labDq,mDqS); }""",
    "second call site unchanged (its own gate is upstream)")

# and the clip sampler must not index a negative clip when borrowed -- scoped to
# labDrawPartner, because that exact line appears four times in the file
_i = s.index("static int labDrawPartner(")
_j = s.index("PClip*c=&P->clips[clip];", _i)
s = (s[:_j]
     + "PClip*c=&P->clips[(clip>=0&&(unsigned)clip<P->nClips)?clip:0];"
       "   /* borrowed: index 0 is only a placeholder -- donorPoseF overwrites the pose */"
     + s[_j + len("PClip*c=&P->clips[clip];"):])
print("   ok: guard the clip index (in labDrawPartner only)")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
