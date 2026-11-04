/* ================= THE RAGDOLL (jacob: "we will need rag doll phisics for when they fall" / "remember, this is a
 * video game and animation engine") ===============================================================================
 * Any body, any time: the skeleton becomes 18 linked POINTS -- verlet, exactly like the hair chains, because the
 * engine already knows that solver -- with bone-length constraints, a stiffened torso and knee/elbow limits. It
 * falls under gravity, carries whatever speed the body had, and collides with the FLOOR, the furniture and the walls
 * through the same distance field the walking collision uses, at the limb thicknesses the contact pass measured off
 * her own mesh. Friction on the ground, so a slam slides and settles instead of skating on her face.
 *   FROM A SCENE: `hdoll=<sec>` / `pdoll=<sec>` / `p2doll=` / `p3doll=` -- that many seconds into the line she
 *   goes limp; `off` hands her back to the clip.  FROM GAMEPLAY: ragdollGo(slot) on a bail, a hit, a fall.
 *   The write-back is the chain solver's: each bone is turned from its animated direction to its simulated one
 *   (chainArc), root first, so fingers and twist bones follow the limb they hang off.
 * ⛔ A HELD EXPOSURE DOES NOT INTEGRATE (ep 1): stop motion holds the fall between exposures like everything else. */
#define RD_J 18
#define RD_MU 0.55f   /* Coulomb: the share of tangential speed a full-depth contact takes */
enum { RD_HIPS, RD_CHEST, RD_NECK, RD_HEAD, RD_SHL, RD_ELL, RD_HNL, RD_SHR, RD_ELR, RD_HNR,
       RD_THL, RD_KNL, RD_ANL, RD_TOL, RD_THR, RD_KNR, RD_ANR, RD_TOR };
static const signed char RD_PAR[RD_J]={ -1, RD_HIPS, RD_CHEST, RD_NECK, RD_CHEST, RD_SHL, RD_ELL, RD_CHEST, RD_SHR, RD_ELR,
                                        RD_HIPS, RD_THL, RD_KNL, RD_ANL, RD_HIPS, RD_THR, RD_KNR, RD_ANR };
/* the bone each joint TURNS (its primary child); -1 = a leaf, it follows its parent */
static const signed char RD_KID[RD_J]={ RD_CHEST, RD_NECK, RD_HEAD, -1, RD_ELL, RD_HNL, -1, RD_ELR, RD_HNR, -1,
                                        RD_KNL, RD_ANL, RD_TOL, -1, RD_KNR, RD_ANR, RD_TOR, -1 };
static const char*RD_NM[RD_J]={"hips","chest","neck","head","shoulderL","elbowL","handL","shoulderR","elbowR","handR",
                               "thighL","kneeL","ankleL","toeL","thighR","kneeR","ankleR","toeR"};
typedef struct { int on,coulomb; float lift[RD_J]; Pack*P; float t; int node[RD_J]; float p[RD_J][3], q[RD_J][3], len[RD_J], rad[RD_J], mass[RD_J];
                 float reach[RD_J]; int use[RD_J]; float fsign; float bend[4]; unsigned long frame;
                 float v0[3]; int seeded; int haveMem; int haveTor; float torV[3]; float tors[3]; float rest; int asleep;
                 float trx[2], trz[2], trzb[2];   /* ⭐ the TORSO's measured half-widths: a chest is not a ball */
                 float hinge[4][3];   /* ⭐ each knee's/elbow's bend axis, in the body frame (lat,up,fwd) */
                 float hp0[3]; int hp0ok;   /* ⭐ ep 13: the hips a frame ago, for the rest test */
                 float downT;               /* ⭐ ep 13: seconds spent DOWN -- two and a half of them is at rest */
                 float skin; } Ragdoll;   /* tors: hips-neck, shoulder-shoulder, thigh-thigh, AS MEASURED */
/* fsign: cross(shoulder-to-shoulder, hips-to-chest) times this points FORWARD out of the chest.
 * bend[]: which way the knee/elbow was bent in the pose she bailed from (kneeL, kneeR, elbowL, elbowR). */   /* reach: parent-to-grandchild limit (a knee does not bend backwards) */
#define RD_GAME 16   /* a game's bodies going limp: this many at once, after the cast's own, and then one for its hero */
#define RD_HERO (SCN_MAXCAST+RD_GAME)
static Ragdoll g_rd[RD_HERO+1];
static int g_rdOff=-1;
static int g_rdSay=0;   /* the first write-back after a cue says what it did to the rig */
static int rdActive(int slot){ return slot>=0&&slot<=RD_HERO&&g_rd[slot].on; }
/* where a limp body actually IS: her hips, and the floor under them (what every lens tracks while she falls) */
static float rdFloorAt(const float*p);
static int rdHipsWorld(int slot,float*o){
    if(!rdActive(slot)||!g_rd[slot].seeded) return 0;
    const float*h=g_rd[slot].p[RD_HIPS];
    o[0]=h[0]; o[2]=h[2]; o[1]=rdFloorAt(h);
    return 1; }
static void rdNodes(Pack*P,int*nd){
    int chest=(P->pb[3]>=0)?P->pb[3]:((P->pb[2]>=0)?P->pb[2]:P->pb[1]);
    CaRest*R=caRestOf(P);
    nd[RD_HIPS]=P->pb[0];  nd[RD_CHEST]=chest;    nd[RD_NECK]=P->pb[4]; nd[RD_HEAD]=P->pb[5];
    nd[RD_SHL]=P->pb[7];   nd[RD_ELL]=P->pb[8];   nd[RD_HNL]=P->gripL;
    nd[RD_SHR]=P->pb[10];  nd[RD_ELR]=P->pb[11];  nd[RD_HNR]=P->gripNode;
    nd[RD_THL]=P->pb[12];  nd[RD_KNL]=P->pb[13];  nd[RD_ANL]=P->footL;  nd[RD_TOL]=R->toe[0];
    nd[RD_THR]=P->pb[14];  nd[RD_KNR]=P->pb[15];  nd[RD_ANR]=P->footR;  nd[RD_TOR]=R->toe[1]; }
/* the world floor under a point (the audit grid's own, else y 0) */
static float rdFloorAt(const float*p){   /* forward-declared above, for the lenses */
    if(g_spOn) return spTopBelow(p[0],p[2],p[1],NULL);   /* on a set that says `climb`, what's really there */
    const CaGrid*G=&g_caG; if(!G->fl) return 0.f;
    int cx=(int)floorf((p[0]-G->x0)/G->cell), cz=(int)floorf((p[2]-G->z0)/G->cell);
    if(cx<0||cz<0||cx>=G->W||cz>=G->H) return 0.f;
    int k=cz*G->W+cx; float fy=G->fy[k];
    if((G->fl[k]&CA_OB)&&G->ty[k]<p[1]+0.35f&&G->ty[k]>fy) return G->ty[k];   /* she landed ON something */
    return fy; }
/* the last two frames of every cast body's ragdoll joints, in world space, and the clock between them */
#define RD_RING 24   /* frames of torso history: ~0.4 s at 60, ~0.8 s at 30 */
static float g_rdRing[SCN_MAXCAST][RD_RING][3]; static float g_rdRingDt[SCN_MAXCAST][RD_RING];
static int g_rdRingN[SCN_MAXCAST];
static float g_rdMem[SCN_MAXCAST][RD_J][3]; static float g_rdMemV[SCN_MAXCAST][RD_J][3];
static int g_rdMemHas[SCN_MAXCAST]; static unsigned long g_rdMemFrame[SCN_MAXCAST]; static Pack*g_rdMemP[SCN_MAXCAST];
static void rdRemember(Pack*P,M4*W,const float*model,int slot){
    if(slot<0||slot>=SCN_MAXCAST||!P||!W||!model) return;
    if(g_rdMemFrame[slot]==g_frameNo) return;           /* once a frame, not once a draw pass */
    if(rdActive(slot)) return;                          /* a limp body's pose is the doll's own */
    float dts=(float)g_animDtS;
    int nd[RD_J]; rdNodes(P,nd);
    int fresh=(g_rdMemP[slot]==P&&g_rdMemHas[slot]&&g_rdMemFrame[slot]+1==g_frameNo&&dts>1e-4f);
    for(int j=0;j<RD_J;j++){
        float w[3]={0,0,0};
        int n=nd[j];
        if(n>=0&&(unsigned)n<P->nNodes) ptWorld(W[n],model,w);
        if(fresh){ for(int k=0;k<3;k++){ float v=(w[k]-g_rdMem[slot][j][k])/dts;
                       if(v>20.f) v=20.f;
                       if(v<-20.f) v=-20.f;
                       g_rdMemV[slot][j][k]+=(v-g_rdMemV[slot][j][k])*0.5f; } }
        else for(int k=0;k<3;k++) g_rdMemV[slot][j][k]=0.f;
        memcpy(g_rdMem[slot][j],w,12); }
    /* and the TORSO's own track: the midpoint of hips and chest, frame by frame */
    { float mid[3]; for(int k=0;k<3;k++) mid[k]=(g_rdMem[slot][RD_HIPS][k]+g_rdMem[slot][RD_CHEST][k])*0.5f;
      int n=g_rdRingN[slot]%RD_RING;
      memcpy(g_rdRing[slot][n],mid,12); g_rdRingDt[slot][n]=dts; g_rdRingN[slot]++; }
    g_rdMemP[slot]=P; g_rdMemHas[slot]=1; g_rdMemFrame[slot]=g_frameNo; }
/* the torso's velocity averaged over the last `back` frames -- what a viewer calls "the direction she was going" */
static int rdTorsoVel(int slot,int back,float*o){
    if(slot<0||slot>=SCN_MAXCAST) return 0;
    int have=g_rdRingN[slot]; if(have<2) return 0;
    if(back>RD_RING-1) back=RD_RING-1;
    if(back>have-1) back=have-1;
    if(back<1) return 0;
    int last=(g_rdRingN[slot]-1)%RD_RING, first=(g_rdRingN[slot]-1-back)%RD_RING;
    float T=0.f; for(int i=1;i<=back;i++){ int k=(g_rdRingN[slot]-i)%RD_RING; T+=g_rdRingDt[slot][k]; }
    if(T<1e-4f) return 0;
    for(int k=0;k<3;k++) o[k]=(g_rdRing[slot][last][k]-g_rdRing[slot][first][k])/T;
    return 1; }
static void ragdollGo(int slot,Pack*P,M4*W,const float*model,const float*vel){
    if(g_rdOff<0){ const char*e=getenv("REVA_NORAGDOLL"); g_rdOff=(e&&e[0]&&e[0]!='0')?1:0; }
    if(g_rdOff||slot<0||slot>RD_HERO||!P||!W||!model) return;
    Ragdoll*R=&g_rd[slot]; memset(R,0,sizeof*R); R->P=P; R->on=1; R->t=0.f;
    rdNodes(P,R->node);
    CtShape*S=ctShapeOf(P,W);
    float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); if(sc<1e-6f) sc=1.f;
    static const signed char SEG[RD_J]={CT_SPINE,CT_CHEST,CT_HEAD,CT_HEAD,CT_UAL,CT_FAL,CT_FAL,CT_UAR,CT_FAR,CT_FAR,
                                        CT_THL,CT_SHL,CT_FTL,CT_FTL,CT_THR,CT_SHR,CT_FTR,CT_FTR};
    /* ⛔ A MISSING TOE IS NOT A MISSING BODY. anima (kaeli) has no toe bones at all and the first cue REFUSED
     * her outright. The four extremities are OPTIONAL -- the limb simply ends at the ankle or the wrist. */
    static const signed char OPT[RD_J]={0,0,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,1};
    int nskip=0;
    for(int j=0;j<RD_J;j++){
        int n=R->node[j];
        R->use[j]=1;
        if(n<0||(unsigned)n>=P->nNodes){
            if(OPT[j]){ R->use[j]=0; nskip++; continue; }
            R->on=0; printf("[ragdoll] slot %d: %s has no %s bone -- no ragdoll on this body\n",slot,P->label,RD_NM[j]); fflush(stdout); return; }
        ptWorld(W[n],model,R->p[j]);
        float rr=(S&&S->ok&&S->r[(int)SEG[j]]>0.f)?S->r[(int)SEG[j]]*sc:0.06f*sc;
        if(rr<0.03f) rr=0.03f;
        R->rad[j]=rr;
        R->mass[j]=(j==RD_HIPS||j==RD_CHEST)?3.0f:((j==RD_HEAD||j==RD_NECK)?1.4f:1.0f); }
    /* ⛔ A BODY ON THE FLOOR IS AS THICK AS IT IS (jacob, on the slam: "she half clips through the floor"). The
     * fall held every joint one LIMB radius above the ground -- but a chest and a hip are ellipses half again as
     * deep as that capsule, so the torso lay buried to its waist. The torso's own measured cross-section is carried
     * in, and the floor holds each joint up by the ellipse's reach IN THE DIRECTION IT IS LYING. */
    for(int t=0;t<2;t++){ R->trx[t]=(S&&S->ok)?S->rx[t]*sc:0.16f; R->trz[t]=(S&&S->ok)?S->rz[t]*sc:0.12f; R->trzb[t]=(S&&S->ok)?S->rzb[t]*sc:0.12f; }
    R->skin=0.02f*sc;   /* and the mesh is a little outside every capsule */
    if(nskip){ printf("[ragdoll] slot %d %s: %d joint(s) this body does not have --",slot,P->label,nskip);
        for(int j=0;j<RD_J;j++) if(!R->use[j]) printf(" %s",RD_NM[j]);
        printf(" (the limb ends at its parent)\n"); }
    for(int j=0;j<RD_J;j++){ int pj=RD_PAR[j];
        if(!R->use[j]){ R->len[j]=R->reach[j]=0.f; continue; }
        R->len[j]=(pj>=0)?sqrtf((R->p[j][0]-R->p[pj][0])*(R->p[j][0]-R->p[pj][0])+(R->p[j][1]-R->p[pj][1])*(R->p[j][1]-R->p[pj][1])+(R->p[j][2]-R->p[pj][2])*(R->p[j][2]-R->p[pj][2])):0.f;
        int gp=(pj>=0)?RD_PAR[pj]:-1;
        R->reach[j]=(gp>=0)?(R->len[j]+R->len[pj])*0.97f:0.f; }
    /* ⛔ WHICH WAY HER KNEES BEND. Without this the solver folded her legs FORWARD through her own pelvis and
     * the skin came back as a knot -- a ragdoll with bone lengths and no hinges is a bag of sticks. The body's
     * own frame (shoulders x spine) gives a forward; the bail pose says which side of the leg the knee is on. */
    { float up[3],rt[3],fw[3];
      ctSub(up,R->p[RD_CHEST],R->p[RD_HIPS]); ctNorm(up);
      ctSub(rt,R->p[RD_SHR],R->p[RD_SHL]); ctNorm(rt);
      ctCross(fw,rt,up); ctNorm(fw);
      float mz[3]={model[8],model[9],model[10]}; float ml=sqrtf(ctDot(mz,mz)); if(ml>1e-6f){ mz[0]/=ml; mz[1]/=ml; mz[2]/=ml; }
      R->fsign=(ctDot(fw,mz)>=0.f)?1.f:-1.f;
      fw[0]*=R->fsign; fw[1]*=R->fsign; fw[2]*=R->fsign;
      static const signed char MID[4]={RD_KNL,RD_KNR,RD_ELL,RD_ELR}, TOP[4]={RD_THL,RD_THR,RD_SHL,RD_SHR}, BOT[4]={RD_ANL,RD_ANR,RD_HNL,RD_HNR};
      static const float DEF[4]={1.f,1.f,-1.f,-1.f};   /* a knee bends forward, an elbow back */
      for(int c=0;c<4;c++){ R->bend[c]=DEF[c];
          if(!R->use[MID[c]]||!R->use[TOP[c]]||!R->use[BOT[c]]) continue;
          float mid[3]; for(int k=0;k<3;k++) mid[k]=(R->p[TOP[c]][k]+R->p[BOT[c]][k])*0.5f;
          float d[3]; ctSub(d,R->p[MID[c]],mid);
          float dp=ctDot(d,fw);
          if(fabsf(dp)>0.02f) R->bend[c]=(dp>0.f)?1.f:-1.f;
          /* ⭐ AND THE AXIS IT TURNS ABOUT. A knee is a HINGE: it has one axis and the shin swings in one plane.
           * Taken off the pose she bailed in (thigh x shin), and kept in the body's frame so it rides her tumble. */
          float ax9[3]={0.f,0.f,0.f};
          if(R->use[MID[c]]&&R->use[TOP[c]]&&R->use[BOT[c]]){
              float u1[3],u2[3]; ctSub(u1,R->p[MID[c]],R->p[TOP[c]]); ctSub(u2,R->p[BOT[c]],R->p[MID[c]]);
              ctNorm(u1); ctNorm(u2); ctCross(ax9,u1,u2);
              if(ctNorm(ax9)<0.12f){ ctCross(ax9,u1,fw); ctNorm(ax9); } }
          if(ax9[0]==0.f&&ax9[1]==0.f&&ax9[2]==0.f){ memcpy(ax9,rt,12); }
          R->hinge[c][0]=ctDot(ax9,rt); R->hinge[c][1]=ctDot(ax9,up); R->hinge[c][2]=ctDot(ax9,fw); } }
    { static const signed char A9[3]={RD_HIPS,RD_SHL,RD_THL}, B9[3]={RD_NECK,RD_SHR,RD_THR};
      for(int c=0;c<3;c++){ float d[3]; ctSub(d,R->p[(int)B9[c]],R->p[(int)A9[c]]); R->tors[c]=sqrtf(ctDot(d,d)); } }
    /* the speed she had, laid in on the FIRST step, where the substep is known */
    float v[3]={vel?vel[0]:0.f,vel?vel[1]:0.f,vel?vel[2]:0.f};
    for(int k=0;k<3;k++) R->v0[k]=v[k];
    R->seeded=0; R->haveMem=0;
    if(slot<SCN_MAXCAST&&g_rdMemHas[slot]&&g_rdMemP[slot]==P&&g_rdMemFrame[slot]+1>=g_frameNo){
        float fast=0.f;
        for(int j=0;j<RD_J;j++){ float l=sqrtf(g_rdMemV[slot][j][0]*g_rdMemV[slot][j][0]+g_rdMemV[slot][j][1]*g_rdMemV[slot][j][1]+g_rdMemV[slot][j][2]*g_rdMemV[slot][j][2]);
            if(l>fast) fast=l; }
        if(fast>0.15f){ R->haveMem=1; } }
    R->haveTor=rdTorsoVel(slot,8,R->torV);
    if(R->haveMem||R->haveTor){
        float t1[3]={0,0,0},t2[3]={0,0,0}; rdTorsoVel(slot,2,t1); rdTorsoVel(slot,16,t2);
        printf("[ragdoll] slot %d: the way she was going -- torso over 2 frames (%.2f,%.2f), over 8 (%.2f,%.2f), over 16 (%.2f,%.2f); hips this frame (%.2f,%.2f)\n",
               slot,t1[0],t1[2],R->torV[0],R->torV[2],t2[0],t2[2],g_rdMemV[slot][RD_HIPS][0],g_rdMemV[slot][RD_HIPS][2]); }
    for(int j=0;j<RD_J;j++) memcpy(R->q[j],R->p[j],12);
    { const char*e=getenv("REVA_RDDBG"); g_rdSay=(e&&e[0]&&e[0]!='0')?1:0; }
    printf("[ragdoll] slot %d %s: LIMP at (%.2f,%.2f,%.2f), carrying %.2f u/s\n",slot,P->label,R->p[RD_HIPS][0],R->p[RD_HIPS][1],R->p[RD_HIPS][2],sqrtf(v[0]*v[0]+v[2]*v[2]));
    fflush(stdout); }
/* how far a joint's flesh reaches straight DOWN: a limb is a capsule (one radius), the hips and the chest are
 * ellipses, so their reach depends on whether she came down on her front, her back or her side. */
static int g_rdNoHinge=-1,g_rdNoCone=-1,g_rdNoSelf=-1;
static void rdSwitches(void){
    if(g_rdNoHinge>=0) return;
    const char*a=getenv("REVA_RDHINGE"),*b=getenv("REVA_RDNOCONE"),*c=getenv("REVA_RDNOSELF");
    g_rdNoHinge=(a&&a[0]&&a[0]!='0')?0:1;   /* OFF unless asked for: it fought the bone lengths */
    g_rdNoCone=(b&&b[0]&&b[0]!='0')?1:0; g_rdNoSelf=(c&&c[0]&&c[0]!='0')?1:0;
    printf("[ragdoll] joints: bend-limit ON, cone %s, self %s, hinge-plane %s\n",
           g_rdNoCone?"OFF":"ON",g_rdNoSelf?"OFF":"ON",g_rdNoHinge?"off (REVA_RDHINGE=1 turns it on)":"ON"); fflush(stdout); }
static void rdShift(Ragdoll*R,int j,const float*d,float k){
    for(int i=0;i<3;i++){ R->p[j][i]+=d[i]*k; R->q[j][i]+=d[i]*k; } }
static float rdDown(const Ragdoll*R,int j){
    if(j!=RD_HIPS&&j!=RD_CHEST) return R->rad[j]+R->skin;
    int t=(j==RD_CHEST)?1:0;
    float up[3],lat[3],fwd[3];
    for(int k=0;k<3;k++){ up[k]=R->p[RD_CHEST][k]-R->p[RD_HIPS][k];
        lat[k]=(R->use[RD_SHR]&&R->use[RD_SHL])?(R->p[RD_SHR][k]-R->p[RD_SHL][k]):0.f; }
    if(ctNorm(up)<1e-5f) return R->rad[j]+R->skin;
    if(ctNorm(lat)<1e-5f){ lat[0]=1.f; lat[1]=0.f; lat[2]=0.f; }
    ctCross(fwd,lat,up); if(ctNorm(fwd)<1e-5f) return R->rad[j]+R->skin;
    ctCross(lat,up,fwd); ctNorm(lat);
    float dn[3]={0.f,-1.f,0.f};
    float dx=ctDot(dn,lat), dz=ctDot(dn,fwd);
    float rx=R->trx[t], rz=(dz>=0.f)?R->trz[t]:R->trzb[t];
    if(rx<1e-4f||rz<1e-4f) return R->rad[j]+R->skin;
    float e=sqrtf((dx*dx)/(rx*rx)+(dz*dz)/(rz*rz));
    float reach=(e>1e-5f)?(sqrtf(dx*dx+dz*dz)/e):rz;
    if(reach<R->rad[j]) reach=R->rad[j];
    return reach+R->skin; }
/* ⛔ 11-04: A GAME'S BODY STANDING WITH ITS FEET ON THE FLOOR. Two things kicked its feet half a metre into the air as it went down. An ankle starts nearer the floor than a limb's radius, because a foot is thinner than a shin, and being put right in one step is a speed of 14 units a second up the whole leg: so that much of the floor's clearance comes in over a moment (lift). And the bones' own limit is 97% of a limb's length, so a straight leg breaks it on the first step, and the only way it can give is along the leg: what it's standing in is the limit for it, and its knees start forward, so there's somewhere for them to go. Film's bodies fall from a stride, and don't meet this */
static void rdFold(Ragdoll*R,float dt){
    for(int j=0;j<RD_J;j++){ if(!R->use[j]) continue; const float need=rdFloorAt(R->p[j])+rdDown(R,j)-R->p[j][1]; R->lift[j]=need>0.f?need:0.f; }
    for(int j=0;j<RD_J;j++){ const int pj=RD_PAR[j],gp=pj>=0?RD_PAR[pj]:-1; if(gp<0||!R->use[j]||!R->use[pj]||!R->use[gp]) continue;
        float d[3]; ctSub(d,R->p[j],R->p[gp]); const float l=sqrtf(ctDot(d,d)); if(l>R->reach[j]) R->reach[j]=l; }
    float up[3],rt[3],fw[3]; ctSub(up,R->p[RD_CHEST],R->p[RD_HIPS]); ctNorm(up); ctSub(rt,R->p[RD_SHR],R->p[RD_SHL]); ctNorm(rt); ctCross(fw,rt,up); if(ctNorm(fw)<1e-5f) return;
    static const signed char K[2]={RD_KNL,RD_KNR};
    for(int c=0;c<2;c++){ const int j=K[c]; if(R->use[j]) for(int k=0;k<3;k++) R->q[j][k]-=fw[k]*R->fsign*1.6f*dt; } }
static void rdStep(Ragdoll*R,float dt){
    rdSwitches();
    /* ⭐ ep 13: ONCE SHE IS DOWN AND SLOW THE CONE RULE STANDS DOWN. Lying face down with a leg folded, a
     * persistently violated hip cone turned the limb a couple of degrees a pass, the bone lengths pulled the hips
     * after it and the floor pushed back: a ratchet that walked her BACKWARDS 0.7 u/s for seven seconds while the
     * phone was on her, with a velocity term of nothing. The cone's job is the fall; on the floor the lengths, the
     * bends and the floor are enough. */
    int settled9=0;
    if(R->seeded&&R->t>0.8f){ float fy9=rdFloorAt(R->p[RD_HIPS])+rdDown(R,RD_HIPS)+0.25f;
        float vx9=(R->p[RD_HIPS][0]-R->q[RD_HIPS][0])/dt, vz9=(R->p[RD_HIPS][2]-R->q[RD_HIPS][2])/dt;
        settled9=(R->p[RD_HIPS][1]<fy9&&vx9*vx9+vz9*vz9<0.36f); }
    const float GR=-CLUB_UNITS_PER_S2*1.0f;
    float dmp=powf(0.992f,dt*60.f);
    if(!R->seeded){ R->seeded=1;
        int sl=(int)(R-&g_rd[0]);
        if(R->haveMem||R->haveTor){
            /* the torso's averaged travel carries the whole body; each joint keeps a quarter of its own motion on
             * top, so an arm that was swinging still trails; and a forward roll about the body's lateral axis puts
             * her over the front instead of sledging along on her chest. */
            float base[3]={0,0,0};
            if(R->haveTor) memcpy(base,R->torV,12);
            else memcpy(base,g_rdMemV[sl][RD_HIPS],12);
            float lat[3],up9[3],fw9[3];
            ctSub(up9,R->p[RD_CHEST],R->p[RD_HIPS]); ctNorm(up9);
            ctSub(lat,R->p[RD_SHR],R->p[RD_SHL]);   ctNorm(lat);
            ctCross(fw9,lat,up9); ctNorm(fw9);
            float sp9=sqrtf(base[0]*base[0]+base[2]*base[2]);
            /* the roll: a rotation about the axis across her travel, at ~1.2 rad/s per u/s of speed, capped */
            float rax[3]={0,0,0};
            if(sp9>0.2f){ float dir[3]={base[0]/sp9,0.f,base[2]/sp9};
                rax[0]=dir[2]; rax[2]=-dir[0];                     /* horizontal, across the direction of travel */
                float w9=0.45f*sp9; if(w9>2.2f) w9=2.2f;   /* a roll, not a cartwheel */
                rax[0]*=w9; rax[1]*=w9; rax[2]*=w9; }
            float com[3]={0,0,0}; for(int k=0;k<3;k++) com[k]=(R->p[RD_HIPS][k]+R->p[RD_CHEST][k])*0.5f;
            for(int j=0;j<RD_J;j++){
                float r9[3]; ctSub(r9,R->p[j],com);
                float spin[3]; ctCross(spin,rax,r9);
                for(int k=0;k<3;k++){
                    float v=base[k]+spin[k]+(R->haveMem?(g_rdMemV[sl][j][k]-g_rdMemV[sl][RD_HIPS][k])*0.25f:0.f);
                    R->q[j][k]=R->p[j][k]-v*dt; } } }
        else { static const float LEAD[RD_J]={1.00f,1.35f,1.50f,1.60f,
                                              1.30f,1.35f,1.25f,
                                              1.30f,1.35f,1.25f,
                                              0.85f,0.65f,0.50f,0.45f,
                                              0.85f,0.65f,0.50f,0.45f};
            for(int j=0;j<RD_J;j++) for(int k=0;k<3;k++) R->q[j][k]=R->p[j][k]-R->v0[k]*dt*LEAD[j];
            if(R->coulomb) rdFold(R,dt); } }
    if(R->coulomb) for(int j=0;j<RD_J;j++) if(R->lift[j]>0.f){ R->lift[j]-=dt*0.5f; if(R->lift[j]<0.f) R->lift[j]=0.f; }
    for(int j=0;j<RD_J;j++){
        float np[3];
        if(!R->use[j]) continue;
        for(int k=0;k<3;k++){ float vel=(R->p[j][k]-R->q[j][k])*dmp;
            np[k]=R->p[j][k]+vel+((k==1)?GR*dt*dt:0.f); }
        memcpy(R->q[j],R->p[j],12); memcpy(R->p[j],np,12); }
    /* ⛔ HALF A LINK PER STEP (the braid rule, [[chain_solver_limits]]): a step longer than half the shortest
     * bone lets a distance constraint recover a bone's LENGTH but not the body's SHAPE -- that was hair that
     * snapped instead of swinging, and here it is a leg that whips. The ceiling is the body's own smallest bone. */
    { float mn9=1e9f; for(int j=0;j<RD_J;j++) if(R->use[j]&&R->len[j]>1e-4f&&R->len[j]<mn9) mn9=R->len[j];
      if(mn9>1e8f) mn9=0.2f;
      const float VMAX=mn9*0.5f;
      for(int j=0;j<RD_J;j++){ if(!R->use[j]) continue;
          float v[3]; ctSub(v,R->p[j],R->q[j]); float vl=sqrtf(ctDot(v,v));
          if(vl>VMAX&&vl>1e-8f){ float k9=VMAX/vl; for(int k=0;k<3;k++) R->q[j][k]=R->p[j][k]-v[k]*k9; } } }
    /* ⛔ THE ORDER MATTERS. With the joint limits AFTER the length solver the two fed each other -- the
     * hinge moved a knee, the bones moved it back, and [rd:fast] (which names the fastest joint in the body)
     * showed that knee running at 13 u/s under a body lying still. The limits go FIRST; the lengths have the
     * last word, and a length correction is what the solver is allowed to damp. */
        /* ⭐ THE JOINTS (jacob: "thier limbs should only go to believable directions, our legs are bending in
         * unnatural ways"). Three rules, all positional, all in the body's own frame so they ride her tumble:
         *   1. a knee and an elbow are HINGES -- the middle joint stays in the plane its own bend axis defines,
         *      so a shin cannot swing sideways out of its leg;
         *   2. it bends ONE WAY -- the side it was bent when she bailed, by at least a fifth of the limb;
         *   3. a thigh and an upper arm live in a CONE off the pelvis and the chest: a leg does not fold up
         *      behind the back or split sideways past the hip's range. */
        { float up[3],rt[3],fw[3];
          ctSub(up,R->p[RD_CHEST],R->p[RD_HIPS]); float ul=ctNorm(up);
          ctSub(rt,R->p[RD_SHR],R->p[RD_SHL]); float rl=ctNorm(rt);
          if(ul>1e-5f&&rl>1e-5f){
            ctCross(fw,rt,up); if(ctNorm(fw)>1e-5f){
              fw[0]*=R->fsign; fw[1]*=R->fsign; fw[2]*=R->fsign;
              ctCross(rt,up,fw); ctNorm(rt);
              static const signed char MID[4]={RD_KNL,RD_KNR,RD_ELL,RD_ELR}, TOP[4]={RD_THL,RD_THR,RD_SHL,RD_SHR}, BOT[4]={RD_ANL,RD_ANR,RD_HNL,RD_HNR};
              static const signed char ROOT[4]={RD_HIPS,RD_HIPS,RD_CHEST,RD_CHEST};
              /* how far the limb's root bone may swing off straight down (legs) or off down-and-out (arms) */
              static const float CONE[4]={1.75f,1.75f,2.40f,2.40f};   /* radians from the cone's axis */
              for(int c=0;c<4;c++){ int m=MID[c],a=TOP[c],b=BOT[c],rt9=ROOT[c];
                  if(!R->use[m]||!R->use[a]||!R->use[b]) continue;
                  if(!g_rdNoHinge){
                  /* --- 1. the hinge plane: the knee may not leave it ------------------------------------- */
                  float hx[3]; for(int k=0;k<3;k++) hx[k]=rt[k]*R->hinge[c][0]+up[k]*R->hinge[c][1]+fw[k]*R->hinge[c][2];
                  if(ctNorm(hx)>1e-5f){
                      float mid[3]; for(int k=0;k<3;k++) mid[k]=(R->p[a][k]+R->p[b][k])*0.5f;
                      float d[3]; ctSub(d,R->p[m],mid);
                      float off=ctDot(d,hx);
                      rdShift(R,m,hx,-off*0.18f); rdShift(R,a,hx,off*0.045f); rdShift(R,b,hx,off*0.045f); }
                  /* --- 2. and it bends the way it bends -------------------------------------------------- */
                  { float mid[3]; for(int k=0;k<3;k++) mid[k]=(R->p[a][k]+R->p[b][k])*0.5f;
                    float d[3]; ctSub(d,R->p[m],mid);
                    float bd[3]; ctCross(bd,hx,(float[3]){R->p[b][0]-R->p[a][0],R->p[b][1]-R->p[a][1],R->p[b][2]-R->p[a][2]});
                    if(ctNorm(bd)>1e-5f){
                        float want=0.02f*(R->len[m]+R->len[b]);   /* just past straight: a LIMIT, not a bend */
                        float have=ctDot(d,bd)*R->bend[c];
                        if(have<want){ float push=(want-have)*0.15f*R->bend[c];
                            rdShift(R,m,bd,push*0.72f); rdShift(R,a,bd,-push*0.14f); rdShift(R,b,bd,-push*0.14f); } } }
                  }
                  /* --- 3. the cone at the hip / the shoulder --------------------------------------------- */
                  if(!g_rdNoCone&&!settled9){ float axis[3]; for(int k=0;k<3;k++) axis[k]=-up[k];
                    float dir[3]; ctSub(dir,R->p[m],R->p[a]); float dl=ctNorm(dir);
                    if(dl>1e-5f){
                        float cs=ctDot(dir,axis); if(cs>1.f)cs=1.f; if(cs<-1.f)cs=-1.f;
                        float ang=acosf(cs);
                        if(ang>CONE[c]){
                            float side[3]; ctCross(side,axis,dir);
                            if(ctNorm(side)>1e-5f){
                                float ex=ang-CONE[c]; if(ex>0.035f) ex=0.035f;   /* a couple of degrees a pass */
                            float cw=cosf(-ex), sw=sinf(-ex);
                                /* Rodrigues: turn the limb back toward its cone, carrying the whole limb */
                                int ids[3]={m,b,-1}; if(c<2) ids[2]=(c==0)?RD_TOL:RD_TOR;
                                for(int q9=0;q9<3;q9++){ int id=ids[q9]; if(id<0||!R->use[id]) continue;
                                    float v[3]; ctSub(v,R->p[id],R->p[a]);
                                    float cr[3]; ctCross(cr,side,v); float dd=ctDot(side,v);
                                    for(int k=0;k<3;k++) R->p[id][k]=R->p[a][k]+v[k]*cw+cr[k]*sw+side[k]*dd*(1.f-cw);
                                    float w9[3]; ctSub(w9,R->q[id],R->p[a]);
                                    float cr2[3]; ctCross(cr2,side,w9); float dd2=ctDot(side,w9);
                                    for(int k=0;k<3;k++) R->q[id][k]=R->p[a][k]+w9[k]*cw+cr2[k]*sw+side[k]*dd2*(1.f-cw); } } } }
                    (void)rt9; } } } } }
        /* ⭐ AND SHE DOES NOT PASS THROUGH HERSELF: every limb joint is kept out of the hips and the chest, and
         * the knees and the ankles out of each other, at the radii the contact pass measured off her mesh. */
        if(!g_rdNoSelf){ static const signed char LIMB[9]={RD_KNL,RD_ANL,RD_KNR,RD_ANR,RD_ELL,RD_HNL,RD_ELR,RD_HNR,RD_HEAD};
          static const signed char CORE[2]={RD_HIPS,RD_CHEST};
          for(int c=0;c<2;c++) for(int q=0;q<9;q++){ int a=CORE[c], b=LIMB[q];
              if(!R->use[a]||!R->use[b]) continue;
              float d[3]; ctSub(d,R->p[b],R->p[a]); float dl=sqrtf(ctDot(d,d));
              float mn=(R->rad[a]+R->rad[b])*0.72f; if(dl>=mn||dl<1e-6f) continue;
              float wa=R->mass[a]/(R->mass[a]+R->mass[b]), push=(mn-dl)/dl*0.5f;
              rdShift(R,b,d,push*wa); rdShift(R,a,d,-push*(1.f-wa)); }
          static const signed char PR[2][2]={{RD_KNL,RD_KNR},{RD_ANL,RD_ANR}};
          for(int c=0;c<2;c++){ int a=PR[c][0], b=PR[c][1];
              if(!R->use[a]||!R->use[b]) continue;
              float d[3]; ctSub(d,R->p[b],R->p[a]); float dl=sqrtf(ctDot(d,d));
              float mn=(R->rad[a]+R->rad[b])*0.9f; if(dl>=mn||dl<1e-6f) continue;
              float push=(mn-dl)/dl*0.5f;
              rdShift(R,b,d,push); rdShift(R,a,d,-push); } }
    /* 6 passes: the deepest chain here is hips->thigh->knee->ankle->toe (5 links) and a Gauss-Seidel pass
     * carries a correction exactly ONE LINK (this repo's own braid lesson, [[chain_solver_limits]]).
     * Jakobsen's "3 to 4 relaxation iterations are enough" is for tetrahedra; a five-link limb needs five.
     * Ten only gave the custom rules more passes in which to fight the bones. */
    for(int it=0;it<6;it++){
        for(int j=0;j<RD_J;j++){ int pj=RD_PAR[j]; if(pj<0||R->len[j]<1e-6f) continue;
            float d[3]; for(int k=0;k<3;k++) d[k]=R->p[j][k]-R->p[pj][k];
            float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-8f) continue;
            float diff=(dl-R->len[j])/dl, wa=R->mass[pj]/(R->mass[pj]+R->mass[j]), wb=1.f-wa;
            for(int k=0;k<3;k++){ R->p[pj][k]+=d[k]*diff*wb; R->p[j][k]-=d[k]*diff*wa; } }
        /* a knee or an elbow does not straighten past its bones, nor fold through itself */
        for(int j=0;j<RD_J;j++){ int pj=RD_PAR[j], gp=(pj>=0)?RD_PAR[pj]:-1; if(gp<0||R->reach[j]<1e-6f) continue;
            float d[3]; for(int k=0;k<3;k++) d[k]=R->p[j][k]-R->p[gp][k];
            float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-8f) continue;
            float lo=R->reach[j]*0.28f, hi=R->reach[j];
            float want=(dl>hi)?hi:((dl<lo)?lo:0.f); if(want==0.f) continue;
            float diff=(dl-want)/dl; const float wg=R->coulomb?R->mass[j]/(R->mass[gp]+R->mass[j]):0.5f;   /* the torso's rule below says why it goes by mass */
            for(int k=0;k<3;k++){ R->p[gp][k]+=d[k]*diff*wg; R->p[j][k]-=d[k]*diff*(1.f-wg); } }
        /* the torso keeps its shape: hips-to-neck and shoulder-to-shoulder hold their length */
        { const signed char A[3]={RD_HIPS,RD_SHL,RD_THL}, B[3]={RD_NECK,RD_SHR,RD_THR};
          for(int c=0;c<3;c++){ int a=A[c], b=B[c];
            float d[3]; for(int k=0;k<3;k++) d[k]=R->p[b][k]-R->p[a][k];
            float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-8f) continue;
            float L=R->tors[c];
            if(L<1e-6f) continue;
            /* ⛔ 11-04: THE CRAWL. Split evenly between a heavy joint and a light one, a correction pushes the whole body along. Hips to neck is also the length of the chain through the chest, so it can only hold with the three in line, and the floor holds a thick chest higher than a line from the hips to a thin neck: this rule is never satisfied on the floor, and it drove a body along its own length at 5 units a second, for as long as it lay there. Film's bodies are nearly as thick at both ends and it comes out as the slow backwards creep its notes describe, which the rest test stops. A game's go by mass, so that whatever these rules argue about, they can't move it anywhere */
            float diff=(dl-L)/dl; const float wa=R->coulomb?R->mass[b]/(R->mass[a]+R->mass[b]):0.5f;
            for(int k=0;k<3;k++){ R->p[a][k]+=d[k]*diff*wa; R->p[b][k]-=d[k]*diff*(1.f-wa); } } }
        }
    /* the world: the floor she is over, and the furniture and walls beside her -- POSITION only, once */
    for(int j=0;j<RD_J;j++){
        if(!R->use[j]) continue;
        float fy=rdFloorAt(R->p[j])+rdDown(R,j)-R->lift[j];
        if(R->p[j][1]<fy) R->p[j][1]=fy;
        if(g_spOn){ if(spSolidAt(R->p[j][0],R->p[j][2],R->p[j][1])){ R->p[j][0]=R->q[j][0]; R->p[j][2]=R->q[j][2]; } }   /* columns are this set's walls */
        else { float mv=0.f; colSdfResolve(&R->p[j][0],&R->p[j][2],R->rad[j],&mv); } }
    /* ⭐ COULOMB FRICTION, THE WAY THE PAPER DOES IT (Jakobsen, "Advanced Character Physics", 2003): the
     * tangential speed is cut in proportion to the PENETRATION DEPTH -- depth stands in for the normal force, so
     * a body pressed into the ground grips and a grazing hand does not -- and the cut NEVER REVERSES the slide:
     * "care should be taken that the tangential velocity does not reverse its direction -- in this case one
     * should simply set it to zero." A flat multiplier here was glue (97% of her speed in one frame); a
     * speed-scaled one was a guess that let her crawl for ever. */
    for(int j=0;j<RD_J;j++){
        if(!R->use[j]) continue;
        float fy=rdFloorAt(R->p[j])+rdDown(R,j)-R->lift[j];
        float dp=fy-R->p[j][1];                       /* how deep this joint was driven in, before the clamp */
        if(dp<-0.002f) continue;                      /* not touching the ground at all */
        if(dp<0.f) dp=0.f;
        R->q[j][1]=R->p[j][1];                        /* the bounce is killed, not reflected */
        float vx=R->p[j][0]-R->q[j][0], vz=R->p[j][2]-R->q[j][2];
        float sp=sqrtf(vx*vx+vz*vz); if(sp<1e-9f) continue;
        float rr=(R->rad[j]>1e-4f)?R->rad[j]:0.1f;
        float cut=settled9?1.f:RD_MU*(dp+0.006f)/rr;   /* the depth, as a fraction of the limb's own thickness -- and at rest (ep 13) a grounded joint HOLDS */
        /* ⛔ 11-04: the world's clamp above has already been, so that depth is always 0 here, and all that's left is the 0.006: next to no friction, and a body that wasn't slow by the time it was down slid about on the floor for seconds. A game's bodies get what the words say, friction that takes a fixed speed per second. Film's takes were made on the old one, and that's still theirs: it's jacob's to change */
        if(R->coulomb&&!settled9) cut=RD_MU*CLUB_UNITS_PER_S2*dt*dt/sp;
        if(cut>1.f) cut=1.f;                          /* at worst it stops: it never runs backwards */
        R->q[j][0]=R->p[j][0]-vx*(1.f-cut);
        R->q[j][2]=R->p[j][2]-vz*(1.f-cut); } }
/* the pose, written back into the bones the chain solver's way: root first, each bone turned onto its point */
static void rdWrite(Pack*P,M4*W,const float*model,Ragdoll*R){
    float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); if(sc<1e-6f) sc=1.f;
    float pm[RD_J][3];
    for(int j=0;j<RD_J;j++){ float d[3]={R->p[j][0]-model[12],R->p[j][1]-model[13],R->p[j][2]-model[14]};
        for(int k=0;k<3;k++) pm[j][k]=(model[k*4]*d[0]+model[k*4+1]*d[1]+model[k*4+2]*d[2])/(sc*sc); }
    { float off[3]; for(int k=0;k<3;k++) off[k]=pm[RD_HIPS][k]-W[R->node[RD_HIPS]][12+k];
      if(g_rdSay) printf("[rd:write] the root moves %.4f u in model space (0 = the seed is exact)\n",
                         sqrtf(off[0]*off[0]+off[1]*off[1]+off[2]*off[2]));
      for(unsigned n=0;n<P->nNodes;n++){ W[n][12]+=off[0]; W[n][13]+=off[1]; W[n][14]+=off[2]; } }
    static const signed char ORDER[RD_J]={RD_HIPS,RD_CHEST,RD_NECK,RD_SHL,RD_ELL,RD_SHR,RD_ELR,RD_THL,RD_KNL,RD_ANL,RD_THR,RD_KNR,RD_ANR,
                                          RD_HEAD,RD_HNL,RD_HNR,RD_TOL,RD_TOR};
    for(int o=0;o<RD_J;o++){ int j=ORDER[o]; int kid=RD_KID[j]; if(kid<0) continue;
        if(!R->use[j]||!R->use[kid]) continue;
        int nj=R->node[j], nk=R->node[kid];
        float from[3], to[3];
        for(int k=0;k<3;k++){ from[k]=W[nk][12+k]-W[nj][12+k]; to[k]=pm[kid][k]-pm[j][k]; }
        float fl=sqrtf(from[0]*from[0]+from[1]*from[1]+from[2]*from[2]);
        float tl=sqrtf(to[0]*to[0]+to[1]*to[1]+to[2]*to[2]);
        if(fl<1e-6f||tl<1e-6f) continue;
        float fn[3]={from[0]/fl,from[1]/fl,from[2]/fl}, tn[3]={to[0]/tl,to[1]/tl,to[2]/tl};
        float axis[3]; ctCross(axis,fn,tn);
        float sn=sqrtf(ctDot(axis,axis)); if(sn<1e-7f) continue;
        axis[0]/=sn; axis[1]/=sn; axis[2]/=sn;
        float ang=atan2f(sn,ctDot(fn,tn)); if(!(ang>1e-5f)) continue;
        if(g_rdSay){ printf("[rd:write] %s -> %s turns %.1f deg (rig bone %.3f, sim bone %.3f)\n",
                            RD_NM[j],RD_NM[kid],ang*57.2958f,fl,tl); fflush(stdout); }
        ctRotateTree(P,W,nj,axis,ang); }
    g_rdSay=0;
    }
static void ragdollTick(Pack*P,M4*W,const float*model,int slot){
    /* the cue first: she can only go limp out of the pose she is standing in, which is what W holds right now */
    if(slot>=0&&slot<SCN_MAXCAST&&g_rdCueLine>=0&&g_scnT>=0.f){
        if(g_rdCueOff[slot]&&rdActive(slot)){ g_rd[slot].on=0; g_rdCueOff[slot]=0;
            printf("[ragdoll] slot %d: back on the clip\n",slot); fflush(stdout); }
        if(g_rdCueAt[slot]>=0.f&&!rdActive(slot)&&scnBodyT()-g_rdCueT0>=g_rdCueAt[slot]){
            /* is she actually going anywhere? the torso's own measured speed, not a heading field */
            float wv[3]; int moving=(rdTorsoVel(slot,8,wv)&&sqrtf(wv[0]*wv[0]+wv[2]*wv[2])>0.6f);
            float late=scnBodyT()-g_rdCueT0-g_rdCueAt[slot];
            if(!moving&&late<2.0f){
                static float said9[SCN_MAXCAST];
                if(said9[slot]<g_rdCueAt[slot]){ said9[slot]=g_rdCueAt[slot];
                    printf("[ragdoll] slot %d: the cue is up but she is not moving -- waiting up to 2s for her to roll\n",slot);
                    fflush(stdout); }
                return; }
            if(!moving) printf("[ragdoll] slot %d: \u26d4 cued on a body AT REST -- this fall has no momentum in it (her run ended before the shot did)\n",slot);
            float at=g_rdCueAt[slot]; g_rdCueAt[slot]=-1.f;
            printf("[ragdoll] line %d slot %d: cued at +%.2fs into the line (body clock %.2f, armed at %.2f)\n",
                   g_rdCueLine,slot,at,scnBodyT(),g_rdCueT0);
            Loco*L=&g_loco[slot];
            float v[3]={sinf(L->trav)*L->spd,0.f,cosf(L->trav)*L->spd};
            { float dts=(float)g_animDtS;
              if(dts>1e-4f){ float mx=(L->x-L->px)/dts, mz=(L->z-L->pz)/dts;
                  float ml=sqrtf(mx*mx+mz*mz);
                  if(ml>0.2f&&ml<30.f){
                      printf("[ragdoll] slot %d: heading says (%.2f,%.2f) u/s, she was actually going (%.2f,%.2f) u/s -- taking the measurement\n",
                             slot,v[0],v[2],mx,mz);
                      v[0]=mx; v[2]=mz; } } }
            ragdollGo(slot,P,W,model,v);
            /* ⛔ SHE LEAVES HER PATH ON THE SPOT. The ragdoll lives in WORLD space and rdWrite re-anchors it to
             * the body's root every frame -- a body still rolling would DRAG its own falling skeleton down the
             * street. Her root freezes where she bailed; the ragdoll carries the speed she had. */
            /* ⭐ ep 13: and she drops her CARVE BANK. The lean she was carving with froze into her root with the
             * rest of it, and the board left at the bail spot (drawn off that root) stood tilted 15 degrees in
             * the air beside her boot for the whole last act. Flat, like a board somebody fell off. */
            if(rdActive(slot)){ L->path=0; L->hold=1; L->lapping=0; L->spd=0.f; L->walkMix=0.f; L->bank=0.f; L->pbank=0.f; } } }
    if(!rdActive(slot)) return;
    Ragdoll*R=&g_rd[slot];
    if(R->P!=P){ R->on=0; return; }
    static int frz=-1; if(frz<0){ const char*e=getenv("REVA_RDFREEZE"); frz=(e&&e[0]&&e[0]!='0')?1:0; }
    float dt=(float)g_animDtS; if(dt>0.05f) dt=0.05f;
    if(R->frame==g_frameNo) dt=0.f;   /* ⛔ already simulated this frame (this is the second draw of it) */
    R->frame=g_frameNo;
    if(dt>0.f&&!frz&&!R->asleep){ int ns=(int)ceilf(dt/0.006f); if(ns<1) ns=1; if(ns>10) ns=10;
        for(int i=0;i<ns;i++) rdStep(R,dt/(float)ns);
        R->t+=dt;
        /* is she still moving? the fastest joint decides, and half a second of stillness puts the body to sleep */
        float h=dt/(float)ns, fast=0.f; int fj=-1;
        for(int j=0;j<RD_J;j++){ if(!R->use[j]) continue;
            float v[3]; ctSub(v,R->p[j],R->q[j]); float vl=sqrtf(ctDot(v,v))/h; if(vl>fast){ fast=vl; fj=j; } }
        { static int dbg2=-1; if(dbg2<0){ const char*e=getenv("REVA_RDDBG"); dbg2=(e&&e[0]&&e[0]!='0')?1:0; }
          if(dbg2){ static float nx2=0.f; if(R->t<0.06f) nx2=0.f;
            if(R->t>=nx2){ nx2=R->t+0.5f; printf("[rd:fast] t=%.2f %s at %.2f u/s\n",R->t,(fj>=0)?RD_NM[fj]:"-",fast); fflush(stdout); } } }
        int down=(R->p[RD_HIPS][1]<rdFloorAt(R->p[RD_HIPS])+rdDown(R,RD_HIPS)+0.25f);
        /* a body that is DOWN and no longer travelling gets a heavy drag: what is left is solver jitter,
         * not motion, and without this she crawled across the floor for ever at a quarter of a unit a second */
        if(down&&fast<2.2f){ for(int j=0;j<RD_J;j++){ if(!R->use[j]) continue;
            for(int k=0;k<3;k++) R->q[j][k]=R->p[j][k]-(R->p[j][k]-R->q[j][k])*0.55f; } }
        /* ⭐ ep 13: A BODY THAT IS DOWN AND WHOSE HIPS HAVE STOPPED IS AT REST, whatever its joints are still
         * doing. The solver's own jitter (a shoulder or an ankle at ~1 u/s under a body lying still) kept the
         * fastest-joint test from ever passing, and the projections walked her BACKWARDS 4.7 u over seven seconds
         * while she was being filmed. The hips' real displacement, frame to frame, is what a viewer calls moving. */
        float hsp=0.f; { float vx=(R->p[RD_HIPS][0]-R->q[RD_HIPS][0])/h, vz=(R->p[RD_HIPS][2]-R->q[RD_HIPS][2])/h; hsp=sqrtf(vx*vx+vz*vz); }
        if(down) R->downT+=dt; else R->downT=0.f;
        /* ⛔ ep 13: the rest timer DECAYS on a busy frame instead of resetting -- at 60 fps thirty quiet frames in a
         * row never came (one projection spike in thirty reset it) -- and two and a half seconds on the floor is at
         * rest whatever the solver is still doing: with self-collision on OR off (A/B'd) a face-down body with a leg
         * folded crept 0.7 u/s for seconds on a velocity term of nothing. Asleep, nothing steps, and it stops. */
        int quiet9=(fast<0.85f)||(R->t>1.2f&&hsp<0.35f)||(R->downT>1.5f&&hsp<0.8f);
        if(down&&quiet9) R->rest+=dt; else R->rest=fmaxf(0.f,R->rest-dt*0.5f);
        if(down&&R->downT>2.5f) R->rest=1.f;
        if(R->rest>0.5f){ R->asleep=1;
            for(int j=0;j<RD_J;j++) memcpy(R->q[j],R->p[j],12);
            printf("[ragdoll] slot %d: settled after %.2fs\n",slot,R->t); fflush(stdout); } }
    else if(dt>0.f&&R->asleep) R->t+=dt;
    else if(frz) R->t+=dt;
    /* ⭐ REVA_RDDBG=1: the fall's shape -- where the hips are, how fast they are still moving, how far she has
     * travelled from the bail. A settled body reads as a flat hips height and a speed at zero. */
    { static int dbg=-1; if(dbg<0){ const char*e=getenv("REVA_RDDBG"); dbg=(e&&e[0]&&e[0]!='0')?1:0; }
      if(dbg){ static float nxt[RD_HERO+1]; if(R->t<0.001f) nxt[slot]=0.f;
        if(R->t>=nxt[slot]){ nxt[slot]=R->t+((R->t<0.6f)?0.06f:0.25f);
            float lo=1e9f; for(int j=0;j<RD_J;j++) if(R->use[j]&&R->p[j][1]<lo) lo=R->p[j][1];
            float vx=R->p[RD_HIPS][0]-R->q[RD_HIPS][0], vz=R->p[RD_HIPS][2]-R->q[RD_HIPS][2];
            printf("[rd] t=%.2f scnT=%.2f dt=%.4f hips=(%.2f,%.2f,%.2f) lowest=%.2f floor=%.2f speed=%.2f\n",R->t,g_scnT,(float)g_animDtS,
                   R->p[RD_HIPS][0],R->p[RD_HIPS][1],R->p[RD_HIPS][2],lo,rdFloorAt(R->p[RD_HIPS]),
                   sqrtf(vx*vx+vz*vz)*60.f); fflush(stdout); } } }
    rdWrite(P,W,model,R); }
/* ---- a game's body going limp: E->bodyLimp, and -1 is its hero. It falls as the film's do, from the pose it's in, the next time it's posed. It's what's drawn, and not the rules: its place on the floor stays what the game says, and a game decides what a limp body means. Each pass through the crowd is one tick, so it comes out the same at any frame rate. ⛔ What's off screen isn't posed, so it doesn't fall until it's seen, and bodyLimpState can't tell a game anything about the rules */
static short g_bodyRd[1024]; static char g_bodyRdGo[1024]; static float g_bodyRdV[1024][3]; static short g_rdOwner[RD_GAME]; static int g_rdGameOn=0,g_heroRd=0,g_heroRdGo=0; static float g_heroRdV[3];
static int apiBodyLimp(int i,float vx,float vy,float vz){
    if(i==-1){ if(!g_heroRd){ g_heroRd=1; g_heroRdGo=1; g_heroRdV[0]=vx; g_heroRdV[1]=vy; g_heroRdV[2]=vz; g_rdGameOn=1; } return 1; }
    if(i<0||i>=1024||i>=g_NS||g_slotHide[i]) return 0;
    if(g_bodyRd[i]) return 1;
    for(int s=0;s<RD_GAME;s++) if(!g_rdOwner[s]){ g_rdOwner[s]=(short)(i+1); g_bodyRd[i]=(short)(s+1); g_bodyRdGo[i]=1; g_bodyRdV[i][0]=vx; g_bodyRdV[i][1]=vy; g_bodyRdV[i][2]=vz; g_rdGameOn=1; return 1; }
    { static int said=0; if(!said){ said=1; printf("[ragdoll] all %d of a game's are in use: bodyLimp gave back 0\n",RD_GAME); fflush(stdout); } }
    return 0; }
static void apiBodyRise(int i){ if(i==-1){ if(g_heroRd){ g_rd[RD_HERO].on=0; g_heroRd=0; g_heroRdGo=0; } return; }
    if(i<0||i>=1024||!g_bodyRd[i]) return;
    int s=g_bodyRd[i]-1; g_rd[SCN_MAXCAST+s].on=0; g_rdOwner[s]=0; g_bodyRd[i]=0; g_bodyRdGo[i]=0; }
static int apiBodyLimpState(int i){ if(i==-1){ if(!g_heroRd) return 0; const Ragdoll*R=&g_rd[RD_HERO]; return (R->on&&R->asleep)?2:1; }
    if(i<0||i>=1024||!g_bodyRd[i]) return 0;
    const Ragdoll*R=&g_rd[SCN_MAXCAST+g_bodyRd[i]-1]; return (R->on&&R->asleep)?2:1; }
/* a game's world starting over: nobody's limp */
static void rdGameClear(void){ for(int s=0;s<RD_GAME;s++) if(g_rdOwner[s]) apiBodyRise(g_rdOwner[s]-1); apiBodyRise(-1); }
static void rdBody(int si,Pack*P,M4*W,const float*model){
    if(si<0){ if(!g_heroRd) return; if(g_heroRdGo){ g_heroRdGo=0; ragdollGo(RD_HERO,P,W,model,g_heroRdV); if(!rdActive(RD_HERO)){ apiBodyRise(-1); return; } g_rd[RD_HERO].coulomb=1; } ragdollTick(P,W,model,RD_HERO); return; }
    if(si>=1024||!g_bodyRd[si]) return;
    const int slot=SCN_MAXCAST+g_bodyRd[si]-1;
    if(g_bodyRdGo[si]){ g_bodyRdGo[si]=0; ragdollGo(slot,P,W,model,g_bodyRdV[si]); if(!rdActive(slot)){ apiBodyRise(si); return; } g_rd[slot].coulomb=1; }
    ragdollTick(P,W,model,slot); }
static void ctSolve(Pack*P,M4*W,const float*model,int slot){
    if(g_ctOn<0){ const char*e=getenv("REVA_NOCONTACT"); g_ctOn=(e&&e[0]&&e[0]!='0')?0:1;
        printf("[contact] %s -- limbs pushed out of bodies, furniture and the floor every frame in a scene%s\n",
               g_ctOn?"ON":"OFF (REVA_NOCONTACT)",g_ctOn?" (REVA_NOCONTACT=1 turns it off)":""); fflush(stdout); atexit(ctReport); }
    if(!P||!W||!model||slot<0||slot>=SCN_MAXCAST||g_scnT<0.f||P->nNodes>1024) return;
    if(rdActive(slot)) return;   /* ⭐ a ragdoll collides for itself */
    CtShape*S=ctShapeOf(P,W);
    if(!S->ok){ ctPublish(P,W,model,slot,S); return; }
    if(g_ctOn){
        caGridSync(); caSupport(slot,&g_caS[slot]);
        float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); if(sc<1e-6f) sc=1.f;
        g_ctFrames++;
        /* 1. THE BODY IN THE WORLD MOVES AS ONE. A torso inside furniture (a roll across a bed she is not lying on, a
         * table) moves the whole body. ⛔ AND SO DO A STANDING BODY'S LEGS: the first cut only bent a standing woman's
         * thighs forward out of a bench and left her hips and feet inside it (jacob: "that image panel has her standing
         * inside of a bench"). Standing (both ankles near the floor, not lying on furniture), the body SLIDES along the
         * floor to the nearest free side -- it is never lifted, and the floor under her is the floor clamp's job. */
        { float move[3]={0,0,0}, deep=0.f; const char*wh="world";
          int standing=0;
          if(g_caS[slot].nsup<=0&&P->footL>=0&&P->footR>=0){ float fl9[3],fr9[3];
              ptWorld(W[P->footL],model,fl9); ptWorld(W[P->footR],model,fr9);
              standing=(fl9[1]<0.45f&&fr9[1]<0.45f);
              /* ⭐ (10-22) ...AND UP ON ITS LEGS. A SEATED body has both feet on the floor too, and its thighs rest on
               * the seat: the slide shoved every sitter in chapter 2 off his chair (0.17-0.67 u, 19 scenes of 27) and the
               * singles framed an empty seat. Standing is the thigh's top above its knee by half a thigh; a sitter's
               * thighs lie level, and its limbs still come out of the seat one by one below. */
              if(standing&&P->pb[12]>=0&&P->pb[13]>=0){ float th9[3],kn9[3];
                  ptWorld(W[P->pb[12]],model,th9); ptWorld(W[P->pb[13]],model,kn9);
                  float tl9=sqrtf((th9[0]-kn9[0])*(th9[0]-kn9[0])+(th9[1]-kn9[1])*(th9[1]-kn9[1])+(th9[2]-kn9[2])*(th9[2]-kn9[2]));
                  if(th9[1]-kn9[1]<0.5f*tl9) standing=0; } }
          static const int WSEG[6]={CT_SPINE,CT_CHEST,CT_THL,CT_SHL,CT_THR,CT_SHR};
          for(int si=0;si<(standing?6:2);si++){ int s=WSEG[si]; float a[3],b[3]; if(!ctSegEnds(P,W,s,a,b)) continue;
              float aw[3],bw[3]; ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,a[0],a[1],a[2],1},model,aw);
              ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,b[0],b[1],b[2],1},model,bw);
              float rs=(s<=CT_CHEST)?fminf(fminf(S->rz[s],S->rzb[s]),S->rx[s])*sc:S->r[s]*sc*0.8f;
              for(int i=0;i<=6;i++){ float u=(float)i/6.f, p[3]={aw[0]+(bw[0]-aw[0])*u,aw[1]+(bw[1]-aw[1])*u,aw[2]+(bw[2]-aw[2])*u};
                  float d9[3]; const char*w9; float pen=ctWorldPen(slot,p,rs,d9,&w9,standing);
                  if(pen>deep+CT_TOL){ deep=pen; move[0]=d9[0]*pen; move[1]=d9[1]*pen; move[2]=d9[2]*pen; wh=w9; } } }
          if(deep>CT_TOL){ float l=sqrtf(ctDot(move,move)); if(l>0.5f){ move[0]*=0.5f/l; move[1]*=0.5f/l; move[2]*=0.5f/l; }
              float mm[3]; for(int k=0;k<3;k++) mm[k]=(model[k*4]*move[0]+model[k*4+1]*move[1]+model[k*4+2]*move[2])/(sc*sc);
              for(unsigned n=0;n<P->nNodes;n++){ W[n][12]+=mm[0]; W[n][13]+=mm[1]; W[n][14]+=mm[2]; }
              ctSay(slot,standing?"standing body":"torso",wh,deep); g_ctFixes++; if(deep>g_ctWorst) g_ctWorst=deep; } }
        /* 2. EACH LIMB, proximal first, twice */
        static const int ORDER[8]={CT_UAL,CT_FAL,CT_UAR,CT_FAR,CT_THL,CT_SHL,CT_THR,CT_SHR};
        for(int pass=0;pass<2;pass++) for(int oi=0;oi<8;oi++){ int s=ORDER[oi];
            int na,nb,own; ctSegNodes(P,s,&na,&nb,&own); if(own<0||S->r[s]<=0.f) continue;
            float a[3],b[3]; if(!ctSegEnds(P,W,s,a,b)) continue;
            float aw[3],bw[3]; ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,a[0],a[1],a[2],1},model,aw);
            ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,b[0],b[1],b[2],1},model,bw);
            float len=sqrtf((bw[0]-aw[0])*(bw[0]-aw[0])+(bw[1]-aw[1])*(bw[1]-aw[1])+(bw[2]-aw[2])*(bw[2]-aw[2]));
            if(len<1e-4f) continue;
            float rs=S->r[s]*sc; int nsmp=(int)(len/0.05f); if(nsmp<4) nsmp=4; if(nsmp>30) nsmp=30;
            /* ⛔ NO OWN-TORSO TEST (yet): the chest ellipse includes the bust, so every forearm crossing below it read
             * 0.14-0.17 u "inside her" and was pushed out. Self-contact stays the animator's until it has a real shape. */
            float deep=CT_TOL, pd[3]={0,0,0}, pp[3]={0,0,0}; char what[24]=""; int found=0;
            for(int i=1;i<=nsmp;i++){ float u=0.25f+0.75f*(float)i/(float)nsmp;
                float p[3]={aw[0]+(bw[0]-aw[0])*u,aw[1]+(bw[1]-aw[1])*u,aw[2]+(bw[2]-aw[2])*u};
                /* ⛔ A LIMB TAPERS: one radius for the whole shin called a standing ankle 0.12 u into the floor and
                 * bent the knee to lift it. Full at the proximal end, 55% at the distal. */
                float rsu=rs*(1.f-0.45f*u);
                float d9[3]; int who=-1; float pen=ctBodyPen(slot,p,rsu,0,d9,&who);
                if(pen>deep){ deep=pen; memcpy(pd,d9,12); memcpy(pp,p,12); found=1;
                    if(who==slot) snprintf(what,sizeof what,"her own torso"); else snprintf(what,sizeof what,"slot %d",who); }
                const char*w9; pen=ctWorldPen(slot,p,rsu,d9,&w9,0);
                if(pen>deep){ deep=pen; memcpy(pd,d9,12); memcpy(pp,p,12); found=1; snprintf(what,sizeof what,"%s",w9); } }
            if(!found) continue;
            float r[3]; ctSub(r,pp,aw);
            float disp[3]={pd[0]*deep,pd[1]*deep,pd[2]*deep};
            float ax[3]; ctCross(ax,r,disp);
            float rl=sqrtf(ctDot(r,r)); if(ctNorm(ax)<1e-8f||rl<1e-4f) continue;
            float k=ctDot(pd,r)/rl, rperp=rl*sqrtf(fmaxf(0.f,1.f-k*k)); if(rperp<0.2f*rl) rperp=0.2f*rl;
            float ang=atanf(deep/rperp); if(ang>0.5236f) ang=0.5236f;
            float axm[3]; ctToModelDir(model,ax,axm);
            ctRotateTree(P,W,own,axm,ang);
            if(pass==0){ ctSay(slot,CT_NM[s],what,deep); g_ctFixes++; if(deep>g_ctWorst) g_ctWorst=deep; } }
        /* 3. ⭐ HER OWN LEGS ARE TWO LEGS. Left against right, capsule to capsule, at her own measured radii --
         * a stance that crosses them (and every borrowed clip does, sooner or later) used to draw one thigh
         * straight through the other. 0.88 of the radii, so legs may TOUCH; deeper than that is not a leg. */
        static const int LPR[4][2]={{CT_THL,CT_THR},{CT_SHL,CT_SHR},{CT_THL,CT_SHR},{CT_SHL,CT_THR}};
        for(int pass=0;pass<2;pass++) for(int q=0;q<4;q++){
            int sA=LPR[q][0], sB=LPR[q][1];
            int naA,nbA,ownA,naB,nbB,ownB; ctSegNodes(P,sA,&naA,&nbA,&ownA); ctSegNodes(P,sB,&naB,&nbB,&ownB);
            if(ownA<0||ownB<0||S->r[sA]<=0.f||S->r[sB]<=0.f) continue;
            float a0[3],b0[3],a1[3],b1[3];
            if(!ctSegEnds(P,W,sA,a0,b0)||!ctSegEnds(P,W,sB,a1,b1)) continue;
            float A0[3],B0[3],A1[3],B1[3];
            ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,a0[0],a0[1],a0[2],1},model,A0);
            ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,b0[0],b0[1],b0[2],1},model,B0);
            ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,a1[0],a1[1],a1[2],1},model,A1);
            ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,b1[0],b1[1],b1[2],1},model,B1);
            float u,v; ctSegSeg(A0,B0,A1,B1,&u,&v);
            float pA[3],pB[3];
            for(int k=0;k<3;k++){ pA[k]=A0[k]+(B0[k]-A0[k])*u; pB[k]=A1[k]+(B1[k]-A1[k])*v; }
            float d[3]; ctSub(d,pB,pA); float dl=sqrtf(ctDot(d,d));
            float rA=S->r[sA]*sc*(1.f-0.45f*u), rB=S->r[sB]*sc*(1.f-0.45f*v);
            float pen=(rA+rB)*0.88f-dl;
            if(!(pen>CT_TOL)) continue;
            if(dl>1e-5f){ d[0]/=dl; d[1]/=dl; d[2]/=dl; } else { d[0]=1.f; d[1]=0.f; d[2]=0.f; }
            for(int side=0;side<2;side++){
                int own=side?ownB:ownA; const float*root=side?A1:A0; const float*pt=side?pB:pA;
                float r9[3]; ctSub(r9,pt,root);
                float sgn=side?1.f:-1.f;
                float disp[3]={d[0]*pen*0.5f*sgn,d[1]*pen*0.5f*sgn,d[2]*pen*0.5f*sgn};
                float ax[3]; ctCross(ax,r9,disp);
                float rl=sqrtf(ctDot(r9,r9)); if(ctNorm(ax)<1e-8f||rl<1e-4f) continue;
                float k9=ctDot(d,r9)/rl, rperp=rl*sqrtf(fmaxf(0.f,1.f-k9*k9)); if(rperp<0.2f*rl) rperp=0.2f*rl;
                float ang=atanf(pen*0.5f/rperp); if(ang>0.35f) ang=0.35f;
                float axm[3]; ctToModelDir(model,ax,axm);
                ctRotateTree(P,W,own,axm,ang); }
            if(pass==0){ char w9[32]; snprintf(w9,sizeof w9,"her own %s",CT_NM[sB]);
                ctSay(slot,CT_NM[sA],w9,pen); g_ctFixes++; if(pen>g_ctWorst) g_ctWorst=pen; } } }
    ctPublish(P,W,model,slot,S); }
static long g_missClip=0;   /* ⛔ lines whose clip did not exist on that body -- each one is a T-pose */
static void ctReport(void){
    if(g_missClip) printf("\n\u26d4 THIS TAKE HAS %ld LINE(S) WITH A MISSING CLIP -- that body stood in a T-POSE for them.\n\n",g_missClip);
    if(g_jgN) { printf("[jiggle] %ld bone-frames, largest swing %.3f u\n",g_jgN,g_jgMax); fflush(stdout); }
    if(g_ctOn>0&&g_ctFrames) { printf("[contact] %ld body-frames, %ld corrections, deepest %.3f u\n",g_ctFrames,g_ctFixes,g_ctWorst); fflush(stdout); } }
/* ⭐ THE BED A BODY LIES ON, AS ONE RULE FOR THE AUDIT AND THE CONTACT PASS: inside its footprint only the DECLARED
 * soft surfaces count -- the `solid` boxes under the simulated bedding (mattress, pillow boxes), which a body may sink
 * CA_SINK into -- and the floor under it. The bed MODEL's own features (bed_01's pillows stand 2.03 u tall, which the
 * grid cannot tell from a headboard) never push or flag the body resting on it. Returns 1 when p is over that bed,
 * with *top the soft surface there (or -1e9 for none: only the floor applies). */
static int caSoftBed(int self,const float*p,float*top,const char**nm){
    if(self<0||self>=CA_SLOTS||g_caS[self].nsup<=0) return 0;
    const CaSlot*S=&g_caS[self]; int in=0;
    for(int k=0;k<S->nsup;k++){ const float*r=S->sup[k]; if(p[0]>=r[0]&&p[0]<=r[2]&&p[2]>=r[1]&&p[2]<=r[3]){ in=1; break; } }
    if(!in) return 0;
    *top=-1e9f; *nm="the floor under her bed";
    for(int q=0;q<g_nSclBox;q++){ const float*B=g_sclBox[q];
        if(fabsf(p[0]-B[0])<=B[3]&&fabsf(p[2]-B[2])<=B[5]&&B[1]+B[4]>*top&&B[1]+B[4]<=S->supY+CA_SUPHI){ *top=B[1]+B[4]; *nm="her bedding"; } }
    return 1; }
