/* ---- THE ONE BODY (11-02, engine plan Phase 2; the plan is design/engine_capabilities.md section 21) --------------------------------------
 * The hero, a crowd slot, a scene's cast and a set's actor are four owners of state over one body. These are the steps they all take, each written once, so a renderer or a
 * rig only has to get them right here. Bottom-up, one step at a time: `driver.py check` (the sim and the packet) and pixel A/Bs on GL say each step changed nothing. */
typedef struct Face Face; static Face*g_faceCur=NULL;   /* the face of the body being drawn (engine/face.inc) */
static float g_instFade=0.f; static unsigned g_instTex=0;   /* (step 21) the set instance being drawn: how far it's faded, and a picture in place of its texture (engine/set.inc, setInstLook) */
static float g_vkEye[4]={0,0,0,0};   /* (step 23) the body being drawn has eyes that glow: a colour, and how much. 0 is none */
/* whose eyes glow, and how: a game's bodies by slot (E->bodyEyes), the hero, and a scene's cast. g_eyeEver: anyone has ever said so */
static float g_slotEye[1024][4],g_heroEye[4]; static int g_eyeEver=0;
static float g_castEyeG[8][4];   /* a scene's bodies, by slot: 1 and up. Slot 0 is the hero, in g_heroEye */
/* film's word for it, on a lab line, a scene's line or a set actor's: `r,g,b[,amount]`, or `off` */
static void eyesSay(float*o,const char*v){ float r=0.f,g=0.f,b=0.f,a=1.f; if(!strncmp(v,"off",3)){ o[3]=0.f; return; }
    if(sscanf(v,"%f,%f,%f,%f",&r,&g,&b,&a)>=3){ o[0]=r; o[1]=g; o[2]=b; o[3]=a<0.f?0.f:a; g_eyeEver=1; } else printf("[eyes] wants r,g,b[,amount] or off, and not '%.20s'\n",v); }
/* ---- (step 23) HAIR NEVER INWARD, EVERY VERTEX. On a VertexCollide pack (or, 11-06, a HairCollide one: REVA's) a hair mesh (its name starts wear~hair~) is kept out of the neck and off the head, the rule the old
 * renderer had in its vertex shader (hairOut), word for word: out of the neck's capsule, then never nearer the head's centre, in the measured ellipsoid's own frame, than
 * where it sits at rest. So hair that's inside the ellipsoid as authored stays as authored, and hair a swinging chain carries inward stops at its own rest distance.
 * It's done on the processor here, for the hair's own vertices, and what comes out goes in the packet as that mesh's positions for the frame, the way a moved face's do, so the
 * toon's line and a mirror's copy draw the same hair. A vertex that moves is put back through its own bones, so the skinning lands it where the rule put it.
 * The sun's pass draws from a stashed palette with no posed nodes beside it, so the shadow keeps the hair as skinned. A crowd's bodies don't come this way (bodyMeshes does).
 * REVA_VCOLLIDE=0 turns the pack's collision off, REVA_HAIRFIT=0 only this, REVA_VCOLR scales the neck's capsule (the test that it runs at all), REVA_VCOLDBG=1 counts. */
static M4*g_vkWorld=NULL;   /* the posed nodes of the body whose meshes are being drawn (bodyMeshes) */
static int headMatsM(Pack*P,const float*w,float*Mo,float*MI);
static int hairFitIs(Pack*P,unsigned mi){ const char*n=packStr(P,P->meshes[mi].name); return n&&!strncmp(n,"wear~hair~",10); }
static const float* hairFitPos(Pack*P,unsigned mi,const float*pal,const float*bind){
    static float*buf=NULL; static unsigned cap=0; static float T[256][12];
    static int on=-1,dbg=0,said=0; static float vr=1.f;
    if(on<0){ const char*e=getenv("REVA_HAIRFIT"); on=!(e&&e[0]=='0'); e=getenv("REVA_VCOLR"); if(e&&*e) vr=(float)atof(e); e=getenv("REVA_VCOLDBG"); dbg=(e&&e[0]=='1'); }
    if(!on) return NULL;
    PMesh*M=&P->meshes[mi]; if(!M->verts||M->boneCount<1||M->boneCount>256||!g_vkWorld) return NULL;
    float Mo[16],MI[16],MoR[16],MIR[16]; if(!headMats(P,g_vkWorld,Mo,MI)||!headMatsM(P,P->headW0,MoR,MIR)) return NULL;
    /* the neck: a capsule from its joint to the head's, as thick as the neck was measured.
     * ⛔ (11-06) NOT THE SHOULDERS OR THE ARMS, tried on REVA's hanging hair: each vertex pushed out of a shoulder's or an upper arm's capsule
     * laid her locks flat round the arm (dark streaks on her skin, seen from her right, worse than without it), whether it went out the shortest
     * way or its own chain's way. Her CHAINS are held out of those capsules (pack.inc), and the vertices go where their chains go */
    float nA[3]={0,0,0},nE[3]={0,0,0},nR=0.f,nEE=0.f;
    { if(!P->boneRok) measureBoneR(P);
      const int a=P->pb[4],b=P->pb[5]; const float r=P->boneR[4];
      if(a>=0&&b>=0&&(unsigned)a<P->nNodes&&(unsigned)b<P->nNodes&&r>0.f){ for(int k=0;k<3;k++){ nA[k]=g_vkWorld[a][12+k]; nE[k]=g_vkWorld[b][12+k]-nA[k]; } nR=r*vr; nEE=nE[0]*nE[0]+nE[1]*nE[1]+nE[2]*nE[2]; if(nEE<1e-10f) nEE=1e-10f; } }
    /* each bone's matrix with the mesh's bind in it: rows of a 3 x 4 */
    for(unsigned q=0;q<M->boneCount;q++){ const float*A=pal+q*16; for(int r=0;r<3;r++) for(int c=0;c<4;c++){ float s=0.f; if(bind){ for(int k=0;k<4;k++) s+=A[k*4+r]*bind[c*4+k]; } else s=A[c*4+r]; T[q][r*4+c]=s; } }
    const float*vp=(const float*)(P->pk+M->offPos); const unsigned char*vj=P->pk+M->offJoints,*vw=P->pk+M->offWeights; int moved=0,byNeck=0,byHead=0; float far9=0.f;
    for(unsigned v=0;v<M->verts;v++){ float X[12]={0,0,0,0,0,0,0,0,0,0,0,0}; const unsigned char*j=vj+v*4,*w=vw+v*4;
        for(int k=0;k<4;k++){ if(!w[k]) continue; const float wk=(float)w[k]*(1.f/255.f),*Tb=T[j[k]<M->boneCount?j[k]:0]; for(int q=0;q<12;q++) X[q]+=wk*Tb[q]; }
        const float*p=vp+v*3; float x[3],V[3],RP[3]; int hit=0;
        for(int r=0;r<3;r++){ x[r]=X[r*4]*p[0]+X[r*4+1]*p[1]+X[r*4+2]*p[2]+X[r*4+3]; V[r]=x[r]; RP[r]=bind?bind[r]*p[0]+bind[4+r]*p[1]+bind[8+r]*p[2]+bind[12+r]:p[r]; }
        if(nR>0.f){ float t=((V[0]-nA[0])*nE[0]+(V[1]-nA[1])*nE[1]+(V[2]-nA[2])*nE[2])/nEE; if(t<0.f) t=0.f; if(t>1.f) t=1.f;
            float d[3]; for(int r=0;r<3;r++) d[r]=V[r]-(nA[r]+nE[r]*t);
            const float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            if(l<nR&&l>1e-6f){ for(int r=0;r<3;r++) V[r]=nA[r]+nE[r]*t+d[r]*(nR/l); hit|=1; } }
        { float un[3],ur[3]; for(int r=0;r<3;r++){ un[r]=MI[r]*V[0]+MI[4+r]*V[1]+MI[8+r]*V[2]+MI[12+r]; ur[r]=MIR[r]*RP[0]+MIR[4+r]*RP[1]+MIR[8+r]*RP[2]+MIR[12+r]; }
          const float Ln=sqrtf(un[0]*un[0]+un[1]*un[1]+un[2]*un[2]),Lr=0.995f*sqrtf(ur[0]*ur[0]+ur[1]*ur[1]+ur[2]*ur[2]);
          if(Ln<Lr&&Ln>1e-5f){ const float k9=Lr/Ln; for(int r=0;r<3;r++) un[r]*=k9; for(int r=0;r<3;r++) V[r]=Mo[r]*un[0]+Mo[4+r]*un[1]+Mo[8+r]*un[2]+Mo[12+r]; hit|=2; } }
        if(!hit) continue;
        /* back through its bones: the position that skins to V */
        const float a=X[0],b=X[1],c=X[2],d=X[4],e=X[5],f=X[6],g=X[8],h=X[9],i=X[10],det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g); if(fabsf(det)<1e-12f) continue;
        if(!moved){ if(cap<M->verts){ free(buf); buf=(float*)malloc((size_t)M->verts*12); cap=buf?M->verts:0; if(!buf) return NULL; } memcpy(buf,vp,(size_t)M->verts*12); }
        const float y[3]={V[0]-X[3],V[1]-X[7],V[2]-X[11]},id=1.f/det; float*o=buf+v*3;
        o[0]=((e*i-f*h)*y[0]+(c*h-b*i)*y[1]+(b*f-c*e)*y[2])*id; o[1]=((f*g-d*i)*y[0]+(a*i-c*g)*y[1]+(c*d-a*f)*y[2])*id; o[2]=((d*h-e*g)*y[0]+(b*g-a*h)*y[1]+(a*e-b*d)*y[2])*id; moved++;
        byNeck+=hit&1; byHead+=(hit>>1)&1;
        { const float m9=sqrtf((V[0]-x[0])*(V[0]-x[0])+(V[1]-x[1])*(V[1]-x[1])+(V[2]-x[2])*(V[2]-x[2])); if(m9>far9) far9=m9; } }
    if(dbg&&(moved||said<4)&&said<40){ said++; printf("[vcoldbg] %s hair mesh %u: %d of %u vertices moved (%d by the neck, %d by the head), the furthest %.4f rig units; neck radius %.3f\n",P->label,mi,moved,M->verts,byNeck,byHead,far9,nR); fflush(stdout); }
    return moved?buf:NULL; }
/* ---- (step 23) FUR'S PHYSICS, for clothing and for animals (11-04). A furry mesh is drawn as shells over its skin (gfx_vk.c, the fur pipelines), and
 * every strand leans the way its body's fur is pushed. That push is one small spring a body: it hangs a little under its own weight, trails behind whatever the body's doing, as if
 * through the air, leans with the set's wind, and when the body stops it overshoots and settles. One step a frame however many times the body's drawn, none on a held exposure, and
 * a jump across the set is a cut, not a gust. It's picture: nothing in the rules reads it, and the packet's hash leaves it out with the rest of what a surface is made of.
 * Not done: a limb's own swing doesn't reach its fur (the push is the whole body's), and fur doesn't part round what touches it. */
#define FUR_BODIES 96
#define FUR_SAG   0.22f    /* how far a strand hangs at rest, in strand lengths */
#define FUR_DRAG  0.085f   /* lean per unit a second of air going past */
#define FUR_K     196.f    /* the spring: it settles in about half a second */
#define FUR_C     9.8f     /* and rings a couple of times on the way */
typedef struct { unsigned key,stamp; int live; float pos[3],x[3],v[3]; } FurBody;
static FurBody g_furB[FUR_BODIES];
static float g_windV[3];   /* the set's wind (engine/set.inc) */
static unsigned g_vkOwner;   /* whose mesh is being drawn: further down */
static const float*g_vkBodyModel=NULL;   /* the body's own matrix while one of its meshes is drawn. Cloth that hangs is drawn with none (its vertices are in the world already), and its fur still goes where its wearer goes */
static unsigned g_vkRestVb=0;            /* and for that cloth, the pack's own positions: where each vertex was at rest */
static unsigned g_vkIdxIb=0, g_vkIdxN=0;   /* (11-07) a light copy's index list and its length, for the mesh being drawn (litePrep): the same vertices, fewer triangles */
static void furPush(const float*model,float*out){
    const unsigned key=(g_vkOwner>>12)+1u; FurBody*B=NULL; int pick=0; unsigned old=0xffffffffu;
    for(int i=0;i<FUR_BODIES;i++) if(g_furB[i].live&&g_furB[i].key==key){ B=&g_furB[i]; break; }
    if(!B){ for(int i=0;i<FUR_BODIES;i++){ if(!g_furB[i].live){ pick=i; break; } if(g_furB[i].stamp<old){ old=g_furB[i].stamp; pick=i; } }
        B=&g_furB[pick]; memset(B,0,sizeof *B); B->key=key; B->live=1; memcpy(B->pos,model+12,12); B->x[1]=-FUR_SAG; B->stamp=g_frameStamp; }
    else if(B->stamp!=g_frameStamp){ float dt=(float)g_chDt; if(dt>0.05f) dt=0.05f; if(dt<1e-4f) dt=1e-4f;
        float tg[3]; float sp2=0.f; for(int k=0;k<3;k++){ tg[k]=(model[12+k]-B->pos[k])/dt; sp2+=tg[k]*tg[k]; }
        if(sp2>60.f*60.f) tg[0]=tg[1]=tg[2]=0.f;                                   /* that far in a frame is a cut */
        for(int k=0;k<3;k++) tg[k]=(g_windV[k]-tg[k])*FUR_DRAG;                    /* the air going past it */
        tg[1]-=FUR_SAG;
        { const float l=sqrtf(tg[0]*tg[0]+tg[1]*tg[1]+tg[2]*tg[2]); if(l>1.3f){ tg[0]*=1.3f/l; tg[1]*=1.3f/l; tg[2]*=1.3f/l; } }
        if(!g_stepHeld) for(int k=0;k<3;k++){ B->v[k]+=(-FUR_K*(B->x[k]-tg[k])-FUR_C*B->v[k])*dt; B->x[k]+=B->v[k]*dt; }
        { const float l=sqrtf(B->x[0]*B->x[0]+B->x[1]*B->x[1]+B->x[2]*B->x[2]); if(l>1.6f){ B->x[0]*=1.6f/l; B->x[1]*=1.6f/l; B->x[2]*=1.6f/l; } }
        memcpy(B->pos,model+12,12); B->stamp=g_frameStamp;
        /* REVA_FURDBG=<n>: the nth furry body's push as numbers, every third frame for a while: where it's being pushed to, and where it has got to */
        { static int dbg9=-2,said9=0; if(dbg9==-2){ const char*e9=getenv("REVA_FURDBG"); dbg9=(e9&&*e9)?atoi(e9):-1; }
          if(dbg9>=0&&B==&g_furB[dbg9%FUR_BODIES]&&(g_frameStamp%3u)==0u&&said9<70){ said9++; printf("[fur] body %d at %.2f %.2f %.2f: pushed to %+.3f %+.3f %+.3f, is at %+.3f %+.3f %+.3f\n",dbg9,B->pos[0],B->pos[1],B->pos[2],tg[0],tg[1],tg[2],B->x[0],B->x[1],B->x[2]); fflush(stdout); } } }
    memcpy(out,B->x,12); }
/* the rest pose, ready to be posed over */
static void bodyRest(Pack*P,float(*T)[3],float(*Q)[4],float(*S)[3]){
    for(unsigned i=0;i<P->nNodes;i++){ memcpy(T[i],P->nodes[i].t,12); memcpy(Q[i],P->nodes[i].q,16); memcpy(S[i],P->nodes[i].s,12); } }
/* a pose to world matrices, parents first */
static void bodyFK(Pack*P,float(*T)[3],float(*Q)[4],float(*S)[3],M4*W){
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,T[i],Q[i],S[i]);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); } }
/* one mesh's palette. It is the bone's world matrix times its INVERSE BIND -- skipping the inverse bind leaves every vertex at its bone's origin, which is a pile of confetti,
 * not a body. The map is unsigned SHORT. */
static void bodyPalette(Pack*P,unsigned mi,M4*W,float*pal){ PMesh*M=&P->meshes[mi];
    unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap); float*ibm=(float*)(P->pk+M->offIBM);
    for(unsigned b=0;b<M->boneCount;b++){ M4 pm; m4_mul(pm,W[bm[b]<P->nNodes?bm[b]:0],*(M4*)(ibm+b*16)); memcpy(pal+b*16,pm,64); } }
/* one mesh of a pack, into the packet: what the GL calls beside it say, as data. g_vkOwner says whose it is: kind << 28 | owner << 12 */
static int g_shadowPass; static void meshTintOf(Pack*P,unsigned mi,float*c); static float meshAlphaOf(Pack*P,unsigned mi);
static const float* faceMeshPos(Pack*P,unsigned mi,float*mouth);
static int toonOn(const void*Pv,int toon); static void toonTell(void);   /* engine/lab.inc */
static float g_vkOpen=0.f;   /* a scene's body, this mesh: how far its mouth is open */
static int g_vkMirror=0;   /* this mesh shows in the set's mirrors and its glossy floor. 1 it does, 2 its backs don't, 4 with no line round it, 8 only there: a mirror's backing, seen in another */
/* what a mirror shows: one more copy of the mesh through each, and through the glossy floor. One bounce, and what's behind the glass isn't in it. The renderer leaves them out if it isn't drawing mirrors, so the packet's the same */
static void vkMirrorCopies(GfxMesh*m,const float*model){
    if(!g_nMir&&!g_gloss.on) return;
    if(g_mirOff<0){ const char*e=getenv("REVA_NOMIRROR"); g_mirOff=(e&&e[0]!='0')?1:0; if(g_mirOff) printf("[mirror] REVA_NOMIRROR: mirrors are dark openings\n"); }
    if(g_mirOff) return;
    const int nFl=(g_gloss.on&&!g_glossDraw)?1:0,f0=m->flags; const float*m0=m->model,*o0=m->mouth;
    for(int k=0;k<g_nMir+nFl;k++){ const Mirror*M=k<g_nMir?&g_mir[k]:&g_glossM; const int fl=k>=g_nMir;
        if((model[12]-M->p[0])*M->n[0]+(model[13]-M->p[1])*M->n[1]+(model[14]-M->p[2])*M->n[2]<-0.5f) continue;
        float mm[16]; m4_mul(mm,(float*)M->R,(float*)model);
        m->model=mm; m->mouth=fl?NULL:o0; m->flags=(f0&~GFX_GLOSS)|GFX_MIRRORED|(fl?GFX_REFL:0)|((g_vkMirror&2)?GFX_MIRCULL:0)|((g_vkMirror&4)?GFX_NOINK:0);
        gfxMesh(m); }
    m->model=m0; m->mouth=o0; m->flags=f0; }
/* ⭐ (11-06) REAL HAIRS (engine/strands.inc): the next colour draw of a hair mesh whose locks are simulated one by one is its hairs instead (role 1), or nothing (role 2:
 * the opaque core under them, whose ribbons would be hard edges among the hairs). Its shadow is the locks' as it was. strandPrep leaves this just before the mesh is
 * drawn, and that draw takes it. A body drawn as a hologram keeps its locks */
typedef struct { const Pack*P; unsigned mi; int role; unsigned vb[4]; unsigned nIndex, nVerts; float tint[3], rough, c[3]; } HsHook;   /* c: her head's middle, in the world */
static HsHook g_hsHook;
static float g_vkTint[3]={1.f,1.f,1.f},g_vkUse=1.f; static unsigned g_vkOwner=0,g_vkPalShared=0; static int g_vkBodyFlags=0,g_vkBodySkip=0,g_vkPassDark=0;   /* (11-12) g_vkPassDark: a pass whose picture nobody sees (the lab's feed): its bodies are posed and stepped, never drawn */
static int g_bodyWashHair=0;   /* (11-13) the wash reaches her hair too (`actorwash ... hair`) */
static float g_bodyWash[4]={0.f,0.f,0.f,0.f};   /* (11-11) a colour every mesh drawn is washed toward, and how far (vkMesh): set round one body's draw, 0 again after */
static int hsIsHair(Pack*P,unsigned mi);   /* engine/strands.inc */
static double now_ms(void);                /* engine/clock.inc */
/* (11-09) KALEIDOSCOPE HAIR: a HairKaleido pack's hair meshes carry a phase that runs with the tick clock (so a take is the same every time), and the
 * shader turns their hue by it and by where each pixel is (mesh.frag). 0 for everything else */
static float kaleidoOf(Pack*P,unsigned mi){ if(!P->hkal||!hsIsHair(P,mi)) return 0.f; const double t9=now_ms()*0.001; return (float)fmod(t9*1.4,6.283185307)+0.001f; }
static void vkMesh(Pack*P,unsigned mi,const float*model,const float*bind,const float*pal,unsigned bones,const float*tint,float use,int flags,const unsigned*vbOver){
    if(g_noDraw||g_vkBodySkip||g_vkPassDark) return;
    if(g_hsHook.P==P&&g_hsHook.mi==mi&&!(flags&GFX_SHADOW)&&!(g_vkBodyFlags&GFX_HOLO)){ const HsHook h9=g_hsHook; g_hsHook.P=NULL;   /* (11-06) its hairs, one by one */
        if(h9.role==2) return;
        static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=g_vkOwner|(mi&0xfffu);
        m9.vb[0]=h9.vb[0]; m9.vb[1]=h9.vb[1]; m9.vb[4]=h9.vb[2]; m9.vb[5]=h9.vb[3]; m9.nIndex=h9.nIndex; m9.nVerts=h9.nVerts; m9.model=I9;
        m9.tint[0]=h9.tint[0]; m9.tint[1]=h9.tint[1]; m9.tint[2]=h9.tint[2]; m9.useTex=0.f; m9.mat[0]=h9.rough; m9.kind=7.f;
        if(g_bodyWashHair&&g_bodyWash[3]>0.f){ const float a9=g_bodyWash[3]>1.f?1.f:g_bodyWash[3]; for(int k=0;k<3;k++) m9.tint[k]+=(g_bodyWash[k]-m9.tint[k])*a9; }   /* (11-13) washed too */
        m9.eye[0]=h9.c[0]; m9.eye[1]=h9.c[1]; m9.eye[2]=h9.c[2]; m9.eye[3]=0.f;   /* her head's middle, where a hair's shader reads it (mesh.frag's STRAND): a hair has no eyes to glow */
        m9.flags=GFX_BLEND|flags|g_vkBodyFlags; m9.alpha=1.f; m9.hue=kaleidoOf(P,mi);
        if(!(g_vkMirror&8)) gfxMesh(&m9);
        if(g_vkMirror) vkMirrorCopies(&m9,I9);
        return; }
    PMesh*M=&P->meshes[mi]; const unsigned*b=vbOver?vbOver:&P->vbo[mi*6]; GfxMesh m; memset(&m,0,sizeof m);
    m.id=g_vkOwner|(mi&0xfffu);
    for(int i=0;i<6;i++) m.vb[i]=b[i];
    if(!(M->flags&1)) m.vb[1]=0;
    if(!(M->flags&2)) m.vb[5]=0;
    m.nIndex=M->idxCount; m.nVerts=M->verts;
    if(g_vkIdxIb){ m.vb[4]=g_vkIdxIb; m.nIndex=g_vkIdxN; }   /* (11-07) its light copy */
    int tx=P->meshTex[mi]; m.tex=(tx>=0&&(M->flags&2))?P->tex[tx]:0;
    if(g_instTex&&m.tex) m.tex=g_instTex;   /* a mover showing one picture of its sheet */
    if(P->meshMat){ memcpy(m.mat,P->meshMat[mi],16); m.kind=P->meshMat[mi][4]; }   /* (step 22) what it's made of, its kind for a body (step 23), and its normal map */
    { const int nt9=P->meshNTex?P->meshNTex[mi]:-1; if(nt9>=0&&(unsigned)nt9<P->nTex&&(M->flags&2)) m.ntex=P->tex[nt9]; }
    m.model=model; m.bind=bind; m.pal=pal; m.bones=bones; m.palShared=g_vkPalShared;
    if(m.tex){ m.tint[0]=tint[0]; m.tint[1]=tint[1]; m.tint[2]=tint[2]; m.useTex=use; }
    else { float c9[3]; meshTintOf(P,mi,c9); m.tint[0]=c9[0]; m.tint[1]=c9[1]; m.tint[2]=c9[2]; m.useTex=0.f; }
    /* (11-11) A BODY WASHED WITH A COLOUR (g_bodyWash: r g b and how much, 0 none). Its own colour, texture and tint together, is mixed that far toward the
     * wash: the Facade video's chorus, where each copy behind the lead sinks into the colour of the card behind her as GHOST's are drawn in one colour-way
     * (a set's `slotwash` line, engine/set.inc). Her hair is drawn by its own road above and keeps its colour; her eyes' glow below is set after this */
    if(g_bodyWash[3]>0.f&&!(flags&GFX_SHADOW)&&(g_bodyWashHair||!hsIsHair(P,mi))){ const float a9=g_bodyWash[3]>1.f?1.f:g_bodyWash[3], u9=m.useTex, d9=1.f-u9+u9*a9;   /* (her hair drawn as its locks' ribbons comes this way too, and keeps its colour) */
        if(d9>1e-6f) for(int k=0;k<3;k++) m.tint[k]=(m.tint[k]*(1.f-u9)*(1.f-a9)+a9*g_bodyWash[k])/d9;
        m.useTex=u9*(1.f-a9); }
    m.flags=flags|g_vkBodyFlags|((P->meshMatFlags[mi]&4)?GFX_CUTOUT:0)|((P->meshMatFlags[mi]&16)?GFX_OCCLUSION:0)|((M->flags&8u)?GFX_BLEND:0); m.hue=kaleidoOf(P,mi);   /* (11-11) flag 16: its alpha is its share of the room (8 is old writers', read by nothing) */
    if(P->meshShd&&!(flags&GFX_SHADOW)) m.shader=P->meshShd[mi];   /* (11-12) its surface's own shader (engine/setshader.inc) */
    if(M->flags&8u) m.alpha=meshAlphaOf(P,mi);   /* a sheer mesh says how sheer in its name: #rrggbbaa */
    if(P->sheer>0.f&&P->sheer<1.f&&!(flags&GFX_SHADOW)){ m.flags|=GFX_BLEND; m.alpha=P->sheer; }   /* and a sheer prop in a marker */
    if(g_instFade>0.f&&!(flags&GFX_SHADOW)){ const float a9=1.f-g_instFade; m.alpha=(m.flags&GFX_BLEND)?m.alpha*a9:a9; m.flags|=GFX_BLEND; }   /* and one the timeline's fading out */
    /* (step 23) eyes that glow. An eye that's a mesh of its own is unlit at the glow's colour, on every machine; a person who's one mesh has it done pixel by pixel, where the eyes setting is on */
    if(g_vkEye[3]>0.f&&!(flags&GFX_SHADOW)){ const float a9=g_vkEye[3]>1.f?1.f:g_vkEye[3];
        if(m.kind>2.5f&&m.kind<3.5f){ const float k9=g_vkEye[3]>1.f?g_vkEye[3]:1.f; m.flags|=GFX_GLOW; m.tint[0]=g_vkEye[0]*k9; m.tint[1]=g_vkEye[1]*k9; m.tint[2]=g_vkEye[2]*k9; m.useTex=m.tex?1.f-a9:0.f; }
        else if(m.kind>4.5f) memcpy(m.eye,g_vkEye,16); }
    if(m.kind>5.5f&&m.kind<6.5f&&!(flags&GFX_SHADOW)){ const float*bm9=g_vkBodyModel?g_vkBodyModel:model; furPush(bm9,m.eye); m.eye[3]=0.f;
        if(g_vkRestVb){ m.rest=g_vkRestVb; m.restScale=sqrtf(bm9[0]*bm9[0]+bm9[1]*bm9[1]+bm9[2]*bm9[2]); } }   /* (step 23) fur: which way this body's is pushed. It rides where the eyes' glow does: nothing is both */
    if(g_glossDraw) m.flags|=GFX_GLOSS;
    { const char*gn9=packStr(P,M->name); const char*gh9=gn9?strchr(gn9,'#'):NULL; if(gh9&&strlen(gh9)>=8&&gh9[7]=='*') m.flags|=GFX_GLOW; }   /* #rrggbb* glows: a penlight in a fan's hand */
    if(P->glow) m.flags|=GFX_GLOW;
    else if(P->toon&&!(flags&GFX_SHADOW)&&toonOn(P,P->toon)){ m.flags|=GFX_TOON|(P->toon==2?GFX_TOONROOM:0); { const char*nm9=packStr(P,M->name); if(nm9&&strstr(nm9,"~face~")) m.flags|=GFX_NOINK; } toonTell(); }   /* the toon pass: the renderer does the line itself */
    float mo9[8]; if(g_faceCur&&!(flags&GFX_SHADOW)){ m.pos=faceMeshPos(P,mi,mo9); if(mo9[6]>0.f) m.mouth=mo9; }   /* a face that's moved, and a mouth that's open */
    if(!m.pos&&(P->vcol||P->hcol)&&g_vcolOn!=0&&g_vkWorld&&pal&&bones&&!(flags&GFX_SHADOW)&&hairFitIs(P,mi)) m.pos=hairFitPos(P,mi,pal,bind);   /* (step 23) its hair, off its head and out of its neck; (11-06) a HairCollide pack's too */
    else if(g_vkOpen>0.002f&&!(flags&GFX_SHADOW)){ mo9[0]=P->mouthBind[0]; mo9[1]=P->mouthBind[1]; mo9[2]=P->mouthBind[2]; mo9[3]=P->mouthBind[5]; mo9[4]=P->mouthBind[3]; mo9[5]=P->mouthBind[4]; mo9[6]=g_vkOpen; mo9[7]=0.f; m.mouth=mo9; }
    if(!(g_vkMirror&8)) gfxMesh(&m);
    if(g_vkMirror&&!(flags&GFX_SHADOW)) vkMirrorCopies(&m,model); }
/* ⭐ (11-07) A BODY'S LIGHT COPIES, made from its own mesh (E->tune->bodyLite), for the Pi's performance in a bigger game. Bike
 * Lane Bash's street people are built while the game runs, about 4,600 triangles each, and from the camp half a kilometre off they were 66% of the Pi's main
 * pass, a few of the picture's lines tall and behind buildings. A crowd body gets two copies the first time one's drawn small: per mesh, an index list over the
 * SAME vertices, in which each vertex is moved onto the one nearest the middle of its cell of a grid over the body (a 40th of its height, then a 16th), among
 * those whose strongest bone is the same, so a thigh never joins the other thigh, and the triangles that collapse go. The same buffers, the same skinning, pose
 * and texture, only fewer triangles: main.inc draws a copy while its cell is under two of the picture's lines, and the sun sees the same copy. What a copy
 * hasn't got that far off nobody sees: a cell's corners are where its vertices were, so a silhouette's finest steps go. Nothing of it is in the rules */
static const float LITE_DIV[2]={40.f,16.f};
typedef struct { int x,y,z,b; float s[3]; unsigned n,best; float bd; } LiteCell;
static void litePrep(Pack*P){
    if(P->litePk==P->pk) return;
    if(P->liteIb){ gfxBuffersDrop((int)(P->nMeshes*2),P->liteIb); free(P->liteIb); P->liteIb=NULL; }
    free(P->liteN); P->liteN=NULL; P->litePk=P->pk;
    float y0=1e9f,y1=-1e9f;
    for(unsigned mi=0;mi<P->nMeshes;mi++){ const PMesh*M=&P->meshes[mi]; if(!(M->flags&4)||M->boneCount>MAXB) continue;
        for(unsigned k=0;k<M->verts;k++){ float y; memcpy(&y,P->pk+M->offPos+(size_t)k*12+4,4); if(y<y0) y0=y; if(y>y1) y1=y; } }   /* a pack's tables needn't be 4-byte aligned: memcpy */
    const float h=y1-y0; if(!(h>1e-4f)) return;
    unsigned*ib=(unsigned*)calloc((size_t)P->nMeshes*2,sizeof(unsigned)),*nn=(unsigned*)calloc((size_t)P->nMeshes*2,sizeof(unsigned));
    if(!ib||!nn){ free(ib); free(nn); return; }
    unsigned long tAll=0,tL[2]={0,0};
    for(unsigned mi=0;mi<P->nMeshes;mi++){ const PMesh*M=&P->meshes[mi];
        if(!(M->flags&4)||M->boneCount>MAXB||M->idxCount<3||!M->verts) continue;
        const unsigned nv=M->verts,nt=M->idxCount/3; tAll+=nt;
        unsigned cap=64; while(cap<nv*2u) cap<<=1;
        LiteCell*C=(LiteCell*)malloc((size_t)cap*sizeof(LiteCell)); unsigned*of=(unsigned*)malloc((size_t)nv*sizeof(unsigned)),*out=(unsigned*)malloc((size_t)nt*3u*sizeof(unsigned));
        float*pv=(float*)malloc((size_t)nv*12); int*bn=(int*)malloc((size_t)nv*sizeof(int));
        if(!C||!of||!out||!pv||!bn){ free(C); free(of); free(out); free(pv); free(bn); continue; }
        for(unsigned k=0;k<nv;k++){ memcpy(pv+k*3,P->pk+M->offPos+(size_t)k*12,12);
            unsigned char j4[4],w4[4]; memcpy(j4,P->pk+M->offJoints+(size_t)k*4,4); memcpy(w4,P->pk+M->offWeights+(size_t)k*4,4);
            int bb=0; for(int q=1;q<4;q++) if(w4[q]>w4[bb]) bb=q; bn[k]=j4[bb]; }   /* its strongest bone */
        for(int l=0;l<2;l++){ const float cell=h/LITE_DIV[l];
            for(unsigned c=0;c<cap;c++) C[c].n=0;
            for(unsigned k=0;k<nv;k++){ const int x=(int)floorf(pv[k*3]/cell),y=(int)floorf(pv[k*3+1]/cell),z=(int)floorf(pv[k*3+2]/cell),b=bn[k];
                unsigned hh=((unsigned)x*73856093u)^((unsigned)y*19349663u)^((unsigned)z*83492791u)^((unsigned)b*2654435761u), c=hh&(cap-1u);
                while(C[c].n&&!(C[c].x==x&&C[c].y==y&&C[c].z==z&&C[c].b==b)) c=(c+1u)&(cap-1u);
                if(!C[c].n){ C[c].x=x; C[c].y=y; C[c].z=z; C[c].b=b; C[c].s[0]=C[c].s[1]=C[c].s[2]=0.f; C[c].bd=1e30f; C[c].best=k; }
                C[c].n++; C[c].s[0]+=pv[k*3]; C[c].s[1]+=pv[k*3+1]; C[c].s[2]+=pv[k*3+2]; of[k]=c; }
            for(unsigned k=0;k<nv;k++){ LiteCell*Q=&C[of[k]]; const float in=1.f/(float)Q->n;   /* the vertex nearest the cell's middle stands for it */
                const float dx=pv[k*3]-Q->s[0]*in,dy=pv[k*3+1]-Q->s[1]*in,dz=pv[k*3+2]-Q->s[2]*in,d=dx*dx+dy*dy+dz*dz; if(d<Q->bd){ Q->bd=d; Q->best=k; } }
            unsigned no=0;
            for(unsigned t=0;t<nt;t++){ unsigned a[3]; memcpy(a,P->pk+M->offIdx+(size_t)t*12,12);
                if(a[0]>=nv||a[1]>=nv||a[2]>=nv) continue;
                const unsigned r0=C[of[a[0]]].best,r1=C[of[a[1]]].best,r2=C[of[a[2]]].best;
                if(r0==r1||r1==r2||r0==r2) continue;   /* collapsed */
                out[no++]=r0; out[no++]=r1; out[no++]=r2; }
            if(no>=3){ unsigned nm; gfxBuffersNew(1,&nm); gfxBufferData(nm,(long)no*4,out,0); ib[mi*2+l]=nm; nn[mi*2+l]=no; tL[l]+=no/3; } }
        free(C); free(of); free(out); free(pv); free(bn); }
    P->liteIb=ib; P->liteN=nn;
    printf("[lite] %s: %lu triangles; its light copies %lu and %lu\n",P->label[0]?P->label:"a body",tAll,tL[0],tL[1]); fflush(stdout); }
/* one posed mesh, into the packet. cloth is clothPrep's answer: 2 is hanging cloth, whose vertices are already in the world, in the buffers the caller passes.
 * mirror: this path's body also shows in the set's mirrors. */
static void bodyDrawMesh(Pack*P,unsigned mi,const float*model,const float*pal,const unsigned*b,const float*tint,float use,int cloth,int mirror){
    g_vkMirror=mirror?1:0;
    g_vkBodyModel=model;
    if(cloth==2){ static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; g_vkRestVb=P->vbo[mi*6]; vkMesh(P,mi,I9,NULL,NULL,0,tint,use,0,b); g_vkRestVb=0; }
    else vkMesh(P,mi,model,(float*)(P->pk+P->meshes[mi].offBind),pal,P->meshes[mi].boneCount,tint,use,0,b);
    g_vkBodyModel=NULL;
    g_vkMirror=0;
}
