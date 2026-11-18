/* ======================== CLOTH =================================================
 * Loose fabric with physics, which adjusts to whatever body it is on: an essential feature for a game.
 *
 * ⭐ AND THE SOLVER WAS ALREADY HERE. chainSim is verlet with distance constraints and has
 * been running hair and tails since H1. Cloth is the same arithmetic on VERTICES instead
 * of bones, with the constraints taken from the mesh's own edges and the particles pushed
 * out of the body.
 *
 * ⛔ FOUR STATIC FITS FAILED BEFORE THIS, and they were always going to: a garment is not
 * a shape, it is a shape plus what gravity and a body do to it. Choosing a better radius
 * cannot produce cloth, because cloth is what happens AFTER you choose one.
 *
 * ⛔ SPHERES, NOT RAY CASTS. Cloth against a triangle mesh is far bigger and slower than
 * this needs; a shirt needs to not pass through a torso, and a torso is a few capsules.
 * The limit: this will not drape over a chair arm or catch on a fence.
 *
 * ⛔ AND A CLOTH MESH IS NOT SKINNED AT DRAW -- the solver outputs WORLD positions, so it
 * is uploaded per frame and drawn with an identity model and palette, or the GPU would
 * skin the simulation a second time. */
/* ⛔ 2048 WAS A SILENT CAP (09-11). clothPrep returned 0 for any garment over it and said
 * nothing, so the first premade garment (the cargo pants, 4271 vertices) skinned straight
 * through the bought character's hips with the solver never engaged, and two A/B sheets came back identical.
 * The pyjamas were under it, which is why it was never met. 8192 is what a modelled garment
 * needs; a refusal prints now. */
#define CLOTH_MAXV 8192
#define CLOTH_MAXE 32768
#define CLOTH_MAXS 20
typedef struct {
    Pack *pk; int mesh;                 /* which mesh of which pack this is */
    int nv, ne;
    float cur[CLOTH_MAXV][3], prv[CLOTH_MAXV][3];
    unsigned short e0[CLOTH_MAXE], e1[CLOTH_MAXE]; float elen[CLOTH_MAXE];
    /* ⭐ FOLDS (09-11): a bend constraint across every interior edge (the two opposite
     * vertices of the two triangles that share it), and the radius inside which two
     * particles that are not neighbours push apart (self-collision) */
    unsigned short b0[CLOTH_MAXE], b1[CLOTH_MAXE]; float blen[CLOTH_MAXE]; int nb;
    unsigned short opp0[CLOTH_MAXE], opp1[CLOTH_MAXE];
    float rself;
    unsigned char pin[CLOTH_MAXV];      /* pinned to the skinned pose (the shoulders) */
    unsigned stamp;                     /* the frame this was last stepped on */
    int pieces;                         /* connected components: shirt + two trouser legs */
    int seeded;
} Cloth;
/* Slots 0..SCN_MAXCAST-1 belong to the hero and the scene cast; the rest are for SET
 * ACTORS, because a street full of people is where curtains and coats will actually be
 * seen and the bench (ridecheck.py) drives its subject as an actor. */
_Static_assert(CLOTH_ACTOR0==SCN_MAXCAST,"cloth actor slots must start after the cast");
static Cloth g_cloth[CLOTH_SLOTS];
static int   g_clothOff=0;
/* ⭐ HOW MUCH OF THE SKINNED POSE THE FABRIC KEEPS. Near 1 is a fitted shirt that moves
 * exactly with her; lower is looser and older and more its own thing; 0 is a cloak. */
static float g_clothFollow=0.55f;
/* ⭐ EASE: how much MORE fabric there is than the body needs. Rest lengths are scaled
 * by (1+ease) while the particles start where they were BUILT, so the garment carries
 * surplus and the surplus becomes folds. Cloth reads as cloth because it creases; a
 * tube built exactly at its own equilibrium has nothing to crease with and comes back
 * looking like foam -- which is what makes the material look
 * too thick. ⛔ It belongs on the REST LENGTH, not the position: building the mesh
 * wider is the same tent further out, and he objected to that in the same breath. */
/* ⛔ DEFAULT OFF, BECAUSE THE SWEEP SAID SO. The theory was sound -- fabric folds
 * because it has surplus -- but the pictures disagree: at 0.10 holes open in the
 * garment and at 0.22 it tears itself apart. Surplus only becomes FOLDS if something
 * resists buckling, and this solver has neither bending constraints nor self-collision,
 * so slack quads simply invert and pass through each other and through her. The knob
 * stays because it is the right lever; it does not pay until bend + self-collision land.
 * Sweep it with REVA_CLOTHEASE and LOOK -- 0.00 / 0.10 / 0.22 is the comparison. */
static float g_clothEase=0.0f;

static int meshIsCloth(Pack*P,unsigned mi){
    if(!(P->meshes[mi].flags&4)) return 0;
    return strstr(packStr(P,P->meshes[mi].name),"~cloth")!=NULL; }
/* ⭐ WARDROBE: A WORN GARMENT. `<name>~worn` is a premade garment dresspack.py put on a
 * finished pack: skinned with the weights of the skin under it (which it hides), drawn and
 * shadowed like the body -- and, like `~cloth`, NEVER MEASURED as the body. The limb radii the
 * contact pass, the ragdoll and the clip audit stand on are the body's own, so a pair of baggy
 * cargo pants does not fatten her calf capsules and push her knees apart. */
static int meshIsGarment(Pack*P,unsigned mi){
    const char*n=packStr(P,P->meshes[mi].name);
    return n&&(strstr(n,"~cloth")||strstr(n,"~worn")||!strncmp(n,"wear~",5)); }

/* Build the particle set and the edge list, once per body. */
static void clothInit(Cloth*C,Pack*P,unsigned mi,float*skinned){
    PMesh*M=&P->meshes[mi];
    C->pk=P; C->mesh=(int)mi;
    C->nv=(int)((M->verts<CLOTH_MAXV)?M->verts:CLOTH_MAXV);
    for(int v=0;v<C->nv;v++){
        for(int k=0;k<3;k++){ C->cur[v][k]=skinned[v*3+k]; C->prv[v][k]=skinned[v*3+k]; }
        C->pin[v]=0; }
    C->ne=0;
    /* ⭐ DEDUP. Every interior edge belongs to two triangles, so adding one per triangle
     * stores it TWICE: 2404 triangles asked for 7212 edges against CLOTH_MAXE 6144, the
     * list filled up and stopped, and the ~1068 it dropped were the sleeves and seams --
     * last in the index buffer. The garment's graph fell into 182 pieces, clothInit pinned
     * the top of every one of them, and a cloth that cannot move keeps the shape it was
     * built in. The log said "6141 edges, 182 pieces" and read like a report.
     * ⛔ The duplicates also solved each edge twice per iteration, so the fabric was
     * stiffer than anything ever asked it to be. */
    #define CL_HB 16                       /* 65536 buckets for at most 32768 edges (open addressing wants room) */
    static int hh[1<<CL_HB];
    for(int i=0;i<(1<<CL_HB);i++) hh[i]=-1;
    int dropped=0;
    unsigned*ix=(unsigned*)(P->pk+M->offIdx);
    C->nb=0; float elsum=0.f;
    for(unsigned t=0;t+2<M->idxCount; t+=3){
        unsigned a=ix[t],b=ix[t+1],c=ix[t+2];
        unsigned pr[3][2]={{a,b},{b,c},{c,a}}; unsigned third[3]={c,a,b};
        for(int k=0;k<3;k++){
            unsigned i=pr[k][0], j=pr[k][1];
            if(i>=(unsigned)C->nv||j>=(unsigned)C->nv||i==j) continue;
            if(i>j){ unsigned tmp=i; i=j; j=tmp; }
            /* nv <= CLOTH_MAXV (2048), so (i,j) packs into one key with no collisions
             * of MEANING -- the table below only has to resolve collisions of BUCKET. */
            unsigned key=(i<<16)|j, h=(key*2654435761u)>>(32-CL_HB), seen=0;
            while(hh[h]>=0){
                if((unsigned)((C->e0[hh[h]]<<16)|C->e1[hh[h]])==key){ seen=1; break; }
                h=(h+1)&((1<<CL_HB)-1); }
            if(seen){ int e=hh[h]; if(C->opp1[e]==65535&&third[k]<(unsigned)C->nv&&third[k]!=C->opp0[e]) C->opp1[e]=(unsigned short)third[k]; continue; }
            if(C->ne>=CLOTH_MAXE){ dropped++; continue; }
            hh[h]=C->ne;
            C->opp0[C->ne]=(third[k]<(unsigned)C->nv)?(unsigned short)third[k]:65535; C->opp1[C->ne]=65535;
            /* ⛔ REST LENGTHS COME FROM THE SKINNED WORLD POSE, NOT THE PACK'S LOCAL
             * VERTICES. The solver runs in world space; the co-star's pyjama pack loads at
             * scale 0.0182, so local-space rest lengths asked 3072 constraints to
             * inflate the garment 55x on frame one and it filled the screen. Measuring
             * the pose we are actually starting from also starts the sim with ZERO
             * stored energy. */
            float d[3]; for(int q=0;q<3;q++) d[q]=skinned[j*3+q]-skinned[i*3+q];
            /* the ease lives HERE, on the rest length, and nowhere near the position:
             * the particles start where they were built (close to her skin) while the
             * fabric behaves as though there were more of it. Building the mesh wider
             * instead would just be the same tent further out. */
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])*(1.f+g_clothEase);
            C->e0[C->ne]=(unsigned short)i; C->e1[C->ne]=(unsigned short)j;
            C->elen[C->ne]=L; elsum+=L; C->ne++; } }
    /* ⭐ THE BENDS: one per interior edge, between the two opposite vertices, at the distance
     * they have now. Softer than the edges (below), so the fabric folds instead of creasing
     * into a polygon -- and it is what stops a hem folding flat back on itself. */
    for(int e=0;e<C->ne&&C->nb<CLOTH_MAXE;e++){ if(C->opp0[e]==65535||C->opp1[e]==65535) continue;
        unsigned i=C->opp0[e], j=C->opp1[e]; float d[3]; for(int q=0;q<3;q++) d[q]=skinned[j*3+q]-skinned[i*3+q];
        C->b0[C->nb]=(unsigned short)i; C->b1[C->nb]=(unsigned short)j; C->blen[C->nb]=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); C->nb++; }
    /* self-collision radius: just over half a mean edge, so neighbours are never "colliding" */
    C->rself=(C->ne>0)?0.55f*(elsum/(float)C->ne)/(1.f+g_clothEase):0.f;
    /* ⛔ AND SAY SO WHEN IT RUNS OUT. Silently keeping the first N edges turns an overflow
     * into a plausible-looking piece count, which is how this hid. */
    if(dropped) printf("[cloth] !! %s: EDGE CAP HIT -- %d edges dropped (CLOTH_MAXE %d). "
                       "The garment WILL fall into extra pieces and over-pin.%c",
                       packStr(P,M->name),dropped,CLOTH_MAXE,10);
    if(M->verts>CLOTH_MAXV) printf("[cloth] !! %s: %u verts truncated to %d%c",
                                   packStr(P,M->name),M->verts,CLOTH_MAXV,10);
    #undef CL_HB

    /* ⭐ PIN THE TOP OF EVERY PIECE, NOT THE TOP OF THE MESH. A shirt hangs from its own
     * shoulders and trousers hang from their own waistband -- but this pinned the highest
     * 8% of the WHOLE garment, so the shirt held and the trousers, having nothing above
     * them, slid straight down her legs and pooled. On the bench it read as one long
     * skirt with a bare torso above it.
     * The pieces are exactly the CONNECTED COMPONENTS of the edge graph, which needs no
     * new file format and no authoring convention: a shirt and two trouser legs are three
     * components, a curtain is one, a bedsheet is one. Union-find over the edges we just
     * built, then the top ring of each. */
    { static short par[CLOTH_MAXV];
      for(int v=0;v<C->nv;v++) par[v]=(short)v;
      for(int e=0;e<C->ne;e++){
          int a=C->e0[e], b=C->e1[e];
          while(par[a]!=a) a=par[a]=par[par[a]];
          while(par[b]!=b) b=par[b]=par[par[b]];
          if(a!=b) par[a]=(short)b; }
      for(int v=0;v<C->nv;v++){ int r=v; while(par[r]!=r) r=par[r]; par[v]=(short)r; }
      /* each component's own vertical extent, then its own top band */
      static float clo[CLOTH_MAXV], chi[CLOTH_MAXV];
      for(int v=0;v<C->nv;v++){ clo[v]=1e9f; chi[v]=-1e9f; }
      for(int v=0;v<C->nv;v++){ int r=par[v]; float y=skinned[v*3+1];
          if(y<clo[r]) clo[r]=y;
          if(y>chi[r]) chi[r]=y; }
      int nc=0;
      for(int v=0;v<C->nv;v++){
          int r=par[v]; float lo=clo[r], hi=chi[r];
          if(hi-lo<1e-6f) continue;
          if(skinned[v*3+1] > lo+(hi-lo)*0.92f) C->pin[v]=1; }
      for(int v=0;v<C->nv;v++) if(par[v]==v) nc++;
      C->pieces=nc; }
    C->seeded=1;
    printf("[cloth] %s mesh %u: %d particles, %d edges, %d bends, self-radius %.4f, %d pieces, %d pinned%c",
           packStr(P,M->name),mi,C->nv,C->ne,C->nb,C->rself,C->pieces,
           ({int p=0; for(int v=0;v<C->nv;v++) p+=C->pin[v]; p;}),10);
    fflush(stdout); }

/* One step. `skinned` is the animated pose in WORLD space -- the pins follow it and the
 * free particles fall away from it. */
/* ⭐ SMALL STEPS HERE TOO (Macklin et al. SCA 2019). The SET cloth got this and the
 * GARMENT solver did not -- found by checking rather than remembering while listing the
 * open avenues, and it was the one item on that list I would have got wrong. n substeps of
 * dt/n with ONE solve each beat one step of dt with n solves, at identical cost. */
/* ⭐ THE BODY MESH ITSELF (09-11). Clothing can't clip into the body: it has to respect
 * the mesh. The capsules are a body drawn with sixteen sausages; a waistband sat inside her
 * hips where no capsule was. The caller skins the body's own vertices (and normals) for this
 * frame and hands them here; a uniform grid over them, and every free particle that sits under
 * its nearest body vertex's tangent plane by less than the clearance is moved out along that
 * normal. `inside` counts particles found under the skin BEFORE the push -- the receipt.
 * REVA_CLOTHMESH=0 keeps the count and skips the push, for the A/B. */
#define CLOTH_BODYMAX 24000
static int g_clothMeshOn=-1, g_clothInside=0, g_clothPushed=0;
static void clothBodyCollide(Cloth*C,const float*bP,const float*bN,int nB,float clr,float rad){
    if(nB<=0||rad<=1e-6f) return;
    if(g_clothMeshOn<0){ const char*e=getenv("REVA_CLOTHMESH"); g_clothMeshOn=(e&&e[0]=='0')?0:1; }
    static int head[8192]; static int nxt[CLOTH_BODYMAX];
    for(int h=0;h<8192;h++) head[h]=-1;
    float inv=1.f/rad;
    for(int i=0;i<nB;i++){ int cx=(int)floorf(bP[i*3]*inv), cy=(int)floorf(bP[i*3+1]*inv), cz=(int)floorf(bP[i*3+2]*inv);
        unsigned h=((unsigned)cx*73856093u^(unsigned)cy*19349663u^(unsigned)cz*83492791u)&8191u; nxt[i]=head[h]; head[h]=i; }
    float r2=rad*rad; int inside=0, pushed=0;
    /* ⛔ PINNED PARTICLES TOO. The pinned band IS the waistband -- it follows the skinned pose
     * and skips every other collision, so a waistband skinned a centimetre inside her hips
     * stayed inside her for the whole take. Respecting the mesh has no exemptions. */
    for(int v=0;v<C->nv;v++){
        float*p=C->cur[v]; int cx=(int)floorf(p[0]*inv), cy=(int)floorf(p[1]*inv), cz=(int)floorf(p[2]*inv);
        int best=-1; float bd=r2;
        for(int dz=-1;dz<=1;dz++) for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){
            unsigned h=((unsigned)(cx+dx)*73856093u^(unsigned)(cy+dy)*19349663u^(unsigned)(cz+dz)*83492791u)&8191u;
            for(int i=head[h];i>=0;i=nxt[i]){ float ddx=p[0]-bP[i*3],ddy=p[1]-bP[i*3+1],ddz=p[2]-bP[i*3+2]; float d2=ddx*ddx+ddy*ddy+ddz*ddz; if(d2<bd){ bd=d2; best=i; } } }
        if(best<0) continue;
        const float*bv=bP+best*3,*bn=bN+best*3;
        float d=(p[0]-bv[0])*bn[0]+(p[1]-bv[1])*bn[1]+(p[2]-bv[2])*bn[2];
        if(d<0.f) inside++;
        if(d<clr&&g_clothMeshOn){ float push=clr-d; p[0]+=bn[0]*push; p[1]+=bn[1]*push; p[2]+=bn[2]*push; pushed++;
            if(C->pin[v]){ C->prv[v][0]+=bn[0]*push; C->prv[v][1]+=bn[1]*push; C->prv[v][2]+=bn[2]*push; } } }   /* a pinned particle moves cur AND prv: no velocity injected */
    g_clothInside=inside; g_clothPushed=pushed; }
static void clothStep(Cloth*C,float*skinned,const float(*sph)[4],int nsph,float dtFull,float scl,
                      const float*bP,const float*bN,int nB,float clr,float rad){
    if(dtFull>0.033f) dtFull=0.033f;
    if(dtFull<1e-4f) dtFull=1e-4f;
    const int NSUB=3;
    for(int ss=0;ss<NSUB;ss++){
    float dt=dtFull/(float)NSUB;
    float g=-9.0f*scl*dt*dt;
    for(int v=0;v<C->nv;v++){
        if(C->pin[v]){ for(int k=0;k<3;k++){ C->prv[v][k]=C->cur[v][k]=skinned[v*3+k]; } continue; }
        float np[3];
        for(int k=0;k<3;k++){
            float vel=(C->cur[v][k]-C->prv[v][k])*0.94f;
            np[k]=C->cur[v][k]+vel+(k==1?g:0.f); }
        /* ⭐ THE SKINNED POSE DOMINATES, and this is the whole difference between a
         * garment and a cloak. At 0.08 this was "a weak pull back", so gravity won a little
         * more every frame and the pyjamas slid off her shoulders and hung open down her
         * front, nothing like pyjamas. That was a correct simulation of the wrong
         * object: pin a sheet at its top and let it fall and you have made a cape.
         * Skinning is what puts a sleeve on an arm and a waistband on a waist. The SIM
         * supplies what skinning cannot -- lag, folds, swing, the way loose cloth arrives a
         * moment after the body does. So the blend leans hard on the skin, and "loose" is
         * how much it is allowed to lag. */
        /* ⛔ AND THE FOLLOW MUST NOT BECOME MOMENTUM. Adding it to np and then storing
         * prv=cur made the pull toward the skin part of the VELOCITY -- in Verlet the
         * velocity IS cur-prv -- so each step the particle was thrown 55% of the way to her
         * skin, remembered that as speed, sailed past, and got shoved back by the
         * constraints. It never settled: on a T-POSE, with nothing moving, 1.8% of the
         * pixels changed every frame, and 5.1% once ease widened the disagreement the pull
         * had to cross. It showed at once, as z-fighting or as vibration,
         * and it is the vibrating one: with the solver off the
         * same frames are bit-identical, which is not what z-fighting does.
         * ⭐ A POSITIONAL ATTACHMENT MOVES cur AND prv TOGETHER. Same steady pull toward
         * the skinned pose, zero velocity injected, so the fabric can come to rest. */
        for(int k=0;k<3;k++){ C->prv[v][k]=C->cur[v][k]; C->cur[v][k]=np[k]; }
        for(int k=0;k<3;k++){
            float f=(skinned[v*3+k]-C->cur[v][k])*g_clothFollow;
            C->cur[v][k]+=f; C->prv[v][k]+=f; } }
    /* ⭐ XPBD here too (design/papers/), same reason: a garment's stiffness should be a
     * property of the fabric, not of how many passes the loop runs. */
    static float glam[CLOTH_MAXE];
    memset(glam,0,(size_t)C->ne*sizeof(float));
    /* ⛔ alphaTilde uses the SUBSTEP dt -- shrink dt by n and alphaTilde grows by n^2, so
     * forgetting this makes the fabric n^2 times softer the moment substepping is on. */
    /* ⭐ THREADBARE IS SOFTER. Compliance is a physical constant now, so "old" is just a
     * bigger number: worn cotton stretches and folds where new cloth holds its shape.
     * 4.0e-7 was nearly inextensible -- that is canvas, not pyjamas. */
    /* ⛔ AND 5.0e-6 OVERSHOT INTO A GOWN. Compliance is a physical constant, so the
     * number is readable: 4.0e-7 was canvas, 5.0e-6 flowed like cheesecloth and billowed
     * behind her on the bench. Old cotton is between them. */
    float aG=1.6e-6f/(dt*dt);
    {
        for(int e=0;e<C->ne;e++){
            int i=C->e0[e], j=C->e1[e];
            float d[3]; for(int k=0;k<3;k++) d[k]=C->cur[j][k]-C->cur[i][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) continue;
            float wi=C->pin[i]?0.f:1.f, wj=C->pin[j]?0.f:1.f;
            float wsum=wi+wj; if(wsum<1e-6f) continue;
            float Cc=L-C->elen[e];
            float dl=(-Cc-aG*glam[e])/(wsum+aG);
            glam[e]+=dl;
            for(int k=0;k<3;k++){ float n=d[k]/L;
                C->cur[i][k]-=wi*n*dl;
                C->cur[j][k]+=wj*n*dl; } }
        /* ⭐ THE BENDS (09-11): the same XPBD distance constraint over the opposite-vertex
         * pairs, fifty times more compliant than the edges. REVA_CLOTHBEND=0 for the A/B. */
        { static int bendOn=-1; if(bendOn<0){ const char*e=getenv("REVA_CLOTHBEND"); bendOn=(e&&e[0]=='0')?0:1; }
          if(bendOn&&C->nb){ static float blam[CLOTH_MAXE]; if(ss==0) memset(blam,0,(size_t)C->nb*sizeof(float));
            float aB=8.0e-5f/(dt*dt);
            for(int e=0;e<C->nb;e++){
                int i=C->b0[e], j=C->b1[e];
                float d[3]; for(int k=0;k<3;k++) d[k]=C->cur[j][k]-C->cur[i][k];
                float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) continue;
                float wi=C->pin[i]?0.f:1.f, wj=C->pin[j]?0.f:1.f; float wsum=wi+wj; if(wsum<1e-6f) continue;
                float Cc=L-C->blen[e]; float dl=(-Cc-aB*blam[e])/(wsum+aB); blam[e]+=dl;
                for(int k=0;k<3;k++){ float n=d[k]/L; C->cur[i][k]-=wi*n*dl; C->cur[j][k]+=wj*n*dl; } }
            /* ⭐ SELF-COLLISION: a uniform grid of cells 2 self-radii wide; any two particles closer
             * than the self radius push apart. Neighbours along an edge sit a full edge apart and never
             * trip it. This is what lets a sleeve fold over an arm instead of through itself. */
            if(C->rself>1e-5f){ static int head[4096]; static short nxt[CLOTH_MAXV]; for(int h=0;h<4096;h++) head[h]=-1;
                float cs=C->rself*2.f, inv=1.f/cs; int pushed=0;
                for(int v=0;v<C->nv;v++){ int cx=(int)floorf(C->cur[v][0]*inv), cy=(int)floorf(C->cur[v][1]*inv), cz=(int)floorf(C->cur[v][2]*inv);
                    unsigned h=((unsigned)cx*73856093u^(unsigned)cy*19349663u^(unsigned)cz*83492791u)&4095u; nxt[v]=(short)head[h]; head[h]=v; }
                float r2=C->rself*C->rself;
                for(int v=0;v<C->nv;v++){ int cx=(int)floorf(C->cur[v][0]*inv), cy=(int)floorf(C->cur[v][1]*inv), cz=(int)floorf(C->cur[v][2]*inv);
                    for(int dz9=-1;dz9<=1;dz9++) for(int dy9=-1;dy9<=1;dy9++) for(int dx9=-1;dx9<=1;dx9++){
                        unsigned h=((unsigned)(cx+dx9)*73856093u^(unsigned)(cy+dy9)*19349663u^(unsigned)(cz+dz9)*83492791u)&4095u;
                        for(int u=head[h];u>=0;u=nxt[u]){ if(u<=v) continue;
                            float ddx=C->cur[u][0]-C->cur[v][0], ddy=C->cur[u][1]-C->cur[v][1], ddz=C->cur[u][2]-C->cur[v][2]; float d2=ddx*ddx+ddy*ddy+ddz*ddz;
                            if(d2>=r2||d2<1e-10f) continue;
                            float dd=sqrtf(d2), push=(C->rself-dd)/dd*0.5f;
                            float wv=C->pin[v]?0.f:1.f, wu=C->pin[u]?0.f:1.f; if(wv+wu<1e-6f) continue;
                            C->cur[v][0]-=ddx*push*wv; C->cur[v][1]-=ddy*push*wv; C->cur[v][2]-=ddz*push*wv;
                            C->cur[u][0]+=ddx*push*wu; C->cur[u][1]+=ddy*push*wu; C->cur[u][2]+=ddz*push*wu; pushed++; } } }
                { static int n9=0; static int dbg=-1; if(dbg<0) dbg=getenv("REVA_CLOTHDBG")?1:0; if(dbg&&ss==0&&(n9++%120)==0){ printf("[cloth] self-collision: %d pairs pushed apart this substep\n",pushed); fflush(stdout); } } } } }
        /* ⛔ AND OUT OF THE BODY, every pass -- resolving edges can push a particle back
         * inside her, so the collision has to be the LAST thing each iteration. */
        for(int v=0;v<C->nv;v++){
            if(C->pin[v]) continue;
            for(int q=0;q<nsph;q++){
                float dx=C->cur[v][0]-sph[q][0], dy=C->cur[v][1]-sph[q][1], dz=C->cur[v][2]-sph[q][2];
                float d2=dx*dx+dy*dy+dz*dz, r=sph[q][3];
                if(d2>=r*r||d2<1e-9f) continue;
                float d=sqrtf(d2), push=(r-d)/d;
                C->cur[v][0]+=dx*push; C->cur[v][1]+=dy*push; C->cur[v][2]+=dz*push; } } }
        /* ⭐ and out of the BODY MESH, last of all, every substep (09-11) */
        clothBodyCollide(C,bP,bN,nB,clr,rad);
    }   /* end substep */ }

/* ⭐ ONE HOOK, BOTH BODIES. This began life inline in labDrawPartner, which quietly said
 * that only the OTHER person in a scene can wear clothes -- and the player is going to
 * wear clothes. It also meant the engine's own four-angle bench (ridecheck.py) could not see
 * a garment at all, because that bench drives its subject as the HERO.
 *
 * Skin the garment on the CPU (the palette is already built by the caller), build the body
 * as spheres at the canonical bones, step, upload, and leave the GPU set up to draw the
 * result UNSKINNED -- identity model, identity palette, identity bind -- because the
 * solver's output is already in world space and skinning it again throws it across the set.
 * Returns 1 if it took the mesh; the caller must hand the model matrix back afterwards. */
/* ⭐ MEASURE THE BODY. Every vertex knows its dominant bone, so each canonical bone gets
 * the spread of the vertices that belong to it -- a thick thigh gets a thick capsule and a
 * thin wrist does not, on any body, with nothing to tune. Done once, in REST, because a
 * radius is a property of the body and not of this frame's pose. */
/* ⭐ THE HEAD AS AN ELLIPSOID (10-18), from the pack's own head vertices (not its hair, not its clothes), in the
 * head bone's REST frame orthonormalised; the 2nd..98th percentile on each axis, so a nose tip or an ear rim does
 * not swell it past the skull (it must sit just INSIDE the head: hair at rest is never pushed). */
static int cmpf9(const void*a,const void*b){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }
static void measureHead(Pack*P){
    if(P->headOK) return;
    P->headOK=-1;
    int h=P->pb[5]; if(h<0||(unsigned)h>=P->nNodes||P->nNodes>1024) return;
    static M4 W[1024];
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc;
        m4_trs(loc,P->nodes[i].t,P->nodes[i].q,P->nodes[i].s);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64);
        else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    float ax[3][3]; const float*w=W[h];
    memcpy(P->headW0,W[h],64);                        /* the head bone at REST: where the hair sits on it */
    for(int c=0;c<3;c++){ float l=sqrtf(w[c*4]*w[c*4]+w[c*4+1]*w[c*4+1]+w[c*4+2]*w[c*4+2]); if(l<1e-9f) return;
        for(int k=0;k<3;k++) ax[c][k]=w[c*4+k]/l; }
    static float vals[3][8192]; int nv=0;
    for(unsigned m=0;m<P->nMeshes&&nv<8192;m++){ PMesh*M=&P->meshes[m];
        if(!(M->flags&4)||!M->boneCount||M->boneCount>MAXB) continue;
        if(meshIsGarment(P,m)) continue;
        unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
        float*rp=(float*)(P->pk+M->offPos);
        unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
        unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
        unsigned step=(M->verts>6000)?(M->verts/6000):1; if(!step) step=1;
        for(unsigned v=0;v<M->verts&&nv<8192;v+=step){
            int best=-1, bw=-1;
            for(int q=0;q<4;q++){ int ww=wp[v*4+q]; if(ww>bw){ bw=ww; best=jp[v*4+q]; } }
            if(best<0||best>=(int)M->boneCount||bw<200) continue;      /* ALL head: not the neck's blend */
            if(bm[best]!=(unsigned)h) continue;
            float d[3]={rp[v*3]-w[12], rp[v*3+1]-w[13], rp[v*3+2]-w[14]};
            for(int c=0;c<3;c++) vals[c][nv]=ax[c][0]*d[0]+ax[c][1]*d[1]+ax[c][2]*d[2];
            nv++; } }
    if(nv<64) return;
    for(int c=0;c<3;c++){ qsort(vals[c],(size_t)nv,sizeof(float),cmpf9);
        float lo=vals[c][nv*2/100], hi=vals[c][nv*98/100];
        P->headC[c]=0.5f*(lo+hi); P->headR[c]=0.5f*(hi-lo)*0.96f;
        if(P->headR[c]<1e-5f) return; }
    P->headOK=1;
    printf("[vcol] %s: head ellipsoid from %d verts, radii %.3f %.3f %.3f (rig u)\n",P->label,nv,
           P->headR[0],P->headR[1],P->headR[2]); fflush(stdout); }

static int g_brfPct=-1;   /* (11-06) which percentile of a bone's flesh the hair's capsules reach: REVA_HAIRCAPPCT, 90 */
static void measureBoneR(Pack*P){
    if(P->boneRok) return;
    P->boneRok=1;
    if(g_brfPct<0){ const char*e=getenv("REVA_HAIRCAPPCT"); g_brfPct=(e&&*e)?atoi(e):90; if(g_brfPct<50) g_brfPct=50; if(g_brfPct>99) g_brfPct=99; }
    for(int c=0;c<NCANON;c++){ P->boneR[c]=0.f; P->boneRF[c]=0.f; }
    static M4 W[1024];
    if(P->nNodes>1024) return;
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc;
        m4_trs(loc,P->nodes[i].t,P->nodes[i].q,P->nodes[i].s);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64);
        else m4_mul(W[i],W[P->nodes[i].parent],loc); }
    /* each bone's first child, so a bone can be a SEGMENT and not a point */
    static short kid[1024];
    for(unsigned i=0;i<P->nNodes;i++) kid[i]=-1;
    for(unsigned i=0;i<P->nNodes;i++){
        int pa=P->nodes[i].parent;
        if(pa>=0&&pa<(int)P->nNodes&&kid[pa]<0) kid[pa]=(short)i; }
    /* node -> canonical slot */
    static short c_of[1024];
    for(unsigned i=0;i<P->nNodes;i++) c_of[i]=-1;
    for(int c=0;c<NCANON;c++) if(P->pb[c]>=0&&(unsigned)P->pb[c]<P->nNodes) c_of[P->pb[c]]=(short)c;
    static float acc[NCANON]; static int cnt[NCANON], cntF[NCANON];
    #define BRF_MAX 262144
    static float dF9[BRF_MAX], tmp9[BRF_MAX]; static unsigned char cF9[BRF_MAX]; int nF9=0;   /* the family's distances, for their 90th percentile */
    for(int c=0;c<NCANON;c++){ acc[c]=0.f; cnt[c]=0; cntF[c]=0; }
    for(unsigned m=0;m<P->nMeshes;m++){
        PMesh*M=&P->meshes[m];
        /* ⛔ SKIN UNDER CLOTHES IS STILL THE BODY (10-18). An undrawn mesh was skipped, and with a wardrobe the
         * skin under a worn garment IS undrawn -- one body's undersuit hid her thighs, so they measured NOTHING and
         * the robe had no thigh capsule to wrap over (37 of 46 bones; the shins from 14 ankle vertices). Hidden
         * SKIN (meshKind 1) is measured; hidden anything else is not. */
        if(!(M->flags&4)||!M->boneCount||M->boneCount>MAXB) continue;
        if(MOFF(P,m)&&!(P->meshKind&&P->meshKind[m]==1)) continue;
        if(meshIsGarment(P,m)) continue;   /* do not measure the clothes (~cloth, and the wardrobe's ~worn) */
        unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
        float*rp=(float*)(P->pk+M->offPos);
        unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
        unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
        unsigned step=(M->verts>4000)?(M->verts/4000):1; if(!step) step=1;
        for(unsigned v=0;v<M->verts;v+=step){
            int best=-1, bw=-1;
            for(int q=0;q<4;q++){ int w=wp[v*4+q]; if(w>bw){ bw=w; best=jp[v*4+q]; } }
            if(best<0||best>=(int)M->boneCount||bw<=0) continue;
            unsigned nd=bm[best]; if(nd>=P->nNodes) continue;
            int c=c_of[nd];
            /* ⭐ (11-06) THE HAIR'S CAPSULES (boneRF): what a canonical bone carries through its helpers too (a twist bone under the upper arm
             * carries most of the arm: REVA's 3,465 of its 4,671 vertices, and an upper arm measured from its own bone alone was a centimetre
             * thinner than her arm), and out to the 90th percentile of that flesh, not its mean: hair falls ONTO a limb's outer surface, and a
             * shoulder or an upper arm isn't round (her shoulder's skin is 4.1-4.6 cm over its bone and 5.5-8.5 behind it, her upper arm's 2.8-5.6
             * over and 6-7 behind, where their round means were 5.5 and 4.1-4.4). Her hair hung into the top of her shoulders and the outside of
             * her raised arms until it was. REVA_HAIRCAPPCT picks another percentile. Counted toward the nearest canonical bone above it; boneR,
             * the cloth's, is as it was */
            int cf=c; unsigned nf=nd;
            for(int a9=P->nodes[nd].parent;cf<0&&a9>=0&&(unsigned)a9<P->nNodes;a9=P->nodes[a9].parent){ cf=c_of[a9]; nf=(unsigned)a9; }
            if(cf<0) continue;
            nd=nf;
            /* ⛔ A BONE IS A LINE, AND MEASURING FROM ITS ORIGIN RETURNS THE LIMB'S LENGTH.
             * That gave LeftForeArm r 24.1 and LeftUpLeg r 30.0 on a body 140 units tall
             * whose leg is actually 6.7 across -- balloons, shoving the garment out. The
             * distance is to the bone's SEGMENT: this bone to its first child, which is
             * what seg_dist() in makepajamas.py has always done. Same mistake, twice. */
            float px=rp[v*3], py=rp[v*3+1], pz=rp[v*3+2];
            float ax=W[nd][12], ay=W[nd][13], az=W[nd][14];
            float bx=ax, by=ay, bz=az;
            if(kid[nd]>=0){ bx=W[kid[nd]][12]; by=W[kid[nd]][13]; bz=W[kid[nd]][14]; }
            float ex=bx-ax, ey=by-ay, ez=bz-az;
            float el=ex*ex+ey*ey+ez*ez;
            float t=0.f;
            if(el>1e-9f){ t=((px-ax)*ex+(py-ay)*ey+(pz-az)*ez)/el;
                          if(t<0.f) t=0.f;
                          if(t>1.f) t=1.f; }
            float dx=px-(ax+ex*t), dy=py-(ay+ey*t), dz=pz-(az+ez*t);
            const float d9=sqrtf(dx*dx+dy*dy+dz*dz);
            if(c>=0){ acc[c]+=d9; cnt[c]++; }
            cntF[cf]++; if(nF9<BRF_MAX){ dF9[nF9]=d9; cF9[nF9]=(unsigned char)cf; nF9++; } } }
    int got=0;
    /* the mean of a shell's radii sits a little inside the shell; 1.15 puts the capsule
     * on the skin rather than under it, and the garment's own clearance does the rest. */
    for(int c=0;c<NCANON;c++) if(cnt[c]>8){ P->boneR[c]=acc[c]/(float)cnt[c]*1.15f; got++; }
    for(int c=0;c<NCANON;c++){ P->boneRF[c]=P->boneR[c]; if(cntF[c]<=8) continue;
        int n9=0; for(int k=0;k<nF9;k++) if(cF9[k]==(unsigned char)c) tmp9[n9++]=dF9[k];
        if(n9>8){ qsort(tmp9,(size_t)n9,sizeof(float),cmpf9); P->boneRF[c]=tmp9[n9*g_brfPct/100]; } }
    #undef BRF_MAX
    printf("[cloth] body measured: %d of %d canonical bones have a radius%c",got,NCANON,10);
    for(int c=0;c<NCANON;c++) if(P->boneR[c]>0.f)
        printf("        canon %2d node %3d %-16s r %.3f  (%d verts)%s%c",
               c,P->pb[c],P->pb[c]>=0?P->nodes[P->pb[c]].name?packStr(P,P->nodes[P->pb[c]].name):"?":"?",
               P->boneR[c],cnt[c],fabsf(P->boneRF[c]-P->boneR[c])>1e-4f?({ static char s9[96]; snprintf(s9,sizeof s9,"; for hair, with its helpers, the %dth percentile r %.3f (%d verts)",g_brfPct,P->boneRF[c],cntF[c]); s9; }):"",10);
    fflush(stdout); }

/* ======================== THE HANGING CLOTH (10-18) ==================================
 * On one body's run, with the skirt pushed out of the legs in the vertex shader, the lab coat didn't behave as a physical object.
 * It was not one. The skirt was SKINNED to a ring of swinging chains
 * and each vertex was then shoved sideways out of the leg capsules -- so a knee coming up under the coat left a
 * flat flap at the hem, the cloth below a raised knee stretched to its own hem, and a heel kicked up behind fanned
 * the whole back of the coat out to the heel. Cloth can do none of those things.
 * ⭐ A SKIRT IS NOW CLOTH. Every `~skirt~` mesh on a VertexCollide pack is SIMULATED, in world space:
 *   - one particle per point of the OUTER wall. The lining is not simulated: it is drawn a wall's thickness behind
 *     its outer point, along the outer surface's normal, so the two walls can never cross or part;
 *   - every point with no weight on a Cloth_* chain is the WAISTBAND, and rides the skinned pose (the hips);
 *   - the mesh's own edges do not stretch; the pair across every edge resists bending (XPBD, a fabric constant);
 *     a TETHER holds each free point within its rest distance (along the cloth) of its nearest waistband point,
 *     so a hem can be LIFTED by a knee but never dragged below its own length;
 *   - gravity in this world's units (9.81 m/s^2 times the body's scale) and quadratic AIR DRAG across the cloth's
 *     face -- a sheet moving flat-on meets the air, edge-on it does not -- so it lags and settles like fabric. The
 *     drag is on the cloth's speed THROUGH THE AIR, and on the spot the air of her own run comes past her: the
 *     planted foot says how fast the ground goes under her (on a path it stands still, so a body really travelling
 *     gets no double);
 *   - THE HIPS HOLD IT OUT: above where the thighs begin, no point comes nearer the hips' axis (in the hips' own
 *     frame) than it was cut -- the cloth was cut to hang from the pelvis's widest point, so that cut IS the pelvis;
 *   - THE HANDS press it IN toward the body (an arm is outside the skirt): her hand's own skin, sampled, against the
 *     cloth's points AND its triangles (a finger is thinner than the gap between two points), down to the skin under
 *     a pelvis point and no further;
 *   - THE SKIN UNDER IT, the back half (buttock, backs of the thighs, calves) against the cloth cut on the back half:
 *     off the nearest skin point's tangent plane by the undersuit and the wall -- the hidden skin included;
 *   - out of capsules on the legs and the feet (heel to toe) at their MEASURED thickness -- plus a garment's ease
 *     where a garment covers the leg in this outfit, a hair where it is bare -- placed where they are at each
 *     INSTANT of the frame (16 substeps), inelastic and with friction, so the knee that lifts the cloth carries it
 *     and never throws it.
 * A new coat is hung on the hips in its cut shape and the body is swung into its pose from rest over half a second
 * (a PRE-ROLL), so a knee that starts the take raised arrives under the cloth instead of starting through it.
 * Drawn from its own buffers (two coats on two bodies are two coats), unskinned, and it casts. The chains still
 * swing (Godot reads them); nothing here hangs on them any more.
 *   REVA_SKIRTSIM=0 (or REVA_NOCLOTH=1): the old drape in the shader -- the control.
 *   REVA_SKIRTDBG=1: the build report, then once a second contacts / deepest push / worst stretch and where, the
 *   air (=2: every deep push off the skin, with the skin point that made it; =3: the planted-foot air, frame by frame).
 *   REVA_SKBEND=<compliance> (default 1e-7: twill in broad panels; 2e-5 crumples like a shirt), REVA_SKSUB=<s>
 *   (substep length, default 1/960), REVA_SKDRAG (default 1.3: double-walled twill), REVA_SKAIR=0, REVA_SKHANDS=0,
 *   REVA_SKLEGM=<m> / REVA_SKLEGR=<x> (the leg margin / radius factor, default by outfit / 1.0), REVA_SKARMS=1|2
 *   (the old arm capsules -- at the waist they crumple the coat), REVA_SKSKIN=0 / REVA_SKCONE=0 /
 *   REVA_SKMU=<static>[,<kinetic>] for the A/B, REVA_VCOLR=<x> scales the capsules (x4: the coat balloons -- the test
 *   that the collision runs at all). */
/* (11-16) twice what they were: REVA's coat's skirt is 3 cm columns and 1.3 cm rows round 280 degrees of her, its lining and its rims, 6,000-odd
 * points (the caps were 4,096 and 8,192 vertices, and a coat over them is refused, said once) */
#define SK_MAXV 16384
#define SK_MAXP 8192
#define SK_MAXE 32768
#define SK_POOL 24
#define SK_NCAP 16
#define SK_BODYMAX 6000
#define SK_HANDMAX 512
#define SK_MAXT 16384
#define SKS_NEAR 8                                /* (11-16) set pieces near one cloth at once (THE SET UNDER IT) */
#define SKC 9                                     /* (11-16) a capsule: A(3) B(3) radius at A, kind, radius at B (a leg tapers) */
typedef struct {
    Pack*pk; int mesh, slot, seeded, warm; unsigned stamp, shStamp, used;
    int nv, np, ne, nb, ncap0, ncone;
    short vp[SK_MAXV];                            /* mesh vertex -> point */
    short rep[SK_MAXP];                           /* point -> a mesh vertex sitting on it */
    short twin[SK_MAXP]; float thick[SK_MAXP];    /* a lining point: its outer point, and how far behind (rig u) */
    unsigned char pin[SK_MAXP], cone[SK_MAXP], back[SK_MAXP];   /* back: cut on the back half of the garment */
    short anchor[SK_MAXP]; float tlen[SK_MAXP];   /* the tether: nearest waistband point, rest distance (rig u) */
    float rr[SK_MAXP], pr[SK_MAXP][3];            /* rest: radius from the hips' axis, and the point itself (rig) */
    float allowC[SK_MAXP], allowS[SK_MAXP];       /* how far INSIDE a limb's / the skin's margin it already sits at rest */
    float cur[SK_MAXP][3], prv[SK_MAXP][3], tg0[SK_MAXP][3], tg1[SK_MAXP][3];
    float nOut[SK_MAXP][3], nR[SK_MAXP][3], pos[SK_MAXP][3];
    unsigned short e0[SK_MAXE], e1[SK_MAXE]; float elen[SK_MAXE];
    unsigned short b0[SK_MAXE], b1[SK_MAXE]; float blen[SK_MAXE];
    int nt; unsigned short tri[SK_MAXT][3];       /* the outer wall's triangles (simulated points) */
    float osign, rsign, tmed, ax, ay, az, legM;    /* legM: off a leg, rig units (a garment's ease or a hair, + the wall) */
    float h0inv[16], H0[16];                      /* the hips at rest (inverse); last frame's rest->world */
    int band; float bandY;                        /* hung by its top tenth, with no chain weights: and the height its band ends at, at rest (rig u) */
    unsigned char tdup[SK_MAXT];                  /* a triangle of the mesh that says again one already said, the other way round: a wall seen from both sides */
    float cap0[SK_NCAP][SKC];
    /* THE SKIN UNDER IT, sampled: which mesh and vertex, where it was last frame and is now, which way it faces */
    int nbv; unsigned char bvm[SK_BODYMAX]; unsigned short bvv[SK_BODYMAX];
    float bp0[SK_BODYMAX][3], bp1[SK_BODYMAX][3], bn1[SK_BODYMAX][3];
    /* THE HANDS, sampled from their skin; and under a pelvis point, the body's own radius (+ clearance) */
    int nhv; unsigned char hvm[SK_HANDMAX]; unsigned short hvv[SK_HANDMAX];
    float hp0[SK_HANDMAX][3], hp1[SK_HANDMAX][3];
    float floorR[SK_MAXP];
    /* ⭐ THE FLOOR UNDER THE BODY (10-18): the plane the body stands or lies on (its model's ground: the origin
     * plus the rest sole) -- a child lying in a warming pan hangs his shift toward the pan, and it stops there */
    float floorY;
    /* THE AIR OF HER OWN RUN: what the planted foot says the ground is doing under her (world u/s) */
    float air[3], fprev[2][3], fground; int fwarm;
    int worstE;                                   /* the most stretched edge, last step (the report) */
    unsigned vbo[2];
    /* (11-16) a cloth hung by its Hang_ nodes (load.inc), and THE SET UNDER IT: the solid pieces near it this frame, each its g_sks entry, its
     * instance, where it was last frame and is now (and their inverses), its scale; and what was said about them */
    int hang; float fwdR[2];                      /* fwdR: her front at rest, heel to toe (rig x, z) */
    float d0prev[3]; int haveD0;                  /* (11-16) how far her hips went in the last substep: the carry, felt only in part */
    int nsn; struct { int ks, inst, still; float M0[16], M1[16], Mi0[16], Mi1[16], k; } sn[SKS_NEAR];
    int setSaid[SKS_NEAR*4], nSetSaid;
    int setContacts, setDeep; float setPen;       /* this frame: contacts with the set, points left deep inside a piece (squeezed there by her), the deepest push */
} Skirt;
static Skirt g_sk[SK_POOL];
static void skBodyFrame(Skirt*S,Pack*P,M4*world,const float*model,int first);
static void skHandFrame(Skirt*S,Pack*P,M4*world,const float*model,int first);
static int g_skirtSim=-1, g_skDbg=0, g_skArms=0, g_skSkin=1, g_skCone=1, g_skHands=1, g_skAir=1; static float g_skBend=1e-7f, g_skMuS=0.25f, g_skMuK=0.15f, g_skDrag=1.3f, g_skSub=0.00104f, g_skLegM=-1.f, g_skLegR=1.0f;
static int g_skContacts=0; static float g_skDeep=0.f, g_skStretch=0.f;

static void skClosestTri(const float*p,const float*a,const float*b,const float*c,float*u,float*v,float*w){
    float ab[3],ac[3],ap[3]; for(int k=0;k<3;k++){ ab[k]=b[k]-a[k]; ac[k]=c[k]-a[k]; ap[k]=p[k]-a[k]; }
    float d1=ab[0]*ap[0]+ab[1]*ap[1]+ab[2]*ap[2], d2=ac[0]*ap[0]+ac[1]*ap[1]+ac[2]*ap[2];
    if(d1<=0.f&&d2<=0.f){ *u=1.f; *v=0.f; *w=0.f; return; }
    float bp[3]; for(int k=0;k<3;k++) bp[k]=p[k]-b[k];
    float d3=ab[0]*bp[0]+ab[1]*bp[1]+ab[2]*bp[2], d4=ac[0]*bp[0]+ac[1]*bp[1]+ac[2]*bp[2];
    if(d3>=0.f&&d4<=d3){ *u=0.f; *v=1.f; *w=0.f; return; }
    float vc=d1*d4-d3*d2;
    if(vc<=0.f&&d1>=0.f&&d3<=0.f){ float t=d1/(d1-d3); *u=1.f-t; *v=t; *w=0.f; return; }
    float cp[3]; for(int k=0;k<3;k++) cp[k]=p[k]-c[k];
    float d5=ab[0]*cp[0]+ab[1]*cp[1]+ab[2]*cp[2], d6=ac[0]*cp[0]+ac[1]*cp[1]+ac[2]*cp[2];
    if(d6>=0.f&&d5<=d6){ *u=0.f; *v=0.f; *w=1.f; return; }
    float vb=d5*d2-d1*d6;
    if(vb<=0.f&&d2>=0.f&&d6<=0.f){ float t=d2/(d2-d6); *u=1.f-t; *v=0.f; *w=t; return; }
    float va=d3*d6-d5*d4;
    if(va<=0.f&&(d4-d3)>=0.f&&(d5-d6)>=0.f){ float t=(d4-d3)/((d4-d3)+(d5-d6)); *u=0.f; *v=1.f-t; *w=t; return; }
    float den=1.f/(va+vb+vc); *v=vb*den; *w=vc*den; *u=1.f-*v-*w; }
static void skXf(const float*m,const float*p,float*o){
    for(int k=0;k<3;k++) o[k]=m[k]*p[0]+m[4+k]*p[1]+m[8+k]*p[2]+m[12+k]; }
/* an affine 4x4 (column-major) inverse; 0 if singular */
static int skInv(const float*m,float*o){
    float a=m[0],b=m[4],c=m[8], d=m[1],e=m[5],f=m[9], g=m[2],h=m[6],i=m[10];
    float A=e*i-f*h, B=f*g-d*i, C=d*h-e*g, det=a*A+b*B+c*C;
    if(fabsf(det)<1e-20f) return 0;
    float id=1.f/det;
    float r[3][3]={{A*id,(c*h-b*i)*id,(b*f-c*e)*id},{B*id,(a*i-c*g)*id,(c*d-a*f)*id},{C*id,(b*g-a*h)*id,(a*e-b*d)*id}};
    for(int rr=0;rr<3;rr++) for(int cc=0;cc<3;cc++) o[cc*4+rr]=r[rr][cc];
    o[3]=o[7]=o[11]=0.f; o[15]=1.f;
    for(int rr=0;rr<3;rr++) o[12+rr]=-(r[rr][0]*m[12]+r[rr][1]*m[13]+r[rr][2]*m[14]);
    return 1; }
static int skFirstChild(Pack*P,int node,const char*want){
    if(node<0) return -1;
    int any=-1;
    for(unsigned i=0;i<P->nNodes;i++){ if(P->nodes[i].parent!=node) continue;
        const char*nm=packStr(P,P->nodes[i].name); char lo[96]; int k=0;
        for(;nm&&k<95&&nm[k];k++){ char c=nm[k]; lo[k]=(c>='A'&&c<='Z')?(char)(c+32):c; } lo[k]=0;
        if(want&&strstr(lo,want)) return (int)i;
        if(any<0) any=(int)i; }
    return any; }
static void skRestFK(Pack*P,M4*W){
    for(unsigned i=0;i<P->nNodes;i++){ M4 loc; m4_trs(loc,P->nodes[i].t,P->nodes[i].q,P->nodes[i].s);
        if(P->nodes[i].parent<0) memcpy(W[i],loc,64); else m4_mul(W[i],W[P->nodes[i].parent],loc); } }

/* ⭐ THE LEG AS IT TAPERS (11-16, REVA's lab coat: its back opened and lifted while she barely moved). A leg's capsule was ONE radius
 * the whole length of the thigh, the mean of all its flesh: hers 10.7 cm, a thigh at her seat and 4 cm fatter than it is at her coat's hem and the
 * knee, so a coat that hangs a hair off her lower thigh was "inside" it wherever her weight shifted. Each thigh and shin is measured near each end
 * (its own and its helpers' skin a quarter of the way along and three quarters, the mean distance from the bone's line times 1.15, boneR's own
 * terms), and its capsule tapers from the one to the other. REVA_SKTAPER=0 is the old one radius */
static struct { const Pack*pk; const unsigned char*b; float r[4][2]; } g_skTap[16];
static int g_skTaperOn=-1;
static int skBodyMesh(Pack*P,unsigned m);   /* below */
static const float* skTaper(Pack*P,int leg){   /* leg: 0 thigh l, 1 shin l, 2 thigh r, 3 shin r; its radius near the top and near the bottom (rig u) */
    for(int i=0;i<16;i++) if(g_skTap[i].pk==P&&g_skTap[i].b==P->pk) return g_skTap[i].r[leg];
    int pick=0; for(int i=0;i<16;i++) if(!g_skTap[i].pk){ pick=i; break; }
    if(g_skTap[pick].pk){ static int rr9=0; pick=(rr9++)%16; }
    g_skTap[pick].pk=P; g_skTap[pick].b=P->pk;
    if(!P->boneRok) measureBoneR(P);
    static M4 RW[1024]; if(P->nNodes>1024) return g_skTap[pick].r[leg];
    skRestFK(P,RW);
    for(int L=0;L<4;L++){ const int s=L/2, shin=L&1, c=shin?(s?15:13):(s?14:12);
        const int A=P->pb[c], K=shin?-1:P->pb[c+1];
        int B=K; if(shin){ int ft=s?P->footR:P->footL, an=ft; while(an>=0&&P->nodes[an].parent>=0&&P->nodes[an].parent!=A) an=P->nodes[an].parent; B=(an>=0&&P->nodes[an].parent==A)?an:ft; }
        float r0=P->boneR[c], r1=P->boneR[c]; g_skTap[pick].r[L][0]=r0; g_skTap[pick].r[L][1]=r1;
        if(A<0||B<0||(unsigned)A>=P->nNodes||(unsigned)B>=P->nNodes) continue;
        const float*a=&RW[A][12], *b=&RW[B][12]; const float e[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]}, el=e[0]*e[0]+e[1]*e[1]+e[2]*e[2]; if(el<1e-10f) continue;
        /* ⛔ (11-16: her right thigh's skin showed through the coat in every frame) NOT THE MEAN: a thigh is wider at its side than
         * through, and her machine leg's core is inside its plates. The OUTER SURFACE: in each band along the bone, the farthest of her skin and
         * of the plates over it (`~worn`, never a `wear~` garment) in each of 24 directions round the bone, and the 90th percentile of those */
        float sec[2][24]; int cnt[2]={0,0}; for(int k=0;k<2;k++) for(int a9=0;a9<24;a9++) sec[k][a9]=0.f;
        float ua[3], ub[3]; { const float il=1.f/sqrtf(el), ax9[3]={e[0]*il,e[1]*il,e[2]*il}; float rf[3]={0.f,0.f,1.f}; if(fabsf(ax9[2])>0.9f){ rf[0]=1.f; rf[2]=0.f; }
            ua[0]=ax9[1]*rf[2]-ax9[2]*rf[1]; ua[1]=ax9[2]*rf[0]-ax9[0]*rf[2]; ua[2]=ax9[0]*rf[1]-ax9[1]*rf[0];
            const float ul=sqrtf(ua[0]*ua[0]+ua[1]*ua[1]+ua[2]*ua[2])+1e-12f; ua[0]/=ul; ua[1]/=ul; ua[2]/=ul;
            ub[0]=ax9[1]*ua[2]-ax9[2]*ua[1]; ub[1]=ax9[2]*ua[0]-ax9[0]*ua[2]; ub[2]=ax9[0]*ua[1]-ax9[1]*ua[0]; }
        for(unsigned m=0;m<P->nMeshes;m++){ PMesh*Mb=&P->meshes[m]; if(!(Mb->flags&4)||!Mb->boneCount||Mb->boneCount>MAXB) continue;
            { const char*nm9=packStr(P,Mb->name); const int plate=nm9&&strstr(nm9,"~worn")&&strncmp(nm9,"wear~",5)&&!MOFF(P,m);
              if(!plate&&!skBodyMesh(P,m)) continue; }
            const float*rp=(const float*)(P->pk+Mb->offPos), *bd=(const float*)(P->pk+Mb->offBind);
            const unsigned short*bm=(const unsigned short*)(P->pk+Mb->offBoneMap); const unsigned char*jp=(const unsigned char*)(P->pk+Mb->offJoints), *wp=(const unsigned char*)(P->pk+Mb->offWeights);
            for(unsigned v=0;v<Mb->verts;v++){ int bj=-1, bw=-1; for(int q=0;q<4;q++) if(wp[v*4+q]>bw){ bw=wp[v*4+q]; bj=jp[v*4+q]; }
                if(bj<0||bj>=(int)Mb->boneCount) continue;
                int nd=bm[bj], own=0; for(int h9=0;nd>=0&&(unsigned)nd<P->nNodes&&h9<8;h9++){ if(nd==A){ own=1; break; } if(nd==B||(K>=0&&nd==K)||nd==P->pb[0]) break; nd=P->nodes[nd].parent; }
                if(!own) continue;
                float p[3]; for(int k=0;k<3;k++) p[k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k];
                const float t=((p[0]-a[0])*e[0]+(p[1]-a[1])*e[1]+(p[2]-a[2])*e[2])/el;
                const int bin=(t>0.15f&&t<0.35f)?0:(t>0.65f&&t<0.90f)?1:-1; if(bin<0) continue;
                const float dx=p[0]-(a[0]+e[0]*t), dy=p[1]-(a[1]+e[1]*t), dz=p[2]-(a[2]+e[2]*t);
                int a9=(int)floorf((atan2f(dx*ub[0]+dy*ub[1]+dz*ub[2],dx*ua[0]+dy*ua[1]+dz*ua[2])+3.14159265f)/6.2831853f*24.f); if(a9<0) a9=0; if(a9>23) a9=23;
                const float d9=sqrtf(dx*dx+dy*dy+dz*dz); if(d9>sec[bin][a9]) sec[bin][a9]=d9;
                cnt[bin]++; } }
        for(int k=0;k<2;k++) if(cnt[k]>=24){ float s9[24]; int n9=0; for(int a9=0;a9<24;a9++) if(sec[k][a9]>0.f) s9[n9++]=sec[k][a9];
            if(n9>=8){ qsort(s9,(size_t)n9,sizeof(float),cmpf9); g_skTap[pick].r[L][k]=s9[(n9*9)/10>n9-1?n9-1:(n9*9)/10]; } }
        printf("[skirt] %s: %s %s measured %.3f near its top and %.3f near its bottom (it was %.3f the whole way)\n",P->label,s?"right":"left",shin?"shin":"thigh",
               g_skTap[pick].r[L][0],g_skTap[pick].r[L][1],P->boneR[c]); }
    fflush(stdout);
    return g_skTap[pick].r[leg]; }

/* THE BODY, as capsules in WORLD space: A(3) B(3) radius-with-margin at A, kind, radius-with-margin at B (11-16: a leg tapers). Same legs as the
 * drape (colSet): the thigh from a fifth of the way down, every radius the body's own measured thickness. Margins in rig metres: off a trouser leg
 * and the wall's own thickness; off a sleeve as well. */
static int skCaps(Pack*P,M4*W,const float*model,float sc,float tmed,float legM,float cap[][SKC]){
    if(!P->boneRok) measureBoneR(P);
    static float vr=-1.f; if(vr<0.f){ const char*e=getenv("REVA_VCOLR"); vr=(e&&*e)?(float)atof(e):1.f; }
    if(g_skTaperOn<0){ const char*e=getenv("REVA_SKTAPER"); g_skTaperOn=!(e&&e[0]=='0'); }
    float mL=legM*sc, mA=(0.022f+tmed)*sc;
    int n=0;
    for(int s=0;s<2;s++){
        int hip=P->pb[s?14:12], knee=P->pb[s?15:13], ft=s?P->footR:P->footL;
        /* ⛔ footL/footR is the LOWEST bone under the knee -- on these rigs the TOE, not the ankle: a shin drawn to it
         * cut diagonally through the foot, and asking the toe for its child found nothing, so the feet had no capsule
         * at all. The ankle is the knee's own child on the way down to it. */
        int ank=ft; while(ank>=0&&knee>=0&&P->nodes[ank].parent>=0&&P->nodes[ank].parent!=knee) ank=P->nodes[ank].parent;
        if(ank<0||knee<0||P->nodes[ank].parent!=knee) ank=ft;
        int toe=(ank!=ft)?ft:skFirstChild(P,ank,"toe");
        int sh=P->pb[s?10:7], el=P->pb[s?11:8];
        int hand=skFirstChild(P,el,"hand");
        float rT=P->boneR[s?14:12], rS=P->boneR[s?15:13], rU=P->boneR[s?10:7], rF=P->boneR[s?11:8];
        struct { int a,b; float k0,ext,r,m; } L[6]={
            {hip,knee,0.2f,0.f,rT*g_skLegR,mL},   /* the thigh */
            {knee,ank,0.f,0.f,rS*g_skLegR,mL},    /* the shin */
            /* the foot IN ITS SHOE, heel to toe-tip: a heel kicked up behind is the first thing through a coat's back */
            {toe,ank,-0.4f,0.35f,rS*0.85f,mL},
            {sh,el,0.f,0.f,rU*1.06f,mA},         /* the upper arm, in its sleeve */
            {el,hand,0.f,0.f,rF*1.06f,mA},       /* the forearm */
            {el,hand,1.f,0.45f,rF*0.95f,mL} };   /* the hand: on past the wrist by half a forearm */
        for(int q=0;q<6&&n<SK_NCAP;q++){ int a=L[q].a, b=L[q].b;
            if(q>=3&&!g_skArms) continue;
            if((q==3||q==4)&&g_skArms<2) continue;   /* REVA_SKARMS=1: the hands alone; 2: the whole arm */
            if(a<0||b<0||(unsigned)a>=P->nNodes||(unsigned)b>=P->nNodes||L[q].r<=0.f) continue;
            float pa[3],pb9[3];
            for(int k=0;k<3;k++){ float A0=W[a][12+k], B0=W[b][12+k]; pa[k]=A0+(B0-A0)*L[q].k0; pb9[k]=B0+(B0-A0)*L[q].ext; }
            skXf(model,pa,cap[n]); skXf(model,pb9,cap[n]+3);
            cap[n][6]=L[q].r*vr*sc+L[q].m; cap[n][7]=(float)(s*6+q); cap[n][8]=cap[n][6];
            /* (11-16) the thigh and the shin taper (skTaper): their radius where the capsule begins and where it ends, from the flesh near each end */
            if(g_skTaperOn&&q<2){ const float*tp=skTaper(P,s*2+q);
                const float t0=(q==0)?0.2f:0.f, rA=tp[0]+(tp[1]-tp[0])*((t0-0.25f)/0.525f), rB=tp[0]+(tp[1]-tp[0])*((1.f-0.25f)/0.525f);   /* the two bins' middles: 0.25 and 0.775 */
                cap[n][6]=fmaxf(rA,0.3f*tp[1])*g_skLegR*vr*sc+L[q].m; cap[n][8]=fmaxf(rB,0.3f*tp[1])*g_skLegR*vr*sc+L[q].m; }
            n++; } }
    return n; }
/* (11-16) a capsule's radius at t (0 at A, 1 at B): a leg tapers */
static inline float skCapR(const float*cap,float t){ return cap[6]+(cap[8]-cap[6])*t; }

/* the outer wall's normals (area-weighted, its own triangles), the lining placed behind them, then the drawn
 * normals over every triangle, weighted by corner ANGLE as Blender weighs them (so a hem's lip still turns the
 * outline under the hem) */
static void skNormals(Skirt*S,const unsigned*ix,unsigned nidx,float sc){
    int np=S->np;
    for(int p=0;p<np;p++){ S->nOut[p][0]=S->nOut[p][1]=S->nOut[p][2]=0.f; S->nR[p][0]=S->nR[p][1]=S->nR[p][2]=0.f; }
    for(unsigned t=0;t+2<nidx;t+=3){
        int a=S->vp[ix[t]], b=S->vp[ix[t+1]], c=S->vp[ix[t+2]];
        if(t/3<SK_MAXT&&S->tdup[t/3]) continue;
        if(S->twin[a]>=0||S->twin[b]>=0||S->twin[c]>=0) continue;
        float u[3],v[3]; for(int k=0;k<3;k++){ u[k]=S->cur[b][k]-S->cur[a][k]; v[k]=S->cur[c][k]-S->cur[a][k]; }
        float n[3]={(u[1]*v[2]-u[2]*v[1])*S->osign,(u[2]*v[0]-u[0]*v[2])*S->osign,(u[0]*v[1]-u[1]*v[0])*S->osign};
        for(int k=0;k<3;k++){ S->nOut[a][k]+=n[k]; S->nOut[b][k]+=n[k]; S->nOut[c][k]+=n[k]; } }
    for(int p=0;p<np;p++){ float*n=S->nOut[p]; float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(l>1e-14f){ n[0]/=l; n[1]/=l; n[2]/=l; } else { n[0]=0.f; n[1]=1.f; n[2]=0.f; } }
    for(int p=0;p<np;p++){ int w=S->twin[p];
        if(w<0) memcpy(S->pos[p],S->cur[p],12);
        else for(int k=0;k<3;k++) S->pos[p][k]=S->cur[w][k]-S->nOut[w][k]*S->thick[p]*sc; }
    for(unsigned t=0;t+2<nidx;t+=3){
        int id[3]={S->vp[ix[t]],S->vp[ix[t+1]],S->vp[ix[t+2]]};
        if(t/3<SK_MAXT&&S->tdup[t/3]) continue;
        float u[3],v[3]; for(int k=0;k<3;k++){ u[k]=S->pos[id[1]][k]-S->pos[id[0]][k]; v[k]=S->pos[id[2]][k]-S->pos[id[0]][k]; }
        float n[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(l<1e-14f) continue;
        for(int k=0;k<3;k++) n[k]*=S->rsign/l;
        for(int c=0;c<3;c++){ const float*P0=S->pos[id[c]], *P1=S->pos[id[(c+1)%3]], *P2=S->pos[id[(c+2)%3]];
            float x[3],y[3]; for(int k=0;k<3;k++){ x[k]=P1[k]-P0[k]; y[k]=P2[k]-P0[k]; }
            float lx=sqrtf(x[0]*x[0]+x[1]*x[1]+x[2]*x[2]), ly=sqrtf(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]);
            if(lx<1e-12f||ly<1e-12f) continue;
            float cs=(x[0]*y[0]+x[1]*y[1]+x[2]*y[2])/(lx*ly); if(cs>1.f) cs=1.f; if(cs<-1.f) cs=-1.f;
            float an=acosf(cs);
            for(int k=0;k<3;k++) S->nR[id[c]][k]+=n[k]*an; } }
    for(int p=0;p<np;p++){ float*n=S->nR[p]; float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(l>1e-14f){ n[0]/=l; n[1]/=l; n[2]/=l; } else memcpy(n,S->nOut[S->twin[p]>=0?S->twin[p]:p],12); } }

/* ⭐ (11-16) WHAT IS HER BODY, to the cloth: every mesh that isn't a garment, drawn or hidden skin -- what measureBoneR measures. The layers below
 * took meshes NAMED skin*, which the show's people are made of; REVA's skin is reva_torso, reva_legs, reva_arms (her hidden skin~h= pieces were all
 * they found), so her hands never pressed her coat and the backs of her thighs were half there */
static int skBodyMesh(Pack*P,unsigned m){
    const char*nm=packStr(P,P->meshes[m].name);
    if(!nm) return 0;
    if(!strncmp(nm,"skin",4)) return 1;
    if(meshIsGarment(P,m)) return 0;
    if(MOFF(P,m)&&!(P->meshKind&&P->meshKind[m]==1)) return 0;
    return (int)m!=P->fmInterior; }

/* Build it once per body: weld the mesh to points, find the lining, the waistband, the edges, bends and tethers. */
static int skirtInit(Skirt*S,Pack*P,unsigned mi){
    PMesh*M=&P->meshes[mi];
    char mnm[96]; { const char*n9=packStr(P,M->name); snprintf(mnm,sizeof mnm,"%s",n9?n9:"?"); }   /* packStr's buffer is reused */
    S->pk=P; S->mesh=(int)mi; S->seeded=-1; S->warm=0;
    if(M->verts>SK_MAXV||!M->idxCount||!(M->flags&1)||M->boneCount>MAXB||P->nNodes>1024){
        printf("[skirt] !! %s '%s': %u verts (cap %d), %u indices, normals %d -- NOT simulated\n",P->label,mnm,M->verts,SK_MAXV,
               M->idxCount,M->flags&1); fflush(stdout); return 0; }
    int nv=(int)M->verts; S->nv=nv;
    float*rp=(float*)(P->pk+M->offPos), *rn=(float*)(P->pk+M->offNorm), *bd=(float*)(P->pk+M->offBind);
    unsigned char*jp=(unsigned char*)(P->pk+M->offJoints), *wp=(unsigned char*)(P->pk+M->offWeights);
    unsigned short*bm=(unsigned short*)(P->pk+M->offBoneMap);
    unsigned*ix=(unsigned*)(P->pk+M->offIdx);
    static float bq[SK_MAXV][3], bn[SK_MAXV][3], cw[SK_MAXV];
    static unsigned char chainB[MAXB];
    /* the cloth is what rides a Cloth_ chain, or (11-16) a Hang_ node: a coat whose free part the tailor left on her thighs, so that skinned it is the
     * coat it always was (load.inc) */
    S->hang=(P->meshHang&&P->meshHang[mi])?1:0;
    for(unsigned b=0;b<M->boneCount;b++){ unsigned nd=bm[b]; const char*nm=(nd<P->nNodes)?packStr(P,P->nodes[nd].name):NULL;
        chainB[b]=(nm&&(!strncmp(nm,"Cloth_",6)||!strncmp(nm,"Hang_",5)))?1:0; }
    float lo[3]={1e9f,1e9f,1e9f}, hi[3]={-1e9f,-1e9f,-1e9f};
    for(int v=0;v<nv;v++){
        for(int k=0;k<3;k++){ bq[v][k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k];
                              bn[v][k]=bd[k]*rn[v*3]+bd[4+k]*rn[v*3+1]+bd[8+k]*rn[v*3+2];
                              if(bq[v][k]<lo[k]) lo[k]=bq[v][k];
                              if(bq[v][k]>hi[k]) hi[k]=bq[v][k]; }
        float c=0.f; for(int q=0;q<4;q++){ int j=jp[v*4+q]; if(j<(int)M->boneCount&&chainB[j]) c+=wp[v*4+q]/255.f; }
        cw[v]=c; }
    float diag=sqrtf((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2]));
    float eps2=(diag*2e-5f)*(diag*2e-5f);
    /* weld: a vertex split for its UVs is still one piece of cloth */
    static float pn[SK_MAXP][3], pcw[SK_MAXP];
    int np=0;
    for(int v=0;v<nv;v++){ int f=-1;
        for(int p=np-1;p>=0;p--){ float dx=bq[v][0]-S->pr[p][0], dy=bq[v][1]-S->pr[p][1], dz=bq[v][2]-S->pr[p][2];
            if(dx*dx+dy*dy+dz*dz<=eps2){ f=p; break; } }
        if(f<0){ if(np>=SK_MAXP){ printf("[skirt] !! %s '%s': over %d points -- NOT simulated\n",P->label,mnm,SK_MAXP); fflush(stdout); return 0; }
                 f=np++; memcpy(S->pr[f],bq[v],12); pn[f][0]=pn[f][1]=pn[f][2]=0.f; pcw[f]=0.f; S->rep[f]=(short)v; }
        S->vp[v]=(short)f; for(int k=0;k<3;k++) pn[f][k]+=bn[v][k]; if(cw[v]>pcw[f]) pcw[f]=cw[v]; }
    S->np=np;
    for(unsigned t=0;t<M->idxCount;t++) if(ix[t]>=(unsigned)nv){ printf("[skirt] !! %s '%s': index out of range -- NOT simulated\n",P->label,mnm); return 0; }
    /* the hips at rest: the axis the cloth hangs round, and the frame its cut is held in */
    static M4 RW[1024]; skRestFK(P,RW);
    int hp=P->pb[0];
    static const float I16[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if(hp>=0){ S->ax=RW[hp][12]; S->ay=RW[hp][13]; S->az=RW[hp][14]; if(!skInv(RW[hp],S->h0inv)) memcpy(S->h0inv,I16,64); }
    else { S->ax=S->ay=S->az=0.f; memcpy(S->h0inv,I16,64); }
    /* OUTER or LINING, by which way the mesh's own normals face round the hips; a lining point is the outer point
     * nearest it, a wall's thickness away */
    static unsigned char outer[SK_MAXP]; static float tk[SK_MAXP]; int nOut=0, nIn=0, nSingle=0;
    for(int p=0;p<np;p++){ float rx=S->pr[p][0]-S->ax, rz=S->pr[p][2]-S->az;
        outer[p]=(pn[p][0]*rx+pn[p][2]*rz>0.f)?1:0; nOut+=outer[p]; S->rr[p]=sqrtf(rx*rx+rz*rz); }
    for(int p=0;p<np;p++){ S->twin[p]=-1; S->thick[p]=0.f; if(outer[p]) continue;
        int best=-1; float b2=1e30f;
        for(int q=0;q<np;q++){ if(!outer[q]) continue;
            float dx=S->pr[p][0]-S->pr[q][0], dy=S->pr[p][1]-S->pr[q][1], dz=S->pr[p][2]-S->pr[q][2], d2=dx*dx+dy*dy+dz*dz;
            if(d2<b2){ b2=d2; best=q; } }
        if(best>=0){ S->twin[p]=(short)best; S->thick[p]=sqrtf(b2); tk[nIn++]=S->thick[p]; } }
    S->tmed=0.f; if(nIn){ qsort(tk,(size_t)nIn,sizeof(float),cmpf9); S->tmed=tk[nIn/2]; }
    for(int p=0;p<np;p++) if(S->twin[p]>=0&&S->thick[p]>3.f*S->tmed+1e-6f){ S->twin[p]=-1; nSingle++; nIn--; }   /* not a lining: its own cloth */
    /* THE EDGES among the simulated points (deduped), and across each the pair of opposite points: the bend */
    #define SKH 16
    static int hh[1<<SKH]; static unsigned short opp0[SK_MAXE], opp1[SK_MAXE];
    for(int i=0;i<(1<<SKH);i++) hh[i]=-1;
    /* ⭐ A WALL WOUND BOTH WAYS (11-05). A mesh may say each triangle twice, once each way round, so that one wall is seen from inside too (R.E.V.A.'s
     * pleated skirt: a lining a wall's thickness behind is nothing where pleats are pressed flat). The second saying is the same cloth. Counted again it
     * cancels every normal the cloth has -- which way is out of it, and what it's lit by -- and gives each triangle a twin facing the other way, so
     * whatever pushes off a triangle is pushed both ways at once. It is drawn, and that is all */
    memset(S->tdup,0,sizeof S->tdup); int nDup=0;
    { static int th[1<<15]; static unsigned short tk3[SK_MAXT][3]; for(int i=0;i<(1<<15);i++) th[i]=-1;   /* (11-16) twice SK_MAXT: open addressing must never fill */
      for(unsigned t=0;t+2<M->idxCount&&t/3<SK_MAXT;t+=3){ int q[3]={S->vp[ix[t]],S->vp[ix[t+1]],S->vp[ix[t+2]]};
          for(int i=0;i<2;i++) for(int j=0;j<2-i;j++) if(q[j]>q[j+1]){ int tm=q[j]; q[j]=q[j+1]; q[j+1]=tm; }
          unsigned h=(((unsigned)q[0]*73856093u)^((unsigned)q[1]*19349663u)^((unsigned)q[2]*83492791u))&((1u<<15)-1u); int seen=0;
          while(th[h]>=0){ const unsigned short*o=tk3[th[h]]; if(o[0]==q[0]&&o[1]==q[1]&&o[2]==q[2]){ seen=1; break; } h=(h+1)&((1u<<15)-1u); }
          if(seen){ S->tdup[t/3]=1; nDup++; } else { th[h]=(int)(t/3); tk3[t/3][0]=(unsigned short)q[0]; tk3[t/3][1]=(unsigned short)q[1]; tk3[t/3][2]=(unsigned short)q[2]; } } }
    if(nDup) printf("[skirt] %s '%s': %d of its %u triangles say again one already said (a wall wound both ways): drawn, and not counted as cloth twice\n",P->label,mnm,nDup,M->idxCount/3);
    S->ne=0; S->nt=0; int drop=0;
    for(unsigned t=0;t+2<M->idxCount;t+=3){
        int a=S->vp[ix[t]], b=S->vp[ix[t+1]], c=S->vp[ix[t+2]];
        if(t/3<SK_MAXT&&S->tdup[t/3]) continue;
        if(a==b||b==c||a==c||S->twin[a]>=0||S->twin[b]>=0||S->twin[c]>=0) continue;
        if(S->nt<SK_MAXT){ S->tri[S->nt][0]=(unsigned short)a; S->tri[S->nt][1]=(unsigned short)b; S->tri[S->nt][2]=(unsigned short)c; S->nt++; }
        int tri[3][3]={{a,b,c},{b,c,a},{c,a,b}};
        for(int k=0;k<3;k++){ int i=tri[k][0], j=tri[k][1], o=tri[k][2]; if(i>j){ int tm=i; i=j; j=tm; }
            unsigned key=((unsigned)i<<16)|(unsigned)j, h=(key*2654435761u)>>(32-SKH); int seen=-1;
            while(hh[h]>=0){ int e=hh[h]; if(S->e0[e]==i&&S->e1[e]==j){ seen=e; break; } h=(h+1)&((1u<<SKH)-1u); }
            if(seen>=0){ if(opp1[seen]==65535&&o!=opp0[seen]) opp1[seen]=(unsigned short)o; continue; }
            if(S->ne>=SK_MAXE){ drop++; continue; }
            hh[h]=S->ne; S->e0[S->ne]=(unsigned short)i; S->e1[S->ne]=(unsigned short)j;
            opp0[S->ne]=(unsigned short)o; opp1[S->ne]=65535;
            float dx=S->pr[j][0]-S->pr[i][0], dy=S->pr[j][1]-S->pr[i][1], dz=S->pr[j][2]-S->pr[i][2];
            S->elen[S->ne]=sqrtf(dx*dx+dy*dy+dz*dz); S->ne++; } }
    #undef SKH
    S->nb=0;
    for(int e=0;e<S->ne&&S->nb<SK_MAXE;e++){ if(opp1[e]==65535) continue; int i=opp0[e], j=opp1[e];
        float dx=S->pr[j][0]-S->pr[i][0], dy=S->pr[j][1]-S->pr[i][1], dz=S->pr[j][2]-S->pr[i][2];
        S->b0[S->nb]=(unsigned short)i; S->b1[S->nb]=(unsigned short)j; S->blen[S->nb]=sqrtf(dx*dx+dy*dy+dz*dz); S->nb++; }
    if(drop){ printf("[skirt] !! %s '%s': %d edges over the cap -- NOT simulated\n",P->label,mnm,drop); fflush(stdout); return 0; }
    /* THE WAISTBAND: no weight on a chain */
    int nPin=0, nFree=0;
    for(int p=0;p<np;p++){ S->pin[p]=0; if(S->twin[p]>=0) continue; if(pcw[p]<0.02f){ S->pin[p]=1; nPin++; } else nFree++; }
    S->band=0; S->bandY=0.f;
    if(!nPin||!nFree){ float y0=1e9f,y1=-1e9f; nPin=nFree=0;
        for(int p=0;p<np;p++) if(S->twin[p]<0){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; }
        for(int p=0;p<np;p++) if(S->twin[p]<0){ S->pin[p]=(S->pr[p][1]>y1-(y1-y0)*0.10f)?1:0; nPin+=S->pin[p]; nFree+=!S->pin[p]; }
        /* a skirt hung by its top band (11-04, REVA's pleated mini): where the band ends, in the rig's own space. Nothing of it may rise above that (below) */
        S->band=1; S->bandY=y1-(y1-y0)*0.10f;
        printf("[skirt] %s '%s': no Cloth_ chain weights -- the top tenth is the waistband\n",P->label,mnm); }
    if(!nPin||!nFree){ printf("[skirt] !! %s '%s': nothing to hang (%d pinned, %d free) -- NOT simulated\n",P->label,mnm,nPin,nFree); fflush(stdout); return 0; }
    /* (11-16) a coat hung by its Hang_ nodes never rises above its own waistband either (in the hips' rest frame): the hem
     * never rises above where gravity and her body put it */
    if(S->hang&&!S->band){ float yb=1e9f; for(int p=0;p<np;p++) if(S->twin[p]<0&&S->pin[p]&&S->pr[p][1]<yb) yb=S->pr[p][1];
        if(yb<1e8f){ S->band=1; S->bandY=yb; } }
    /* THE TETHERS: the shortest way along the cloth from each free point to the waistband (Dijkstra on its edges) */
    { static int start[SK_MAXP+1], adj[2*SK_MAXE]; static float adjL[2*SK_MAXE], dist[SK_MAXP]; static unsigned char done[SK_MAXP];
      for(int p=0;p<=np;p++) start[p]=0;
      for(int e=0;e<S->ne;e++){ start[S->e0[e]+1]++; start[S->e1[e]+1]++; }
      for(int p=0;p<np;p++) start[p+1]+=start[p];
      static int fill[SK_MAXP]; for(int p=0;p<np;p++) fill[p]=start[p];
      for(int e=0;e<S->ne;e++){ int i=S->e0[e], j=S->e1[e];
          adj[fill[i]]=j; adjL[fill[i]++]=S->elen[e]; adj[fill[j]]=i; adjL[fill[j]++]=S->elen[e]; }
      for(int p=0;p<np;p++){ dist[p]=1e30f; done[p]=0; S->anchor[p]=-1; S->tlen[p]=0.f;
          if(S->twin[p]<0&&S->pin[p]){ dist[p]=0.f; S->anchor[p]=(short)p; } }
      for(;;){ int u=-1; float bdist=1e29f;
          for(int p=0;p<np;p++) if(!done[p]&&dist[p]<bdist){ bdist=dist[p]; u=p; }
          if(u<0) break;
          done[u]=1;
          for(int k=start[u];k<start[u+1];k++){ int q=adj[k]; float nd=dist[u]+adjL[k];
              if(nd<dist[q]){ dist[q]=nd; S->anchor[q]=S->anchor[u]; } } }
      for(int p=0;p<np;p++) S->tlen[p]=(dist[p]<1e29f)?dist[p]:0.f; }
    /* which way round the triangles go: the outer wall's normals must leave the body, the drawn ones agree with the
     * mesh's own */
    { float so=0.f, sr=0.f;
      for(unsigned t=0;t+2<M->idxCount;t+=3){
          int a=S->vp[ix[t]], b=S->vp[ix[t+1]], c=S->vp[ix[t+2]];
          if(t/3<SK_MAXT&&S->tdup[t/3]) continue;
          float u[3],v[3]; for(int k=0;k<3;k++){ u[k]=S->pr[b][k]-S->pr[a][k]; v[k]=S->pr[c][k]-S->pr[a][k]; }
          float n[3]={u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
          for(int q=0;q<3;q++){ unsigned vv=ix[t+q]; sr+=n[0]*bn[vv][0]+n[1]*bn[vv][1]+n[2]*bn[vv][2]; }
          if(S->twin[a]<0&&S->twin[b]<0&&S->twin[c]<0){
              float cx=(S->pr[a][0]+S->pr[b][0]+S->pr[c][0])/3.f-S->ax, cz=(S->pr[a][2]+S->pr[b][2]+S->pr[c][2])/3.f-S->az;
              so+=n[0]*cx+n[2]*cz; } }
      S->osign=(so<0.f)?-1.f:1.f; S->rsign=(sr<0.f)?-1.f:1.f; }
    /* THE PELVIS ZONE: rest heights above where the thigh capsules begin */
    { float ylow=-1e9f;
      for(int s=0;s<2;s++){ int a=P->pb[s?14:12], b=P->pb[s?15:13];
          if(a>=0&&b>=0){ float y=RW[a][13]+(RW[b][13]-RW[a][13])*0.2f; if(y>ylow) ylow=y; } }
      int nc=0; for(int p=0;p<np;p++){ S->cone[p]=(S->twin[p]<0&&!S->pin[p]&&S->pr[p][1]>ylow&&ylow>-1e8f)?1:0; nc+=S->cone[p]; }
      S->ncone=nc; }
    /* ⭐ THE SKIN UNDER IT (10-18): every vertex of the body's skin -- "skin", and the skin HIDDEN under a garment
     * ("skin~h=...") -- from half the skirt's length below its hem up to its waistband, at rest. The capsules are the
     * fast limbs; this is the SHAPE: the buttock and the back of the thigh pushed through the coat whenever a leg
     * swung back, because no capsule starts that high on the thigh (its sphere would stand out of the hip). */
    /* ⛔ NOT THE ARMS. The rest pose hangs the arms at the hips, so "skin from the hem to the waist" took in the
     * forearms and hands -- and a hand swinging past shoved the coat 4-5 cm out of its way. An arm is outside the
     * skirt; its skin is not what the skirt lies on. A vertex belongs to the arm when its strongest bone hangs off a
     * clavicle or an upper arm. */
    static unsigned char armN[1024];
    for(unsigned i=0;i<P->nNodes&&i<1024;i++){ armN[i]=0;
        for(int a=(int)i,h9=0;a>=0&&h9<64;a=P->nodes[a].parent,h9++)
            if(a==P->pb[6]||a==P->pb[7]||a==P->pb[9]||a==P->pb[10]){ armN[i]=(a>=0); break; } }
    /* ⛔ AND ONLY THE BACK OF HER. Over the whole body it wadded the front panel up in the hip crease whenever a knee
     * rose: there the skin folds, and the nearest skin point's tangent plane is the underside of the belly or the top
     * of the thigh -- it shoved the cloth down into the thigh, the thigh shoved it back, and the panel balled up at
     * her hip (with the layer off it hung as a panel). The front is the thigh capsule and the hips' rule; the skin
     * keeps the BACK: the buttock and the back of the thigh. Forward is heel to toe, at rest. */
    float fwd[2]={0.f,1.f};
    { int ft=P->footL, kn=P->pb[13]; int an=ft;
      while(an>=0&&kn>=0&&P->nodes[an].parent>=0&&P->nodes[an].parent!=kn) an=P->nodes[an].parent;
      if(an>=0&&ft>=0&&an!=ft){ float fx=RW[ft][12]-RW[an][12], fz=RW[ft][14]-RW[an][14], fl=sqrtf(fx*fx+fz*fz);
          if(fl>1e-6f){ fwd[0]=fx/fl; fwd[1]=fz/fl; } } }
    /* ⛔ AND ONLY THE BACK OF THE CLOTH MEETS IT. A closed shift's FRONT, pulled back between the legs by a stride,
     * found the backs of the thighs as its nearest skin -- facing away from it -- and was shoved 35 mm back through the
     * leg every substep (stretch x8 on the boys at 16 substeps; it hid at 8). Cloth cut on the front half never meets
     * this layer; the legs' capsules and the hips keep it. */
    for(int p=0;p<np;p++) S->back[p]=((S->pr[p][0]-S->ax)*fwd[0]+(S->pr[p][2]-S->az)*fwd[1]<0.f)?1:0;
    S->fwdR[0]=fwd[0]; S->fwdR[1]=fwd[1];
    { float y0=1e9f,y1=-1e9f; for(int p=0;p<np;p++){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; }
      float ylo=y0-0.5f*(y1-y0), yhi=y1+0.02f*(y1-y0);
      int cand=0;
      for(int pass=0;pass<2;pass++){ int stride=(pass==0)?1:(cand/SK_BODYMAX+1), k9=0; if(pass==1) S->nbv=0;
          for(unsigned m=0;m<P->nMeshes&&m<255;m++){ PMesh*Mb=&P->meshes[m];
              const char*nm=packStr(P,Mb->name);
              if(m==mi||!nm||!skBodyMesh(P,m)||!(Mb->flags&4)||!Mb->boneCount||Mb->boneCount>MAXB||Mb->verts>65535) continue;
              float*brp=(float*)(P->pk+Mb->offPos), *bbd=(float*)(P->pk+Mb->offBind);
              unsigned short*bbm=(unsigned short*)(P->pk+Mb->offBoneMap);
              unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
              for(unsigned v=0;v<Mb->verts;v++){
                  float y=bbd[1]*brp[v*3]+bbd[5]*brp[v*3+1]+bbd[9]*brp[v*3+2]+bbd[13];
                  if(y<ylo||y>yhi) continue;
                  { float x=bbd[0]*brp[v*3]+bbd[4]*brp[v*3+1]+bbd[8]*brp[v*3+2]+bbd[12], z=bbd[2]*brp[v*3]+bbd[6]*brp[v*3+1]+bbd[10]*brp[v*3+2]+bbd[14];
                    if((x-S->ax)*fwd[0]+(z-S->az)*fwd[1]>0.f) continue;       /* the front half: not this layer's */
                    /* ⛔ AND ONLY SKIN THAT FACES BACK. The inner thighs face each other across the crotch, and a cloth
                     * point in front of that gap took whichever thigh was nearer as its plane -- 6 cm "inside" one,
                     * then the other: the tight freezer shift balled up round his hips standing STILL. A buttock, the
                     * back of a thigh, a calf face backward; that is what this layer is for. */
                    if(Mb->flags&1){ float*bnp=(float*)(P->pk+Mb->offNorm);
                        float nx=bbd[0]*bnp[v*3]+bbd[4]*bnp[v*3+1]+bbd[8]*bnp[v*3+2], ny=bbd[1]*bnp[v*3]+bbd[5]*bnp[v*3+1]+bbd[9]*bnp[v*3+2],
                              nz=bbd[2]*bnp[v*3]+bbd[6]*bnp[v*3+1]+bbd[10]*bnp[v*3+2], nl=sqrtf(nx*nx+ny*ny+nz*nz)+1e-12f;
                        if(-(nx*fwd[0]+nz*fwd[1])<0.35f*nl) continue; } }
                  { int bj=-1, bw=-1; for(int q=0;q<4;q++) if(bwp[v*4+q]>bw){ bw=bwp[v*4+q]; bj=bjp[v*4+q]; }
                    if(bj>=0&&bj<(int)Mb->boneCount){ unsigned nd=bbm[bj]; if(nd<P->nNodes&&nd<1024&&armN[nd]) continue; } }
                  if(pass==0){ cand++; continue; }
                  if((k9++)%stride) continue;
                  if(S->nbv<SK_BODYMAX){ S->bvm[S->nbv]=(unsigned char)m; S->bvv[S->nbv]=(unsigned short)v; S->nbv++; } } } } }
    /* ⭐ AT REST THE COLLISION MOVES NOTHING (the drape's law). A garment is cut with its own ease, and a tight one -- the
     * freezer shift, 4 mm at the hips -- sits INSIDE the margins a loose coat needs: the solver shoved it off the body
     * on its first frame, against its own waistband, and wadded it up round his hips (stretch x6, kids x13). So each
     * point is measured against the body at REST, and however far it already sits inside a margin is its own
     * allowance: the body only ever pushes cloth it has MOVED into. */
    { static const float I16b[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
      if(!P->boneRok) measureBoneR(P);
      /* ⭐ THE LEGS AS THEY ARE (10-18). The capsules were the measured radius (already 15% outside the skin) x1.06 and
       * then 1.2 cm more -- a boy's thigh 60% fatter than it is: his tight shift could not go round both legs mid-stride
       * and rode up to his hips (at 8 substeps it stretched instead, which hid it). Now: the measured radius, and off it
       * a garment's ease where a garment covers the leg in this outfit (the leg's skin is HIDDEN), a hair where it is
       * bare -- then the wall. */
      { int tl=P->pb[12], tr=P->pb[14], bestM=-1, bestN=0;
        for(unsigned m=0;m<P->nMeshes;m++){ PMesh*Mb=&P->meshes[m]; const char*nm=packStr(P,Mb->name);
            if(m==mi||!nm||!skBodyMesh(P,m)||!Mb->boneCount||Mb->boneCount>MAXB) continue;
            unsigned short*bbm=(unsigned short*)(P->pk+Mb->offBoneMap);
            unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
            int n=0; for(unsigned v=0;v<Mb->verts;v++){ int bj=-1, bw=-1; for(int q=0;q<4;q++) if(bwp[v*4+q]>bw){ bw=bwp[v*4+q]; bj=bjp[v*4+q]; }
                if(bj>=0&&bj<(int)Mb->boneCount&&((int)bbm[bj]==tl||(int)bbm[bj]==tr)) n++; }
            if(n>bestN){ bestN=n; bestM=(int)m; } }
        int covered=(bestM>=0&&MOFF(P,(unsigned)bestM))?1:0;
        S->legM=((g_skLegM>=0.f)?g_skLegM:(covered?0.006f:0.002f))+S->tmed;
        printf("[skirt] %s: the legs are %s under it -- capsules at their measured thickness, %.1f mm off%c",
               P->label,covered?"COVERED (a garment's ease)":"BARE",S->legM*1000.f,10); }
      float capR[SK_NCAP][SKC]; int ncr=skCaps(P,RW,I16b,1.f,S->tmed,S->legM,capR);
      int nA=0, nS=0; float aMax=0.f, sMax=0.f;
      for(int p=0;p<np;p++){ S->allowC[p]=0.f; S->allowS[p]=0.f; if(S->twin[p]>=0||S->pin[p]) continue;
          for(int c=0;c<ncr;c++){ const float*A=capR[c], *B=capR[c]+3;
              float e[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, e2=e[0]*e[0]+e[1]*e[1]+e[2]*e[2];
              float t=(e2>1e-12f)?((S->pr[p][0]-A[0])*e[0]+(S->pr[p][1]-A[1])*e[1]+(S->pr[p][2]-A[2])*e[2])/e2:0.f;
              if(t<0.f) t=0.f;
              if(t>1.f) t=1.f;
              float dx=S->pr[p][0]-(A[0]+e[0]*t), dy=S->pr[p][1]-(A[1]+e[1]*t), dz=S->pr[p][2]-(A[2]+e[2]*t);
              float in=skCapR(capR[c],t)-sqrtf(dx*dx+dy*dy+dz*dz);
              if(in>S->allowC[p]) S->allowC[p]=in; }
          if(S->allowC[p]>0.f){ nA++; if(S->allowC[p]>aMax) aMax=S->allowC[p]; } }
      if(S->nbv){ skBodyFrame(S,P,RW,I16b,1);
          float clr=0.008f+S->tmed, rad=0.05f;
          for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; int best=-1; float bd=rad*rad;
              for(int i=0;i<S->nbv;i++){ float dx=S->pr[p][0]-S->bp1[i][0], dy=S->pr[p][1]-S->bp1[i][1], dz=S->pr[p][2]-S->bp1[i][2];
                  float d2=dx*dx+dy*dy+dz*dz; if(d2<bd){ bd=d2; best=i; } }
              if(best<0) continue;
              float d=(S->pr[p][0]-S->bp1[best][0])*S->bn1[best][0]+(S->pr[p][1]-S->bp1[best][1])*S->bn1[best][1]+(S->pr[p][2]-S->bp1[best][2])*S->bn1[best][2];
              if(d<clr){ S->allowS[p]=clr-d; nS++; if(clr-d>sMax) sMax=clr-d; } } }
      printf("[skirt] %s: at rest %d points sit inside a limb's margin (up to %.1f mm) and %d inside the skin's (up to %.1f mm) -- allowed, never pushed\n",
             P->label,nA,aMax*1000.f,nS,sMax*1000.f); }
    /* ⭐ THE HANDS (10-18). In close crops: her hand hanging at her hip, its lower half INSIDE the coat -- the side panel
     * flares past where the hand swings. An arm is outside the skirt, so a hand only ever presses the cloth IN, toward
     * the body, and the body stops it: under a pelvis point the cloth may go down to the skin's own radius plus the
     * undersuit and the wall (floorR), never through her. The hand is its own skin, sampled (a capsule round a hand
     * is a fat sausage that shoves cloth 4 cm away from fingers 1 cm thick). */
    { static unsigned char handN[1024];
      int hl=skFirstChild(P,P->pb[8],"hand"), hr=skFirstChild(P,P->pb[11],"hand");
      for(unsigned i=0;i<P->nNodes&&i<1024;i++){ handN[i]=0;
          for(int a=(int)i,h9=0;a>=0&&h9<64;a=P->nodes[a].parent,h9++) if(a==hl||a==hr){ handN[i]=(a>=0); break; } }
      int cand=0;
      for(int pass=0;pass<2;pass++){ int stride=(pass==0)?1:(cand/256+1), k9=0; if(pass==1) S->nhv=0;   /* ~256: a hand sampled every centimetre or so */
          for(unsigned m=0;m<P->nMeshes&&m<255;m++){ PMesh*Mb=&P->meshes[m];
              const char*nm=packStr(P,Mb->name);
              if(m==mi||!nm||!skBodyMesh(P,m)||!(Mb->flags&4)||!Mb->boneCount||Mb->boneCount>MAXB||Mb->verts>65535) continue;
              unsigned short*bbm=(unsigned short*)(P->pk+Mb->offBoneMap);
              unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
              for(unsigned v=0;v<Mb->verts;v++){
                  int bj=-1, bw=-1; for(int q=0;q<4;q++) if(bwp[v*4+q]>bw){ bw=bwp[v*4+q]; bj=bjp[v*4+q]; }
                  if(bj<0||bj>=(int)Mb->boneCount) continue;
                  unsigned nd=bbm[bj]; if(nd>=P->nNodes||nd>=1024||!handN[nd]) continue;
                  if(pass==0){ cand++; continue; }
                  if((k9++)%stride) continue;
                  if(S->nhv<SK_HANDMAX){ S->hvm[S->nhv]=(unsigned char)m; S->hvv[S->nhv]=(unsigned short)v; S->nhv++; } } } }
      /* the body under each pelvis point, at rest: the skin's largest radius from the hips' axis near its angle and
       * height (the arms left out: at rest they hang at the hips) */
      float y0=1e9f,y1=-1e9f; for(int p=0;p<np;p++){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; }
      float hwin=0.04f*(y1-y0), clr=0.006f+S->tmed;
      static float sk3[24000][3]; int ns=0;
      for(unsigned m=0;m<P->nMeshes&&m<255;m++){ PMesh*Mb=&P->meshes[m];
          const char*nm=packStr(P,Mb->name);
          if(m==mi||!nm||!skBodyMesh(P,m)||!(Mb->flags&4)||!Mb->boneCount||Mb->boneCount>MAXB) continue;
          float*brp=(float*)(P->pk+Mb->offPos), *bbd=(float*)(P->pk+Mb->offBind);
          unsigned short*bbm=(unsigned short*)(P->pk+Mb->offBoneMap);
          unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
          for(unsigned v=0;v<Mb->verts&&ns<24000;v++){
              float y=bbd[1]*brp[v*3]+bbd[5]*brp[v*3+1]+bbd[9]*brp[v*3+2]+bbd[13];
              if(y<y0-hwin||y>y1+hwin) continue;
              int bj=-1, bw=-1; for(int q=0;q<4;q++) if(bwp[v*4+q]>bw){ bw=bwp[v*4+q]; bj=bjp[v*4+q]; }
              if(bj>=0&&bj<(int)Mb->boneCount){ unsigned nd=bbm[bj]; if(nd<P->nNodes&&nd<1024&&armN[nd]) continue; }
              sk3[ns][0]=bbd[0]*brp[v*3]+bbd[4]*brp[v*3+1]+bbd[8]*brp[v*3+2]+bbd[12]; sk3[ns][1]=y;
              sk3[ns][2]=bbd[2]*brp[v*3]+bbd[6]*brp[v*3+1]+bbd[10]*brp[v*3+2]+bbd[14]; ns++; } }
      int nfl=0; float room=0.f;
      for(int p=0;p<np;p++){ S->floorR[p]=S->rr[p]*0.985f; if(!S->cone[p]) continue;
          float th=atan2f(S->pr[p][0]-S->ax,S->pr[p][2]-S->az), best=-1.f;
          for(int i=0;i<ns;i++){ if(fabsf(sk3[i][1]-S->pr[p][1])>hwin) continue;
              float dx=sk3[i][0]-S->ax, dz=sk3[i][2]-S->az, dth=atan2f(dx,dz)-th;
              while(dth>3.14159265f) dth-=6.28318531f;
              while(dth<-3.14159265f) dth+=6.28318531f;
              if(fabsf(dth)>0.12f) continue;
              float r=sqrtf(dx*dx+dz*dz); if(r>best) best=r; }
          if(best>0.f&&best+clr<S->floorR[p]){ S->floorR[p]=best+clr; nfl++; room+=S->rr[p]*0.985f-S->floorR[p]; } }
      printf("[skirt] %s: %d hand points; %d pelvis points may be pressed in toward the body, %.1f mm on average\n",
             P->label,S->nhv,nfl,nfl?room/(float)nfl*1000.f:0.f); }
    int nFreeT=0; float tmax=0.f; for(int p=0;p<np;p++) if(S->twin[p]<0&&!S->pin[p]){ nFreeT+=(S->anchor[p]>=0); if(S->tlen[p]>tmax) tmax=S->tlen[p]; }
    printf("[skirt] %s '%s': %d verts -> %d points (%d outer, %d lining %.1f mm behind, %d single), %d edges, %d bends, "
           "%d waistband, %d free (%d tethered, longest %.2f m), %d in the pelvis zone, %d skin points under it, normals %+.0f/%+.0f\n",
           P->label,mnm,nv,np,nOut,nIn,S->tmed*1000.f,nSingle,S->ne,S->nb,nPin,nFree,nFreeT,tmax,S->ncone,S->nbv,S->osign,S->rsign);
    fflush(stdout);
    S->seeded=1;
    return 1; }

/* the skin under the cloth, this frame, in world space (and last frame's kept for the substeps between) */
static void skBodyFrame(Skirt*S,Pack*P,M4*world,const float*model,int first){
    if(!S->nbv) return;
    if(!first) memcpy(S->bp0,S->bp1,sizeof(float)*3*(size_t)S->nbv);
    static float bpal[MAXB*16]; int curM=-1;
    for(int i=0;i<S->nbv;i++){ int m=S->bvm[i]; PMesh*Mb=&P->meshes[m];
        if(m!=curM){ curM=m; unsigned short*bm=(unsigned short*)(P->pk+Mb->offBoneMap); float*ibm=(float*)(P->pk+Mb->offIBM);
            for(unsigned b=0;b<Mb->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]<P->nNodes?bm[b]:0],*(M4*)(ibm+b*16)); memcpy(bpal+b*16,pm,64); } }
        unsigned v=S->bvv[i];
        float*brp=(float*)(P->pk+Mb->offPos), *bnp=(float*)(P->pk+Mb->offNorm), *bbd=(float*)(P->pk+Mb->offBind);
        unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
        float bq[3], nq[3]={0,1,0};
        for(int k=0;k<3;k++) bq[k]=bbd[k]*brp[v*3]+bbd[4+k]*brp[v*3+1]+bbd[8+k]*brp[v*3+2]+bbd[12+k];
        if(Mb->flags&1) for(int k=0;k<3;k++) nq[k]=bbd[k]*bnp[v*3]+bbd[4+k]*bnp[v*3+1]+bbd[8+k]*bnp[v*3+2];
        float acc[3]={0,0,0}, nac[3]={0,0,0}, ws=0.f;
        for(int q=0;q<4;q++){ float w=bwp[v*4+q]/255.f; if(w<=0.f) continue; const float*pm=bpal+bjp[v*4+q]*16;
            for(int k=0;k<3;k++){ acc[k]+=w*(pm[k]*bq[0]+pm[4+k]*bq[1]+pm[8+k]*bq[2]+pm[12+k]); nac[k]+=w*(pm[k]*nq[0]+pm[4+k]*nq[1]+pm[8+k]*nq[2]); }
            ws+=w; }
        if(ws<1e-4f){ memcpy(acc,bq,12); memcpy(nac,nq,12); }
        skXf(model,acc,S->bp1[i]);
        float wn[3]; for(int k=0;k<3;k++) wn[k]=model[k]*nac[0]+model[4+k]*nac[1]+model[8+k]*nac[2];
        float l=sqrtf(wn[0]*wn[0]+wn[1]*wn[1]+wn[2]*wn[2]); if(l<1e-12f){ wn[0]=0.f; wn[1]=1.f; wn[2]=0.f; l=1.f; }
        for(int k=0;k<3;k++) S->bn1[i][k]=wn[k]/l; }
    if(first) memcpy(S->bp0,S->bp1,sizeof(float)*3*(size_t)S->nbv); }

/* the hands, this frame, in world space (last frame's kept for the substeps between) */
static void skHandFrame(Skirt*S,Pack*P,M4*world,const float*model,int first){
    if(!S->nhv) return;
    if(!first) memcpy(S->hp0,S->hp1,sizeof(float)*3*(size_t)S->nhv);
    static float hpal[MAXB*16]; int curM=-1;
    for(int i=0;i<S->nhv;i++){ int m=S->hvm[i]; PMesh*Mb=&P->meshes[m];
        if(m!=curM){ curM=m; unsigned short*bm=(unsigned short*)(P->pk+Mb->offBoneMap); float*ibm=(float*)(P->pk+Mb->offIBM);
            for(unsigned b=0;b<Mb->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]<P->nNodes?bm[b]:0],*(M4*)(ibm+b*16)); memcpy(hpal+b*16,pm,64); } }
        unsigned v=S->hvv[i];
        float*brp=(float*)(P->pk+Mb->offPos), *bbd=(float*)(P->pk+Mb->offBind);
        unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
        float bq[3]; for(int k=0;k<3;k++) bq[k]=bbd[k]*brp[v*3]+bbd[4+k]*brp[v*3+1]+bbd[8+k]*brp[v*3+2]+bbd[12+k];
        float acc[3]={0,0,0}, ws=0.f;
        for(int q=0;q<4;q++){ float w=bwp[v*4+q]/255.f; if(w<=0.f) continue; const float*pm=hpal+bjp[v*4+q]*16;
            for(int k=0;k<3;k++) acc[k]+=w*(pm[k]*bq[0]+pm[4+k]*bq[1]+pm[8+k]*bq[2]+pm[12+k]);
            ws+=w; }
        if(ws<1e-4f) memcpy(acc,bq,12);
        skXf(model,acc,S->hp1[i]); }
    if(first) memcpy(S->hp0,S->hp1,sizeof(float)*3*(size_t)S->nhv); }

/* ======================== THE SET UNDER IT (11-16) =================================================================================================
 * REVA's lab coat is cloth, and she sits in an armchair in it. This cloth met her body and the floor and nothing of the
 * set, and the note at the top of this file said so ("this will not drape over a chair arm"): sat, the coat's skirt would hang straight down through
 * the seat. Now a SET PIECE -- a prop or a mover -- that is a SOLID is solid to hanging cloth: every edge of its meshes in two triangles (it has an
 * inside) and its triangles wound outward (a room seen from inside is closed too, wound in, and is no solid). The Open marker doesn't count: Open is
 * about bodies walking into a piece, and she sits IN the armchair while her coat lies ON it. Each solid pack's triangles are kept once, in its own
 * space (SkSolid), with the way out of each face, edge and corner (angle-weighted, so a point by a corner knows which side of it it is: Baerentzen
 * and Aanaes 2005), in a grid whose cells list the triangles within SKS_R of them, and a flag per cell for whether its middle is inside. Each frame
 * the pieces whose box is near the cloth are taken (skSetNear), and in every substep each free point is put out of a piece it's in, or nearer than
 * the cloth's clearance, by the nearest surface: inelastic, with the body's friction, against the piece's own motion. A point deeper than SKS_DEEP
 * is no contact: her body put it there (she sits on her coat), and it stays out of sight until she lets it go. FIRST after the floor, so her body
 * has the last word. REVA_SKSET=0 is the control; REVA_SKIRTDBG says which pieces a cloth lies on. */
#define SKS_POOL 64
#define SKS_R 0.05f        /* a piece's grid answers within this of a point, in its own units: the cloth's clearance and SKS_DEEP, with room */
#define SKS_DEEP 0.015f    /* deeper than this (rig metres) under a piece's surface: squeezed there by her body, and left there */
typedef struct {
    const Pack*pk; const unsigned char*bytes; int solid; unsigned used;
    int nt; float*v, *n, *bx;          /* triangles (9 floats), ways out (21: the face, the edges ab bc ca, the corners a b c), boxes (6) */
    float lo[3], hi[3], cell; int g[3]; int*cs, *ci; unsigned char*in;   /* the grid: cell c's triangles are ci[cs[c] .. cs[c+1]); in[c], its middle inside */
    double vol; int open, edges;
} SkSolid;
static SkSolid g_sks[SKS_POOL];
static int g_skSet=-1;
static void sksFree(SkSolid*K){ free(K->v); free(K->n); free(K->bx); free(K->cs); free(K->ci); free(K->in); memset(K,0,sizeof *K); }
static unsigned sksHash3(int x,int y,int z){ return ((unsigned)x*73856093u)^((unsigned)y*19349663u)^((unsigned)z*83492791u); }
/* a pack's triangles in its own space (each mesh's bind applied), kept if they make a solid */
static void sksBuild(SkSolid*K,const Pack*Q){
    const double t0=wall_ms();
    unsigned cap=0;
    for(unsigned mi=0;mi<Q->nMeshes;mi++){ const PMesh*M=&Q->meshes[mi]; if(!(M->flags&4)||M->boneCount>MAXB||M->verts<3||MOFF(Q,mi)) continue;
        cap+=M->idxCount?M->idxCount/3u:M->verts/3u; }
    if(!cap||cap>300000u) return;                    /* nothing, or a whole room: no furniture is that big */
    float*V=(float*)malloc((size_t)cap*36u); if(!V) return;
    int nt=0; double vol=0.0;
    for(unsigned mi=0;mi<Q->nMeshes;mi++){ const PMesh*M=&Q->meshes[mi]; if(!(M->flags&4)||M->boneCount>MAXB||M->verts<3||MOFF(Q,mi)) continue;
        const float*bd=(const float*)(Q->pk+M->offBind), *rp=(const float*)(Q->pk+M->offPos); const unsigned*ix=M->idxCount?(const unsigned*)(Q->pk+M->offIdx):NULL;
        const unsigned n=M->idxCount?M->idxCount/3u:M->verts/3u;
        for(unsigned t=0;t<n&&nt<(int)cap;t++){ unsigned i3[3]={ix?ix[t*3]:t*3,ix?ix[t*3+1]:t*3+1,ix?ix[t*3+2]:t*3+2};
            if(i3[0]>=M->verts||i3[1]>=M->verts||i3[2]>=M->verts) continue;
            float*o=V+(size_t)nt*9;
            for(int c=0;c<3;c++){ const float*p=rp+(size_t)i3[c]*3; for(int k=0;k<3;k++) o[c*3+k]=bd[k]*p[0]+bd[4+k]*p[1]+bd[8+k]*p[2]+bd[12+k]; }
            float e1[3]={o[3]-o[0],o[4]-o[1],o[5]-o[2]}, e2[3]={o[6]-o[0],o[7]-o[1],o[8]-o[2]};
            float cr[3]={e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]};
            if(cr[0]*cr[0]+cr[1]*cr[1]+cr[2]*cr[2]<1e-16f) continue;
            vol+=(double)(o[0]*(o[4]*o[8]-o[5]*o[7])-o[1]*(o[3]*o[8]-o[5]*o[6])+o[2]*(o[3]*o[7]-o[4]*o[6]))/6.0;
            nt++; } }
    K->v=V; K->nt=nt; K->vol=vol;
    if(nt<4){ return; }
    float lo[3]={1e30f,1e30f,1e30f}, hi[3]={-1e30f,-1e30f,-1e30f};
    for(int i=0;i<nt*3;i++) for(int k=0;k<3;k++){ const float x=V[i*3+k]; if(x<lo[k]) lo[k]=x; if(x>hi[k]) hi[k]=x; }
    const float diag=sqrtf((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2])), q9=1.f/fmaxf(diag*2e-6f,1e-9f);
    /* corners welded by place: a mesh cut along its texture's seams is one surface */
    const int nc=nt*3; unsigned hs=1; while(hs<(unsigned)nc*2u) hs<<=1;
    int*hk=(int*)malloc(sizeof(int)*hs), *id=(int*)malloc(sizeof(int)*(size_t)nc); int nid=0;
    int*rep=(int*)malloc(sizeof(int)*(size_t)nc);
    if(!hk||!id||!rep){ free(hk); free(id); free(rep); return; }
    for(unsigned i=0;i<hs;i++) hk[i]=-1;
    for(int i=0;i<nc;i++){ const int qx=(int)lrintf((V[i*3]-lo[0])*q9), qy=(int)lrintf((V[i*3+1]-lo[1])*q9), qz=(int)lrintf((V[i*3+2]-lo[2])*q9);
        unsigned h=sksHash3(qx,qy,qz)&(hs-1u); int got=-1;
        while(hk[h]>=0){ const int j=hk[h], r=rep[j];
            if((int)lrintf((V[r*3]-lo[0])*q9)==qx&&(int)lrintf((V[r*3+1]-lo[1])*q9)==qy&&(int)lrintf((V[r*3+2]-lo[2])*q9)==qz){ got=j; break; }
            h=(h+1u)&(hs-1u); }
        if(got<0){ got=nid++; hk[h]=got; rep[got]=i; }
        id[i]=got; }
    free(hk);
    /* each edge: how many triangles have it, and the sum of their faces' ways out */
    float*fn=(float*)malloc(sizeof(float)*3*(size_t)nt);
    unsigned es=1; while(es<(unsigned)nc*2u) es<<=1;
    long long*ek=(long long*)malloc(sizeof(long long)*es); int*ec=(int*)calloc(es,sizeof(int)); float*en=(float*)calloc((size_t)es*3,sizeof(float));
    float*vn=(float*)calloc((size_t)nid*3,sizeof(float));
    if(!fn||!ek||!ec||!en||!vn){ free(fn); free(ek); free(ec); free(en); free(vn); free(id); free(rep); return; }
    for(unsigned i=0;i<es;i++) ek[i]=-1;
    const float flip=(vol<0.0)?-1.f:1.f;
    for(int t=0;t<nt;t++){ const float*o=V+(size_t)t*9;
        float e1[3]={o[3]-o[0],o[4]-o[1],o[5]-o[2]}, e2[3]={o[6]-o[0],o[7]-o[1],o[8]-o[2]};
        float c[3]={e1[1]*e2[2]-e1[2]*e2[1],e1[2]*e2[0]-e1[0]*e2[2],e1[0]*e2[1]-e1[1]*e2[0]}, l=sqrtf(c[0]*c[0]+c[1]*c[1]+c[2]*c[2])+1e-30f;
        for(int k=0;k<3;k++) fn[t*3+k]=c[k]/l*flip;
        for(int s=0;s<3;s++){ int a=id[t*3+s], b=id[t*3+(s+1)%3]; if(a>b){ int tm=a; a=b; b=tm; }
            const long long key=((long long)a<<32)|(long long)(unsigned)b; unsigned h=(unsigned)((key*0x9E3779B97F4A7C15ull)>>32)&(es-1u);
            while(ek[h]>=0&&ek[h]!=key) h=(h+1u)&(es-1u);
            ek[h]=key; ec[h]++; for(int k=0;k<3;k++) en[h*3+k]+=fn[t*3+k]; }
        /* a corner's way out: its faces' weighted by the angle each has there */
        for(int s=0;s<3;s++){ const float*A=o+s*3, *B=o+((s+1)%3)*3, *C=o+((s+2)%3)*3;
            float u[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, w[3]={C[0]-A[0],C[1]-A[1],C[2]-A[2]};
            float lu=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]), lw=sqrtf(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]); if(lu<1e-12f||lw<1e-12f) continue;
            float cs=(u[0]*w[0]+u[1]*w[1]+u[2]*w[2])/(lu*lw); if(cs>1.f) cs=1.f; if(cs<-1.f) cs=-1.f;
            const float an=acosf(cs); const int vi=id[t*3+s]; for(int k=0;k<3;k++) vn[vi*3+k]+=fn[t*3+k]*an; } }
    int open=0, edges=0; for(unsigned i=0;i<es;i++) if(ek[i]>=0){ edges++; if(ec[i]!=2) open++; }
    K->open=open; K->edges=edges;
    K->solid=(vol!=0.0&&flip>0.f&&open<=((edges/500>2)?edges/500:2))?1:0;
    if(K->solid){
        K->n=(float*)malloc(sizeof(float)*21*(size_t)nt); K->bx=(float*)malloc(sizeof(float)*6*(size_t)nt);
        if(!K->n||!K->bx) K->solid=0; }
    if(K->solid){
        for(int t=0;t<nt;t++){ float*nn=K->n+(size_t)t*21; const float*o=V+(size_t)t*9;
            memcpy(nn,fn+t*3,12);
            for(int s=0;s<3;s++){ int a=id[t*3+s], b=id[t*3+(s+1)%3]; if(a>b){ int tm=a; a=b; b=tm; }
                const long long key=((long long)a<<32)|(long long)(unsigned)b; unsigned h=(unsigned)((key*0x9E3779B97F4A7C15ull)>>32)&(es-1u);
                while(ek[h]!=key) h=(h+1u)&(es-1u);
                float*e=nn+3+s*3; float l=sqrtf(en[h*3]*en[h*3]+en[h*3+1]*en[h*3+1]+en[h*3+2]*en[h*3+2]);
                if(l>1e-12f) for(int k=0;k<3;k++) e[k]=en[h*3+k]/l; else memcpy(e,fn+t*3,12);
                const int vi=id[t*3+s]; float*c9=nn+12+s*3; l=sqrtf(vn[vi*3]*vn[vi*3]+vn[vi*3+1]*vn[vi*3+1]+vn[vi*3+2]*vn[vi*3+2]);
                if(l>1e-12f) for(int k=0;k<3;k++) c9[k]=vn[vi*3+k]/l; else memcpy(c9,fn+t*3,12); }
            float*b=K->bx+(size_t)t*6; for(int k=0;k<3;k++){ b[k]=fminf(o[k],fminf(o[3+k],o[6+k])); b[3+k]=fmaxf(o[k],fmaxf(o[3+k],o[6+k])); } } }
    free(fn); free(ek); free(ec); free(en); free(vn); free(id); free(rep);
    if(!K->solid) return;
    /* the grid: cells listing the triangles within SKS_R of them */
    const float R=SKS_R; float ext=0.f; for(int k=0;k<3;k++){ K->lo[k]=lo[k]-R; K->hi[k]=hi[k]+R; ext=fmaxf(ext,K->hi[k]-K->lo[k]); }
    K->cell=fmaxf(0.05f,ext/96.f);
    long long ncell=1; for(int k=0;k<3;k++){ K->g[k]=(int)ceilf((K->hi[k]-K->lo[k])/K->cell); if(K->g[k]<1) K->g[k]=1; ncell*=K->g[k]; }
    K->cs=(int*)calloc((size_t)ncell+1,sizeof(int)); K->in=(unsigned char*)calloc((size_t)ncell,1);
    if(!K->cs||!K->in){ K->solid=0; return; }
    for(int pass=0;pass<2;pass++){
        if(pass==1){ for(long long c=0;c<ncell;c++) K->cs[c+1]+=K->cs[c]; K->ci=(int*)malloc(sizeof(int)*(size_t)(K->cs[ncell]?K->cs[ncell]:1));
            if(!K->ci){ K->solid=0; return; }
            for(long long c=ncell;c>0;c--) K->cs[c]=K->cs[c-1];
            K->cs[0]=0; }
        for(int t=0;t<nt;t++){ const float*b=K->bx+(size_t)t*6; int c0[3],c1[3];
            for(int k=0;k<3;k++){ c0[k]=(int)floorf((b[k]-R-K->lo[k])/K->cell); c1[k]=(int)floorf((b[3+k]+R-K->lo[k])/K->cell);
                if(c0[k]<0) c0[k]=0;
                if(c1[k]>=K->g[k]) c1[k]=K->g[k]-1; }
            for(int z=c0[2];z<=c1[2];z++) for(int y=c0[1];y<=c1[1];y++) for(int x=c0[0];x<=c1[0];x++){ const long long c=((long long)z*K->g[1]+y)*K->g[0]+x;
                if(pass==0) K->cs[c+1]++; else K->ci[K->cs[c+1]++]=t; } } }
    /* inside or out, each cell's middle: a vertical line through it, the surfaces it crosses below the middle counted (a hair off the middle, so it
     * never runs down an edge) */
    { const int nx=K->g[0], nz=K->g[2]; const long long ncol=(long long)nx*nz;
      int*cc=(int*)calloc((size_t)ncol+1,sizeof(int)), *cl=NULL; float*ys=(float*)malloc(sizeof(float)*4096);
      if(cc&&ys) for(int pass=0;pass<2;pass++){   /* the triangles over each column, by their boxes seen from above */
          if(pass==1){ for(long long c=0;c<ncol;c++) cc[c+1]+=cc[c];
              cl=(int*)malloc(sizeof(int)*(size_t)(cc[ncol]?cc[ncol]:1)); if(!cl) break;
              for(long long c=ncol;c>0;c--) cc[c]=cc[c-1];
              cc[0]=0; }
          for(int t=0;t<nt;t++){ const float*b=K->bx+(size_t)t*6;
              int x0=(int)floorf((b[0]-K->lo[0])/K->cell), x1=(int)floorf((b[3]-K->lo[0])/K->cell), z0=(int)floorf((b[2]-K->lo[2])/K->cell), z1=(int)floorf((b[5]-K->lo[2])/K->cell);
              if(x0<0) x0=0;
              if(z0<0) z0=0;
              if(x1>=nx) x1=nx-1;
              if(z1>=nz) z1=nz-1;
              for(int z=z0;z<=z1;z++) for(int x=x0;x<=x1;x++){ const long long c=(long long)z*nx+x; if(pass==0) cc[c+1]++; else cl[cc[c+1]++]=t; } } }
      if(cl) for(int z=0;z<nz;z++) for(int x=0;x<nx;x++){
          const float px=K->lo[0]+((float)x+0.5f)*K->cell+K->cell*0.0001237f, pz=K->lo[2]+((float)z+0.5f)*K->cell+K->cell*0.0000731f; int ny=0;
          const long long col=(long long)z*nx+x;
          for(int e=cc[col];e<cc[col+1]&&ny<4096;e++){ const int t=cl[e]; const float*b=K->bx+(size_t)t*6; if(px<b[0]||px>b[3]||pz<b[2]||pz>b[5]) continue;
              const float*o=V+(size_t)t*9; const float ax=o[0],az=o[2],bx9=o[3],bz=o[5],cx=o[6],cz=o[8];
              const float d=(bz-cz)*(ax-cx)+(cx-bx9)*(az-cz); if(fabsf(d)<1e-14f) continue;
              const float l1=((bz-cz)*(px-cx)+(cx-bx9)*(pz-cz))/d, l2=((cz-az)*(px-cx)+(ax-cx)*(pz-cz))/d, l3=1.f-l1-l2;
              if(l1<0.f||l2<0.f||l3<0.f) continue;
              ys[ny++]=l1*o[1]+l2*o[4]+l3*o[7]; }
          for(int y=0;y<K->g[1];y++){ const float py=K->lo[1]+((float)y+0.5f)*K->cell; int below=0;
              for(int i=0;i<ny;i++) if(ys[i]<py) below++;
              K->in[((long long)z*K->g[1]+y)*nx+x]=(unsigned char)(below&1); } }
      free(cc); free(cl); free(ys); }
    printf("[skirt] the set's %s is solid to hanging cloth: %d triangles, %.3f cubic units, a grid of %d x %d x %d (%.0f ms)\n",
           Q->label[0]?Q->label:"piece",nt,vol,K->g[0],K->g[1],K->g[2],wall_ms()-t0); fflush(stdout); }
/* a pack's entry, made the first time a cloth comes near it */
static int sksGet(const Pack*Q){
    for(int i=0;i<SKS_POOL;i++) if(g_sks[i].pk==Q&&g_sks[i].bytes==Q->pk){ g_sks[i].used=g_frameStamp; return i; }
    /* a free place, or the one longest unasked (and not for 300 frames: a crowded set would read the same packs again every frame) */
    int pick=-1; unsigned old=0xffffffffu;
    for(int i=0;i<SKS_POOL;i++){ if(!g_sks[i].pk){ pick=i; break; } if(g_sks[i].used+300u<g_frameStamp&&g_sks[i].used<old){ old=g_sks[i].used; pick=i; } }
    if(pick<0){ static int said9=0; if(!said9){ said9=1; printf("[skirt] more than %d of the set's packs near hanging cloth at once: the rest aren't solid to it\n",SKS_POOL); fflush(stdout); } return -1; }
    sksFree(&g_sks[pick]); g_sks[pick].pk=Q; g_sks[pick].bytes=Q->pk; g_sks[pick].used=g_frameStamp;
    sksBuild(&g_sks[pick],Q);
    return pick; }
/* the nearest surface of a solid to p (its own space) within r: its point q, the way out at q, and how far p is outside it (under 0, inside) */
static int sksNear(const SkSolid*K,const float*p,float r,float*q,float*n,float*sd){
    int c[3]; for(int k=0;k<3;k++){ c[k]=(int)floorf((p[k]-K->lo[k])/K->cell); if(c[k]<0||c[k]>=K->g[k]) return 0; }
    const long long ce=((long long)c[2]*K->g[1]+c[1])*K->g[0]+c[0];
    float best=r*r, bu=0.f, bv=0.f, bw=0.f, bq[3]={0,0,0}; int bt=-1;
    for(int e=K->cs[ce];e<K->cs[ce+1];e++){ const int t=K->ci[e]; const float*b=K->bx+(size_t)t*6;
        const float dx=fmaxf(fmaxf(b[0]-p[0],p[0]-b[3]),0.f), dy=fmaxf(fmaxf(b[1]-p[1],p[1]-b[4]),0.f), dz=fmaxf(fmaxf(b[2]-p[2],p[2]-b[5]),0.f);
        if(dx*dx+dy*dy+dz*dz>=best) continue;
        const float*v=K->v+(size_t)t*9; float u,vv,w; skClosestTri(p,v,v+3,v+6,&u,&vv,&w);
        float qq[3]; for(int k=0;k<3;k++) qq[k]=u*v[k]+vv*v[3+k]+w*v[6+k];
        const float d2=(p[0]-qq[0])*(p[0]-qq[0])+(p[1]-qq[1])*(p[1]-qq[1])+(p[2]-qq[2])*(p[2]-qq[2]);
        if(d2<best){ best=d2; bt=t; bu=u; bv=vv; bw=w; memcpy(bq,qq,12); } }
    if(bt<0) return 0;
    const float*nn=K->n+(size_t)bt*21, *fn;
    const int zu=(bu==0.f), zv=(bv==0.f), zw=(bw==0.f);
    if(zu+zv+zw>=2) fn=(bu>0.f)?nn+12:(bv>0.f)?nn+15:nn+18;   /* a corner */
    else if(zw) fn=nn+3;                                     /* the edge ab */
    else if(zu) fn=nn+6;                                     /* bc */
    else if(zv) fn=nn+9;                                     /* ca */
    else fn=nn;                                              /* the face */
    const float d[3]={p[0]-bq[0],p[1]-bq[1],p[2]-bq[2]}, dl=sqrtf(best), s=d[0]*fn[0]+d[1]*fn[1]+d[2]*fn[2];
    memcpy(q,bq,12);
    if(dl>1e-7f){ const float sg=(s>=0.f)?1.f:-1.f; for(int k=0;k<3;k++) n[k]=d[k]/dl*sg; *sd=sg*dl; }
    else { memcpy(n,fn,12); *sd=0.f; }
    return 1; }
/* is a point (a piece's own space) inside it or nearer than r to its surface */
static int sksBlocked(const SkSolid*K,const float*p,float r){
    int c[3]; for(int k=0;k<3;k++){ c[k]=(int)floorf((p[k]-K->lo[k])/K->cell); if(c[k]<0||c[k]>=K->g[k]) return 0; }
    float q[3],n[3],sd; if(sksNear(K,p,SKS_R,q,n,&sd)) return sd<r;
    return K->in[((long long)c[2]*K->g[1]+c[1])*K->g[0]+c[0]]; }
/* the solid pieces near a cloth whose points are in lo..hi (the world) this frame: where each was last frame (if it was near then) and is now */
static void skSetNear(Skirt*S,const float*lo9,const float*hi9,float pad){
    int on=S->nsn; static struct { int ks, inst; float M1[16]; } old[SKS_NEAR];
    for(int k=0;k<on;k++){ old[k].ks=S->sn[k].ks; old[k].inst=S->sn[k].inst; memcpy(old[k].M1,S->sn[k].M1,64); }
    S->nsn=0;
    if(g_skSet<0){ const char*e=getenv("REVA_SKSET"); g_skSet=!(e&&e[0]=='0');
        printf("[skirt] the set under the hanging cloth: %s\n",g_skSet?"ON -- a solid prop or mover near it is solid to it (REVA_SKSET=0 is the control)":"OFF (REVA_SKSET=0)"); fflush(stdout); }
    if(!g_skSet||g_setNI<=0) return;
    float lo[3],hi[3],mid[3]; for(int k=0;k<3;k++){ lo[k]=lo9[k]-pad; hi[k]=hi9[k]+pad; mid[k]=0.5f*(lo[k]+hi[k]); }
    const float span=sqrtf((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2]));
    for(int i=0;i<g_setNI&&S->nsn<SKS_NEAR;i++){ const SetInst*I=&g_setI[i]; if(I->hid||I->room||I->fade>0.5f) continue;
        const Pack*Q=&g_setPk[I->pk]; if(!Q->pk||!Q->nMeshes) continue;
        /* a first look by its origin, before its triangles are ever read: no piece of furniture is 8 units across */
        const float ox=I->m[12]-mid[0], oy=I->m[13]-mid[1], oz=I->m[14]-mid[2];
        const float sx=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]);
        if(sqrtf(ox*ox+oy*oy+oz*oz)>span+8.f*fmaxf(sx,1e-3f)) continue;
        const int ks=sksGet(Q); if(ks<0||!g_sks[ks].solid) continue;
        const SkSolid*K=&g_sks[ks];
        float wl[3]={1e30f,1e30f,1e30f}, wh[3]={-1e30f,-1e30f,-1e30f};
        for(int c=0;c<8;c++){ const float pc[3]={(c&1)?K->hi[0]:K->lo[0],(c&2)?K->hi[1]:K->lo[1],(c&4)?K->hi[2]:K->lo[2]}; float w[3]; skXf(I->m,pc,w);
            for(int k=0;k<3;k++){ if(w[k]<wl[k]) wl[k]=w[k]; if(w[k]>wh[k]) wh[k]=w[k]; } }
        if(wh[0]<lo[0]||wl[0]>hi[0]||wh[1]<lo[1]||wl[1]>hi[1]||wh[2]<lo[2]||wl[2]>hi[2]) continue;
        int k9=S->nsn; S->sn[k9].ks=ks; S->sn[k9].inst=i; memcpy(S->sn[k9].M1,I->m,64);
        if(!skInv(I->m,S->sn[k9].Mi1)) continue;
        memcpy(S->sn[k9].M0,I->m,64);
        for(int k=0;k<on;k++) if(old[k].inst==i&&old[k].ks==ks){ memcpy(S->sn[k9].M0,old[k].M1,64); break; }
        if(!skInv(S->sn[k9].M0,S->sn[k9].Mi0)) memcpy(S->sn[k9].Mi0,S->sn[k9].Mi1,64);
        S->sn[k9].still=!memcmp(S->sn[k9].M0,S->sn[k9].M1,64);
        { const float d=fabsf(I->m[0]*(I->m[5]*I->m[10]-I->m[6]*I->m[9])-I->m[4]*(I->m[1]*I->m[10]-I->m[2]*I->m[9])+I->m[8]*(I->m[1]*I->m[6]-I->m[2]*I->m[5]));
          S->sn[k9].k=cbrtf(d>1e-12f?d:1e-12f); }
        S->nsn++;
        { int said=0; for(int s=0;s<S->nSetSaid;s++) if(S->setSaid[s]==i){ said=1; break; }
          if(!said&&S->nSetSaid<(int)(sizeof S->setSaid/sizeof S->setSaid[0])){ S->setSaid[S->nSetSaid++]=i;
              printf("[skirt] %s: its cloth lies on the set's %s (%d triangles, a %s)\n",S->pk->label,g_setNm[I->pk],K->nt,I->mover>=0?"mover":"prop"); fflush(stdout); } } } }

/* ONE FRAME: N substeps; the waistband, the hips' frame and the body's capsules all interpolated through it */
/* "in toward the body": the world-horizontal line from the hips' centre (a STANDING body's radial). A LYING body does
 * not use it -- see the hand pass. */
static int skRadial(const float*x,const float*hc,float*m){
    float mx=x[0]-hc[0], mz=x[2]-hc[2], ml=sqrtf(mx*mx+mz*mz); if(ml<1e-6f) return 0;
    m[0]=mx/ml; m[1]=0.f; m[2]=mz/ml; return 1; }
/* (11-16) WHAT PUSHED IT, layer by layer, over a frame's substeps (skReport says them): the floor, the set, the hips, the band, the hands, the skin,
 * the legs: how many pushes, the largest, the sum, and the point the largest moved */
/* (11-16) is a point inside one of the legs (this substep's capsules, `lg`: A(3) B(3) rA rB), short of its own rest allowance */
static int skInLegs(const float (*lg)[8],int nlg,const float*x,float allow){
    for(int c=0;c<nlg;c++){ const float*A=lg[c], *B=lg[c]+3; const float e[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, e2=e[0]*e[0]+e[1]*e[1]+e[2]*e[2];
        float t=(e2>1e-12f)?((x[0]-A[0])*e[0]+(x[1]-A[1])*e[1]+(x[2]-A[2])*e[2])/e2:0.f; if(t<0.f) t=0.f; if(t>1.f) t=1.f;
        const float dx=x[0]-(A[0]+e[0]*t), dy=x[1]-(A[1]+e[1]*t), dz=x[2]-(A[2]+e[2]*t), r=lg[c][6]+(lg[c][7]-lg[c][6])*t-allow;
        if(dx*dx+dy*dy+dz*dz<r*r) return 1; }
    return 0; }
#define SKL_N 7
static int g_skLN[SKL_N], g_skLP[SKL_N]; static float g_skLM[SKL_N]; static double g_skLS[SKL_N];
static const char*const g_skLNm[SKL_N]={"floor","set","hips","band","hands","skin","legs"};
#define SKLAYER(l,amt,pt) do{ const float a9_=(amt); g_skLN[l]++; g_skLS[l]+=a9_; if(a9_>g_skLM[l]){ g_skLM[l]=a9_; g_skLP[l]=(pt); } }while(0)
static void skLayersClear(void){ for(int l=0;l<SKL_N;l++){ g_skLN[l]=0; g_skLP[l]=-1; g_skLM[l]=0.f; g_skLS[l]=0.0; } }
static void skirtStep(Skirt*S,float dtFull,float sc,float cap1[][SKC],int ncap,const float*H1,const float*air){
    Pack*P9=S->pk;
    /* (11-16) a recording at 10 frames a second steps its whole 0.1 s, in as many substeps as it needs: at most 0.034 s of it was stepped, and the
     * cloth moved at a third of her speed in every such recording. At 30 and 60 frames a second nothing has changed (32 substeps at the most there) */
    if(dtFull>0.105f) dtFull=0.105f;
    if(dtFull<1e-4f) return;
    int N=(int)ceilf(dtFull/g_skSub); if(N<2) N=2; if(N>32&&dtFull<=0.0341f) N=32; if(N>104) N=104;
    float h=dtFull/(float)N, gy=9.81f*sc*h*h;
    float damp=expf(-0.4f*h);                     /* still air: a small loss */
    /* 1/2 rho Cd / (mass per area), per world unit: twill DOUBLE-WALLED (the outer and its lining, ~450 g/m^2), Cd ~1.
     * 3.6 (one layer) blew the coat out nearly level behind her at a 3.5 m/s run -- a cape in a gale; 1.3 trails it
     * at ~45 degrees, the coat of someone running */
    float cq=g_skDrag/sc;
    float aB=g_skBend/(h*h);
    int np=S->np, nc=(ncap<S->ncap0)?ncap:S->ncap0;
    int contacts=0; float deep=0.f;
    /* ⭐ HEAVY AND CALM (11-16: the first cloth coat was far too energetic, with no weight to it, and it swayed in a wind the engine doesn't
     * simulate). A coat hung by its Hang_ nodes keeps only the swing her body gives it:
     * each substep its own motion RELATIVE TO HER HIPS (where the hips carry that point in the substep, from where they were) loses g_skCalm a second
     * (6: a swing's reach is a twentieth after a second, so it settles within a beat or two), while whatever her hips do it does with them. A heavy
     * twill's own swing dies fast and it never drifts; the air only resists its motion through still air (no wind of her run: skirtPrep).
     * REVA_SKCALM=<per second> sets it, 0 the old small loss */
    static float calm9=-1.f; if(calm9<0.f){ const char*e9=getenv("REVA_SKCALM"); calm9=(e9&&*e9)?(float)atof(e9):6.f; if(calm9<0.f) calm9=0.f; }
    /* ⭐ ITS OWN SHAPE (11-16, a fast spin: the calm above damps a point toward HER HIPS' TURN AT THE POINT ITSELF, so a coat already swung
     * out was turned with her at its swung-out reach, and a fast turn spun it up into a bell; at calm 6 the back hem lifted off her thighs). A hung
     * coat keeps the shape it was cut in: each free point is pulled toward where the cut puts it carried rigidly by her HIPS (pr through the hips'
     * frame: the coat as tailored, not as her thighs skin it), at g_skShape a second (the pull's own frequency, k = w^2), damped toward that
     * place's own motion (2 x g_skShapeZ x w more a second, on top of the calm), and no harder than g_skShapeMax metres of pull: past that her
     * legs, her hands and the set push it where they push it and it only leans back. Her legs, hands, skin and the set still have the last word
     * each substep. REVA_SKSHAPE=<per second> (0: off, the calm alone), REVA_SKSHAPEZ, REVA_SKSHAPEMAX=<m>. The fast spin
     * ([skirtshape] in REVA_SKIRTDBG): off, the hem 45 cm from its cut and 36 cm above it, her seat bare; at 20, 17 cm and 6; at 30, 12 and 2.5,
     * hanging as the old rigid coat did a tenth of a second after the turn. 30 it is */
    static float shapeW9=-1.f, shapeZ9=0.7f, shapeM9=0.10f;
    if(shapeW9<0.f){ const char*e9=getenv("REVA_SKSHAPE"); shapeW9=(e9&&*e9)?(float)atof(e9):20.f;   /* (11-16) 20 over 30, chosen by eye: a little sway on a turn, then it settles */ if(shapeW9<0.f) shapeW9=0.f;
        e9=getenv("REVA_SKSHAPEZ"); if(e9&&*e9) shapeZ9=(float)atof(e9); if(shapeZ9<0.f) shapeZ9=0.f;
        e9=getenv("REVA_SKSHAPEMAX"); if(e9&&*e9) shapeM9=(float)atof(e9); if(shapeM9<0.001f) shapeM9=0.001f;
        printf("[skirt] a hung coat's own shape: %s (%.1f a second, damping %.2f of critical, no harder than %.0f mm of pull; REVA_SKSHAPE=0 is the calm alone)\n",
               shapeW9>0.f?"ON":"OFF",shapeW9,shapeZ9,shapeM9*1000.f); fflush(stdout); }
    const int shp9=S->hang&&shapeW9>0.f;
    const float kS9=shapeW9*shapeW9*h*h, capS9=shapeM9*sc;
    const float dampH=expf(-(0.4f+(S->hang?calm9:0.f)+(shp9?2.f*shapeZ9*shapeW9:0.f))*h);
    for(int s=0;s<N;s++){
        float a0=(float)s/(float)N, a1=(float)(s+1)/(float)N;
        float Cm[16]; int carry9=0;   /* the hips' own motion this substep: where the hips carry a point from its place at the substep's start */
        float dd9[3]={0.f,0.f,0.f};   /* (11-16) and how much the hips' own going changed since the last substep: the carry's jolt */
        float Ha[16],Hb[16]; int hab9=0;   /* the hips at the substep's start and end (rest -> world): ITS OWN SHAPE's places too */
        if(S->hang&&(calm9>0.f||shp9)){ float Hai[16]; for(int k=0;k<16;k++){ Ha[k]=S->H0[k]+(H1[k]-S->H0[k])*a0; Hb[k]=S->H0[k]+(H1[k]-S->H0[k])*a1; } hab9=1;
            if(skInv(Ha,Hai)){ m4_mul(Cm,Hb,Hai); carry9=1; }
            /* ⭐ (11-16) A CARRY IS FELT ONLY IN PART. A scene slid her 6.7 m/s and stopped her dead: the coat kept her speed
             * and flew out as she stopped. Her hips' going through the world is felt by the coat at g_skCarry of it (0.25), so a start or a stop
             * jolts it a quarter as hard; whatever her legs and hands do to it is felt whole. REVA_SKCARRY=1 is the whole of it, as before */
            { static float carryK9=-1.f; if(carryK9<0.f){ const char*e9=getenv("REVA_SKCARRY"); carryK9=(e9&&*e9)?(float)atof(e9):0.25f; if(carryK9<0.f) carryK9=0.f; if(carryK9>1.f) carryK9=1.f; }
              const float hc0[3]={S->ax,S->ay,S->az}; float oa[3], ob[3]; skXf(Ha,hc0,oa); skXf(Hb,hc0,ob);
              const float d0[3]={ob[0]-oa[0],ob[1]-oa[1],ob[2]-oa[2]};
              if(S->haveD0) for(int k=0;k<3;k++) dd9[k]=(1.f-carryK9)*(d0[k]-S->d0prev[k]);
              memcpy(S->d0prev,d0,12); S->haveD0=1; } }
        for(int p=0;p<np;p++){ if(S->twin[p]>=0) continue;
            if(S->pin[p]){ for(int k=0;k<3;k++){ S->prv[p][k]=S->tg0[p][k]+(S->tg1[p][k]-S->tg0[p][k])*a0;
                                                 S->cur[p][k]=S->tg0[p][k]+(S->tg1[p][k]-S->tg0[p][k])*a1; } continue; }
            float v[3], pull9[3]={0.f,0.f,0.f};
            if(shp9&&hab9){ float ta[3], tb[3]; skXf(Ha,S->pr[p],ta); skXf(Hb,S->pr[p],tb);   /* its cut, carried by her hips, at the substep's start and end */
                for(int k=0;k<3;k++){ const float vt=tb[k]-ta[k]; v[k]=vt+((S->cur[p][k]-S->prv[p][k])+dd9[k]-vt)*dampH; }
                const float dx=tb[0]-S->cur[p][0], dy=tb[1]-S->cur[p][1], dz=tb[2]-S->cur[p][2], dl=sqrtf(dx*dx+dy*dy+dz*dz);
                const float f=(dl>capS9)?kS9*capS9/dl:kS9; pull9[0]=dx*f; pull9[1]=dy*f; pull9[2]=dz*f; }
            else if(carry9){ float xc[3]; skXf(Cm,S->cur[p],xc);
                for(int k=0;k<3;k++){ const float vc=xc[k]-S->cur[p][k]; v[k]=vc+((S->cur[p][k]-S->prv[p][k])+dd9[k]-vc)*dampH; } }
            else for(int k=0;k<3;k++) v[k]=(S->cur[p][k]-S->prv[p][k])*damp;
            /* the drag is on the cloth's speed THROUGH THE AIR: on the spot, the air of her own run comes past her */
            { const float*n=S->nOut[p]; float vn=(v[0]-air[0]*h)*n[0]+(v[1]-air[1]*h)*n[1]+(v[2]-air[2]*h)*n[2];
              float keep=1.f/(1.f+cq*fabsf(vn));       /* implicit quadratic drag: v -> v/(1+cq|v|h), v in u per substep */
              for(int k=0;k<3;k++) v[k]-=n[k]*vn*(1.f-keep); }
            for(int k=0;k<3;k++) S->prv[p][k]=S->cur[p][k];
            S->cur[p][0]+=v[0]+pull9[0]; S->cur[p][1]+=v[1]-gy+pull9[1]; S->cur[p][2]+=v[2]+pull9[2]; }
        /* the cloth does not stretch */
        for(int e=0;e<S->ne;e++){ int i=S->e0[e], j=S->e1[e];
            float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[j][k]-S->cur[i][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-9f) continue;
            float wi=S->pin[i]?0.f:1.f, wj=S->pin[j]?0.f:1.f, ws=wi+wj; if(ws<=0.f) continue;
            float c=(L-S->elen[e]*sc)/(L*ws);
            for(int k=0;k<3;k++){ S->cur[i][k]+=wi*c*d[k]; S->cur[j][k]-=wj*c*d[k]; } }
        /* it resists folding, softly */
        for(int e=0;e<S->nb;e++){ int i=S->b0[e], j=S->b1[e];
            float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[j][k]-S->cur[i][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-9f) continue;
            float wi=S->pin[i]?0.f:1.f, wj=S->pin[j]?0.f:1.f, ws=wi+wj; if(ws<=0.f) continue;
            float dl=-(L-S->blen[e]*sc)/(ws+aB);
            for(int k=0;k<3;k++){ float n=d[k]/L; S->cur[i][k]-=wi*n*dl; S->cur[j][k]+=wj*n*dl; } }
        /* and a hem is never further from the waistband than its cloth reaches */
        for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; int a=S->anchor[p]; if(a<0) continue;
            float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[p][k]-S->cur[a][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]), Lm=S->tlen[p]*sc;
            if(L>Lm&&L>1e-9f){ float f=Lm/L; for(int k=0;k<3;k++) S->cur[p][k]=S->cur[a][k]+d[k]*f; } }
        /* the hips' frame at this instant */
        float Hm[16]; for(int k=0;k<16;k++) Hm[k]=S->H0[k]+(H1[k]-S->H0[k])*a1;
        /* out of the body, where the body is at this instant -- last, so it wins. Four passes, in this order:
         *  0 THE HIPS HOLD IT OUT: in the hips' rest frame, never nearer their axis than it was cut,
         *  1 THE HANDS press it IN toward the body (an arm is outside the skirt), down to the skin under a pelvis
         *    point and no further -- after the hips, so a hand on the hip wins over the cut; the old arm capsules
         *    (REVA_SKARMS) run here too, opt-in,
         *  2 THE SKIN UNDER IT: off the nearest back-facing skin point's tangent plane by the undersuit and the wall,
         *  3 THE LEGS -- each pushes the cloth out of itself the shortest way.
         * Every push is a MOVE, never a throw: the velocity it would add off the surface is taken back out. */
        /* ⭐ THE FLOOR (10-18): nothing goes below the plane the body stands or lies on -- a boy's shift in a warming
         * pan falls onto the pan and stays there. Inelastic, with the same friction as every contact. FIRST, so the
         * body's passes have the last word: where the cloth is SQUEEZED between a body lying on it and the floor, it
         * goes out of the body (and under the floor, where nobody sees it) instead of fighting both every substep. */
        { float fy=S->floorY+0.002f*sc;
          for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]||S->cur[p][1]>=fy) continue;
            float pen=fy-S->cur[p][1]; S->cur[p][1]=fy; SKLAYER(0,pen,p);
            float rx=S->cur[p][0]-S->prv[p][0], rz=S->cur[p][2]-S->prv[p][2], lt=sqrtf(rx*rx+rz*rz);
            if(lt>1e-9f){ float f=(lt<g_skMuS*pen)?1.f:fminf(g_skMuK*pen/lt,1.f); S->cur[p][0]-=rx*f; S->cur[p][2]-=rz*f; }
            if(S->cur[p][1]>S->prv[p][1]) S->prv[p][1]=S->cur[p][1];
            contacts++; } }
        /* ⭐ THE SET UNDER IT (11-16): out of the solid pieces near it (skSetNear), by the nearest surface, inelastic, with friction against the
         * piece's own motion; a point deeper than SKS_DEEP is her body's doing and stays. Before her body's passes, so hers is the last word */
        if(S->nsn){ const float clr=(0.004f+S->tmed)*sc, deepW=SKS_DEEP*sc;
          for(int k9=0;k9<S->nsn;k9++){ const SkSolid*K=&g_sks[S->sn[k9].ks]; const float*M0=S->sn[k9].M0, *M1=S->sn[k9].M1;
            float Ma[16], Mb[16], Mi[16];                   /* the piece at this substep's start and end, and the end's inverse */
            if(S->sn[k9].still){ memcpy(Ma,M1,64); memcpy(Mb,M1,64); memcpy(Mi,S->sn[k9].Mi1,64); }
            else { for(int i=0;i<16;i++){ Ma[i]=M0[i]+(M1[i]-M0[i])*a0; Mb[i]=M0[i]+(M1[i]-M0[i])*a1; } if(!skInv(Mb,Mi)) continue; }
            const float ik=1.f/S->sn[k9].k, clrL=clr*ik; float rL=(clr+deepW)*ik; if(rL>SKS_R) rL=SKS_R;
            const float deepL=rL-clrL;
            for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; float*x=S->cur[p];
                float xl[3]; skXf(Mi,x,xl);
                if(xl[0]<K->lo[0]||xl[0]>K->hi[0]||xl[1]<K->lo[1]||xl[1]>K->hi[1]||xl[2]<K->lo[2]||xl[2]>K->hi[2]) continue;
                float q[3],nq[3],sd; if(!sksNear(K,xl,rL,q,nq,&sd)||sd>=clrL) continue;
                if(sd<-deepL){ if(s==N-1) S->setDeep++; continue; }   /* squeezed in by her: out of sight, and hers (counted on the last substep) */
                float yl[3]; for(int k=0;k<3;k++) yl[k]=q[k]+nq[k]*clrL;
                const float xo[3]={x[0],x[1],x[2]}; skXf(Mb,yl,x);
                float n[3]={Mb[0]*nq[0]+Mb[4]*nq[1]+Mb[8]*nq[2],Mb[1]*nq[0]+Mb[5]*nq[1]+Mb[9]*nq[2],Mb[2]*nq[0]+Mb[6]*nq[1]+Mb[10]*nq[2]};
                { const float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(l<1e-12f) continue; n[0]/=l; n[1]/=l; n[2]/=l; }
                const float pen=sqrtf((x[0]-xo[0])*(x[0]-xo[0])+(x[1]-xo[1])*(x[1]-xo[1])+(x[2]-xo[2])*(x[2]-xo[2]));
                float qa[3],qb[3],dm[3]; skXf(Ma,q,qa); skXf(Mb,q,qb); for(int k=0;k<3;k++) dm[k]=qb[k]-qa[k];   /* the surface under it moved this */
                float rel[3]; for(int k=0;k<3;k++) rel[k]=(x[k]-S->prv[p][k])-dm[k];
                const float rn=rel[0]*n[0]+rel[1]*n[1]+rel[2]*n[2];
                float rt[3]={rel[0]-n[0]*rn,rel[1]-n[1]*rn,rel[2]-n[2]*rn}; const float lt=sqrtf(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]);
                if(lt>1e-9f){ const float f=(lt<g_skMuS*pen)?1.f:fminf(g_skMuK*pen/lt,1.f); for(int k=0;k<3;k++) x[k]-=rt[k]*f; }
                { float r2[3]; for(int k=0;k<3;k++) r2[k]=(x[k]-S->prv[p][k])-dm[k];
                  const float vn=r2[0]*n[0]+r2[1]*n[1]+r2[2]*n[2]; if(vn>0.f) for(int k=0;k<3;k++) S->prv[p][k]+=n[k]*vn; }
                if(s==N-1){ S->setContacts++; } SKLAYER(1,pen,p);
                contacts++; if(pen>S->setPen) S->setPen=pen; } } }
        /* (11-16) this substep's legs, for a hand that mustn't squeeze cloth into one (below) */
        float lg9[SK_NCAP][8]; int nlg9=0;
        if(S->hang) for(int c=0;c<nc;c++){ if(((int)cap1[c][7])%6>=3) continue;
            for(int k=0;k<6;k++) lg9[nlg9][k]=S->cap0[c][k]+(cap1[c][k]-S->cap0[c][k])*a1;
            lg9[nlg9][6]=cap1[c][6]; lg9[nlg9][7]=cap1[c][8]; nlg9++; }
        for(int pass=0;pass<4;pass++){
          if(pass==2){
            if(S->nbv&&g_skSkin){ float rad=0.05f*sc, clr=(0.008f+S->tmed)*sc, inv=1.f/rad;
                static int head[4096]; static int nxt[SK_BODYMAX]; static float bpI[SK_BODYMAX][3];
                for(int hh9=0;hh9<4096;hh9++) head[hh9]=-1;
                for(int i=0;i<S->nbv;i++){
                    for(int k=0;k<3;k++) bpI[i][k]=S->bp0[i][k]+(S->bp1[i][k]-S->bp0[i][k])*a1;
                    int cx=(int)floorf(bpI[i][0]*inv), cy=(int)floorf(bpI[i][1]*inv), cz=(int)floorf(bpI[i][2]*inv);
                    unsigned hh9=((unsigned)cx*73856093u^(unsigned)cy*19349663u^(unsigned)cz*83492791u)&4095u; nxt[i]=head[hh9]; head[hh9]=i; }
                for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]||!S->back[p]) continue; float*x=S->cur[p];
                    int cx=(int)floorf(x[0]*inv), cy=(int)floorf(x[1]*inv), cz=(int)floorf(x[2]*inv), best=-1; float bd=rad*rad;
                    for(int dz=-1;dz<=1;dz++) for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++){
                        unsigned hh9=((unsigned)(cx+dx)*73856093u^(unsigned)(cy+dy)*19349663u^(unsigned)(cz+dz)*83492791u)&4095u;
                        for(int i=head[hh9];i>=0;i=nxt[i]){ float ex=x[0]-bpI[i][0], ey=x[1]-bpI[i][1], ez=x[2]-bpI[i][2], d2=ex*ex+ey*ey+ez*ez;
                            if(d2<bd){ bd=d2; best=i; } } }
                    if(best<0) continue;
                    const float*bn=S->bn1[best];
                    float d=(x[0]-bpI[best][0])*bn[0]+(x[1]-bpI[best][1])*bn[1]+(x[2]-bpI[best][2])*bn[2];
                    float clrP=clr-S->allowS[p]*sc; if(d>=clrP) continue;
                    if(d<clrP-1.5f*clr) continue;   /* deep under one point's plane is a wrong plane, not a contact */
                    float push=clrP-d;
                    { static int dbg9=-1, said9=0; if(dbg9<0){ const char*e=getenv("REVA_SKIRTDBG"); dbg9=(e&&e[0]=='2'); }
                      if(dbg9&&push>0.015f*sc&&said9<40){ said9++; PMesh*Mb=&P9->meshes[S->bvm[best]];
                        printf("[skirtdbg] skin push %.1f mm: point %d (rest y %.3f, pin %d cone %d) <- skin '%s' v%u at (%.3f %.3f %.3f) n (%.2f %.2f %.2f) d %.1f mm\n",
                               push/sc*1000.f,p,S->pr[p][1],S->pin[p],S->cone[p],packStr(P9,Mb->name),S->bvv[best],
                               bpI[best][0],bpI[best][1],bpI[best][2],bn[0],bn[1],bn[2],d/sc*1000.f); } }
                    { for(int k=0;k<3;k++) x[k]+=bn[k]*push; } SKLAYER(5,push,p);
                    /* inelastic, as below: put back on the skin, never thrown off it */
                    float r2[3]; for(int k=0;k<3;k++) r2[k]=(x[k]-S->prv[p][k])-(S->bp1[best][k]-S->bp0[best][k])/(float)N;
                    float vn=r2[0]*bn[0]+r2[1]*bn[1]+r2[2]*bn[2]; if(vn>0.f) for(int k=0;k<3;k++) S->prv[p][k]+=bn[k]*vn;
                    contacts++; if(push>deep) deep=push; } }
            continue; }
          if(pass==0){
            if(S->ncone&&g_skCone){ float Hi[16];
                if(skInv(Hm,Hi)) for(int p=0;p<np;p++){ if(!S->cone[p]) continue;
                    float r[3]; skXf(Hi,S->cur[p],r);
                    float rx=r[0]-S->ax, rz=r[2]-S->az, rad=sqrtf(rx*rx+rz*rz), want=S->rr[p]*0.985f;
                    if(rad<want&&rad>1e-6f){ float f=want/rad, xo[3]={S->cur[p][0],S->cur[p][1],S->cur[p][2]};
                        r[0]=S->ax+rx*f; r[2]=S->az+rz*f; skXf(Hm,r,S->cur[p]);
                        /* a move, not a throw: and it no longer heads INTO her */
                        float dn[3]={S->cur[p][0]-xo[0],S->cur[p][1]-xo[1],S->cur[p][2]-xo[2]}, dl=sqrtf(dn[0]*dn[0]+dn[1]*dn[1]+dn[2]*dn[2]);
                        { for(int k=0;k<3;k++) S->prv[p][k]+=dn[k]; } SKLAYER(2,dl,p);
                        if(dl>1e-9f){ float vel[3]={S->cur[p][0]-S->prv[p][0],S->cur[p][1]-S->prv[p][1],S->cur[p][2]-S->prv[p][2]};
                            float vn=(vel[0]*dn[0]+vel[1]*dn[1]+vel[2]*dn[2])/dl; if(vn<0.f) for(int k=0;k<3;k++) S->prv[p][k]+=dn[k]/dl*vn; } } } }
            /* ⭐ NEVER ABOVE ITS OWN BAND (11-04). A knee lifts a hem, and a long coat's hem never gets near its waist. A mini skirt's does: in a deep
             * crouch the thighs come up level with its band, the cloth goes out the nearest way, which is up, and nothing brings it back: it sits
             * round REVA's ribs as a tube (REVA_SKBAND=0 shows it; what blew out across the room was the renderer's fault, below). So in the hips'
             * rest frame no free point of a band-hung skirt rises above where its band ends. Only skirts hung by
             * the top-tenth rule take this (no Cloth_ weights), so no garment film has shot is touched. REVA_SKBAND=0 is the control */
            { static int bandOn=-1; if(bandOn<0){ const char*e9=getenv("REVA_SKBAND"); bandOn=!(e9&&e9[0]=='0'); }
              if(S->band&&bandOn){ float Hi[16];
                if(skInv(Hm,Hi)) for(int p=0;p<np;p++){ if(S->pin[p]||S->twin[p]>=0) continue;
                    float r[3]; skXf(Hi,S->cur[p],r);
                    if(r[1]>S->bandY){ float xo[3]={S->cur[p][0],S->cur[p][1],S->cur[p][2]};
                        r[1]=S->bandY; skXf(Hm,r,S->cur[p]);
                        { for(int k=0;k<3;k++) S->prv[p][k]+=S->cur[p][k]-xo[k]; } SKLAYER(3,fabsf(S->cur[p][1]-xo[1]),p); } } } }   /* a move, not a throw */
            continue; }
          if(pass==1&&S->nhv&&g_skHands){ float rh=(0.006f+S->tmed)*sc;      /* off the hand's own skin: a hair and the wall */
            static float hpI[SK_HANDMAX][3]; float lo[3]={1e9f,1e9f,1e9f}, hi[3]={-1e9f,-1e9f,-1e9f};
            for(int i=0;i<S->nhv;i++) for(int k=0;k<3;k++){ hpI[i][k]=S->hp0[i][k]+(S->hp1[i][k]-S->hp0[i][k])*a1;
                if(hpI[i][k]-rh<lo[k]) lo[k]=hpI[i][k]-rh;
                if(hpI[i][k]+rh>hi[k]) hi[k]=hpI[i][k]+rh; }
            float hc[3]; { float h3[3]={S->ax,S->ay,S->az}; skXf(Hm,h3,hc); }
            float Hi[16]; int hasHi=skInv(Hm,Hi);
            /* ⭐ A LYING BODY'S HANDS ARE SOLID, NOT PRESSING (10-18). "Press the coat IN toward the body" is a hand on a
             * STANDING body's hip, gravity holding the cloth against it. A boy lying in a warming pan with his hands on the
             * pan beside his thighs is a different contact: along the world line it dragged his shift up toward his waist,
             * and along his own radial it drove the drape into his thighs against the leg capsules (stretch x8). Lying (the
             * hips' up more than 60 degrees off the world's), cloth goes OUT of the hand the shortest way. */
            int lying9=0; { float hl=sqrtf(Hm[4]*Hm[4]+Hm[5]*Hm[5]+Hm[6]*Hm[6]); if(hl>1e-9f&&fabsf(Hm[5]/hl)<0.5f) lying9=1; }
            /* ⛔ (11-16) A COAT OPEN DOWN ITS FRONT HAS HER HANDS INSIDE IT AS OFTEN AS OUT (REVA's lab coat: a hand at her hip,
             * between the front panel and her). Pressed IN toward her, the panel was carried through her hand to its far side and flung about by it
             * every substep: with the hands off (REVA_SKHANDS=0) the coat hung still (0.5 cm from where it hangs, against 6 to 17).
             * A coat hung by its Hang_ nodes takes her hands as SOLID, as a lying body's are: out of a hand the shortest way, points and triangles */
            if(S->hang) lying9=1;
            for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; float*x=S->cur[p];
                if(x[0]<lo[0]||x[0]>hi[0]||x[1]<lo[1]||x[1]>hi[1]||x[2]<lo[2]||x[2]>hi[2]) continue;
                float m[3]={0.f,1.f,0.f}; if(!lying9&&!skRadial(x,hc,m)) continue;
                float xo[3]={x[0],x[1],x[2]}; int hit=0;
                for(int i=0;i<S->nhv;i++){
                    float d[3]={x[0]-hpI[i][0],x[1]-hpI[i][1],x[2]-hpI[i][2]}, d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
                    if(d2>=rh*rh) continue;
                    if(lying9){ float dl=sqrtf(d2), nn[3]={0.f,1.f,0.f};       /* out of the hand, the shortest way */
                        if(dl>1e-6f){ nn[0]=d[0]/dl; nn[1]=d[1]/dl; nn[2]=d[2]/dl; }
                        /* ⛔ (11-16) A HAND NEVER SCOOPS A HUNG COAT UP: rising under its side panel (her hand hangs half inside the cut at her hip) it
                         * carried the hem to her waist. It moves the cloth aside, level, and passes up or down through it */
                        if(S->hang){ const float hl9=sqrtf(nn[0]*nn[0]+nn[2]*nn[2]); if(hl9<0.25f) continue; nn[0]/=hl9; nn[1]=0.f; nn[2]/=hl9; }
                        for(int k=0;k<3;k++) x[k]+=nn[k]*(rh-dl);
                        hit=1; continue; }
                    float dm=d[0]*m[0]+d[2]*m[2], s2=dm+sqrtf(fmaxf(rh*rh-(d2-dm*dm),0.f));   /* in along the radial, out of the sphere's far side */
                    if(s2>0.f){ x[0]-=m[0]*s2; x[2]-=m[2]*s2; hit=1; } }
                if(!hit) continue;
                /* ⛔ (11-16) SQUEEZED BETWEEN HER HAND AND HER LEG, THE LEG WINS AND THE HAND GIVES WAY (REVA's lab coat: her left hand
                 * against her left thigh with the coat between, the hand pushing the cloth into the leg and the leg back into the hand every substep,
                 * 4 to 6 metres of pushes a frame, until the front corners flew up). A hand never puts cloth inside a leg: there it leaves it, and
                 * sinks into the cloth as a hand pressing it to a thigh does */
                if(nlg9&&skInLegs((const float(*)[8])lg9,nlg9,x,S->allowC[p]*sc)){ memcpy(x,xo,12); continue; }
                if(!lying9&&S->cone[p]&&hasHi){ float r[3]; skXf(Hi,x,r);        /* no deeper than the body under it */
                    float rx=r[0]-S->ax, rz=r[2]-S->az, rad=sqrtf(rx*rx+rz*rz);
                    if(rad<S->floorR[p]&&rad>1e-6f){ float f=S->floorR[p]/rad; r[0]=S->ax+rx*f; r[2]=S->az+rz*f; skXf(Hm,r,x); } }
                float dn[3]={x[0]-xo[0],x[1]-xo[1],x[2]-xo[2]}, dl=sqrtf(dn[0]*dn[0]+dn[1]*dn[1]+dn[2]*dn[2]);
                { for(int k=0;k<3;k++) S->prv[p][k]+=dn[k]; } SKLAYER(4,dl,p);           /* moved, not thrown */
                if(dl>1e-9f){ float vel[3]={x[0]-S->prv[p][0],x[1]-S->prv[p][1],x[2]-S->prv[p][2]};
                    float vn=(vel[0]*dn[0]+vel[1]*dn[1]+vel[2]*dn[2])/dl; if(vn<0.f) for(int k=0;k<3;k++) S->prv[p][k]+=dn[k]/dl*vn; }
                contacts++; if(dl>deep) deep=dl; }
            /* ⭐ AND THE HAND'S SKIN AGAINST THE CLOTH'S TRIANGLES (10-18). A finger is thinner than the gap between two
             * cloth points (2-3 cm): pushed only point by point, her fingertips came through the coat at the hip as
             * islands. Every hand point stays rh OUTSIDE (along the way out from the hips) any outer triangle it lies
             * over; the triangle's corners go in by their barycentric share, moved, not thrown. A hand point far BEHIND
             * a triangle (the open front's panel swinging past a hand in the gap) is not this contact. */
            if(!lying9) for(int t=0;t<S->nt;t++){ int ia=S->tri[t][0], ib=S->tri[t][1], ic=S->tri[t][2];
                float*A=S->cur[ia], *B=S->cur[ib], *C=S->cur[ic];
                float tl[3],tu[3]; for(int k=0;k<3;k++){ tl[k]=fminf(A[k],fminf(B[k],C[k]))-rh; tu[k]=fmaxf(A[k],fmaxf(B[k],C[k]))+rh; }
                if(tu[0]<lo[0]||tl[0]>hi[0]||tu[1]<lo[1]||tl[1]>hi[1]||tu[2]<lo[2]||tl[2]>hi[2]) continue;
                float iA=S->pin[ia]?0.f:1.f, iB=S->pin[ib]?0.f:1.f, iC=S->pin[ic]?0.f:1.f;
                if(iA+iB+iC<=0.f) continue;
                for(int i=0;i<S->nhv;i++){ const float*c=hpI[i];
                    if(c[0]<tl[0]||c[0]>tu[0]||c[1]<tl[1]||c[1]>tu[1]||c[2]<tl[2]||c[2]>tu[2]) continue;
                    float u,v,w; skClosestTri(c,A,B,C,&u,&v,&w);
                    float q[3]; for(int k=0;k<3;k++) q[k]=u*A[k]+v*B[k]+w*C[k];
                    float m[3]; if(!skRadial(q,hc,m)) continue;
                    float d[3]={c[0]-q[0],c[1]-q[1],c[2]-q[2]};
                    float sd=d[0]*m[0]+d[2]*m[2], dp2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2]-sd*sd;
                    if(dp2>rh*rh||sd>=rh||sd<-3.f*rh) continue;
                    float den=u*u*iA+v*v*iB+w*w*iC; if(den<1e-6f) continue;
                    float push=(rh-sd)/den, gA=u*iA*push, gB=v*iB*push, gC=w*iC*push;
                    for(int k=0;k<3;k++){ float dk=m[k];
                        A[k]-=dk*gA; S->prv[ia][k]-=dk*gA; B[k]-=dk*gB; S->prv[ib][k]-=dk*gB; C[k]-=dk*gC; S->prv[ic][k]-=dk*gC; } SKLAYER(4,fmaxf(gA,fmaxf(gB,gC)),ia);
                    contacts++; } }
            /* ⭐ A LYING BODY'S HAND AGAINST THE CLOTH'S TRIANGLES (10-18, second round). A man asleep in the
             * pan with his hands beside his thighs showed it. The lying hand pass pushed cloth POINTS out of the hand, and a finger is
             * thinner than the 2-3 cm between two of them: the hem of his shift ran through his fingers. Every hand point
             * now stays rh off any outer triangle near it, the triangle moved AWAY from it the shortest way -- over a hand
             * lying under the hem, under a hand resting on top -- by its corners' barycentric shares, moved, not thrown.
             * REVA_SKLYTRI=0 is the control. */
            { static int lt9=-1; if(lt9<0){ const char*e=getenv("REVA_SKLYTRI"); lt9=!(e&&e[0]=='0'); }
              if(lying9&&lt9) for(int t=0;t<S->nt;t++){ int ia=S->tri[t][0], ib=S->tri[t][1], ic=S->tri[t][2];
                float*A=S->cur[ia], *B=S->cur[ib], *C=S->cur[ic];
                float tl[3],tu[3]; for(int k=0;k<3;k++){ tl[k]=fminf(A[k],fminf(B[k],C[k]))-rh; tu[k]=fmaxf(A[k],fmaxf(B[k],C[k]))+rh; }
                if(tu[0]<lo[0]||tl[0]>hi[0]||tu[1]<lo[1]||tl[1]>hi[1]||tu[2]<lo[2]||tl[2]>hi[2]) continue;
                float iA=S->pin[ia]?0.f:1.f, iB=S->pin[ib]?0.f:1.f, iC=S->pin[ic]?0.f:1.f;
                if(iA+iB+iC<=0.f) continue;
                for(int i=0;i<S->nhv;i++){ const float*c=hpI[i];
                    if(c[0]<tl[0]||c[0]>tu[0]||c[1]<tl[1]||c[1]>tu[1]||c[2]<tl[2]||c[2]>tu[2]) continue;
                    float u,v,w; skClosestTri(c,A,B,C,&u,&v,&w);
                    float q[3]; for(int k=0;k<3;k++) q[k]=u*A[k]+v*B[k]+w*C[k];
                    float d[3]={q[0]-c[0],q[1]-c[1],q[2]-c[2]}, dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                    if(dl>=rh) continue;
                    float n[3];
                    if(dl>1e-7f){ n[0]=d[0]/dl; n[1]=d[1]/dl; n[2]=d[2]/dl; }
                    else { float e1[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, e2[3]={C[0]-A[0],C[1]-A[1],C[2]-A[2]};   /* ON it: up */
                        n[0]=e1[1]*e2[2]-e1[2]*e2[1]; n[1]=e1[2]*e2[0]-e1[0]*e2[2]; n[2]=e1[0]*e2[1]-e1[1]*e2[0];
                        float nl=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(nl<1e-12f) continue;
                        if(n[1]<0.f) nl=-nl;
                        for(int k=0;k<3;k++) n[k]/=nl; }
                    if(S->hang){ const float hl9=sqrtf(n[0]*n[0]+n[2]*n[2]); if(hl9<0.25f) continue; n[0]/=hl9; n[1]=0.f; n[2]/=hl9; }   /* (11-16) level, never up (above) */
                    float den=u*u*iA+v*v*iB+w*w*iC; if(den<1e-6f) continue;
                    float push=(rh-dl)/den, gA=u*iA*push, gB=v*iB*push, gC=w*iC*push;
                    if(nlg9){   /* (11-16) a corner the hand would put inside a leg stays where it is (the squeeze above) */
                        float t3[3]; { for(int k=0;k<3;k++) t3[k]=A[k]+n[k]*gA; } if(gA>0.f&&skInLegs((const float(*)[8])lg9,nlg9,t3,S->allowC[ia]*sc)) gA=0.f;
                        { for(int k=0;k<3;k++) t3[k]=B[k]+n[k]*gB; } if(gB>0.f&&skInLegs((const float(*)[8])lg9,nlg9,t3,S->allowC[ib]*sc)) gB=0.f;
                        { for(int k=0;k<3;k++) t3[k]=C[k]+n[k]*gC; } if(gC>0.f&&skInLegs((const float(*)[8])lg9,nlg9,t3,S->allowC[ic]*sc)) gC=0.f; }
                    for(int k=0;k<3;k++){ float dk=n[k];
                        A[k]+=dk*gA; S->prv[ia][k]+=dk*gA; B[k]+=dk*gB; S->prv[ib][k]+=dk*gB; C[k]+=dk*gC; S->prv[ic][k]+=dk*gC; } SKLAYER(4,fmaxf(gA,fmaxf(gB,gC)),ia);
                    contacts++; } } }
            /* and no corner deeper than the body under it */
            if(hasHi&&!lying9) for(int p=0;p<np;p++){ if(!S->cone[p]) continue; float r[3]; skXf(Hi,S->cur[p],r);
                float rx=r[0]-S->ax, rz=r[2]-S->az, rad=sqrtf(rx*rx+rz*rz);
                if(rad<S->floorR[p]&&rad>1e-6f){ float f=S->floorR[p]/rad, xo[3]={S->cur[p][0],S->cur[p][1],S->cur[p][2]};
                    r[0]=S->ax+rx*f; r[2]=S->az+rz*f; skXf(Hm,r,S->cur[p]);
                    for(int k=0;k<3;k++) S->prv[p][k]+=S->cur[p][k]-xo[k]; } } }
          if(pass==1&&!g_skArms) continue;
          for(int c=0;c<nc;c++){
            int arm=((int)cap1[c][7])%6>=3; if(arm!=(pass==1)) continue;   /* pass 1 the arm capsules (opt-in), pass 3 the legs */
            float A[3],B[3],Ap[3],Bp[3], r=fmaxf(cap1[c][6],cap1[c][8]);
            for(int k=0;k<3;k++){ A[k]=S->cap0[c][k]+(cap1[c][k]-S->cap0[c][k])*a1;   B[k]=S->cap0[c][3+k]+(cap1[c][3+k]-S->cap0[c][3+k])*a1;
                                  Ap[k]=S->cap0[c][k]+(cap1[c][k]-S->cap0[c][k])*a0;  Bp[k]=S->cap0[c][3+k]+(cap1[c][3+k]-S->cap0[c][3+k])*a0; }
            float e[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, e2=e[0]*e[0]+e[1]*e[1]+e[2]*e[2];
            float lo[3],hi[3]; for(int k=0;k<3;k++){ lo[k]=fminf(A[k],B[k])-r; hi[k]=fmaxf(A[k],B[k])+r; }
            for(int p=0;p<np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; float*x=S->cur[p];
                if(x[0]<lo[0]||x[0]>hi[0]||x[1]<lo[1]||x[1]>hi[1]||x[2]<lo[2]||x[2]>hi[2]) continue;
                float t=(e2>1e-12f)?((x[0]-A[0])*e[0]+(x[1]-A[1])*e[1]+(x[2]-A[2])*e[2])/e2:0.f;
                if(t<0.f) t=0.f;
                if(t>1.f) t=1.f;
                float q[3]={A[0]+e[0]*t,A[1]+e[1]*t,A[2]+e[2]*t};
                float d[3]={x[0]-q[0],x[1]-q[1],x[2]-q[2]}, dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                float rP=skCapR(cap1[c],t)-S->allowC[p]*sc; if(dl>=rP) continue;      /* its own rest allowance comes off */
                float n[3];
                if(dl>1e-7f){ n[0]=d[0]/dl; n[1]=d[1]/dl; n[2]=d[2]/dl; } else memcpy(n,S->nOut[p],12);
                float xo[3]={x[0],x[1],x[2]};
                for(int k=0;k<3;k++) x[k]=q[k]+n[k]*rP;
                float pen=sqrtf((x[0]-xo[0])*(x[0]-xo[0])+(x[1]-xo[1])*(x[1]-xo[1])+(x[2]-xo[2])*(x[2]-xo[2]));
                /* ⭐ FRICTION (Macklin et al. 2014): this substep's slide across the surface, against the surface's own
                 * motion under it, is cancelled while short against the push and shortened when long -- so the knee
                 * that lifts the cloth carries it */
                float dm[3]; for(int k=0;k<3;k++) dm[k]=q[k]-(Ap[k]+(Bp[k]-Ap[k])*t);   /* the surface under it moved this */
                float rel[3]; for(int k=0;k<3;k++) rel[k]=(x[k]-S->prv[p][k])-dm[k];
                float rn=rel[0]*n[0]+rel[1]*n[1]+rel[2]*n[2];
                float rt[3]={rel[0]-n[0]*rn,rel[1]-n[1]*rn,rel[2]-n[2]*rn}, lt=sqrtf(rt[0]*rt[0]+rt[1]*rt[1]+rt[2]*rt[2]);
                if(lt>1e-9f){ float f=(lt<g_skMuS*pen)?1.f:fminf(g_skMuK*pen/lt,1.f); for(int k=0;k<3;k++) x[k]-=rt[k]*f; }
                /* ⭐ INELASTIC: the push puts the cloth back ON the surface, it does not throw it. Whatever speed off the
                 * surface the correction gave it beyond the surface's own is taken back out of its velocity (prv moves
                 * with it): a 30 mm correction in a 2 ms substep is 15 m/s, and that flung the coat over her shoulders */
                { float r2[3]; for(int k=0;k<3;k++) r2[k]=(x[k]-S->prv[p][k])-dm[k];
                  float vn=r2[0]*n[0]+r2[1]*n[1]+r2[2]*n[2];
                  if(vn>0.f) for(int k=0;k<3;k++) S->prv[p][k]+=n[k]*vn; }
                contacts++; if(pen>deep) deep=pen; SKLAYER(6,pen,p); } } } }
    /* ⛔ OPEN (11-05): a short band-hung skirt after a deep crouch (REVA's 23 cm pleated mini in her skate push).
     * FOUND: the sheets the size of the room were never this simulation's. Its points said where they were (REVA_SKIRTDBG, "furthest past its tether")
     * and none was ever more than 3 cm past, in the same frames as a sheet. They were the renderer's: a part of a dynamic buffer written to a place its
     * frame's ring had since given to something else (gfx_vk.c, gfxBufferSub), so every hanging cloth was at risk of it; and a wall wound both ways
     * cancelled this cloth's normals (skirtInit, tdup). Both are fixed.
     * LEFT, and this cloth's own. With the skin's layer on, the crouch stands the skirt out round her hips like a tutu (or, with the band rule off, flips
     * it up round her ribs as a tube), and it stays there when she stands. With that layer off (REVA_SKSKIN=0) it drapes over her thighs in the crouch,
     * and comes back down ragged, caught up on one side with its hem uneven. Friction off (REVA_SKMU=0), hands off, arm capsules off and the cone off
     * change neither; the skin's layer makes no push deeper than 15 mm (REVA_SKIRTDBG=2 says none). Earlier, before the renderer's fault was known, bend
     * stiffness, a lining, re-hanging and holding points to their tethers were tried against the sheets, which proves nothing about this.
     * Her skirt rides her skin's own weights until it's right (build_reva.py --skirt-cloth hangs it instead). */
    /* ⛔ A POINT THAT WENT NON-FINITE is put back on its waistband anchor, and it is SAID: one NaN is a spike to the
     * edge of the frame (a triangle with a vertex at infinity), and a quiet fix would hide the pass that made it */
    { int bad=0; for(int p=0;p<np;p++){ if(S->twin[p]>=0) continue;
        if(isfinite(S->cur[p][0])&&isfinite(S->cur[p][1])&&isfinite(S->cur[p][2])) continue;
        int a=S->anchor[p]; const float*o=(a>=0&&isfinite(S->cur[a][1]))?S->cur[a]:S->tg1[0];
        memcpy(S->cur[p],o,12); memcpy(S->prv[p],o,12); bad++; }
      if(bad){ static int said9=0; if(said9++<8){ printf("[skirt] %s: %d point(s) went NON-FINITE this step -- put back on the waistband\n",P9->label,bad); fflush(stdout); } } }
    g_skContacts=contacts; g_skDeep=deep/sc;
    { float ws=0.f; S->worstE=-1; for(int e=0;e<S->ne;e++){ int i=S->e0[e], j=S->e1[e];
          float dx=S->cur[j][0]-S->cur[i][0], dy=S->cur[j][1]-S->cur[i][1], dz=S->cur[j][2]-S->cur[i][2];
          float r=sqrtf(dx*dx+dy*dy+dz*dz)/(S->elen[e]*sc+1e-12f); if(r>ws){ ws=r; S->worstE=e; } }
      g_skStretch=ws; }
    for(int p=0;p<np;p++) if(S->twin[p]<0&&S->pin[p]) memcpy(S->tg0[p],S->tg1[p],12);
    memcpy(S->cap0,cap1,sizeof(float)*SKC*(size_t)ncap); S->ncap0=ncap; memcpy(S->H0,H1,64); }

/* (11-16) is a hung cloth's cut, shifted, in a solid piece near it (or nearer one than r): how many of its points */
static int skHangBlocked(const Skirt*S,const float*H,const float*shift,float r){
    int bad=0;
    for(int p=0;p<S->np;p++){ if(S->twin[p]>=0) continue; float w[3]; skXf(H,S->pr[p],w); w[0]+=shift[0]; w[1]+=shift[1]; w[2]+=shift[2];
        for(int k=0;k<S->nsn;k++){ const SkSolid*K=&g_sks[S->sn[k].ks]; float l[3]; skXf(S->sn[k].Mi1,w,l);
            if(l[0]<K->lo[0]||l[0]>K->hi[0]||l[1]<K->lo[1]||l[1]>K->hi[1]||l[2]<K->lo[2]||l[2]>K->hi[2]) continue;
            if(sksBlocked(K,l,r/S->sn[k].k)){ bad++; break; } } }
    return bad; }
static int sewnPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world);   /* SEWN ON IT, below */
static void skReport(Skirt*S,Pack*P,unsigned mi,const float*pal,const float*model,M4*world,float sc);   /* below */
/* the clothPrep hook for a skirt: returns 2 when it drew the mesh from its own buffers (the caller binds g_skVbo) */
static int skirtPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world){
    if(g_skirtSim<0){ const char*e=getenv("REVA_SKIRTSIM"); g_skirtSim=!(e&&e[0]=='0');
        const char*b=getenv("REVA_SKBEND"); if(b&&*b) g_skBend=(float)atof(b);
        const char*a=getenv("REVA_SKARMS"); if(a&&*a) g_skArms=atoi(a);
        g_skDbg=getenv("REVA_SKIRTDBG")?1:0;
        { const char*o9=getenv("REVA_SKHANDS"); if(o9&&o9[0]=='0') g_skHands=0; o9=getenv("REVA_SKAIR"); if(o9&&o9[0]=='0') g_skAir=0;
          o9=getenv("REVA_SKDRAG"); if(o9&&*o9) g_skDrag=(float)atof(o9); o9=getenv("REVA_SKSUB"); if(o9&&*o9) g_skSub=(float)atof(o9); o9=getenv("REVA_SKLEGM"); if(o9&&*o9) g_skLegM=(float)atof(o9); o9=getenv("REVA_SKLEGR"); if(o9&&*o9) g_skLegR=(float)atof(o9); }
        { const char*e9=getenv("REVA_SKSKIN"); if(e9&&e9[0]=='0') g_skSkin=0; e9=getenv("REVA_SKCONE"); if(e9&&e9[0]=='0') g_skCone=0;
          e9=getenv("REVA_SKMU"); if(e9&&*e9){ g_skMuS=(float)atof(e9); const char*c9=strchr(e9,','); g_skMuK=c9?(float)atof(c9+1):g_skMuS*0.7f; } }
        printf("[skirt] hanging cloth: %s (bend compliance %.1e, hands %s, arm capsules %s, the air of the run %s)\n",
               g_skirtSim?"ON -- every ~skirt~ on a VertexCollide pack is simulated":"OFF (REVA_SKIRTSIM=0): a skirt is only skinned to its chains (the old renderer's drape went with it)",
               g_skBend,g_skHands?"on":"off",g_skArms?"on":"off",g_skAir?"on":"off"); fflush(stdout); }
    if(g_vcolOn<0){ const char*e=getenv("REVA_VCOLLIDE"); g_vcolOn=!(e&&e[0]=='0');
        printf("[vcol] per-vertex collision: %s\n",g_vcolOn?"ON (packs with a VertexCollide node)":"OFF (REVA_VCOLLIDE=0)"); fflush(stdout); }
    if(!g_skirtSim) return 0;
    /* (11-16) a mesh that rides Hang_ nodes hangs on any pack (load.inc); one sewn on such a cloth is drawn on it */
    const int hang9=P->meshHang?P->meshHang[mi]:0;
    if(hang9==2) return sewnPrep(slot,P,mi,pal,model,world);
    if(!hang9&&(!P->vcol||g_vcolOn==0)) return 0;
    PMesh*M=&P->meshes[mi];
    if(!hang9){ const char*nm=packStr(P,M->name); if(!nm||!strstr(nm,"~skirt~")) return 0; }
    Skirt*S=NULL;
    for(int i=0;i<SK_POOL;i++) if(g_sk[i].pk==P&&g_sk[i].mesh==(int)mi&&g_sk[i].slot==slot){ S=&g_sk[i]; break; }
    if(!S){ int pick=-1; unsigned old=0xffffffffu;
        for(int i=0;i<SK_POOL;i++){ if(!g_sk[i].pk){ pick=i; break; } if(g_sk[i].used<old){ old=g_sk[i].used; pick=i; } }
        S=&g_sk[pick]; S->pk=NULL; S->seeded=0; S->slot=slot; S->stamp=0; S->shStamp=0; }
    if(S->seeded<0) return 0;                       /* refused once, and said why */
    S->used=g_frameStamp;
    float*rp=(float*)(P->pk+M->offPos), *bd=(float*)(P->pk+M->offBind);
    unsigned char*jp=(unsigned char*)(P->pk+M->offJoints), *wp=(unsigned char*)(P->pk+M->offWeights);
    unsigned*ix=(unsigned*)(P->pk+M->offIdx);
    float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
    if(S->pk!=P||S->mesh!=(int)mi||!S->seeded){ S->slot=slot; if(!skirtInit(S,P,mi)) return 0; S->stamp=0; }
    S->floorY=model[13]+P->mny*sc;
    if(S->stamp!=g_frameStamp){ S->stamp=g_frameStamp;
        /* where the waistband is this frame: its own skin */
        for(int p=0;p<S->np;p++){ if(S->twin[p]>=0||!S->pin[p]) continue; int v=S->rep[p];
            float b3[3]; for(int k=0;k<3;k++) b3[k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k];
            float acc[3]={0,0,0}, ws=0.f;
            for(int q=0;q<4;q++){ float w=wp[v*4+q]/255.f; if(w<=0.f) continue; const float*pm=pal+jp[v*4+q]*16;
                for(int k=0;k<3;k++) acc[k]+=w*(pm[k]*b3[0]+pm[4+k]*b3[1]+pm[8+k]*b3[2]+pm[12+k]);
                ws+=w; }
            if(ws<1e-4f) memcpy(acc,b3,12);
            skXf(model,acc,S->tg1[p]); }
        /* the hips: rest -> world this frame */
        float H1[16]; { int hp=P->pb[0]; M4 t9;
            if(hp>=0){ m4_mul(t9,model,world[hp]); m4_mul(H1,t9,S->h0inv); } else memcpy(H1,model,64); }
        float caps[SK_NCAP][SKC]; int nc=skCaps(P,world,model,sc,S->tmed,S->legM,caps);
        skBodyFrame(S,P,world,model,!S->warm);
        skHandFrame(S,P,world,model,!S->warm);
        /* ⭐ THE AIR OF HER OWN RUN (10-18). On the bench she runs on the spot, so nothing carried her coat through the
         * air and nothing streamed it back: the knees kicked the panels wide and they hung there. The planted foot
         * says how fast the ground is going under her -- on the spot it slides back at the run's speed, on a path it
         * stands still -- so the air comes past her at exactly that, and a body really travelling gets no double. */
        { int fn[2]={P->footL,P->footR}; float fp[2][3], dt9=(float)g_chDt;
          for(int k=0;k<2;k++){ float o3[3]={0.f,0.f,0.f}; if(fn[k]>=0){ o3[0]=world[fn[k]][12]; o3[1]=world[fn[k]][13]; o3[2]=world[fn[k]][14]; } skXf(model,o3,fp[k]); }
          /* (11-16) no wind at all on a coat hung by its Hang_ nodes (the engine simulates no wind): a body the scene carries
           * isn't running, and her feet slide with her */
          if(!g_skAir||S->hang||fn[0]<0||fn[1]<0){ S->air[0]=S->air[1]=S->air[2]=0.f; }
          else if(S->fwarm&&S->warm&&dt9>1e-4f){
              int lo=(fp[0][1]<fp[1][1])?0:1; float low=fp[lo][1];
              if(low<S->fground) S->fground=low; else S->fground+=(low-S->fground)*fminf(1.f,dt9*0.5f);   /* the floor, followed slowly up */
              if(low<S->fground+0.05f*sc){ float a=1.f-expf(-dt9/0.30f);
                  float vx=(fp[lo][0]-S->fprev[lo][0])/dt9, vz=(fp[lo][2]-S->fprev[lo][2])/dt9;
                  S->air[0]+=(vx-S->air[0])*a; S->air[2]+=(vz-S->air[2])*a; S->air[1]=0.f; }
              { static int d9=-1, n9=0; if(d9<0){ const char*e=getenv("REVA_SKIRTDBG"); d9=(e&&e[0]=='3'); }
                if(d9&&(g_frameStamp%15u)==0u&&n9<400){ n9++; printf("[skairdbg] %s f%u feet y %.3f %.3f x %.3f %.3f z %.3f %.3f low %d ground %.3f dt %.4f air %.3f %.3f\n",
                    P->label,g_frameStamp,fp[0][1],fp[1][1],fp[0][0],fp[1][0],fp[0][2],fp[1][2],lo,S->fground,dt9,S->air[0],S->air[2]); } } }
          else { S->fground=fminf(fp[0][1],fp[1][1]); S->air[0]=S->air[1]=S->air[2]=0.f; }
          memcpy(S->fprev,fp,sizeof fp); S->fwarm=1; }
        float jump=0.f;
        if(S->warm) for(int p=0;p<S->np;p++){ if(S->twin[p]>=0||!S->pin[p]) continue;
            float dx=S->tg1[p][0]-S->tg0[p][0], dy=S->tg1[p][1]-S->tg0[p][1], dz=S->tg1[p][2]-S->tg0[p][2], d=sqrtf(dx*dx+dy*dy+dz*dz);
            if(d>jump) jump=d; }
        S->setContacts=0; S->setDeep=0; S->setPen=0.f; skLayersClear();
        /* (11-16) THE SET UNDER IT: the solid pieces near the cloth this frame (at the hang, below: near where it could be hung) */
        /* (11-16) a jump is more than half a metre or 15 m/s for the frame's length: at 10 frames a second a quick step was taken for one */
        const float jumpMax9=fmaxf(0.5f,15.f*(float)g_chDt)*sc;
        if(S->warm&&jump<=jumpMax9){ float lo9[3]={1e30f,1e30f,1e30f}, hi9[3]={-1e30f,-1e30f,-1e30f};
            for(int p=0;p<S->np;p++){ if(S->twin[p]>=0) continue; for(int k=0;k<3;k++){ if(S->cur[p][k]<lo9[k]) lo9[k]=S->cur[p][k]; if(S->cur[p][k]>hi9[k]) hi9[k]=S->cur[p][k]; } }
            skSetNear(S,lo9,hi9,0.25f*sc); }
        if(!S->warm||jump>jumpMax9){
            /* ⭐ HUNG IN ITS CUT, THEN THE PRE-ROLL: the coat on the hips as it was cut, the body from REST into this
             * frame's pose over half a second of its own time -- every limb arrives under the cloth from where it
             * hangs at rest, never starting through it */
            if(S->warm) printf("[skirt] %s: the body jumped %.2f u in a frame -- the coat is re-hung\n",P->label,jump);
            S->haveD0=0;   /* (11-16) no carry carried over a re-hang */
            /* ⭐ THE SIT-DOWN (11-16). Hung in its cut on her hips as they are, a coat on a body SITTING hangs straight down through the seat, and
             * the pre-roll can't take it out: it starts inside the chair. So when the cut, there, is in a solid piece of the set, she is hung where
             * it's clear -- the nearest place in front of her (heel to toe), behind her, to either side or above, 5 cm at a time to 1.2 m -- and the
             * pre-roll carries her from there into her pose over a second: she sits down in her coat, and the seat meets it from in front and below
             * as a seat does. Nothing's moved where the cut is clear: a body standing is hung as it always was */
            float sh9[3]={0.f,0.f,0.f}; int PRE=30;
            { float lo9[3]={1e30f,1e30f,1e30f}, hi9[3]={-1e30f,-1e30f,-1e30f};
              for(int p=0;p<S->np;p++){ if(S->twin[p]>=0) continue; float w9[3]; skXf(H1,S->pr[p],w9);
                  for(int k=0;k<3;k++){ if(w9[k]<lo9[k]) lo9[k]=w9[k]; if(w9[k]>hi9[k]) hi9[k]=w9[k]; } }
              skSetNear(S,lo9,hi9,1.45f*sc);
              const float clr9=(0.004f+S->tmed)*sc; static const float zero9[3]={0.f,0.f,0.f};
              const int bad0=S->nsn?skHangBlocked(S,H1,zero9,clr9):0;
              if(bad0){ float fw[3]={H1[0]*S->fwdR[0]+H1[8]*S->fwdR[1],0.f,H1[2]*S->fwdR[0]+H1[10]*S->fwdR[1]}; float fl=sqrtf(fw[0]*fw[0]+fw[2]*fw[2]);
                  if(fl<1e-6f){ fw[0]=0.f; fw[2]=1.f; fl=1.f; }
                  fw[0]/=fl; fw[2]/=fl;
                  const float dirs[5][3]={{fw[0],0.f,fw[2]},{-fw[0],0.f,-fw[2]},{fw[2],0.f,-fw[0]},{-fw[2],0.f,fw[0]},{0.f,1.f,0.f}};
                  static const char*dnm[5]={"in front of","behind","to one side of","to the other side of","above"};
                  int got=-1; float dd=0.f;
                  for(int s9=1;s9<=24&&got<0;s9++){ dd=0.05f*(float)s9;
                      for(int j=0;j<5;j++){ float t3[3]={dirs[j][0]*dd*sc,dirs[j][1]*dd*sc,dirs[j][2]*dd*sc}; if(!skHangBlocked(S,H1,t3,clr9)){ got=j; break; } } }
                  if(got>=0){ for(int k=0;k<3;k++) sh9[k]=dirs[got][k]*dd*sc;
                      PRE=60; printf("[skirt] %s: hung in its cut %d of its points were in the set: hung %.2f m %s where she is instead, and sat down into her pose over a second\n",P->label,bad0,dd,dnm[got]); }
                  else printf("[skirt] %s: hung in its cut %d of its points are in the set, and no place within 1.2 m is clear: hung where she is\n",P->label,bad0);
                  fflush(stdout); } }
            float H1s[16]; memcpy(H1s,H1,64); H1s[12]+=sh9[0]; H1s[13]+=sh9[1]; H1s[14]+=sh9[2];
            const int moved9=(sh9[0]!=0.f||sh9[1]!=0.f||sh9[2]!=0.f);
            static float pinR[SK_MAXP][3], pinC[SK_MAXP][3];
            for(int p=0;p<S->np;p++){ float w9[3]; skXf(H1s,S->pr[p],w9);
                memcpy(S->cur[p],w9,12); memcpy(S->prv[p],w9,12); if(!(S->twin[p]<0&&S->pin[p])) memcpy(S->tg0[p],w9,12); }
            for(int p=0;p<S->np;p++) if(S->twin[p]<0&&S->pin[p]){ memcpy(pinC[p],S->tg1[p],12);
                if(moved9){ skXf(H1s,S->pr[p],pinR[p]); memcpy(S->tg0[p],pinR[p],12); }   /* sat down: the waistband from her rest, out there, into her pose */
                else { memcpy(S->cur[p],S->tg1[p],12); memcpy(S->prv[p],S->tg1[p],12); memcpy(S->tg0[p],S->tg1[p],12); memcpy(pinR[p],S->tg1[p],12); } }
            static M4 RW[1024]; skRestFK(P,RW);
            float capR[SK_NCAP][SKC]; int ncr=skCaps(P,RW,H1s,sc,S->tmed,S->legM,capR);
            memcpy(S->cap0,capR,sizeof(float)*SKC*(size_t)ncr); S->ncap0=ncr; memcpy(S->H0,H1s,64);
            /* the skin too: from rest (on these hips) into this frame's pose */
            static float bpR[SK_BODYMAX][3], bpC[SK_BODYMAX][3]; size_t bsz=sizeof(float)*3*(size_t)S->nbv;
            skBodyFrame(S,P,RW,H1s,1); memcpy(bpR,S->bp1,bsz);
            skBodyFrame(S,P,world,model,1); memcpy(bpC,S->bp1,bsz);
            static float hpR[SK_HANDMAX][3], hpC[SK_HANDMAX][3]; size_t hsz=sizeof(float)*3*(size_t)S->nhv;   /* the hands too */
            skHandFrame(S,P,RW,H1s,1); memcpy(hpR,S->hp1,hsz);
            skHandFrame(S,P,world,model,1); memcpy(hpC,S->hp1,hsz);
            static const float still9[3]={0.f,0.f,0.f};
            skNormals(S,ix,M->idxCount,sc);
            float capI[SK_NCAP][SKC], up=0.f, Hu[16];
            for(int f=1;f<=PRE;f++){ float u=(float)f/(float)PRE; u=u*u*(3.f-2.f*u);
                for(int c=0;c<nc&&c<ncr;c++) for(int k=0;k<SKC;k++) capI[c][k]=capR[c][k]+(caps[c][k]-capR[c][k])*u;
                for(int i=0;i<S->nbv;i++) for(int k=0;k<3;k++){ S->bp0[i][k]=bpR[i][k]+(bpC[i][k]-bpR[i][k])*up;
                                                               S->bp1[i][k]=bpR[i][k]+(bpC[i][k]-bpR[i][k])*u; }
                for(int i=0;i<S->nhv;i++) for(int k=0;k<3;k++){ S->hp0[i][k]=hpR[i][k]+(hpC[i][k]-hpR[i][k])*up;
                                                               S->hp1[i][k]=hpR[i][k]+(hpC[i][k]-hpR[i][k])*u; }
                if(moved9) for(int p=0;p<S->np;p++) if(S->twin[p]<0&&S->pin[p]) for(int k=0;k<3;k++) S->tg1[p][k]=pinR[p][k]+(pinC[p][k]-pinR[p][k])*u;
                memcpy(Hu,H1,64); for(int k=0;k<3;k++) Hu[12+k]=H1s[12+k]+(H1[12+k]-H1s[12+k])*u;
                up=u;
                skirtStep(S,1.f/60.f,sc,capI,(nc<ncr)?nc:ncr,Hu,still9);
                skNormals(S,ix,M->idxCount,sc); }
            for(int p=0;p<S->np;p++) if(S->twin[p]<0&&S->pin[p]){ memcpy(S->tg1[p],pinC[p],12); memcpy(S->tg0[p],pinC[p],12); }
            memcpy(S->bp0,bpC,bsz); memcpy(S->bp1,bpC,bsz); memcpy(S->hp0,hpC,hsz); memcpy(S->hp1,hpC,hsz);
            for(int f=0;f<10;f++){ skirtStep(S,1.f/60.f,sc,caps,nc,H1,still9); skNormals(S,ix,M->idxCount,sc); }   /* and let it settle a moment */
            if(g_skDbg){ printf("[skirt] %s hung: pre-roll %d frames, %d capsules; at rest in the pose: %d contacts (%d with the set, %d points deep in it), deepest %.1f mm, worst stretch %.3f\n",
                                P->label,PRE,nc,g_skContacts,S->setContacts,S->setDeep,g_skDeep*1000.f,g_skStretch); fflush(stdout); }
            S->warm=1; }
        else if(!g_stepHeld) skirtStep(S,(float)g_chDt,sc,caps,nc,H1,S->air);   /* a held exposure does not integrate */
        else { for(int p=0;p<S->np;p++) if(S->twin[p]<0&&S->pin[p]) memcpy(S->tg0[p],S->tg1[p],12);
               memcpy(S->cap0,caps,sizeof(float)*SKC*(size_t)nc); S->ncap0=nc; memcpy(S->H0,H1,64); }
        skNormals(S,ix,M->idxCount,sc);
        /* (11-16) the numbers on her, for a hung cloth: REVA_SKIRTDBG=1, every REVA_SKEVERY frames (60) */
        if(g_skDbg&&S->hang){ static int ev9=-1; if(ev9<0){ const char*e9=getenv("REVA_SKEVERY"); ev9=(e9&&*e9)?atoi(e9):60; if(ev9<1) ev9=1; }
            if((g_frameStamp%(unsigned)ev9)==0u) skReport(S,P,mi,pal,model,world,sc); }
        if(g_skDbg&&(g_frameStamp%60u)==0u){ {
            float wy=0.f, wa=0.f; int we=S->worstE;
            if(we>=0){ int i=S->e0[we]; float y0=1e9f,y1=-1e9f; for(int p=0;p<S->np;p++){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; }
                wy=(y1-S->pr[i][1])/(y1-y0+1e-9f); wa=atan2f(S->pr[i][0]-S->ax,S->pr[i][2]-S->az)*57.2958f; }
            printf("[skirt] %s: %d contacts, deepest push %.1f mm, worst stretch %.3f (%.0f%% down, %.0f deg round), air %.2f m/s, %d capsules\n",
                   P->label,g_skContacts,g_skDeep*1000.f,g_skStretch,wy*100.f,wa,
                   sqrtf(S->air[0]*S->air[0]+S->air[2]*S->air[2])/sc,nc); fflush(stdout); }
          /* and the point that's furthest from its own tether's reach, with what holds it: a sheet across the room is one point somewhere it can't be */
          { int far=-1; float fd=0.f; for(int p=0;p<S->np;p++){ int a=S->anchor[p]; const float*o=(S->twin[p]<0&&a>=0)?S->cur[a]:S->pos[p];
                float dx=S->pos[p][0]-o[0], dy=S->pos[p][1]-o[1], dz=S->pos[p][2]-o[2], d=sqrtf(dx*dx+dy*dy+dz*dz)/sc-(a>=0?S->tlen[p]:0.f);
                if(d>fd||!(d==d)){ fd=d; far=p; } }
            if(far>=0){ int ne9=0, nb9=0; for(int e=0;e<S->ne;e++) if(S->e0[e]==far||S->e1[e]==far) ne9++; for(int e=0;e<S->nb;e++) if(S->b0[e]==far||S->b1[e]==far) nb9++;
                printf("[skirt] %s: furthest past its tether: point %d of %d, %.3f m past (tether %.3f m), pin %d twin %d anchor %d, %d edges %d bends, at %.2f %.2f %.2f, its normal %.2f %.2f %.2f\n",
                       P->label,far,S->np,fd,S->tlen[far],S->pin[far],S->twin[far],S->anchor[far],ne9,nb9,S->pos[far][0],S->pos[far][1],S->pos[far][2],S->nOut[far][0],S->nOut[far][1],S->nOut[far][2]); fflush(stdout); } } }
        /* the drawn mesh: every vertex at its point */
        static float vpos[SK_MAXV*3], vnrm[SK_MAXV*3];
        for(int v=0;v<S->nv;v++){ int p=S->vp[v]; memcpy(vpos+v*3,S->pos[p],12); memcpy(vnrm+v*3,S->nR[p],12); }
        if(!S->vbo[0]){ gfxBuffersNew(2,S->vbo);
            for(int k=0;k<2;k++){  gfxBufferData(S->vbo[k], (long)(S->nv*12), NULL, 1); } }   /* as big as it is, not as big as one could be: it's a new place in the frame's ring every frame */
         gfxBufferSub(S->vbo[0], 0, (long)(S->nv*12), vpos);
         gfxBufferSub(S->vbo[1], 0, (long)(S->nv*12), vnrm); }
    g_skVbo[0]=S->vbo[0]; g_skVbo[1]=S->vbo[1];
    /* the body's centre, for the lining's outline (colSet, uColOn 3) */
    { int hp=P->pb[0]; float c0[3]={0,0,0}; if(hp>=0){ c0[0]=world[hp][12]; c0[1]=world[hp][13]; c0[2]=world[hp][14]; }
      skXf(model,c0,g_skirtBC); g_skirtLive=1; }
    if(S->shStamp!=g_frameStamp){ S->shStamp=g_frameStamp; sunStashW(P,(int)mi,S->vbo[0],S->vbo[1],(int)M->boneCount,model); }
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(unsigned b9=0;b9<M->boneCount&&b9<MAXB;b9++) memcpy(pal+b9*16,I9,64);
    { float al=meshAlphaOf(P,mi);
      if(al<0.999f){    } }
    return 2; }

/* (11-16) THE NUMBERS ON HER, for a hung cloth (REVA_SKIRTDBG=1, every REVA_SKEVERY frames): how far its free points are from where the coat as
 * skinned would have them (REVA_SKIRTSIM=0's coat); how many are inside her legs (their capsules at her flesh, the measured radius over 1.15) or behind
 * her skin (a back skin point's plane, by more than 2 mm); how many are inside a solid piece of the set, and of those how many deeper than SKS_DEEP
 * (squeezed there by her); and its worst stretch. Metres on her rig */
static void skReport(Skirt*S,Pack*P,unsigned mi,const float*pal,const float*model,M4*world,float sc){
    PMesh*M=&P->meshes[mi];
    const float*rp=(const float*)(P->pk+M->offPos), *bd=(const float*)(P->pk+M->offBind);
    const unsigned char*jp=(const unsigned char*)(P->pk+M->offJoints), *wp=(const unsigned char*)(P->pk+M->offWeights);
    float caps[SK_NCAP][SKC]; const int nc=skCaps(P,world,model,sc,0.f,0.f,caps);
    double sum=0.0; float most=0.f, legD=0.f, skinD=0.f, setD=0.f; int nf=0, inLeg=0, inSkin=0, inSet=0, deepSet=0, mostP=-1;
    const char*legNm="";
    for(int p=0;p<S->np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; const float*x=S->cur[p]; nf++;
        { const int v=S->rep[p]; float b3[3], acc[3]={0,0,0}, ws=0.f, w3[3];
          for(int k=0;k<3;k++) b3[k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k];
          for(int q=0;q<4;q++){ const float w=wp[v*4+q]/255.f; if(w<=0.f) continue; const float*pm=pal+jp[v*4+q]*16;
              for(int k=0;k<3;k++) acc[k]+=w*(pm[k]*b3[0]+pm[4+k]*b3[1]+pm[8+k]*b3[2]+pm[12+k]);
              ws+=w; }
          if(ws<1e-4f) memcpy(acc,b3,12);
          skXf(model,acc,w3);
          const float d=sqrtf((x[0]-w3[0])*(x[0]-w3[0])+(x[1]-w3[1])*(x[1]-w3[1])+(x[2]-w3[2])*(x[2]-w3[2]))/sc; sum+=d; if(d>most){ most=d; mostP=p; } }
        for(int c=0;c<nc;c++){ const int kind=((int)caps[c][7])%6; if(kind>=3) continue;
            const float*A=caps[c], *B=caps[c]+3; const float e[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, e2=e[0]*e[0]+e[1]*e[1]+e[2]*e[2];
            float t=(e2>1e-12f)?((x[0]-A[0])*e[0]+(x[1]-A[1])*e[1]+(x[2]-A[2])*e[2])/e2:0.f; if(t<0.f) t=0.f; if(t>1.f) t=1.f;
            const float dx=x[0]-(A[0]+e[0]*t), dy=x[1]-(A[1]+e[1]*t), dz=x[2]-(A[2]+e[2]*t);
            const float in=(skCapR(caps[c],t)/1.15f-sqrtf(dx*dx+dy*dy+dz*dz))/sc;
            if(in>0.f){ inLeg++; if(in>legD){ legD=in; legNm=(kind==0)?"thigh":(kind==1)?"shin":"foot"; } break; } }
        if(S->back[p]&&S->nbv){ int best=-1; float bd2=0.05f*sc*0.05f*sc;
            for(int i=0;i<S->nbv;i++){ const float dx=x[0]-S->bp1[i][0], dy=x[1]-S->bp1[i][1], dz=x[2]-S->bp1[i][2], d2=dx*dx+dy*dy+dz*dz; if(d2<bd2){ bd2=d2; best=i; } }
            if(best>=0){ const float*bn=S->bn1[best]; const float d=((x[0]-S->bp1[best][0])*bn[0]+(x[1]-S->bp1[best][1])*bn[1]+(x[2]-S->bp1[best][2])*bn[2])/sc;
                if(d<-0.002f){ inSkin++; if(-d>skinD) skinD=-d; } } }
        for(int k=0;k<S->nsn;k++){ const SkSolid*K=&g_sks[S->sn[k].ks]; float l[3]; skXf(S->sn[k].Mi1,x,l);
            if(l[0]<K->lo[0]||l[0]>K->hi[0]||l[1]<K->lo[1]||l[1]>K->hi[1]||l[2]<K->lo[2]||l[2]>K->hi[2]) continue;
            float q[3],n[3],sd;
            if(!sksNear(K,l,SKS_R,q,n,&sd)){   /* no surface near it: its cell says whether it's deep inside */
                int c3[3], ok=1; for(int kk=0;kk<3;kk++){ c3[kk]=(int)floorf((l[kk]-K->lo[kk])/K->cell); if(c3[kk]<0||c3[kk]>=K->g[kk]) ok=0; }
                if(ok&&K->in[((long long)c3[2]*K->g[1]+c3[1])*K->g[0]+c3[0]]){ const float dm=SKS_R*S->sn[k].k/sc; inSet++; deepSet++; if(dm>setD) setD=dm; break; }
                continue; }
            if(sd<0.f){ const float dm=-sd*S->sn[k].k/sc; inSet++; if(dm>SKS_DEEP) deepSet++; if(dm>setD) setD=dm; break; } } }
    float mh=0.f; if(mostP>=0){ float y0=1e9f,y1=-1e9f; for(int p=0;p<S->np;p++){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; } mh=(y1-S->pr[mostP][1])/(y1-y0+1e-9f); }
    printf("[skirtnum] %s frame %u: %d free points. From the coat as skinned: mean %.1f cm, most %.1f cm (%.0f%% down, %.0f deg round). In her legs: %d (deepest %.1f mm, %s). "
           "Behind her skin: %d (deepest %.1f mm). In the set: %d (deepest %.1f mm; %d deeper than %.0f mm, squeezed). Worst stretch %.3f. Contacts %d (the set %d, deepest push there %.1f mm)\n",
           P->label,g_frameStamp,nf,nf?(float)(sum/nf)*100.f:0.f,most*100.f,mh*100.f,mostP>=0?atan2f(S->pr[mostP][0]-S->ax,S->pr[mostP][2]-S->az)*57.2958f:0.f,
           inLeg,legD*1000.f,legNm,inSkin,skinD*1000.f,inSet,setD*1000.f,deepSet,SKS_DEEP*1000.f,g_skStretch,g_skContacts,S->setContacts,S->setPen/sc*1000.f);
    /* (11-16) and how far it is from ITS OWN SHAPE: the cut carried by her hips (what REVA_SKSHAPE pulls it toward), and how high any point is above it */
    { float Hc[16]; { int hp=P->pb[0]; M4 t9; if(hp>=0){ m4_mul(t9,model,world[hp]); m4_mul(Hc,t9,S->h0inv); } else memcpy(Hc,model,64); }
      double s9=0.0; float m9=0.f, up9=0.f; int n9=0, mp9=-1, up9p=-1; float y0=1e9f,y1=-1e9f;
      for(int p=0;p<S->np;p++){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; }
      for(int p=0;p<S->np;p++){ if(S->twin[p]>=0||S->pin[p]) continue; float t[3]; skXf(Hc,S->pr[p],t);
          const float dx=S->cur[p][0]-t[0], dy=S->cur[p][1]-t[1], dz=S->cur[p][2]-t[2], d=sqrtf(dx*dx+dy*dy+dz*dz)/sc;
          s9+=d; n9++; if(d>m9){ m9=d; mp9=p; } if(dy/sc>up9){ up9=dy/sc; up9p=p; } }
      #define SK9DN(pp) ((pp)>=0?(y1-S->pr[pp][1])/(y1-y0+1e-9f)*100.f:0.f)
      #define SK9AN(pp) ((pp)>=0?atan2f(S->pr[pp][0]-S->ax,S->pr[pp][2]-S->az)*57.2958f:0.f)
      printf("[skirtshape] %s frame %u: from its cut on her hips: mean %.1f cm, most %.1f cm (%.0f%% down, %.0f deg round); most above its cut %.1f cm (%.0f%% down, %.0f deg round)\n",
             P->label,g_frameStamp,n9?(float)(s9/n9)*100.f:0.f,m9*100.f,SK9DN(mp9),SK9AN(mp9),up9*100.f,SK9DN(up9p),SK9AN(up9p));
      #undef SK9DN
      #undef SK9AN
    }
    /* and what pushed it this frame, layer by layer */
    { char lb[900]; int o=0; float y0=1e9f,y1=-1e9f; for(int p=0;p<S->np;p++){ if(S->pr[p][1]<y0) y0=S->pr[p][1]; if(S->pr[p][1]>y1) y1=S->pr[p][1]; }
      lb[0]=0;
      for(int l=0;l<SKL_N&&o<(int)sizeof lb-120;l++){ if(!g_skLN[l]) continue; const int pp=g_skLP[l]; float hd=0.f, an=0.f;
          if(pp>=0&&pp<S->np){ hd=(y1-S->pr[pp][1])/(y1-y0+1e-9f)*100.f; an=atan2f(S->pr[pp][0]-S->ax,S->pr[pp][2]-S->az)*57.2958f; }
          o+=snprintf(lb+o,sizeof lb-(size_t)o," %s %d (most %.1f mm, %.0f%% down %.0f deg; all %.1f cm)",g_skLNm[l],g_skLN[l],g_skLM[l]/sc*1000.f,hd,an,(float)(g_skLS[l]/sc*100.0)); }
      printf("[skirtlay] %s frame %u:%s\n",P->label,g_frameStamp,o?lb:" nothing pushed it"); }
    fflush(stdout); }

/* ======================== SEWN ON IT (11-16) =========================================================================================================
 * A pocket on a coat that hangs: another mesh of the coat's piece that rides its Hang_ nodes (load.inc: <base>.pockets beside <base>.hang) is drawn ON
 * the cloth, each of its vertices where it lay at rest over the nearest of the cloth's own triangles -- at that point of the triangle as it is now,
 * turned as the cloth's surface there turns (its smooth normal and the triangle's first edge), its offset kept. So it goes wherever the cloth goes
 * and never parts from it. REVA_SKIRTSIM=0 draws it skinned, as it draws the coat */
#define SW_POOL 24
typedef struct { Pack*pk; int mesh, slot, sib, ok; unsigned stamp, shStamp, used; int nv; int*tri; float*bc, *off, *nl; unsigned vbo[2]; } Sewn;
static Sewn g_sw[SW_POOL];
/* a frame on a cloth triangle at barycentric (u, v, w): its point, and three axes (along its first edge, across, its smooth normal) */
static void swFrame(const float (*X)[3],const float (*Nn)[3],const unsigned short*t,const float*b,float*q,float*e1,float*e2,float*n){
    const float*A=X[t[0]], *B=X[t[1]], *C=X[t[2]];
    for(int k=0;k<3;k++){ q[k]=b[0]*A[k]+b[1]*B[k]+b[2]*C[k]; n[k]=b[0]*Nn[t[0]][k]+b[1]*Nn[t[1]][k]+b[2]*Nn[t[2]][k]; }
    float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(l<1e-12f){ n[0]=0.f; n[1]=1.f; n[2]=0.f; l=1.f; }
    for(int k=0;k<3;k++) n[k]/=l;
    float d[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}; const float dn=d[0]*n[0]+d[1]*n[1]+d[2]*n[2];
    for(int k=0;k<3;k++) e1[k]=d[k]-n[k]*dn;
    l=sqrtf(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]);
    if(l<1e-12f){ const float a[3]={fabsf(n[0])<0.9f?1.f:0.f,fabsf(n[0])<0.9f?0.f:1.f,0.f}; e1[0]=a[1]*n[2]-a[2]*n[1]; e1[1]=a[2]*n[0]-a[0]*n[2]; e1[2]=a[0]*n[1]-a[1]*n[0]; l=sqrtf(e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2]); }
    for(int k=0;k<3;k++) e1[k]/=l;
    e2[0]=n[1]*e1[2]-n[2]*e1[1]; e2[1]=n[2]*e1[0]-n[0]*e1[2]; e2[2]=n[0]*e1[1]-n[1]*e1[0]; }
static int sewnPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world){
    const int sib=P->meshHangSib?P->meshHangSib[mi]:-1; if(sib<0||(unsigned)sib>=P->nMeshes) return 0;
    Skirt*S=NULL;
    for(int i=0;i<SK_POOL;i++) if(g_sk[i].pk==P&&g_sk[i].mesh==sib&&g_sk[i].slot==slot){ S=&g_sk[i]; break; }
    if(!S||S->stamp!=g_frameStamp){   /* the cloth it's sewn on hasn't been stepped this frame yet: it's stepped now, with its own bones */
        static float palS[MAXB*16]; bodyPalette(P,(unsigned)sib,world,palS);
        if(!skirtPrep(slot,P,(unsigned)sib,palS,model,world)) return 0;
        S=NULL; for(int i=0;i<SK_POOL;i++) if(g_sk[i].pk==P&&g_sk[i].mesh==sib&&g_sk[i].slot==slot){ S=&g_sk[i]; break; } }
    if(!S||S->seeded<=0||!S->warm) return 0;
    PMesh*M=&P->meshes[mi];
    Sewn*W=NULL;
    for(int i=0;i<SW_POOL;i++) if(g_sw[i].pk==P&&g_sw[i].mesh==(int)mi&&g_sw[i].slot==slot){ W=&g_sw[i]; break; }
    if(!W){ int pick=-1; unsigned old=0xffffffffu;
        for(int i=0;i<SW_POOL;i++){ if(!g_sw[i].pk){ pick=i; break; } if(g_sw[i].used<old){ old=g_sw[i].used; pick=i; } }
        W=&g_sw[pick]; free(W->tri); free(W->bc); free(W->off); free(W->nl);
        { const unsigned v0=W->vbo[0], v1=W->vbo[1]; memset(W,0,sizeof *W); W->vbo[0]=v0; W->vbo[1]=v1; }
        W->pk=P; W->mesh=(int)mi; W->slot=slot; W->ok=0; }
    W->used=g_frameStamp;
    if(W->ok<0) return 0;
    if(!W->ok){   /* sewn: each vertex to the nearest of the cloth's outer triangles, at rest */
        const int nv=(int)M->verts; const float*rp=(const float*)(P->pk+M->offPos), *rn=(const float*)(P->pk+M->offNorm), *bd=(const float*)(P->pk+M->offBind);
        W->nv=nv; W->tri=(int*)malloc(sizeof(int)*(size_t)nv); W->bc=(float*)malloc(sizeof(float)*3*(size_t)nv);
        W->off=(float*)malloc(sizeof(float)*3*(size_t)nv); W->nl=(float*)malloc(sizeof(float)*3*(size_t)nv);
        if(!W->tri||!W->bc||!W->off||!W->nl||!(M->flags&1)||!S->nt){ W->ok=-1; return 0; }
        /* the cloth's normals at rest, its outer wall's own */
        static float rN[SK_MAXP][3]; for(int p=0;p<S->np;p++) rN[p][0]=rN[p][1]=rN[p][2]=0.f;
        for(int t=0;t<S->nt;t++){ const unsigned short*q=S->tri[t]; const float*A=S->pr[q[0]], *B=S->pr[q[1]], *C=S->pr[q[2]];
            const float u[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]}, v[3]={C[0]-A[0],C[1]-A[1],C[2]-A[2]};
            const float n[3]={(u[1]*v[2]-u[2]*v[1])*S->osign,(u[2]*v[0]-u[0]*v[2])*S->osign,(u[0]*v[1]-u[1]*v[0])*S->osign};
            for(int c=0;c<3;c++) for(int k=0;k<3;k++) rN[q[c]][k]+=n[k]; }
        /* the triangles by where their middles are, in 4 cm cells */
        float lo[3]={1e30f,1e30f,1e30f}, hi[3]={-1e30f,-1e30f,-1e30f};
        for(int p=0;p<S->np;p++) for(int k=0;k<3;k++){ if(S->pr[p][k]<lo[k]) lo[k]=S->pr[p][k]; if(S->pr[p][k]>hi[k]) hi[k]=S->pr[p][k]; }
        const float cell=0.04f; int g[3]; for(int k=0;k<3;k++){ g[k]=(int)((hi[k]-lo[k])/cell)+1; if(g[k]<1) g[k]=1; }
        const int ncell=g[0]*g[1]*g[2]; int*cs=(int*)calloc((size_t)ncell+1,sizeof(int)), *ci=(int*)malloc(sizeof(int)*(size_t)S->nt);
        if(!cs||!ci){ free(cs); free(ci); W->ok=-1; return 0; }
        #define SWCELL(pt,c3) { for(int k9=0;k9<3;k9++){ c3[k9]=(int)(((pt)[k9]-lo[k9])/cell); if(c3[k9]<0) c3[k9]=0; if(c3[k9]>=g[k9]) c3[k9]=g[k9]-1; } }
        for(int t=0;t<S->nt;t++){ const unsigned short*q=S->tri[t]; float m3[3]; int c3[3]; for(int k=0;k<3;k++) m3[k]=(S->pr[q[0]][k]+S->pr[q[1]][k]+S->pr[q[2]][k])/3.f; SWCELL(m3,c3); cs[(c3[2]*g[1]+c3[1])*g[0]+c3[0]+1]++; }
        for(int c=0;c<ncell;c++) cs[c+1]+=cs[c];
        { int*fill=(int*)malloc(sizeof(int)*(size_t)ncell); if(!fill){ free(cs); free(ci); W->ok=-1; return 0; }
          memcpy(fill,cs,sizeof(int)*(size_t)ncell);
          for(int t=0;t<S->nt;t++){ const unsigned short*q=S->tri[t]; float m3[3]; int c3[3]; for(int k=0;k<3;k++) m3[k]=(S->pr[q[0]][k]+S->pr[q[1]][k]+S->pr[q[2]][k])/3.f; SWCELL(m3,c3); ci[fill[(c3[2]*g[1]+c3[1])*g[0]+c3[0]]++]=t; }
          free(fill); }
        int far=0; float farD=0.f;
        for(int v=0;v<nv;v++){ float p[3], n[3]; for(int k=0;k<3;k++){ p[k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k]; n[k]=bd[k]*rn[v*3]+bd[4+k]*rn[v*3+1]+bd[8+k]*rn[v*3+2]; }
            { const float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(l>1e-12f){ n[0]/=l; n[1]/=l; n[2]/=l; } }
            int c3[3]; SWCELL(p,c3); int bt=-1; float bd2=1e30f, bu=0.f,bv=0.f,bw=0.f;
            for(int r=1;r<=6&&bt<0;r++)
                for(int z=c3[2]-r;z<=c3[2]+r;z++) for(int y=c3[1]-r;y<=c3[1]+r;y++) for(int x=c3[0]-r;x<=c3[0]+r;x++){
                    if(x<0||y<0||z<0||x>=g[0]||y>=g[1]||z>=g[2]) continue;
                    const int c=(z*g[1]+y)*g[0]+x;
                    for(int e=cs[c];e<cs[c+1];e++){ const int t=ci[e]; const unsigned short*q=S->tri[t]; float u,vv,w; skClosestTri(p,S->pr[q[0]],S->pr[q[1]],S->pr[q[2]],&u,&vv,&w);
                        float qq[3]; for(int k=0;k<3;k++) qq[k]=u*S->pr[q[0]][k]+vv*S->pr[q[1]][k]+w*S->pr[q[2]][k];
                        const float d2=(p[0]-qq[0])*(p[0]-qq[0])+(p[1]-qq[1])*(p[1]-qq[1])+(p[2]-qq[2])*(p[2]-qq[2]);
                        if(d2<bd2){ bd2=d2; bt=t; bu=u; bv=vv; bw=w; } } }
            if(bt<0){ W->ok=-1; free(cs); free(ci); printf("[skirt] %s: '%s' has a point nowhere near the cloth it's sewn on: drawn skinned\n",P->label,packStr(P,M->name)); fflush(stdout); return 0; }
            const float b3[3]={bu,bv,bw}; float q[3],e1[3],e2[3],nn[3]; swFrame((const float(*)[3])S->pr,(const float(*)[3])rN,S->tri[bt],b3,q,e1,e2,nn);
            const float d[3]={p[0]-q[0],p[1]-q[1],p[2]-q[2]};
            W->tri[v]=bt; memcpy(W->bc+v*3,b3,12);
            W->off[v*3]=d[0]*e1[0]+d[1]*e1[1]+d[2]*e1[2]; W->off[v*3+1]=d[0]*e2[0]+d[1]*e2[1]+d[2]*e2[2]; W->off[v*3+2]=d[0]*nn[0]+d[1]*nn[1]+d[2]*nn[2];
            W->nl[v*3]=n[0]*e1[0]+n[1]*e1[1]+n[2]*e1[2]; W->nl[v*3+1]=n[0]*e2[0]+n[1]*e2[1]+n[2]*e2[2]; W->nl[v*3+2]=n[0]*nn[0]+n[1]*nn[1]+n[2]*nn[2];
            if(bd2>farD){ farD=bd2; far=v; } }
        #undef SWCELL
        free(cs); free(ci); W->ok=1; W->sib=sib;
        printf("[skirt] %s: '%s' (%d vertices) sewn on its cloth, the furthest %.1f mm off it (vertex %d)\n",P->label,packStr(P,M->name),nv,sqrtf(farD)*1000.f,far); fflush(stdout); }
    if(W->stamp!=g_frameStamp){ W->stamp=g_frameStamp;
        static float vpos[SK_MAXV*3], vnrm[SK_MAXV*3];
        if(W->nv>SK_MAXV){ W->ok=-1; return 0; }
        const float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
        for(int v=0;v<W->nv;v++){ float q[3],e1[3],e2[3],n[3]; swFrame((const float(*)[3])S->cur,(const float(*)[3])S->nOut,S->tri[W->tri[v]],W->bc+v*3,q,e1,e2,n);
            const float*o=W->off+v*3, *l=W->nl+v*3;
            for(int k=0;k<3;k++){ vpos[v*3+k]=q[k]+(o[0]*e1[k]+o[1]*e2[k]+o[2]*n[k])*sc; vnrm[v*3+k]=l[0]*e1[k]+l[1]*e2[k]+l[2]*n[k]; } }
        if(!W->vbo[0]){ gfxBuffersNew(2,W->vbo); for(int k=0;k<2;k++) gfxBufferData(W->vbo[k],(long)(W->nv*12),NULL,1); }
        gfxBufferSub(W->vbo[0],0,(long)(W->nv*12),vpos);
        gfxBufferSub(W->vbo[1],0,(long)(W->nv*12),vnrm); }
    g_skVbo[0]=W->vbo[0]; g_skVbo[1]=W->vbo[1];
    { int hp=P->pb[0]; float c0[3]={0,0,0}; if(hp>=0){ c0[0]=world[hp][12]; c0[1]=world[hp][13]; c0[2]=world[hp][14]; } skXf(model,c0,g_skirtBC); g_skirtLive=1; }
    if(W->shStamp!=g_frameStamp){ W->shStamp=g_frameStamp; sunStashW(P,(int)mi,W->vbo[0],W->vbo[1],(int)M->boneCount,model); }
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(unsigned b9=0;b9<M->boneCount&&b9<MAXB;b9++) memcpy(pal+b9*16,I9,64);
    return 2; }

static int clothPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world){
    { int st9=strandPrep(slot,P,mi,pal,model,world); if(st9) return st9; }   /* (11-06) hair, lock by lock (engine/strands.inc) */
    if(g_clothOff||slot<0||slot>=CLOTH_SLOTS) return 0;
    { int sk9=skirtPrep(slot,P,mi,pal,model,world); if(sk9) return sk9; }   /* a skirt is hanging cloth (10-18) */
    if(!meshIsCloth(P,mi)) return 0;
    PMesh*M=&P->meshes[mi];
    if(M->verts>=CLOTH_MAXV){ static int said=0; if(!said){ said=1;
        printf("[cloth] !! %s: %u vertices is over CLOTH_MAXV %d -- NOT simulated, skinned only (raise the cap or decimate)\n",packStr(P,M->name),M->verts,CLOTH_MAXV); fflush(stdout); }
        return 0; }
    Cloth*C=&g_cloth[slot];
    static float skinW[CLOTH_MAXV*3];
    float*rp=(float*)(P->pk+M->offPos);
    unsigned char*jp=(unsigned char*)(P->pk+M->offJoints);
    unsigned char*wp=(unsigned char*)(P->pk+M->offWeights);
    float*bindm=(float*)(P->pk+M->offBind);
    for(unsigned v=0;v<M->verts;v++){
        float bp[3]={rp[v*3],rp[v*3+1],rp[v*3+2]};
        float bq[3];
        for(int k=0;k<3;k++) bq[k]=bindm[k]*bp[0]+bindm[4+k]*bp[1]+bindm[8+k]*bp[2]+bindm[12+k];
        float acc[3]={0,0,0}, wsum=0.f;
        for(int q=0;q<4;q++){
            float w=wp[v*4+q]/255.f; if(w<=0.f) continue;
            float*pm=pal+jp[v*4+q]*16;
            for(int k=0;k<3;k++)
                acc[k]+=w*(pm[k]*bq[0]+pm[4+k]*bq[1]+pm[8+k]*bq[2]+pm[12+k]);
            wsum+=w; }
        if(wsum<1e-4f){ acc[0]=bq[0]; acc[1]=bq[1]; acc[2]=bq[2]; }
        for(int k=0;k<3;k++)
            skinW[v*3+k]=model[k]*acc[0]+model[4+k]*acc[1]+model[8+k]*acc[2]+model[12+k]; }
    if(!C->seeded||C->pk!=P||C->mesh!=(int)mi) clothInit(C,P,mi,skinW);
    if(C->stamp!=g_frameStamp){
        C->stamp=g_frameStamp;
        float sph[CLOTH_MAXS][4]; int nsph=0;
        /* ⭐ EVERY CANONICAL BONE, AT ITS MEASURED THICKNESS. Six spheres with guessed
         * radii covered the spine and the feet and left the arms, thighs and shins bare,
         * which is exactly where a garment went through her. */
        { float sc9=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
          measureBoneR(P);
          for(int c=0;c<NCANON&&nsph<CLOTH_MAXS;c++){
              int bn=P->pb[c];
              if(bn<0||P->boneR[c]<=0.f) continue;
              float wv[3];
              for(int k=0;k<3;k++)
                  wv[k]=model[k]*world[bn][12]+model[4+k]*world[bn][13]+model[8+k]*world[bn][14]+model[12+k];
              sph[nsph][0]=wv[0]; sph[nsph][1]=wv[1]; sph[nsph][2]=wv[2];
              sph[nsph][3]=P->boneR[c]*sc9; nsph++; } }
        /* ⭐ THE BODY, SKINNED ON THE CPU FOR THIS FRAME (09-11): the largest skinned mesh that is
         * not cloth, hair, or a face part, sampled to CLOTH_BODYMAX vertices with its normals,
         * through its OWN bone map and inverse binds against the same world matrices. Cached per
         * pack per frame, so two garments on one body skin it once. */
        static float bodyP[CLOTH_BODYMAX*3], bodyN[CLOTH_BODYMAX*3]; static int nBody=0;
        static Pack*bodyPk=NULL; static unsigned bodyStamp=0; static int bodyMi=-1;
        float sc9b=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
        if(bodyPk!=P||bodyStamp!=g_frameStamp){
            bodyPk=P; bodyStamp=g_frameStamp; nBody=0;
            if(bodyMi<0||bodyPk!=P){ bodyMi=-1; unsigned best=0;
                for(unsigned m9=0;m9<P->nMeshes;m9++){ PMesh*Mb=&P->meshes[m9];
                    if(!(Mb->flags&4)||!Mb->boneCount||Mb->boneCount>MAXB||meshIsGarment(P,m9)||MOFF(P,m9)) continue;
                    const char*nm=packStr(P,Mb->name);
                    if(strstr(nm,"hair")||strstr(nm,"braid")||strstr(nm,"eye")||strstr(nm,"teeth")||strstr(nm,"tongue")||strstr(nm,"lash")||strstr(nm,"brow")||strstr(nm,"interior")) continue;
                    if(Mb->verts>best){ best=Mb->verts; bodyMi=(int)m9; } }
                if(bodyMi>=0&&getenv("REVA_CLOTHDBG")) printf("[cloth] body mesh for collision: '%s' (%u verts)\n",packStr(P,P->meshes[bodyMi].name),P->meshes[bodyMi].verts); }
            if(bodyMi>=0){ PMesh*Mb=&P->meshes[bodyMi];
                static float bpal[MAXB*16];
                unsigned short*bm=(unsigned short*)(P->pk+Mb->offBoneMap); float*ibm=(float*)(P->pk+Mb->offIBM);
                for(unsigned b=0;b<Mb->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]<P->nNodes?bm[b]:0],*(M4*)(ibm+b*16)); memcpy(bpal+b*16,pm,64); }
                float*brp=(float*)(P->pk+Mb->offPos), *bnp=(Mb->flags&1)?(float*)(P->pk+Mb->offNorm):NULL;
                unsigned char*bjp=(unsigned char*)(P->pk+Mb->offJoints), *bwp=(unsigned char*)(P->pk+Mb->offWeights);
                float*bbind=(float*)(P->pk+Mb->offBind);
                unsigned step=(Mb->verts>CLOTH_BODYMAX)?(Mb->verts/CLOTH_BODYMAX+1):1;
                for(unsigned v=0;v<Mb->verts&&nBody<CLOTH_BODYMAX;v+=step){
                    float bp[3]={brp[v*3],brp[v*3+1],brp[v*3+2]}, bq[3], nq[3]={0,1,0};
                    for(int k=0;k<3;k++) bq[k]=bbind[k]*bp[0]+bbind[4+k]*bp[1]+bbind[8+k]*bp[2]+bbind[12+k];
                    if(bnp){ float n0[3]={bnp[v*3],bnp[v*3+1],bnp[v*3+2]}; for(int k=0;k<3;k++) nq[k]=bbind[k]*n0[0]+bbind[4+k]*n0[1]+bbind[8+k]*n0[2]; }
                    float acc[3]={0,0,0}, nacc[3]={0,0,0}, wsum=0.f;
                    for(int q=0;q<4;q++){ float w=bwp[v*4+q]/255.f; if(w<=0.f) continue; float*pm=bpal+bjp[v*4+q]*16;
                        for(int k=0;k<3;k++){ acc[k]+=w*(pm[k]*bq[0]+pm[4+k]*bq[1]+pm[8+k]*bq[2]+pm[12+k]); nacc[k]+=w*(pm[k]*nq[0]+pm[4+k]*nq[1]+pm[8+k]*nq[2]); }
                        wsum+=w; }
                    if(wsum<1e-4f){ memcpy(acc,bq,12); memcpy(nacc,nq,12); }
                    float wp9[3], wn9[3];
                    for(int k=0;k<3;k++){ wp9[k]=model[k]*acc[0]+model[4+k]*acc[1]+model[8+k]*acc[2]+model[12+k];
                                          wn9[k]=model[k]*nacc[0]+model[4+k]*nacc[1]+model[8+k]*nacc[2]; }
                    float nl=sqrtf(wn9[0]*wn9[0]+wn9[1]*wn9[1]+wn9[2]*wn9[2]); if(nl<1e-6f){ wn9[0]=0; wn9[1]=1; wn9[2]=0; nl=1.f; }
                    bodyP[nBody*3]=wp9[0]; bodyP[nBody*3+1]=wp9[1]; bodyP[nBody*3+2]=wp9[2];
                    bodyN[nBody*3]=wn9[0]/nl; bodyN[nBody*3+1]=wn9[1]/nl; bodyN[nBody*3+2]=wn9[2]/nl; nBody++; } } }
        if(!g_stepHeld) clothStep(C,skinW,(const float(*)[4])sph,nsph,(float)g_chDt,1.f,bodyP,bodyN,nBody,0.012f*sc9b,0.06f*sc9b);   /* a held exposure does not integrate (see chainSim) */
        { static int d8=0; if(getenv("REVA_CLOTHDBG")&&(d8++%60)==0){ printf("[cloth] body mesh: %d samples, %d particles were INSIDE the skin before the push, %d pushed clear (%s)\n",nBody,g_clothInside,g_clothPushed,g_clothMeshOn?"mesh collision on":"REVA_CLOTHMESH=0"); fflush(stdout); } }
        /* ⭐ WHERE IS IT, ACTUALLY? Collapsed, exploded and invisible are three different
         * bugs that all look identical in a render -- an empty frame. The box says which:
         * compare the garment's extent against the SKINNED pose it is supposed to hang on
         * and against the body spheres it is supposed to sit outside. */
        { static int d9=0; if(getenv("REVA_CLOTHDBG")&&(d9++%60)==0){
            float lo[3]={1e9f,1e9f,1e9f}, hi[3]={-1e9f,-1e9f,-1e9f};
            float slo[3]={1e9f,1e9f,1e9f}, shi[3]={-1e9f,-1e9f,-1e9f};
            for(int v=0;v<C->nv;v++) for(int k=0;k<3;k++){
                if(C->cur[v][k]<lo[k]) lo[k]=C->cur[v][k];
                if(C->cur[v][k]>hi[k]) hi[k]=C->cur[v][k];
                if(skinW[v*3+k]<slo[k]) slo[k]=skinW[v*3+k];
                if(skinW[v*3+k]>shi[k]) shi[k]=skinW[v*3+k]; }
            printf("[clothdbg] slot %d sim %.2f,%.2f,%.2f .. %.2f,%.2f,%.2f  (size %.2f,%.2f,%.2f)%c"
                   "           skin %.2f,%.2f,%.2f .. %.2f,%.2f,%.2f  (size %.2f,%.2f,%.2f) spheres %d r0 %.3f%c",
                   slot,lo[0],lo[1],lo[2],hi[0],hi[1],hi[2],hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2],10,
                   slo[0],slo[1],slo[2],shi[0],shi[1],shi[2],shi[0]-slo[0],shi[1]-slo[1],shi[2]-slo[2],
                   nsph,nsph?sph[0][3]:0.f,10);
            fflush(stdout); } } }
    gfxBufferSub(P->vbo[mi*6], 0, (long)(C->nv*12), C->cur);
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(unsigned b9=0;b9<M->boneCount&&b9<MAXB;b9++) memcpy(pal+b9*16,I9,64);
    /* ⭐ SHEER. Blending and no depth write, handed back by the caller's restore line. */
    { float al=meshAlphaOf(P,mi);
      if(al<0.999f){
                      } }
    return 1; }


