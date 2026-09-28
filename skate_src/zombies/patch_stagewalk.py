"""patch_stagewalk.py -- SHE HAS TO WALK, AND THEY MUST NOT PASS THROUGH EACH OTHER.

jacob, on the first take of `denial`: "already i see characters clipping through each
othere, very unantural sliding movement from kaeli instead of taking steps... needs work
and you know it." Both are real and both are mine.

⛔ THE SLIDE IS BUILT IN. Per-line staging eases g_labPGap / g_labPYaw / g_labPSide toward
each line's marks with a 0.42s time constant, and the partner's model matrix is built
straight off those three numbers. So when a line moves her from gap 1.85 to 1.60 her body
TRANSLATES half a metre over four tenths of a second while playing a STANDING IDLE. That
is not a bug in the ease; it is the ease with no locomotion attached. It never showed on
`the_spot`'s black stage because there is no ground texture to slide against.

  THE FIX: measure her actual world speed from the ease each frame, and when it exceeds a
  threshold, play a WALK instead of the staged clip -- her own if she has one, otherwise
  borrowed off a bank through the same canonical transfer the zombies use -- at a phase
  rate from gaitRate(), which is the club's existing "how fast does this clip's planted
  foot travel" measurement. Below the threshold she goes back to the staged clip. Nothing
  about the marks changes; she just walks to them now.

⛔ AND NOTHING STOPPED THEM OVERLAPPING. A scene could name any pgap it liked -- `denial`
asks for 0.95 in its tight -- and two bodies at 0.95 with a lateral offset of 0.25 are
inside one another. There was no floor because the LAB path deliberately has none: the
authored contact moves (z_hug, z_kiss at a negotiated 0.573) need to get closer than any
conversational minimum. So the clamp goes on the SCENE staging path only, and its floor is
MEASURED from the two bodies' shoulders rather than picked.
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


# ---------------------------------------------------- 1. globals
a = """static float g_labPGap=1.60f;                  /* how far in front of the hero she stands, WORLD units */"""
b = """/* Z7: the partner's own locomotion. She is staged by marks that MOVE between lines, and
 * a body that changes place without a gait is a body sliding on ice. */
static float g_partSpd=0.f;        /* her world speed this frame, from the staging ease */
static Pack *g_partWalkP=NULL;     /* whose walk she borrows (her own pack, or a bank) */
static int   g_partWalkC=-1;
static double g_partWalkPh=0.0;
static float g_partWalkMix=0.f;    /* 0 = staged clip, 1 = walking; eased so it never pops */
static float g_stageMinSep=0.f;    /* MEASURED floor on how close the scene may stage them */
static float g_labPGap=1.60f;                  /* how far in front of the hero she stands, WORLD units */"""
sub(a, b, "partner locomotion globals")

# ---------------------------------------------------- 2. measure speed + clamp, in the ease
a = """                        { float st8=(sfps>0.f)?(1.f/sfps):dtf;
                          float k8=1.f-expf(-st8/0.42f);
                          g_labPGap += (S8->pgap -g_labPGap )*k8;
                          g_labPYaw += (S8->pyaw -g_labPYaw )*k8;
                          g_labPSide+= (S8->pside-g_labPSide)*k8; } } }"""
b = """                        { float st8=(sfps>0.f)?(1.f/sfps):dtf;
                          float k8=1.f-expf(-st8/0.42f);
                          float g0=g_labPGap, s0=g_labPSide;
                          /* ⛔ A FLOOR ON THE MARKS, measured not chosen. Two bodies at the
                           * radius the script asks for can be inside one another -- and the
                           * LAB path must keep no floor at all, because the authored contact
                           * moves have to get closer than any conversational minimum. So the
                           * clamp lives here, on the scene staging path only. */
                          float tg=S8->pgap, ts=S8->pside;
                          if(g_stageMinSep>0.f){
                              float r=sqrtf(tg*tg+ts*ts);
                              if(r<g_stageMinSep&&r>1e-4f){ float f=g_stageMinSep/r; tg*=f; ts*=f; }
                              else if(r<=1e-4f) tg=g_stageMinSep; }
                          g_labPGap += (tg      -g_labPGap )*k8;
                          g_labPYaw += (S8->pyaw-g_labPYaw )*k8;
                          g_labPSide+= (ts      -g_labPSide)*k8;
                          /* HER SPEED, from how far the marks actually moved her. */
                          if(st8>1e-5f){
                              float dx=g_labPGap-g0, dz=g_labPSide-s0;
                              float v=sqrtf(dx*dx+dz*dz)/st8;
                              g_partSpd += (v-g_partSpd)*(1.f-expf(-st8/0.12f)); }
                        } } }"""
sub(a, b, "clamp the marks and measure her speed")

# ---------------------------------------------------- 3. the walk, in labDrawPartner
a = """    scnHeadLayer(P,Q,1);
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    chainSim(P,W,1);   /* H1: the partner's chains swing too */"""
b = """    /* ⭐ SHE WALKS TO HER MARK. Above this the staged clip has already been sampled into
     * T/Q/S; if the staging is currently MOVING her, overwrite it with a walk whose phase
     * runs at gaitRate() for her measured speed. Mixed in and out over ~0.18s so the
     * hand-off never pops, and taken from her own pack when she has a walk, otherwise off
     * a bank through the same canonical transfer the zombie clips use. */
    if(g_scnT>=0.f){
        const float WALK_ON=0.22f;                    /* world units/sec worth walking for */
        float want=(g_partSpd>WALK_ON)?1.f:0.f;
        float stp=(float)((g_fixedStepMs>0.0)?g_fixedStepMs*0.001:0.0166);
        g_partWalkMix += (want-g_partWalkMix)*(1.f-expf(-stp/0.18f));
        if(g_partWalkMix>0.02f){
            if(g_partWalkP!=P){                        /* resolve her walk once per body */
                g_partWalkP=NULL; g_partWalkC=-1;
                for(unsigned c=0;c<P->nClips;c++){ const char*nm=packStr(P,P->clips[c].name);
                    if(!strcmp(nm,"w_walk")||!strcmp(nm,"walking")||!strcmp(nm,"v_walk1")){
                        g_partWalkP=P; g_partWalkC=(int)c; break; } }
                if(g_partWalkC<0&&P->cbOK&&P->pbOK>=10){
                    int bc=-1; Pack*bp=bankFind("w_walk",&bc);
                    if(bp){ g_partWalkP=bp; g_partWalkC=bc; } }
                if(g_partWalkC<0) g_partWalkP=P;        /* nothing to borrow: leave her be */ }
            if(g_partWalkC>=0&&g_partWalkP){
                PClip*wc=&g_partWalkP->clips[g_partWalkC];
                g_partWalkPh += stp*wc->fps*gaitRate(g_partWalkP,g_partWalkC,g_partSpd);
                static float WQ[1024][4], WT[1024][3], WS[1024][3];
                for(unsigned i=0;i<P->nNodes;i++){
                    memcpy(WQ[i],P->nodes[i].q,16); memcpy(WT[i],P->nodes[i].t,12);
                    memcpy(WS[i],P->nodes[i].s,12); }
                if(g_partWalkP==P) clipSampleF(P,wc,g_partWalkPh,WT,WQ,WS);
                else               donorPoseF(g_partWalkP,g_partWalkC,g_partWalkPh,P,WQ);
                float m=g_partWalkMix;
                for(unsigned i=0;i<P->nNodes;i++) qnlerp(Q[i],Q[i],WQ[i],m); } }
        else { g_partWalkPh=0.0; } }
    scnHeadLayer(P,Q,1);
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    chainSim(P,W,1);   /* H1: the partner's chains swing too */"""
sub(a, b, "the partner walks when the staging moves her")

# ---------------------------------------------------- 4. measure the separation floor
a = """                g_heroIOD=sqrtf((el[0]-er[0])*(el[0]-er[0])+(el[1]-er[1])*(el[1]-er[1])+(el[2]-er[2])*(el[2]-er[2]));"""
b = """                g_heroIOD=sqrtf((el[0]-er[0])*(el[0]-er[0])+(el[1]-er[1])*(el[1]-er[1])+(el[2]-er[2])*(el[2]-er[2]));
                /* THE SEPARATION FLOOR, from the two bodies rather than from taste: half
                 * a shoulder span each (clavicle to clavicle through the canon map), plus
                 * a hand's margin so sleeves do not interpenetrate either. Computed once,
                 * when both bodies are known. */
                if(g_stageMinSep<=0.f&&g_labPartner){
                    float w1=0.f,w2=0.f;
                    if(P->pb[6]>=0&&P->pb[9]>=0){
                        float*a1=world[P->pb[6]],*b1=world[P->pb[9]];
                        w1=sqrtf((a1[12]-b1[12])*(a1[12]-b1[12])+(a1[14]-b1[14])*(a1[14]-b1[14])
                                 +(a1[13]-b1[13])*(a1[13]-b1[13]))*P->scale; }
                    Pack*Q2=g_labPartner;
                    if(Q2->pb[6]>=0&&Q2->pb[9]>=0){
                        /* her rest shoulders are enough: a shoulder span does not animate */
                        float ax=Q2->nodes[Q2->pb[6]].t[0]-Q2->nodes[Q2->pb[9]].t[0];
                        float az=Q2->nodes[Q2->pb[6]].t[2]-Q2->nodes[Q2->pb[9]].t[2];
                        w2=sqrtf(ax*ax+az*az)*Q2->scale; }
                    if(w1<0.05f) w1=0.42f; if(w2<0.05f) w2=0.42f;
                    g_stageMinSep=(w1+w2)*0.5f+0.34f;
                    printf("[stage] separation floor %.2f world units "
                           "(shoulders %.2f + %.2f, margin 0.34)@N",g_stageMinSep,w1,w2);
                    fflush(stdout); }""".replace("@N", BS_N)
sub(a, b, "measure the separation floor from both bodies")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
