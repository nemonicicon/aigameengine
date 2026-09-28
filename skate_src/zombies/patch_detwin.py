"""patch_detwin.py -- THE ARMS STOP TWINNING.

Richard Williams, on an arm swing running at a different rate to the feet: the arms
"TWIN unnaturally on the same side as the feet", and the whole flexibility chapter is
about parts NOT arriving together. jacob's note on the skaters was "terrible lol, keep
reiterating" and "the skaters' arms read posed".

⛔ AND THEY WERE A LITERAL MIRROR PAIR. Both arms took the same `armSpread`, the same
`armFwd` and the SAME `elbow` -- the only difference between them was a sign flip on the
spread axis. Every extreme landed on both arms on the same frame. That is twinning in its
purest form, and no amount of tuning the pose library could have fixed it, because the
pose library only has ONE set of arm numbers to give.

Three things, in the order they matter:

  1. THE STANCE SPLIT. A rider's arms are not a mirror pair to begin with. The arm over
     the nose of the board rides higher and further forward; the trailing arm hangs back
     and lower. SK_STANCE already says which way the deck points, so which arm leads is a
     FACT the engine holds, not a guess.
  2. A SIGNATURE PER BODY. Deterministic from the body, so a crew of six reads as six
     people and it is the SAME six every take. Not noise -- noise would flicker.
  3. INDEPENDENT SLOW DRIFT, out of phase between the two arms. Williams' actual point is
     that the extremes landing on the same frame is what reads mechanical; the symmetry
     itself is only the symptom.

⛔ AND THE ARM CODE EXISTED TWICE -- once in `skateApply` for the set's riders and once
inlined in the hero's `g_skate && g_procRig` block. De-twinning one copy would have left
half the crew twinned, exactly the way the mouth aperture survived two fixes because its
shader source was duplicated. Both call one function now.

REVA_TWIN=1 restores the old mirror, so the comparison can be shot.
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


ARMS = '''
/* ⭐ ONE SOURCE FOR THE RIDER'S ARMS. This code existed TWICE -- in skateApply for the
 * set's riders, and inlined in the hero's g_skate block -- so any fix applied to one copy
 * left the other wrong. That is how the mouth aperture survived two fixes.
 *
 * ⛔ AND IT TWINNED. Both arms took the same spread, the same forward and the SAME elbow,
 * differing only by a sign on the spread axis: a mirror pair hitting every extreme on the
 * same frame. Williams calls this out by name, and no tuning of the pose library could
 * have cured it, because the library only carries ONE set of arm numbers.
 *
 *   sig -- the body's signature, 0..1. DETERMINISTIC from the body, so a crew of six is
 *          six people and it is the same six every take. Noise would flicker.
 *   t   -- seconds, for a slow drift that is out of phase between the two arms. */
static void skateArms(Pack*P,const SkPose*z,float Q[][4],float sig,float t){
    float off[4],tq[4];
    static int am=-1, notw=-1;
    if(am<0){ const char*e=getenv("REVA_ARMMODE"); am=e?atoi(e):0; }
    if(notw<0) notw=getenv("REVA_TWIN")?1:0;
    for(int side=0;side<2;side++){
        int up=P->pb[side?10:7], fo=P->pb[side?11:8];
        float sgn=side?-1.f:1.f;
        if(up>=0) memcpy(Q[up],P->nodes[up].q,16);
        if(fo>=0) memcpy(Q[fo],P->nodes[fo].q,16);
        float spread=z->armSpread, fwd=z->armFwd, elb=z->elbow;
        if(!notw){
            /* 1. THE STANCE SPLIT -- which arm is over the nose is a fact the engine holds */
            float lead=(side==((SK_STANCE>0.f)?0:1))?1.f:-1.f;
            spread += lead*0.15f;
            fwd    += lead*0.20f;
            elb    -= lead*0.17f;
            /* 2. THE SIGNATURE */
            float s2=sig*6.2831853f;
            spread += sinf(s2+(float)side*2.1f)*0.10f;
            elb    += cosf(s2*1.7f+(float)side*1.3f)*0.13f;
            /* 3. AND THEY MUST NOT ARRIVE TOGETHER */
            float w=t*0.85f + sig*5.0f + (float)side*1.9f;
            fwd    += sinf(w)*0.10f;
            spread += sinf(w*0.63f+1.1f)*0.075f; }
        if(up>=0){
            /* WHICH axis spreads an arm, and whether the sides need opposite signs, is a
             * property of the rig -- a mirrored rig already reverses the motion and
             * flipping the sign as well throws one arm out. REVA_ARMMODE selects
             * axis(Z,Y,X) x sign(mirror,same) so it can be MEASURED on a new body. */
            float sg=(am&1)?1.f:sgn;
            float ax=0.f,ay=0.f,az=0.f;
            if(am<2){ az=1.f; } else if(am<4){ ay=1.f; } else { ax=1.f; }
            qaxis(off,ax,ay,az,sg*spread);
            qaxis(tq,1,0,0,fwd);
            qmul(off,off,tq); qmul(Q[up],Q[up],off); }
        if(fo>=0){ qaxis(off,1,0,0,elb); qmul(Q[fo],Q[fo],off); } } }
'''

# ---- 1. the shared function, and skateApply delegates to it -------------------------
sub("""static void skateApply(Pack*P,const SkPose*z,float Q[][4],float T[][3],float legLen){""",
    ARMS + """
static void skateApply(Pack*P,const SkPose*z,float Q[][4],float T[][3],float legLen,
                       float sig,float t){""",
    "skateArms + skateApply signature")

sub("""    /* ARMS. Reset first, then spread. WHICH axis spreads an arm, and whether the sides
     * need opposite signs, is a property of the rig -- a mirrored rig already reverses
     * the motion and flipping the sign as well throws one arm out. REVA_ARMMODE selects
     * axis(Z,Y,X) x sign(mirror,same) so it can be MEASURED on a new body. */
    for(int side=0;side<2;side++){
        int up=P->pb[side?10:7], fo=P->pb[side?11:8];
        float sgn=side?-1.f:1.f;
        if(up>=0) memcpy(Q[up],P->nodes[up].q,16);
        if(fo>=0) memcpy(Q[fo],P->nodes[fo].q,16);
        if(up>=0){
            static int am=-1;
            if(am<0){ const char*e=getenv("REVA_ARMMODE"); am=e?atoi(e):0; }
            float sg=(am&1)?1.f:sgn;
            float ax=0.f,ay=0.f,az=0.f;
            if(am<2){ az=1.f; } else if(am<4){ ay=1.f; } else { ax=1.f; }
            qaxis(off,ax,ay,az,sg*z->armSpread);
            qaxis(tq,1,0,0,z->armFwd);
            qmul(off,off,tq); qmul(Q[up],Q[up],off); }
        if(fo>=0){ qaxis(off,1,0,0,z->elbow); qmul(Q[fo],Q[fo],off); } } }""",
    """    skateArms(P,z,Q,sig,t);   /* the one source; see the note on it */ }""",
    "skateApply delegates")

# ---- 2. the actor call site: identity and a clock -----------------------------------
sub("""        skateApply(P,sk,Q,T,legLen); }""",
    """        /* the signature is the BODY's, so the same rider is the same rider every take,
         * and the clock is the skate phase the tick already advances at 30/s */
        skateApply(P,sk,Q,T,legLen,skSig,(float)(phase/30.0)); }""",
    "actorDraw passes signature + clock")

sub("""                      const SkPose*sk,float*footOut,float plantMix,
                      WmsState*wms,float wdt){""",
    """                      const SkPose*sk,float*footOut,float plantMix,
                      WmsState*wms,float wdt,float skSig){""",
    "actorDraw takes a signature")

sub("""                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix,
                  &A->wms,g_actorDt);""",
    """                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix,
                  &A->wms,g_actorDt,
                  /* a stable per-rider signature: the index and the pack, not a clock */
                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f);""",
    "the call site's signature")

# ---- 3. the hero's inlined copy calls the same function -----------------------------
HERO_OLD = """                for(int side=0;side<2;side++){
                    int up=P->pb[side?10:7], fo=P->pb[side?11:8];
                    float sgn=side?-1.f:1.f;"""
i = s.index(HERO_OLD)
j = s.index("if(fo>=0){ qaxis(off,1,0,0,pz.elbow); qmul(Q[fo],Q[fo],off); } }", i)
j = s.index(chr(10), j) + 1
sub(s[i:j], LF.join([
    "                /* the hero rides the SAME de-twinned arms as the crew -- this block",
    "                 * used to be a second copy of them, which is how a fix reaches only",
    "                 * half a cast. sgn/am/the reset all live in skateArms now. */",
    "                skateArms(P,&pz,Q,0.37f,(float)tsec);", ""]),
    "the hero's copy is gone")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
