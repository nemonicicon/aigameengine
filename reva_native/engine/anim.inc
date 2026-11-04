/* ================= S4b THE DONOR BANK ==============================================
 * The census says the building owns 4 runs, 1 attack, 1 down across 133 packs -- but
 * kaeli's ZAMN ingest carries a whole action set (w_run / x_gunfire / x_down). One
 * canon-mapped rig DONATES: its clip rotations replay on any other canon-mapped rig
 * as rest-DELTAS (delta = inv(restDonor) * Qdonor, applied on the target's rest --
 * the same local-offset layering the genome uses). Rotation-only: the kaeli lesson. */
/* S4f DONOR BANK v2: role -> (pack, clip), elected by EXACT clip name across the banks
 * in priority order (mob1 mocap first, kaeli second). Walk variety rotates per slot;
 * the scared walks are the panic layer's authored body. */
static Pack*g_dnGunP=NULL;  static int g_dnGun=-1;
static Pack*g_dnDownP=NULL; static int g_dnDown=-1;
static Pack*g_dnIdleP=NULL; static int g_dnIdle=-1;
static Pack*g_dnRunP=NULL;  static int g_dnRun=-1;
static Pack*g_dnFleeP=NULL; static int g_dnFlee=-1, g_dnFlee2=-1;
static Pack*g_dnWalksP[8]; static int g_dnWalks[8]; static int g_dnNWalks=0;
/* S4g: the sets -- a standing body picks from the idle+fidget set (rotating slowly, so
 * a hundred people waiting are not one loop a hundred times), a chatting pair plays a
 * conversation loop instead of standing there. Both were baked in S4f and left unwired. */
static Pack*g_dnIdlesP[6]; static int g_dnIdles[6]; static int g_dnNIdles=0;
static Pack*g_dnConvoP[4]; static int g_dnConvo[4]; static int g_dnNConvo=0;
static void dnElect(Pack*pk){
    if(!pk||!pk->nClips||pk->pbOK<10) return;
    for(unsigned c=0;c<pk->nClips;c++){ const char*nm=packStr(pk,pk->clips[c].name);
        if(!strcmp(nm,"x_gunfire")){ if(!g_dnGunP){ g_dnGunP=pk; g_dnGun=(int)c; } }
        else if(!strcmp(nm,"x_down")){ if(!g_dnDownP){ g_dnDownP=pk; g_dnDown=(int)c; } }
        else if((!strcmp(nm,"idle")||!strncmp(nm,"x_fidget",8))
                &&clipLoopsOK(pk,&pk->clips[c],pk->label,nm)){
            if(!strcmp(nm,"idle")&&!g_dnIdleP){ g_dnIdleP=pk; g_dnIdle=(int)c; }
            if(g_dnNIdles<6){ g_dnIdlesP[g_dnNIdles]=pk; g_dnIdles[g_dnNIdles]=(int)c; g_dnNIdles++; } }
        else if(!strncmp(nm,"x_convo",7)){
            if(g_dnNConvo<4){ g_dnConvoP[g_dnNConvo]=pk; g_dnConvo[g_dnNConvo]=(int)c; g_dnNConvo++; } }
        else if(!strcmp(nm,"w_run")){ if(!g_dnRunP){ g_dnRunP=pk; g_dnRun=(int)c; } }
        else if(!strcmp(nm,"x_flee")){ if(!g_dnFleeP||g_dnFleeP==pk){ g_dnFleeP=pk; g_dnFlee=(int)c; } }
        else if(!strcmp(nm,"x_flee2")){ if(!g_dnFleeP||g_dnFleeP==pk){ g_dnFleeP=pk; g_dnFlee2=(int)c; } }
        else if((!strcmp(nm,"w_walk")||!strncmp(nm,"v_walk",6))&&g_dnNWalks<8){
            g_dnWalksP[g_dnNWalks]=pk; g_dnWalks[g_dnNWalks]=(int)c; g_dnNWalks++; } } }
/* ================= Z4 THE BANKS: a clip is addressable across packs =================
 * dnElect above wires clips to ROLES the crowd asks for by behaviour. A set file asks
 * the other way round -- by NAME, for one actor -- and until now a clip name resolved
 * only inside the actor's own pack, which is why a 900-vert town citizen could do
 * nothing but the walk and idle it shipped with.
 *
 * bankFind() closes that: name a clip that lives in ANY loaded bank and the actor plays
 * it through the canonical transfer. The zombie ingest is the first customer -- the film
 * says "the zombies ARE the neighbours", and this is that sentence in code: the same
 * citizen body, the same set line, a different clip name. */
/* ⛔ WAS 4, AND bankAdd DROPPED THE FIFTH IN SILENCE. Adding v1_kmoves as a bank (so the
 * two-body room repertoire reaches any body) made five: mob1, kaeli, moves, kmoves,
 * zombies -- and the zombie bank, added last, fell off the end without a word. Same
 * shape as MAXB and MORPH_MAX: a cap nobody measured, failing quietly. */
#define NBANK 8
static Pack*g_bankP[NBANK]; static int g_nBank=0;
static void bankAdd(Pack*p){
    if(!p||!p->nClips) return;
    if(g_nBank>=NBANK){ printf("[bank] %s NOT ADDED -- NBANK=%d is full\n",p->label,NBANK); return; }
    g_bankP[g_nBank++]=p; }
static Pack*bankFind(const char*nm,int*ci){
    for(int b=0;b<g_nBank;b++){ Pack*p=g_bankP[b];
        for(unsigned c=0;c<p->nClips;c++)
            if(!strcmp(packStr(p,p->clips[c].name),nm)){ *ci=(int)c; return p; } }
    return NULL; }
/* THE ZOMBIE BANK (v1_zmoves: 32 ZAMN clips on the UE5 mannequin). Held separately from
 * the donor roles on purpose -- a neighbour walking to her mailbox must never be handed
 * a shamble. Nothing here is elected; it is only ever asked for by name. */
static Pack*g_zombP=NULL;
/* ================= P1.5 THE INTERPOLATING SAMPLER (engine rung E1) ==================
 * Every clip in the club was sampled NEAREST-FRAME -- ((unsigned)phase)%frames, then a
 * raw memcpy -- so at an 18fps bake against a ~100fps render every pose was HELD ~5
 * render frames: a systemic stair-step under all 133 packs. Worse, that memcpy loop was
 * copy-pasted at EIGHT sites, because in a file this size rewriting four lines beats
 * finding the other seven (measured 2026-08-26; the single-file disease). One sampler
 * now: fractional phase, qnlerp between adjacent frames (adjacent-frame nlerp is slerp
 * for all practical purposes, and qnlerp already carries the hemisphere flip + the squid
 * cure). Deliberately NOT converted: the audits (gaitRate, clipUpright, donorOK,
 * standArmsOK) -- they measure the BAKED frames, not playback -- and the genome's frozen
 * stance frames, where frac is 0 by construction. */
static void clipSampleF(Pack*P,PClip*c,double ph,float(*T)[3],float(*Q)[4],float(*S)[3]){
    if(!c->frames) return;
    double fp=fmod(ph,(double)c->frames); if(fp<0) fp+=c->frames;
    unsigned f0=(unsigned)fp, f1=(f0+1)%c->frames; float ft=(float)(fp-(double)f0);
    unsigned char*tr=P->pk+c->offTracks;
    for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(node>=P->nNodes) continue;
        float*d=(float*)(P->pk+offD);
        if(type==0) qnlerp(Q[node],d+(size_t)f0*4,d+(size_t)f1*4,ft);
        else if(type==3){ /* H6: morph weight, 1 float/frame, interpolated into the sink */
            if(g_morphSink&&node<(unsigned)g_morphSinkN)
                g_morphSink[node]=d[f0]+(d[f1]-d[f0])*ft; }
        else { float*a=d+(size_t)f0*3,*b=d+(size_t)f1*3,*o=(type==1)?T[node]:S[node];
               o[0]=a[0]+(b[0]-a[0])*ft; o[1]=a[1]+(b[1]-a[1])*ft; o[2]=a[2]+(b[2]-a[2])*ft; } } }
static void donorPoseF(Pack*dp,int clip,double ph,Pack*tp,float(*Q)[4]){
    PClip*c=&dp->clips[clip];
    if(!c->frames) return;
    double fp=fmod(ph,(double)c->frames); if(fp<0) fp+=c->frames;
    unsigned f0=(unsigned)fp, f1=(f0+1)%c->frames; float ft=(float)(fp-(double)f0);
    unsigned char*tr=dp->pk+c->offTracks;
    /* ================= P5: ABSOLUTE canonical transfer =================================
     * The old path composed the donor's LOCAL rest-delta onto the target's rest -- only
     * valid when both rigs share rest conventions (it mangled reva's run, flung borrowed
     * arms out sideways, and read "raise your arm 31 deg from wherever YOUR rest is").
     * Now: pose the DONOR rig fully, take each canon bone's WORLD delta D, and put the
     * target's canonical frame WHERE THE DONOR'S IS, facing-normalized:
     *     W_t' = G_t * inv(G_d) * D * A_d * inv(A_t) * W_t
     * locals recovered parents-first through the canon-ancestor linkage. A move now
     * means the same POSE on every body. Proven in Python (retarget.py) before landing
     * here: kaeli and reva hit authored targets to 0.1 degree. */
    if(dp->cbOK&&tp->cbOK&&dp->nNodes<=512){
        static float dq[512][4], dwq[512][4];
        for(unsigned i=0;i<dp->nNodes;i++) memcpy(dq[i],dp->nodes[i].q,16);
        for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
            if(type!=0||node>=dp->nNodes) continue;
            float*d=(float*)(dp->pk+offD);
            qnlerp(dq[node],d+(size_t)f0*4,d+(size_t)f1*4,ft); }
        for(unsigned i=0;i<dp->nNodes;i++){ int pa=dp->nodes[i].parent;
            if(pa<0) memcpy(dwq[i],dq[i],16); else qmul(dwq[i],dwq[pa],dq[i]); }
        float GG[4],gi[4]; qconj(gi,dp->cbG); qmul(GG,tp->cbG,gi);
        float wt[NCANON][4]; int wtv[NCANON];
        for(int c9=0;c9<NCANON;c9++) wtv[c9]=0;
        for(int c9=0;c9<NCANON;c9++){
            int db=dp->pb[c9], tb=tp->pb[c9];
            if(db<0||tb<0||!dp->cbV[c9]||!tp->cbV[c9]) continue;
            float wc[4],D[4],t1[4],t2[4],t3[4],t4[4],Wp[4];
            qconj(wc,dp->cbW[c9]); qmul(D,dwq[db],wc);
            qmul(t1,GG,D); qmul(t2,t1,dp->cbA[c9]);
            qconj(t3,tp->cbA[c9]); qmul(t4,t3,tp->cbW[c9]);
            qmul(Wp,t2,t4);
            memcpy(wt[c9],Wp,16); wtv[c9]=1;
            float Pw[4];
            int a=tp->cbAnc[c9];
            if(a>=0&&wtv[a]) qmul(Pw,wt[a],tp->cbPre[c9]);
            else memcpy(Pw,tp->cbWpar[c9],16);
            float pi[4]; qconj(pi,Pw); qmul(Q[tb],pi,Wp);
            { float l9=sqrtf(Q[tb][0]*Q[tb][0]+Q[tb][1]*Q[tb][1]+Q[tb][2]*Q[tb][2]+Q[tb][3]*Q[tb][3]);
              if(l9>1e-6f){ Q[tb][0]/=l9; Q[tb][1]/=l9; Q[tb][2]/=l9; Q[tb][3]/=l9; } } }
        return; }
    /* fallback: the S4b rest-delta, for packs without a basis (non-canon rigs) */
    for(unsigned t=0;t<c->nTracks;t++){ unsigned node=*(unsigned*)(tr+t*12), type=*(unsigned*)(tr+t*12+4), offD=*(unsigned*)(tr+t*12+8);
        if(type!=0) continue;   /* rotation-only across rigs; hips stay the target's own */
        int cn=dp->nodeCanon?dp->nodeCanon[node]:-1; if(cn<0) continue;
        int tb=tp->pb[cn]; if(tb<0) continue;
        float*d=(float*)(dp->pk+offD);
        float qd[4]; qnlerp(qd,d+(size_t)f0*4,d+(size_t)f1*4,ft);   /* the donor frame, interpolated */
        float rd[4],inv[4],del[4];
        memcpy(rd,dp->nodes[node].q,16);
        inv[0]=-rd[0]; inv[1]=-rd[1]; inv[2]=-rd[2]; inv[3]=rd[3];
        qmul(del,inv,qd);
        qmul(Q[tb],tp->nodes[tb].q,del); } }
/* integer wrapper: the audits keep exact baked frames (frac 0 == the old memcpy) */
static void donorPose(Pack*dp,int clip,unsigned frame,Pack*tp,float(*Q)[4]){
    donorPoseF(dp,clip,(double)frame,tp,Q); }

/* ================= LAYERED ANIMATION (09-11) ========================================
 * The audit: "no additive layering, no masked upper/lower body blend". Carrying a crate
 * while walking, talking while boarding a window, waving while running -- none of it could
 * be composed from two clips; a body played ONE. Now a second clip is blended onto the
 * UPPER BODY (the first spine bone and everything hanging off it: chest, neck, head,
 * clavicles, arms, hands) while the legs and hips keep the first. The overlay resolves like
 * any clip -- the body's own pack, then any bank through the canonical transfer -- and
 * runs on its own clock. One function, called from all three body paths.
 *   hero: ctl over=<clip>[@w] / scene hover=   partner: pover= / p2over=   actor: `over <clip> [w]` */
#define OVER_CACHE 16
typedef struct { Pack*P; char name[32]; Pack*cp; int ci; } OverRes;
static OverRes g_overRes[OVER_CACHE]; static int g_nOverRes=0;
static int overResolve(Pack*P,const char*name,Pack**cp,int*ci){
    for(int i=0;i<g_nOverRes;i++) if(g_overRes[i].P==P&&!strcmp(g_overRes[i].name,name)){ *cp=g_overRes[i].cp; *ci=g_overRes[i].ci; return *ci>=0; }
    Pack*rp=NULL; int rc=-1;
    for(unsigned c=0;c<P->nClips;c++) if(!strcmp(packStr(P,P->clips[c].name),name)){ rp=P; rc=(int)c; break; }
    if(rc<0){ int bc=-1; Pack*bp=bankFind(name,&bc);
        if(bp&&P->cbOK&&P->pbOK>=10){ rp=bp; rc=bc; }
        else if(bp) printf("[over] '%s' is in a bank but %s has no canonical basis to receive it\n",name,P->label);
        else printf("[over] '%s' NOT FOUND in %s or any bank\n",name,P->label); }
    else if(getenv("REVA_OVERDBG")) printf("[over] %s: '%s' from %s\n",P->label,name,rp==P?"her own pack":rp->label);
    if(g_nOverRes<OVER_CACHE){ OverRes*R=&g_overRes[g_nOverRes++]; R->P=P; snprintf(R->name,32,"%s",name); R->cp=rp; R->ci=rc; }
    *cp=rp; *ci=rc; return rc>=0; }
static const unsigned char* overMask(Pack*P){
    static struct { Pack*P; unsigned char m[1024]; } cache[8]; static int nc=0;
    for(int i=0;i<nc;i++) if(cache[i].P==P) return cache[i].m;
    int slot=(nc<8)?nc++:0; cache[slot].P=P; unsigned char*m=cache[slot].m; memset(m,0,1024);
    int root=(P->pb[1]>=0)?P->pb[1]:((P->pb[2]>=0)?P->pb[2]:-1);
    if(root<0){ printf("[over] %s has no spine bone in its canon -- no upper body to layer on\n",P->label); return m; }
    int cnt=0;
    for(unsigned i=0;i<P->nNodes&&i<1024;i++){ int n=(int)i, depth=0;
        while(n>=0&&depth<64){ if(n==root){ m[i]=1; cnt++; break; } n=P->nodes[n].parent; depth++; } }
    if(getenv("REVA_OVERDBG")) printf("[over] %s: upper-body mask = %d of %u bones\n",P->label,cnt,P->nNodes);
    return m; }
static void poseOverlay(Pack*P,float(*Q)[4],const char*name,double ph,float w){
    if(!name||!name[0]||w<=0.001f||!strcmp(name,"off")) return;
    Pack*cp; int ci; if(!overResolve(P,name,&cp,&ci)) return;
    static float Q2[1024][4], T2[1024][3], S2[1024][3];
    unsigned n=P->nNodes<1024?P->nNodes:1024;
    for(unsigned i=0;i<n;i++){ memcpy(Q2[i],P->nodes[i].q,16); memcpy(T2[i],P->nodes[i].t,12); memcpy(S2[i],P->nodes[i].s,12); }
    if(cp==P) clipSampleF(P,&P->clips[ci],ph,T2,Q2,S2); else donorPoseF(cp,ci,ph,P,Q2);
    const unsigned char*m=overMask(P);
    if(w>1.f) w=1.f;
    for(unsigned i=0;i<n;i++) if(m[i]) qnlerp(Q[i],Q[i],Q2[i],w); }
static double overPhase(Pack*P,const char*name,double tsec){
    Pack*cp; int ci; if(!name||!name[0]||!strcmp(name,"off")||!overResolve(P,name,&cp,&ci)) return 0.0;
    return tsec*(double)cp->clips[ci].fps; }
static char  g_overClip[SCN_MAXCAST][32];   /* slot 0 the hero, 1.. the cast */
static float g_overW[SCN_MAXCAST]={1.f,1.f,1.f,1.f,1.f};
static float g_motE[SCN_MAXCAST];  /* ⭐ motion energy per slot, rad/s summed over bones (09-11): the cutter's signal */
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
        /* ⛔ NORMALISE BY HIP HEIGHT ABOVE THE FEET, NOT BY ABSOLUTE REST Y. The first
         * version divided by restBoneY alone, which is the pelvis's WORLD rest height --
         * and a rig rooted at its pelvis (kmoves: hip rests at y=0.0099) makes that ratio
         * 1.89/0.0099 = 191x. A curl-up borrowed onto Anima came back as a drop of
         * -16,624 rig units: she vanished underground and the two-head camera followed her
         * into an empty frame. The zombie bank only ever worked because the UE mannequin's
         * pelvis rests at 269. Hip-above-feet is ~1-2 units on every human rig. */
        float dR=restBoneY(dp,db)-dp->mny, tR=restBoneY(tp,tb)-tp->mny;
        if(dR<0.05f||tR<0.05f) return 0.f;
        return drop*(tR/dR); }
    return 0.f; }
/* S6f DOES THIS TRANSFER SURVIVE? donorPose composes the source's rest-relative delta
 * onto the target's rest. That is only valid when both rigs orient their bones the same
 * way. The UE mannequin lends to itself perfectly; reva's 27-node Meshy rig has a
 * different rest convention, and a run's large rotations came out as a crumpled tangle
 * -- upright by the hip test, but plainly broken to look at.
 *
 * So do not trust the transfer, TEST it: build the pose the donor would produce on this
 * target and check the body still stands. Hips above both feet, knees below the hips,
 * head above the hips. An incompatible pair is refused once, cached, and the target
 * falls back to whatever it owns. Correct by construction: a mangled pose cannot reach
 * the screen, because the pose is checked before the pairing is allowed. */
static int donorOK(Pack*dp,int clip,Pack*tp){
    if(!dp||!tp||clip<0||(unsigned)clip>=dp->nClips) return 0;
    if(tp->pb[0]<0||tp->footL<0||tp->footR<0||tp->nNodes>512) return 0;
    /* S7: 48 was sized for the gait roles alone. The stand ladder asks about every
     * donor idle x every body, which overran it -- and an overrun cache does not
     * just recompute, it RE-PRINTS: 1607 refusal lines in one 8s run, enough
     * stdout to disturb the sim being measured. Bigger, and it only speaks when
     * the verdict is actually new. */
    static struct { Pack*d; int c; Pack*t; int ok; } cache[512]; static int cn=0;
    for(int i=0;i<cn;i++) if(cache[i].d==dp&&cache[i].c==clip&&cache[i].t==tp) return cache[i].ok;
    static float T[512][3],Q[512][4],S[512][3]; static M4 W[512];
    PClip*c=&dp->clips[clip];
    int ok=1;
    for(int k=0;k<6&&ok;k++){
        for(unsigned i=0;i<tp->nNodes;i++){ memcpy(T[i],tp->nodes[i].t,12);
            memcpy(Q[i],tp->nodes[i].q,16); memcpy(S[i],tp->nodes[i].s,12); }
        donorPose(dp,clip,(unsigned)((float)k/6.f*(float)c->frames)%c->frames,tp,Q);
        bodyFK(tp,T,Q,S,W);
        float hip=W[tp->pb[0]][13];
        float lo=W[tp->footL][13]<W[tp->footR][13]?W[tp->footL][13]:W[tp->footR][13];
        float want=(tp->effH>0.5f?tp->effH:3.15f)*0.28f/(tp->scale>1e-6f?tp->scale:1.f);
        if(hip-lo<want) ok=0;                                  /* collapsed */
        if(tp->pb[5]>=0&&W[tp->pb[5]][13]<hip) ok=0;            /* head below the hips */
        if(tp->pb[13]>=0&&W[tp->pb[13]][13]>hip) ok=0;          /* knee above the hips */ }
    int fresh=0;
    if(cn<512){ cache[cn].d=dp; cache[cn].c=clip; cache[cn].t=tp; cache[cn].ok=ok; cn++; fresh=1; }
    if(!ok&&fresh) printf("DONOR REFUSED: %s clip %d -> %s (transfer does not hold the body up)\n",
                   dp->label,clip,tp->label);
    return ok; }

/* STATIC PROP DRAW (jacob 08-21, the skate work). A prop is a pack with no rig: one
 * joint at the origin, so the palette is a single identity and the vertices land exactly
 * where the baker put them. Props are baked at FINAL WORLD SIZE and therefore ignore the
 * per-pack height normalisation that sizes a dancer -- the caller owns the model matrix.
 * Assumes the skinned program is already current; restores nothing. */
/* S7 THE BORROWED STAND. Most of the roster ships no idle clip at all: the whole
 * AssetsvilleTown cast has dance loops and nothing else, and reva has no stand either.
 * Asked to stand they fell through to "hold the walk cycle 27% of the way in" -- a
 * mid-stride statue, one foot up, legs splayed (measured: stance 5.7x hip width against
 * a healthy 1.2-2.7). The crowd has drawn its stand from the donor bank since S4g; the
 * hero never did. First donor idle whose transfer actually holds the body up wins. */
/* ARMS HANG IN A STAND. donorOK only asks whether the body stays UP -- hips above feet,
 * head above hips, knee below hip. It says nothing about arms, because in the gaits it
 * was written for arms legitimately swing. A stand is different, and the difference was
 * visible the moment the borrowed stand shipped: legs and torso transferred fine while
 * Character_01's right arm locked straight out sideways, the signature of a bone whose
 * rest orientation does not match the donor's, so the rest-delta lands as a 60-degree
 * error. Measured on the review wall at 40-68 deg against 8-21 for a real stand. */
static int standArmsOK(Pack*dp,int clip,Pack*tp){
    if(!dp||!tp||clip<0||(unsigned)clip>=dp->nClips||tp->nNodes>512) return 0;
    int ua[2]={tp->pb[7],tp->pb[10]}, fo[2]={tp->pb[8],tp->pb[11]};
    if(ua[0]<0||ua[1]<0||fo[0]<0||fo[1]<0) return 1;    /* no arms mapped: nothing to judge */
    /* S7: 48 was sized for the gait roles alone. The stand ladder asks about every
     * donor idle x every body, which overran it -- and an overrun cache does not
     * just recompute, it RE-PRINTS: 1607 refusal lines in one 8s run, enough
     * stdout to disturb the sim being measured. Bigger, and it only speaks when
     * the verdict is actually new. */
    static struct { Pack*d; int c; Pack*t; int ok; } cache[512]; static int cn=0;
    for(int i=0;i<cn;i++) if(cache[i].d==dp&&cache[i].c==clip&&cache[i].t==tp) return cache[i].ok;
    static float T[512][3],Q[512][4],S[512][3]; static M4 W[512];
    PClip*c=&dp->clips[clip];
    double worst=0.0;
    for(int k=0;k<4;k++){
        for(unsigned i=0;i<tp->nNodes;i++){ memcpy(T[i],tp->nodes[i].t,12);
            memcpy(Q[i],tp->nodes[i].q,16); memcpy(S[i],tp->nodes[i].s,12); }
        donorPose(dp,clip,(unsigned)((float)k/4.f*(float)c->frames)%c->frames,tp,Q);
        bodyFK(tp,T,Q,S,W);
        for(int s=0;s<2;s++){
            float dx=W[fo[s]][12]-W[ua[s]][12], dy=W[fo[s]][13]-W[ua[s]][13], dz=W[fo[s]][14]-W[ua[s]][14];
            float n=sqrtf(dx*dx+dy*dy+dz*dz); if(n<1e-5f) continue;
            double a=acos(fmax(-1.0,fmin(1.0,-dy/n)))*57.2957795;   /* away from straight down */
            if(a>worst) worst=a; } }
    int ok=(worst<40.0);   /* a real stand measures 8-21 deg; the broken ones 28-68 */
    int fresh=0;
    if(cn<512){ cache[cn].d=dp; cache[cn].c=clip; cache[cn].t=tp; cache[cn].ok=ok; cn++; fresh=1; }
    if(!ok&&fresh) printf("STAND: %s clip %d -> %s arms fly out %.0f deg, masking them\n",
                   dp->label,clip,tp->label,worst);
    return ok; }
/* Returns 0 none, 1 whole-body, 2 body-but-mask-the-arms. Falling back to the REST pose
 * when the arms will not transfer trades a live idle for a statue, and refusing outright
 * gives the statue too -- so keep the legs and spine, which do transfer, and leave the
 * arms on the body's own rest rotations. Stiff arms beat arms out sideways. */
static int dnStandPick(Pack*P,Pack**dp,int*dc){
    if(!P||P->pbOK<10) return 0;
    for(int i=0;i<g_dnNIdles;i++)
        if(donorOK(g_dnIdlesP[i],g_dnIdles[i],P)&&standArmsOK(g_dnIdlesP[i],g_dnIdles[i],P)){
            *dp=g_dnIdlesP[i]; *dc=g_dnIdles[i]; return 1; }
    for(int i=0;i<g_dnNIdles;i++)
        if(donorOK(g_dnIdlesP[i],g_dnIdles[i],P)){ *dp=g_dnIdlesP[i]; *dc=g_dnIdles[i]; return 2; }
    return 0; }
static void sunStash(Pack*P,int mi,const float*pal,int nb,const float*model,int crowd);   /* the sun's caster stash (09-11), defined below */
/* ⭐ THE NORMAL MAP, BOUND (09-11). Unit 1, next to the base colour on unit 0, and the
 * current program's uNrm says whether this mesh has one. g_curNrmLoc is set wherever a mesh
 * program is bound for drawing (-1 on the hologram and the sun pass). REVA_NONORMAL=1 for
 * the A/B. Every draw path that binds a base colour calls this right after. */
static GLint g_curNrmLoc=-1; static int g_noNormal=-1;
static void nrmBind(Pack*P,unsigned mi){
    if(g_noNormal<0){ const char*e=getenv("REVA_NONORMAL"); g_noNormal=(e&&e[0]!='0')?1:0; }
    int nt=(P->meshNTex&&!g_noNormal)?P->meshNTex[mi]:-1;
    if(nt>=0&&P->tex[nt]){ glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,P->tex[nt]); glActiveTexture(GL_TEXTURE0);
        if(g_curNrmLoc>=0) glUniform1f(g_curNrmLoc,1.f);
        { static int said=0; if(said<3){ said++; printf("[normal] bound %s mesh %u slot %d on unit 1, uNrm loc %d%s\n",P->label,mi,nt,g_curNrmLoc,g_curNrmLoc<0?" -- NO LOCATION: the program was bound without setting g_curNrmLoc":""); fflush(stdout); } } }
    else if(g_curNrmLoc>=0) glUniform1f(g_curNrmLoc,0.f); }
/* ⭐ ep 12: A HELD PRIMITIVE IN MORE THAN ONE COLOUR. drawProp handed every mesh of a pack the ONE tint
 * its caller set, so an authored primitive was one colour -- and a cigarette is three (paper, filter,
 * ember). The garments' convention is reused, not a new one: a mesh named `...#rrggbb` takes that colour,
 * one without a colour takes the caller's, and `#rrggbb*` marks the part that GLOWS. Only a caller that
 * fills g_propMC (drawHeldOne, on the tint path) gets any of this: every other draw is untouched. */
static struct { GLint uTint, uEmis; float base[3]; float emisAll, emisGlow; } g_propMC={-1,-1,{1.f,1.f,1.f},0.f,1.f};
static void propMeshColour(Pack*P,unsigned mi){
    float c[3]={g_propMC.base[0],g_propMC.base[1],g_propMC.base[2]}; int glow=0;
    const char*mn=packStr(P,P->meshes[mi].name); const char*h=mn?strchr(mn,'#'):NULL;
    if(h&&strlen(h)>=7){ char hx[7]; memcpy(hx,h+1,6); hx[6]=0; unsigned v=(unsigned)strtoul(hx,NULL,16);
        c[0]=((v>>16)&255)/255.f; c[1]=((v>>8)&255)/255.f; c[2]=(v&255)/255.f; glow=(h[7]=='*'); }
    glUniform3f(g_propMC.uTint,c[0],c[1],c[2]);
    if(g_propMC.uEmis>=0) glUniform1f(g_propMC.uEmis,glow?g_propMC.emisGlow:g_propMC.emisAll); }
static int g_propOnly=-1;   /* one mesh of a pack, not all of them: a far stand-in */
static void drawProp(Pack*P,const float*model,GLint uModel,GLint uPal,GLint uBind){
    static const M4 ident={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    /* ⛔ ONE identity is only enough for a ONE-BONE prop. A weapon is weighted entirely
     * to bone 0; a character is not, and every vertex on a bone this never wrote reads an
     * unset uniform and collapses to the origin -- the mesh comes back as a star of
     * triangles radiating from a point. The bind pose IS the identity palette (the pack's
     * inverse binds are the inverse of the rest world matrices), it just has to be
     * identity for all of them. */
    static float idN[MAXB*16]; static int idInit=0;
    if(!idInit){ idInit=1; for(int b=0;b<MAXB;b++) memcpy(idN+b*16,ident,64); }
    glUniformMatrix4fv(uModel,1,GL_FALSE,model);
    for(unsigned mi=g_propOnly>=0?(unsigned)g_propOnly:0u,me=g_propOnly>=0?mi+1u:P->nMeshes;mi<me;mi++){ PMesh*M=&P->meshes[mi];
        if(!(M->flags&4)||M->boneCount>MAXB||MOFF(P,mi)) continue;
            meshAlphaState(M);
        glUniformMatrix4fv(uPal,(GLsizei)(M->boneCount?M->boneCount:1),GL_FALSE,idN);
        glUniformMatrix4fv(uBind,1,GL_FALSE,(float*)(P->pk+M->offBind));
        GLuint*b=&P->vbo[mi*6];
        bodyStreams(M,b);
        { int tx=P->meshTex[mi]; if(tx>=0) glBindTexture(GL_TEXTURE_2D,P->tex[tx]); nrmBind(P,mi); }
        if(g_propMC.uTint>=0) propMeshColour(P,mi);   /* ⭐ ep 12: per-mesh colour and glow, for a held primitive */
        GLint shA9=-1;   /* ⭐ (10-22) a SHEER prop: blended, not depth-writing, at its marker's opacity */
        if(P->sheer>0.f&&P->sheer<1.f){ GLint pr9=0; glGetIntegerv(GL_CURRENT_PROGRAM,&pr9); shA9=glGetUniformLocation((GLuint)pr9,"uAlpha");
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); if(shA9>=0) glUniform1f(shA9,P->sheer); }
        { int toon9=P->glow?0:toonOn(P,P->toon); toonCelSet(toon9); colSet(P,NULL,mi);
          int look9=(toon9&&P->toon==2)||P->glow; if(look9) toonSetLook(toon9&&P->toon==2,P->glow);
        if(M->idxCount) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,b[4]);
#ifdef REVA_PACKET
        g_vkMirror=1|2|((P->mirOnly||P->sheer>0.f)?4:0)|(P->mirOnly?8:0); vkMesh(P,mi,model,(float*)(P->pk+M->offBind),NULL,0,g_vkTint,g_vkUse,g_shadowPass?GFX_SHADOW:0,NULL); g_vkMirror=0;
#endif
        if(!P->mirOnly){   /* ⭐ (10-22) a MirrorOnly prop shows only in reflections: a mirror's backing, seen in another */
        if(M->idxCount) glDrawElements(GL_TRIANGLES,M->idxCount,GL_UNSIGNED_INT,0);
        else glDrawArrays(GL_TRIANGLES,0,M->verts);
          toonInkDraw(toon9,packStr(P,M->name),M->idxCount,M->verts); }
          mirInst(uModel,toon9,packStr(P,M->name),M->idxCount,M->verts,1|((P->mirOnly||P->sheer>0.f)?2:0));
          if(look9) toonSetLook(0,0); }
        if(P->sheer>0.f&&P->sheer<1.f){ if(shA9>=0) glUniform1f(shA9,1.f); glDisable(GL_BLEND); glDepthMask(GL_TRUE); } }
}

