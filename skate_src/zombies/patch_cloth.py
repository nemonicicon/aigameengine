"""patch_cloth.py -- CLOTH. Loose fabric that hangs, swings and stays out of the body.

jacob: "cant we implement some kind of loose fabric program/feature that uses like ray
tracing casting or whatever so it has physics and can adjust to whatever it is on? seems
like an essential feature for the game."

He is right, and it is the correct answer to the pyjamas. Four static fits failed because a
GARMENT IS NOT A SHAPE -- it is a shape plus what gravity and a body do to it. No amount of
choosing a radius produces cloth, because cloth is the thing that happens after you choose
one.

⭐ AND THE SOLVER ALREADY EXISTS IN THIS FILE. chainSim is verlet with distance constraints,
running her hair and tails since H1. Cloth is the same three lines applied to VERTICES
instead of bones, with two additions: the constraints come from the mesh's own EDGES
instead of a chain, and the particles are pushed out of the body.

    verlet integrate + gravity     -- what chainSim already does
    constrain every mesh edge      -- to its rest length, a few passes
    push out of the body           -- spheres at the canonical bones, sized from the rig
    pin the top ring               -- to its SKINNED position, so it hangs off the shoulders

⛔ IT IS SPHERES, NOT RAY CASTS. jacob said "ray tracing casting or whatever", and the
honest engineering answer is that cloth against a triangle mesh is a much bigger and much
slower thing than this film needs -- a shirt needs to not go through a torso, and a torso
is a handful of capsules. Written down so the limit is known: this will not drape over a
chair arm or catch on a fence.

⛔ AND A CLOTH MESH IS NOT SKINNED AT DRAW. The solver produces WORLD positions, so the
mesh is uploaded per frame and drawn with an identity model and an identity palette --
otherwise the GPU would skin the simulation a second time.

A mesh opts in by name: `pyjamas~cloth#5A6E9E`.
REVA_NOCLOTH=1 falls back to the skinned pose.
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


CLOTH = '''
/* ======================== CLOTH =================================================
 * jacob: "some kind of loose fabric program/feature ... so it has physics and can adjust
 * to whatever it is on. seems like an essential feature for the game."
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
#define CLOTH_MAXV 2048
#define CLOTH_MAXE 6144
#define CLOTH_MAXS 12
typedef struct {
    Pack *pk; int mesh;                 /* which mesh of which pack this is */
    int nv, ne;
    float cur[CLOTH_MAXV][3], prv[CLOTH_MAXV][3];
    unsigned short e0[CLOTH_MAXE], e1[CLOTH_MAXE]; float elen[CLOTH_MAXE];
    unsigned char pin[CLOTH_MAXV];      /* pinned to the skinned pose (the shoulders) */
    int seeded;
} Cloth;
static Cloth g_cloth[SCN_MAXCAST];
static int   g_clothOff=0;

static int meshIsCloth(Pack*P,unsigned mi){
    if(!(P->meshes[mi].flags&4)) return 0;
    return strstr(packStr(P,P->meshes[mi].name),"~cloth")!=NULL; }

/* Build the particle set and the edge list, once per body. */
static void clothInit(Cloth*C,Pack*P,unsigned mi,float*skinned){
    PMesh*M=&P->meshes[mi];
    C->pk=P; C->mesh=(int)mi;
    C->nv=(int)((M->verts<CLOTH_MAXV)?M->verts:CLOTH_MAXV);
    float*rp=(float*)(P->pk+M->offPos);
    float mny=1e9f,mxy=-1e9f;
    for(int v=0;v<C->nv;v++){ float y=rp[v*3+1]; if(y<mny)mny=y; if(y>mxy)mxy=y; }
    for(int v=0;v<C->nv;v++){
        for(int k=0;k<3;k++){ C->cur[v][k]=skinned[v*3+k]; C->prv[v][k]=skinned[v*3+k]; }
        /* ⭐ THE TOP RING IS PINNED. A shirt hangs FROM the shoulders; without a pin the
         * whole garment is a bag that falls to the floor on frame one. */
        C->pin[v]=(rp[v*3+1] > mny+(mxy-mny)*0.92f)?1:0; }
    C->ne=0;
    unsigned*ix=(unsigned*)(P->pk+M->offIdx);
    for(unsigned t=0;t<M->idxCount && C->ne+3<CLOTH_MAXE; t+=3){
        unsigned a=ix[t],b=ix[t+1],c=ix[t+2];
        unsigned pr[3][2]={{a,b},{b,c},{c,a}};
        for(int k=0;k<3;k++){
            unsigned i=pr[k][0], j=pr[k][1];
            if(i>=(unsigned)C->nv||j>=(unsigned)C->nv||i==j) continue;
            if(i>j){ unsigned tmp=i; i=j; j=tmp; }
            float d[3]; for(int q=0;q<3;q++) d[q]=rp[j*3+q]-rp[i*3+q];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            C->e0[C->ne]=(unsigned short)i; C->e1[C->ne]=(unsigned short)j;
            C->elen[C->ne]=L; C->ne++;
            if(C->ne>=CLOTH_MAXE) break; } }
    C->seeded=1;
    printf("[cloth] %s mesh %u: %d particles, %d edges, %d pinned%c",
           packStr(P,M->name),mi,C->nv,C->ne,
           ({int p=0; for(int v=0;v<C->nv;v++) p+=C->pin[v]; p;}),10);
    fflush(stdout); }

/* One step. `skinned` is the animated pose in WORLD space -- the pins follow it and the
 * free particles fall away from it. */
static void clothStep(Cloth*C,float*skinned,const float(*sph)[4],int nsph,float dt,float scl){
    if(dt>0.033f) dt=0.033f; if(dt<1e-4f) dt=1e-4f;
    float g=-9.0f*scl*dt*dt;
    for(int v=0;v<C->nv;v++){
        if(C->pin[v]){ for(int k=0;k<3;k++){ C->prv[v][k]=C->cur[v][k]=skinned[v*3+k]; } continue; }
        float np[3];
        for(int k=0;k<3;k++){
            float vel=(C->cur[v][k]-C->prv[v][k])*0.94f;
            np[k]=C->cur[v][k]+vel+(k==1?g:0.f); }
        /* a weak pull back toward the skinned pose, so a garment follows the body it is on
         * instead of being left behind when she walks */
        for(int k=0;k<3;k++) np[k]+=(skinned[v*3+k]-C->cur[v][k])*0.08f;
        for(int k=0;k<3;k++){ C->prv[v][k]=C->cur[v][k]; C->cur[v][k]=np[k]; } }
    for(int pass=0;pass<3;pass++){
        for(int e=0;e<C->ne;e++){
            int i=C->e0[e], j=C->e1[e];
            float d[3]; for(int k=0;k<3;k++) d[k]=C->cur[j][k]-C->cur[i][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) continue;
            float diff=(L-C->elen[e])/L*0.5f;
            float wi=C->pin[i]?0.f:1.f, wj=C->pin[j]?0.f:1.f;
            float tot=wi+wj; if(tot<1e-6f) continue;
            for(int k=0;k<3;k++){
                C->cur[i][k]+=d[k]*diff*(wi/tot)*2.f*0.5f;
                C->cur[j][k]-=d[k]*diff*(wj/tot)*2.f*0.5f; } }
        /* ⛔ AND OUT OF THE BODY, every pass -- resolving edges can push a particle back
         * inside her, so the collision has to be the LAST thing each iteration. */
        for(int v=0;v<C->nv;v++){
            if(C->pin[v]) continue;
            for(int q=0;q<nsph;q++){
                float dx=C->cur[v][0]-sph[q][0], dy=C->cur[v][1]-sph[q][1], dz=C->cur[v][2]-sph[q][2];
                float d2=dx*dx+dy*dy+dz*dz, r=sph[q][3];
                if(d2>=r*r||d2<1e-9f) continue;
                float d=sqrtf(d2), push=(r-d)/d;
                C->cur[v][0]+=dx*push; C->cur[v][1]+=dy*push; C->cur[v][2]+=dz*push; } } } }
'''

sub("static void scnHeadLayer(Pack*P,float Q[][4],int who){", CLOTH + "\nstatic void scnHeadLayer(Pack*P,float Q[][4],int who){",
    "the cloth solver")

sub("        castInit();   /* ⛔ before anything can load a partner: a zero struct is a body inside a body */",
    """        castInit();   /* ⛔ before anything can load a partner: a zero struct is a body inside a body */
        g_clothOff = getenv("REVA_NOCLOTH")?1:0;
        if(g_clothOff) printf("[cloth] OFF (REVA_NOCLOTH) -- garments use the skinned pose%c",10);""",
    "the cloth kill switch")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched (solver in; the draw hook is next)")
