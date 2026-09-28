"""patch_hipdrop.py -- carry a borrowed clip's HIP HEIGHT across the retarget.

THE DEFECT, seen on the first zombiecheck sheet. donorPoseF is rotation-only, so a clip
that puts a body on the ground arrives correctly ORIENTED and still suspended at standing
hip height: z_lay_a, z_imp_a, z_burst_a and the two ground attacks all came back lying
flat in mid-air, and the belly crawls came back standing up. Measured on the source GLBs
before the bake: pelvis y is 0.84 standing, 0.82-0.92 walking, and 0.11 on the crawls.
That number is the whole difference between a corpse and a man doing a strange dance, and
it lives in a TRANSLATION track the transfer never reads.

THE FIX. Read the donor's pelvis translation at the current phase, take its offset from
the donor's own rest, normalise by the two rigs' rest hip heights (a citizen is not a
mannequin's size) and drop the body by it. Rest heights are measured by walking the rest
hierarchy, so nothing here is tuned -- it is the same measure-then-replant shape the skate
crouch uses, and for the same reason: the alternative is a magic number per clip.
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


# ------------------------------------------------- 1. the helpers, after donorPose()
a = """/* integer wrapper: the audits keep exact baked frames (frac 0 == the old memcpy) */
static void donorPose(Pack*dp,int clip,unsigned frame,Pack*tp,float(*Q)[4]){
    donorPoseF(dp,clip,(double)frame,tp,Q); }"""
b = a + """
/* ================= Z4 THE HIP DROP =================================================
 * donorPoseF above is ROTATION-ONLY, which is right for a body that stays on its feet
 * and wrong for every clip that puts one on the ground: the pose arrives correctly
 * oriented and still suspended at standing hip height. The first zombie sheet showed it
 * exactly -- z_lay_a, z_imp_a, z_burst_a and the two ground attacks lying flat in the
 * air, the belly crawls standing up. The height lives in the pelvis TRANSLATION track
 * (measured on the source: pelvis y 0.84 standing, 0.11 on a crawl) and nothing read it.
 *
 * restBoneY walks a bone's rest chain and returns its world height, so the two rigs can
 * be compared without a tuned constant: a citizen is not the size of a mannequin, and
 * the ratio of their rest hips is exactly the conversion. */
static float restBoneY(Pack*P,int bone){
    if(!P||bone<0||(unsigned)bone>=P->nNodes) return 0.f;
    int chain[64],n=0,b=bone;
    while(b>=0&&n<64){ chain[n++]=b; b=P->nodes[b].parent; }
    M4 acc; float t0[3],q0[4],s0[3];
    memcpy(t0,P->nodes[chain[n-1]].t,12); memcpy(q0,P->nodes[chain[n-1]].q,16);
    memcpy(s0,P->nodes[chain[n-1]].s,12);
    m4_trs(acc,t0,q0,s0);
    for(int i=n-2;i>=0;i--){ M4 loc,tmp; float tt[3],qq[4],ss[3];
        memcpy(tt,P->nodes[chain[i]].t,12); memcpy(qq,P->nodes[chain[i]].q,16);
        memcpy(ss,P->nodes[chain[i]].s,12);
        m4_trs(loc,tt,qq,ss); m4_mul(tmp,acc,loc); memcpy(acc,tmp,64); }
    return acc[13]; }
/* How far this clip drops the donor's hips below its own rest, expressed in the TARGET's
 * rig units. Zero for anything that stays upright, so an upright clip costs nothing. */
static float donorHipDrop(Pack*dp,int clip,double ph,Pack*tp){
    if(!dp||!tp||clip<0||(unsigned)clip>=dp->nClips) return 0.f;
    int db=dp->pb[0], tb=tp->pb[0];
    if(db<0||tb<0) return 0.f;
    PClip*c=&dp->clips[clip]; if(!c->frames) return 0.f;
    double fp=fmod(ph,(double)c->frames); if(fp<0) fp+=c->frames;
    unsigned f0=(unsigned)fp, f1=(f0+1)%c->frames; float ft=(float)(fp-(double)f0);
    unsigned char*tr=dp->pk+c->offTracks;
    for(unsigned t=0;t<c->nTracks;t++){
        unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(type!=1||node!=(unsigned)db) continue;            /* type 1 = translation */
        float*d=(float*)(dp->pk+offD);
        float y=d[(size_t)f0*3+1]+(d[(size_t)f1*3+1]-d[(size_t)f0*3+1])*ft;
        float drop=y-dp->nodes[db].t[1];                     /* below its own rest */
        if(drop>-1e-4f) return 0.f;                          /* upright: nothing to pay */
        float dR=restBoneY(dp,db), tR=restBoneY(tp,tb);
        if(dR<1e-4f) return 0.f;
        return drop*(tR/dR); }
    return 0.f; }"""
sub(a, b, "restBoneY + donorHipDrop")

# ------------------------------------------------- 2. apply it to the actor's matrix
a = """        m[12]=px; m[13]=A->y-P->mny*eff+A->air; m[14]=pz;"""
b = ("""        /* Z4: a borrowed clip that goes to the ground takes the body down with it. */
        float hipDrop=(A->dnP&&A->dnC>=0)?donorHipDrop(A->dnP,A->dnC,A->phase,P):0.f;
        m[12]=px; m[13]=A->y-P->mny*eff+A->air+hipDrop*eff; m[14]=pz;""")
sub(a, b, "the actor's model matrix takes the drop")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
