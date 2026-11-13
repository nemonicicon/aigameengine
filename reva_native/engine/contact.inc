/* ================= THE CONTACT PASS (after ROBOT NEKO ep 12: an engine that handles clipping
 * itself makes every animation easier to get right) ==========
 * The audit above only ever REPORTED a limb inside something; every fix happened offline, in an authoring script
 * that had to land every contact to the millimetre before it was allowed to save -- ep 12's cuddle took eight rounds
 * and four hours that way. A game engine settles the last few centimetres itself, every frame, after the pose.
 *   THE SHAPES: measured ONCE per body off its own skinned mesh (hair excluded): each limb's thickness (p55 of its
 *   vertices' distance to the bone), and the torso as two ELLIPSES (hips->chest, chest->neck) with its front-back
 *   depth separate from its width. ⛔ The audit's old body capsules were a fixed fraction of height and a chest was
 *   shoulder-wide in EVERY direction, so an arm resting on a belly read 0.1 u "inside" and nearly every frame of the
 *   ep 12 cuddle was "bad". Anything that judges two bodies touching needs the measured shape.
 *   THE PASS (scenes only; REVA_NOCONTACT=1 is the control): a torso inside the world (the bed from the floor, a
 *   table) moves the WHOLE body out; an arm or leg inside another body, the world, the furniture it rests on (past
 *   the sink it may take into bedding) or its own torso (a forearm) rotates at its shoulder / elbow / hip / knee until
 *   its deepest sample is on the surface. Two passes, a body at most 0.5 u and a joint at most 30 degrees a frame;
 *   the pose is re-derived from the clip every frame, so the correction is too -- it cannot drift or accumulate.
 *   It speaks: `[contact] slot N: <limb> out of <what> by X u` the first time each pair is fixed, and a summary. */
#define CT_NSEG 13
static float caSegDist(const float*a,const float*b,const float*p);
static void ctReport(void);
static int caSoftBed(int self,const float*p,float*top,const char**nm);
#define CT_TOL  0.008f
enum { CT_SPINE, CT_CHEST, CT_HEAD, CT_UAL, CT_FAL, CT_UAR, CT_FAR, CT_THL, CT_SHL, CT_FTL, CT_THR, CT_SHR, CT_FTR };
static const char*CT_NM[CT_NSEG]={"spine","chest","head","upperarmL","forearmL","upperarmR","forearmR",
                                  "thighL","shinL","footL","thighR","shinR","footR"};
typedef struct { Pack*P; const unsigned char*pk; int ok; float r[CT_NSEG], rx[2], rz[2], rzb[2]; } CtShape;   /* rz front, rzb back */
typedef struct { unsigned stamp; int n, ok[CT_NSEG], ell[CT_NSEG]; float a[CT_NSEG][3], b[CT_NSEG][3], r[CT_NSEG],
                 lat[CT_NSEG][3], fwd[CT_NSEG][3], rx[CT_NSEG], rz[CT_NSEG], rzb[CT_NSEG]; } CtBody;
static CtShape g_ctShape[16]; static int g_ctNShape=0;
static CtBody  g_ctBody[SCN_MAXCAST];
static int     g_ctOn=-1;
static long    g_ctFixes=0, g_ctFrames=0; static float g_ctWorst=0.f;
static int ctFloatCmp(const void*a,const void*b){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }
static float ctPct(float*v,int n,float q){ if(n<=0) return 0.f; qsort(v,(size_t)n,sizeof(float),ctFloatCmp);
    int i=(int)(q*(float)(n-1)); if(i<0) i=0; if(i>=n) i=n-1; return v[i]; }
static void ctSub(float*o,const float*a,const float*b){ o[0]=a[0]-b[0]; o[1]=a[1]-b[1]; o[2]=a[2]-b[2]; }
static float ctDot(const float*a,const float*b){ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void ctCross(float*o,const float*a,const float*b){ float t0=a[1]*b[2]-a[2]*b[1], t1=a[2]*b[0]-a[0]*b[2], t2=a[0]*b[1]-a[1]*b[0]; o[0]=t0; o[1]=t1; o[2]=t2; }
static float ctNorm(float*v){ float l=sqrtf(ctDot(v,v)); if(l>1e-8f){ v[0]/=l; v[1]/=l; v[2]/=l; } return l; }
/* the segment ends, by canonical joint: (a, b, the node that owns the segment's vertices and rotates it) */
static void ctSegNodes(Pack*P,int seg,int*na,int*nb,int*own){
    int chest=(P->pb[3]>=0)?P->pb[3]:((P->pb[2]>=0)?P->pb[2]:P->pb[1]);
    CaRest*R=caRestOf(P);
    int A=-1,B=-1,O=-1;
    switch(seg){
      case CT_SPINE: A=P->pb[0]; B=chest; O=P->pb[0]; break;
      case CT_CHEST: A=chest; B=P->pb[4]; O=chest; break;
      case CT_HEAD:  A=P->pb[4]; B=P->pb[5]; O=P->pb[4]; break;
      case CT_UAL: A=P->pb[7];  B=P->pb[8];  O=P->pb[7];  break;
      case CT_FAL: A=P->pb[8];  B=(P->gripL>=0)?P->gripL:-1; O=P->pb[8]; break;
      case CT_UAR: A=P->pb[10]; B=P->pb[11]; O=P->pb[10]; break;
      case CT_FAR: A=P->pb[11]; B=(P->gripNode>=0)?P->gripNode:-1; O=P->pb[11]; break;
      case CT_THL: A=P->pb[12]; B=P->pb[13]; O=P->pb[12]; break;
      case CT_SHL: A=P->pb[13]; B=P->footL;  O=P->pb[13]; break;
      case CT_FTL: A=P->footL;  B=R->toe[0]; O=P->footL;  break;
      case CT_THR: A=P->pb[14]; B=P->pb[15]; O=P->pb[14]; break;
      case CT_SHR: A=P->pb[15]; B=P->footR;  O=P->pb[15]; break;
      case CT_FTR: A=P->footR;  B=R->toe[1]; O=P->footR;  break; }
    int n=(int)P->nNodes;
    *na=(A>=0&&A<n)?A:-1; *nb=(B>=0&&B<n)?B:-1; *own=(O>=0&&O<n)?O:-1; }
/* the ends in MODEL space (W's own), with the extras: a forearm runs on through the hand, a head up to the crown */
static int ctSegEnds(Pack*P,M4*W,int seg,float*a,float*b){
    int na,nb,own; ctSegNodes(P,seg,&na,&nb,&own);
    if(na<0||nb<0) return 0;
    memcpy(a,W[na]+12,12); memcpy(b,W[nb]+12,12);
    float d[3]; ctSub(d,b,a);
    if(seg==CT_FAL||seg==CT_FAR){ b[0]+=d[0]*0.40f; b[1]+=d[1]*0.40f; b[2]+=d[2]*0.40f; }   /* the hand */
    if(seg==CT_HEAD){ b[0]+=d[0]*0.60f; b[1]+=d[1]*0.60f; b[2]+=d[2]*0.60f; }                 /* the crown */
    return 1; }
static void ctTorsoFrame(Pack*P,M4*W,const float*a,const float*b,float*lat,float*fwd){
    float ax[3]; ctSub(ax,b,a); ctNorm(ax);
    float l[3]={1.f,0.f,0.f};
    if(P->pb[7]>=0&&P->pb[10]>=0) ctSub(l,W[P->pb[10]]+12,W[P->pb[7]]+12);
    else if(P->pb[12]>=0&&P->pb[14]>=0) ctSub(l,W[P->pb[14]]+12,W[P->pb[12]]+12);
    float k=ctDot(l,ax); l[0]-=ax[0]*k; l[1]-=ax[1]*k; l[2]-=ax[2]*k;
    if(ctNorm(l)<1e-6f){ l[0]=1.f; l[1]=l[2]=0.f; }
    memcpy(lat,l,12); ctCross(fwd,ax,l); ctNorm(fwd); }
static int ctHairMesh(Pack*P,PMesh*M){
    const char*nm=packStr(P,M->name); char lo[96]; int k=0;
    for(;nm[k]&&k<95;k++){ char c=nm[k]; lo[k]=(c>='A'&&c<='Z')?c+32:c; } lo[k]=0;
    return strstr(lo,"hair")||strstr(lo,"braid")||strstr(lo,"scalp")||strstr(lo,"pony")||strstr(lo,"lash")||strstr(lo,"bang"); }
/* MEASURED ONCE, off the body's own skin in the pose it is first drawn in */
static CtShape*ctShapeOf(Pack*P,M4*W){
    for(int i=0;i<g_ctNShape;i++) if(g_ctShape[i].P==P&&g_ctShape[i].pk==P->pk) return &g_ctShape[i];
    CtShape*S=&g_ctShape[(g_ctNShape<16)?g_ctNShape++:15]; memset(S,0,sizeof*S); S->P=P; S->pk=P->pk;
    int segOf[1024]; int nn=(int)((P->nNodes<1024)?P->nNodes:1024);
    int owner[CT_NSEG]; for(int s=0;s<CT_NSEG;s++){ int a,b; ctSegNodes(P,s,&a,&b,&owner[s]); }
    for(int n=0;n<nn;n++){ segOf[n]=-1;
        for(int a=n,g=0;a>=0&&a<nn&&g<64;a=P->nodes[a].parent,g++){ int hit=-1;
            for(int s=0;s<CT_NSEG;s++) if(owner[s]==a){ hit=s; break; }
            if(hit>=0){ segOf[n]=hit; break; } } }
    /* the hand and toe nodes own nothing of their own: they belong to the forearm / foot that runs through them */
    float segA[CT_NSEG][3], segB[CT_NSEG][3]; int segOK[CT_NSEG];
    for(int s=0;s<CT_NSEG;s++) segOK[s]=ctSegEnds(P,W,s,segA[s],segB[s]);
    float lat[2][3], fwd[2][3];
    for(int t=0;t<2;t++) if(segOK[t]) ctTorsoFrame(P,W,segA[t],segB[t],lat[t],fwd[t]);
    enum{ CAPN=12000 };
    static float dist[CT_NSEG][CAPN], dl[2][CAPN], df[2][CAPN], dbk[2][CAPN]; int nd[CT_NSEG]={0}, nt[2]={0}, nf[2]={0}, nb2[2]={0};
    long total=0; for(unsigned mi=0;mi<P->nMeshes;mi++) total+=P->meshes[mi].verts;
    int step=(int)(total/60000)+1;
    for(unsigned mi=0;mi<P->nMeshes;mi++){ PMesh*M=&P->meshes[mi];
        if(!(M->flags&4)||!M->boneCount||ctHairMesh(P,M)||MOFF(P,mi)) continue;
        float*rp=(float*)(P->pk+M->offPos); unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
        unsigned char*wp=(unsigned char*)(P->pk+M->offWeights); float*bindm=(float*)(P->pk+M->offBind);
        unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap); float*ibm=(float*)(P->pk+M->offIBM);
        for(unsigned v=0;v<M->verts;v+=(unsigned)step){
            float bq[3]; for(int k=0;k<3;k++) bq[k]=bindm[k]*rp[v*3]+bindm[4+k]*rp[v*3+1]+bindm[8+k]*rp[v*3+2]+bindm[12+k];
            float acc[3]={0,0,0}, ws=0.f; int dom=-1; float dw=0.f;
            for(int q=0;q<4;q++){ float w=wp[v*4+q]/255.f; if(w<=0.f) continue;
                int jb=jp[v*4+q]; if(jb>=(int)M->boneCount) continue;
                int node=bm[jb]; if(node<0||node>=nn) continue;
                M4 pm; m4_mul(pm,W[node],*(M4*)(ibm+jb*16));
                for(int k=0;k<3;k++) acc[k]+=w*(pm[k]*bq[0]+pm[4+k]*bq[1]+pm[8+k]*bq[2]+pm[12+k]);
                ws+=w; if(w>dw){ dw=w; dom=node; } }
            if(ws<1e-3f||dom<0) continue;
            acc[0]/=ws; acc[1]/=ws; acc[2]/=ws;
            int s=segOf[dom]; if(s<0||!segOK[s]) continue;
            float d=caSegDist(segA[s],segB[s],acc);
            if(nd[s]<CAPN) dist[s][nd[s]++]=d;
            if(s<=CT_CHEST){ float ab[3],ap[3]; ctSub(ab,segB[s],segA[s]); ctSub(ap,acc,segA[s]);
                float L=ctDot(ab,ab), t=(L>1e-9f)?ctDot(ap,ab)/L:0.f; if(t<0.f)t=0.f; if(t>1.f)t=1.f;
                float o[3]={ap[0]-ab[0]*t,ap[1]-ab[1]*t,ap[2]-ab[2]*t};
                if(nt[s]<CAPN){ dl[s][nt[s]++]=fabsf(ctDot(o,lat[s])); }
                { float fz=ctDot(o,fwd[s]); if(fz>=0.f){ if(nf[s]<CAPN) df[s][nf[s]++]=fz; } else { if(nb2[s]<CAPN) dbk[s][nb2[s]++]=-fz; } } } } }
    int good=0;
    for(int s=0;s<CT_NSEG;s++){ S->r[s]=(nd[s]>=8)?ctPct(dist[s],nd[s],0.55f):0.f; if(S->r[s]>0.f) good++; }
    for(int t=0;t<2;t++){ S->rx[t]=(nt[t]>=8)?ctPct(dl[t],nt[t],0.80f):S->r[t];
        S->rz[t]=(nf[t]>=8)?ctPct(df[t],nf[t],0.80f):S->r[t]; S->rzb[t]=(nb2[t]>=8)?ctPct(dbk[t],nb2[t],0.80f):S->r[t]; }
    S->ok=(good>=6);
    printf("[contact] %s: shapes measured off %ld verts (hair excluded) --",P->label,total);
    for(int s=0;s<CT_NSEG;s++) printf(" %s %.3f",CT_NM[s],S->r[s]);
    printf(" | torso: hips-chest %.3f wide, %.3f front / %.3f back; chest-neck %.3f, %.3f / %.3f (model units)%s\n",
           S->rx[0],S->rz[0],S->rzb[0],S->rx[1],S->rz[1],S->rzb[1],S->ok?"":" -- TOO FEW SEGMENTS, contact OFF for this body");
    fflush(stdout);
    return S; }
/* PUBLISH: this body's shapes in WORLD space, for everyone else (and the audit) to be tested against */
static void rdRemember(Pack*P,M4*W,const float*model,int slot);   /* the ragdoll's velocity transfer, below */
static void ctPublish(Pack*P,M4*W,const float*model,int slot,CtShape*S){
    rdRemember(P,W,model,slot);   /* ⭐ what every joint was doing THIS frame, for a fall that may start the next */
    if(slot<0||slot>=SCN_MAXCAST) return;
    CtBody*B=&g_ctBody[slot]; memset(B,0,sizeof*B); B->stamp=g_frameStamp;
    float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
    for(int s=0;s<CT_NSEG;s++){ float a[3],b[3]; if(!S->ok||S->r[s]<=0.f||!ctSegEnds(P,W,s,a,b)) continue;
        ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,a[0],a[1],a[2],1},model,B->a[s]);
        ptWorld((float[16]){1,0,0,0,0,1,0,0,0,0,1,0,b[0],b[1],b[2],1},model,B->b[s]);
        B->r[s]=S->r[s]*sc; B->ok[s]=1;
        if(s<=CT_CHEST){ float lat[3],fwd[3]; ctTorsoFrame(P,W,a,b,lat,fwd);
            for(int k=0;k<3;k++){ B->lat[s][k]=model[k]*lat[0]+model[4+k]*lat[1]+model[8+k]*lat[2];
                                  B->fwd[s][k]=model[k]*fwd[0]+model[4+k]*fwd[1]+model[8+k]*fwd[2]; }
            ctNorm(B->lat[s]); ctNorm(B->fwd[s]);
            B->ell[s]=1; B->rx[s]=S->rx[s]*sc; B->rz[s]=S->rz[s]*sc; B->rzb[s]=S->rzb[s]*sc; } }
    B->n=CT_NSEG; }
/* how far p (flesh radius rs) is inside one published shape; dir = the way out */
static void ctSegSeg(const float*p1,const float*q1,const float*p2,const float*q2,float*s_out,float*t_out){
    float d1[3],d2[3],r[3]; ctSub(d1,q1,p1); ctSub(d2,q2,p2); ctSub(r,p1,p2);
    float a=ctDot(d1,d1), e=ctDot(d2,d2), f=ctDot(d2,r), sc9=0.f, tc=0.f;
    if(a<1e-9f&&e<1e-9f){ *s_out=0.f; *t_out=0.f; return; }
    if(a<1e-9f){ sc9=0.f; tc=f/e; }
    else { float c=ctDot(d1,r);
        if(e<1e-9f){ tc=0.f; sc9=-c/a; }
        else { float b=ctDot(d1,d2), den=a*e-b*b;
            sc9=(den>1e-9f)?((b*f-c*e)/den):0.f;
            if(sc9<0.f)sc9=0.f;
            if(sc9>1.f)sc9=1.f;
            tc=(b*sc9+f)/e; } }
    if(tc<0.f){ tc=0.f; sc9=(a>1e-9f)?(-ctDot(d1,r)/a):0.f; }
    else if(tc>1.f){ tc=1.f; sc9=(a>1e-9f)?((ctDot(d1,d2)-ctDot(d1,r))/a):0.f; }
    if(sc9<0.f)sc9=0.f;
    if(sc9>1.f)sc9=1.f;
    *s_out=sc9; *t_out=tc; }
static int camHandAt(int slot,float*o){
    if(slot<0||slot>=SCN_MAXCAST) return 0;
    const CtBody*B=&g_ctBody[slot];
    if(!B->n||!B->ok[CT_FAR]||B->stamp+2u<g_frameStamp) return 0;
    memcpy(o,B->b[CT_FAR],12);
    return 1; }
static float ctShapePen(const CtBody*B,int s,const float*p,float rs,float*dir){
    float ab[3],ap[3]; ctSub(ab,B->b[s],B->a[s]); ctSub(ap,p,B->a[s]);
    float L=ctDot(ab,ab), t=(L>1e-9f)?ctDot(ap,ab)/L:0.f; if(t<0.f)t=0.f; if(t>1.f)t=1.f;
    float o[3]={ap[0]-ab[0]*t,ap[1]-ab[1]*t,ap[2]-ab[2]*t};
    if(!B->ell[s]){ float d=sqrtf(ctDot(o,o)); float pen=B->r[s]+rs-d;
        if(dir){ if(d>1e-6f){ dir[0]=o[0]/d; dir[1]=o[1]/d; dir[2]=o[2]/d; } else { dir[0]=0.f; dir[1]=1.f; dir[2]=0.f; } }
        return pen; }
    float x=ctDot(o,B->lat[s]), z=ctDot(o,B->fwd[s]);
    float rx=B->rx[s]+rs, rz=((z>=0.f)?B->rz[s]:B->rzb[s])+rs;
    float e=sqrtf((x*x)/(rx*rx)+(z*z)/(rz*rz));
    if(e>=1.f) return -1.f;
    float nx=x/(rx*rx), nz=z/(rz*rz);                        /* the ellipse's normal at that angle */
    float ang=(e>1e-6f)?1.f:0.f; (void)ang;
    float rEff=(e>1e-6f)?sqrtf(x*x+z*z)/e:((rx<rz)?rx:rz);
    float pen=rEff*(1.f-e);
    if(dir){ float n[3]={B->lat[s][0]*nx+B->fwd[s][0]*nz,B->lat[s][1]*nx+B->fwd[s][1]*nz,B->lat[s][2]*nx+B->fwd[s][2]*nz};
        if(ctNorm(n)<1e-8f){ memcpy(n,B->fwd[s],12); }
        memcpy(dir,n,12); }
    return pen; }
/* other bodies (and, for a forearm, its own torso): the deepest, and which */
static float ctBodyPen(int self,const float*p,float rs,int ownTorso,float*dir,int*who){
    float best=-1e9f; if(who) *who=-1;
    for(int o=0;o<SCN_MAXCAST;o++){ const CtBody*B=&g_ctBody[o];
        if(!B->n||B->stamp+1u<g_frameStamp) continue;
        if(o==self&&!ownTorso) continue;
        for(int s=0;s<CT_NSEG;s++){ if(!B->ok[s]) continue;
            if(o==self&&s>CT_CHEST) continue;                     /* herself: only the torso, and only for a forearm */
            float d9[3]; float pen=ctShapePen(B,s,p,rs,d9);
            if(pen>best){ best=pen; if(dir) memcpy(dir,d9,12); if(who) *who=o; } } }
    return best; }
/* the world at p: the furniture this body rests on (past its sink), then the grid (floor, obstacles) */
static float ctWorldPen(int self,const float*p,float rs,float*dir,const char**what,int sideOnly){
    float best=-1e9f; *what="floor";
    /* ⭐ THE FURNITURE A BODY LIES ON IS SOFT TO THAT BODY. Over its footprint -- the mattress, the bed's own
     * pillows (bed_01's stand 2.03 u tall against the headboard, and the grid calls anything that tall a
     * headboard), the bedding -- the animator put her INTO it on purpose; only the floor under it still pushes.
     * The first pass pushed kaeli's hand out from under her pillow and her arm stood up in the air. */
    { float st; const char*nm;
      if(caSoftBed(self,p,&st,&nm)){ if(sideOnly) return -1e9f; dir[0]=0.f; dir[1]=1.f; dir[2]=0.f; *what=nm;
          return (st>-1e8f)?(st-CA_SINK)-(p[1]-rs):0.f-(p[1]-rs); } }
    const CaGrid*G=&g_caG;
    if(!G->fl){ if(sideOnly) return -1e9f; best=0.f-(p[1]-rs); dir[0]=0.f; dir[1]=1.f; dir[2]=0.f; return best; }
    int cx=(int)floor((p[0]-G->x0)/G->cell), cz=(int)floor((p[2]-G->z0)/G->cell);
    if(cx<0||cz<0||cx>=G->W||cz>=G->H){ if(sideOnly) return -1e9f; best=0.f-(p[1]-rs); dir[0]=0.f; dir[1]=1.f; dir[2]=0.f; return best; }
    int k=cz*G->W+cx; float fy=G->fy[k];
    best=sideOnly?-1e9f:fy-(p[1]-rs); dir[0]=0.f; dir[1]=1.f; dir[2]=0.f;
    if(!(G->fl[k]&CA_OB)) return best;
    float top=G->ty[k]; if(!(p[1]<top&&p[1]>fy)) return best;
    float up=top-(p[1]-rs);
    /* the nearest free cell, ring by ring: the way out sideways */
    float bx=0.f,bz=0.f,bd=1e9f; const int R=8;
    for(int r=1;r<=R&&bd>=1e9f;r++) for(int dz=-r;dz<=r;dz++) for(int dx=-r;dx<=r;dx++){
        if(dx!=-r&&dx!=r&&dz!=-r&&dz!=r) continue;
        int ax=cx+dx, az=cz+dz; if(ax<0||az<0||ax>=G->W||az>=G->H) continue;
        if(G->fl[az*G->W+ax]&CA_OB) continue;
        float ox=G->x0+((float)ax+0.5f)*G->cell-p[0], oz=G->z0+((float)az+0.5f)*G->cell-p[2], e=ox*ox+oz*oz;
        if(e<bd){ bd=e; bx=ox; bz=oz; } }
    float side=(bd<1e9f)?sqrtf(bd)-0.5f*G->cell+rs:1e9f;
    *what="obstacle";
    if(up<=side&&!sideOnly){ dir[0]=0.f; dir[1]=1.f; dir[2]=0.f; return up; }
    if(side>=1e8f) return best;
    float l=sqrtf(bx*bx+bz*bz); dir[0]=bx/l; dir[1]=0.f; dir[2]=bz/l; return side; }
/* rotate node j and everything under it about j's position, in MODEL space */
static void ctRotateTree(Pack*P,M4*W,int j,const float*axis,float ang){
    float c=cosf(ang), s=sinf(ang), t=1.f-c, x=axis[0], y=axis[1], z=axis[2];
    float R[9]={t*x*x+c, t*x*y+s*z, t*x*z-s*y,  t*x*y-s*z, t*y*y+c, t*y*z+s*x,  t*x*z+s*y, t*y*z-s*x, t*z*z+c};
    float pv[3]={W[j][12],W[j][13],W[j][14]};
    int nn=(int)P->nNodes;
    for(int n=0;n<nn;n++){ int in=0;
        for(int a=n,g=0;a>=0&&a<nn&&g<64;a=P->nodes[a].parent,g++) if(a==j){ in=1; break; }
        if(!in) continue;
        float*m=W[n];
        for(int col=0;col<3;col++){ float v0=m[col*4],v1=m[col*4+1],v2=m[col*4+2];
            m[col*4]=R[0]*v0+R[3]*v1+R[6]*v2; m[col*4+1]=R[1]*v0+R[4]*v1+R[7]*v2; m[col*4+2]=R[2]*v0+R[5]*v1+R[8]*v2; }
        float d0=m[12]-pv[0], d1=m[13]-pv[1], d2=m[14]-pv[2];
        m[12]=pv[0]+R[0]*d0+R[3]*d1+R[6]*d2; m[13]=pv[1]+R[1]*d0+R[4]*d1+R[7]*d2; m[14]=pv[2]+R[2]*d0+R[5]*d1+R[8]*d2; } }
static void ctToModelDir(const float*model,const float*vw,float*vm){   /* model = s*Rot + t: M^T v, normalised */
    for(int k=0;k<3;k++) vm[k]=model[k*4]*vw[0]+model[k*4+1]*vw[1]+model[k*4+2]*vw[2];
    ctNorm(vm); }
static void ctSay(int slot,const char*limb,const char*what,float d){
    static char said[64][40]; static int ns=0; char key[40]; snprintf(key,sizeof key,"%d%s%s",slot,limb,what);
    for(int i=0;i<ns;i++) if(!strcmp(said[i],key)) return;
    if(ns<64) snprintf(said[ns++],40,"%s",key);
    printf("[contact] slot %d: %s out of %s by %.3f u (said once per pair)\n",slot,limb,what,d); fflush(stdout); }
/* ⭐ THE JIGGLE: each soft-mass bone's tip (projected out to ~0.13 u, the mass's reach) follows its ANIMATED position
 * through a damped spring in WORLD space -- 3.2 Hz, damping 0.22, so it overshoots and settles like flesh -- capped at
 * 0.05 u, and the bone is turned to point at the sprung tip. Integrated on the EXPOSURE clock: a held frame holds its
 * jiggle, exactly as the chains and the cloth do (stop motion, ep 1). Runs before the contact pass, so a bounce can
 * never carry into anything. */
/* ⭐ SET ACTORS JIGGLE TOO (09-29, REVA: "i want ... boob jiggle physic"): the state is per CHAIN slot -- the cast's
 * five and the actors' sixteen -- so a skater on a set springs exactly as the hero does. */
static float g_jgP[CH_SLOTS][4][3], g_jgV[CH_SLOTS][4][3]; static int g_jgInit[CH_SLOTS][4]; static const Pack*g_jgPk[CH_SLOTS];
static unsigned long g_jgFrame[CH_SLOTS];   /* ⛔ the springs integrate once per frame, not once per draw pass */
static int g_jgOff=-1; static float g_jgMax=0.f; static long g_jgN=0;
static void jiggleTick(Pack*P,M4*W,const float*model,int slot){
    if(g_jgOff<0){ const char*e=getenv("REVA_NOJIGGLE"); g_jgOff=(e&&e[0]&&e[0]!='0')?1:0; }
    if(g_jgOff||!P||!P->nJg||slot<0||slot>=CH_SLOTS) return;
    if(g_jgPk[slot]!=P){ g_jgPk[slot]=P; for(int q=0;q<4;q++) g_jgInit[slot][q]=0; }
    float dt=(float)g_animDtS; if(dt>0.1f) dt=0.1f;
    if(g_jgFrame[slot]==g_frameNo) dt=0.f;
    g_jgFrame[slot]=g_frameNo;
    const float w0=6.2831853f*3.2f, zeta=0.22f, MAXOFF=0.05f, REACH=0.13f;
    for(int q=0;q<P->nJg;q++){ int r=P->jgRoot[q], t=P->jgTip[q];
        float rw[3],tw[3]; ptWorld(W[r],model,rw); ptWorld(W[t],model,tw);
        float ax[3]; ctSub(ax,tw,rw); float bl=ctNorm(ax); if(bl<1e-5f) continue;
        float tgt[3]={rw[0]+ax[0]*REACH,rw[1]+ax[1]*REACH,rw[2]+ax[2]*REACH};
        float*p=g_jgP[slot][q],*v=g_jgV[slot][q];
        if(!g_jgInit[slot][q]){ memcpy(p,tgt,12); v[0]=v[1]=v[2]=0.f; g_jgInit[slot][q]=1; }
        if(dt>0.f){ int ns=(int)ceilf(dt/0.004f); if(ns<1) ns=1; float h=dt/(float)ns;
            for(int i=0;i<ns;i++) for(int k=0;k<3;k++){ float a=w0*w0*(tgt[k]-p[k])-2.f*zeta*w0*v[k]; v[k]+=a*h; p[k]+=v[k]*h; } }
        float off[3]; ctSub(off,p,tgt); float ol=sqrtf(ctDot(off,off));
        if(ol>MAXOFF){ for(int k=0;k<3;k++){ p[k]=tgt[k]+off[k]*MAXOFF/ol; v[k]*=0.5f; } ol=MAXOFF; }
        if(ol>g_jgMax) g_jgMax=ol;
        g_jgN++;
        float want[3]; ctSub(want,p,rw); if(ctNorm(want)<1e-6f) continue;
        float axis[3]; ctCross(axis,ax,want); float sn=ctNorm(axis); if(sn<1e-6f) continue;
        float ang=atan2f(sn,ctDot(ax,want));
        float axm[3]; ctToModelDir(model,axis,axm);
        ctRotateTree(P,W,r,axm,ang); } }
