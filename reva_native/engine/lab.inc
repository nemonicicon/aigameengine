/* ================= THE ANIM LAB (jacob 08-26: "a special mode specifically for testing
 * animations... reva the only actor... a test place") ==============================
 * One body, one clip, a measured floor and nothing else moving. The lab drives her
 * through the state tree on a clock, drops a mark wherever a foot touches down (feet
 * that plant leave neat pairs; feet that slide leave streaks) and reports the same
 * slip ratio motioncheck.py computes, live on the HUD. */
enum { LAB_IDLE=0, LAB_WALK, LAB_RUN, LAB_AIR, LAB_TRICK, LAB_SWING, LAB_GUN, LAB_N };
static const char*LAB_NAME[LAB_N]={"IDLE","WALK","RUN","AIR","TRICK","MELEE SWING","GUNFIRE"};
static float g_heroF[2][3];        /* her feet in world space, stashed by the draw */
static int   g_rvClipNow=-1; static float g_rvRateNow=0.f;   /* what the hero is actually playing */
/* GAIT CONSTANT. Clip playback rate = speed * this. It decides whether a planted foot
 * holds still in the world or slides: too high and she over-strides (the clip's legs
 * travel farther than the body does), too low and she moonwalks. REVA_GAITMUL lets the
 * lab sweep it and MEASURE the minimum instead of guessing. */
static float g_gaitMul=0.f;
static float gaitMul(void){ if(g_gaitMul<=0.f){ const char*e=getenv("REVA_GAITMUL");
        g_gaitMul=e?(float)atof(e):0.42f; } return g_gaitMul; }
/* ================= AUTO GAIT SYNC (S6) ==============================================
 * A single speed->playback constant cannot be right for two clips with different stride
 * lengths -- reva's walk wants 0.42 and her run wants something else entirely, and every
 * borrowed donor cycle is different again. So MEASURE the clip instead of tuning a magic
 * number: run forward kinematics over the cycle, take the widest the feet ever get
 * (that is one step), double it for a full stride, and solve for the playback rate that
 * makes the body travel exactly that far per cycle. Feet then plant by construction, for
 * any clip on any rig. Verified in the ANIM LAB: net foot drift per contact. */
/* S6e IS THIS CLIP ACTUALLY A GAIT? A locomotion role is mined by NAME, and a name can
 * lie: reva ships a clip called w_run that bakes into a crumpled, near-horizontal tangle
 * on her 27-node rig. No playback rate fixes a broken pose -- every gait sweep against it
 * failed because the premise was wrong. So validate the pose itself: across the cycle an
 * upright gait keeps its hips well above its lowest foot. A clip that fails is refused
 * for the role and the donor bank supplies a real one instead. */
static int clipUpright(Pack*P,int clip){
    if(!P||clip<0||(unsigned)clip>=P->nClips||P->footL<0||P->footR<0||P->pb[0]<0||P->nNodes>512) return 1;
    PClip*c=&P->clips[clip]; if(!c->frames) return 1;
    static float T[512][3],Q[512][4],S[512][3]; static M4 W[512];
    double acc=0.0; int n=0;
    for(int k=0;k<8;k++){
        unsigned f=(unsigned)((float)k/8.f*(float)c->frames)%c->frames;
        bodyRest(P,T,Q,S);
        unsigned char*tr=P->pk+c->offTracks;
        for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12),type=*(unsigned*)(tr+t*12+4),offD=*(unsigned*)(tr+t*12+8);
            if(node>=P->nNodes) continue;
            float*d=(float*)(P->pk+offD); int comp=type==0?4:3;
            if(type>=3) continue; else if(type==0) memcpy(Q[node],d+(size_t)f*comp,16);
            else if(type==1) memcpy(T[node],d+(size_t)f*comp,12);
            else memcpy(S[node],d+(size_t)f*comp,12); }
        bodyFK(P,T,Q,S,W);
        float lowest=W[P->footL][13]<W[P->footR][13]?W[P->footL][13]:W[P->footR][13];
        float hip=W[P->pb[0]][13];
        /* hip height alone is not enough -- reva's w_run keeps its hips up while folding
         * the body into a tangle. Demand a standing skeleton: head above hips, knees
         * below hips, feet below knees. */
        /* Head above hips is a fair test of "standing". Knee/foot ORDER is not: a run's
         * flight phase tucks the trailing foot above its own knee, and testing that
         * rejected mob1's w_run -- a clip this bench had already certified at 0.20. A
         * check that fails known-good data is a broken check. */
        if(P->pb[5]>=0&&W[P->pb[5]][13]<hip) return 0;
        acc+=(hip-lowest)*P->scale; n++; }
    if(!n) return 1;
    float hipH=(float)(acc/n);
    float want=(P->effH>0.5f?P->effH:3.15f)*0.30f;   /* hips clear the ground by ~half a body; 0.30 is generous */
    /* ⭐ (10-22) ...of THIS body: effH is a 3.15 floor, and a true-size five-year-old (1.84 u, hips ~0.9 u over his
     * toe) failed an adult's 0.945 -- the boys' walk was refused and they glided to every mark */
    if(P->trueScale&&P->trueH>0.5f) want=P->trueH*0.30f;
    return hipH>=want; }
/* ---- P7: THE PARTNER -- a SECOND BODY in the lab (jacob: "lets see kaeli and reva
 * interacting. how about a high five?"). Every authored move so far has been one body
 * alone, and a five is a different class of problem: the contact point belongs to
 * NEITHER body, it lives in the world between them.
 *
 * The partner deliberately gets the SIMPLEST possible pipeline -- sample a clip, FK,
 * skin, draw -- with no state tree, no gait, no grounding and no donor transfer. She
 * plays exactly what she is given. That is what makes an interaction verifiable: if the
 * two hands meet, they meet because the SCORE put them there, not because some runtime
 * helper nudged a limb into place.
 *
 * Placement is the ENGINE's job here, not the authored root track. Putting the
 * separation in the data would have been tidier, but kaeli's pack is the known unit
 * chimera -- her clip translations and node rests disagree, so an authored root
 * displacement dissolves on her. Gap and facing are engine-side and explicit, and the
 * hand-to-hand distance below is what proves they agree with the score. */

/* ======================= THE CAST: MORE THAN TWO PEOPLE =========================
 * jacob: "our scene system needs to be capable of handling more than 2 people."
 *
 * ⛔ The scene system was a two-hander to the bone -- one g_labPartner, one g_labPClip,
 * one g_partHeadW, and 74 references to a second body that could only ever be a second
 * body. Act V had to be split in half to be shootable.
 *
 * ⛔ AND A STRAIGHT REFACTOR IS THE WRONG MOVE. This project already wrote that down:
 * turning hero/partner into an array "would have rewritten the system that made THE
 * SPOT." Hand-editing 74 call sites is how a working scene system stops working.
 *
 * ⭐ So the globals BECOME slot 1 of this array, addressed through #define. Every
 * existing reference resolves to g_cast[g_castI] without being touched; g_castI is 1
 * everywhere it used to be implicit; the two-body path is bit-identical by construction.
 * Slots 2 and 3 are the same struct, drawn by the same function, in a loop.
 *
 * ⛔ SLOT 0 IS THE HERO and is NOT stored here -- she is the lab's own body, drawn by the
 * main path, and her marks live in g_heroHeadW/g_heroEyeW. The cast array is everyone
 * ELSE. Slot 0's entries stay zero and must never be drawn from. */
/* SCN_MAXCAST / SCN_BODIES / SCN_VOICE are defined at the top of the file now -- chainSim
 * and the overlay arrays need them ~5600 lines earlier than this. */
_Static_assert(SCN_MAXCAST<=8,"chainSim, the Williams state, the face latch and the held-light "
                              "collector all keep one entry per cast slot -- grow their arrays with this");
typedef struct {
    Pack  *pk;  int clip;  double phase;      /* who, what she is playing, and where in it */
    Pack  *dnP; int dnC;                      /* ...or a clip borrowed from a bank */
    float  gap, side, yaw;                    /* placement relative to the HERO's frame */
    float  headW[3], eyeW[3], mouthW[3];      /* measured by her draw, in world space */
    int    headOK, faceOK;  float iod;
    int    mouthOK;   /* ⭐ ep 12: a face_mouth mark on its own (no eyes) -- the smoke's mouth, never the aperture's */
    /* ⭐ HELD AT A PHASE. >=0 means this body is holding a single frame of its clip -- a
     * POSE, not an animation. Every clip in the club is a verb; a set needs nouns. */
    float  hold;
    Pack   storage;                           /* the loaded pack itself */
} CastSlot;
static CastSlot g_cast[SCN_MAXCAST];
static int g_castI = 1;      /* the slot labDrawPartner is working on right now */

#define g_labPartner   (g_cast[g_castI].pk)
#define g_labPClip     (g_cast[g_castI].clip)
#define g_labPPhase    (g_cast[g_castI].phase)
#define g_labPHold     (g_cast[g_castI].hold)
#define g_labPDnP      (g_cast[g_castI].dnP)
#define g_labPDnC      (g_cast[g_castI].dnC)
#define g_labPGap      (g_cast[g_castI].gap)
#define g_labPSide     (g_cast[g_castI].side)
#define g_labPYaw      (g_cast[g_castI].yaw)
#define g_labPartnerPk (g_cast[g_castI].storage)
#define g_partHeadW    (g_cast[g_castI].headW)
#define g_partEyeW     (g_cast[g_castI].eyeW)
#define g_partMouthW   (g_cast[g_castI].mouthW)
#define g_partHeadOK   (g_cast[g_castI].headOK)
#define g_partFaceOK   (g_cast[g_castI].faceOK)
#define g_partIOD      (g_cast[g_castI].iod)

/* ⛔ THE DEFAULTS ARE NOT ZERO. gap 1.60 / yaw 180 / clip -1 used to be static
 * initialisers; a struct array zero-fills, which would stand every extra INSIDE the hero
 * facing the same way with clip 0. Called once at startup. */
static void castInit(void){
    for(int i=0;i<SCN_MAXCAST;i++){
        g_cast[i].pk=NULL; g_cast[i].clip=-1; g_cast[i].phase=0.0;
        g_cast[i].dnP=NULL; g_cast[i].dnC=-1;
        /* ⛔ -1, NOT 0. A zeroed struct means hold=0, which reads as "hold at frame 0"
         * rather than "do not hold" -- every partner outside a scene would have frozen on
         * its first frame. Exactly the default-value trap that ate focus=hero an hour
         * earlier, where hero IS 0. */
        g_cast[i].hold=-1.f;
        /* ⛔ NOT ALL THE SAME MARK. 1.60/0/180 is directly in front of the hero, which
         * is where slot 1 stands -- a third body inheriting it is a body inside another
         * body. Later slots take the other points of a triangle, which is also simply
         * correct blocking for three people in a conversation. */
        g_cast[i].gap =(i<=1)?1.60f:1.45f;
        g_cast[i].side=(i<=1)?0.00f:((i&1)?-1.25f:1.25f);
        g_cast[i].yaw =(i<=1)?180.0f:((i&1)?214.0f:146.0f);
        g_cast[i].headOK=0; g_cast[i].faceOK=0; g_cast[i].mouthOK=0; g_cast[i].iod=0.f; } }

/* (slot 1's fields are the old partner globals; see the cast note above) */
/* Z7: the partner's own locomotion. She is staged by marks that MOVE between lines, and
 * a body that changes place without a gait is a body sliding on ice. */
/* and the PARTNER can borrow too -- she had no donor path at all, only her own clips,
 * which is why a scene could not put a conversation gesture on her. */
static float g_partSpd=0.f;        /* her world speed this frame, from the staging ease */
static Pack *g_partWalkP=NULL;     /* whose walk she borrows (her own pack, or a bank) */
static int   g_partWalkC=-1;
static float g_partWalkMix=0.f;    /* 0 = staged clip, 1 = walking; eased so it never pops */
static float g_stageMinSep=0.f;    /* MEASURED floor on how close the scene may stage them */
/* Each body publishes its own SHOULDER WIDTH from its own FK, in world units. Measured
 * across the upperarm bones (canon 7 and 10) -- the clavicles sit near the sternum and
 * under-report a span by half. */
static float g_heroShoulderW=0.f, g_partShoulderW=0.f;
static float g_labAimY=0.75f;                  /* ctl aimy=: close-up column aim height above the wall aim (0.75 = chest; ~1.3 = faces) */
static int   g_labFace=0;                      /* ctl face=4|8: the FACE RIG -- head-aimed close panels */
static int   g_labDqsOn=1;                     /* ctl dqs=0: force LBS in the lab (DQS balloons soft-bound shoulders) */
static int   g_labHi5=0;                       /* print the inter-body hand distance (the contact gate) */
static float g_labHeroHand[3]={0,0,0};         /* hero's right hand, world -- filled during her draw */
static int   g_labHeroHandOK=0;

/* ---- H2: DUAL-QUATERNION palette upload (homecoming) ------------------------------
 * Converts a matrix palette to 2 vec4 per bone: real quat from the (de-scaled) 3x3,
 * dual = 0.5 * t_quat * real. The shader hemisphere-corrects against the strongest
 * influence, so no correction is needed here. Falls through to plain LBS matrices
 * when dq==0 -- one call site serves both paths. */
static int g_dqs=-1;   /* env REVA_LBS=1 forces 0 */
static GLint g_labUse=-1, g_labTint=-1;
/* ⭐ THE TOON PASS (10-18). Four programs draw bodies (PR and PRD, opaque and cutout); their uniform locations
 * are looked up once per program here. toonCel() before a body mesh's draw (0 for every non-toon pack: uniforms
 * persist per program), toonInkDraw() right after it while its index buffer is still bound. */
static float g_toonCel=0.30f, g_toonInk=0.0020f, g_toonRim=0.30f; static int g_toonOff=-1;
static int g_toonKeyCam=1; static float g_roomGrad=1.f;   /* ⭐ (10-23) the key from the lens, the room's ceiling light (REVA_TOONKEY, REVA_ROOMGRAD) */   /* ⭐ (10-23) the rim: REVA_TOONRIM */
static const float g_inkC[3]={0.110f,0.086f,0.118f};
typedef struct { GLuint prog; GLint cel, inkc, ink, asp, con, ca, cb, cr, hm, hmi, bc, hmr, wy, hemi, emis, rim, refl, gloss; } ToonLoc;
static ToonLoc g_tl[8]; static int g_ntl=0;
static ToonLoc*toonLoc(void){ GLint pr=0; glGetIntegerv(GL_CURRENT_PROGRAM,&pr);
    for(int i=0;i<g_ntl;i++) if(g_tl[i].prog==(GLuint)pr) return &g_tl[i];
    if(g_ntl>=8) return NULL;
    ToonLoc*t=&g_tl[g_ntl++]; t->prog=(GLuint)pr;
    t->cel=glGetUniformLocation(pr,"uCel"); t->inkc=glGetUniformLocation(pr,"uInkC");
    t->ink=glGetUniformLocation(pr,"uInk"); t->asp=glGetUniformLocation(pr,"uAsp");
    t->con=glGetUniformLocation(pr,"uColOn"); t->ca=glGetUniformLocation(pr,"uCapA"); t->cb=glGetUniformLocation(pr,"uCapB");
    t->cr=glGetUniformLocation(pr,"uCapR"); t->hm=glGetUniformLocation(pr,"uHeadM"); t->hmi=glGetUniformLocation(pr,"uHeadMI");
    t->bc=glGetUniformLocation(pr,"uBodyC"); t->hmr=glGetUniformLocation(pr,"uHeadMIR");
    t->wy=glGetUniformLocation(pr,"uWaistY");
    t->hemi=glGetUniformLocation(pr,"uHemi"); t->emis=glGetUniformLocation(pr,"uEmis");
    t->rim=glGetUniformLocation(pr,"uRim"); t->refl=glGetUniformLocation(pr,"uRefl"); t->gloss=glGetUniformLocation(pr,"uGloss");
    return t; }
/* ⭐ (10-23) THE GLOSSY FLOOR (jacob: "a general like shader/reflections pass"). A set's `gloss y [F0] [max] [fade]` makes
 * its floor a second MIRROR, lying down: every body and prop above it is drawn a second time through its plane (mirInst,
 * as the wall mirrors are), fading with its depth below it (uRefl: a polished floor shows what is near it), while the
 * floor itself -- a pack carrying the marker `Floor` -- is held back from drawSet. It goes down LAST, here, after
 * every body: its own colour plus the reflection under it times a Fresnel term (F0 looking straight down, rising
 * toward `max` at a grazing angle) -- glBlendFunc(ONE, SRC_ALPHA) with the floor's alpha set to F. In a closed room
 * every floor pixel has the room's reflection under it. One bounce: a reflection is not reflected again, and the floor
 * seen in a wall mirror is drawn opaque. No `Floor` pack, no `gloss` line, or REVA_NOGLOSS=1: the floor is drawn as it
 * always was, in drawSet. */
static void glossFloorPass(GLuint prog,GLint uVP,GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,const float*vp){
    if(!g_gloss.on||!g_setNI) return;
    glUseProgram(prog); glUniformMatrix4fv(uVP,1,GL_FALSE,vp);
    ToonLoc*tl=toonLoc();
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_SRC_ALPHA);
    if(tl&&tl->gloss>=0) glUniform4f(tl->gloss,g_gloss.f0,g_gloss.fmax,0.f,1.f);
    glActiveTexture(GL_TEXTURE0);
    g_glossDraw=1; int n=0;
    for(int i=0;i<g_setNI;i++){ SetInst*I=&g_setI[i]; if(!g_setPk[I->pk].floor) continue;
        if(I->tinted){ glUniform1f(uUse,0.f); glUniform3f(uTint,I->tint[0],I->tint[1],I->tint[2]); }
        else         { glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }
#ifdef REVA_PACKET
        if(I->tinted){ g_vkUse=0.f; memcpy(g_vkTint,I->tint,12); } else { g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; }
        g_vkOwner=((unsigned)GFX_KIND_PROP<<28)|(((unsigned)i&0xffffu)<<12);
#endif
        drawProp(&g_setPk[I->pk],I->m,uModel,uPal,uBind); n++; }
    g_glossDraw=0;
    if(tl&&tl->gloss>=0) glUniform4f(tl->gloss,0.f,0.f,0.f,0.f);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE); glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
    { static int said=0; if(!said){ said=1; printf("[gloss] %d floor piece(s) drawn over their reflections\n",n); fflush(stdout); } } }

/* ⭐ PER-VERTEX COLLISION (10-18). headMats: the measured head ellipsoid on the POSED head bone (rotation only --
 * the bone's frame orthonormalised), as a matrix from the unit sphere to rig space and back. */
static void measureHead(Pack*P);
static int g_vcolOn=-1;
static int headMatsM(Pack*P,const float*w,float*Mo,float*MI);
static int headMats(Pack*P,M4*W,float*Mo,float*MI){
    if(!P->headOK) measureHead(P);
    int h=P->pb[5]; if(P->headOK<=0||h<0||(unsigned)h>=P->nNodes) return 0;
    return headMatsM(P,W[h],Mo,MI); }
static int headMatsM(Pack*P,const float*w,float*Mo,float*MI){
    float ax[3][3];
    for(int c=0;c<3;c++){ float l=sqrtf(w[c*4]*w[c*4]+w[c*4+1]*w[c*4+1]+w[c*4+2]*w[c*4+2]); if(l<1e-9f) return 0;
        for(int k=0;k<3;k++) ax[c][k]=w[c*4+k]/l; }
    const float*t=&w[12];
    for(int k=0;k<16;k++){ Mo[k]=0.f; MI[k]=0.f; }
    for(int c=0;c<3;c++) for(int k=0;k<3;k++) Mo[c*4+k]=ax[c][k]*P->headR[c];
    for(int k=0;k<3;k++) Mo[12+k]=t[k]+ax[0][k]*P->headC[0]+ax[1][k]*P->headC[1]+ax[2][k]*P->headC[2];
    Mo[15]=1.f;
    for(int i=0;i<3;i++){ float r=P->headR[i];
        for(int j=0;j<3;j++) MI[j*4+i]=ax[i][j]/r;
        MI[12+i]=(-(ax[i][0]*t[0]+ax[i][1]*t[1]+ax[i][2]*t[2])-P->headC[i])/r; }
    MI[15]=1.f;
    return 1; }
static void colSet(Pack*P,M4*W,unsigned mi){
    int live9=g_skirtLive; g_skirtLive=0;   /* ⭐ this mesh is HANGING CLOTH (skirtPrep): simulated, world space */
    ToonLoc*t=toonLoc(); if(!t||t->con<0) return;
    if(live9){ glUniform1f(t->con,3.f); if(t->bc>=0) glUniform3f(t->bc,g_skirtBC[0],g_skirtBC[1],g_skirtBC[2]); return; }
    if(g_vcolOn<0){ const char*e=getenv("REVA_VCOLLIDE"); g_vcolOn=!(e&&e[0]=='0');
        printf("[vcol] per-vertex collision: %s\n",g_vcolOn?"ON (packs with a VertexCollide node)":"OFF (REVA_VCOLLIDE=0)"); fflush(stdout); }
    float on=0.f;
    if(P&&W&&P->vcol&&g_vcolOn&&mi<P->nMeshes){ const char*n=packStr(P,P->meshes[mi].name);
        if(n&&(strstr(n,"~skirt~")||strstr(n,"~cloth"))) on=1.f;
        else if(n&&!strncmp(n,"wear~hair~",10)) on=2.f; }
    glUniform1f(t->con,on);
    if(on<0.5f) return;
    if(!P->boneRok) measureBoneR(P);
    float A[12],B[12],R[4]; int nc=0;
    for(int k=0;k<12;k++){ A[k]=0.f; B[k]=0.f; } for(int k=0;k<4;k++) R[k]=0.f;
    if(on<1.5f){ const int segs[4][2]={{12,13},{13,-2},{14,15},{15,-3}};
        for(int q=0;q<4;q++){ int a=P->pb[segs[q][0]];
            int b=segs[q][1]>=0?P->pb[segs[q][1]]:(segs[q][1]==-2?P->footL:P->footR);
            float r=P->boneR[segs[q][0]];
            if(a>=0&&b>=0&&(unsigned)a<P->nNodes&&(unsigned)b<P->nNodes&&r>0.f){
                /* a THIGH starts a fifth of the way down: its sphere at the hip joint reaches out of the hip */
                float k0=(q==0||q==2)?0.2f:0.f;
                for(int k=0;k<3;k++){ A[nc*3+k]=W[a][12+k]+(W[b][12+k]-W[a][12+k])*k0; B[nc*3+k]=W[b][12+k]; }
                R[nc]=r*1.06f; nc++; } } }   /* a hair over the leg: the cloth rides outside the trousers */
    else { int a=P->pb[4], b=P->pb[5]; float r=P->boneR[4];
        if(a>=0&&b>=0&&(unsigned)a<P->nNodes&&(unsigned)b<P->nNodes&&r>0.f){
            for(int k=0;k<3;k++){ A[k]=W[a][12+k]; B[k]=W[b][12+k]; } R[0]=r; nc=1; } }
    { static float vr=-1.f; if(vr<0.f){ const char*e=getenv("REVA_VCOLR"); vr=(e&&*e)?(float)atof(e):1.f; }
      for(int k=0;k<4;k++) R[k]*=vr; }   /* REVA_VCOLR: scale the capsules -- the test that the push RUNS at all */
    glUniform3fv(t->ca,4,A); glUniform3fv(t->cb,4,B); glUniform1fv(t->cr,4,R);
    if(t->bc>=0){ int hp=P->pb[0]; if(hp>=0&&(unsigned)hp<P->nNodes) glUniform3f(t->bc,W[hp][12],W[hp][13],W[hp][14]); }
    if(t->wy>=0){ int sp9=P->pb[1], hp=P->pb[0];                 /* the waist: where the spine starts */
        float wy=(sp9>=0&&(unsigned)sp9<P->nNodes)?W[sp9][13]:((hp>=0&&(unsigned)hp<P->nNodes)?W[hp][13]+0.2f:1e9f);
        glUniform1f(t->wy,wy); }
    { static int dbg=-1, said=0; if(dbg<0){ const char*e=getenv("REVA_VCOLDBG"); dbg=(e&&e[0]=='1'); }
      if(dbg&&said<24){ said++;
        printf("[vcoldbg] %s mesh %u '%s' on %.0f prog %u loc con %d ca %d cr %d | caps %d r %.3f %.3f %.3f %.3f | A0 %.3f %.3f %.3f B0 %.3f %.3f %.3f\n",
               P->label,mi,packStr(P,P->meshes[mi].name),on,t->prog,t->con,t->ca,t->cr,nc,R[0],R[1],R[2],R[3],
               A[0],A[1],A[2],B[0],B[1],B[2]); fflush(stdout); } }
    if(on>1.5f){ float Mo[16],MI[16],MoR[16],MIR[16];
        if(headMats(P,W,Mo,MI)&&headMatsM(P,P->headW0,MoR,MIR)){
            glUniformMatrix4fv(t->hm,1,GL_FALSE,Mo); glUniformMatrix4fv(t->hmi,1,GL_FALSE,MI);
            if(t->hmr>=0) glUniformMatrix4fv(t->hmr,1,GL_FALSE,MIR); }
        else glUniform1f(t->con,0.f); } }   /* no head measured: no hair rule (never the cloth rule on hair) */
static int toonOn(const void*Pv,int toon){
    if(g_toonOff<0){ const char*e=getenv("REVA_NOTOON"); g_toonOff=(e&&*e&&*e!='0');
        const char*c=getenv("REVA_TOONCEL"); if(c&&*c) g_toonCel=(float)atof(c);
        const char*k=getenv("REVA_TOONINK"); if(k&&*k) g_toonInk=(float)atof(k);
        const char*rm=getenv("REVA_TOONRIM"); if(rm&&*rm) g_toonRim=(float)atof(rm);
        printf("[toon] cel %.2f ink %.4f%s\n",g_toonCel,g_toonInk,g_toonOff?" -- OFF (REVA_NOTOON)":""); fflush(stdout); }
    return Pv&&toon&&!g_toonOff; }
#ifdef REVA_PACKET
static void toonTell(void){ gfxToonSet(g_toonCel,g_toonOff?0.f:g_toonRim,(float)g_toonKeyCam,g_roomGrad,g_toonInk,g_inkC); }
#endif
static void toonCelSet(int on){ ToonLoc*t=toonLoc(); if(t&&t->cel>=0) glUniform1f(t->cel,on?g_toonCel:0.f);
    if(t&&t->rim>=0) glUniform1f(t->rim,(on&&!g_toonOff)?g_toonRim:0.f); }
/* ⭐ THE SHIP'S SETS (10-18). hemi: the cel cut taken on the surface's facing UP (0.5+0.5*N.y) times the sun's
 * shadow, not on the sun's direction -- a room is lit from its ceiling, so every wall reads lit and every underside
 * falls into the shadow tone, while the bodies keep the key. glow: uEmis 1, the surface at its own colour. Both
 * reset to 0 after the prop. Not in the sun's own pass (its program has neither). */
static void toonSetLook(int hemi,int glow){ if(g_shadowPass) return; ToonLoc*t=toonLoc(); if(!t) return;
    if(t->hemi>=0) glUniform1f(t->hemi,hemi?1.f:0.f);
    if(t->emis>=0) glUniform1f(t->emis,glow?1.f:0.f); }
static void toonInkDraw(int on,const char*meshName,GLsizei idxCount,GLsizei verts){
    if(!on||g_toonInk<=0.f||g_glossDraw) return;   /* ⭐ (10-23) a glossy floor's own pass: no hull (a flat floor's is under it) */
    if(meshName&&strstr(meshName,"~face~")) return;          /* the face's drawn shapes are decals: no line */
    ToonLoc*t=toonLoc(); if(!t||t->ink<0||t->inkc<0) return;
    GLint vpv[4]; glGetIntegerv(GL_VIEWPORT,vpv);
    GLboolean cull=glIsEnabled(GL_CULL_FACE); GLint cf=GL_BACK; glGetIntegerv(GL_CULL_FACE_MODE,&cf);
    glEnable(GL_CULL_FACE); glCullFace(GL_FRONT);
    glUniform1f(t->ink,g_toonInk);
    if(t->asp>=0) glUniform2f(t->asp,(float)vpv[3]/(float)(vpv[2]>0?vpv[2]:1),1.f);
    glUniform4f(t->inkc,g_inkC[0],g_inkC[1],g_inkC[2],1.f);
    /* ⛔ A DEPTH BIAS: where a surface and an ink hull meet at the SAME depth (two cloth walls pressed together, a
     * fold) the hull won and drew black blotches; pushed a hair back, the surface wins and the silhouette is unchanged */
    glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(2.5f,4.f);   /* slope-scaled: a grazing surface's depth runs fast */
    if(idxCount) glDrawElements(GL_TRIANGLES,idxCount,GL_UNSIGNED_INT,0); else glDrawArrays(GL_TRIANGLES,0,verts);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glUniform1f(t->ink,0.f); glUniform4f(t->inkc,0.f,0.f,0.f,0.f);
    glCullFace((GLenum)cf); if(!cull) glDisable(GL_CULL_FACE); }
/* ⭐ (10-22) MIRRORS (engine_capabilities.md section 44). The book: "half the walls on the ship were mirrored to
 * make the rooms seem more spacious" (p18-19); 2.9 turns on Marie-Claude catching "a glimpse of herself in the
 * wall mirror". A mirror is an OPENING in a wall with the room's reflection behind it -- the old game-engine mirror:
 * every body and prop in front of the glass is drawn a SECOND time through the glass's plane (its model matrix
 * reflected), right after its own draw, from the pose already solved. No second camera, no second step of the sim
 * (the frame is one loop that simulates as it draws -- a reflected CAMERA would have stepped every cloth twice). The
 * depth test does the masking: the real wall round the opening hides the reflected room everywhere else, so a
 * set's room must be closed (nothing of another room behind its mirror wall). A reflection flips the winding: the
 * ink hull culls the other side (glFrontFace CW). Props cull BACK faces in the reflection -- the reflected copy of
 * the mirror's own wall lies IN its plane, facing away. The model matrix is read back from the program, so the call
 * site needs nothing but its uniform (the hanging cloth, drawn in world space, reflects the same way). One bounce:
 * a reflection is not reflected again. REVA_NOMIRROR=1 is the control. */
static void mirInst(GLint uModel,int toon,const char*meshName,GLsizei idxCount,GLsizei verts,int flags){
    /* flags: 1 cull back faces (props); 2 no ink -- a mirror's backing and its glass, reflected through their OWN mirror,
     * land just inside the room facing away, and an inverted hull drawn from behind blacked the first mirror out */
    int cull=flags&1;
    if((!g_nMir&&!g_gloss.on)||g_shadowPass||uModel<0) return;
    if(g_mirOff<0){ const char*e=getenv("REVA_NOMIRROR"); g_mirOff=(e&&e[0]!='0')?1:0;
        if(g_mirOff) printf("[mirror] REVA_NOMIRROR: mirrors are dark openings\n"); }
    if(g_mirOff) return;
    GLint pr=0; glGetIntegerv(GL_CURRENT_PROGRAM,&pr); if(!pr) return;
    float cur[16]; glGetUniformfv((GLuint)pr,uModel,cur);
    GLboolean cf0=glIsEnabled(GL_CULL_FACE);
    static int dbg=-1, ndbg=0; if(dbg<0){ const char*e=getenv("REVA_MIRDBG"); dbg=(e&&e[0]!='0')?1:0; }
    ToonLoc*tl=toonLoc();
    int nFl=(g_gloss.on&&!g_glossDraw)?1:0;   /* ⭐ (10-23) the glossy floor, lying down; never reflected in itself */
    for(int k=0;k<g_nMir+nFl;k++){ Mirror*M=(k<g_nMir)?&g_mir[k]:&g_glossM; int fl=(k>=g_nMir);
        float dt=(cur[12]-M->p[0])*M->n[0]+(cur[13]-M->p[1])*M->n[1]+(cur[14]-M->p[2])*M->n[2];
        if(dbg&&ndbg<40){ ndbg++; printf("[mirror] %s prog %d uModel %d t %.2f %.2f %.2f -> mirror %d dt %.2f%s (glerr %x)\n",
            meshName?meshName:"?",(int)pr,(int)uModel,cur[12],cur[13],cur[14],k,dt,dt<-0.5f?" SKIP":"",glGetError()); fflush(stdout); }
        if(dt<-0.5f) continue;                     /* behind the glass: nothing of it shows in it */
        float mm[16]; m4_mul(mm,M->R,cur);
        glUniformMatrix4fv(uModel,1,GL_FALSE,mm);
        glFrontFace(GL_CW);
        if(cull){ glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
        if(fl&&tl&&tl->refl>=0) glUniform4f(tl->refl,M->p[1],1.f/g_gloss.fade,0.f,1.f);
        if(g_glossDraw&&!fl){ glDisable(GL_BLEND); if(tl&&tl->gloss>=0) glUniform4f(tl->gloss,0.f,0.f,0.f,0.f); }
        if(idxCount) glDrawElements(GL_TRIANGLES,idxCount,GL_UNSIGNED_INT,0); else glDrawArrays(GL_TRIANGLES,0,verts);
        if(!(flags&2)) toonInkDraw(toon,meshName,idxCount,verts);
        if(fl&&tl&&tl->refl>=0) glUniform4f(tl->refl,0.f,0.f,0.f,0.f);
        if(g_glossDraw&&!fl){ glEnable(GL_BLEND); glBlendFunc(GL_ONE,GL_SRC_ALPHA); if(tl&&tl->gloss>=0) glUniform4f(tl->gloss,g_gloss.f0,g_gloss.fmax,0.f,1.f); }
        if(cull&&!cf0) glDisable(GL_CULL_FACE);
        glFrontFace(GL_CCW); }
    glUniformMatrix4fv(uModel,1,GL_FALSE,cur); }
static GLint g_labMouthP=-1, g_labMouthS=-1, g_labOpen=-1;   /* the aperture, for hero AND partner */   /* the ACTIVE lab program's texture/tint locs (PR vs PRD) */
static void skinPal(GLint loc,GLint locS,int dq,const float*mats,int n,float s){
    if(g_dqs<0){ const char*e=getenv("REVA_LBS"); g_dqs=(e&&e[0]=='1')?0:1; }
    if(!dq||!g_dqs){ glUniformMatrix4fv(loc,n,GL_FALSE,mats); return; }
    static float buf[64*8];
    float is=(s>1e-6f)?1.f/s:1.f;
    for(int b=0;b<n&&b<64;b++){ const float*m=&mats[b*16]; float q[4];
        float r00=m[0]*is,r10=m[1]*is,r20=m[2]*is;
        float r01=m[4]*is,r11=m[5]*is,r21=m[6]*is;
        float r02=m[8]*is,r12=m[9]*is,r22=m[10]*is;
        float tr=r00+r11+r22;
        if(tr>0){ float sq=sqrtf(tr+1.f)*2.f;
            q[3]=0.25f*sq; q[0]=(r21-r12)/sq; q[1]=(r02-r20)/sq; q[2]=(r10-r01)/sq; }
        else if(r00>r11&&r00>r22){ float sq=sqrtf(1.f+r00-r11-r22)*2.f;
            q[3]=(r21-r12)/sq; q[0]=0.25f*sq; q[1]=(r01+r10)/sq; q[2]=(r02+r20)/sq; }
        else if(r11>r22){ float sq=sqrtf(1.f+r11-r00-r22)*2.f;
            q[3]=(r02-r20)/sq; q[0]=(r01+r10)/sq; q[1]=0.25f*sq; q[2]=(r12+r21)/sq; }
        else { float sq=sqrtf(1.f+r22-r00-r11)*2.f;
            q[3]=(r10-r01)/sq; q[0]=(r02+r20)/sq; q[1]=(r12+r21)/sq; q[2]=0.25f*sq; }
        float ql=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
        if(ql>1e-8f){ q[0]/=ql; q[1]/=ql; q[2]/=ql; q[3]/=ql; }
        float tx=m[12],ty=m[13],tz=m[14];
        buf[b*8+0]=q[0]; buf[b*8+1]=q[1]; buf[b*8+2]=q[2]; buf[b*8+3]=q[3];
        buf[b*8+4]=0.5f*( tx*q[3]+ty*q[2]-tz*q[1]);
        buf[b*8+5]=0.5f*(-tx*q[2]+ty*q[3]+tz*q[0]);
        buf[b*8+6]=0.5f*( tx*q[1]-ty*q[0]+tz*q[3]);
        buf[b*8+7]=0.5f*(-tx*q[0]-ty*q[1]-tz*q[2]); }
    glUniform4fv(loc,2*n,buf); if(locS>=0) glUniform1f(locS,s); }

/* ================= MORPH BLENDING, for ANY body ==============================
 * Was hero-only and capped at 80k vertices. A conversation needs it on both faces,
 * and kaeli's mesh is 294k, so this blends over the target's own VERTEX RANGE and
 * uploads only that window. Restores the base exactly once when the weights fall
 * back to zero (the caller owns that little bit of state, so hero and partner do
 * not fight over one static flag). */
static int morphIdx(Pack*P,const char*nm);
static int morphSetName(Pack*P,float*wts,const char*nm,float v);
#define JAW_MAX_RAD 0.384f              /* ~22 degrees at full open */
static Pack *g_heroPk;                  /* defined below; the aperture needs to tell
                                         * hero from partner when it reads g_jawOpen */
static float g_jawOpen[2]={0.f,0.f};    /* 0 = hero, 1 = partner; read by the aperture */
static float g_jawForce=-1.f;           /* REVA_JAWFORCE / ctl jaw= : the bench override */
/* ---- THE FACE MARK GIZMO -----------------------------------------------------
 * Draw the face landmarks as crosshairs, in the same picture as the face they claim
 * to describe. Every face rung before this one was calibrated by INFERRING a mark's
 * position from how the camera framed her, which cannot tell "the mark is wrong"
 * apart from "the feature is not where I thought" -- and this session found BOTH at
 * once on reva (her mark floated 0.035 rig in front of her lips AND 0.032 to the side
 * of her face's own midline). With the marks drawn, the eye settles it in one look.
 *   cyan   the lip line          green  the mouth CORNERS (the aperture's width)
 *   yellow the eye line          magenta the chin
 * On under REVA_FACEDBG=1 or whenever the face rig (ctl face=10) is up. */
static int g_faceDbg=-1;
static void ptWorld(const float*mw,const float*model,float*dst);
static void gzCross(float*v,int*n,int cap,const float*p,float s,float r,float g,float b){
    if(*n+6>cap) return;
    const float ax[3][3]={{s,0,0},{0,s,0},{0,0,s}};
    for(int a=0;a<3;a++){
        for(int e=0;e<2;e++){ float sg=e?1.f:-1.f; float*d=v+(*n)*6;
            d[0]=p[0]+ax[a][0]*sg; d[1]=p[1]+ax[a][1]*sg; d[2]=p[2]+ax[a][2]*sg;
            d[3]=r; d[4]=g; d[5]=b; (*n)++; } } }
static int faceGizmo(Pack*P,M4*world,const float*model,float*v,int cap,float scl){
    int n=0; float p[3];
    if(!P) return 0;
    /* the marks are drawn at a size tied to the face itself, so the crosshair reads
     * the same on a 1.8-unit rig and a 168-unit one */
    float s=scl;
    if(P->fmMouth>=0){ ptWorld(world[P->fmMouth],model,p); gzCross(v,&n,cap,p,s,0.15f,1.f,1.f); }
    if(P->fmMouthL>=0){ ptWorld(world[P->fmMouthL],model,p); gzCross(v,&n,cap,p,s*0.75f,0.1f,1.f,0.25f); }
    if(P->fmMouthR>=0){ ptWorld(world[P->fmMouthR],model,p); gzCross(v,&n,cap,p,s*0.75f,0.1f,1.f,0.25f); }
    if(P->fmEyeL>=0){ ptWorld(world[P->fmEyeL],model,p); gzCross(v,&n,cap,p,s*0.75f,1.f,0.9f,0.1f); }
    if(P->fmEyeR>=0){ ptWorld(world[P->fmEyeR],model,p); gzCross(v,&n,cap,p,s*0.75f,1.f,0.9f,0.1f); }
    if(P->fmChin>=0){ ptWorld(world[P->fmChin],model,p); gzCross(v,&n,cap,p,s*0.75f,1.f,0.2f,0.9f); }
    /* the HINGE, in red: a jaw that swings wrong is nearly always a hinge in the wrong
     * place, and it is the one landmark that is inside the head where nothing can be
     * read off the surface */
    if(P->fmJaw>=0){ ptWorld(world[P->fmJaw],model,p); gzCross(v,&n,cap,p,s,1.f,0.25f,0.15f); }
    /* the mouth LINE: corner to corner, so its width is read directly off her face */
    if(P->fmMouthL>=0&&P->fmMouthR>=0&&n+2<=cap){
        float a[3],b[3]; ptWorld(world[P->fmMouthL],model,a); ptWorld(world[P->fmMouthR],model,b);
        float*d=v+n*6; d[0]=a[0];d[1]=a[1];d[2]=a[2]; d[3]=0.1f;d[4]=1.f;d[5]=0.25f; n++;
        d=v+n*6; d[0]=b[0];d[1]=b[1];d[2]=b[2]; d[3]=0.1f;d[4]=1.f;d[5]=0.25f; n++; }
    return n; }
/* Arm the mouth aperture for a body: where her lip line sits in BIND space and how
 * far open the jaw is right now. Everything without face marks passes 0 and the
 * shader leaves the face alone. */
/* how far this body's mouth is open, 0..1 -- one source of truth for the aperture */
static float mouthOpenOf(Pack*P,const float*wts){
    float open=0.f;
    /* ⛔ ep 12: THE APERTURE'S OWN GATE, HERE, where BOTH arms read it. mouthArm refused a body without eye
     * marks, but mouthMeshArm set every mesh's uOpen straight from this -- so the day kaeli got a lone
     * `face_mouth` (for the smoke), her jaw weight would have armed an aperture positioned by whichever
     * body last set mouthP/mouthS (reva's lips, in kaeli's bind space): a hole in her face on every word. */
    if(!P||P->fmMouth<0||P->fmEyeL<0||P->fmEyeR<0) return 0.f;
    /* THE APERTURE FOLLOWS THE JAW BONE when the body has one, and only falls back to
     * the old "jaw" morph weight for a body that was never rigged. Two sources for one
     * opening is how a hole ends up out of step with the face around it. */
    if(P&&P->fmJaw>=0) open=g_jawOpen[(P==g_heroPk)?0:1];
    else if(P&&P->fmMouth>=0){ int j=morphIdx((Pack*)P,"jaw"); if(j>=0&&wts) open=wts[j]; }
    return open<0.f?0.f:(open>1.f?1.f:open); }
/* THE APERTURE IS A SKIN EFFECT AND MUST NEVER CUT THE INTERIOR. The discard is keyed
 * on the REST position, and the teeth, tongue and cavity sit exactly where the hole
 * is -- so armed for the interior mesh it would delete the very geometry it exists to
 * reveal. Depth alone cannot separate them either: the gate reaches 1.1 mouth
 * half-widths back, which is past the teeth. So the interior is its OWN MESH and the
 * opening is switched off while it draws. */
static void mouthMeshArm(Pack*P,unsigned mi,const float*wts,GLint uOpen){
    if(uOpen<0||!P) return;
    glUniform1f(uOpen,((int)mi==P->fmInterior)?0.f:mouthOpenOf(P,wts)); }
static void mouthArm(Pack*P,const float*wts,GLint uMouthP,GLint uMouthS,GLint uOpen){
    if(uOpen<0) return;
    float open=mouthOpenOf(P,wts);
    if(!P||P->fmMouth<0||P->fmEyeL<0){ glUniform1f(uOpen,0.f); return; }
    if(uMouthP>=0) glUniform4f(uMouthP,P->mouthBind[0],P->mouthBind[1],P->mouthBind[2],P->mouthBind[5]);
    if(uMouthS>=0) glUniform2f(uMouthS,P->mouthBind[3],P->mouthBind[4]);
    glUniform1f(uOpen,open<0.f?0.f:(open>1.f?1.f:open)); }
/* a node's world position through a body's model matrix -- the face marks reach the
 * camera the same way the hands reach the contact gates */
static void ptWorld(const float*mw,const float*model,float*dst){
    float ax=mw[12],ay=mw[13],az=mw[14];
    dst[0]=model[0]*ax+model[4]*ay+model[8]*az+model[12];
    dst[1]=model[1]*ax+model[5]*ay+model[9]*az+model[13];
    dst[2]=model[2]*ax+model[6]*ay+model[10]*az+model[14]; }
static void morphBlend(Pack*P,float*wts,int*wasAct){
    if(!P||!P->nMorph) return;
    { static const char*mf8=(const char*)1;      /* bench: pin one target, scene or not */
      if(mf8==(const char*)1) mf8=getenv("REVA_MORPHFORCE");
      /* name=v[,name=v...] (10-18: both eyelids at once) */
      for(const char*it8=mf8;it8&&*it8;){ char nm8[32]="jaw"; float v8=1.f; const char*end8=strchr(it8,',');
          const char*eq=strchr(it8,'='); if(eq&&end8&&eq>end8) eq=NULL;
          if(eq){ int k8=0; while(it8+k8<eq&&k8<31){ nm8[k8]=it8[k8]; k8++; } nm8[k8]=0; v8=(float)atof(eq+1); }
          else v8=(float)atof(it8);
          /* by NAME, so a shape split across primitives moves all of its pieces */
          morphSetName(P,wts,nm8,v8);
          it8=end8?end8+1:NULL; } }
    int act=0; for(int m9=0;m9<P->nMorph;m9++) if(fabsf(wts[m9])>1e-3f) act=1;
    if(getenv("REVA_MORPHDBG")){ static int n9=0; if(n9<3){ n9++;
        printf("[morph:call] nMorph %d act %d wasAct %d w=[%.2f %.2f %.2f %.2f]\n",
               P->nMorph,act,*wasAct,wts[0],wts[1],wts[2],wts[3]); fflush(stdout); } }
    if(!act&&!*wasAct) return;
    static float mbuf[900000];              /* 300k verts x 3 floats */
    for(unsigned mesh9=0;mesh9<P->nMeshes;mesh9++){
        unsigned lo9=0xffffffffu, hi9=0;
        for(int m9=0;m9<P->nMorph;m9++){
            if(P->morphMesh[m9]!=mesh9) continue;
            if(act&&fabsf(wts[m9])<=1e-3f) continue;    /* restoring: every target's range */
            if(P->morphLo[m9]<lo9) lo9=P->morphLo[m9];
            if(P->morphHi[m9]>hi9) hi9=P->morphHi[m9]; }
        if(lo9>hi9) continue;
        PMesh*M9=&P->meshes[mesh9]; unsigned nv9=M9->verts;
        if(hi9>=nv9) hi9=nv9?nv9-1:0;
        unsigned cnt9=hi9-lo9+1; if(cnt9>300000) cnt9=300000;
        memcpy(mbuf,(float*)(P->pk+M9->offPos)+(size_t)lo9*3,(size_t)cnt9*12);
        for(int m9=0;m9<P->nMorph;m9++){
            if(P->morphMesh[m9]!=mesh9) continue;
            float w9=wts[m9]; if(fabsf(w9)<=1e-3f) continue;
            unsigned char*sd9=P->pk+P->morphOff[m9];
            for(unsigned s9=0;s9<P->morphSparse[m9];s9++){
                unsigned vi9=*(unsigned*)(sd9+s9*16); float*dl9=(float*)(sd9+s9*16+4);
                if(vi9>=lo9&&vi9-lo9<cnt9){ float*d9=mbuf+(size_t)(vi9-lo9)*3;
                    d9[0]+=dl9[0]*w9; d9[1]+=dl9[1]*w9; d9[2]+=dl9[2]*w9; } } }
        glBindBuffer(GL_ARRAY_BUFFER,P->vbo[mesh9*6]);
        glBufferSubData(GL_ARRAY_BUFFER,(GLintptr)((size_t)lo9*12),(GLsizeiptr)((size_t)cnt9*12),mbuf);
        if(getenv("REVA_MORPHDBG")){ static int said9=0; if(said9<4){ said9++;
            GLenum e9=glGetError();
            printf("[morph] mesh %u verts %u  range %u..%u (%u)  vbo %u  err 0x%x  w=[%.2f %.2f %.2f %.2f]\n",
                   mesh9,M9->verts,lo9,hi9,cnt9,P->vbo[mesh9*6],e9,
                   wts[0],wts[1],wts[2],wts[3]); fflush(stdout); } } }
    *wasAct=act; }

static int morphIdx(Pack*P,const char*nm){
    if(!P) return -1;
    for(int m9=0;m9<P->nMorph;m9++) if(!strcmp(packStr(P,P->morphName[m9]),nm)) return m9;
    return -1; }

/* \u2b50 SET EVERY MORPH THAT CARRIES THIS NAME, not the first one.
 * glTF splits a mesh by MATERIAL and repeats each sculpted shape on every primitive, so one
 * authored shape arrives as several rpack morphs -- Anima's `jaw` is three (her lips, her
 * face and her teeth, 4873 + 14694 + 20725 deltas on three different meshes with three
 * different VBOs). Driving it through morphIdx moved her lips and left her jaw shut, and
 * the whole expression read as 1.58% of a face panel changing.
 * They cannot be merged in the baker -- they belong to different meshes -- so the NAME is
 * the handle, and a name means all of them. Returns how many it set. */
static int morphSetName(Pack*P,float*wts,const char*nm,float v){
    if(!P||!wts) return 0;
    int n9=0;
    for(int m9=0;m9<P->nMorph;m9++)
        if(!strcmp(packStr(P,P->morphName[m9]),nm)){ wts[m9]=v; n9++; }
    return n9; }

