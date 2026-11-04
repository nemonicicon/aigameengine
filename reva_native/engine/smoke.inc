/* ================= SMOKE (ROBOT NEKO ep 12) =====================================================
 * 1 AM, kaeli at the window with a cigarette -- and the engine had never drawn smoke: not a wisp, not
 * a breath. This is the whole of it:
 *     hprop=v1_x_cigarette@0.16@0@hand@F2EFE8@light:0.5@smoke    the prop's FAR TIP smokes (@smoke:<rate>)
 *     hpuff=1.4[,sec...]   ppuff= p2puff= p3puff=                  an EXHALE off that body's mouth mark
 *     ctl smoke=0|1        REVA_SMOKEDBG=1
 * THE LOOK: many small, overlapping, very faint sprites -- never a few big ones. Off the tip it leaves
 * as a thin laminar thread (small, dense, rising nearly straight) and breaks into curls higher up: the
 * curl field's strength grows with a sprite's AGE, so the first quarter-second is a thread and the rest
 * is turbulence. An exhale is a denser JET along the head's own forward (a head tipped back blows it
 * up) that slows in the air and BLOOMS. Both ride the set's `wind` -- the curtains' breeze, so the
 * smoke and the curtains agree which way the draught goes -- plus a divergence-free curl field, so the
 * smoke swirls instead of jittering.
 * THE LIGHT: lit per sprite on the CPU by the terms the body shader uses -- the sky ambient, the sun as
 * scatter, every set practical and every held light at the club's 1/(1+dd*0.055) -- so smoke by the
 * night light reads warm and smoke in the streetlight cool. An EMBER also lights its own smoke up close
 * (1/(1+dd*40)): the wisp glows at its root and not a foot above it.
 * THE DRAG: while the hand holding a smoking prop is at the mouth (its grip end or the hand itself
 * within SMOKE_DRAG_R of the mouth mark) the coal's light and glow ease to x3 (0.2 s in, 0.5 s out) and
 * the tip smokes harder. Automatic.
 * THE CLOCK: the EXPOSURE clock (g_animDtS). A held exposure holds the smoke like animated cotton; at
 * step=60 it advances every frame. Seeded per scene, so a fixed-step take is the same smoke twice.
 * ⛔ WHAT IT CANNOT DO: no soft fade where a sprite meets a wall (this ANGLE context has no
 * OES_depth_texture, so the depth buffer cannot be read back) -- the sprites stay small so the hard line
 * is short; no self-shadowing; no collision (smoke passes through a ceiling). */
#define SMOKE_MAX    3000     /* sprites alive at once; past it new smoke is REFUSED, loudly */
#define SMOKE_TEX    64
#define SMOKE_DRAG_R 0.10f    /* u: the grip end / hand this close to the mouth mark is a drag */
#define SMOKE_TIPRATE 30.f    /* sprites a second off a @smoke tip at rate 1 */
#define PUFF_MAX     8
#define JET_MAX      8
#define JET_SECS     1.2f     /* an exhale lasts this long */
typedef struct { float p[3], v[3], age, life, s0, s1, a0, rot, rotv, seed; int kind; float c[3], a, s, d2; } SmokeP;
static SmokeP g_smk[SMOKE_MAX]; static int g_nSmk=0;
static int    g_smokeOn=1, g_smokeDbg=-1;
static float  g_smkT=0.f;
static float  g_smkOwe[SCN_MAXCAST][2];
static unsigned g_smkRng=0x2545F491u;
static float  g_puffAt[SCN_MAXCAST][PUFF_MAX]; static int g_puffN[SCN_MAXCAST], g_puffNext[SCN_MAXCAST];
static int    g_puffLine=-1; static float g_puffT0=0.f;
typedef struct { int slot; float t, owe; } SmokeJet;
static SmokeJet g_jet[JET_MAX]; static int g_nJet=0;
static float smkRand(void){ g_smkRng^=g_smkRng<<13; g_smkRng^=g_smkRng>>17; g_smkRng^=g_smkRng<<5;
    return (float)(g_smkRng&0xffffffu)/16777216.f; }
static float smkSS(float a,float b,float x){ float t=(x-a)/(b-a); if(t<0.f) t=0.f; if(t>1.f) t=1.f; return t*t*(3.f-2.f*t); }
/* a scene (re)starts: no smoke, no exhale and no drag outlives the take that made it, and the dice are
 * re-seeded so a fixed-step take is the same smoke twice */
static void puffReset(void){
    for(int s=0;s<SCN_MAXCAST;s++){ g_puffN[s]=g_puffNext[s]=0; g_smkOwe[s][0]=g_smkOwe[s][1]=0.f; g_heldDrag[s][0]=g_heldDrag[s][1]=0.f; }
    g_puffLine=-1; g_nJet=0; g_nSmk=0; g_smkT=0.f; g_smkRng=0x2545F491u; }
/* A LINE'S STAGING APPLIES: this line's exhales are armed (the last line's never carry) */
/* ⭐ THE RAGDOLL CUE. Read on the cut, fired inside ragdollTick where the body's world matrices exist: a
 * scene cannot go limp from here, because a limp body needs its own pose to fall out of. `off` releases. */
static float g_rdCueAt[SCN_MAXCAST]; static int g_rdCueOff[SCN_MAXCAST];
static int g_rdCueLine=-1; static float g_rdCueT0=0.f;
static void dollArm(int li){
    for(int s=0;s<SCN_MAXCAST;s++){ g_rdCueAt[s]=-1.f; g_rdCueOff[s]=0; }
    g_rdCueLine=li; g_rdCueT0=scnBodyT();
    if(li<0||li>=g_scnN) return;
    const ScnLine*L=&g_scnL[li];
    const char*src[4]={L->hdoll,L->pdoll,L->p2doll,L->p3doll};
    for(int s=0;s<4&&s<SCN_MAXCAST;s++){ const char*p=src[s]; if(!p||!*p) continue;
        if(!strcmp(p,"off")){ g_rdCueOff[s]=1; continue; }
        char*e=NULL; float at=strtof(p,&e);
        if(e==p||at<0.f){ printf("[ragdoll] line %d slot %d: '%s' is not sec (>= 0 into the line) or `off` -- IGNORED\n",li,s,p); continue; }
        g_rdCueAt[s]=at;
        if(at>L->dur+1.f) printf("[ragdoll] line %d slot %d: the fall at +%.2fs is past the line's end (+%.2fs) -- it fires only while this line still has the stage\n",li,s,at,L->dur); }
    fflush(stdout); }
static void puffArm(int li){
    if(li<0||li>=g_scnN) return;
    const ScnLine*L=&g_scnL[li];
    for(int s=0;s<SCN_MAXCAST;s++){ g_puffN[s]=0; g_puffNext[s]=0; }
    g_puffLine=li; g_puffT0=g_scnT;
    const char*src[4]={L->hpuff,L->ppuff,L->p2puff,L->p3puff};
    for(int s=0;s<4&&s<SCN_MAXCAST;s++){ const char*p=src[s];
        while(p&&*p){ char*e=NULL; float at=strtof(p,&e);
            if(e==p||at<0.f){ printf("[smoke] line %d slot %d: '%s' is not sec[,sec...] (sec >= 0 into the line) -- the rest IGNORED\n",li,s,p); break; }
            if(g_puffN[s]>=PUFF_MAX) printf("[smoke] line %d slot %d: more than %d puffs -- +%.2fs dropped (PUFF_MAX)\n",li,s,PUFF_MAX,at);
            else { int k=g_puffN[s]++; g_puffAt[s][k]=at;
                for(;k>0&&g_puffAt[s][k]<g_puffAt[s][k-1];k--){ float t9=g_puffAt[s][k]; g_puffAt[s][k]=g_puffAt[s][k-1]; g_puffAt[s][k-1]=t9; }
                if(at>L->dur+1.f) printf("[smoke] line %d slot %d: a puff at +%.2fs is past the line's end (+%.2fs) -- it fires only while this line still has the stage\n",li,s,at,L->dur); }
            p=(e&&*e==',')?e+1:NULL; } }
    fflush(stdout); }
/* A DIVERGENCE-FREE FIELD: the curl of three sine-product potentials, two octaves drifting in time. What
 * makes smoke CURL instead of jitter -- a sprite follows the flow, and so does its neighbour. */
static void smkCurl(const float*p,float t,float*o){
    static const float K[2]={4.3f,10.1f}, A[2]={1.f,0.45f};
    o[0]=o[1]=o[2]=0.f;
    for(int oc=0;oc<2;oc++){ float k=K[oc], w=t*(0.35f+0.30f*(float)oc);
        float x=p[0]*k, y=p[1]*k, z=p[2]*k;
        float f1=y+w*1.1f, f2=z-w*0.7f+1.3f, f3=z+w*0.9f+2.1f, f4=x-w*0.6f+0.4f, f5=x+w*0.8f+3.7f, f6=y-w*1.2f+5.3f;
        /* psi = ( sin f1 sin f2, sin f3 sin f4, sin f5 sin f6 ); v = curl psi */
        float dzy=sinf(f5)*cosf(f6), dyz=cosf(f3)*sinf(f4), dxz=sinf(f1)*cosf(f2);
        float dzx=cosf(f5)*sinf(f6), dyx=sinf(f3)*cosf(f4), dxy=cosf(f1)*sinf(f2);
        o[0]+=A[oc]*(dzy-dyz); o[1]+=A[oc]*(dxz-dzx); o[2]+=A[oc]*(dyx-dxy); } }
/* LIT LIKE EVERYTHING ELSE IN THE ROOM (see the note above); rgb out, before the tone knee */
static void smkLight(const float*p,float*c){
    float r,g,b;
    if(g_sun[3]>0.f){ r=g_sky[0]+g_sunC[0]*g_sun[3]*0.35f; g=g_sky[1]+g_sunC[1]*g_sun[3]*0.35f; b=g_sky[2]+g_sunC[2]*g_sun[3]*0.35f; }
    else { r=g=b=0.30f; }                        /* the club's dark-room rig: its ambient floor and key, averaged */
    for(int k=0;k<g_setNL&&k<NLIGHT;k++){ const float*L=g_setLp+k*3,*C=g_setLc+k*3;
        float dx=L[0]-p[0], dy=L[1]-p[1], dz=L[2]-p[2];
        float f=0.55f*g_lampScale/(1.f+(dx*dx+dy*dy+dz*dz)*(0.055f+g_setLk[k]));
        r+=C[0]*f; g+=C[1]*f; b+=C[2]*f; }
    for(int k=0;k<g_nHL;k++){ float dx=g_hlP[k][0]-p[0], dy=g_hlP[k][1]-p[1], dz=g_hlP[k][2]-p[2], dd=dx*dx+dy*dy+dz*dz;
        float f=0.55f/(1.f+dd*(0.055f+g_hlK[k]))+(g_hlEmber[k]?1.2f/(1.f+dd*40.f):0.f);
        r+=g_hlC[k][0]*f; g+=g_hlC[k][1]*f; b+=g_hlC[k][2]*f; }
    c[0]=0.86f*r; c[1]=0.86f*g; c[2]=0.88f*b; }   /* pale grey-white, a breath cooler than paper */
static int smkSpawn(void){
    if(g_nSmk>=SMOKE_MAX){ static unsigned ref9=0;
        if((ref9++%600u)==0u){ printf("[smoke] SMOKE_MAX (%d) sprites alive -- new smoke REFUSED (%u refused so far): raise SMOKE_MAX or thin the emitters\n",SMOKE_MAX,ref9); fflush(stdout); }
        return -1; }
    SmokeP*S=&g_smk[g_nSmk]; memset(S,0,sizeof*S);
    S->rot=smkRand()*6.2832f; S->rotv=(smkRand()-0.5f)*0.9f; S->seed=smkRand();
    return g_nSmk++; }
/* WHERE A BODY BREATHES FROM AND WHICH WAY: the mouth mark, and the head's own axis nearest the face's
 * horizontal forward -- so the pitch comes with it. 1 = the mouth mark, 0 = no mouth mark (a point in
 * front of the head), -1 = nothing published for that body. */
static int smkMouth(int s,float*mo,float*fw){
    if(s<0||s>=SCN_MAXCAST) return -1;
    const float*M=NULL,*Hd=NULL;
    if(s==0){ if(g_heroFaceOK) M=g_heroMouthW; if(g_heroHeadOK) Hd=g_heroHeadW; }
    else { if(g_cast[s].faceOK||g_cast[s].mouthOK) M=g_cast[s].mouthW; if(g_cast[s].headOK) Hd=g_cast[s].headW; }
    float hf[3]={0.f,0.f,1.f};
    if(s==0&&g_heroFaceOK){ hf[0]=g_heroFaceFwd[0]; hf[2]=g_heroFaceFwd[2]; }
    else if(M&&Hd){ hf[0]=M[0]-Hd[0]; hf[2]=M[2]-Hd[2]; }
    else if(s>0&&g_castAtOK[s]){ hf[0]=sinf(g_castAt[s][2]); hf[2]=cosf(g_castAt[s][2]); }
    { float l=sqrtf(hf[0]*hf[0]+hf[2]*hf[2]); if(l>1e-5f){ hf[0]/=l; hf[2]/=l; } else { hf[0]=0.f; hf[2]=1.f; } }
    fw[0]=hf[0]; fw[1]=-0.10f; fw[2]=hf[2];
    if(g_castAttOK[s][ATT_HEAD]){ const float*A=g_castAttM[s][ATT_HEAD]; float best=0.f, sg=1.f; int bk=-1;
        for(int k=0;k<3;k++){ float cx=A[k*4], cy=A[k*4+1], cz=A[k*4+2], L=sqrtf(cx*cx+cy*cy+cz*cz);
            if(L<1e-6f) continue;
            float d=(cx*hf[0]+cz*hf[2])/L;
            if(fabsf(d)>best){ best=fabsf(d); bk=k; sg=(d<0.f)?-1.f:1.f; } }
        if(bk>=0&&best>0.55f){ float L=sqrtf(A[bk*4]*A[bk*4]+A[bk*4+1]*A[bk*4+1]+A[bk*4+2]*A[bk*4+2]);
            fw[0]=sg*A[bk*4]/L; fw[1]=sg*A[bk*4+1]/L-0.10f; fw[2]=sg*A[bk*4+2]/L; } }
    { float l=sqrtf(fw[0]*fw[0]+fw[1]*fw[1]+fw[2]*fw[2]); if(l>1e-5f){ fw[0]/=l; fw[1]/=l; fw[2]/=l; } }
    if(M){ memcpy(mo,M,12); return 1; }
    if(g_castAttOK[s][ATT_HEAD]){ const float*A=g_castAttM[s][ATT_HEAD];
        mo[0]=A[12]+fw[0]*0.14f; mo[1]=A[13]-0.02f; mo[2]=A[14]+fw[2]*0.14f; return 0; }
    return -1; }
static void smkEmitTip(const float*tip,int kick){
    int k=smkSpawn(); if(k<0) return;
    SmokeP*S=&g_smk[k];
    S->p[0]=tip[0]+(smkRand()-0.5f)*0.004f; S->p[1]=tip[1]+smkRand()*0.003f; S->p[2]=tip[2]+(smkRand()-0.5f)*0.004f;
    S->v[0]=(smkRand()-0.5f)*0.02f; S->v[1]=0.10f+0.05f*smkRand()+(kick?0.04f:0.f); S->v[2]=(smkRand()-0.5f)*0.02f;
    S->life=2.4f+1.4f*smkRand(); S->s0=0.007f+0.004f*smkRand(); S->s1=0.09f+0.08f*smkRand();
    S->a0=0.10f+0.06f*smkRand(); S->kind=0; }
static void smkEmitJet(const float*mo,const float*f,float jt){
    int k=smkSpawn(); if(k<0) return;
    SmokeP*S=&g_smk[k];
    float u[3]={f[2],0.f,-f[0]}; float lu=sqrtf(u[0]*u[0]+u[2]*u[2]);
    if(lu<1e-4f){ u[0]=1.f; u[2]=0.f; lu=1.f; }
    u[0]/=lu; u[2]/=lu;
    float w[3]={u[1]*f[2]-u[2]*f[1], u[2]*f[0]-u[0]*f[2], u[0]*f[1]-u[1]*f[0]};   /* u x f */
    float a=(smkRand()-0.5f)*0.32f, b=(smkRand()-0.5f)*0.32f;
    float q[3]={f[0]+u[0]*a+w[0]*b, f[1]+u[1]*a+w[1]*b, f[2]+u[2]*a+w[2]*b};
    float lq=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]); if(lq<1e-5f) lq=1.f;
    float sp=(0.85f+0.35f*smkRand())*(1.f-0.55f*jt/JET_SECS);
    for(int c=0;c<3;c++){ S->p[c]=mo[c]+f[c]*0.015f+(smkRand()-0.5f)*0.012f; S->v[c]=q[c]/lq*sp; }
    S->v[1]-=0.04f;
    S->life=2.2f+1.2f*smkRand(); S->s0=0.018f+0.010f*smkRand(); S->s1=0.24f+0.14f*smkRand();
    S->a0=0.15f+0.08f*smkRand(); S->kind=1; }
/* ONE STEP OF THE AIR: every sprite eases toward the wind + its rise + the curl field, at its own time
 * constant. A thread couples to the draught slowly and turbulates late; a jet slows fast and churns. */
static void smkStep(float dt){
    g_smkT+=dt;
    float gust=1.f+g_windGust*0.35f*sinf(g_smkT*0.61f)*sinf(g_smkT*1.37f+0.8f);
    float W[3]={g_windV[0]*gust,g_windV[1]*gust,g_windV[2]*gust};
    int n=0;
    for(int i=0;i<g_nSmk;i++){ SmokeP*S=&g_smk[i];
        S->age+=dt;
        if(S->age>=S->life) continue;
        float u=S->age/S->life, tb, rise, tau, wk;
        if(S->kind==0){ tb=0.17f*smkSS(0.25f,1.3f,S->age); rise=0.16f*(1.f-u)+0.03f; tau=0.9f; wk=0.12f+0.88f*smkSS(0.3f,1.6f,S->age); }
        else          { tb=0.06f+0.12f*smkSS(0.2f,1.4f,S->age); rise=0.05f+0.05f*u; tau=0.40f+0.5f*u; wk=0.35f+0.65f*smkSS(0.2f,1.5f,S->age); }
        float q[3]={S->p[0]+S->seed*0.04f,S->p[1],S->p[2]-S->seed*0.03f}, cv[3]; smkCurl(q,g_smkT,cv);
        float tv[3]={W[0]*wk+cv[0]*tb, W[1]*wk+rise+cv[1]*tb, W[2]*wk+cv[2]*tb};
        float e=1.f-expf(-dt/tau);
        for(int a=0;a<3;a++){ S->v[a]+=(tv[a]-S->v[a])*e; S->p[a]+=S->v[a]*dt; }
        S->rot+=S->rotv*dt;
        if(n!=i) g_smk[n]=*S;
        n++; }
    g_nSmk=n; }
static void smkXf(const float*M,const float*p,float*o){
    for(int a=0;a<3;a++) o[a]=M[a]*p[0]+M[4+a]*p[1]+M[8+a]*p[2]+M[12+a]; }
static float smkDist(const float*a,const float*b){ float x=a[0]-b[0], y=a[1]-b[1], z=a[2]-b[2]; return sqrtf(x*x+y*y+z*z); }
/* ONCE PER FRAME, after every body has been drawn (so the mouths and the hands are this frame's) */
static void smokeTick(void){
    static unsigned stamp9=0xffffffffu;
    if(stamp9==g_frameStamp) return;
    stamp9=g_frameStamp;
    if(g_smokeDbg<0){ const char*e=getenv("REVA_SMOKEDBG"); g_smokeDbg=(e&&e[0]&&e[0]!='0')?1:0; }
    if(!g_smokeOn){ g_nSmk=0; g_nJet=0; return; }
    float dt=(float)g_animDtS;
    if(!(dt>0.f)) return;                        /* a held exposure: the smoke holds with everything else */
    if(dt>0.25f) dt=0.25f;
    /* ⛔ ONE BIG STEP IS BEADED SMOKE. An exposure at step=12, or an 8 fps test capture, is 1/12..1/8 s: born
     * together, a frame's sprites would travel as one clump. The air runs in substeps of <= 1/30 s and each
     * substep gets its own births, so a slow capture and a smooth one draw the same thread. */
    int nsub=(int)ceilf(dt*30.f); if(nsub<1) nsub=1; if(nsub>8) nsub=8;
    float h=dt/(float)nsub;
    static float dbgNear[SCN_MAXCAST][2]; static float dbgTip[SCN_MAXCAST][2][3];
    float tipAt[SCN_MAXCAST][2][3]; float tipRate[SCN_MAXCAST][2]; int tipKick[SCN_MAXCAST][2];
    for(int i=0;i<SCN_MAXCAST;i++) for(int w=0;w<2;w++){ tipRate[i][w]=0.f; tipKick[i][w]=0; }
    for(int i=0;i<SCN_MAXCAST;i++) for(int w=0;w<2;w++){
        const char*spec=w?g_castProp2[i]:g_castProp[i];
        HeldSpec H; dbgNear[i][w]=-1.f;
        if(!heldParse(spec,&H)||H.smoke<=0.f){ g_heldDrag[i][w]=0.f; g_smkOwe[i][w]=0.f; continue; }
        if(!g_heldMSt[i][w]||g_heldMSt[i][w]+2u<g_frameStamp) continue;   /* not drawn lately: nothing to smoke */
        const HeldProp*HQ=heldPropOf(g_heldMPk[i][w]);
        if(!HQ) continue;
        const float*M=g_heldM[i][w];
        float tip[3], nr[3]; smkXf(M,HQ->tip,tip); smkXf(M,HQ->nearP,nr);
        memcpy(dbgTip[i][w],tip,12);
        /* THE DRAG: the grip end (the filter) or the hand itself at the mouth */
        float mo[3], fw[3]; int at=0;
        if(smkMouth(i,mo,fw)==1){ float d=smkDist(nr,mo), d2=smkDist(M+12,mo); if(d2<d) d=d2;
            dbgNear[i][w]=d; at=(d<SMOKE_DRAG_R);
            static int said9[SCN_MAXCAST][2];
            if(at&&!said9[i][w]){ said9[i][w]=1;
                printf("[smoke] slot %d%s '%s': a DRAG -- %.3f u from the mouth mark (< %.2f): the ember's light and glow ease to x3 (0.2 s in, 0.5 s out) and the tip smokes harder (said once)\n",
                       i,w?" prop2":"",H.stem,d,SMOKE_DRAG_R); fflush(stdout); } }
        g_heldDrag[i][w]+=at?dt/0.2f:-dt/0.5f;
        if(g_heldDrag[i][w]<0.f) g_heldDrag[i][w]=0.f;
        if(g_heldDrag[i][w]>1.f) g_heldDrag[i][w]=1.f;
        /* THE TIP */
        { static int said9[SCN_MAXCAST][2];
          if(!said9[i][w]){ said9[i][w]=1;
              printf("[smoke] slot %d%s '%s': the far tip smokes at (%.2f,%.2f,%.2f), %.0f sprites/s (x%.2f), wind (%.2f,%.2f,%.2f)\n",
                     i,w?" prop2":"",H.stem,tip[0],tip[1],tip[2],SMOKE_TIPRATE*H.smoke,H.smoke,g_windV[0],g_windV[1],g_windV[2]); fflush(stdout); } }
        memcpy(tipAt[i][w],tip,12);
        tipRate[i][w]=SMOKE_TIPRATE*H.smoke*(1.f+1.5f*g_heldDrag[i][w]);
        tipKick[i][w]=(g_heldDrag[i][w]>0.5f); }
    /* THE EXHALES: fired on an exposure, `sec` into the line, then run for JET_SECS off the mouth as it moves */
    if(g_scnT>=0.f&&g_puffLine>=0){ float t=scnBodyT()-g_puffT0;
        for(int s=0;s<SCN_MAXCAST;s++) while(g_puffNext[s]<g_puffN[s]&&t>=g_puffAt[s][g_puffNext[s]]){
            float at=g_puffAt[s][g_puffNext[s]++];
            float mo[3], fw[3]; int mk=smkMouth(s,mo,fw);
            if(mk<0){ printf("[smoke] line %d slot %d puff at +%.2fs REFUSED: that body published no mouth mark and no head\n",g_puffLine,s,at); fflush(stdout); continue; }
            if(g_nJet>=JET_MAX){ printf("[smoke] line %d slot %d puff at +%.2fs REFUSED: %d exhales already running (JET_MAX)\n",g_puffLine,s,at,JET_MAX); fflush(stdout); continue; }
            g_jet[g_nJet].slot=s; g_jet[g_nJet].t=0.f; g_jet[g_nJet].owe=0.f; g_nJet++;
            printf("[smoke] line %d slot %d puff at t=%.2f (+%.2fs into the line, keyed +%.2f) off the %s (%.2f,%.2f,%.2f) along (%.2f,%.2f,%.2f)\n",
                   g_puffLine,s,scnBodyT(),t,at,mk==1?"mouth mark":"head (NO mouth mark)",mo[0],mo[1],mo[2],fw[0],fw[1],fw[2]);
            fflush(stdout); } }
    /* THE AIR, in substeps: step what is already in it, then the births that belong to this substep */
    { float jmo[JET_MAX][3], jfw[JET_MAX][3]; int jok[JET_MAX];
      for(int j=0;j<g_nJet;j++) jok[j]=(smkMouth(g_jet[j].slot,jmo[j],jfw[j])>=0);
      for(int sb=0;sb<nsub;sb++){
          smkStep(h);
          for(int i=0;i<SCN_MAXCAST;i++) for(int w=0;w<2;w++){ if(tipRate[i][w]<=0.f) continue;
              g_smkOwe[i][w]+=tipRate[i][w]*h;
              int nb=(int)g_smkOwe[i][w]; g_smkOwe[i][w]-=(float)nb;
              for(int k=0;k<nb;k++) smkEmitTip(tipAt[i][w],tipKick[i][w]); }
          for(int j=0;j<g_nJet;j++){ SmokeJet*J=&g_jet[j];
              if(jok[j]&&J->t<JET_SECS){ J->owe+=150.f*expf(-J->t/0.5f)*h;
                  int nb=(int)J->owe; J->owe-=(float)nb;
                  for(int k=0;k<nb;k++) smkEmitJet(jmo[j],jfw[j],J->t); }
              J->t+=h; } }
      int n=0;
      for(int j=0;j<g_nJet;j++) if(g_jet[j].t<JET_SECS) g_jet[n++]=g_jet[j];
      g_nJet=n; }
    if(g_smokeDbg){ static unsigned n9=0;
        if((n9++%30u)==0u){ int nt=0; float top=-1e9f; int it=-1;
            for(int i=0;i<g_nSmk;i++){ if(g_smk[i].kind==0) nt++; if(g_smk[i].p[1]>top){ top=g_smk[i].p[1]; it=i; } }
            printf("[smokedbg] %d sprites (%d tip, %d exhale), %d jets, cap %d\n",g_nSmk,nt,g_nSmk-nt,g_nJet,SMOKE_MAX);
            for(int i=0;i<SCN_MAXCAST;i++) for(int w=0;w<2;w++) if(dbgNear[i][w]>=0.f){ float c9[3]; smkLight(dbgTip[i][w],c9);
                printf("[smokedbg]   slot %d%s tip (%.2f,%.2f,%.2f) lit (%.2f,%.2f,%.2f)  grip end %.3f u from the mouth  drag %.2f -> ember x%.2f\n",
                       i,w?" prop2":"",dbgTip[i][w][0],dbgTip[i][w][1],dbgTip[i][w][2],c9[0],c9[1],c9[2],dbgNear[i][w],g_heldDrag[i][w],heldDragK(i,w)); }
            if(it>=0){ float c9[3]; smkLight(g_smk[it].p,c9);
                printf("[smokedbg]   highest sprite (%.2f,%.2f,%.2f) age %.2f lit (%.2f,%.2f,%.2f)\n",
                       g_smk[it].p[0],g_smk[it].p[1],g_smk[it].p[2],g_smk[it].age,c9[0],c9[1],c9[2]); }
            fflush(stdout); } } }
/* ---- the draw: camera-facing soft sprites, far to near, depth-tested and not depth-written ---- */
static GLuint g_smkProg=0, g_smkTex=0, g_smkVB=0, g_smkIB=0;
static GLint  g_smkVP=-1, g_smkTone=-1, g_smkSmp=-1;
static float smkHash(int x,int y,int o){
    unsigned h=(unsigned)x*374761393u+(unsigned)y*668265263u+(unsigned)o*2246822519u;
    h=(h^(h>>13))*1274126177u; h^=h>>16; return (float)(h&0xffffu)/65535.f; }
static float smkVNoise(float x,float y,int o){
    int ix=(int)floorf(x), iy=(int)floorf(y); float fx=x-(float)ix, fy=y-(float)iy;
    fx=fx*fx*(3.f-2.f*fx); fy=fy*fy*(3.f-2.f*fy);
    float a=smkHash(ix,iy,o), b=smkHash(ix+1,iy,o), c=smkHash(ix,iy+1,o), d=smkHash(ix+1,iy+1,o);
    return a+(b-a)*fx+(c-a)*fy+(a-b-c+d)*fx*fy; }
static void smkInitGL(void){
    if(g_smkProg) return;
    const char*vs=
      "attribute vec3 p; attribute vec2 uv; attribute vec4 c;\n"
      "uniform mat4 vp; varying vec2 vuv; varying vec4 vc;\n"
      "void main(){ vuv=uv; vc=c; gl_Position=vp*vec4(p,1.0); }\n";
    const char*fs=
      "precision mediump float; varying vec2 vuv; varying vec4 vc;\n"
      "uniform sampler2D smp; uniform float uTone;\n"
      "void main(){ float a=texture2D(smp,vuv).a*vc.a; if(a<0.002) discard;\n"
      "  vec3 lit=vc.rgb;\n"
      /* the SAME highlight knee the meshes roll off under, so smoke and faces clip alike */
      "  if(uTone>0.0){ vec3 kn=vec3(uTone); vec3 ov=max(lit-kn,vec3(0.0));\n"
      "    vec3 wp=vec3(0.96); lit=min(lit,kn)+(wp-kn)*(vec3(1.0)-exp(-ov/(wp-kn))); }\n"
      "  gl_FragColor=vec4(lit,a); }\n";
    GLuint pr=glCreateProgram();
    glAttachShader(pr,sh(GL_VERTEX_SHADER,vs)); glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
    glBindAttribLocation(pr,0,"p"); glBindAttribLocation(pr,1,"uv"); glBindAttribLocation(pr,2,"c");
    glLinkProgram(pr);
    GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
    if(!ok){ char lg[512]; glGetProgramInfoLog(pr,512,NULL,lg);
        printf("[smoke] LINK FAILED -- no smoke will be drawn: %s\n",lg); fflush(stdout); g_smkProg=(GLuint)-1; return; }
    g_smkProg=pr;
    g_smkVP=glGetUniformLocation(pr,"vp"); g_smkTone=glGetUniformLocation(pr,"uTone"); g_smkSmp=glGetUniformLocation(pr,"smp");
    /* THE PUFF: a soft radial falloff broken up by three octaves of value noise, white, alpha only --
     * the colour is the light's. Mipmapped, so a far sprite is a blur and not a sparkle. */
    static unsigned char px[SMOKE_TEX*SMOKE_TEX*4];
    for(int y=0;y<SMOKE_TEX;y++) for(int x=0;x<SMOKE_TEX;x++){
        float fx=((float)x+0.5f)/(float)SMOKE_TEX*2.f-1.f, fy=((float)y+0.5f)/(float)SMOKE_TEX*2.f-1.f;
        float r=sqrtf(fx*fx+fy*fy);
        float base=expf(-r*r*3.0f)*(1.f-smkSS(0.70f,1.0f,r));
        float nz=0.55f*smkVNoise((float)x/8.f,(float)y/8.f,1)+0.30f*smkVNoise((float)x/4.f,(float)y/4.f,2)+0.15f*smkVNoise((float)x/2.f,(float)y/2.f,3);
        float a=base*(0.50f+0.50f*nz);
        unsigned char*o=px+(y*SMOKE_TEX+x)*4;
        o[0]=o[1]=o[2]=255; o[3]=(unsigned char)(a*255.f+0.5f); }
    glGenTextures(1,&g_smkTex); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g_smkTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,SMOKE_TEX,SMOKE_TEX,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
#ifdef REVA_PACKET
    gfxTextureHud(g_smkTex);
#endif
    static unsigned short ix[SMOKE_MAX*6];
    for(int k=0;k<SMOKE_MAX;k++){ unsigned short b=(unsigned short)(k*4);
        ix[k*6]=b; ix[k*6+1]=(unsigned short)(b+1); ix[k*6+2]=(unsigned short)(b+2);
        ix[k*6+3]=b; ix[k*6+4]=(unsigned short)(b+2); ix[k*6+5]=(unsigned short)(b+3); }
    glGenBuffers(1,&g_smkVB); glGenBuffers(1,&g_smkIB);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g_smkIB); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof ix,ix,GL_STATIC_DRAW);
    printf("[smoke] ready: %dx%d puff, cap %d sprites\n",SMOKE_TEX,SMOKE_TEX,SMOKE_MAX); fflush(stdout); }
static int smkCmp(const void*a,const void*b){
    float x=g_smk[*(const int*)a].d2, y=g_smk[*(const int*)b].d2; return (x<y)-(x>y); }   /* far first */
static void smokeDraw(const float*vp){
    if(!g_smokeOn||g_nSmk<=0) return;
    smkInitGL();
    if(g_smkProg==(GLuint)-1) return;
    static int ord[SMOKE_MAX]; int n=0;
    for(int i=0;i<g_nSmk;i++){ SmokeP*S=&g_smk[i]; float u=S->age/S->life;
        if(u>=1.f) continue;
        S->s=S->s0+(S->s1-S->s0)*powf(u,0.75f);
        S->a=S->a0*(0.35f+0.65f*smkSS(0.f,0.22f,S->age))*powf(1.f-u,1.5f);
        if(S->a<0.002f) continue;
        smkLight(S->p,S->c);
        float dx=S->p[0]-g_camEye[0], dy=S->p[1]-g_camEye[1], dz=S->p[2]-g_camEye[2];
        S->d2=dx*dx+dy*dy+dz*dz;
        ord[n++]=i; }
    if(!n) return;
    qsort(ord,(size_t)n,sizeof ord[0],smkCmp);
    static float vb[SMOKE_MAX*4*9];
    for(int j=0;j<n;j++){ const SmokeP*S=&g_smk[ord[j]];
        float d[3]={S->p[0]-g_camEye[0],S->p[1]-g_camEye[1],S->p[2]-g_camEye[2]};
        float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(l<1e-5f) l=1e-5f;
        d[0]/=l; d[1]/=l; d[2]/=l;
        float rt[3]={-d[2],0.f,d[0]}; float lr=sqrtf(rt[0]*rt[0]+rt[2]*rt[2]);   /* d x up */
        if(lr<1e-4f){ rt[0]=1.f; rt[2]=0.f; lr=1.f; }
        rt[0]/=lr; rt[2]/=lr;
        float up[3]={rt[1]*d[2]-rt[2]*d[1], rt[2]*d[0]-rt[0]*d[2], rt[0]*d[1]-rt[1]*d[0]};   /* rt x d */
        float cr=cosf(S->rot), sr=sinf(S->rot);
        float R[3], U[3];
        for(int a=0;a<3;a++){ R[a]=(rt[a]*cr+up[a]*sr)*S->s; U[a]=(up[a]*cr-rt[a]*sr)*S->s; }
        static const float CU[4][2]={{0.f,0.f},{1.f,0.f},{1.f,1.f},{0.f,1.f}};
        static const float CS[4][2]={{-1.f,-1.f},{1.f,-1.f},{1.f,1.f},{-1.f,1.f}};
        for(int k=0;k<4;k++){ float*o=vb+(j*4+k)*9;
            for(int a=0;a<3;a++) o[a]=S->p[a]+R[a]*CS[k][0]+U[a]*CS[k][1];
            o[3]=CU[k][0]; o[4]=CU[k][1];
            o[5]=S->c[0]; o[6]=S->c[1]; o[7]=S->c[2]; o[8]=S->a; } }
#ifdef REVA_PACKET
    if(!g_noDraw){ static float t9[SMOKE_MAX*6*9]; int m9=0; static const int Q9[6]={0,1,2,0,2,3};   /* far first, as they're sorted */
        for(int j=0;j<n;j++) for(int k=0;k<6;k++){ const float*o=vb+(j*4+Q9[k])*9; float*w=t9+m9*9; w[0]=o[0]; w[1]=o[1]; w[2]=o[2]; w[3]=o[5]; w[4]=o[6]; w[5]=o[7]; w[6]=o[8]; w[7]=o[3]; w[8]=o[4]; m9++; }
        gfxFxTex(t9,m9,g_smkTex); }
#endif
    GLint prev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prev);
    GLboolean cull=glIsEnabled(GL_CULL_FACE);
    glUseProgram(g_smkProg);
    glUniformMatrix4fv(g_smkVP,1,GL_FALSE,vp);
    glUniform1f(g_smkTone,g_tone);
    glUniform1i(g_smkSmp,0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,g_smkTex);
    glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBindBuffer(GL_ARRAY_BUFFER,g_smkVB);
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)((size_t)n*4*9*sizeof(float)),vb,GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,36,(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,36,(void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,36,(void*)20);
    glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g_smkIB);
    glDrawElements(GL_TRIANGLES,(GLsizei)(n*6),GL_UNSIGNED_SHORT,0);
    /* ⛔ HAND EVERYTHING BACK: the passes after this one assume opaque, depth-writing, their own program */
    glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
    if(cull) glEnable(GL_CULL_FACE);
    glUseProgram((GLuint)prev); }


/* THE MOUTH OPENING, in one place. Two tracks, multiplied:
 *   the ENVELOPE is the speech's own loudness -- it has the rhythm and the stress, and
 *   nothing else can give you those;
 *   the VISEME is the shape walked off the text -- and it has the CLOSURES, which the
 *   envelope cannot have, because /m/ /b/ /p/ are loud and shut.
 * A jaw on the envelope alone is a hinge opening on volume (jacob: "not very good mouth
 * movements"); a jaw on the viseme alone is a mouth reading a script with no voice in
 * it. The floor of 0.30 is so a quiet syllable still forms its shape instead of
 * vanishing. */
static float scnMouth(const void*Lv,float t);
static int scnActive(void){
    if(g_scnT<0.f) return -1;
    for(int i9=0;i9<g_scnN;i9++)
        if(g_scnT>=g_scnL[i9].t0&&g_scnT<g_scnL[i9].t0+g_scnL[i9].dur) return i9;
    return -1; }

/* ⭐ A SCENE IS STAGED BEFORE IT SPEAKS. scnActive() returns -1 until the first line's
 * TIME, which is exactly right for dialogue, subtitles and the mouth envelope -- and wrong
 * for STAGING. A scene that opens on a held beat spent it unstaged, with the bodies and the
 * camera still wherever the last shot left them.
 * ⛔ The cold open's new 3.4s opening image -- jacob's "a still room, reva just chilling,
 * thinking, as she does" -- came back BLACK for exactly this reason. It was invisible until
 * now only because every other scene opens with a beat of nearly zero.
 * The two questions are genuinely different, so they get two functions: this one says what
 * the picture should LOOK like, scnActive() still says who is TALKING. */
static int scnStageLine(void){
    if(g_scnT<0.f||g_scnN<=0) return -1;
    if(g_scnT<g_scnL[0].t0) return 0;      /* the opening beat IS line 0's image */
    return scnActive(); }
/* ⭐ ep 11: HOW LONG A LINE HOLDS THE PICTURE -- from its cut to the next line's: its words, the gap after them and
 * the next line's beat (a silence belongs to whoever just spoke), and for line 0 the opening beat before it too.
 * A cammove/camorbit with no camsecs= runs exactly this long, so it lands on the next cut. */
static float scnLineSpan(int l){
    if(l<0||l>=g_scnN) return 0.f;
    float a=(l==0)?0.f:g_scnL[l].t0, b=(l+1<g_scnN)?g_scnL[l+1].t0:g_scnLen;
    return (b>a+1e-3f)?(b-a):g_scnL[l].dur; }

static float scnMouth(const void*Lv,float t){
    const ScnLine*L=(const ScnLine*)Lv; if(!L) return 0.f;
    if(L->silent) return 0.f;   /* ⭐ ep 10: an ACTION line moves no mouth -- not even the hinge's resting 0.30 */
    int fi=(int)((t-L->t0)*30.f); if(fi<0) fi=0;
    float a=0.f, v=1.f;
    if(L->nEnv){ int k=fi>=L->nEnv?L->nEnv-1:fi; a=(float)L->env[k]/100.f; }
    if(L->nVis){ int k=fi>=L->nVis?L->nVis-1:fi; v=(float)L->vis[k]/100.f; }
    if(a>1.f) a=1.f;
    a=powf(a,0.55f);          /* speech RMS lives at 0.2-0.4; linear leaves it mumbling */
    /* ⭐ THE PUPPET JAW'S GAIN (ROBOT NEKO ep 4). `mouth hinge` drops the viseme track, which
     * is what leaves the jaw swinging on loudness alone -- and on a loud line that hinge opens
     * further than a puppet's ever would. Scaling it is the one dial the look was missing.
     * Only in hinge mode: with a viseme track the shape is the performance and this would
     * quietly flatten it. */
    return v*(0.30f+0.70f*a)*((L->nVis||g_jawGain<0.f)?1.f:g_jawGain); }

