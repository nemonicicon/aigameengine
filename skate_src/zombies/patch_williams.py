"""patch_williams.py -- THE WILLIAMS LAYER.

jacob handed over Richard Williams' "The Animator's Survival Kit" with no other words.
Taking that as the note it plainly is: our bodies move like they are on rails, and the
book says exactly why and exactly what to do about it.

Three things out of it, applied as a procedural overlay on top of whatever clip is
playing, so they reach EVERY body in the club -- authored clips, borrowed clips,
retargeted zombie shambles, the skaters -- without re-authoring one frame of animation:

  1. THE UP AND DOWN (the walks chapter). Williams is blunt that a traced live-action
     walk FLOATS, and that the cure is to exaggerate the ups and downs, because it is
     the up-and-down of the masses that reads as weight. Our case is worse than tracing:
     donorPoseF is ROTATION-ONLY, so a retargeted walk arrives with the pelvis height
     DELETED. Every citizen in the film has been gliding along a rail.
     He puts the body DOWN just after the contact and UP just after the passing position.

  2. THE BELT LINE (same chapter). It tilts back and forth favouring the leg that is
     lowest -- down with the foot that is down.

  3. OVERLAPPING ACTION / BREAKING JOINTS (the flexibility chapter). Parts do not start
     and stop together: the head arrives late, and when an arm swings, the hand keeps on
     going and the elbow breaks. That one is NOT gated on walking -- a head turn drags
     whether or not the feet are moving, which is why our idles read dead.

⭐ AND THE PHASE COMES FROM THE FEET, NOT FROM THE CLOCK. Williams defines a walk by its
CONTACT and PASSING positions, and those are FOOT positions. Measuring the feet after FK
gives the phase for any clip, on any rig, borrowed or authored, at any playback rate,
with nothing to keep in sync and no per-clip metadata to author. Feet apart = contact =
body down; feet together = passing = body up.

⛔ AND A STANDING BODY MUST GET NOTHING. The bounce amplitude is driven by how fast the
feet are actually swapping, so an idle does not bob. One term, no state machine, and it
is correct for a clip nobody labelled -- which is most of the 105 citizens.

⛔ RIG SPACE IS NOT BONE SPACE. A correction post-multiplied onto a bone's local quat
turns about that BONE's axes, which on a UE rig point down the bone and have nothing to
do with the character's forward. Every axis here is therefore expressed in the bone's own
frame first (wmsLocalAxis) -- otherwise the belt line rolls about whatever direction the
pelvis bone happens to have been drawn along.

Ken Harris' note is in here too: understate it. The dial defaults below full, ctl `wms=`
moves it, REVA_NOWMS=1 kills it.
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


NL = chr(92) + "n"   # a literal backslash-n for C printf

# ---------------------------------------------------------------- 1. the layer itself
LAYER = '''
/* ======================== THE WILLIAMS LAYER ====================================
 * Richard Williams, "The Animator's Survival Kit": the walks chapter (weight, the
 * belt line) and the flexibility chapter (overlapping action, breaking joints),
 * applied as an overlay ON TOP of whatever clip is playing -- so it reaches authored
 * clips, borrowed clips and retargeted shambles alike without re-authoring a frame.
 *
 * ⭐ THE PHASE IS MEASURED OFF THE FEET, NOT THE CLOCK. Williams defines a walk by its
 * CONTACT and PASSING positions, and both are foot positions -- so reading the feet
 * after FK gives the phase for any clip on any rig at any playback rate, with nothing
 * to keep in sync and no per-clip metadata to author:
 *      feet apart    = contact  -> the body goes DOWN
 *      feet together = passing  -> the body goes UP
 * ...each after a short lag, because he is explicit that the weight goes down JUST
 * AFTER the contact and up JUST AFTER the passing position.
 *
 * ⭐ WHY THIS IS NEEDED AT ALL: donorPoseF is ROTATION-ONLY, so every retargeted walk
 * in the club arrives with its pelvis HEIGHT deleted. Williams says a traced walk
 * floats even WITH its translation; ours had none. That is the rail every citizen in
 * the film has been gliding along. */
typedef struct {
    int   init;
    float u;          /* lagged contact/passing measure: 0 = passing, 1 = contact */
    float act;        /* how hard the feet are actually swapping; 0 = standing still */
    float prevSep;
    float lq[3][4];   /* the smoothed local quats the dragged parts chase */
} WmsState;

static float g_wms=0.85f;   /* ctl wms= -- Ken Harris' note was to understate it */
static int   g_wmsOff=0;    /* REVA_NOWMS=1 */

/* A rig-space direction expressed in a bone's OWN frame. A correction post-multiplied
 * onto a local quat turns about that bone's axes, and on a UE rig those point down the
 * bone -- nothing to do with the character's forward. This is what makes the belt line
 * roll about the direction of travel on every rig instead of one of them. */
static void wmsLocalAxis(const float*Wb,float wx,float wy,float wz,float*out){
    out[0]=Wb[0]*wx+Wb[1]*wy+Wb[2]*wz;
    out[1]=Wb[4]*wx+Wb[5]*wy+Wb[6]*wz;
    out[2]=Wb[8]*wx+Wb[9]*wy+Wb[10]*wz;
    float n=sqrtf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]);
    if(n>1e-6f){ out[0]/=n; out[1]/=n; out[2]/=n; }
    else { out[0]=out[1]=0.f; out[2]=1.f; } }

static void wmsApply(Pack*P,float T[][3],float Q[][4],M4*W,WmsState*st,float dt,float amt){
    if(!st||g_wmsOff||amt<=0.002f||!P||P->pb[0]<0) return;
    if(dt<0.f) dt=0.f; if(dt>0.10f) dt=0.10f;
    int hp=P->pb[0];

    /* ---- OVERLAPPING ACTION. Not gated on walking: Williams' bulldog drags its jowls
     * when it TURNS ITS HEAD, and our idles read dead for exactly this reason. The head
     * arrives late; the forearm arrives late, which is the hand keeping on going and the
     * elbow breaking. A one-pole chase IS a phase delay, which is what drag is. */
    static const int DRAG[3]={5,8,11};              /* head, lowerarm_l, lowerarm_r */
    static const float DK[3]={0.50f,0.38f,0.38f};
    static const float DT_[3]={0.085f,0.065f,0.065f};
    for(int k=0;k<3;k++){
        int b=(DRAG[k]<NCANON)?P->pb[DRAG[k]]:-1; if(b<0) continue;
        if(!st->init) memcpy(st->lq[k],Q[b],16);
        float lag[4]; memcpy(lag,st->lq[k],16);
        /* chase the live value... */
        float kk=1.f-expf(-dt/DT_[k]);
        float d=lag[0]*Q[b][0]+lag[1]*Q[b][1]+lag[2]*Q[b][2]+lag[3]*Q[b][3];
        float sgn=(d<0.f)?-1.f:1.f;
        for(int j=0;j<4;j++) lag[j]+=(Q[b][j]*sgn-lag[j])*kk;
        float n=sqrtf(lag[0]*lag[0]+lag[1]*lag[1]+lag[2]*lag[2]+lag[3]*lag[3]);
        if(n>1e-6f){ for(int j=0;j<4;j++) lag[j]/=n; }
        memcpy(st->lq[k],lag,16);
        /* ...and then SIT PART WAY BACK toward it, which is the part arriving late. */
        float out[4]; qnlerp(out,Q[b],lag,DK[k]*amt); memcpy(Q[b],out,16); }

    /* ---- MEASURE THE STRIDE. In RIG space the club's bodies face +Z (the model matrix
     * carries the yaw), which is the same convention the knee test and the board use. */
    if(P->footL<0||P->footR<0){ st->init=1; return; }
    const float*L=W[P->footL], *R=W[P->footR];
    float sep=L[14]-R[14];                                  /* +ve: the left foot leads */
    float low=(L[13]<R[13])?L[13]:R[13];
    float legLen=W[hp][13]-low;
    if(legLen<1e-4f){ st->init=1; return; }
    float u=fabsf(sep)/(0.50f*legLen); if(u>1.f) u=1.f;     /* 1 = contact, 0 = passing */

    if(!st->init){ st->init=1; st->u=u; st->act=0.f; st->prevSep=sep; return; }

    /* ⛔ A STANDING BODY GETS NOTHING. Amplitude follows how fast the feet are swapping,
     * so an idle does not bob and a walk does -- and it is right for the 105 citizens
     * whose clips nobody labelled. */
    float dsep=(dt>1e-5f)?(fabsf(sep-st->prevSep)/dt):0.f;
    st->prevSep=sep;
    float at=dsep/(legLen*1.30f); if(at>1.f) at=1.f;
    st->act += (at-st->act)*(1.f-expf(-dt/0.30f));

    /* THE LAG: down JUST AFTER the contact, up JUST AFTER the passing position. */
    st->u += (u-st->u)*(1.f-expf(-dt/0.055f));

    float g=amt*st->act;
    if(g<0.004f) return;

    /* ---- 1. THE UP AND DOWN -- his headline, and the whole feeling of weight. The
     * pelvis moves along RIG up, expressed in its parent's frame and divided by the
     * parent's scale, so it is the same centimetres on a rig baked in metres and one
     * baked in centimetres. */
    {   float dy=(0.5f-st->u)*2.f*0.030f*legLen*g;
        int pa=P->nodes[hp].parent;
        float up[3]={0.f,1.f,0.f}, ps=1.f;
        if(pa>=0){ wmsLocalAxis(W[pa],0.f,1.f,0.f,up);
                   ps=sqrtf(W[pa][4]*W[pa][4]+W[pa][5]*W[pa][5]+W[pa][6]*W[pa][6]);
                   if(ps<1e-6f) ps=1.f; }
        T[hp][0]+=up[0]*dy/ps; T[hp][1]+=up[1]*dy/ps; T[hp][2]+=up[2]*dy/ps; }

    /* ---- 2. THE BELT LINE, down with the foot that is down. Rolled about the direction
     * of travel, in the pelvis' own frame. */
    {   float tilt=(L[13]-R[13])/legLen;
        if(tilt>1.f) tilt=1.f; if(tilt<-1.f) tilt=-1.f;
        float ax[3]; wmsLocalAxis(W[hp],0.f,0.f,1.f,ax);
        float off[4]; qaxis(off,ax[0],ax[1],ax[2],tilt*0.13f*g);
        qmul(Q[hp],Q[hp],off); }

    /* ---- 3. THE SHOULDERS OPPOSE THE HIPS. Applied to the highest spine bone that
     * resolved, about rig UP, against whichever leg is leading. */
    {   int cb=-1;
        for(int c=3;c>=1;c--) if(P->pb[c]>=0){ cb=P->pb[c]; break; }
        if(cb>=0){ float sw=sep/(0.50f*legLen); if(sw>1.f) sw=1.f; if(sw<-1.f) sw=-1.f;
                   float ax[3]; wmsLocalAxis(W[cb],0.f,1.f,0.f,ax);
                   float off[4]; qaxis(off,ax[0],ax[1],ax[2],-sw*0.075f*g);
                   qmul(Q[cb],Q[cb],off); } } }
'''

sub("static void actorDraw(Pack*P,int clip,Pack*dnP,int dnC,double phase,const float*model,",
    LAYER + "\nstatic void actorDraw(Pack*P,int clip,Pack*dnP,int dnC,double phase,const float*model,",
    "the layer, above actorDraw")

# ---------------------------------------------------------------- 2. call it
sub("""                      GLint mModel,GLint mPal,GLint mBind,float*pal,
                      const SkPose*sk,float*footOut,float plantMix){   /* footOut: 2 rig-space feet + the rest-foot height */""",
    """                      GLint mModel,GLint mPal,GLint mBind,float*pal,
                      const SkPose*sk,float*footOut,float plantMix,
                      WmsState*wms,float wdt){   /* footOut: 2 rig-space feet + the rest-foot height */""",
    "actorDraw takes the state and a dt")

sub("""    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    /* THE FEET, MEASURED. The board is placed from these, so they are the contract --""",
    """    /* ⭐ THE WILLIAMS LAYER needs the pose MEASURED before it can comment on it -- the
     * contact/passing phase is the distance between the feet -- so FK first, overlay,
     * then FK again. The skater path has already paid for the first one. */
    if(wms){
        if(!sk) for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
            if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
        wmsApply(P,T,Q,W,wms,wdt,g_wms); }
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    /* THE FEET, MEASURED. The board is placed from these, so they are the contract --""",
    "FK, overlay, FK")

sub("""        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix);""",
    """        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix,
                  &A->wms,g_actorDt);""",
    "the call site")

# ---------------------------------------------------------------- 3. per-actor state
sub("""                 int leash;                    /* >=0: lead from THIS actor's neck to that actor's hand */""",
    """                 int leash;                    /* >=0: lead from THIS actor's neck to that actor's hand */
                 WmsState wms;                 /* the Williams layer's memory for this body */""",
    "SetActor carries its own")

sub("""            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f; A->leash=-1;""",
    """            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f; A->leash=-1;
            memset(&A->wms,0,sizeof A->wms);""",
    "and starts it clean")

# ---------------------------------------------------------------- 4. the dials
sub('                        if((q=strstr(buf,"dqs="))) g_labDqsOn=atoi(q+4)?1:0;',
    '                        if((q=strstr(buf,"dqs="))) g_labDqsOn=atoi(q+4)?1:0;\n'
    '                        if((q=strstr(buf,"wms="))) g_wms=(float)atof(q+4);',
    "ctl wms=")

sub("""    g_procRig = (trBone[2]>=0 && trBone[3]>=0 && revaP.pbOK>=10);""",
    '    g_wmsOff = getenv("REVA_NOWMS")?1:0;' + LF
    + '    if(g_wmsOff) printf("[wms] the Williams layer is OFF (REVA_NOWMS)' + NL + '");' + LF
    + "    g_procRig = (trBone[2]>=0 && trBone[3]>=0 && revaP.pbOK>=10);",
    "the kill switch")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
