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
    if(dt<0.f) dt=0.f;
    if(dt>0.10f) dt=0.10f;
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

    if(!st->init){ st->init=1; st->u=u; st->um=u; st->act=0.f; st->prevSep=sep; return; }

    /* ⛔ A STANDING BODY GETS NOTHING. Amplitude follows how fast the feet are swapping,
     * so an idle does not bob and a walk does -- and it is right for the 105 citizens
     * whose clips nobody labelled. */
    float dsep=(dt>1e-5f)?(fabsf(sep-st->prevSep)/dt):0.f;
    st->prevSep=sep;
    float at=dsep/(legLen*1.30f); if(at>1.f) at=1.f;
    st->act += (at-st->act)*(1.f-expf(-dt/0.30f));

    /* THE LAG: down JUST AFTER the contact, up JUST AFTER the passing position. */
    st->u += (u-st->u)*(1.f-expf(-dt/0.055f));
    /* ⛔ AND A SLOW MEAN TO SUBTRACT, so the bounce is a pure oscillation. Against a fixed
     * 0.5 the term has a net sign for any clip whose stride is not symmetric, and the
     * side-by-side caught it at once: she walked the whole strip standing TALLER. A body
     * that hovers because of its own animation layer is this project's oldest bug. */
    st->um += (st->u-st->um)*(1.f-expf(-dt/0.90f));

    float g=amt*st->act;
    { static int dbg=-1, nn=0; if(dbg<0) dbg=getenv("REVA_WMSDBG")?1:0;
      if(dbg&&(nn++%20)==0) printf("[wms] leg=%.3f sep=%.3f u=%.2f act=%.3f g=%.3f\n",
                                   legLen,sep,st->u,st->act,g); }
    if(g<0.004f) return;

    /* ---- 1. THE UP AND DOWN -- his headline, and the whole feeling of weight. The
     * pelvis moves along RIG up, expressed in its parent's frame and divided by the
     * parent's scale, so it is the same centimetres on a rig baked in metres and one
     * baked in centimetres. */
    /* 0.045 -> a 9%-of-leg-length peak-to-peak rise and fall, about 5cm on these bodies.
     * A real walk is nearer 4-5cm, and Williams is explicit that TRACING the real amount
     * still floats -- you have to go past it. Measured at 0.030 the head moved 10px in a
     * 720 line frame; the club's own dial (ctl wms=, default 0.85) is where taste lives. */
    {   float dy=(st->um-st->u)*2.f*0.045f*legLen*g;
        int pa=P->nodes[hp].parent;
        float up[3]={0.f,1.f,0.f}, ps=1.f;
        if(pa>=0){ wmsLocalAxis(W[pa],0.f,1.f,0.f,up);
                   ps=sqrtf(W[pa][4]*W[pa][4]+W[pa][5]*W[pa][5]+W[pa][6]*W[pa][6]);
                   if(ps<1e-6f) ps=1.f; }
        T[hp][0]+=up[0]*dy/ps; T[hp][1]+=up[1]*dy/ps; T[hp][2]+=up[2]*dy/ps; }

    /* ---- 2. THE BELT LINE, down with the foot that is down. Rolled about the direction
     * of travel, in the pelvis' own frame. */
    {   float tilt=(L[13]-R[13])/legLen;
        if(tilt>1.f) tilt=1.f;
        if(tilt<-1.f) tilt=-1.f;
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

/* ⭐ A MESH CARRIES ITS OWN COLOUR IN ITS NAME -- `#rrggbb` on an untextured mesh, so an
 * authored garment brings a colour without the pack format needing a material it does not
 * have. The hero's draw learned this when kaeli's pyjamas were appended; the ACTOR path
 * never did, so a garment on anyone in the street rendered in whatever texture the
 * previous prop had left bound -- which is why a blue pyjama came back BROWN. */
static void meshTintOf(Pack*P,unsigned mi,float*c){
    c[0]=0.42f; c[1]=0.47f; c[2]=0.62f;
    const char*mn=packStr(P,P->meshes[mi].name);
    const char*h=mn?strchr(mn,'#'):NULL;
    if(h&&strlen(h)>=7){ char hx[7]; memcpy(hx,h+1,6); hx[6]=0;
        unsigned v=(unsigned)strtoul(hx,NULL,16);
        c[0]=((v>>16)&255)/255.f; c[1]=((v>>8)&255)/255.f; c[2]=(v&255)/255.f; } }

/* ⭐ AND `#rrggbbAA` MAKES IT SHEER. jacob: "her pajamas should be threadbare, sheer, loose,
 * old". A mesh's alpha defaults to 1.0, so a garment only has to ask -- two more hex digits on the name it already carries. 1.0 if absent, so
 * every existing mesh is unchanged. */

static float meshAlphaOf(Pack*P,unsigned mi){
    const char*mn=packStr(P,P->meshes[mi].name);
    const char*h=mn?strchr(mn,'#'):NULL;
    if(!h) return 1.f;
    size_t n=0; while(isxdigit((unsigned char)h[1+n])) n++;
    if(n<8) return 1.f;
    char hx[3]={h[7],h[8],0};
    return (float)strtoul(hx,NULL,16)/255.f; }

/* ⭐ CLOTH, FORWARD-DECLARED. The solver lives with the scene cast further down because
 * that is where SCN_MAXCAST and skinPal are declared, but the SET ACTOR path is up here --
 * and actors are where a curtain, a coat or a dress is actually going to be seen. One
 * prototype is cheaper than moving three systems. */
#define CLOTH_SLOTS  16
#define CLOTH_ACTOR0 SCN_MAXCAST /* slots below this are hero + cast */
static int clothPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world);

static const char*g_actorOver=NULL; static float g_actorOverW=1.f;   /* the layer for the actor being drawn (09-11) */
/* ⭐ A SET ACTOR'S MORPHS (10-18): the hero and the scene partner always blended their clip's morph tracks; a set actor
 * never did, so a boy asleep in a warming pan could not shut his eyes. The actor being drawn hands its weights here. */
static float*g_actorMorphW=NULL; static int*g_actorMorphWas=NULL;
static void morphBlend(Pack*P,float*wts,int*wasAct);   /* below */
static void jiggleTick(Pack*P,M4*W,const float*model,int slot);   /* the soft mass, defined with the ragdoll */
/* ⭐ every mesh of a posed body, for the owners that own nothing else about it: a scene's cast and a set's actors (the one body, engine/body.inc). Each mesh: its palette, whether cloth has
 * taken it over, the sun's stash, its streams, its texture or the colour in its name, the draw.
 * ⛔ The model matrix an actor passes is its GROUNDED one: simulating a garment against the pre-grounding matrix hangs it off a body that is not where the body is.
 * ⛔ The model goes back AFTER the draw: clothPrep leaves an IDENTITY model so world-space particles draw where they are, and restoring the body's first pushes them through it a second time.
 * ⛔ A tint path for an untextured mesh on every owner: without one a garment samples whatever texture the last mesh left bound, and alpha-tests itself away. */
static int meshIsCloth(Pack*P,unsigned mi);
/* the old shadow pass's cast list, which the hero and the crowd fill as they draw */
#define NCASTMAX 8
/* ⭐ (queue step 24) A SET ACTOR'S LIGHTER COPIES. A body can have props/<stem>~lod1.rpack and ~lod2 (skate_src/author/lod.py): the same skeleton, the same meshes by
 * name, fewer triangles. When the actor is small in the picture its copy is drawn on the actor's own pose, so it moves exactly as the body does. What a copy doesn't have:
 * a face that moves, cloth that hangs or swings on chains of its own, hair kept off the head vertex by vertex. Far off, none of them is seen. It wears what the body wears,
 * piece by piece by name. The machine's `lod` setting says how soon: the body itself is the top detail and is never thinned up close. */
static Pack* actorLod(Pack*P,const float*mdl){
    const ptrdiff_t pi=P-g_setPk; if(pi<0||pi>=SET_MAXP) return NULL;
    if(g_setLod[pi][0]<0&&g_setLod[pi][1]<0) return NULL;
    static const float bias9[5]={0.5f,0.75f,1.f,1.4f,2.f};
    const float ex=mdl[12]-g_camEye[0], ey=mdl[13]-g_camEye[1], ez=mdl[14]-g_camEye[2], d=sqrtf(ex*ex+ey*ey+ez*ez)+1e-3f;
    const float big=fmaxf(P->effH,1.f)/d*bias9[g_gfxLod<0?0:g_gfxLod>4?4:g_gfxLod];
    int lv=-1; if(big<0.075f&&g_setLod[pi][1]>=0) lv=1; else if(big<0.20f&&g_setLod[pi][0]>=0) lv=0; else if(big<0.075f&&g_setLod[pi][0]>=0) lv=0;
    if(lv<0) return NULL;
    Pack*Q=&g_setPk[g_setLod[pi][lv]];
    return Q->nNodes==P->nNodes?Q:NULL; }   /* a copy made from an older bake of the body has other bones: it isn't this body's */
static void sunStash(Pack*P,int mi,const float*pal,int nb,const float*model,int crowd);
static void bodyMeshesLod(Pack*P,Pack*Q,M4*W,const float*model,float*pal){
    if(Q->meshOff&&P->meshOff){ unsigned long long w9=0ull;   /* what the body has on, piece by piece by name */
        for(int i=0;i<P->nPiece;i++) if((P->worn>>i)&1ull){ const int j=wardFind(Q->pieceName,Q->nPiece,P->pieceName[i]); if(j>=0) w9|=1ull<<j; }
        if(w9!=Q->worn||Q->outfit!=P->outfit){ Q->worn=w9; Q->outfit=P->outfit; wardApply(Q); } }
    static int dbg9=-1; if(dbg9<0){ const char*e9=getenv("REVA_LODDBG"); dbg9=(e9&&e9[0]!='0')?1:0; }
    static const float wh9[3]={1.f,1.f,1.f}, gr9[3]={0.2f,1.f,0.2f};
    if(dbg9){ static struct { const Pack*q; float x,z; } seen9[32]; static int nSeen9=0; int k9=0; for(;k9<nSeen9;k9++) if(seen9[k9].q==Q&&seen9[k9].x==model[12]&&seen9[k9].z==model[14]) break;
        if(k9==nSeen9&&nSeen9<32){ seen9[nSeen9].q=Q; seen9[nSeen9].x=model[12]; seen9[nSeen9].z=model[14]; nSeen9++; printf("[lod] the actor %s at %.0f,%.0f is drawn from %s\n",P->label,model[12],model[14],Q->label); fflush(stdout); } }   /* REVA_LODDBG: said once an actor and copy */
    for(unsigned mi=0;mi<Q->nMeshes;mi++){ PMesh*M=&Q->meshes[mi];
        if(!(M->flags&4)||M->boneCount>MAXB||MOFF(Q,mi)) continue;
        bodyPalette(Q,mi,W,pal);
        sunStash(Q,(int)mi,pal,(int)M->boneCount,model,0);
        bodyDrawMesh(Q,mi,model,pal,&Q->vbo[mi*6],dbg9?gr9:wh9,dbg9?0.55f:1.f,0,1); } }
static float castPal[NCASTMAX][MAXB*16]; static M4 castModelA[NCASTMAX];
static Pack*castP[NCASTMAX]; static int castMi[NCASTMAX]; static int nCast=0;
/* one owner's way through bodyMeshes. hero: she also goes on the cast list, her cloth never casts, and the tint is left as it's set. noCast: this pass casts nothing. meshEnd: draw only the meshes below this (0 for all).
 * texTint: the tint over a texture, and NULL leaves uUse and uTint alone. */
typedef struct { int clothSlot; const float*morphW; int hero,noCast; unsigned meshEnd; const float*texTint; } BodyGL;
static float mouthOpenOf(Pack*P,const float*wts);
static void bodyMeshes(Pack*P,M4*W,const float*model,float*pal,const BodyGL*g){
    unsigned end9=(g->meshEnd&&g->meshEnd<P->nMeshes)?g->meshEnd:P->nMeshes;
    g_vkWorld=W;   /* (step 23) the posed nodes, for a VertexCollide pack's hair (engine/body.inc, hairFitPos) */
    for(unsigned mi=0;mi<end9;mi++){ PMesh*M=&P->meshes[mi];
        g_vkOpen=(g->morphW&&(int)mi!=P->fmInterior)?mouthOpenOf(P,g->morphW):0.f;
        if(!(M->flags&4)||M->boneCount>MAXB||MOFF(P,mi)) continue;
        bodyPalette(P,mi,W,pal);
        /* ⛔ THE MOUTH INTERIOR MUST NOT CAST. It is teeth, a tongue and a cavity dome sealed inside her skull -- it can throw no shadow anyone could see, and it would spend a place on the cast list.
         * ⛔ AND A CLOTH MESH MUST NOT CAST. The shadow pass replays the stashed palette against the real model matrix; a simulated garment's palette is the identity, so its shadow would be the garment's REST pose. */
        if(g->hero&&!g->noCast&&nCast<NCASTMAX&&(int)mi!=P->fmInterior&&!meshIsCloth(P,mi)){ memcpy(castPal[nCast],pal,M->boneCount*64); castP[nCast]=P; castMi[nCast]=(int)mi; memcpy(castModelA[nCast],model,64); nCast++; }
        if(g->hero&&!g->noCast&&(int)mi!=P->fmInterior&&!meshIsCloth(P,mi)) sunStash(P,(int)mi,pal,(int)M->boneCount,model,0);   /* ⭐ the hero casts under the sun (09-11) */
        int isCloth9=clothPrep(g->clothSlot,P,mi,pal,model,W);
        if(!isCloth9){
            if(!g->hero&&(int)mi!=P->fmInterior) sunStash(P,(int)mi,pal,(int)M->boneCount,model,0);   /* ⭐ she casts (09-11) */
             }
        unsigned bs9[6]; unsigned*b=&P->vbo[mi*6];
        if(isCloth9==2){ memcpy(bs9,b,sizeof bs9); bs9[0]=g_skVbo[0]; bs9[1]=g_skVbo[1]; b=bs9; }   /* hanging cloth: its own buffers */
        { int tx=P->meshTex[mi];
          if(tx>=0){  if(g->texTint){   } }
          else if(g->texTint){ float c9[3]; meshTintOf(P,mi,c9);   }   /* an authored mesh carries its own colour, in its name */
           }
        { static const float wh9[3]={1.f,1.f,1.f}; bodyDrawMesh(P,mi,model,pal,b,wh9,1.f,isCloth9,1); }
        if(isCloth9){    }
        if(!g->hero){   } }
    g_vkOpen=0.f; g_vkWorld=NULL;
}
static void actorDraw(Pack*P,int clip,Pack*dnP,int dnC,double phase,const float*model,float*pal,
                      const SkPose*sk,float*footOut,float plantMix,
                      WmsState*wms,float wdt,float skSig,int groundIt,
                      const float*lookAt,int clothSlot){   /* footOut: 2 rig-space feet + the rest-foot height */
    if(!P||P->nNodes>1024) return;
    static float T[1024][3], Q[1024][4], S[1024][3]; static M4 W[1024];
    float restFoot=1e9f;
    bodyRest(P,T,Q,S);
    /* Z4: A BORROWED CLIP GOES THROUGH THE CANONICAL TRANSFER. donorPoseF poses the
     * bank's rig, takes each canonical bone's WORLD delta and puts this body's frame
     * where the bank's is -- so one authored shamble is the same shamble on any of the
     * 105 citizens, whatever their rest pose happens to be.
     * AND IT IS ROTATION-ONLY: the 16 canonical bones move, everything else holds its
     * rest. The pelvis HEIGHT the crawl clips carry does not cross -- see design/zombies.md.
     * Otherwise the pack's own clip, through the P1.5 INTERPOLATING sampler: actorDraw
     * was written after it and had quietly gone back to the nearest-frame memcpy, which
     * is a stair-step at any capture rate that is not the bake rate. */
    if(dnP&&dnC>=0&&(unsigned)dnC<dnP->nClips) donorPoseF(dnP,dnC,phase,P,Q);
    else if(clip>=0&&(unsigned)clip<P->nClips){
        float*sink9=g_morphSink; int sinkN9=g_morphSinkN;
        if(g_actorMorphW&&P->nMorph){ memset(g_actorMorphW,0,sizeof(float)*MORPH_MAX); g_morphSink=g_actorMorphW; g_morphSinkN=P->nMorph; }
        clipSampleF(P,&P->clips[clip],phase,T,Q,S);
        g_morphSink=sink9; g_morphSinkN=sinkN9; }
    if(g_actorMorphW&&g_actorMorphWas&&P->nMorph) morphBlend(P,g_actorMorphW,g_actorMorphWas);   /* the eyes shut before the draw */
    /* ⭐ THE UPPER-BODY LAYER (09-11): the caller names it in g_actorOver for the length of the call */
    if(g_actorOver&&g_actorOver[0]) poseOverlay(P,Q,g_actorOver,overPhase(P,g_actorOver,(double)g_setT),g_actorOverW);
    /* A SKATER IS POSED, NOT PLAYED. Measure the body's own leg length off the REST
     * pose first -- FK once, read hip to lowest foot -- so the crouch is a fraction of
     * this body rather than a number tuned on a body that is no longer on disk. */
    if(sk){
        bodyFK(P,T,Q,S,W);
        float hipY=(P->pb[0]>=0)?W[P->pb[0]][13]:0.f, ftY=1e9f;
        if(P->footL>=0&&W[P->footL][13]<ftY) ftY=W[P->footL][13];
        if(P->footR>=0&&W[P->footR][13]<ftY) ftY=W[P->footR][13];
        restFoot=ftY;
        float legLen=(ftY<1e8f)?(hipY-ftY):0.f;
        /* the signature is the BODY's, so the same rider is the same rider every take,
         * and the clock is the skate phase the tick already advances at 30/s */
        skateApply(P,sk,Q,T,legLen,skSig,(float)(phase/30.0)); }
    /* ⭐ SHE LOOKS AT IT. The same idea scnHeadLayer uses for the scene pair, aimed at a
     * world point instead of a person, on a body that has no scene. Neck and head split
     * the angle the way a real neck does -- the head takes about two thirds.
     * ⛔ CLAMPED, AND THE CLAMP IS THE POINT: a head has ~70 degrees before the shoulders
     * must follow, and past that an actor should TURN, which is a path and not a look. So
     * it saturates rather than unscrewing. */
    if(lookAt){
        float bx=model[12], bz=model[14];
        float dx=lookAt[0]-bx, dz=lookAt[2]-bz;
        float want=atan2f(dx,dz);
        float body=atan2f(model[8],model[10]);       /* the body's own facing */
        float d=want-body;
        while(d> 3.14159265f) d-=6.28318531f;
        while(d<-3.14159265f) d+=6.28318531f;
        const float MAXY=1.22f;                       /* ~70 degrees */
        if(d> MAXY) d= MAXY;
        if(d<-MAXY) d=-MAXY;
        int nk=P->pb[4], hd=P->pb[5];
        float off[4];
        if(hd>=0){ qaxis(off,0,1,0,d*0.66f); qmul(Q[hd],Q[hd],off); }
        if(nk>=0){ qaxis(off,0,1,0,d*0.34f); qmul(Q[nk],Q[nk],off); } }
    /* ⭐ THE WILLIAMS LAYER needs the pose MEASURED before it can comment on it -- the
     * contact/passing phase is the distance between the feet -- so FK first, overlay,
     * then FK again. The skater path has already paid for the first one. */
    if(wms){
        if(!sk) bodyFK(P,T,Q,S,W);
        wmsApply(P,T,Q,W,wms,wdt,g_wms); }
    bodyFK(P,T,Q,S,W);
    stretchTick(P,W);                                         /* ⭐ a drape held between two limbs (09-29) */
    if(g_actorChainSlot>=0) chainSim(P,W,g_actorChainSlot);   /* ⭐ her hair swings on the pose just solved (09-27) */
    /* THE FEET, MEASURED. The board is placed from these, so they are the contract --
     * and REVA_SKDBG prints them, which is what turns "her feet look wrong" into a
     * number that can be checked. */
    /* reported in RIG space; the caller puts them through the same matrix the body is
     * drawn with, plant included, or the board and the body disagree by the correction */
    /* the lead's two ends, in RIG space -- the caller puts them through the model matrix
     * exactly as it does the feet, so they stay attached through any pose. */
    if(footOut){
        int nb9=(P->pb[4]>=0)?P->pb[4]:P->pb[5];        /* neck, else head */
        int hb9=(P->gripNode>=0)?P->gripNode:P->pb[11]; /* grip hand, else forearm_r */
        if(nb9>=0){ footOut[8]=W[nb9][12]; footOut[9]=W[nb9][13]; footOut[10]=W[nb9][14]; }
        if(hb9>=0){ footOut[11]=W[hb9][12]; footOut[12]=W[hb9][13]; footOut[13]=W[hb9][14]; }
        for(int fk=0;fk<2;fk++){ int fb=fk?P->footR:P->footL;
            if(fb>=0){ footOut[fk*3+0]=W[fb][12]; footOut[fk*3+1]=W[fb][13]; footOut[fk*3+2]=W[fb][14]; }
            else      { footOut[fk*3+0]=footOut[fk*3+1]=footOut[fk*3+2]=0.f; } }
        footOut[6]=(restFoot<1e8f)?restFoot:0.f;
        /* THE KNEE TEST. A bent knee puts the ankle BEHIND it along the body's forward.
         * Positive means the leg is inverted -- the thing the eye catches instantly and
         * that no log ever mentioned until it was asked for. */
        footOut[7]=0.f;
        if(P->pb[13]>=0&&P->footL>=0)
            footOut[7]=W[P->footL][14]-W[P->pb[13]][14]; }
    /* PLANT THE FEET. The crouch bent the knees, which raises the soles off the rest
     * height; lift the body back by exactly that so a rider stands ON the board rather
     * than hovering over it or sinking into it. Rig-space delta, scaled by the model's
     * own scale to reach the world. */
    M4 mdl; memcpy(mdl,model,64);
    /* ⭐ STAND ON THE GROUND. The model was placed so the REST pose's lowest bone sits at
     * the actor's y; nothing ever looked again, so an animated body floated or sank by
     * however far its legs had moved since. Lift by exactly that difference and the
     * planted foot lands on the surface, whatever the pose is doing. Same mechanism the
     * skater's deck plant uses -- it was simply never given to anybody else. */
    if(groundIt&&!sk&&P->footL>=0&&P->footR>=0){
        float fy9=(W[P->footL][13]<W[P->footR][13])?W[P->footL][13]:W[P->footR][13];
        for(int w9=0;w9<P->nWheel;w9++){ float y9=W[P->wheel[w9]][13]; if(y9<fy9) fy9=y9; }   /* ⭐ ep 9: a deck stands on its wheels */
        float d9=(P->mny-fy9);
        /* a third of her height. The rest span is not stored, but P->scale was DERIVED
         * from it at load (scale = 3.15/(span*1.13)), so the span comes back out of it --
         * a measured number rather than a new constant to get wrong on the next body. */
        float span9=(P->scale>1e-6f)?(3.15f/(P->scale*1.13f)):3.f;
        float lim9=span9*0.33f;
        if(d9> lim9) d9= lim9;
        if(d9<-lim9) d9=-lim9;
        float eff9=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
        mdl[13]+=d9*eff9; }
    if(sk&&restFoot<1e8f){
        float ftY=1e9f;
        if(P->footL>=0&&W[P->footL][13]<ftY) ftY=W[P->footL][13];
        if(P->footR>=0&&W[P->footR][13]<ftY) ftY=W[P->footR][13];
        if(ftY<1e8f){
            float eff=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
            /* ...and then UP ONTO THE DECK. Planting to the rest height puts the soles on
             * the ROAD; a rider stands on a board, whose deck sits about 9cm up -- 0.16
             * club units at 1.75 per metre. The deck is then placed 0.16 back down from
             * the soles, which lands its wheels on the road. One number, used twice, in
             * opposite directions: they cannot drift apart. */
            mdl[13]+=((restFoot-ftY)*eff + SK_DECK_H)*plantMix; } }
    /* REVA_ACTORBOX=1: once a second, how far each actor's bones reach as posed, in its rig's units, and where it stands. A body's size from its own file, worked out anywhere
     * else, can be set beside this with no picture: it's how a dog that came out as a spike was told from a file that was one (step 23) */
    { static int box9=-1; if(box9<0){ const char*e9=getenv("REVA_ACTORBOX"); box9=(e9&&e9[0]=='1'); }
      if(box9&&(g_frameStamp%60u)==0u){ float lo9[3]={1e9f,1e9f,1e9f},hi9[3]={-1e9f,-1e9f,-1e9f}; int nb9=0;
          for(unsigned i9=0;i9<P->nNodes;i9++){ if(P->nodes[i9].kind!=1u) continue; nb9++; for(int k9=0;k9<3;k9++){ const float v9=W[i9][12+k9]; if(v9<lo9[k9]) lo9[k9]=v9; if(v9>hi9[k9]) hi9[k9]=v9; } }
          printf("[actorbox] %s clip %d%s: %d bones reach %.3f %.3f %.3f, y %.3f to %.3f; drawn x%.3f at y %.3f\n",P->label,clip,dnP?" (borrowed)":"",nb9,hi9[0]-lo9[0],hi9[1]-lo9[1],hi9[2]-lo9[2],lo9[1],hi9[1],
                 sqrtf(mdl[0]*mdl[0]+mdl[1]*mdl[1]+mdl[2]*mdl[2]),mdl[13]); fflush(stdout); } }
    if(g_actorChainSlot>=0) chainPublish(g_actorChainSlot,mdl);   /* her chains feel where she goes */
    if(g_actorChainSlot>=0) jiggleTick(P,W,mdl,g_actorChainSlot);  /* ⭐ and her soft mass springs (09-29) */
    /* ⭐ AND IF IT IS STANDING IN WATER, THE WATER KNOWS. The push scales with speed, so
     * standing still barely marks the surface, wading leaves a wake and arriving fast
     * makes a splash -- all three out of one multiply, because the body already knows how
     * fast it is going. waterDisturb ignores anything outside a pool's footprint. */
    if(g_nWat) waterDisturb(mdl[12],mdl[13],mdl[14],0.85f,0.010f*(float)g_chDt*60.f);
    { Pack*Q9=actorLod(P,mdl);
      if(Q9) bodyMeshesLod(P,Q9,W,mdl,pal);
      else { static const float wh9[3]={1.f,1.f,1.f}; BodyGL g9={clothSlot,NULL,0,0,0,wh9}; bodyMeshes(P,W,mdl,pal,&g9); } } }


static void actorsTick(float dt){
    g_actorDt=dt;
    int capWait9=0;
    { static int gate=-1;
      if(gate<0) gate=(getenv("REVA_SHOT")&&!getenv("REVA_SCENEFPS"))?1:0;
      if(!gate||g_capBegun) g_setT+=dt;
      capWait9=(gate&&!g_capBegun); }
    for(int i=0;i<g_setNA;i++){ SetActor*A=&g_setA[i];
        Pack*P=&g_setPk[A->pk];
        /* ---- Z7a THE CUES. The last cue whose time has passed is the one playing. ---- */
        if(A->nCue){
            int want=-1;
            for(int k=0;k<A->nCue;k++) if(A->cue[k].t<=g_setT) want=k;
            if(want!=A->curCue){
                A->curCue=want;
                if(want>=0){
                    actorClip(A,P,A->cue[want].clip,i,(A->spd>0.f)||A->onPath);
                    A->phase=0.0;
                    printf("[cue] t=%.2f actor %d -> %s\n",g_setT,i,A->cue[want].clip);
                    fflush(stdout); } } }
        /* ---- Z7a THE PATH. Waypoints are TIMES, so speed falls out of the blocking. ---- */
        if(A->nWp){
            int seg=-1;
            for(int k=0;k<A->nWp;k++) if(A->wp[k].t<=g_setT) seg=k;
            float x0,y0,z0,x1,y1,z1,t0,t1,u;
            if(seg<0){ x0=A->x; y0=A->y; z0=A->z; t0=0.f;
                       x1=A->wp[0].x; y1=A->wp[0].y; z1=A->wp[0].z; t1=A->wp[0].t; }
            else if(seg>=A->nWp-1){ SetWp*L=&A->wp[A->nWp-1];
                       x0=x1=L->x; y0=y1=L->y; z0=z1=L->z; t0=0.f; t1=1.f; }
            else { x0=A->wp[seg].x; y0=A->wp[seg].y; z0=A->wp[seg].z; t0=A->wp[seg].t;
                   x1=A->wp[seg+1].x; y1=A->wp[seg+1].y; z1=A->wp[seg+1].z; t1=A->wp[seg+1].t; }
            u=(t1>t0)?((g_setT-t0)/(t1-t0)):1.f;
            if(u<0.f) u=0.f;
            if(u>1.f) u=1.f;
            A->cx=x0+(x1-x0)*u; A->cy=y0+(y1-y0)*u; A->cz=z0+(z1-z0)*u;
            /* ⭐ AND SHE DOES NOT WALK THROUGH THE VAN. The path is authored in a world
             * that has things in it; this is the first time anything has checked. */
            colResolve(&A->cx,&A->cz,0.42f);
            float dx=x1-x0, dz=z1-z0, d=sqrtf(dx*dx+dz*dz);
            A->cspd=(t1>t0&&u<1.f)?(d/(t1-t0)):0.f;
            /* face the way she is going; club forward is +z */
            if(d>1e-3f&&A->cspd>0.f) A->cyaw=atan2f(dx,dz)*57.2957795f; }
        /* the clip's own fps drives the phase -- and a BORROWED clip's fps lives in the
         * bank it came from, not in the body playing it */
        /* ⭐ ON A PATH, THE STRIDE MATCHES THE SPEED. gaitRate() is the club's measure of
         * how far a clip's planted foot travels per frame, so a body crossing a lawn in
         * four seconds takes the strides that carry her there instead of sliding. */
        float grate=A->rate;
        if((A->nWp||A->mindOn)&&A->cspd>0.02f){
            Pack*gp=A->dnP?A->dnP:P; int gc=A->dnP?A->dnC:A->clip;
            if(gc>=0) grate=A->rate*gaitRate(gp,gc,A->cspd); }
        /* ⭐ a z_rm_ clip is a move with a BEGINNING (10-18): under a capture it waits for the shot, as the set clock does,
         * so a burst opens on its first frame (a boy waking, not a boy already awake) */
        { const char*cn9=(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)?packStr(A->dnP,A->dnP->clips[A->dnC].name)
                        :((A->clip>=0&&(unsigned)A->clip<P->nClips)?packStr(P,P->clips[A->clip].name):"");
          const int rm9=(cn9&&!strncmp(cn9,"z_rm_",5)); if(capWait9&&rm9) grate=0.f;
          /* (step 21) where her clip starts: `clip>12.5s` or `clip>0.33` on her line, or the set's `clipstart` for a z_rm_ clip. While a capture's waiting she stands on that frame, and plays on from it */
          if(!A->cued||(capWait9&&rm9)){ const PClip*c8=(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)?&A->dnP->clips[A->dnC]:((A->clip>=0&&(unsigned)A->clip<P->nClips)?&P->clips[A->clip]:NULL);
              if(c8){ double st9=-1.0; if(A->cueS>=0.f) st9=(double)A->cueS*c8->fps; else if(A->cueF>=0.f) st9=(double)A->cueF*c8->frames; else if(rm9&&g_clipStartS>0.f) st9=(double)g_clipStartS*c8->fps;
                  if(st9>=0.0){ if(c8->frames>1&&st9>(double)c8->frames-1.0) st9=(double)c8->frames-1.0; A->phase=st9; } A->cued=1; } } }
        if(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips)
            A->phase+=dt*A->dnP->clips[A->dnC].fps*grate;
        else if(A->clip>=0&&(unsigned)A->clip<P->nClips)
            A->phase+=dt*P->clips[A->clip].fps*grate;
        /* ⭐ A z_rm_ TRANSITION ON A SET ACTOR PLAYS ONCE AND HOLDS (10-18) -- a boy waking in a pan does not fall back
         * asleep and wake again every eight seconds. A z_rm_ LOOP (the boys' huddle) still loops. */
        { Pack*cp9=NULL; PClip*c9=NULL;
          if(A->dnP&&A->dnC>=0&&(unsigned)A->dnC<A->dnP->nClips){ cp9=A->dnP; c9=&A->dnP->clips[A->dnC]; }
          else if(A->clip>=0&&(unsigned)A->clip<P->nClips){ cp9=P; c9=&P->clips[A->clip]; }
          if(c9&&c9->frames>1&&!strncmp(packStr(cp9,c9->name),"z_rm_",5)&&A->phase>(double)(c9->frames-1)
             &&clipHoldsEnd(cp9,c9,100+i,packStr(cp9,c9->name))) A->phase=(double)(c9->frames-1); }
        if(A->spd>0.f&&A->len>0.f){ A->travel+=A->spd*dt;
            while(A->travel>A->len) A->travel-=A->len; }
        if(A->skate) A->phase+=dt*30.0; }
    mindTick(dt);   /* ⭐ the minds decide and move AFTER the timelines (09-11) */ }

