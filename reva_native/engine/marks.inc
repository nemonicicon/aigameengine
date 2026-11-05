/* ==================== MARK EDITOR ===================================================
 * jacob: "man i could place them for you eaasier".
 * ⭐ AND HE CAN. Placing a face mark is a thing a person does by LOOKING, and I converged
 * on kaeli's eyes by bisecting render sweeps -- too wide, then too high, then too low --
 * while he could have pointed at them. face_calibrate.py cannot do it on her either (heavy
 * liner and a blue fringe defeat the darkest-texel rule), so the automatic path is out and
 * the person who can see is the right instrument.
 *
 *   REVA_MARKEDIT=1 + REVA_NOMOUSEGRAB=1
 *   1 2 3 4      choose eye_l / eye_r / mouth / chin
 *   LEFT CLICK   place it. The file is written on every click; nothing to save.
 *
 * ⭐ THE CLICK LANDS IN RIG SPACE. The ray hits her SKINNED triangles -- where she really is
 * on screen, in whatever pose she is in -- and the point returned is the barycentric blend
 * of that triangle's REST positions. A mark placed on a posed, tilted, walking body is
 * therefore still correct in the rest frame the pack stores, which is exactly the mistake
 * that cost me three rounds by hand. */
__attribute__((unused)) static int   g_markEdit=0, g_markSel=0;
static M4    g_markW[1024]; static float g_markM[16]; static Pack*g_markP=NULL;
static float g_markPt[4][3]; static int g_markGot[4]={0,0,0,0};
static const char*MARKNM[4]={"face_eye_l","face_eye_r","face_mouth","face_chin"};

/* ⭐ THE RAY, FROM THE CAMERA'S OWN BASIS. Exact, and no matrix inverse anywhere: forward
 * is eye->aim, right and up fall out of it, and the pixel's normalised offsets are scaled
 * by tan(fov/2). The first draft tried to read a ray out of the rows of the view-projection
 * without inverting it, which is how you get an answer that looks right and is not. */
__attribute__((unused)) static void markRay(float sx,float sy,int W,int H,const float*eye,const float*aim,
                    float fovDeg,float*dir){
    float f[3]; for(int k=0;k<3;k++) f[k]=aim[k]-eye[k];
    float fl=sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]); if(fl<1e-6f) fl=1.f;
    for(int k=0;k<3;k++) f[k]/=fl;
    float up[3]={0.f,1.f,0.f};
    float r[3]={f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0]};
    float rl=sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]); if(rl<1e-6f){ r[0]=1.f; r[1]=r[2]=0.f; rl=1.f; }
    for(int k=0;k<3;k++) r[k]/=rl;
    float u[3]={r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0]};
    float th=tanf(fovDeg*0.5f*0.0174532925f);
    float ndx=((2.f*sx/(float)W)-1.f)*th*(VIEW_ASPECT(W,H));
    float ndy=(1.f-(2.f*sy/(float)H))*th;
    for(int k=0;k<3;k++) dir[k]=f[k]+r[k]*ndx+u[k]*ndy;
    float dl=sqrtf(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]); if(dl<1e-6f) dl=1.f;
    for(int k=0;k<3;k++) dir[k]/=dl; }

/* ray vs the subject's skinned triangles; returns the REST position of the hit */
__attribute__((unused)) static int markPick(Pack*P,M4*world,const float*model,const float*ro,const float*rd,float*restOut){
    float bestT=1e18f; int got=0;
    for(unsigned mi=0;mi<P->nMeshes;mi++){
        PMesh*M=&P->meshes[mi];
        if(!(M->flags&4)||!M->boneCount||M->boneCount>MAXB||MOFF(P,mi)) continue;
        if(meshIsGarment(P,mi)) continue;   /* the skin, not the clothes (~cloth, ep 14 ~worn) */
        unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
        float*ibm=(float*)(P->pk+M->offIBM);
        float*rp=(float*)(P->pk+M->offPos);
        unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
        unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
        unsigned*ix=(unsigned*)(P->pk+M->offIdx);
        if(!M->idxCount) continue;
        static float pal[MAXB*16];
        for(unsigned b=0;b<M->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]],*(M4*)(ibm+b*16)); memcpy(pal+b*16,pm,64); }
        for(unsigned t=0;t+2<M->idxCount;t+=3){
            unsigned id[3]={ix[t],ix[t+1],ix[t+2]};
            float wv[3][3];
            for(int c=0;c<3;c++){
                unsigned v=id[c]; float acc[3]={0,0,0}, ws=0.f;
                for(int q=0;q<4;q++){ float w=wp[v*4+q]/255.f; if(w<=0.f) continue;
                    float*pm=pal+jp[v*4+q]*16;
                    for(int k=0;k<3;k++) acc[k]+=w*(pm[k]*rp[v*3]+pm[4+k]*rp[v*3+1]+pm[8+k]*rp[v*3+2]+pm[12+k]);
                    ws+=w; }
                if(ws<1e-4f){ acc[0]=rp[v*3]; acc[1]=rp[v*3+1]; acc[2]=rp[v*3+2]; }
                for(int k=0;k<3;k++) wv[c][k]=model[k]*acc[0]+model[4+k]*acc[1]+model[8+k]*acc[2]+model[12+k]; }
            /* Moller-Trumbore */
            float e1[3],e2[3],pv[3],tv[3],qv[3];
            for(int k=0;k<3;k++){ e1[k]=wv[1][k]-wv[0][k]; e2[k]=wv[2][k]-wv[0][k]; }
            pv[0]=rd[1]*e2[2]-rd[2]*e2[1]; pv[1]=rd[2]*e2[0]-rd[0]*e2[2]; pv[2]=rd[0]*e2[1]-rd[1]*e2[0];
            float det=e1[0]*pv[0]+e1[1]*pv[1]+e1[2]*pv[2];
            if(fabsf(det)<1e-9f) continue;
            float inv=1.f/det;
            for(int k=0;k<3;k++) tv[k]=ro[k]-wv[0][k];
            float u=(tv[0]*pv[0]+tv[1]*pv[1]+tv[2]*pv[2])*inv;
            if(u<0.f||u>1.f) continue;
            qv[0]=tv[1]*e1[2]-tv[2]*e1[1]; qv[1]=tv[2]*e1[0]-tv[0]*e1[2]; qv[2]=tv[0]*e1[1]-tv[1]*e1[0];
            float v2=(rd[0]*qv[0]+rd[1]*qv[1]+rd[2]*qv[2])*inv;
            if(v2<0.f||u+v2>1.f) continue;
            float tt=(e2[0]*qv[0]+e2[1]*qv[1]+e2[2]*qv[2])*inv;
            if(tt<=0.01f||tt>=bestT) continue;
            bestT=tt; got=1;
            /* ⭐ THE REST POSITION, not the world one -- that is what a mark IS. */
            for(int k=0;k<3;k++)
                restOut[k]=rp[id[0]*3+k]*(1.f-u-v2)+rp[id[1]*3+k]*u+rp[id[2]*3+k]*v2; } }
    return got; }

__attribute__((unused)) static void markWrite(const char*subj){
    char pth[256]; snprintf(pth,sizeof pth,"anim_bench/marks_%s.txt",subj?subj:"subject");
    FILE*f=fopen(pth,"wb"); if(!f) return;
    fprintf(f,"# placed by hand in the mark editor (REVA_MARKEDIT=1). Rig space.\n");
    fprintf(f,"# bake with: face_marks.py <pack> --from %s\n",pth);
    for(int i=0;i<4;i++) if(g_markGot[i])
        fprintf(f,"%s %.6f %.6f %.6f\n",MARKNM[i],g_markPt[i][0],g_markPt[i][1],g_markPt[i][2]);
    fclose(f);
    printf("[mark] wrote %s%c",pth,10); fflush(stdout); }

/* ⭐ EYES THAT AIM WITHOUT EYE BONES (09-11). jacob on the cut: "the eyes. holy shit the eyes
 * are so bad on most models." Reva's are painted and kaeli's are lid shells, but ANIMA
 * carries the ARKit eye-look shapes (eyeLookIn/Out/Up/Down, Left/Right) and they were
 * never wired. The same residual gaze eyeAim() gives a bone rig drives them here: yaw>0
 * is her RIGHT (lookTarget's frame), so the right eye looks Out and the left looks In.
 * A rig with neither bones nor shapes is skipped, not faked -- and says so once. */
static float g_gazeForce[2]={-9.f,0.f};   /* ctl gaze=yaw,pitch (radians) forces the hero's eyes, for the bench; -9 = off */
static void eyeMorphAim(Pack*P,float*wts,float dyaw,float dpitch){
    if(!P||!wts||!P->nMorph) return;
    if(morphIdx(P,"eyeLookOutLeft")<0){ static Pack*said[8]; static int ns=0; int k; for(k=0;k<ns;k++) if(said[k]==P) return;
        if(ns<8) said[ns++]=P;
        if(!P->eyesRig) printf("[eyes] %s has no eye bones and no eyeLook shapes -- the head aims alone\n",P->label);
        return; }
    float yr=dyaw/0.61f, pr=dpitch/0.35f;
    if(yr>1.f)yr=1.f;
    if(yr<-1.f)yr=-1.f;
    if(pr>1.f)pr=1.f;
    if(pr<-1.f)pr=-1.f;
    float R=yr>0.f?yr:0.f, L=yr<0.f?-yr:0.f, U=pr>0.f?pr:0.f, D=pr<0.f?-pr:0.f;
    morphSetName(P,wts,"eyeLookOutRight",R); morphSetName(P,wts,"eyeLookInLeft",R);
    morphSetName(P,wts,"eyeLookOutLeft",L);  morphSetName(P,wts,"eyeLookInRight",L);
    morphSetName(P,wts,"eyeLookUpLeft",U);   morphSetName(P,wts,"eyeLookUpRight",U);
    morphSetName(P,wts,"eyeLookDownLeft",D); morphSetName(P,wts,"eyeLookDownRight",D); }
/* ---- THE EXPOSURE LATCH ON THE FACE SOLVE (ROBOT NEKO ep 6) ---------------------
 * ep 1's law was written for the chains and the cloth: "a held exposure does not
 * integrate". The face was never brought under it, and it cost exactly what ep 4 and
 * ep 5 measured -- on a held pair the only thing that moves is a head and its hair.
 *
 * It is NOT a clock. The head layer has run on scnBodyT(), the latched exposure clock,
 * since ep 1. It is a FEEDBACK LOOP: lookTarget() aims each head at the OTHER body's
 * published eye mark, and that mark is republished out of last frame's FK. One rendered
 * frame is one iteration of that solve, so a held exposure goes on converging a pose the
 * shutter has already photographed -- a fraction of a degree at the neck, which is a
 * bright moving hairline on a close-up.
 *
 * So the face solve is latched to the exposure that owns it: on a held frame the
 * exposure's own result is replayed verbatim, joints and morph weights both. The clip
 * pose feeding in is already identical (latched clock), so replaying the OUTPUT is the
 * same statement as "do not solve again", and it catches every term at once -- the gaze
 * loop, the eye aim, the eye shapes, the jaw, the joint boil.
 *
 * ⛔ PER CAST SLOT, NOT PER `who`. scnHeadLayer is called with who=1 for EVERY cast
 * member -- the slot is g_castI. A cache keyed on `who` is the ep 2 bug (kaeli and mrs
 * parsons overwriting each other) wearing a fix's clothes.
 * REVA_NOHOLDLATCH=1 is the control run. */
#define HOLD_MAXN 512
static float g_holdQ[SCN_MAXCAST][HOLD_MAXN][4];
static float g_holdMW[SCN_MAXCAST][MORPH_MAX];
static long  g_holdIdx[SCN_MAXCAST];   /* the exposure each slot's cache belongs to */
static int   g_holdHas[SCN_MAXCAST];   /* nodes actually cached, 0 = nothing yet */
static int   g_holdOff=-1;             /* REVA_NOHOLDLATCH */
static int holdSlot(int who){ return who ? ((g_castI>=1&&g_castI<SCN_MAXCAST)?g_castI:1) : 0; }

/* ⭐ (10-23) THE PERFORMANCE LAYER. jacob, after the film pass's second sample: "Not yet. Keep refining the sample
 * first." Measured on it (a frame difference per shot): 68% of 2.4's frames and 43% of 2.3's did not change at all. In
 * a single only the mouth moved -- the old layer's drift was 3 degrees over 20 s, under a pixel a frame -- and its
 * look-at never ran for the show's cast: lookTarget wants face marks, which no code-built person carries, and it knew
 * two bodies (every partner aimed at the HERO; in 2.4 a boy lurking unseen at the door). Now, per scene SLOT:
 *  - WHO TO LOOK AT. A listener looks at the line's speaker; the speaker at the one it is said to (the line's `to=`,
 *    else the last other speaker, else the nearest body) and under-turns, the eyes finishing it; hlook=0 / plook=0
 *    still refuse. Head to head: the eye marks where a body has them, the head bone where it does not.
 *  - ON AN OFF-SCREEN VOICE'S LINE, OR AN ACTION'S, A BODY LOOKS ABOUT: a new place every 3-5 s, a third of the time
 *    back to what it was doing.
 *  - A TURN IS A SPRING (critically damped, ~0.45 s), never a snap -- integrated on the scene clock.
 *  - THE SPEAKER MOVES WITH ITS WORDS: a nod on each strong stress (the rise of its own voice's envelope convolved
 *    with a 0.4-s half sine, past the 80th percentile of 2.4's) and a turn and a tilt that drift with its voice and
 *    settle in its pauses; the chest turns a little with the head.
 *  - A LISTENER NODS as the speaker finishes (half the lines, by slot and line).
 *  - EVERY BODY BREATHES (the chest, 0.24 Hz), and none holds dead still.
 * Deterministic -- the scene clock, the lines and the slot; no rand() -- so [simhash] holds and a held exposure
 * replays. REVA_ACT=0 is the old layer (the control); REVA_ACTDBG=1 prints each slot's target as it changes. */
static int g_perf=-1, g_perfDbg=0;
static float g_perfS[SCN_MAXCAST][4];       /* per slot, the head's spring: yaw, pitch and their rates */
static float g_perfT[SCN_MAXCAST];          /* the scene time it was integrated to */
static int   g_perfHas[SCN_MAXCAST], g_perfTgt[SCN_MAXCAST];
static float g_perfF[SCN_MAXCAST][3];      /* per slot, where the CLIP's head faces, low-passed (the look-at's base) */
static void perfRot(const float*q,const float*v,float*o){   /* v turned by q: v + w t + u x t, t = 2 u x v */
    float tx=2.f*(q[1]*v[2]-q[2]*v[1]), ty=2.f*(q[2]*v[0]-q[0]*v[2]), tz=2.f*(q[0]*v[1]-q[1]*v[0]);
    o[0]=v[0]+q[3]*tx+(q[1]*tz-q[2]*ty); o[1]=v[1]+q[3]*ty+(q[2]*tx-q[0]*tz); o[2]=v[2]+q[3]*tz+(q[0]*ty-q[1]*tx); }
/* where the head faces in the rig's frame (+Z forward, the body's own): the rig's forward as the head bone held it at
 * rest, carried through the pose's rotations, root to head */
static int perfHeadFwd(Pack*P,float Q[][4],float*o){
    int ch[64], n=0;
    for(int i=P->pb[5];i>=0&&n<64;i=P->nodes[i].parent) ch[n++]=i;
    if(!n) return 0;
    float Rc[4]={0.f,0.f,0.f,1.f}, Rr[4]={0.f,0.f,0.f,1.f}, c[4], fl[3];
    const float Z[3]={0.f,0.f,1.f};
    for(int k=n-1;k>=0;k--){ qmul(Rc,Rc,Q[ch[k]]); qmul(Rr,Rr,P->nodes[ch[k]].q); }
    qconj(c,Rr); perfRot(c,Z,fl); perfRot(Rc,fl,o);
    float l=sqrtf(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]); if(l<1e-5f) return 0;
    o[0]/=l; o[1]/=l; o[2]/=l; return 1; }
static void perfInit(void){
    const char*e9=getenv("REVA_ACT"); g_perf=!(e9&&e9[0]=='0');
    e9=getenv("REVA_ACTDBG"); g_perfDbg=(e9&&e9[0]!='0');
    for(int s=0;s<SCN_MAXCAST;s++){ g_perfHas[s]=0; g_perfTgt[s]=-9; }
    printf("[act] the performance layer %s\n",g_perf?"ON -- heads find who is talking, speakers move with their words, "
           "the rest look about and breathe (REVA_ACT=0 is the control)":"OFF (REVA_ACT=0): the old head layer");
    fflush(stdout); }
static float perfRnd(int s,int k,int salt){
    unsigned h=(unsigned)s*0x9E3779B1u^(unsigned)k*0x85EBCA77u^(unsigned)salt*0xC2B2AE3Du;
    h^=h>>16; h*=0x7FEB352Du; h^=h>>15; h*=0x846CA68Bu; h^=h>>16;
    return (float)(h>>8)/16777216.f; }
/* where slot s looks FROM and is looked AT: between its eye marks, else its head bone */
static int perfEye(int s,float*o){
    if(s<0||s>=SCN_BODIES) return 0;   /* a slot is in the scene when it has a body */
    if(s==0){ if(g_heroFaceOK){ memcpy(o,g_heroEyeW,12); return 1; } }
    else { if(!g_cast[s].pk) return 0;
           if(g_cast[s].faceOK){ memcpy(o,g_cast[s].eyeW,12); return 1; } }
    if(castHeadOK(s)){ memcpy(o,castHead(s),12); return 1; }
    return 0; }
static float perfEnv(const ScnLine*L,float t){
    if(!L||L->silent||L->nEnv<2) return 0.f;
    float f=(t-L->t0)*30.f; if(f<0.f||f>=(float)(L->nEnv-1)) return 0.f;
    int k=(int)f; float u=f-(float)k, v=((float)L->env[k]*(1.f-u)+(float)L->env[k+1]*u)/100.f;
    return v>1.f?1.f:v; }
/* a line's strong stresses, 0..1.2: each rise of the voice (over 0.1 s) convolved with a 0.4-s half sine, less 1.7 --
 * the 80th percentile of 2.4's (the median is 1.0: every syllable rises; only the stresses rise that far) */
static float perfStress(const ScnLine*L,float t){
    float s=0.f;
    for(int k=0;k<12;k++){ float tau=((float)k+0.5f)/30.f, r=perfEnv(L,t-tau)-perfEnv(L,t-tau-0.10f);
        if(r>0.f) s+=r*sinf(3.14159f*tau/0.40f); }
    s=(s-1.7f)*1.25f; return s<0.f?0.f:(s>1.2f?1.2f:s); }
static void perfLayer(Pack*P,float Q[][4],int who,int li,float t){
    int me=holdSlot(who);
    if(!P||me<0||me>=SCN_MAXCAST) return;
    /* the line framing this moment: the one being said, else the last begun (a gap keeps its look) */
    int lk=li;
    if(lk<0) for(int i=0;i<g_scnN;i++) if(g_scnL[i].t0<=t) lk=i;
    const ScnLine*L=(li>=0)?&g_scnL[li]:NULL, *LK=(lk>=0)?&g_scnL[lk]:NULL;
    int spk=LK?LK->spk:-1;
    int speaking=(L&&L->spk==me&&!L->silent);
    int refuse=(g_scnT>=0.f&&!g_scnLook[me?1:0])||g_prd[me].pon||!g_lookOn;
    /* WHO */
    int tgt=-1;
    if(LK&&!refuse&&!LK->silent){
        if(spk>=0&&spk<SCN_BODIES&&spk!=me) tgt=spk;                          /* listening */
        else if(spk==me){                                                      /* saying it: to whom */
            if(LK->to>=0&&LK->to<SCN_BODIES&&LK->to!=me) tgt=LK->to;
            for(int i=lk-1;i>=0&&tgt<0;i--){ int s=g_scnL[i].spk; if(s>=0&&s<SCN_BODIES&&s!=me&&!g_scnL[i].silent) tgt=s; }
            if(tgt<0){ float E[3],F[3],best=1e9f;
                if(perfEye(me,E)) for(int s=0;s<SCN_BODIES;s++){ if(s==me||!perfEye(s,F)) continue;
                    float d=(F[0]-E[0])*(F[0]-E[0])+(F[2]-E[2])*(F[2]-E[2]); if(d<best){ best=d; tgt=s; } } } } }
    float yawT=0.f, pitchT=0.f; int aimed=0, about=0;
    float dt0=t-g_perfT[me]; int fresh=(!g_perfHas[me]||dt0<0.f||dt0>0.75f);
    /* ⭐ THE LOOK-AT IS ABSOLUTE. An offset on the clip's head was not a look: a sitting clip holds the head down at
     * its console, and "look at Marie" came out as a look at Marie's lap (2.4's first take). Where the CLIP's head faces
     * -- its rotations carried root to head, low-passed over 1.2 s so the clip's own nods still play -- is corrected to
     * where the other one's head is. */
    { float fc[3], *FD=g_perfF[me];
      if(perfHeadFwd(P,Q,fc)){
          if(fresh){ memcpy(FD,fc,12); }
          else if(dt0>0.f){ float a=1.f-expf(-dt0/1.2f); for(int k=0;k<3;k++) FD[k]+=(fc[k]-FD[k])*a;
              float l=sqrtf(FD[0]*FD[0]+FD[1]*FD[1]+FD[2]*FD[2]); if(l>1e-5f) for(int k=0;k<3;k++) FD[k]/=l; } }
      else if(fresh){ FD[0]=0.f; FD[1]=0.f; FD[2]=1.f; } }
    { float E[3],F[3]; const int pt=(me<STK_LOOKS&&me<g_stkNK&&g_stkLook[me][3]>0.5f);   /* (step 21) the set's timeline has a place for this body to look: it beats who's talking, and a body told not to look */
      if(pt){ F[0]=g_stkLook[me][0]; F[1]=g_stkLook[me][1]; F[2]=g_stkLook[me][2]; }
      if((pt||(tgt>=0&&perfEye(tgt,F)))&&perfEye(me,E)){
          float by=who?g_partYaw:g_rvHead, dx=F[0]-E[0], dy=F[1]-E[1], dz=F[2]-E[2], d=sqrtf(dx*dx+dz*dz);
          if(d>1e-4f){ float f=dx*sinf(by)+dz*cosf(by), r=dx*cosf(by)-dz*sinf(by);
              const float*FD=g_perfF[me];
              float yc=atan2f(FD[0],FD[2]), pc=atan2f(FD[1],sqrtf(FD[0]*FD[0]+FD[2]*FD[2]));
              yawT=atan2f(r,f)-yc; pitchT=(atan2f(dy,d)-pc)*LOOK_PITCH_SIGN; aimed=1;
              if(yawT>3.14159f) yawT-=6.28318f;
              if(yawT<-3.14159f) yawT+=6.28318f;
              if(yawT>0.70f) yawT=0.70f;
              if(yawT<-0.70f) yawT=-0.70f;
              { const float pm=pt?0.55f:0.35f;   /* a place the track gave can be well above or below: a body looks up at something tall */
                if(pitchT>pm) pitchT=pm;
                if(pitchT<-pm) pitchT=-pm; } } } }
    if(!aimed&&!refuse){   /* NOBODY TO LOOK AT: look about */
        float Pd=3.2f+0.7f*(float)me; int k=(int)floorf(t/Pd); float u=t-(float)k*Pd;
        int kk=(u<perfRnd(me,k,5)*0.5f*Pd)?k-1:k;
        if(perfRnd(me,kk,3)>=0.34f){ yawT=(perfRnd(me,kk,1)-0.5f)*0.85f; pitchT=(perfRnd(me,kk,2)-0.6f)*0.28f;
            /* ⭐ CHEAT TO THE LENS: a look about turned away from a close lens gave a close-up of the back of a boy's head
             * (2.3, at his mirror). Looking at nothing in particular, a body looks to the CAMERA's side of the room, as an
             * actor cheats out -- not into the lens -- unless the lens is behind it. */
            float E[3];
            if(g_camOn&&perfEye(me,E)){ float by=who?g_partYaw:g_rvHead, dx=g_camPos[0]-E[0], dz=g_camPos[2]-E[2];
                float cy=atan2f(dx*cosf(by)-dz*sinf(by),dx*sinf(by)+dz*cosf(by));
                if(fabsf(cy)<2.2f){ if(yawT*cy<0.f) yawT=-yawT;
                    yawT=0.6f*yawT+0.25f*cy; if(yawT>0.62f) yawT=0.62f; if(yawT<-0.62f) yawT=-0.62f; } } }
        about=1; }
    if(g_perfDbg&&tgt!=g_perfTgt[me]){ char b9[16]=""; if(tgt>=0) snprintf(b9,sizeof b9," %d",tgt);
        printf("[act] t %.2f slot %d %s%s%s\n",t,me,tgt>=0?"looks at slot":(refuse?"refuses to look":"looks about"),b9,
               (tgt>=0&&!aimed)?" -- but has no head to find":""); fflush(stdout); }
    g_perfTgt[me]=tgt;
    /* THE TURN: a critically damped spring on the scene clock */
    float wgt=speaking?0.72f:1.f, hy=yawT*wgt, hp=pitchT*wgt, *S=g_perfS[me];
    float dt=t-g_perfT[me];
    if(!g_perfHas[me]||dt<0.f||dt>0.75f){ S[0]=hy; S[1]=hp; S[2]=S[3]=0.f; g_perfHas[me]=1; }
    else if(dt>0.f){ const float W=10.f; int n=(int)ceilf(dt/0.004f); if(n>200) n=200; float h=dt/(float)n;
        for(int i=0;i<n;i++){ S[2]+=h*(W*W*(hy-S[0])-2.f*W*S[2]); S[3]+=h*(W*W*(hp-S[1])-2.f*W*S[3]);
                              S[0]+=h*S[2]; S[1]+=h*S[3]; } }
    g_perfT[me]=t;
    if(g_perfDbg){ static float said9[SCN_MAXCAST]; if(t-said9[me]>=1.f||t<said9[me]){ said9[me]=t;
        const float*FD=g_perfF[me];
        printf("[act] t %.1f slot %d clip head yaw %+.0f pitch %+.0f (up)  ask yaw %+.0f pitch %+.0f (down)  head at %+.0f %+.0f%s\n",
               t,me,atan2f(FD[0],FD[2])*57.3f,atan2f(FD[1],sqrtf(FD[0]*FD[0]+FD[2]*FD[2]))*57.3f,yawT*57.3f,pitchT*57.3f,
               S[0]*57.3f,S[1]*57.3f,aimed?"":(about?"  (looking about)":"")); fflush(stdout); } }
    float yaw=S[0], pitch=S[1], roll=0.f, sway=0.f;
    /* THE SPEAKER'S WORDS */
    if(speaking){
        float va=0.f; for(int k=0;k<15;k++) va+=perfEnv(L,t-(float)k/30.f);
        va=va/15.f*2.4f; if(va>1.f) va=1.f;                                   /* the voice over the last half second */
        float ph=(float)me*1.7f+(float)li*0.9f;
        float dy=va*(0.080f*sinf(1.21f*t+ph)+0.035f*sinf(2.63f*t+2.f*ph));
        yaw+=dy; sway=dy;
        roll+=va*0.030f*sinf(0.97f*t+1.3f*ph);   /* (a 0.05 tilt on a clip's own read as dozing off) */
        pitch+=va*0.020f*sinf(1.73f*t+0.7f*ph)+0.075f*perfStress(L,t); }      /* positive pitch: the face DOWN */
    /* THE LISTENER'S NOD, as the speaker finishes */
    else if(aimed){
        for(int i=(lk>0?lk-1:0);i<=lk&&i<g_scnN;i++){ const ScnLine*Li=&g_scnL[i];
            if(Li->silent||Li->spk==me||Li->spk<0||Li->spk>=SCN_BODIES) continue;
            float u=t-(Li->t0+Li->dur);
            if(u>=0.f&&u<0.7f&&perfRnd(me,i,9)<0.5f) pitch+=0.060f*sinf(3.14159f*u/0.7f); } }
    /* NOBODY HOLDS DEAD STILL */
    yaw  +=0.022f*sinf(0.37f*t+(float)me*2.1f);
    pitch+=0.012f*sinf(0.53f*t+(float)me*1.3f);
    roll +=0.010f*sinf(0.29f*t+(float)me*0.7f);
    float off[4]; int nk=P->pb[4], hd=P->pb[5];
    if(nk>=0){ qaxis(off,1,0,0,pitch*0.45f); qmul(Q[nk],Q[nk],off); qaxis(off,0,1,0,yaw*0.45f); qmul(Q[nk],Q[nk],off);
               qaxis(off,0,0,1,roll*0.45f); qmul(Q[nk],Q[nk],off); }
    if(hd>=0){ qaxis(off,1,0,0,pitch*0.55f); qmul(Q[hd],Q[hd],off); qaxis(off,0,1,0,yaw*0.55f); qmul(Q[hd],Q[hd],off);
               qaxis(off,0,0,1,roll*0.55f); qmul(Q[hd],Q[hd],off); }
    /* THE EYES LEAD: on the target while the head is still on its way there, and steady against the nods */
    if(aimed||(about&&(yawT!=0.f||pitchT!=0.f))){
        float ey=yawT-yaw, ep=pitchT-pitch;
        eyeAim(P,Q,ey,ep);
        eyeMorphAim(P,who?g_partMorphW:g_heroMorphW,ey,ep*LOOK_PITCH_SIGN); }
    /* THE BREATH, and the chest turning a little with a speaker's head */
    { int ch=(P->pb[3]>=0)?P->pb[3]:P->pb[2];
      float br=sinf(6.2832f*0.24f*t+(float)me*1.9f);
      if(ch>=0){ qaxis(off,1,0,0,-0.014f*br); qmul(Q[ch],Q[ch],off);
                 if(sway!=0.f){ qaxis(off,0,1,0,0.22f*sway); qmul(Q[ch],Q[ch],off); } } } }
static void scnHeadLayer(Pack*P,float Q[][4],int who){
    if(g_holdOff<0){ const char*e9=getenv("REVA_NOHOLDLATCH"); g_holdOff=(e9&&e9[0]!='0');
        if(g_holdOff){ printf("[hold] face latch OFF (REVA_NOHOLDLATCH) -- the solve re-runs on held exposures\n"); fflush(stdout); } }
    int sl9=holdSlot(who);
    float*MW9 = who ? g_partMorphW : g_heroMorphW;
    /* a held frame replays the exposure that owns it, and solves nothing */
    if(!g_holdOff&&g_stepFps>0.f&&g_stepHeld&&P&&g_scnT>=0.f&&g_holdHas[sl9]>0&&g_holdIdx[sl9]==g_stepIdx){
        unsigned n9=P->nNodes; if(n9>(unsigned)g_holdHas[sl9]) n9=(unsigned)g_holdHas[sl9];
        memcpy(Q,g_holdQ[sl9],(size_t)n9*16);
        if(MW9) memcpy(MW9,g_holdMW[sl9],sizeof(float)*MORPH_MAX);
        { static long nrep9[SCN_MAXCAST];
          if(nrep9[sl9]++==0||(nrep9[sl9]%240)==0)
              printf("[hold] slot %d: face solve replayed on %ld held exposure frame(s) (%s, %u nodes)\n",
                     sl9,nrep9[sl9],P->label,n9), fflush(stdout); }
        return; }
    if(g_scnT<0.f||!P){
        /* no scene: the jaw still has to answer the bench override, or a forced open
         * would do nothing outside a conversation */
        if(P) jawLayer(P,Q,who,0.f);
        if(P&&who==0&&g_gazeForce[0]>-8.f){ eyeAim(P,Q,g_gazeForce[0],g_gazeForce[1]); eyeMorphAim(P,g_heroMorphW,g_gazeForce[0],g_gazeForce[1]); }
        return; }
    int nk=P->pb[4], hd=P->pb[5];
    float t=scnBodyT();
    int li=scnActive();
    if(g_perf<0) perfInit();
    /* ⭐ (10-23) the SLOT, not `who` -- who is 1 for every partner: slot 3's jaw opened on slot 1's words */
    int speaking=(li>=0&&g_scnL[li].spk==(g_perf?holdSlot(who):who));
    float amp=(li>=0)?scnMouth(&g_scnL[li],t):0.f;
    float off[4];
    if(g_perf) perfLayer(P,Q,who,li,t);
    else {   /* the old head layer: REVA_ACT=0, the control */
    float ph=t*0.6f+(float)who*2.1f;
    float yaw=sinf(ph*0.53f)*0.055f, pitch=sinf(ph*0.37f)*0.035f;
    if(speaking){ pitch-=0.075f*amp; yaw+=sinf(t*2.1f+who)*0.035f*amp; }
    else        { pitch+=0.045f*(0.5f+0.5f*sinf(t*0.75f+(float)who*1.7f)); }
    /* AIM AT THE OTHER ONE. The listener holds it; the speaker looks away a little
     * more, which is what people do. The idle drift above is kept underneath at a
     * quarter strength so the head still breathes instead of locking like a turret. */
    { float ly,lp;
      if(lookTarget(who,who?g_partYaw:g_rvHead,&ly,&lp)){
          float wgt = speaking?0.72f:1.0f;
          yaw   = yaw*0.25f   + ly*wgt;
          pitch = pitch*0.25f + lp*wgt*LOOK_PITCH_SIGN;
          /* ⭐ AND THE EYES TAKE WHAT THE HEAD LEFT. wgt<1 means the head deliberately
           * stops short of the target; that remainder is the eye aim, so a speaker who
           * turns her head only three quarters of the way still has her gaze ON the
           * person -- which is what actually reads as looking at someone. */
          eyeAim(P,Q,ly*(1.f-wgt),lp*(1.f-wgt)*LOOK_PITCH_SIGN);
          eyeMorphAim(P,who?g_partMorphW:g_heroMorphW,ly*(1.f-wgt),lp*(1.f-wgt));   /* ⭐ and the shapes, on a rig that has them (09-11) */
          /* THE CHECK THAT MATTERS is not "did the head move" -- it is "is she facing
           * her". The face's own forward comes from the eye marks after FK, so this
           * compares the result against the target rather than against the input. */
          if(g_faceDbg>0&&!who){ static double lt9=-9; if(g_scnT-lt9>1.0){ lt9=g_scnT;
              const float*me=g_heroEyeW,*them=g_partEyeW;
              float dx=them[0]-me[0], dz=them[2]-me[2];
              float dl=sqrtf(dx*dx+dz*dz); if(dl>1e-5f){ dx/=dl; dz/=dl;
                  float dot=g_heroFaceFwd[0]*dx+g_heroFaceFwd[2]*dz;
                  if(dot>1.f)dot=1.f;
                  if(dot<-1.f)dot=-1.f;
                  printf("[look] t %.1f  hero face-to-partner OFF BY %.1f deg  (yaw ask %.1f)\n",
                         g_scnT,acosf(dot)*57.2958f,ly*57.2958f); fflush(stdout); } } } } }
    if(nk>=0){ qaxis(off,1,0,0,pitch*0.45f); qmul(Q[nk],Q[nk],off);
               qaxis(off,0,1,0,yaw*0.45f);   qmul(Q[nk],Q[nk],off); }
    if(hd>=0){ qaxis(off,1,0,0,pitch*0.55f); qmul(Q[hd],Q[hd],off);
               qaxis(off,0,1,0,yaw*0.55f);   qmul(Q[hd],Q[hd],off); }
    }   /* the old head layer */
    /* ⭐ THE ANIMATOR'S HAND, IN THE JOINTS. Ep 1's boil moved the whole puppet a hair and
     * nobody could see it: a root offset of +-0.005 is a pixel or two in a wide. A puppet's
     * boil is in the JOINTS -- every exposure the neck, the head and the arms are set down a
     * fraction of a degree off where they were. Exposure-seeded, so a held frame holds it;
     * scaled by boil=; off whenever stop motion is. */
    if(g_stepFps>0.f&&g_boil>0.f){
        static const int   BB[6]={4,5,7,8,10,11};                /* neck head upperarm forearm x2 */
        static const float BA[6]={0.010f,0.012f,0.016f,0.014f,0.016f,0.014f};
        for(int b9=0;b9<6;b9++){ int bn=P->pb[BB[b9]]; if(bn<0) continue;
            float o9[4];
            qaxis(o9,1,0,0,stepBoil(who*16+b9,5)*BA[b9]); qmul(Q[bn],Q[bn],o9);
            qaxis(o9,0,1,0,stepBoil(who*16+b9,6)*BA[b9]); qmul(Q[bn],Q[bn],o9);
            qaxis(o9,0,0,1,stepBoil(who*16+b9,7)*BA[b9]); qmul(Q[bn],Q[bn],o9); } }
    /* THE JAW RIDES THE SPEAKER'S OWN LOUDNESS -- and only the speaker's: a listener
     * with a moving jaw is what made the first conversation read as two people
     * chewing at each other. */
    jawLayer(P,Q,who,speaking?amp:0.f);
    /* ...and this exposure's answer is what every held frame after it replays */
    if(!g_holdOff&&g_stepFps>0.f&&!g_stepHeld){
        if(P->nNodes>HOLD_MAXN){ static Pack*said9[8]; static int ns9=0; int k9,seen9=0;
            for(k9=0;k9<ns9;k9++) if(said9[k9]==P) seen9=1;
            if(!seen9){ if(ns9<8) said9[ns9++]=P;
                printf("[hold] %s has %u nodes > HOLD_MAXN %d -- her face is NOT latched on a hold (raise it)\n",
                       P->label,P->nNodes,HOLD_MAXN); fflush(stdout); }
            g_holdHas[sl9]=0; return; }
        memcpy(g_holdQ[sl9],Q,(size_t)P->nNodes*16);
        if(MW9) memcpy(g_holdMW[sl9],MW9,sizeof(float)*MORPH_MAX);
        g_holdHas[sl9]=(int)P->nNodes; g_holdIdx[sl9]=g_stepIdx; } }

