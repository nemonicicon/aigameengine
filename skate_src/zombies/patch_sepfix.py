"""patch_sepfix.py -- measure the separation floor from BOTH bodies properly.

The first cut reported:

    [stage] separation floor 0.58 world units (shoulders 0.37 + 0.11, margin 0.34)

Two things wrong with that, and the instrument is what showed them.

⛔ 0.11 IS NOT A SHOULDER SPAN. The hero's was measured through FK world matrices; the
partner's was taken from her clavicles' LOCAL rest translations, which are offsets inside
the chest's frame and are not a span at all. She is not narrow -- she was mismeasured. Her
world matrices already exist inside labDrawPartner (the FK has just run to draw her), so
the measurement belongs there, where it is exact and free.

⛔ AND CLAVICLES ARE THE WRONG BONES. 0.37 world units is 0.21 m at club scale -- half a
shoulder width -- because a clavicle starts near the sternum, not at the shoulder. The
canon map's upperarm bones (7 and 10) ARE the shoulder joints, which is the width two
people actually have to keep between them.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
CR, LF = chr(13), chr(10)
CRLF = CR + LF
s = io.open(P, encoding="utf-8", newline="").read()
WAS_CRLF = CRLF in s
if WAS_CRLF:
    s = s.replace(CRLF, LF)
orig = s
BS_N = chr(92) + "n"


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- globals: each body reports its own width from its own FK ----
sub("""static float g_stageMinSep=0.f;    /* MEASURED floor on how close the scene may stage them */""",
    """static float g_stageMinSep=0.f;    /* MEASURED floor on how close the scene may stage them */
/* Each body publishes its own SHOULDER WIDTH from its own FK, in world units. Measured
 * across the upperarm bones (canon 7 and 10) -- the clavicles sit near the sternum and
 * under-report a span by half. */
static float g_heroShoulderW=0.f, g_partShoulderW=0.f;""",
    "per-body shoulder width globals")

# ---- the partner measures herself where her FK already exists ----
sub("""    scnHeadLayer(P,Q,1);
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    chainSim(P,W,1);   /* H1: the partner's chains swing too */""",
    """    scnHeadLayer(P,Q,1);
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    /* HER WIDTH, from the FK that has just run. Doing it here rather than off her rest
     * node translations is the difference between 0.37 and a bogus 0.11. */
    if(g_partShoulderW<=0.f&&P->pb[7]>=0&&P->pb[10]>=0){
        float*a7=W[P->pb[7]],*b7=W[P->pb[10]];
        float dx=a7[12]-b7[12], dy=a7[13]-b7[13], dz=a7[14]-b7[14];
        g_partShoulderW=sqrtf(dx*dx+dy*dy+dz*dz)*P->scale; }
    chainSim(P,W,1);   /* H1: the partner's chains swing too */""",
    "the partner measures herself from FK")

# ---- the hero, and the combination ----
old_hero = s[s.index("                /* THE SEPARATION FLOOR, from the two bodies"):]
old_hero = old_hero[:old_hero.index("fflush(stdout); }") + len("fflush(stdout); }")]
sub(old_hero,
    """                /* THE SEPARATION FLOOR, from the two bodies rather than from taste:
                 * half a shoulder each, plus a hand's margin so sleeves do not
                 * interpenetrate. Across the UPPERARM bones, which are the shoulder
                 * joints; the clavicles sit near the sternum and under-report by half. */
                if(g_heroShoulderW<=0.f&&P->pb[7]>=0&&P->pb[10]>=0){
                    float*a7=world[P->pb[7]],*b7=world[P->pb[10]];
                    float dx=a7[12]-b7[12], dy=a7[13]-b7[13], dz=a7[14]-b7[14];
                    g_heroShoulderW=sqrtf(dx*dx+dy*dy+dz*dz)*P->scale; }
                if(g_stageMinSep<=0.f&&g_heroShoulderW>0.f&&g_partShoulderW>0.f){
                    g_stageMinSep=(g_heroShoulderW+g_partShoulderW)*0.5f+0.34f;
                    printf("[stage] separation floor %.2f world units "
                           "(shoulders %.2f + %.2f, margin 0.34)@N",
                           g_stageMinSep,g_heroShoulderW,g_partShoulderW);
                    fflush(stdout); }""".replace("@N", BS_N),
    "the hero measures herself, and the floor combines the two")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
