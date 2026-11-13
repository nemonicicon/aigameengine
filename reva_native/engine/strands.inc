/* ---- (11-06) HAIR, LOCK BY LOCK ----------------------------------------------------------------------------------------------------------------------------------
 * REVA's hair hung on the engine's hair chains moved a few strands and worked on the hair in sections, where physics belongs on the whole head, strand by strand.
 * The standard is a high one, for animation: it can be scaled back to run live in a game.
 * A hair mesh built as ribbons -- each lock 2 x N vertices in a row, the two edges of its N points, root first -- on a pack
 * with a `HairStrandsN` node (skate_src/author/strand_hair.py) is moved here, EVERY LOCK ON ITS OWN (hers: 2,362 locks of 10 points):
 *   * its points live in the world, so the body's own motion swings them (inertia, and the air's drag on what moves);
 *   * the root is pinned where her scalp carries it;
 *   * each segment is held toward the way it lies in her hairstyle, from the point above it (a lock's own bend: stiff at the root, nearly free at the tip);
 *   * each point keeps its length from the one above (follow the leader, with its damping: Mueller, Kim & Chentanez 2012, "Fast simulation of inextensible
 *     hair and fur");
 *   * it is kept off her skull (never nearer its middle, in the measured head ellipsoid's own frame, than it lies at rest: engine/body.inc's rule), and out of
 *     her body: every surface round her neck, shoulders, back and upper arms -- skin, the choker, the top, the android's plates -- skinned each frame and pushed
 *     out of along its own normal, with a little friction so a lock rests on a shoulder instead of sliding off. ⛔ Not round capsules: a capsule round a shoulder
 *     that isn't round left the chained hair floating 2 cm off it.
 * Drawn from its own buffers, already in the world, as the hanging skirt's are (engine/cloth.inc: clothPrep's answer 2): each ribbon rebuilt round its lock's new
 * line, its width and its normal turned with it. A hair mesh and its opaque core (the same locks) are simulated once. A held exposure doesn't integrate; the first
 * frame, or a cut, hangs it from her pose and lets it settle first. It's picture: nothing in the rules reads it.
 * REVA_STRANDS=0: off (the control: her hair rides her head). REVA_STRANDK=root,tip: how hard a point is held to its style, a substep. REVA_STRANDSUB: substeps a
 * frame (12). REVA_STRANDDBG=1: once a second, its numbers.
 * ⭐ (11-06) REAL HAIRS. A human hair is about 0.05–0.1 mm thick, usually quoted as about 0.07 mm. Dark hair tends to be at the thick
 * end, around 0.08–0.12 mm. A woman's head has roughly 100,000 of them. Her locks are the guides now, simulated as before, and each carries its share of 100,000 hairs
 * (REVA_HAIRS; 0 draws the locks' ribbons, the control): each its own true width, 0.08 to 0.12 mm (REVA_HAIRMM), at its own place across its lock and a little through it,
 * straight down it as hers lie, as long as its lock or a little shorter, a little lighter or darker than the next, and narrowing over its last part. A hair is far thinner
 * than a pixel, so the renderer draws it about a pixel wide across its own way as the lens sees it, and as see-through as the share of that pixel it really covers
 * (mesh_static.vert and mesh.frag built with STRAND; gfx_vk.c's P_STRAND), and lights it along its way: a hundred lie over one another in the mass and the mass is
 * solid, and at its edges they thin, one by one, to a haze. Drawn blended, after everything solid, in the order the locks are (under-layers first); the locks' ribbons
 * still cast the shadow, and the opaque core under them isn't drawn. Their colour is her hair picture's, where it's opaque (load.inc, Pack.hairRGB; REVA_HAIRRGB).
 * Not done: the hair writes no depth (depth of field and the occlusion see through it), a hair doesn't shadow the one beside it, and the engine's own rasterizer draws the
 * locks. */
#define HS_POOL 8
#define HS_SETTLE_FRAME 8   /* (11-09) frames of hair settling a frame, among every hair on the stage */
#define HS_BODYMAX 48000
#define HS_GRID 131072
#define HS_VOXN 176   /* (11-06) the GPU's closest-point grid of her body: cells a side at most (4 mm over 70 cm) */
typedef struct {
    Pack*pk; int slot, np, ns, warm; unsigned verts, stamp, used, shStamp; float key[3];
    float (*x)[3], (*xp)[3], (*tg0)[3], (*tg1)[3], (*hw)[3], (*nr)[3], (*cor)[3];
    float (*rp)[3]; float *len, *rr, *kk; float (*v)[3]; float *geo;   /* (Jolt's way) each point's velocity; its distance from the root along the lock */
    unsigned vbo[2]; float *vp, *vn;
    /* (11-06) REAL HAIRS: how many to a lock, how many in all, and which mesh draws them; each hair's place across its lock (hu, -1 to 1) and through it (hv), how far
     * down the lock it reaches (hl, a share), its true half-width in metres (hr); each guide point's across (wv: the ribbon's half-width, carried down), normal (nv) and
     * way (tv), from hsRibbons; the hairs' buffers (where, each point twice; which way, as long as how wide; the triangles; which side and how light), what's written
     * into the first two this frame, and how long that took */
    int hrp;   /* (11-06) points a hair is drawn with: its lock's, and REVA_HAIRR between each two on the curve through them */
    int hk, nh, miDraw; float *hu, *hv, *hl, *hr; float (*wv)[3], (*nv)[3], (*tv)[3]; unsigned hvbo[4]; float *hp, *hn; double hms;
    /* (11-06) last frame's skull (world to its ellipsoid and back), so each substep's is between; and the kick meter: each point's place from its root, the last two
     * frames, and a window's sums */
    float (*ro)[3];   /* (11-06) each point's haircut moved off her body at rest (hsSettle), in her rest space */
    int gpu; unsigned gs[GFX_SB_N];   /* (11-06) simulated and drawn on the GPU (hsGpuFrame), and its buffers there, by name (gfx.h's GFX_SB_*) */
    float E0[16], Ei0[16], hc[3]; int eHave; double strayS, strayN; float strayMax; unsigned char *pushed; float jKick[3], jAt[3], jNear, jSide; int jPushed, jPro; char jWhat[48]; float (*jr1)[3], (*jr2)[3]; int jn, jFrames, jMs, jMj, midHits; double jSum50, jSum99; float jMax;
    double ms; int contacts; float deep, stretch;
    int settleLeft;   /* (11-09) frames of its settle still to run: HS_SETTLE_FRAME a frame among every hair on the stage (hsGpuFrame) */
    int chk;          /* (11-11) drawn frames since it was made, for REVA_STRANDCHECK (hsGpuCheck) */
    float lenK; int lenSaid;   /* (11-11) how long its locks are in the world against their lengths in the pack (g_hsLen, below), and whether it's been said */
    int dmp, dmpRest;   /* (11-11) REVA_STRANDDUMP: stepped frames so far, and whether its rest has been written (hsDump) */
    float cutK;         /* (11-11) how much of each lock's own length the haircut is cut to (g_hsTrim): 1 the whole cut */
    float (*d0)[3]; int cutWait;   /* (11-11) each lock root's move last frame, and 1 on the frame after a hang (only recorded then): the cut rule (g_hsCutM) */
} HStrands;
static HStrands g_hs[HS_POOL];
static int g_hsOn=-1, g_hsSub=12, g_hsDbg=0; static float g_hsK0=0.35f, g_hsK1=0.02f, g_hsBend=0.12f, g_hsMu=0.2f;
/* ⭐ (11-06) JOLT'S WAY (g_hsMode 1, the default; REVA_STRANDMODE=0 the old solver). Its numbers: a straight hair bends with a compliance (XPBD: alpha,
 * over the substep squared), the first point past the root is kept within SKIN1 of where the haircut puts it and the second within SKIN2 (Jolt's skinned
 * constraint: how the lock leaves her scalp), damping a second (Jolt: v *= 1 - c dt), friction (Jolt's: Mueller et al. 2017), the speed's ceiling */
static int g_hsMode=1; static float g_hsBendC=2.0e-6f, g_hsDampC=1.2f, g_hsSkin1=0.004f, g_hsSkin2=0.02f, g_hsFric=0.4f, g_hsVmax=4.0f;
/* (11-06) REAL HAIRS: how many on a head (REVA_HAIRS), how thick one is in mm, thinnest to thickest (REVA_HAIRMM=a,b: dark hair's), and their colour when it isn't
 * the hair picture's (REVA_HAIRRGB=r,g,b) */
/* (11-06) the skull and the body between frames, a substep at a time (REVA_STRANDLERP=0: at the frame's end for every substep, as before); the middles of
 * segments near her head kept out of her body too (REVA_STRANDMID=N: N points between a segment's ends, 2 its thirds; 0 off) */
static int g_hsGpu=1;   /* (11-06) hair on the GPU where the renderer computes (REVA_STRANDGPU=0: the processor) */
static int g_hsSnapCut=1, g_hsCutPending=0, g_hsCutActive=0; static unsigned g_hsCutSeen=0xffffffffu;   /* (11-11) a scene line that snaps its marks is a cut: every head hung again on it and the frame after (REVA_STRANDSNAP=0: off) */
static int g_hsCarry=1;   /* (11-12) REVA_STRANDCARRY=0: a head put somewhere whole is hung again, as before (hsCarry) */
static float g_hsFast=3.f;   /* (11-13) REVA_STRANDFAST: a head moved rigidly faster than this (m/s) is carried, as for a put (0: never) */
static int g_hsAdapt=1;   /* (11-12) REVA_STRANDADAPT=0: every frame g_hsSub substeps, however far her head moved in it (hsGpuFrame's sub) */
static float g_hsCutM=0.12f; static int g_hsCutSay=0;   /* (11-11) REVA_STRANDJUMP: a change in a root's move past this many metres in a frame is a cut, and the hair is hung again (0: the teleport rule alone); REVA_STRANDJUMPSAY=1 */
static int g_hsTick=0, g_hsChk=0;   /* (11-11) REVA_STRANDTICK=1: a GPU hair's frame made in a pass that only ticks too, as before (strandPrep). REVA_STRANDCHECK=1: the GPU's copy read back (hsGpuCheck) */
/* ⛔ (11-11) A LOCK'S LENGTHS ARE THE PACK'S, AND ITS POINTS ARE IN THE WORLD. Each segment's length and each point's tether (S->len, S->geo) are measured in the
 * pack at rest, and the step holds the world's points to them with no word of how big the body is drawn. A body drawn at 2.4 times her size had hair 2.4 times
 * too short for her, two points of each lock held to the haircut on her scalp and the rest pulled in toward them (the music video's giants, 11-11). Found by
 * reading the hair back from the GPU (REVA_STRANDCHECK, on Mesa's software Vulkan): a settled head's segments 29% of their own length at her size on a giant,
 * 66% on her at true size. The lengths now follow the body's size against her pack's own (S->lenK = how she's drawn over Pack.scale), so a giant's hair is
 * what hers is at true size (66% there too), and at her pack's own size not a number has moved.
 * ⛔ AND AT HER PACK'S OWN SIZE IT IS STILL SHORT, and that's left as it was: REVA's pack is drawn at 1.55 of its own units (Pack.scale, her true height), so
 * her hair has always been simulated 1/1.55 as long as her haircut is cut. In a picture, a head still held as hung reaches her shoulders and a settled one ends
 * at her jaw. Every picture of her simulated hair so far is the short one, so which is her hair is said at start-up:
 * REVA_STRANDLEN=1 is the whole length (102% in the read-back), REVA_STRANDLEN=0 the pack's lengths whatever her size, as before */
static int g_hsLen=-1;
/* ⛔ (11-11) THE WORMS WERE HER BODY'S SURFACES, NOT HER HAIR'S LENGTH. In a take of a chorus, stray hairs at the outline of the coloured REVAs'
 * hair wriggled like worms, very fast. The first guess was the short lengths above pulling against the whole haircut's places. Measured
 * (REVA_STRANDDUMP, skate_src/author/hairmeasure.py, a head standing still long after settling): that disagreement is there and it's still (her first two
 * segments sit 5% and 3% stretched and don't change from frame to frame), and at the whole length there were MORE restless locks (625 against 287), because
 * more of her hair lies on her. With her body's surfaces off there was 1. Every restless lock was one lying on her forehead, brows and temples, and what
 * moved them is in shaders/strand_sim.comp's comment over sdfAt: a push to the nearest point's flat plane has a step in it wherever the nearest point
 * changes, and a substep is short enough to turn a tenth of a millimetre into 36 mm/s. g_hsField (the default; REVA_STRANDFIELD=0 the old pushes, the
 * control): her body is read as a distance with no steps in it. On the GPU only: the processor's path (REVA_STRANDGPU=0, and a build with no compute) keeps
 * the old pushes */
static int g_hsField=1;
/* ⭐ (11-11) THE SHORT HAIRCUT IS A HAIRCUT (g_hsTrim, the default; REVA_STRANDTRIM=0: the short lengths alone, as before, the control). A coloured REVA's hair
 * was the whole haircut's places with lengths 1/Pack.scale as long (65% at her true size, 59% where a scene drew her at 1.69): two things that disagree. What
 * that did, measured: each time a head is hung (a take's first frame, a cut) every lock began at the whole haircut's length and was pulled in to the short one
 * in one substep, at the speed's ceiling, and 150 to 180 locks on top of her head were left lying in closed loops that never came undone (a strand
 * curled into a loop against the background, in the picture); and her first two segments sat stretched for good. Now a lock that isn't simulated whole is CUT: its places
 * each frame, its rest places, its lengths and how far out it lies are the first g_hsCut of its own line through the haircut (REVA_STRANDCUT, 0.66: the
 * length chosen for them, ending at her jaw, at any size she's drawn). Hung from that, nothing has to pull in: 0 loops. A HairWhole node or
 * REVA_STRANDLEN=1 is the whole cut; REVA_STRANDLEN=0 is the pack's lengths in the world's units and no trim, as it always was */
static int g_hsTrim=1; static float g_hsCut=0.66f;
/* ⭐ (11-11) THE HAIRCUT'S OWN SHELL (g_hsShell, the default; REVA_STRANDSHELL=0: only her skull, as before, the control). Her ears didn't look right
 * under her hair. The haircut lies over her ears as a sheet (looked at: skate_src/author/hairmeasure.py's dumps drawn from each side). Simulated, each lock
 * was kept no nearer her skull's middle than 1.02 of its ellipsoid whatever the haircut said, so the cut's layers fell flat against her head, and then each
 * lock that came down on an ear went one side of it or the other: a slit over each ear with the ear in it (12 of an ear's 80 half-centimetre squares bare).
 * Nothing holds a lock out where the hair under it would: no hair touches another here. So the cut itself is the floor: a point is kept no nearer her
 * skull's middle than it lies in the haircut, wholly while that's within g_hsShellA of the ellipsoid (1.3: her head, and past her ears), easing to the old
 * 1.02 by g_hsShellB (1.7: the ends hang free, under her jaw and on her shoulders). It can always swing out. Both ears: 100% covered, at either length */
static int g_hsShell=1; static float g_hsShellA=1.3f, g_hsShellB=1.7f;
/* ⭐ (11-11) A HAIR'S OWN STIFFNESS (REVA_STRANDHOLD=<cm>[,<share>]; 0 off, the control). A hair held at one end bends under its own weight by the fourth power of
 * its length, so against its length by the cube: a fringe of five centimetres keeps the way it was combed and a lock of twenty hangs. Each point is drawn
 * toward its place in the haircut by g_hsHoldW a substep, wholly within g_hsHoldL cm of its root and less by the cube of how far along the lock it is past
 * that. It is what keeps a fringe on her brow when her head tilts, and it's nothing to the long hair at her sides (at 19 cm, a thousandth) */
static float g_hsHoldL=2.f, g_hsHoldW=0.3f;
/* ⛔ (11-11) THE HOLD WAS A SPRING WITH NOTHING TO STOP IT. A pull of 0.3 of the way a substep, wholly near the root, is a spring at about 30 Hz, and a
 * position pull in this step goes into the speed: on a still head nothing excited it, and under the chorus clip's head snaps (565 mm/s for one frame, every
 * half second) it rang. Measured (skate_src/author/hairmeasure.py's dumps, scratch buzz.py: a point whose motion against the haircut turns back three
 * frames running): 3,262 locks buzzing with the hold, 910 without it, 1,922 with none of the 11-11 fixes. So the hold has its own damping: what's held
 * loses its speed against the haircut's own speed there, wholly where the pull is whole (2 sqrt(k) of it a substep, at most all of it) and less by the same cube down the lock, so a long lock swings as it did.
 * REVA_STRANDHOLD's third number (g_hsHoldD, 1; 0: the spring as it was, the control) */
static float g_hsHoldD=1.f;
static int g_hsSettle=1;   /* (11-06) the haircut settled off her at rest (REVA_STRANDSETTLE=0: as authored) */
static int g_hsLerp=1, g_hsMid=2, g_hsReach=0; static float g_hsCohere=0.4f;   /* g_hsCohere: hair against hair, a frame's pull toward the neighbours' mean speed */   /* g_hsReach: a point gone deeper into her than a cell is found two cells out and taken out (REVA_STRANDREACH=0: not) */
static int g_hsHairR=2;   /* (11-06) drawn points to a lock's segment: each hair on the curve through its lock's points (REVA_HAIRR; 1 the straight segments) */
static int g_hsHairs=-1; static float g_hsHairMm[2]={0.08f,0.12f}, g_hsHairRGB[3]={-1.f,-1.f,-1.f};
static float hsRand(unsigned*s){ *s=*s*1664525u+1013904223u; return (float)((*s>>8)&0xffffffu)*(1.f/16777216.f); }   /* the hairs' own dice: picture, never the rules' rand() */
static float hsSmooth(float a,float b,float x){ float t=(x-a)/(b-a); t=t<0.f?0.f:t>1.f?1.f:t; return t*t*(3.f-2.f*t); }
static void measureHead(Pack*P);
/* the body's surfaces near the head, for one pack: which vertices (picked once), and this frame's place and normal for each, in a grid */
typedef struct { Pack*pk; int n; unsigned short mi[HS_BODYMAX]; unsigned vi[HS_BODYMAX]; } HsBody;
static float g_hsBP[HS_BODYMAX][3], g_hsBN[HS_BODYMAX][3]; static int g_hsNB=0, g_hsHead[HS_GRID], g_hsNext[HS_BODYMAX];
/* (11-06) each picked point at this frame's end (F) and the last frame's (0), by its place in the pick, and whether it's there; g_hsBP and g_hsBN are a substep's,
 * between the two (hsBodyAt) */
static int g_hsBMap[HS_BODYMAX]; static unsigned char g_hsBPro[HS_BODYMAX];   /* (11-06) a point of her head standing out of her skull (an ear, her nose, her lips): by pick, and a substep's */   /* (11-06) a substep's body point: its place in the pick */
static float g_hsBP0[HS_BODYMAX][3], g_hsBN0[HS_BODYMAX][3]; static unsigned char g_hsBOk0[HS_BODYMAX]; static int g_hsBHave0=0;
static float g_hsCell=1.f, g_hsLo[3], g_hsHi[3]; static unsigned g_hsBStamp=0; static int g_hsBSlot=-1; static const Pack*g_hsBPk=NULL;
/* ⭐ (11-09) EACH BODY HAS ITS OWN: which of her points were picked, and where each was at the end of her last frame. Four REVAs on a stage shared one, so each
 * one's was picked again every frame (877,481 vertices looked over) and her last frame was lost: her body stood at the frame's end for all twelve substeps,
 * the jolt that kicked the hair resting on her neck and shoulders (11-06). One a body (its pack and slot) now, the least lately used given up past HS_POOL.
 * One REVA alone is as before: hers is the only one. g_hsB, g_hsBProP, g_hsBOkF, g_hsBPF, g_hsBNF and g_hsBHaveF are the body in use's (hsBodyUse) */
typedef struct { const Pack*pk; int slot, haveF; unsigned used; HsBody B; unsigned char ProP[HS_BODYMAX], OkF[HS_BODYMAX]; float PF[HS_BODYMAX][3], NF[HS_BODYMAX][3]; } HsBodyMem;
static HsBodyMem g_hsBMem[HS_POOL], *g_hsBm=&g_hsBMem[0];
#define g_hsB (g_hsBm->B)
#define g_hsBProP (g_hsBm->ProP)
#define g_hsBOkF (g_hsBm->OkF)
#define g_hsBPF (g_hsBm->PF)
#define g_hsBNF (g_hsBm->NF)
#define g_hsBHaveF (g_hsBm->haveF)
/* this body's own made the one in use: 1 when it holds a frame of hers already (so her body can be had between frames, a substep at a time) */
static int hsBodyUse(const Pack*P,int slot){
    HsBodyMem*m=NULL; for(int i=0;i<HS_POOL;i++) if(g_hsBMem[i].pk==P&&g_hsBMem[i].slot==slot){ m=&g_hsBMem[i]; break; }
    const int had=m!=NULL;
    if(!m){ unsigned old=0xffffffffu; for(int i=0;i<HS_POOL;i++){ if(!g_hsBMem[i].pk){ m=&g_hsBMem[i]; break; } if(g_hsBMem[i].used<old){ old=g_hsBMem[i].used; m=&g_hsBMem[i]; } }
        m->pk=P; m->slot=slot; m->haveF=0; m->B.pk=NULL; m->B.n=0; }
    m->used=g_frameStamp; g_hsBm=m; return had&&m->haveF; }
static unsigned hsHash(int a,int b,int c){ return ((unsigned)a*73856093u^(unsigned)b*19349663u^(unsigned)c*83492791u)&(HS_GRID-1); }
static int hsIsHair(Pack*P,unsigned mi){ const char*n=packStr(P,P->meshes[mi].name); if(!n) return 0;
    for(const char*s=n;*s;s++) if((s[0]=='h'||s[0]=='H')&&(s[1]=='a')&&(s[2]=='i')&&(s[3]=='r')) return 1;
    return 0; }
static int hsSkipBody(const char*nm){ if(!nm) return 1;
    static const char*no[]={"hair","Hair","braid","eye","Eye","lash","brow","teeth","tongue","interior","mouth","scalp",NULL};
    for(int i=0;no[i];i++) if(strstr(nm,no[i])) return 1;
    return 0; }
static void hsAffInv(const float*m,float*o){   /* an affine 4x4 (column-major) inverted */
    const float a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g); if(fabsf(det)<1e-20f) det=1e-20f; const float id=1.f/det;
    o[0]=(e*i-f*h)*id; o[4]=(c*h-b*i)*id; o[8]=(b*f-c*e)*id; o[1]=(f*g-d*i)*id; o[5]=(a*i-c*g)*id; o[9]=(c*d-a*f)*id; o[2]=(d*h-e*g)*id; o[6]=(b*g-a*h)*id; o[10]=(a*e-b*d)*id;
    o[3]=o[7]=o[11]=0.f; o[15]=1.f;
    for(int r=0;r<3;r++) o[12+r]=-(o[r]*m[12]+o[4+r]*m[13]+o[8+r]*m[14]); }
static void hsXf(const float*m,const float*p,float*o){ for(int r=0;r<3;r++) o[r]=m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r]; }
/* the body near the head, picked once a pack: vertices of every mesh that isn't hair or a face part, within 3 head-radii of the head's joint at rest, and
 * not riding the head itself (the skull is the ellipsoid's) */
static void hsBodyPick(Pack*P){
    HsBody*B=&g_hsB; B->pk=P; B->n=0;
    if(!P->headOK) measureHead(P);
    const int h=P->pb[5]; if(h<0||(unsigned)h>=P->nNodes||P->headOK<=0) return;
    const float*hw=P->headW0; float R=0.f; for(int k=0;k<3;k++) if(P->headR[k]>R) R=P->headR[k]; R*=3.2f;
    float Mo0[16],MI0[16]; const int hm=headMatsM(P,P->headW0,Mo0,MI0);
    unsigned tot=0;
    for(unsigned m=0;m<P->nMeshes;m++){ PMesh*M=&P->meshes[m]; if((M->flags&4)&&M->boneCount&&M->boneCount<=MAXB&&!hsSkipBody(packStr(P,M->name))) tot+=M->verts; }
    const unsigned step=tot>HS_BODYMAX*2u?tot/(HS_BODYMAX*2u)+1u:1u;
    for(unsigned m=0;m<P->nMeshes&&B->n<HS_BODYMAX;m++){ PMesh*M=&P->meshes[m];
        if(!(M->flags&4)||!M->boneCount||M->boneCount>MAXB||hsSkipBody(packStr(P,M->name))) continue;
        const float*rp=(const float*)(P->pk+M->offPos),*bd=(const float*)(P->pk+M->offBind);
        const unsigned char*jp=P->pk+M->offJoints,*wp=P->pk+M->offWeights; const unsigned short*bm=(const unsigned short*)(P->pk+M->offBoneMap);
        for(unsigned v=0;v<M->verts&&B->n<HS_BODYMAX;v+=step){
            int best=0,bw=-1; for(int q=0;q<4;q++) if(wp[v*4+q]>bw){ bw=wp[v*4+q]; best=jp[v*4+q]; }
            float r9[3]; for(int k=0;k<3;k++) r9[k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k];
            /* her head's own skin is the skull's ellipsoid's to keep the hair off -- except what stands out of it: her ears, her nose, her lips, her
             * chin. ⛔ (11-06) left out, her ears poked through her sleek hair (the bench, from her right) */
            int pro9=0; if(best<(int)M->boneCount&&bm[best]==(unsigned)h&&bw>=128){ if(!hm) continue; float u9[3]; hsXf(MI0,r9,u9); if(u9[0]*u9[0]+u9[1]*u9[1]+u9[2]*u9[2]<1.03f*1.03f) continue; pro9=1; }
            const float dx=r9[0]-hw[12],dy=r9[1]-hw[13],dz=r9[2]-hw[14];
            if(dx*dx+dy*dy+dz*dz>R*R) continue;
            g_hsBProP[B->n]=(unsigned char)pro9; B->mi[B->n]=(unsigned short)m; B->vi[B->n]=v; B->n++; } }
    printf("[strands] %s: %d surface points of her body near her head to keep the hair out of (within %.2f rig u of the head's joint)\n",P->label,B->n,R); fflush(stdout); }
/* ⭐ (11-06) A SUBSTEP'S SURFACES: each picked point u of the way from where it was at the end of last frame to where it is at the end of this one, and the grid of
 * them. ⛔ Before, the body stood at the frame's end for all twelve substeps while the hair's roots walked there from last frame's pose: every frame she moved,
 * the surfaces round her neck and shoulders jumped ahead of the hair resting on them and kicked it (seen as erratic movement) */
static void hsBodyAt(float u){
    const int lerp=g_hsBHave0&&g_hsLerp&&u<1.f; g_hsNB=0;
    for(int k=0;k<3;k++){ g_hsLo[k]=1e30f; g_hsHi[k]=-1e30f; }
    for(int i=0;i<g_hsB.n;i++){ if(!g_hsBOkF[i]) continue; float*o=g_hsBP[g_hsNB],*on=g_hsBN[g_hsNB];
        if(lerp&&g_hsBOk0[i]){ for(int k=0;k<3;k++){ o[k]=g_hsBP0[i][k]+(g_hsBPF[i][k]-g_hsBP0[i][k])*u; on[k]=g_hsBN0[i][k]+(g_hsBNF[i][k]-g_hsBN0[i][k])*u; }
            const float l=sqrtf(on[0]*on[0]+on[1]*on[1]+on[2]*on[2]); if(l<1e-9f) memcpy(on,g_hsBNF[i],12); else for(int k=0;k<3;k++) on[k]/=l; }
        else { memcpy(o,g_hsBPF[i],12); memcpy(on,g_hsBNF[i],12); }
        for(int k=0;k<3;k++){ if(o[k]<g_hsLo[k]) g_hsLo[k]=o[k]; if(o[k]>g_hsHi[k]) g_hsHi[k]=o[k]; }
        g_hsBPro[g_hsNB]=g_hsBProP[i]; g_hsBMap[g_hsNB]=i; g_hsNB++; }
    for(int i=0;i<HS_GRID;i++) g_hsHead[i]=-1;
    for(int i=0;i<g_hsNB;i++){ const unsigned hb=hsHash((int)floorf(g_hsBP[i][0]/g_hsCell),(int)floorf(g_hsBP[i][1]/g_hsCell),(int)floorf(g_hsBP[i][2]/g_hsCell));
        g_hsNext[i]=g_hsHead[hb]; g_hsHead[hb]=i; } }
/* this frame's surfaces: each picked vertex skinned with its own mesh's palette, into the world, and a grid of them */
static void hsBodyFrame(Pack*P,M4*world,const float*model,float sc,int keep){
    if(g_hsB.pk!=P){ hsBodyPick(P); keep=0; }
    static float bpal[MAXB*16]; int lastM=-1; g_hsNB=0;
    g_hsBHave0=keep&&g_hsBHaveF; if(g_hsBHave0){ memcpy(g_hsBP0,g_hsBPF,(size_t)g_hsB.n*12); memcpy(g_hsBN0,g_hsBNF,(size_t)g_hsB.n*12); memcpy(g_hsBOk0,g_hsBOkF,(size_t)g_hsB.n); }
    memset(g_hsBOkF,0,(size_t)g_hsB.n); g_hsBHaveF=1;
    for(int i=0;i<g_hsB.n;i++){ const unsigned m=g_hsB.mi[i]; PMesh*M=&P->meshes[m]; if(MOFF(P,m)) continue;
        if((int)m!=lastM){ lastM=(int)m; const unsigned short*bm=(const unsigned short*)(P->pk+M->offBoneMap); const float*ibm=(const float*)(P->pk+M->offIBM);
            for(unsigned b=0;b<M->boneCount;b++){ M4 pm; m4_mul(pm,world[bm[b]<P->nNodes?bm[b]:0],*(M4*)(ibm+b*16)); memcpy(bpal+b*16,pm,64); } }
        const unsigned v=g_hsB.vi[i]; const float*rp=(const float*)(P->pk+M->offPos),*bd=(const float*)(P->pk+M->offBind);
        const float*np9=(M->flags&1)?(const float*)(P->pk+M->offNorm):NULL; const unsigned char*jp=P->pk+M->offJoints,*wp=P->pk+M->offWeights;
        float b3[3],n3[3]={0.f,1.f,0.f}; for(int k=0;k<3;k++) b3[k]=bd[k]*rp[v*3]+bd[4+k]*rp[v*3+1]+bd[8+k]*rp[v*3+2]+bd[12+k];
        if(np9) for(int k=0;k<3;k++) n3[k]=bd[k]*np9[v*3]+bd[4+k]*np9[v*3+1]+bd[8+k]*np9[v*3+2];
        float a[3]={0,0,0},an[3]={0,0,0},ws=0.f;
        for(int q=0;q<4;q++){ const float w=wp[v*4+q]*(1.f/255.f); if(w<=0.f) continue; const float*pm=bpal+(jp[v*4+q]<M->boneCount?jp[v*4+q]:0)*16;
            for(int k=0;k<3;k++){ a[k]+=w*(pm[k]*b3[0]+pm[4+k]*b3[1]+pm[8+k]*b3[2]+pm[12+k]); an[k]+=w*(pm[k]*n3[0]+pm[4+k]*n3[1]+pm[8+k]*n3[2]); } ws+=w; }
        if(ws<1e-4f){ memcpy(a,b3,12); memcpy(an,n3,12); }
        float*o=g_hsBPF[i],*on=g_hsBNF[i]; hsXf(model,a,o);
        for(int k=0;k<3;k++) on[k]=model[k]*an[0]+model[4+k]*an[1]+model[8+k]*an[2];
        const float l=sqrtf(on[0]*on[0]+on[1]*on[1]+on[2]*on[2]); if(l<1e-9f) continue; on[0]/=l; on[1]/=l; on[2]/=l;
        g_hsBOkF[i]=1; }
    g_hsCell=0.015f*sc*(P->headR[0]>0.f?(P->headR[0]+P->headR[1]+P->headR[2])/(3.f*0.1f):1.f);   /* ~1.5 cm on a head the size of hers */
    hsBodyAt(1.f); }
/* a point out of her body: the nearest surface point within a cell, and out along its normal to a hair's breadth off it. Returns how deep it was (0: clear) */
static float hsBodyPush(float*x,float*xp,float margin,float mu){
    if(x[0]<g_hsLo[0]-g_hsCell||x[0]>g_hsHi[0]+g_hsCell||x[1]<g_hsLo[1]-g_hsCell||x[1]>g_hsHi[1]+g_hsCell||x[2]<g_hsLo[2]-g_hsCell||x[2]>g_hsHi[2]+g_hsCell) return 0.f;
    const float fx=x[0]/g_hsCell,fy=x[1]/g_hsCell,fz=x[2]/g_hsCell; const int cx=(int)floorf(fx),cy=(int)floorf(fy),cz=(int)floorf(fz);
    const int ox=(fx-cx<0.5f)?-1:0, oy=(fy-cy<0.5f)?-1:0, oz=(fz-cz<0.5f)?-1:0;
    int best=-1; float bd=g_hsCell*g_hsCell;
    for(int a=0;a<2;a++) for(int b=0;b<2;b++) for(int c=0;c<2;c++)
        for(int i=g_hsHead[hsHash(cx+ox+a,cy+oy+b,cz+oz+c)];i>=0;i=g_hsNext[i]){ const float*s=g_hsBP[i];
            const float dx=x[0]-s[0],dy=x[1]-s[1],dz=x[2]-s[2],d2=dx*dx+dy*dy+dz*dz; if(d2<bd){ bd=d2; best=i; } }
    if(best<0) return 0.f;
    const float*s=g_hsBP[best],*n=g_hsBN[best];
    const float d=(x[0]-s[0])*n[0]+(x[1]-s[1])*n[1]+(x[2]-s[2])*n[2];
    if(d>=margin) return 0.f;
    const float push=margin-d; for(int k=0;k<3;k++) x[k]+=n[k]*push;
    /* friction: what it was moving along the surface, cut by mu */
    float v[3]={x[0]-xp[0],x[1]-xp[1],x[2]-xp[2]}; const float vn=v[0]*n[0]+v[1]*n[1]+v[2]*n[2];
    for(int k=0;k<3;k++) xp[k]+=(v[k]-n[k]*vn)*mu;
    return push; }
static void hsFree(HStrands*S){ if(S->gs[0]){ for(int k=0;k<GFX_SB_N;k++) if(k!=GFX_SB_HAIRP&&k!=GFX_SB_HAIRN&&k!=GFX_SB_RIBP&&k!=GFX_SB_RIBN) gfxBufferDrop(S->gs[k]); memset(S->gs,0,sizeof S->gs); } S->gpu=0;
    free(S->ro); S->ro=NULL; free(S->pushed); S->pushed=NULL; free(S->jr1); free(S->jr2); S->jr1=S->jr2=NULL; S->jn=0; S->eHave=0; free(S->hu); free(S->hv); free(S->hl); free(S->hr); free(S->wv); free(S->nv); free(S->tv); free(S->hp); free(S->hn);
    S->hu=S->hv=S->hl=S->hr=S->hp=S->hn=NULL; S->wv=S->nv=S->tv=NULL; S->hk=S->nh=0; if(S->hvbo[0]){ gfxBuffersDrop(4,S->hvbo); memset(S->hvbo,0,sizeof S->hvbo); }
    free(S->d0); S->d0=NULL; free(S->v); free(S->geo); S->v=NULL; S->geo=NULL; free(S->x); free(S->xp); free(S->tg0); free(S->tg1); free(S->hw); free(S->nr); free(S->cor); free(S->rp); free(S->len); free(S->rr); free(S->kk); free(S->vp); free(S->vn);
    S->x=S->xp=S->tg0=S->tg1=S->hw=S->nr=S->cor=S->rp=NULL; S->len=S->rr=S->kk=S->vp=S->vn=NULL; }
/* ⭐ (11-06) HAIR ON THE GPU, under a compute path. Where the renderer computes
 * (gfxCompute), a lock's state lives in the GPU's own memory, and each frame the processor hands it only what needs her skeleton: the haircut's places for the
 * frame (hsTargets), her body near her head at the frame's end and the last, and their grid. The GPU does the frame's substeps (shaders/strand_sim.comp), hair
 * against hair and the speed's ceiling (strand_cohere.comp), the ribbons the shadow is cast with (strand_ribbon.comp) and the hundred thousand hairs
 * (strand_hairs.comp), and they're drawn from where it wrote them: none of it comes back. The shaders are the processor's own lines, so REVA_STRANDGPU=0 is
 * the same hair, moved by the processor, for a control; the kick meter (REVA_STRANDDBG) is only there */
static void hsGpuInit(HStrands*S){
    const int np=S->np, n=S->ns*np, nh=S->nh>0?S->nh:1, nr=S->hrp>1?S->hrp:2; const long n12=(long)n*12;
    gfxBuffersNew(GFX_SB_N,S->gs);
    if(S->hvbo[0]){ gfxBufferDrop(S->hvbo[0]); gfxBufferDrop(S->hvbo[1]); }   /* the processor's per-frame hairs: the GPU's are drawn instead */
    S->hvbo[0]=S->gs[GFX_SB_HAIRP]; S->hvbo[1]=S->gs[GFX_SB_HAIRN]; S->vbo[0]=S->gs[GFX_SB_RIBP]; S->vbo[1]=S->gs[GFX_SB_RIBN];
    for(int k=GFX_SB_X;k<=GFX_SB_NR;k++) gfxBufferGpu(S->gs[k],n12,NULL);
    gfxBufferGpu(S->gs[GFX_SB_BODY],(long)HS_BODYMAX*64,NULL); gfxBufferGpu(S->gs[GFX_SB_GRID],(long)(HS_GRID+HS_BODYMAX)*4,NULL);
    { float*L=malloc((size_t)n*16); if(L){ for(int p=0;p<n;p++){ L[p*4]=S->len[p]; L[p*4+1]=S->geo[p]; L[p*4+2]=S->rr[p]; L[p*4+3]=0.f; } gfxBufferGpu(S->gs[GFX_SB_LOCK],(long)n*16,L); free(L); } }
    { float*Hd=calloc((size_t)nh,16); if(Hd){ for(int h=0;h<S->nh;h++){ Hd[h*4]=S->hu[h]; Hd[h*4+1]=S->hv[h]; Hd[h*4+2]=S->hl[h]; Hd[h*4+3]=S->hr[h]; } gfxBufferGpu(S->gs[GFX_SB_HAIR],(long)nh*16,Hd); free(Hd); } }
    gfxBufferGpu(S->gs[GFX_SB_RIBP],(long)S->verts*12,NULL); gfxBufferGpu(S->gs[GFX_SB_RIBN],(long)S->verts*12,NULL);
    gfxBufferGpu(S->gs[GFX_SB_WNT],(long)n*36,NULL);
    gfxBufferGpu(S->gs[GFX_SB_HAIRP],(long)nh*nr*24,NULL); gfxBufferGpu(S->gs[GFX_SB_HAIRN],(long)nh*nr*24,NULL);
    gfxBufferGpu(S->gs[GFX_SB_VGRID],64L*64L*64L*16L,NULL);
    gfxBufferGpu(S->gs[GFX_SB_VOX],(long)HS_VOXN*HS_VOXN*HS_VOXN*4L,NULL);   /* her body's closest-point grid */
    printf("[strands] %s: on the GPU: %d locks and %d hairs simulated and drawn there (REVA_STRANDGPU=0: the processor)\n",S->pk->label,S->ns,S->nh); fflush(stdout); }
/* the frame's step on the GPU: what it needs this frame, and its numbers. PRM: 0-15 her skull last frame (world to its ellipsoid), 16-31 and back, 32-63 this frame's;
 * 64 locks, 65 points a lock, 66 substeps, 67 a substep's dt, 68 the body's scale, 69 gravity, 70 damping a second, 71 the margin, 72 the bend's compliance, 73-74
 * how far the first two points may leave the haircut, 75 friction, 76 the skull known, 77 points between a segment's ends, 78 the body grid's cell, 79-84 its box,
 * 85 her body's points, 86 between frames, 87 how far in a middle counts, 88 the step along by an ear, 89 the speed's ceiling, 90 hair against hair, 91 its cell,
 * 92-94 its corner, 95 the settle's dt, 96 its cells a side, 97 hairs, 98 hairs a lock, 99 drawn points a hair */
/* ⭐ (11-12) A BODY PUT SOMEWHERE WHOLE KEEPS ITS HAIR. The music video's copies hop one column on every beat (the original video's strip of faces slides a
 * column a beat), and a head that jumps was hung again: drawn as its haircut is cut for a frame or two while it settled (the settle is a budget
 * shared by every head, HS_SETTLE_FRAME), so the critic saw the hair puff on every beat. When her head's move takes last frame's roots onto this
 * frame's, whole (within 1.5 cm) and turned less than 30 degrees, the hair goes with it instead: its places, last places and speeds by that move,
 * its shape and its swing kept. A bigger turn (a body snapped into a new pose on an edit's cut) is still hung again: carried, its hair would hold
 * the old pose's shape and swing down through the picture. On the GPU the state is read back first (the device waits; a carry is a few a take).
 * D takes a world point by her head's move. 0 if it couldn't be read back (then she's hung again) */
static int hsCarry(HStrands*S,const float*D){
    const int n=S->ns*S->np; const long n12=(long)n*12;
    if(S->gpu&&(!gfxBufferRead(S->gs[GFX_SB_X],S->x,n12)||!gfxBufferRead(S->gs[GFX_SB_XP],S->xp,n12)||!gfxBufferRead(S->gs[GFX_SB_V],S->v,n12))) return 0;
    for(int p=0;p<n;p++){ float o[3]; hsXf(D,S->x[p],o); memcpy(S->x[p],o,12); hsXf(D,S->xp[p],o); memcpy(S->xp[p],o,12);
        const float*v=S->v[p]; o[0]=D[0]*v[0]+D[4]*v[1]+D[8]*v[2]; o[1]=D[1]*v[0]+D[5]*v[1]+D[9]*v[2]; o[2]=D[2]*v[0]+D[6]*v[1]+D[10]*v[2]; memcpy(S->v[p],o,12); }
    if(S->gpu){ gfxBufferGpu(S->gs[GFX_SB_X],n12,S->x); gfxBufferGpu(S->gs[GFX_SB_XP],n12,S->xp); gfxBufferGpu(S->gs[GFX_SB_V],n12,S->v); }
    return 1; }
/* sub: this frame's substeps (g_hsSub, or more on a frame her head moved far: the step's own, never the settle's) */
static void hsGpuFrame(HStrands*S,float sc,const float*E,const float*Ei,int haveE,int hang,float dt,int sub){
    const int np=S->np, n=S->ns*np; const long n12=(long)n*12;
    if(sub<1||hang||S->settleLeft>0) sub=g_hsSub;
    GfxStrands st; memset(&st,0,sizeof st); st.id=(unsigned)(S-g_hs); memcpy(st.buf,S->gs,sizeof st.buf);
    st.ns=S->ns; st.np=np; st.nh=S->nh; st.sub=sub; st.vdim=64;
    if(hang){ memcpy(S->tg0,S->tg1,(size_t)n*12); gfxBufferGpu(S->gs[GFX_SB_X],n12,S->tg1); gfxBufferGpu(S->gs[GFX_SB_XP],n12,S->tg1);
        float*z=calloc((size_t)n,12); if(z){ gfxBufferGpu(S->gs[GFX_SB_V],n12,z); free(z); } S->settleLeft=40; }
    /* ⭐ (11-09) THE SETTLE, A BUDGET A FRAME SHARED BY EVERY HAIR. Four REVAs on a stage each settled their hair in the same first frame, 4 x 40
     * frames x 12 substeps in one submission, and the GPU ran past the 2 s Windows gives a frame (the take stopped after 2 frames). Now at most
     * HS_SETTLE_FRAME frames of settling go in a frame, shared out in turn; a hair waiting its turn is held as it was hung (drawn, not stepped:
     * settle -1). One REVA settles as before, over 5 frames instead of 1 */
    if(S->settleLeft>0){ static unsigned st9=0xffffffffu; static int used9=0; if(st9!=g_frameStamp){ st9=g_frameStamp; used9=0; }
        int per9=HS_SETTLE_FRAME-used9; if(per9>S->settleLeft) per9=S->settleLeft;
        if(per9>0){ st.settle=per9; S->settleLeft-=per9; used9+=per9; }
        else { st.settle=-1; static int said9=0; if(!said9){ said9=1; printf("[strands] more heads were hung at once than settle in a frame: %d frames of settling a frame among them all, in turn, and a head waiting is held as it was hung (HS_SETTLE_FRAME)\n",HS_SETTLE_FRAME); fflush(stdout); }
               if(g_hsCutSay){ printf("[strands] %s: held as hung this frame (%u), %d frames of its settle to come\n",S->pk?S->pk->label:"?",g_frameStamp,S->settleLeft); fflush(stdout); } } }
    gfxBufferGpu(S->gs[GFX_SB_TG0],n12,S->tg0); gfxBufferGpu(S->gs[GFX_SB_TG1],n12,S->tg1); gfxBufferGpu(S->gs[GFX_SB_HW],n12,S->hw); gfxBufferGpu(S->gs[GFX_SB_NR],n12,S->nr);
    /* her body as the grid has it, a point to four vec4s: last frame's place and whether it was there, its normal and whether it stands out of her skull, this frame's, its normal */
    { static float body[HS_BODYMAX*16]; for(int c=0;c<g_hsNB;c++){ const int i=g_hsBMap[c]; float*o=body+c*16; const int h0=g_hsBHave0&&g_hsBOk0[i];
          memcpy(o,h0?g_hsBP0[i]:g_hsBPF[i],12); o[3]=h0?1.f:0.f; memcpy(o+4,h0?g_hsBN0[i]:g_hsBNF[i],12); o[7]=(float)g_hsBPro[c]; memcpy(o+8,g_hsBPF[i],12); o[11]=1.f; memcpy(o+12,g_hsBNF[i],12); o[15]=0.f; }
      if(g_hsNB) gfxBufferGpu(S->gs[GFX_SB_BODY],(long)g_hsNB*64,body); }
    float*q=st.prm;
    if(haveE){ const int was=S->eHave&&g_hsLerp&&!hang; memcpy(q,was?S->E0:E,64); memcpy(q+16,was?S->Ei0:Ei,64); memcpy(q+32,E,64); memcpy(q+48,Ei,64); }
    q[64]=(float)S->ns; q[65]=(float)np; q[66]=(float)sub; q[67]=dt/(float)sub; q[68]=sc; q[69]=(float)CLUB_UNITS_PER_S2; q[70]=g_hsDampC; q[71]=0.004f*sc; q[72]=g_hsBendC;
    q[73]=g_hsSkin1*sc; q[74]=g_hsSkin2*sc; q[75]=g_hsFric; q[76]=haveE?1.f:0.f; q[77]=(float)g_hsMid; q[78]=g_hsCell; memcpy(q+79,g_hsLo,12); memcpy(q+82,g_hsHi,12); q[85]=(float)g_hsNB;
    q[86]=(g_hsLerp&&g_hsBHave0)?1.f:0.f; q[87]=0.005f*sc; q[88]=0.003f*sc; q[89]=g_hsVmax*sc; q[90]=g_hsCohere;
    { const float vc=0.025f*sc; q[91]=vc; for(int k=0;k<3;k++) q[92+k]=(haveE?Ei[12+k]:S->hc[k])-32.f*vc; }
    { static int dbg9=-1; if(dbg9<0){ const char*e9=getenv("REVA_STRANDSEGDBG"); dbg9=(e9&&*e9)?atoi(e9):0; } q[109]=(float)dbg9; }
    { static int off9=-1; if(off9<0){ const char*e9=getenv("REVA_STRANDOFF"); off9=(e9&&*e9)?atoi(e9):0; }   /* (11-11) REVA_STRANDOFF, for finding a fault: 1 her body isn't there, 2 her skull isn't, 3 neither */
      if(off9&1){ q[85]=0.f; } if(off9&2){ q[76]=0.f; } }
    q[95]=(1.f/60.f)/(float)g_hsSub; q[96]=64.f; q[97]=(float)S->nh; q[98]=(float)S->hk; q[99]=(float)S->hrp;
    q[110]=S->lenK;   /* (11-11) 110: how long a lock is in the world, against its lengths in the pack (g_hsLen) */
    q[111]=(float)g_hsField;   /* (11-11) 111: her body as a distance with no steps in it (g_hsField) */
    q[112]=g_hsShell?g_hsShellA:0.f; q[113]=g_hsShellB;   /* (11-11) 112-113: the haircut's own shell, kept whole to the first and eased off by the second (g_hsShell; 0: only her skull) */
    q[114]=g_hsHoldL*0.01f*sc; q[115]=g_hsHoldW; q[116]=g_hsHoldD;   /* 116: the hold's damping (g_hsHoldD) */          /* (11-11) 114-115: a hair's own stiffness: how far from its root it holds wholly (the world's units; 0 off), and by how much a substep (g_hsHoldL) */
    /* her body's closest-point grid: 4 mm cells over her body's box and the reach round it (no more than HS_VOXN a side: bigger cells, then), 101-103 cells a side,
     * 104 a cell, 105-107 its corner, 108 the reach (the hash grid's cell: a point further than that from all of her isn't near her) */
    { const float R=g_hsCell; float ext=0.f; for(int k=0;k<3;k++){ q[105+k]=g_hsLo[k]-R; const float e=g_hsHi[k]+R-q[105+k]; if(e>ext) ext=e; }
      float vs=0.004f*sc; if(ext/vs>(float)HS_VOXN) vs=ext/(float)HS_VOXN; q[104]=vs; q[108]=R;
      for(int k=0;k<3;k++){ int d9=(int)ceilf((g_hsHi[k]+R-q[105+k])/vs); if(d9<1) d9=1; if(d9>HS_VOXN) d9=HS_VOXN; q[101+k]=(float)d9; }
      if(!g_hsNB) q[101]=q[102]=q[103]=0.f; }
    gfxStrands(&st); }
/* (11-11) REVA_STRANDCHECK=1: WHAT THE GPU HOLDS, READ BACK, once a head, when its first drawn frame has run: its once-only data (each point's lengths, each
 * hair's numbers) held against the processor's, how many of its points are further from their root than their lock is long, and how long its segments are
 * against their own length at her size. It's how both faults of 11-11 were held (gfx_vk.c's gpuKeep; g_hsLen above) */
static void hsGpuCheck(HStrands*S,float sc){
    const int np=S->np, n=S->ns*np, nh=S->nh; int badL=0, badH=0, far9=0;
    float*L=malloc((size_t)n*16), *Hd=malloc((size_t)(nh>0?nh:1)*16), *X=malloc((size_t)n*12);
    if(!L||!Hd||!X){ free(L); free(Hd); free(X); return; }
    const int okL=gfxBufferRead(S->gs[GFX_SB_LOCK],L,(long)n*16), okH=nh>0&&gfxBufferRead(S->gs[GFX_SB_HAIR],Hd,(long)nh*16), okX=gfxBufferRead(S->gs[GFX_SB_X],X,(long)n*12);
    if(okL) for(int p=0;p<n;p++) if(!(L[p*4]==S->len[p]&&L[p*4+1]==S->geo[p]&&L[p*4+2]==S->rr[p])) badL++;
    if(okH) for(int h=0;h<nh;h++) if(!(Hd[h*4]==S->hu[h]&&Hd[h*4+1]==S->hv[h]&&Hd[h*4+2]==S->hl[h]&&Hd[h*4+3]==S->hr[h])) badH++;
    double is9=0.0, own9=0.0;   /* how long its segments are, against their own length at her size */
    if(okX) for(int p=0;p<n;p++){ const float*r=X+(p-p%np)*3,*x=X+p*3; const float d=sqrtf((x[0]-r[0])*(x[0]-r[0])+(x[1]-r[1])*(x[1]-r[1])+(x[2]-r[2])*(x[2]-r[2])); if(!(d<=1.5f*S->geo[p]*sc+0.01f*sc)) far9++;
        if(p%np){ const float*q=x-3; const float l=sqrtf((x[0]-q[0])*(x[0]-q[0])+(x[1]-q[1])*(x[1]-q[1])+(x[2]-q[2])*(x[2]-q[2])); if(l==l&&l<1e6f){ is9+=l; own9+=S->len[p]*sc; } } }
    printf("[strands] %s: read back from the GPU (REVA_STRANDCHECK): %d of %d points' lengths and %d of %d hairs' numbers are not the processor's, %d points are further from their root than their lock is long, and its segments are %.0f%% of their own length at her size (x%.2f)%s\n",
           S->pk->label,okL?badL:-1,n,okH?badH:-1,nh,okX?far9:-1,own9>0.0?100.0*is9/own9:-1.0,sc,(badL||badH||far9)?"  <-- WRONG":""); fflush(stdout);
    free(L); free(Hd); free(X); }
/* (11-11) REVA_STRANDDUMP=<folder>[,every[,from]]: A HEAD'S POINTS WRITTEN OUT TO MEASURE, every so many stepped frames from the from-th: where each point is, its speed
 * and where the haircut had it (the frame the step was made for), as the GPU left them (or the processor, REVA_STRANDGPU=0), with her skull's frame; and once, each
 * point's lengths and its place at rest. skate_src/author/hairmeasure.py reads them: how fast a still head's hair is still moving, which locks, and how curled.
 * It's how the worms of 11-11 were measured (stray hairs wriggling very fast) */
static const char*g_hsDumpDir=NULL; static int g_hsDumpEvery=1, g_hsDumpFrom=0, g_hsDumpBody=0;
static void hsDump(HStrands*S,float sc,const float*X,const float*V){
    const int n=S->ns*S->np, f=S->dmp++; if(f<g_hsDumpFrom||((f-g_hsDumpFrom)%g_hsDumpEvery)) return;
    char nm[64]; int k9=0; for(const char*c=S->pk->label;*c&&k9<60;c++) nm[k9++]=(*c==' '||*c=='/'||*c=='\\')?'_':*c; nm[k9]=0;
    char pth[512]; const int hd[4]={0x52494148,S->ns,S->np,f}; const float fh[4]={sc,S->lenK,(float)g_chDt,(float)g_hsSub};
    if(!S->dmpRest){ S->dmpRest=1; snprintf(pth,sizeof pth,"%s/%s_s%d_rest.bin",g_hsDumpDir,nm,S->slot); FILE*o=fopen(pth,"wb");
        if(o){ fwrite(hd,4,4,o); fwrite(fh,4,4,o); fwrite(S->len,4,(size_t)n,o); fwrite(S->geo,4,(size_t)n,o); fwrite(S->rr,4,(size_t)n,o); fwrite(S->rp,12,(size_t)n,o);
            if(S->ro){ fwrite(S->ro,12,(size_t)n,o); } fclose(o); }
        else { printf("[strands] REVA_STRANDDUMP: %s can't be written\n",pth); fflush(stdout); }
        /* her body near her head as the hair is kept out of it, this frame: a point's place, its normal, whether it stands out of her skull, its mesh; and the meshes' names */
        snprintf(pth,sizeof pth,"%s/%s_s%d_body.bin",g_hsDumpDir,nm,S->slot); o=fopen(pth,"wb");
        if(o){ int cnt=0; for(int i=0;i<g_hsB.n;i++) if(g_hsBOkF[i]) cnt++; fwrite(&cnt,4,1,o);
            for(int i=0;i<g_hsB.n;i++){ if(!g_hsBOkF[i]) continue; const float e9[2]={(float)g_hsBProP[i],(float)g_hsB.mi[i]}; fwrite(g_hsBPF[i],4,3,o); fwrite(g_hsBNF[i],4,3,o); fwrite(e9,4,2,o); }
            fclose(o); }
        snprintf(pth,sizeof pth,"%s/%s_s%d_meshes.txt",g_hsDumpDir,nm,S->slot); o=fopen(pth,"w");
        if(o){ for(unsigned m=0;m<S->pk->nMeshes;m++){ const char*n9=packStr(S->pk,S->pk->meshes[m].name); fprintf(o,"%u %u %s%s\n",m,S->pk->meshes[m].verts,n9?n9:"?",MOFF(S->pk,m)?" (off)":""); } fclose(o); } }
    snprintf(pth,sizeof pth,"%s/%s_s%d_%05d.bin",g_hsDumpDir,nm,S->slot,f); FILE*o=fopen(pth,"wb"); if(!o) return;
    fwrite(hd,4,4,o); fwrite(fh,4,4,o); fwrite(S->E0,4,16,o); fwrite(S->Ei0,4,16,o); fwrite(X,12,(size_t)n,o); fwrite(V,12,(size_t)n,o); fwrite(S->tg0,12,(size_t)n,o); fclose(o);
    /* and, asked for (a fourth number), her body as it was that frame, every picked point in the pick's order: with these a take's motion can be stepped again away
     * from the engine, a changed step against the same head (the hair bench) */
    if(g_hsDumpBody&&g_hsBHave0){ snprintf(pth,sizeof pth,"%s/%s_s%d_%05d_b.bin",g_hsDumpDir,nm,S->slot,f); o=fopen(pth,"wb");
        if(o){ fwrite(&g_hsB.n,4,1,o); for(int i=0;i<g_hsB.n;i++){ const float e9[2]={(float)g_hsBOk0[i],(float)g_hsBProP[i]}; fwrite(g_hsBP0[i],4,3,o); fwrite(g_hsBN0[i],4,3,o); fwrite(e9,4,2,o); } fclose(o); } } }
static void hsGpuDump(HStrands*S,float sc){
    const int n=S->ns*S->np; float*X=malloc((size_t)n*12),*V=malloc((size_t)n*12);
    if(X&&V&&gfxBufferRead(S->gs[GFX_SB_X],X,(long)n*12)&&gfxBufferRead(S->gs[GFX_SB_V],V,(long)n*12)) hsDump(S,sc,X,V);
    free(X); free(V); }
static float hsBodyPushN(float*x,float margin,float*nOut);   /* below */
static int hsSegOut(float*a,float*b,int fixA,float margin,float deepIn,float step,int mid,int proOnly,float*nOut,float*depth);
/* ⭐ (11-06) THE HAIRCUT SETTLED OFF HER AT REST. Her hair was still going through her ear -- her locks were grown off her skull and her neck (build_reva.py)
 * and nothing knew of her ears, so the haircut itself went through them, and the simulation holds each lock's first two points to the haircut. Here each lock's
 * rest shape is walked out from its root and pushed clear of her body as she stands at rest: each point within a hair's breadth of her, and each segment's thirds
 * when they're well inside (a segment through an ear goes a centimetre in; one lying on her rounded neck dips a few mm, and stays), every segment its own length
 * from the root out. What each point moved is kept (S->ro), and hsTargets puts the haircut there: it rides her head, so it turns with her */
static void hsSettleAll(HStrands*S,int*locks,int*moved,float*most);   /* (11-08) below: the settle on the hair's cores */
static void hsSettle(HStrands*S,Pack*P){
    const int np=S->np; if(np>64) return;
    hsBodyUse(P,S->slot);   /* (11-09) her own body, not whoever's hair went before */
    if(g_hsB.pk!=P) hsBodyPick(P);
    /* her body at rest, in the hair's own rest space, and a grid of it */
    g_hsNB=0; for(int k=0;k<3;k++){ g_hsLo[k]=1e30f; g_hsHi[k]=-1e30f; }
    for(int i=0;i<g_hsB.n&&g_hsNB<HS_BODYMAX;i++){ PMesh*M=&P->meshes[g_hsB.mi[i]]; if(!(M->flags&1)) continue;
        const unsigned v=g_hsB.vi[i]; const float*bd=(const float*)(P->pk+M->offBind),*rp=(const float*)(P->pk+M->offPos),*nn=(const float*)(P->pk+M->offNorm);
        float*o=g_hsBP[g_hsNB],*on=g_hsBN[g_hsNB]; hsXf(bd,rp+v*3,o);
        for(int k=0;k<3;k++) on[k]=bd[k]*nn[v*3]+bd[4+k]*nn[v*3+1]+bd[8+k]*nn[v*3+2];
        const float l=sqrtf(on[0]*on[0]+on[1]*on[1]+on[2]*on[2]); if(l<1e-9f) continue; for(int k=0;k<3;k++) on[k]/=l;
        for(int k=0;k<3;k++){ if(o[k]<g_hsLo[k]) g_hsLo[k]=o[k]; if(o[k]>g_hsHi[k]) g_hsHi[k]=o[k]; }
        g_hsBPro[g_hsNB]=g_hsBProP[i]; g_hsBMap[g_hsNB]=i; g_hsNB++; }
    g_hsCell=0.015f*(P->headR[0]>0.f?(P->headR[0]+P->headR[1]+P->headR[2])/(3.f*0.1f):1.f);
    for(int i=0;i<HS_GRID;i++) g_hsHead[i]=-1;
    for(int i=0;i<g_hsNB;i++){ const unsigned hb=hsHash((int)floorf(g_hsBP[i][0]/g_hsCell),(int)floorf(g_hsBP[i][1]/g_hsCell),(int)floorf(g_hsBP[i][2]/g_hsCell)); g_hsNext[i]=g_hsHead[hb]; g_hsHead[hb]=i; }
    int moved=0, locks=0; float most=0.f;
    hsSettleAll(S,&locks,&moved,&most);   /* (11-08) on the hair's cores: 100,000 hairs one at a time froze the start for 15 s */
    for(int p=0;p<S->ns*np;p++) for(int k=0;k<3;k++) S->rp[p][k]+=S->ro[p][k];
    printf("[strands] %s: the haircut settled off her at rest: %d locks touched her, %d points moved, the most %.1f mm (REVA_STRANDSETTLE=0: as grown)\n",P->label,locks,moved,1000.f*most); fflush(stdout);
    if(g_hsBHaveF) hsBodyAt(1.f); else g_hsNB=0; }   /* the frame's own surfaces back, if there are any yet */
/* (11-11) a lock cut to the first k of its own line (g_hsTrim): its np places, in place, each k of the way along the line it was that far along; and up to two
 * more rows carried the same way (the ribbon's half-width and its normal) */
static void hsTrimLock(float(*t)[3],float(*a)[3],float(*b)[3],int np,float k){
    float g[64],q[3][64][3]; if(np>64||np<2) return; float(*r[3])[3]={t,a,b};
    g[0]=0.f; for(int j=1;j<np;j++) g[j]=g[j-1]+sqrtf((t[j][0]-t[j-1][0])*(t[j][0]-t[j-1][0])+(t[j][1]-t[j-1][1])*(t[j][1]-t[j-1][1])+(t[j][2]-t[j-1][2])*(t[j][2]-t[j-1][2]));
    for(int j=0;j<np;j++){ const float at=g[j]*k; int i=0; while(i<np-2&&g[i+1]<at) i++; const float sp=g[i+1]-g[i], f=sp>1e-9f?(at-g[i])/sp:0.f;
        for(int w=0;w<3;w++) if(r[w]) for(int e=0;e<3;e++) q[w][j][e]=r[w][i][e]+(r[w][i+1][e]-r[w][i][e])*f; }
    for(int w=0;w<3;w++) if(r[w]) memcpy(r[w],q[w],(size_t)np*12); }
static int hsInit(HStrands*S,Pack*P,unsigned mi,float cutK){
    PMesh*M=&P->meshes[mi]; const int np=P->hstr; const unsigned nv=M->verts;
    hsFree(S); S->pk=P; S->cutK=(cutK>0.05f&&cutK<0.999f&&np<=64)?cutK:1.f; S->verts=nv; S->np=np; S->ns=(int)(nv/(unsigned)(2*np)); S->warm=0; S->stamp=0; S->chk=0; S->lenK=1.f; S->lenSaid=0; S->dmp=S->dmpRest=0;
    S->gpu=g_hsGpu&&g_hsMode&&np<=64&&gfxCompute();
    const int n=S->ns*np;
    S->x=malloc((size_t)n*12); S->xp=malloc((size_t)n*12); S->tg0=malloc((size_t)n*12); S->tg1=malloc((size_t)n*12); S->hw=malloc((size_t)n*12); S->nr=malloc((size_t)n*12);
    S->cor=malloc((size_t)n*12); S->rp=malloc((size_t)n*12); S->len=malloc((size_t)n*4); S->rr=malloc((size_t)n*4); S->kk=malloc((size_t)np*4);
    S->vp=malloc((size_t)nv*12); S->vn=malloc((size_t)nv*12); S->v=calloc((size_t)n,12); S->geo=malloc((size_t)n*4);
    S->d0=calloc((size_t)S->ns,12); S->cutWait=1;   /* (11-11) the cut rule's last moves */
    S->wv=calloc((size_t)n,12); S->nv=calloc((size_t)n,12); S->tv=calloc((size_t)n,12);
    if(!S->v||!S->geo||!S->x||!S->xp||!S->tg0||!S->tg1||!S->hw||!S->nr||!S->cor||!S->rp||!S->len||!S->rr||!S->kk||!S->vp||!S->vn){ hsFree(S); S->pk=NULL; return 0; }
    const float*raw=(const float*)(P->pk+M->offPos),*bd=(const float*)(P->pk+M->offBind);
    float Mo0[16],MI0[16]; const int head=headMatsM(P,P->headW0,Mo0,MI0);
    for(int s=0;s<S->ns;s++) for(int j=0;j<np;j++){ const int p=s*np+j; const unsigned v=(unsigned)(s*2*np+2*j);
        float a[3],b[3]; hsXf(bd,raw+v*3,a); hsXf(bd,raw+(v+1)*3,b);
        for(int k=0;k<3;k++) S->rp[p][k]=0.5f*(a[k]+b[k]);
        if(j){ const float*q=S->rp[p-1]; S->len[p]=sqrtf((S->rp[p][0]-q[0])*(S->rp[p][0]-q[0])+(S->rp[p][1]-q[1])*(S->rp[p][1]-q[1])+(S->rp[p][2]-q[2])*(S->rp[p][2]-q[2])); } else S->len[p]=0.f;
        if(head){ float u[3]; hsXf(MI0,S->rp[p],u); S->rr[p]=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); } else S->rr[p]=0.f; }
    /* ⭐ only the ROOT keeps its style: the angle a lock leaves her scalp (its first segment, a little of its second). Beyond that it's gravity,
     * its own springiness (REVA_STRANDBEND) and what it rests on. ⛔ (11-06) held toward its style all the way down (0.35 at the root to 0.02
     * at the tip), her hair kept its cut upside down, like hairspray: it stuck straight up and kept its shape where gravity
     * says it should hang down */
    for(int j=0;j<np;j++){ const float t=(float)j/(float)(np>1?np-1:1); S->kk[j]=g_hsK0*(1.f-t)+g_hsK1; }   /* round 3 (11-06): held toward the haircut all down the lock -- the most coherent so far, too stiff ("retains its shape when gravity says it should hang down"); round 4 (the root alone) and round 5 (each lock's own shape: scratchpad strands_r5.inc) curled into a frizzy ball, the style having gravity baked into it */
    for(int s=0;s<S->ns;s++){ float g=0.f; for(int j=0;j<np;j++){ g+=S->len[s*np+j]; S->geo[s*np+j]=g; } }
    S->ro=calloc((size_t)n,12); if(S->ro&&g_hsSettle){ hsSettle(S,P); if(head) for(int p=0;p<n;p++){ float u[3]; hsXf(MI0,S->rp[p],u); S->rr[p]=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); } }   /* along the lock from its root */
    /* (11-11) the short haircut is a haircut (g_hsTrim): each lock's rest places the first cutK of its own line through the (settled) cut, and its lengths, its
     * tether and how far out it lies in her skull's ellipsoid measured from those */
    if(S->cutK<1.f){ for(int s=0;s<S->ns;s++){ float(*r)[3]=S->rp+s*np; hsTrimLock(r,NULL,NULL,np,S->cutK); float g=0.f;
            for(int j=0;j<np;j++){ const int p=s*np+j; S->len[p]=j?sqrtf((r[j][0]-r[j-1][0])*(r[j][0]-r[j-1][0])+(r[j][1]-r[j-1][1])*(r[j][1]-r[j-1][1])+(r[j][2]-r[j-1][2])*(r[j][2]-r[j-1][2])):0.f; g+=S->len[p]; S->geo[p]=g;
                if(head){ float u[3]; hsXf(MI0,r[j],u); S->rr[p]=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); } } }
        printf("[strands] %s: her haircut is cut to %.0f%% of each lock's own length: its places, its lengths and its shell (REVA_STRANDCUT; REVA_STRANDTRIM=0: the lengths alone, as before)\n",P->label,100.f*S->cutK); fflush(stdout); }
    /* (11-06) REAL HAIRS: each lock's share, each hair with its own place, length, width and shade, from the hairs' own dice (the same every run) */
    S->hk=S->nh=0; S->miDraw=(int)mi;
    if(g_hsHairs>0&&S->wv&&S->nv&&S->tv&&np>=2){ const int K=(g_hsHairs+S->ns-1)/S->ns, nh=K*S->ns, R9=g_hsHairR<1?1:g_hsHairR>4?4:g_hsHairR, nr=(np-1)*R9+1;
        const size_t pv=(size_t)nh*(size_t)nr*2u, ni=(size_t)nh*(size_t)(nr-1)*6u; S->hrp=nr;
        S->hu=malloc((size_t)nh*4); S->hv=malloc((size_t)nh*4); S->hl=malloc((size_t)nh*4); S->hr=malloc((size_t)nh*4); if(!S->gpu){ S->hp=malloc(pv*12); S->hn=malloc(pv*12); }
        float*uv9=malloc(pv*8); unsigned*ix9=malloc(ni*4);
        if(S->hu&&S->hv&&S->hl&&S->hr&&(S->gpu||(S->hp&&S->hn))&&uv9&&ix9){ S->hk=K; S->nh=nh;
            for(int s=0;s<S->ns;s++){ unsigned sd=0x9E3779B9u^((unsigned)s*2654435761u); for(int w9=0;w9<4;w9++) hsRand(&sd);
                for(int k=0;k<K;k++){ const int h=s*K+k;
                    S->hu[h]=((float)k+hsRand(&sd))/(float)K*2.f-1.f;   /* spread evenly across its lock, each a little off its place */
                    S->hv[h]=(hsRand(&sd)-0.5f)*0.7f;                     /* and a little before or behind it */
                    S->hl[h]=1.f-0.10f*hsRand(&sd);                       /* as long as its lock, or a little shorter */
                    S->hr[h]=0.0005f*(g_hsHairMm[0]+(g_hsHairMm[1]-g_hsHairMm[0])*hsRand(&sd));   /* half its width, in metres */
                    const float sh=0.62f+0.83f*hsRand(&sd)*hsRand(&sd)+0.25f*hsRand(&sd);   /* a little lighter or darker than the next, most of them dark */
                    for(int j=0;j<nr;j++){ float*o=uv9+((size_t)h*nr+j)*4; const float tt=(float)j/(float)(nr-1); o[0]=-sh; o[1]=tt; o[2]=sh; o[3]=tt; }
                    for(int j=0;j+1<nr;j++){ unsigned*t9=ix9+((size_t)h*(nr-1)+j)*6; const unsigned b9=(unsigned)(((size_t)h*nr+j)*2);
                        t9[0]=b9; t9[1]=b9+1; t9[2]=b9+3; t9[3]=b9; t9[4]=b9+3; t9[5]=b9+2; } } }
            gfxBuffersNew(4,S->hvbo);
            gfxBufferData(S->hvbo[2],(long)(ni*4),ix9,0); gfxBufferData(S->hvbo[3],(long)(pv*8),uv9,0);
            }
        free(uv9); free(ix9);
        /* which of the hair meshes on these locks draws them: the blended one. The other, if there is one, is the opaque core under it, and isn't drawn */
        for(unsigned q=0;q<P->nMeshes;q++){ PMesh*Q=&P->meshes[q]; if(Q->verts!=M->verts||!hsIsHair(P,q)||memcmp(P->pk+Q->offPos,raw,12)) continue; if(Q->flags&8u){ S->miDraw=(int)q; break; } }
        if(S->nh) printf("[strands] %s: %d hairs, %d to a lock, %.2f to %.2f mm thick, each drawn with %d points on the curve through its lock's %d, by mesh %d (%.1f MB a frame)\n",P->label,S->nh,S->hk,g_hsHairMm[0],g_hsHairMm[1],nr,np,S->miDraw,(double)pv*24.0/1048576.0); }
    if(S->gpu) hsGpuInit(S);
    memcpy(S->key,raw,12);
    printf("[strands] %s mesh %u: %d locks of %d points simulated one by one (%d substeps a frame, held %.3f at the root to %.3f at the tip)\n",P->label,mi,S->ns,np,g_hsSub,S->kk[1],S->kk[np-1]); fflush(stdout);
    return 1; }
/* this frame's style: each point where her pose carries it (the middle of its two edges, skinned), its half-width and its normal */
static void hsTargetsR(HStrands*S,Pack*P,unsigned mi,const float*pal,const float*model,int s0,int s1){
    PMesh*M=&P->meshes[mi]; const float*raw=(const float*)(P->pk+M->offPos),*bd=(const float*)(P->pk+M->offBind);
    const float*nraw=(M->flags&1)?(const float*)(P->pk+M->offNorm):NULL; const unsigned char*jp=P->pk+M->offJoints,*wp=P->pk+M->offWeights;
    const int np=S->np;
    for(int s=s0;s<s1;s++){ for(int j=0;j<np;j++){ const int p=s*np+j; float e[2][3], nn[3]={0,0,0};
        for(int h=0;h<2;h++){ const unsigned v=(unsigned)(s*2*np+2*j+h); float b3[3],n3[3]={0,0,0}; hsXf(bd,raw+v*3,b3); if(S->ro) for(int k=0;k<3;k++) b3[k]+=S->ro[p][k];
            if(nraw&&!h) for(int k=0;k<3;k++) n3[k]=bd[k]*nraw[v*3]+bd[4+k]*nraw[v*3+1]+bd[8+k]*nraw[v*3+2];
            float a[3]={0,0,0},an[3]={0,0,0},ws=0.f;
            for(int q=0;q<4;q++){ const float w=wp[v*4+q]*(1.f/255.f); if(w<=0.f) continue; const float*pm=pal+(jp[v*4+q]<M->boneCount?jp[v*4+q]:0)*16;
                for(int k=0;k<3;k++){ a[k]+=w*(pm[k]*b3[0]+pm[4+k]*b3[1]+pm[8+k]*b3[2]+pm[12+k]); if(!h) an[k]+=w*(pm[k]*n3[0]+pm[4+k]*n3[1]+pm[8+k]*n3[2]); } ws+=w; }
            if(ws<1e-4f){ memcpy(a,b3,12); memcpy(an,n3,12); }
            hsXf(model,a,e[h]); if(!h) for(int k=0;k<3;k++) nn[k]=model[k]*an[0]+model[4+k]*an[1]+model[8+k]*an[2]; }
        for(int k=0;k<3;k++){ S->tg1[p][k]=0.5f*(e[0][k]+e[1][k]); S->hw[p][k]=0.5f*(e[1][k]-e[0][k]); }
        const float l=sqrtf(nn[0]*nn[0]+nn[1]*nn[1]+nn[2]*nn[2]); for(int k=0;k<3;k++) S->nr[p][k]=l>1e-9f?nn[k]/l:0.f; }
        if(S->cutK<1.f) hsTrimLock(S->tg1+s*np,S->hw+s*np,S->nr+s*np,np,S->cutK); } }   /* (11-11) the short haircut is a haircut (g_hsTrim): this frame's places, the first cutK of each lock's line */
/* a point off her skull and out of her body. Returns how far it was pushed */
static float hsCollide(HStrands*S,int p,float*x,float*xp,const float*E,const float*Ei,float margin){
    float moved=0.f;
    if(E&&S->rr[p]>0.f){ float q[3]; hsXf(E,x,q); const float L=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]), lim=S->rr[p]<=1.3f?0.995f*S->rr[p]:fminf(0.995f*S->rr[p],1.05f);   /* the cap keeps its stack (11-06: free, it collapsed onto her skull); the ends hang free */
        if(L<lim&&L>1e-6f){ for(int k=0;k<3;k++) q[k]*=lim/L; float w9[3]; hsXf(Ei,q,w9);
            moved=sqrtf((w9[0]-x[0])*(w9[0]-x[0])+(w9[1]-x[1])*(w9[1]-x[1])+(w9[2]-x[2])*(w9[2]-x[2])); memcpy(x,w9,12); S->contacts++; } }
    if(g_hsNB){ const float dp=hsBodyPush(x,xp,margin,g_hsMu); if(dp>0.f){ S->contacts++; if(dp>S->deep) S->deep=dp; if(dp>moved) moved=dp; } }
    return moved; }
/* one step of every lock: dt seconds, the style at u1 of the way from last frame's (tg0) to this one's (tg1) */
static void hsStep(HStrands*S,float dt,float u0,float u1,float sc,const float*E,const float*Ei){
    const int np=S->np; const float g=(float)CLUB_UNITS_PER_S2*dt*dt, damp=expf(-1.5f*dt), margin=0.004f*sc, sfl=0.9f;
    float tmp[64][3];
    for(int s=0;s<S->ns;s++){ float(*x)[3]=S->x+s*np,(*xp)[3]=S->xp+s*np,(*a)[3]=S->tg0+s*np,(*b)[3]=S->tg1+s*np,(*c)[3]=S->cor+s*np;
        float t[2][3];
        for(int k=0;k<3;k++){ xp[0][k]=x[0][k]; x[0][k]=a[0][k]+(b[0][k]-a[0][k])*u1; }   /* the root, where her scalp is */
        /* inertia, the air's drag, gravity */
        for(int j=1;j<np;j++){ for(int k=0;k<3;k++){ const float v=(x[j][k]-xp[j][k])*damp; xp[j][k]=x[j][k]; x[j][k]+=v; } x[j][1]-=g; }
        /* its style, from the point above: the segment as her pose holds it, held hardest at the root */
        for(int j=1;j<np;j++){ for(int k=0;k<3;k++){ t[0][k]=a[j-1][k]+(b[j-1][k]-a[j-1][k])*u1; t[1][k]=a[j][k]+(b[j][k]-a[j][k])*u1; }
            const float kk=S->kk[j]; for(int k=0;k<3;k++) x[j][k]+=kk*((x[j-1][k]+t[1][k]-t[0][k])-x[j][k]); }
        /* its own bend: a sharp angle eased toward the line through its neighbours (Jacobi, so no direction is favoured) */
        if(g_hsBend>0.f&&np<=64){ for(int j=1;j<np-1;j++) for(int k=0;k<3;k++) tmp[j][k]=0.5f*(x[j-1][k]+x[j+1][k])-x[j][k];
            for(int j=1;j<np-1;j++) for(int k=0;k<3;k++) x[j][k]+=g_hsBend*tmp[j][k]; }
        /* its length, from the root out: follow the leader, each point off her skull and out of her body as it's placed, so the next follows it
         * from where it ends up (a lock bends round her shoulder instead of kinking at it), and the damping that keeps that from stiffening it */
        for(int j=1;j<np;j++){ const int p=s*np+j;
            for(int it=0;it<2;it++){ float d[3]={x[j][0]-x[j-1][0],x[j][1]-x[j-1][1],x[j][2]-x[j-1][2]}; const float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                if(l<1e-9f) break;
                const float r=S->len[p]*S->lenK/l; float nx[3]; for(int k=0;k<3;k++) nx[k]=x[j-1][k]+d[k]*r;
                if(!it) for(int k=0;k<3;k++) c[j][k]=nx[k]-x[j][k];
                memcpy(x[j],nx,12);
                if(it||hsCollide(S,p,x[j],xp[j],E,Ei,margin)<1e-7f*sc) break; } }   /* its length is the last word: a hair doesn't stretch */
        for(int j=1;j<np-1;j++) for(int k=0;k<3;k++) xp[j][k]+=sfl*c[j+1][k]; }
    (void)u0; }
/* the ribbons round the locks' new lines, into the buffers the draw takes. Each ribbon's width is carried down its lock from the root (parallel
 * transport: at each point, the one above's made square to the lock here), so no ribbon twists edge-on; its normal is the width across the lock,
 * the root's side out */
static void hsRibbons(HStrands*S){
    const int np=S->np; float worst=0.f;
    for(int s=0;s<S->ns;s++){ float w[3]={0,0,0}, sgn=1.f;
        for(int j=0;j<np;j++){ const int p=s*np+j; const float*c=S->x[p];
            const float*a=S->x[p-(j>0)],*b=S->x[p+(j<np-1)]; float t[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]}; float tl=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);
            if(tl<1e-9f){ t[0]=0; t[1]=-1; t[2]=0; tl=1.f; } for(int k=0;k<3;k++) t[k]/=tl;
            const float*h=S->hw[p]; const float hl=sqrtf(h[0]*h[0]+h[1]*h[1]+h[2]*h[2]);
            const float*src=j?w:h; float u[3]; memcpy(u,src,12);
            const float ut=u[0]*t[0]+u[1]*t[1]+u[2]*t[2]; for(int k=0;k<3;k++) u[k]-=t[k]*ut;
            const float ul=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
            if(ul>1e-9f) for(int k=0;k<3;k++) w[k]=u[k]*hl/ul; else memcpy(w,h,12);
            float n[3]={w[1]*t[2]-w[2]*t[1],w[2]*t[0]-w[0]*t[2],w[0]*t[1]-w[1]*t[0]}; const float nl=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            if(nl>1e-12f) for(int k=0;k<3;k++) n[k]/=nl; else memcpy(n,S->nr[p],12);
            if(!j) sgn=(n[0]*S->nr[p][0]+n[1]*S->nr[p][1]+n[2]*S->nr[p][2]<0.f)?-1.f:1.f;
            if(S->wv){ memcpy(S->wv[p],w,12); for(int k=0;k<3;k++){ S->nv[p][k]=sgn*n[k]; S->tv[p][k]=t[k]; } }   /* (11-06) for the hairs along it */
            float*v0=S->vp+(size_t)(s*2*np+2*j)*3,*v1=v0+3,*n0=S->vn+(size_t)(s*2*np+2*j)*3,*n1=n0+3;
            for(int k=0;k<3;k++){ v0[k]=c[k]-w[k]; v1[k]=c[k]+w[k]; n0[k]=n1[k]=sgn*n[k]; }
            if(j){ const float*q=S->x[p-1]; const float l=sqrtf((c[0]-q[0])*(c[0]-q[0])+(c[1]-q[1])*(c[1]-q[1])+(c[2]-q[2])*(c[2]-q[2])), L=S->len[p]*S->lenK;
                if(L>0.25f*S->geo[s*np+np-1]/(float)(np-1)){ const float e=fabsf(l/L-1.f); if(e>worst) worst=e; } } } }
    S->stretch=worst; }
/* (11-06) REAL HAIRS, this frame: each hair down its lock, at its own place across and through it, to its own length; each point twice, a side each (the renderer spreads
 * them a pixel apart, across the hair's way), with the hair's way as long as its true half-width, narrowing over its last part */
static void hsHairsR(HStrands*S,float sc,int s0,int s1){
    const int np=S->np, K=S->hk, nr=S->hrp; if(!K||np<2||nr<2) return;
    for(int s=s0;s<s1;s++){ const float(*x)[3]=S->x+s*np,(*w)[3]=S->wv+s*np,(*nv)[3]=S->nv+s*np;
        for(int k=0;k<K;k++){ const int h=s*K+k; const float u=S->hu[h], v=S->hv[h], L=S->hl[h]*(float)(np-1), r=S->hr[h]*sc;
            float*o=S->hp+(size_t)h*nr*6,*on=S->hn+(size_t)h*nr*6;
            for(int j=0;j<nr;j++,o+=6,on+=6){ const float tt=(float)j/(float)(nr-1), q=L*tt; int i=(int)q; if(i>np-2) i=np-2; const float f=q-(float)i, g=1.f-f, f2=f*f, f3=f2*f;
                /* (11-06) on the Catmull-Rom curve through the lock's points, its way the curve's own; across and through the lock, as before */
                const float*P0=x[i>0?i-1:0],*P1=x[i],*P2=x[i+1],*P3=x[i+2<np?i+2:np-1];
                float c[3],t[3],a[3],nn[3];
                for(int e=0;e<3;e++){ const float A=P2[e]-P0[e], Bq=2.f*P0[e]-5.f*P1[e]+4.f*P2[e]-P3[e], Cq=3.f*(P1[e]-P2[e])+P3[e]-P0[e];
                    c[e]=P1[e]+0.5f*(A*f+Bq*f2+Cq*f3); t[e]=0.5f*(A+2.f*Bq*f+3.f*Cq*f2);
                    a[e]=w[i][e]*g+w[i+1][e]*f; nn[e]=nv[i][e]*g+nv[i+1][e]*f; }
                const float al=sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); float tl=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); if(tl<1e-9f){ t[0]=0.f; t[1]=-1.f; t[2]=0.f; tl=1.f; }
                const float hw=r*(1.f-0.6f*hsSmooth(0.86f,1.f,tt))/tl;
                for(int e=0;e<3;e++){ const float pe=c[e]+a[e]*u+nn[e]*(v*al); o[e]=o[3+e]=pe; on[e]=on[3+e]=t[e]*hw; } } } } }
/* ⭐ (11-06) THE KICK METER (REVA_STRANDDBG): each point's place from its lock's root, frame to frame. Moving smoothly, its second difference is small; a kick
 * the motion didn't call for shows as a spike (erratic movement that would make sense in wind, and there is none).
 * Every 30 stepped frames: the median and the 99th, each the window's mean of the frames', and the worst, in mm, and where */
static void hsKicks(HStrands*S,float sc){
    const int np=S->np, n=S->ns*np;
    if(!S->jr1){ S->jr1=calloc((size_t)n,12); S->jr2=calloc((size_t)n,12); S->jn=0; if(!S->jr1||!S->jr2) return; }
    static unsigned hist[402]; memset(hist,0,sizeof hist); unsigned cnt=0; float worst=0.f; int ws=-1,wj=-1; const float mm=1000.f/(sc>1e-6f?sc:1.f);
    for(int s=0;s<S->ns;s++){ const float*r0=S->x[s*np];
        for(int j=2;j<np;j++){ const int p=s*np+j; const float r[3]={S->x[p][0]-r0[0],S->x[p][1]-r0[1],S->x[p][2]-r0[2]};
            if(S->jn>=2){ float a=0.f; for(int k=0;k<3;k++){ const float d=r[k]-2.f*S->jr1[p][k]+S->jr2[p][k]; a+=d*d; } a=sqrtf(a)*mm;
                int b=(int)(a*4.f); if(b>401) b=401; hist[b]++; cnt++; if(a>worst){ worst=a; ws=s; wj=j; } }
            memcpy(S->jr2[p],S->jr1[p],12); memcpy(S->jr1[p],r,12); } }
    S->jn++;
    if(cnt){ unsigned c=0; int b50=-1,b99=-1; for(int b=0;b<402;b++){ c+=hist[b]; if(b50<0&&c>=cnt/2u) b50=b; if((double)c>=0.99*(double)cnt){ b99=b; break; } }
        S->jSum50+=b50*0.25; S->jSum99+=b99*0.25; S->jFrames++;
        if(worst>S->jMax){ S->jMax=worst; S->jMs=ws; S->jMj=wj; const int p=ws*np+wj; const float*x=S->x[p];
            for(int k=0;k<3;k++){ S->jAt[k]=(x[k]-S->hc[k])*mm; S->jKick[k]=(x[k]-S->x[ws*np][k]-2.f*S->jr2[p][k]+0.f)*0.f; }
            S->jPushed=S->pushed?S->pushed[p]:-1;
            /* what of her body is nearest it, how far, and which side */
            int bi=-1; float bd=1e30f; for(int i=0;i<g_hsNB;i++){ const float dx=x[0]-g_hsBP[i][0],dy=x[1]-g_hsBP[i][1],dz=x[2]-g_hsBP[i][2],d2=dx*dx+dy*dy+dz*dz; if(d2<bd){ bd=d2; bi=i; } }
            S->jWhat[0]=0; S->jNear=-1.f; S->jSide=0.f;
            S->jPro=-1; if(bi>=0){ const int pk9=g_hsBMap[bi]; S->jPro=g_hsBPro[bi]; const char*nm9=packStr(S->pk,S->pk->meshes[g_hsB.mi[pk9]].name); snprintf(S->jWhat,sizeof S->jWhat,"%s",nm9?nm9:"?");
                S->jNear=sqrtf(bd)*mm; S->jSide=((x[0]-g_hsBP[bi][0])*g_hsBN[bi][0]+(x[1]-g_hsBP[bi][1])*g_hsBN[bi][1]+(x[2]-g_hsBP[bi][2])*g_hsBN[bi][2])*mm; } } }
    if(S->jFrames>=30){ printf("[strands] %s, kicks over %d frames (each point from its root, beyond moving smoothly): median %.2f mm, 99th %.2f mm, worst %.1f mm (lock %d point %d); %d pushes at the middles of segments\n",
        S->pk->label,S->jFrames,S->jSum50/S->jFrames,S->jSum99/S->jFrames,S->jMax,S->jMs,S->jMj,S->midHits);
        printf("[strands]   the worst: %+.0f %+.0f %+.0f mm from her head's middle, pushed in %d substeps of its frame; nearest of her: %s%s, %.1f mm off, %+.1f mm along its normal\n",
            S->jAt[0],S->jAt[1],S->jAt[2],S->jPushed,S->jWhat,S->jPro==1?" (standing out of her skull)":"",S->jNear,S->jSide);
        if(S->strayN>0.0) printf("[strands]   out of step: a point's speed from its 2.5 cm cell's mean, %.0f mm/s on average, %.0f at the most (hair against hair %.2f)\n",S->strayS/S->strayN,S->strayMax,g_hsCohere);
        S->strayS=S->strayN=0.0; S->strayMax=0.f; fflush(stdout);
        S->jSum50=S->jSum99=0.0; S->jFrames=0; S->jMax=0.f; S->midHits=0; } }
/* (11-06) the hairs' colour: REVA_HAIRRGB, else the hair picture's (load.inc), else a black with a little blue in it */
static void hsHairColour(const Pack*P,float*o){
    if(g_hsHairRGB[0]>=0.f){ memcpy(o,g_hsHairRGB,12); return; }
    if(P->hairRGB[3]>0.f){ memcpy(o,P->hairRGB,12); return; }
    o[0]=0.055f; o[1]=0.052f; o[2]=0.070f; }
/* a point out of her body, returning how far and along what (Jolt pushes positions out, and acts on velocity after) */
static float hsBodyPushP(float*x,float margin,float*nOut,int*pro){
    if(pro) *pro=-1;
    const float out9=2.f*g_hsCell; if(x[0]<g_hsLo[0]-out9||x[0]>g_hsHi[0]+out9||x[1]<g_hsLo[1]-out9||x[1]>g_hsHi[1]+out9||x[2]<g_hsLo[2]-out9||x[2]>g_hsHi[2]+out9) return 0.f;
    const float fx=x[0]/g_hsCell,fy=x[1]/g_hsCell,fz=x[2]/g_hsCell; const int cx=(int)floorf(fx),cy=(int)floorf(fy),cz=(int)floorf(fz);
    const int ox=(fx-cx<0.5f)?-1:0, oy=(fy-cy<0.5f)?-1:0, oz=(fz-cz<0.5f)?-1:0;
    int best=-1; float bd=g_hsCell*g_hsCell;
    for(int a=0;a<2;a++) for(int b=0;b<2;b++) for(int c=0;c<2;c++)
        for(int i=g_hsHead[hsHash(cx+ox+a,cy+oy+b,cz+oz+c)];i>=0;i=g_hsNext[i]){ const float*q=g_hsBP[i];
            const float dx=x[0]-q[0],dy=x[1]-q[1],dz=x[2]-q[2],d2=dx*dx+dy*dy+dz*dz; if(d2<bd){ bd=d2; best=i; } }
    int deep9=0;
    /* ⭐ (11-06) nothing within a cell: a point that's gone into her deeper than that is looked for two cells out, and taken out only if it's inside. ⛔ Before, a lock's
     * tip 2 cm under her shoulder found nothing, wasn't pushed, was dragged along inside her by its lock and popped out later: the worst flicks, measured (lock 2741) */
    if(best<0&&g_hsReach){ bd=4.f*g_hsCell*g_hsCell; deep9=1;
        for(int a=-1;a<=1;a++) for(int b=-1;b<=1;b++) for(int c=-1;c<=1;c++)
            for(int i=g_hsHead[hsHash(cx+a,cy+b,cz+c)];i>=0;i=g_hsNext[i]){ const float*q=g_hsBP[i];
                const float dx=x[0]-q[0],dy=x[1]-q[1],dz=x[2]-q[2],d2=dx*dx+dy*dy+dz*dz; if(d2<bd){ bd=d2; best=i; } } }
    if(best<0) return 0.f;
    if(pro) *pro=g_hsBPro[best];
    const float*q=g_hsBP[best],*n=g_hsBN[best]; const float d=(x[0]-q[0])*n[0]+(x[1]-q[1])*n[1]+(x[2]-q[2])*n[2];
    if(d>=(deep9?0.f:margin)) return 0.f;
    const float push=margin-d; for(int k=0;k<3;k++){ x[k]+=n[k]*push; nOut[k]=n[k]; }
    return push; }
static float hsBodyPushN(float*x,float margin,float*nOut){ return hsBodyPushP(x,margin,nOut,NULL); }
/* ⭐ (11-06) A SEGMENT KEPT OUT OF HER, a..b. What's near its middle says how: by a part of her head that stands out of her skull (an ear: thin, so a segment goes
 * through it with both ends clear and no point of it far in) it's looked along every few mm (step) and pushed out from the first touch; by the rest of her, at its
 * points between (mid: 2 its thirds), and only where one's deepIn past the margin, and then only to the surface (a segment lying on her rounded neck dips a few mm
 * under it between its ends: lifting that every substep was jitter). fixA: a doesn't move (a root), so b goes further, three times at most. proOnly: only by an ear.
 * Returns how many points were pushed; the deepest push and its way in depth and nOut */
static int hsSegOut(float*a,float*b,int fixA,float margin,float deepIn,float step,int mid,int proOnly,float*nOut,float*depth){
    float m9[3],n9[3]; for(int k=0;k<3;k++) m9[k]=0.5f*(a[k]+b[k]);
    int pro=-1; hsBodyPushP(m9,margin*4.f,n9,&pro); if(pro<0) return 0;
    if(proOnly&&pro!=1) return 0;
    const float L=sqrtf((b[0]-a[0])*(b[0]-a[0])+(b[1]-a[1])*(b[1]-a[1])+(b[2]-a[2])*(b[2]-a[2]));
    int ns=mid; float thr=margin+deepIn; if(pro==1){ ns=(int)(L/step); if(ns<2) ns=2; if(ns>16) ns=16; thr=0.f; }
    int hits=0;
    for(int m=1;m<=ns;m++){ const float t=(float)m/(float)(ns+1); for(int k=0;k<3;k++) m9[k]=a[k]+(b[k]-a[k])*t;
        float dp=hsBodyPushP(m9,margin,n9,NULL); if(dp<=thr) continue; if(pro!=1) dp-=margin;
        if(!fixA){ const float s9=dp/((1.f-t)*(1.f-t)+t*t); for(int k=0;k<3;k++){ a[k]+=n9[k]*s9*(1.f-t); b[k]+=n9[k]*s9*t; } }
        else { const float g9=fminf(1.f/t,3.f)*dp; for(int k=0;k<3;k++) b[k]+=n9[k]*g9; }
        hits++; if(depth&&dp>*depth){ *depth=dp; memcpy(nOut,n9,12); } }
    return hits; }
/* one substep of every lock, Jolt's way. u1: how far across the frame this substep ends, for the skinned targets */
typedef struct { int contacts, midHits; float deep; } HsAcc;   /* (11-06) what a core's share of the locks counted */
static void hsStepXR(HStrands*S,float dt,float u1,float sc,const float*E,const float*Ei,int s0,int s1,HsAcc*A){
    const int np=S->np; const float gdt=(float)CLUB_UNITS_PER_S2*dt, damp=fmaxf(0.f,1.f-g_hsDampC*dt), margin=0.004f*sc, idt=1.f/dt;
    const float aB=g_hsBendC*sc/(dt*dt);            /* XPBD: the bend's compliance over the substep squared (in world units) */
    const float sk1=g_hsSkin1*sc, sk2=g_hsSkin2*sc;
    float cn[64][3]; float cd[64];
    for(int s=s0;s<s1;s++){ float(*x)[3]=S->x+s*np,(*xp)[3]=S->xp+s*np,(*v)[3]=S->v+s*np,(*a)[3]=S->tg0+s*np,(*b)[3]=S->tg1+s*np;
        if(np>64) continue;
        /* the root: kinematic, where her scalp is at the end of this substep */
        for(int k=0;k<3;k++){ xp[0][k]=x[0][k]; x[0][k]=a[0][k]+(b[0][k]-a[0][k])*u1; v[0][k]=(x[0][k]-xp[0][k])*idt; }
        /* gravity and damping on the velocity, then the positions move */
        for(int j=1;j<np;j++){ v[j][1]-=gdt; for(int k=0;k<3;k++){ v[j][k]*=damp; xp[j][k]=x[j][k]; x[j][k]+=v[j][k]*dt; } }
        /* ONE pass of constraints, in Jolt's order: the skinned (how the lock leaves her scalp), the edges, the bends, the long-range tether */
        for(int j=1;j<=2&&j<np;j++){ float t[3]; for(int k=0;k<3;k++) t[k]=a[j][k]+(b[j][k]-a[j][k])*u1;
            const float md=j==1?sk1:sk2; float d[3]={x[j][0]-t[0],x[j][1]-t[1],x[j][2]-t[2]}; const float l2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
            if(l2>md*md){ const float r=md/sqrtf(l2); for(int k=0;k<3;k++) x[j][k]=t[k]+d[k]*r; } }
        /* (11-11) a hair's own stiffness (g_hsHoldL): toward its place in the haircut, wholly within holdL of its root and less by the cube of how far along it is */
        if(g_hsHoldL>0.f){ const float hl9=g_hsHoldL*0.01f*sc; for(int j=1;j<np;j++){ float kq=hl9/fmaxf(S->geo[s*np+j]*S->lenK,1e-6f); kq=kq>=1.f?1.f:kq*kq*kq; kq*=g_hsHoldW;
            for(int k=0;k<3;k++) x[j][k]+=((a[j][k]+(b[j][k]-a[j][k])*u1)-x[j][k])*kq; } }
        for(int j=1;j<np;j++){ const float w0=j==1?0.f:1.f, w1=1.f; float d[3]={x[j][0]-x[j-1][0],x[j][1]-x[j-1][1],x[j][2]-x[j-1][2]};
            const float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]), den=l*(w0+w1); if(den<1e-12f) continue;
            const float f=(l-S->len[s*np+j]*S->lenK)/den; for(int k=0;k<3;k++){ x[j-1][k]+=w0*d[k]*f; x[j][k]-=w1*d[k]*f; } }
        /* a straight hair's bend: the point two along kept at the two segments' length from this one (Jolt's distance bend), with a compliance */
        for(int j=0;j+2<np;j++){ const float w0=j==0?0.f:1.f, w1=1.f; const float L=(S->len[s*np+j+1]+S->len[s*np+j+2])*S->lenK;
            float d[3]={x[j+2][0]-x[j][0],x[j+2][1]-x[j][1],x[j+2][2]-x[j][2]}; const float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            const float den=l*(w0+w1+aB); if(den<1e-12f) continue;
            const float f=(l-L)/den; for(int k=0;k<3;k++){ x[j][k]+=w0*d[k]*f; x[j+2][k]-=w1*d[k]*f; } }
        for(int j=2;j<np;j++){ float d[3]={x[j][0]-x[0][0],x[j][1]-x[0][1],x[j][2]-x[0][2]}; const float l2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2], g=S->geo[s*np+j]*S->lenK;
            if(l2>g*g){ const float r=g/sqrtf(l2); for(int k=0;k<3;k++) x[j][k]=x[0][k]+d[k]*r; } }
        /* the velocity from where the constraints left it: Jolt rebuilds it BEFORE the collisions, so a push out of her skull or her neck moves the
         * point and is never turned into speed. ⛔ (11-06) rebuilt after, every push became outward velocity and her moving head flung her hair */
        for(int j=1;j<np;j++) for(int k=0;k<3;k++) v[j][k]=(x[j][k]-xp[j][k])*idt;
        /* (11-11) the hold's own damping (g_hsHoldD): its speed against the haircut's own speed there taken off, as much as critically damps the hold's pull */
        if(g_hsHoldL>0.f&&g_hsHoldD>0.f){ const float hl9=g_hsHoldL*0.01f*sc, ift9=1.f/(dt*(float)g_hsSub); for(int j=1;j<np;j++){ float kq=hl9/fmaxf(S->geo[s*np+j]*S->lenK,1e-6f); kq=kq>=1.f?1.f:kq*kq*kq;
            const float dd=fminf(1.f,g_hsHoldD*2.f*sqrtf(g_hsHoldW)*kq); for(int k=0;k<3;k++){ const float vt=(b[j][k]-a[j][k])*ift9; v[j][k]+=(vt-v[j][k])*dd; } } }
        /* collisions: off her skull, out of her body -- positions only; then what's moving into the surface stops, and what slides feels friction */
        for(int j=1;j<np;j++){ const int p=s*np+j; cd[j]=0.f; int near9=0;
            if(E&&S->rr[p]>0.f){ float q[3]; hsXf(E,x[j],q); const float L=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]); float lim=fminf(0.995f*S->rr[p],1.02f); near9=L<1.6f;
                if(g_hsShell){ float t9=(S->rr[p]-g_hsShellA)/(g_hsShellB-g_hsShellA); t9=t9<0.f?0.f:t9>1.f?1.f:t9; t9=t9*t9*(3.f-2.f*t9);   /* (11-11) the haircut's own shell (g_hsShell) */
                    const float keep9=fminf(0.995f*S->rr[p],0.995f*g_hsShellB); lim=fminf(keep9+(1.02f-keep9)*t9,0.995f*S->rr[p]); }
                if(L<lim&&L>1e-6f){ float o[3]; memcpy(o,x[j],12); for(int k=0;k<3;k++) q[k]*=lim/L; hsXf(Ei,q,x[j]);
                    float d[3]={x[j][0]-o[0],x[j][1]-o[1],x[j][2]-o[2]}; const float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                    if(dl>1e-9f){ cd[j]=dl; for(int k=0;k<3;k++) cn[j][k]=d[k]/dl; } A->contacts++; } }
            if(g_hsNB){ float n9[3]; const float dp=hsBodyPushN(x[j],margin,n9); if(dp>cd[j]){ cd[j]=dp; memcpy(cn[j],n9,12); } if(dp>0.f){ A->contacts++; if(dp>A->deep) A->deep=dp; } }
            /* ⭐ (11-06) and near her head, the segment up to it (hsSegOut): through an ear with both its ends outside it is the trap (the hair was still going through her ear).
             * Not the first segment, nor the second but by an ear: those points are held to the haircut */
            if(near9&&j>=2&&g_hsMid>0&&g_hsNB){ float n9[3],dep=0.f; const int h9=hsSegOut(x[j-1],x[j],0,margin,0.005f*sc,0.003f*sc,g_hsMid,j<3,n9,&dep);
                if(h9){ if(dep>cd[j]){ cd[j]=dep; memcpy(cn[j],n9,12); } A->contacts+=h9; A->midHits+=h9; } }
            if(cd[j]>0.f&&S->pushed&&S->pushed[p]<255) S->pushed[p]++;
            if(cd[j]>0.f){ const float*n=cn[j]; const float vn=v[j][0]*n[0]+v[j][1]*n[1]+v[j][2]*n[2];
                float vt[3]={v[j][0]-n[0]*vn,v[j][1]-n[1]*vn,v[j][2]-n[2]*vn}; const float vtl=sqrtf(vt[0]*vt[0]+vt[1]*vt[1]+vt[2]*vt[2]);
                const float fr=vtl>1e-9f?fminf(g_hsFric*cd[j]/(vtl*dt),1.f):0.f;
                for(int k=0;k<3;k++) v[j][k]-=vt[k]*fr+(vn<0.f?n[k]*vn:0.f); } } } }
/* ⭐ (11-06) THE LOCKS SIDE BY SIDE. A take was slow to render while the machine was hardly working -- every lock was stepped on one core of
 * twelve. A lock never touches another, only her skull and her body, which stand still while a substep runs, so the locks are dealt out to the machine's cores
 * (REVA_STRANDTHREADS; its cores less one by default, 1 the old way): a substep's step, the frame's pose for them, and the hairs drawn along them. Each lock comes
 * out the same whichever core had it, and the counts are added in the cores' order */
#define HS_TMAX 32
static int g_hsNT=0, g_hsPool=0; static SDL_sem*g_hsGo[HS_TMAX],*g_hsDoneS=NULL;
static struct { int kind; HStrands*S; Pack*P; unsigned mi; const float*pal,*model,*E,*Ei; float dt,u1,sc; HsAcc acc[HS_TMAX]; } g_hsJob;
static void hsSettleR(HStrands*S,int s0,int s1,HsAcc*A);
static void hsJobRun(int t){ HStrands*S=g_hsJob.S; const int n=S->ns, s0=(int)((long long)n*t/g_hsNT), s1=(int)((long long)n*(t+1)/g_hsNT);
    if(g_hsJob.kind==1){ HsAcc*A=&g_hsJob.acc[t]; A->contacts=A->midHits=0; A->deep=0.f; hsStepXR(S,g_hsJob.dt,g_hsJob.u1,g_hsJob.sc,g_hsJob.E,g_hsJob.Ei,s0,s1,A); }
    else if(g_hsJob.kind==2) hsHairsR(S,g_hsJob.sc,s0,s1);
    else if(g_hsJob.kind==4){ HsAcc*A=&g_hsJob.acc[t]; A->contacts=A->midHits=0; A->deep=0.f; hsSettleR(S,s0,s1,A); }   /* (11-08) 4: 3 is the targets*/
    else hsTargetsR(S,g_hsJob.P,g_hsJob.mi,g_hsJob.pal,g_hsJob.model,s0,s1); }
static int hsWorker(void*arg){ const int t=(int)(intptr_t)arg; for(;;){ SDL_SemWait(g_hsGo[t]); hsJobRun(t); SDL_SemPost(g_hsDoneS); } return 0; }
static void hsParallel(void){
    if(!g_hsPool){ g_hsPool=1; g_hsDoneS=SDL_CreateSemaphore(0); if(!g_hsDoneS) g_hsNT=1;
        for(int t=1;t<g_hsNT;t++){ char nm[16]; snprintf(nm,sizeof nm,"strands%d",t); g_hsGo[t]=SDL_CreateSemaphore(0);
            if(!g_hsGo[t]||!SDL_CreateThread(hsWorker,nm,(void*)(intptr_t)t)){ g_hsNT=t; break; } }
        printf("[strands] the locks dealt out to %d cores\n",g_hsNT); fflush(stdout); }
    for(int t=1;t<g_hsNT;t++) SDL_SemPost(g_hsGo[t]);
    hsJobRun(0);
    for(int t=1;t<g_hsNT;t++) SDL_SemWait(g_hsDoneS); }
static void hsStepX(HStrands*S,float dt,float u1,float sc,const float*E,const float*Ei){
    if(g_hsNT<=1){ HsAcc A={0,0,0.f}; hsStepXR(S,dt,u1,sc,E,Ei,0,S->ns,&A); S->contacts+=A.contacts; S->midHits+=A.midHits; if(A.deep>S->deep) S->deep=A.deep; return; }
    g_hsJob.kind=1; g_hsJob.S=S; g_hsJob.dt=dt; g_hsJob.u1=u1; g_hsJob.sc=sc; g_hsJob.E=E; g_hsJob.Ei=Ei; hsParallel();
    for(int t=0;t<g_hsNT;t++){ const HsAcc*A=&g_hsJob.acc[t]; S->contacts+=A->contacts; S->midHits+=A->midHits; if(A->deep>S->deep) S->deep=A->deep; } }
static void hsHairs(HStrands*S,float sc){ if(g_hsNT<=1){ hsHairsR(S,sc,0,S->ns); return; } g_hsJob.kind=2; g_hsJob.S=S; g_hsJob.sc=sc; hsParallel(); }
/* (11-08) the haircut settled off her at rest, a range of locks: hsSettle's own loop, each core counting for itself (locks in contacts, points in midHits, the most
 * in deep) */
static void hsSettleR(HStrands*S,int s0,int s1,HsAcc*A){ const int np=S->np; const float margin=0.004f, deep=0.003f;
    float x[64][3];
    for(int s9=s0;s9<s1;s9++){ const float(*r)[3]=S->rp+s9*np; memcpy(x,r,(size_t)np*12); int any=0;
        for(int it=0;it<12;it++){
            for(int j=1;j<np;j++){ float n9[3]; if(hsBodyPushN(x[j],margin,n9)>0.f) any=1;
                if(hsSegOut(x[j-1],x[j],j==1,margin,deep,0.003f,2,0,n9,NULL)) any=1; }
            for(int j=1;j<np;j++){ float d[3]={x[j][0]-x[j-1][0],x[j][1]-x[j-1][1],x[j][2]-x[j-1][2]}; const float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(l<1e-9f) continue;
                const float L=S->len[s9*np+j]/l; for(int k=0;k<3;k++) x[j][k]=x[j-1][k]+d[k]*L; } }
        if(!any) continue;
        A->contacts++;
        for(int j=1;j<np;j++){ const int p=s9*np+j; float d[3]; for(int k=0;k<3;k++) d[k]=x[j][k]-r[j][k]; const float l=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
            if(l>0.0005f){ A->midHits++; if(l>A->deep) A->deep=l; } memcpy(S->ro[p],d,12); } }
}
static void hsSettleAll(HStrands*S,int*locks,int*moved,float*most){
    if(g_hsNT<=1){ HsAcc A={0,0,0.f}; hsSettleR(S,0,S->ns,&A); *locks=A.contacts; *moved=A.midHits; *most=A.deep; return; }
    g_hsJob.kind=4; g_hsJob.S=S; hsParallel();
    for(int t=0;t<g_hsNT;t++){ const HsAcc*A=&g_hsJob.acc[t]; *locks+=A->contacts; *moved+=A->midHits; if(A->deep>*most) *most=A->deep; } }
static void hsTargets(HStrands*S,Pack*P,unsigned mi,const float*pal,const float*model){ if(g_hsNT<=1){ hsTargetsR(S,P,mi,pal,model,0,S->ns); return; }
    g_hsJob.kind=3; g_hsJob.S=S; g_hsJob.P=P; g_hsJob.mi=mi; g_hsJob.pal=pal; g_hsJob.model=model; hsParallel(); }
/* ⭐ (11-06) HAIR AGAINST HAIR. Once a frame each point's speed goes into its cell (2.5 cm, a hash of the cell's own three numbers), and each point but the root
 * is pulled toward its cell's mean: neighbouring hairs drag on one another, so the hair moves as a mass and not as locks swaying out of step (Petrovic, Henne and
 * Anderson 2005). How far each strayed before the pull is kept for REVA_STRANDDBG */
#define HS_VG 65536
static int g_hsVgK[HS_VG][3]; static float g_hsVgS[HS_VG][4]; static unsigned g_hsVgT[HS_VG], g_hsVgNow=0;
static int hsVgSlot(const int*c,int make){ unsigned h=hsHash(c[0],c[1],c[2])&(HS_VG-1);
    for(int k=0;k<64;k++,h=(h+1)&(HS_VG-1)){ if(g_hsVgT[h]!=g_hsVgNow){ if(!make) return -1; g_hsVgT[h]=g_hsVgNow; memcpy(g_hsVgK[h],c,12); g_hsVgS[h][0]=g_hsVgS[h][1]=g_hsVgS[h][2]=g_hsVgS[h][3]=0.f; return (int)h; }
        if(g_hsVgK[h][0]==c[0]&&g_hsVgK[h][1]==c[1]&&g_hsVgK[h][2]==c[2]) return (int)h; }
    return -1; }
static void hsCohere(HStrands*S,float sc){
    if(g_hsCohere<=0.f) return;
    const int np=S->np, n=S->ns*np; const float cell=0.025f*sc, ic=1.f/cell, mm=1000.f/(sc>1e-6f?sc:1.f);
    g_hsVgNow++; if(!g_hsVgNow){ memset(g_hsVgT,0,sizeof g_hsVgT); g_hsVgNow=1; }
    for(int p=0;p<n;p++){ const int c[3]={(int)floorf(S->x[p][0]*ic),(int)floorf(S->x[p][1]*ic),(int)floorf(S->x[p][2]*ic)}; const int h=hsVgSlot(c,1); if(h<0) continue;
        for(int k=0;k<3;k++){ g_hsVgS[h][k]+=S->v[p][k]; }
        g_hsVgS[h][3]+=1.f; }
    for(int p=0;p<n;p++){ if(p%np==0) continue;
        const int c[3]={(int)floorf(S->x[p][0]*ic),(int)floorf(S->x[p][1]*ic),(int)floorf(S->x[p][2]*ic)}; const int h=hsVgSlot(c,0); if(h<0||g_hsVgS[h][3]<1.5f) continue;
        const float iw=1.f/g_hsVgS[h][3]; float d[3]; for(int k=0;k<3;k++) d[k]=g_hsVgS[h][k]*iw-S->v[p][k];
        if(g_hsDbg){ const float a=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2])*mm; S->strayS+=a; S->strayN+=1.0; if(a>S->strayMax) S->strayMax=a; }
        for(int k=0;k<3;k++) S->v[p][k]+=g_hsCohere*d[k]; } }
static void hsClampV(HStrands*S,float sc){ const float vm=g_hsVmax*sc, vm2=vm*vm; const int n=S->ns*S->np;
    for(int i=0;i<n;i++){ const float v2=S->v[i][0]*S->v[i][0]+S->v[i][1]*S->v[i][1]+S->v[i][2]*S->v[i][2]; if(v2>vm2){ const float r=vm/sqrtf(v2); for(int k=0;k<3;k++) S->v[i][k]*=r; } } }
/* clothPrep's hook: 2 when it has drawn the mesh's locks into its own buffers (the caller binds g_skVbo) */
static int strandPrep(int slot,Pack*P,unsigned mi,float*pal,const float*model,M4*world){
    if(g_hsOn<0){ const char*e=getenv("REVA_STRANDS"); g_hsOn=!(e&&e[0]=='0');
        e=getenv("REVA_STRANDSUB"); if(e&&*e){ g_hsSub=atoi(e); if(g_hsSub<1) g_hsSub=1; if(g_hsSub>64) g_hsSub=64; }
        e=getenv("REVA_STRANDK"); if(e&&*e){ g_hsK0=(float)atof(e); const char*c=strchr(e,','); if(c) g_hsK1=(float)atof(c+1); }
        g_hsDbg=getenv("REVA_STRANDDBG")?1:0;
        e=getenv("REVA_STRANDMODE"); if(e&&e[0]=='0') g_hsMode=0;
        e=getenv("REVA_STRANDBENDC"); if(e&&*e) g_hsBendC=(float)atof(e);
        e=getenv("REVA_STRANDDAMP"); if(e&&*e) g_hsDampC=(float)atof(e);
        e=getenv("REVA_STRANDSKIN"); if(e&&*e){ g_hsSkin1=(float)atof(e); const char*c9=strchr(e,','); if(c9) g_hsSkin2=(float)atof(c9+1); }
        e=getenv("REVA_STRANDFRIC"); if(e&&*e) g_hsFric=(float)atof(e);
        e=getenv("REVA_STRANDBEND"); if(e&&*e) g_hsBend=(float)atof(e);
        e=getenv("REVA_STRANDMU"); if(e&&*e) g_hsMu=(float)atof(e);
        e=getenv("REVA_HAIRS"); g_hsHairs=(e&&*e)?atoi(e):100000; if(g_hsHairs<0) g_hsHairs=0;
        e=getenv("REVA_STRANDLERP"); if(e&&e[0]=='0') g_hsLerp=0;
        e=getenv("REVA_STRANDREACH"); if(e&&*e) g_hsReach=e[0]!='0';
        e=getenv("REVA_STRANDCOHERE"); if(e&&*e) g_hsCohere=(float)atof(e);
        e=getenv("REVA_STRANDSETTLE"); if(e&&e[0]=='0') g_hsSettle=0;
        e=getenv("REVA_STRANDSNAP"); if(e&&e[0]=='0') g_hsSnapCut=0; e=getenv("REVA_STRANDJUMP"); if(e) g_hsCutM=(float)atof(e); e=getenv("REVA_STRANDJUMPSAY"); if(e&&e[0]=='1') g_hsCutSay=1;
        e=getenv("REVA_STRANDGPU"); if(e&&e[0]=='0') g_hsGpu=0;
        e=getenv("REVA_STRANDTICK"); if(e&&e[0]=='1') g_hsTick=1;
        e=getenv("REVA_STRANDCARRY"); if(e&&e[0]=='0') g_hsCarry=0;
        e=getenv("REVA_STRANDADAPT"); if(e&&e[0]=='0') g_hsAdapt=0;
        e=getenv("REVA_STRANDFAST"); if(e&&*e) g_hsFast=(float)atof(e);
        e=getenv("REVA_STRANDLEN"); if(e&&(e[0]=='0'||e[0]=='1')) g_hsLen=e[0]-'0';
        e=getenv("REVA_STRANDCHECK"); if(e&&e[0]&&e[0]!='0') g_hsChk=1;
        e=getenv("REVA_STRANDTRIM"); if(e&&e[0]=='0') g_hsTrim=0;
        e=getenv("REVA_STRANDCUT"); if(e&&*e){ g_hsCut=(float)atof(e); if(g_hsCut<0.2f) g_hsCut=0.2f; if(g_hsCut>1.f) g_hsCut=1.f; }
        e=getenv("REVA_STRANDSHELL"); if(e&&*e){ if(e[0]=='0'&&!e[1]) g_hsShell=0; else { g_hsShellA=(float)atof(e); const char*c9=strchr(e,','); if(c9) g_hsShellB=(float)atof(c9+1); if(g_hsShellA<1.02f) g_hsShellA=1.02f; if(g_hsShellB<g_hsShellA+0.05f) g_hsShellB=g_hsShellA+0.05f; } }
        e=getenv("REVA_STRANDHOLD"); if(e&&*e){ g_hsHoldL=(float)atof(e); const char*c9=strchr(e,','); if(c9){ g_hsHoldW=(float)atof(c9+1); const char*d9=strchr(c9+1,','); if(d9) g_hsHoldD=(float)atof(d9+1); } if(g_hsHoldD<0.f) g_hsHoldD=0.f; if(g_hsHoldD>2.f) g_hsHoldD=2.f; if(g_hsHoldL<0.f) g_hsHoldL=0.f; if(g_hsHoldW<0.f) g_hsHoldW=0.f; if(g_hsHoldW>1.f) g_hsHoldW=1.f; }
        if(!g_hsTrim||!g_hsShell||g_hsHoldL<=0.f) printf("[strands] controls: the short haircut %s, the haircut's shell %s, a hair's own stiffness %s\n",g_hsTrim?"cut":"its lengths alone (REVA_STRANDTRIM=0)",g_hsShell?"kept":"not kept (REVA_STRANDSHELL=0)",g_hsHoldL>0.f?"on":"off (REVA_STRANDHOLD=0)");
        e=getenv("REVA_STRANDFIELD"); if(e&&e[0]=='0'){ g_hsField=0; printf("[strands] REVA_STRANDFIELD=0: her body kept out of the hair by its nearest point's plane, as before (the control)\n"); }
        e=getenv("REVA_STRANDDUMP"); if(e&&*e){ static char dir9[400]; snprintf(dir9,sizeof dir9,"%s",e); char*c9=strchr(dir9,','); if(c9){ *c9=0; g_hsDumpEvery=atoi(c9+1); const char*d9=strchr(c9+1,','); if(d9){ g_hsDumpFrom=atoi(d9+1); d9=strchr(d9+1,','); if(d9) g_hsDumpBody=atoi(d9+1); } }
            if(g_hsDumpEvery<1){ g_hsDumpEvery=1; } if(g_hsDumpFrom<0){ g_hsDumpFrom=0; } g_hsDumpDir=dir9; printf("[strands] REVA_STRANDDUMP: each head's points written to %s every %d stepped frames from the %dth\n",dir9,g_hsDumpEvery,g_hsDumpFrom); }
        e=getenv("REVA_STRANDTHREADS"); g_hsNT=(e&&*e)?atoi(e):SDL_GetCPUCount()-1; if(g_hsNT<1) g_hsNT=1; if(g_hsNT>HS_TMAX) g_hsNT=HS_TMAX;
        e=getenv("REVA_STRANDMID"); if(e&&*e) g_hsMid=atoi(e)<0?0:atoi(e)>6?6:atoi(e);
        if(!g_hsLerp||!g_hsMid) printf("[strands] controls: the skull and body %s, the middles of segments %s\n",g_hsLerp?"between frames":"at the frame's end (REVA_STRANDLERP=0)",g_hsMid?"kept out":"not kept out (REVA_STRANDMID=0)");
#ifdef REVA_SOFT
        g_hsHairs=0;   /* the engine's own rasterizer has no hair pipeline: it draws the locks */
#endif
        e=getenv("REVA_HAIRR"); if(e&&*e) g_hsHairR=atoi(e);
        e=getenv("REVA_HAIRMM"); if(e&&*e){ g_hsHairMm[0]=(float)atof(e); const char*c9=strchr(e,','); g_hsHairMm[1]=c9?(float)atof(c9+1):g_hsHairMm[0]; }
        e=getenv("REVA_HAIRRGB"); if(e&&*e&&sscanf(e,"%f,%f,%f",&g_hsHairRGB[0],&g_hsHairRGB[1],&g_hsHairRGB[2])!=3) g_hsHairRGB[0]=-1.f;
        if(g_hsHairs) printf("[strands] real hairs: about %d on a head, %.2f to %.2f mm thick (REVA_HAIRS=0: the locks' ribbons)\n",g_hsHairs,g_hsHairMm[0],g_hsHairMm[1]);
        printf("[strands] hair lock by lock: %s\n",g_hsOn?"ON (packs with a HairStrandsN node)":"OFF (REVA_STRANDS=0): hair rides the head"); fflush(stdout); }
    if(!g_hsOn||P->hstr<2) return 0;
    PMesh*M=&P->meshes[mi];
    if(!M->boneCount||M->boneCount>MAXB||!hsIsHair(P,mi)||M->verts%(unsigned)(2*P->hstr)) return 0;
    const float*raw=(const float*)(P->pk+M->offPos);
    HStrands*S=NULL;
    for(int i=0;i<HS_POOL;i++) if(g_hs[i].pk==P&&g_hs[i].slot==slot&&g_hs[i].verts==M->verts&&!memcmp(g_hs[i].key,raw,12)){ S=&g_hs[i]; break; }
    if(!S){ int pick=0; unsigned old=0xffffffffu;
        for(int i=0;i<HS_POOL;i++){ if(!g_hs[i].pk){ pick=i; break; } if(g_hs[i].used<old){ old=g_hs[i].used; pick=i; } }
        /* (11-11) a lock that isn't simulated whole is cut (g_hsTrim): said to hsInit, which cuts its rest places and its lengths */
        const int whole9=g_hsLen==1||(g_hsLen<0&&P->hairWhole);
        S=&g_hs[pick]; S->slot=slot; if(!hsInit(S,P,mi,(g_hsLen==0||whole9||!g_hsTrim)?1.f:g_hsCut)) return 0; }
    S->used=g_frameStamp;
    const float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
    /* (11-11) how long her locks are in the world, against their lengths in the pack (g_hsLen's comment): her size against her pack's own, unless REVA_STRANDLEN says;
     * a cut lock's lengths are the cut's already, so they're her size's (g_hsTrim) */
    { const float ps9=P->scale>1e-6f?P->scale:1.f; float k9=g_hsLen==0?1.f:(g_hsLen==1||(g_hsLen<0&&P->hairWhole)||S->cutK<1.f)?sc:sc/ps9;   /* (11-11) a pack with a HairWhole node: the whole haircut (902; her coloured copies keep the short one) */ if(fabsf(k9-1.f)<1e-4f) k9=1.f; S->lenK=k9;
      if(!S->lenSaid){ S->lenSaid=1; if(fabsf(sc-k9)>0.02f*sc){ printf("[strands] %s: drawn at %.2f of her pack's units, and her locks are simulated %.0f%% as long as her haircut is cut (REVA_STRANDLEN=1: the whole length)\n",P->label,sc,100.f*k9/sc); fflush(stdout); } } }
    if(S->stamp!=g_frameStamp){ S->stamp=g_frameStamp;
        clock_t t0=clock();
        hsTargets(S,P,mi,pal,model);
        float Mo[16],MI[16],E[16],Ei[16],iM[16]; const float*Ep=NULL,*Eip=NULL;
        if(headMats(P,world,Mo,MI)){ hsAffInv(model,iM); m4_mul(E,MI,iM); m4_mul(Ei,(float*)model,Mo); Ep=E; Eip=Ei; }
        S->contacts=0; S->deep=0.f;
        float jump=0.f, cut=0.f; if(S->warm) for(int s=0;s<S->ns;s++){ const float*a=S->tg0[s*S->np],*b=S->tg1[s*S->np]; const float m0=b[0]-a[0],m1=b[1]-a[1],m2=b[2]-a[2]; const float d=sqrtf(m0*m0+m1*m1+m2*m2); if(d>jump) jump=d;
            if(S->d0){ const float c0=m0-S->d0[s][0],c1=m1-S->d0[s][1],c2=m2-S->d0[s][2]; const float c=sqrtf(c0*c0+c1*c1+c2*c2); if(c>cut) cut=c; S->d0[s][0]=m0; S->d0[s][1]=m1; S->d0[s][2]=m2; } }
        /* ⭐ (11-11) THE CUT RULE: a root's move that changes by more than a head's acceleration can make it change in a frame is a cut (a clip's new picture, a snap,
         * a seam) and the hair is hung again, as for a teleport; the frame after a hang only records (patch_strand_cut's note in the scratchpad: the chorus endings) */
        float dtc9=(float)g_chDt; if(dtc9>0.2f) dtc9=0.2f; if(dtc9<0.f) dtc9=0.f;
        const float cutLim=sc*fmaxf(g_hsCutM,60.f*dtc9*dtc9);
        const int cut9=g_hsCutM>0.f&&S->warm&&!S->cutWait&&cut>cutLim;
        if(g_hsCutSay&&S->warm&&cut>0.03f*sc){ printf("[strands] %s: its roots' move changed %.3f m in a frame (the cut rule's line %.3f m)%s%s\n",P->label,cut/sc,cutLim/sc,cut9?" -- a cut":"",S->cutWait?" (the frame after a hang: recorded)":""); fflush(stdout); }
        if(g_hsCutSeen!=g_frameStamp){ g_hsCutSeen=g_frameStamp; g_hsCutActive=g_hsCutPending>0; if(g_hsCutPending>0) g_hsCutPending--; }   /* (11-11) the edit's cut, this frame */
        const int snap9=g_hsCutActive&&S->warm&&jump>0.02f*sc;   /* (11-12) a head the line moved: one standing where she stood keeps her hair as it is */
        if(snap9){ static unsigned said9=0xffffffffu; if(said9!=g_frameStamp){ said9=g_frameStamp; printf("[strands] a line snapped its marks (the edit's cut): a head it moved is carried or hung again\n"); fflush(stdout); } }
        int carry9=0;   /* (11-12) hsCarry: a head put somewhere whole, turned a little, keeps its hair */
        const float dtf9=(float)g_chDt>1e-4f?(float)g_chDt:1.f/30.f; const int fast9=g_hsFast>0.f&&S->warm&&jump>g_hsFast*sc*dtf9;   /* (11-13) faster than a person moves */
        if(g_hsCarry&&S->warm&&Ep&&S->eHave&&(jump>0.5f*sc||cut9||snap9||fast9)&&!(S->gpu&&g_noDraw&&!g_hsTick)){
            float D9[16]; m4_mul(D9,Ei,S->E0);   /* last frame's world, through her head then, to this frame's */
            const float c9=(D9[0]+D9[5]+D9[10]-1.f)*0.5f; float e9=0.f;
            for(int s=0;s<S->ns;s++){ float p9[3]; hsXf(D9,S->tg0[s*S->np],p9); const float*b9=S->tg1[s*S->np]; const float q0=p9[0]-b9[0],q1=p9[1]-b9[1],q2=p9[2]-b9[2]; const float q=q0*q0+q1*q1+q2*q2; if(q>e9) e9=q; }
            e9=sqrtf(e9);
            if(c9>0.8660f&&e9<0.015f*sc) carry9=hsCarry(S,D9);   /* within 30 degrees: the music video's wrap-around copy turns 17 */
            { static int said9=0; if(g_hsCutSay||(carry9&&said9<6)||(carry9&&(said9%200)==0)){ printf("[strands] %s: put %.2f u away in a frame, turned %.1f degrees, its roots %.1f mm off a whole move -- %s\n",P->label,jump,57.29578f*acosf(fmaxf(-1.f,fminf(1.f,c9))),1000.f*e9/sc,carry9?"the hair is CARRIED with her":"hung again"); fflush(stdout); } if(carry9) said9++; } }
        if(g_hsBStamp!=g_frameStamp||g_hsBPk!=P||g_hsBSlot!=slot){ const int keep9=hsBodyUse(P,slot); g_hsBStamp=g_frameStamp; g_hsBPk=P; g_hsBSlot=slot; hsBodyFrame(P,world,model,sc,keep9&&!carry9); }
        const int hangAny9=!S->warm||((jump>0.5f*sc||cut9||snap9)&&!carry9);
        { static const char*tr9=(const char*)1; if(tr9==(const char*)1) tr9=getenv("REVA_STRANDTRACE");   /* (11-12) REVA_STRANDTRACE=<a body's name, or part of it>: its hair's step, frame by frame */
          if(tr9&&*tr9&&strstr(P->label,tr9)){ printf("[strandtrace] %s frame %u slot %d mesh %u: scale %.4f, locks %.4f of the pack's, warm %d, jump %.4f u, cut %.4f m, snap %d, carry %d, hang %d, settle %d, gpu %d, noDraw %d\n",
              P->label,g_frameStamp,slot,mi,sc,S->lenK,S->warm,jump,cut/sc,snap9,carry9,hangAny9,S->settleLeft,S->gpu,g_noDraw); fflush(stdout); } }
        if(g_hsCutSay&&hangAny9&&S->warm){ printf("[strands] %s: HUNG AGAIN, frame %u: jump %.3f u (a teleport past %.3f), cut %d, snap %d; a carry needs her head now and last frame (%d, %d), a drawn pass (%d)\n",
                                                  P->label,g_frameStamp,jump,0.5f*sc,cut9,snap9,Ep!=NULL,S->eHave,!(S->gpu&&g_noDraw&&!g_hsTick)); fflush(stdout); }
        if(carry9){ memcpy(S->tg0,S->tg1,(size_t)S->ns*S->np*12); S->eHave=0; }   /* nothing between last frame's pose and this one's: she was put here */
        /* ⭐ (11-12) A FRAME HER HEAD MOVED FAR TAKES MORE SUBSTEPS: at most 1.25 mm of her roots' move a substep, up to 48. The music video's second-chorus lead
         * snaps her head about 3 cm in one frame on the beat: at the draft's 6 substeps her whole haircut collapsed short for that frame, at 12 her fringe
         * still flicked up spiky (the critic: "the lead still twitches on the beat"). Only that frame pays; on the GPU only (the processor's step is
         * g_hsSub always). REVA_STRANDADAPT=0: g_hsSub every frame */
        int sub9=g_hsSub;
        if(g_hsAdapt&&S->gpu&&S->warm&&!hangAny9&&!carry9&&S->settleLeft<=0&&sc>1e-6f){ const int need9=(int)ceilf((jump/sc)/0.00125f); if(need9>sub9) sub9=need9>48?48:need9;
            if(sub9>g_hsSub){ static int said9=0; if(g_hsCutSay||said9<4){ said9++; printf("[strands] %s: her roots moved %.1f cm in a frame -- %d substeps for it (%d a frame otherwise)\n",P->label,100.f*jump/sc,sub9,g_hsSub); fflush(stdout); } } }
        const int n=S->ns*S->np;
        if(S->gpu){   /* ⭐ (11-06) on the GPU: hung and settled there, or the frame's substeps; a held exposure steps nothing and draws what's there */
            const int hang9=hangAny9;
            if(hang9&&S->warm&&!snap9){ if(cut9&&jump<=0.5f*sc) printf("[strands] %s: a cut (its roots' move changed %.2f m in a frame) -- the hair is hung again\n",P->label,cut/sc); else printf("[strands] %s: her head jumped %.2f u in a frame -- the hair is hung again\n",P->label,jump); }
            /* ⛔ (11-11) A PASS THAT ONLY TICKS GIVES THE GPU NOTHING OF THE HAIR. A late frame owes up to four ticks and only its last pass draws (g_noDraw). The
             * packet takes no hair step with no frame open (gfxStrands), so a step made in such a pass never ran, but its settling was counted as done (a head hung
             * on a late frame got 8 of its 40 frames: REVA_GPUOPSAY had it on a frame's step in its second picture), each pass staged 6 MB a head of targets nobody
             * read, and a hang's starting places went into a ring about to be reset (gfx_vk.c, gpuKeep: the device lost on a set of four REVAs' first picture).
             * Nothing is made for the GPU in such a pass now, and a hang that falls in one is made in the pass that draws. REVA_STRANDTICK=1: every pass, as before */
            if(g_noDraw&&!g_hsTick){ if(hang9) S->warm=0; }
            else {
            if(g_hsChk&&S->warm&&!g_noDraw&&++S->chk==2) hsGpuCheck(S,sc);   /* (11-11) REVA_STRANDCHECK: what the GPU holds of it, read back once, when its first drawn frame has run */
            if(g_hsDbg&&S->warm&&!hang9&&!g_stepHeld&&gfxBufferRead(S->gs[GFX_SB_X],S->x,(long)n*12)) hsKicks(S,sc);   /* (11-06) measured: last frame's, read back */
            if(g_hsDumpDir&&S->warm&&!hang9&&!g_stepHeld&&S->settleLeft<=0) hsGpuDump(S,sc);   /* (11-11) REVA_STRANDDUMP: last frame's, read back */
            if(hang9||!g_stepHeld){ float dt=(float)g_chDt; if(dt>0.05f) dt=0.05f; if(dt<1e-4f) dt=1e-4f; hsGpuFrame(S,sc,E,Ei,Ep!=NULL,hang9,dt,sub9); }
            S->warm=1; } }
        else if(hangAny9){
            /* hung from her pose, then let it settle under its own weight for two thirds of a second, her pose held */
            if(S->warm&&!snap9){ if(cut9&&jump<=0.5f*sc) printf("[strands] %s: a cut (its roots' move changed %.2f m in a frame) -- the hair is hung again\n",P->label,cut/sc); else printf("[strands] %s: her head jumped %.2f u in a frame -- the hair is hung again\n",P->label,jump); }
            memcpy(S->x,S->tg1,(size_t)n*12); memcpy(S->xp,S->tg1,(size_t)n*12); memcpy(S->tg0,S->tg1,(size_t)n*12); memset(S->v,0,(size_t)n*12);
            hsBodyAt(1.f);
            for(int f=0;f<40;f++){ for(int k=0;k<g_hsSub;k++){ if(g_hsMode) hsStepX(S,(1.f/60.f)/(float)g_hsSub,1.f,sc,Ep,Eip); else hsStep(S,(1.f/60.f)/(float)g_hsSub,0.f,1.f,sc,Ep,Eip); } if(g_hsMode) hsClampV(S,sc); }
            S->warm=1; S->jn=0; }
        else if(!g_stepHeld){ float dt=(float)g_chDt; if(dt>0.05f) dt=0.05f; if(dt<1e-4f) dt=1e-4f;
            float Eu9[16],Eiu9[16];
            if(g_hsDbg){ if(!S->pushed) S->pushed=calloc((size_t)n,1); else memset(S->pushed,0,(size_t)n); }
            for(int k=0;k<g_hsSub;k++){ const float u1=(float)(k+1)/(float)g_hsSub; const float*Eu=Ep,*Eiu=Eip;
                /* ⭐ (11-06) the skull and the body u1 of the way from last frame's pose to this one's, as the roots are */
                if(Ep&&S->eHave&&g_hsLerp){ for(int q=0;q<16;q++){ Eu9[q]=S->E0[q]+(E[q]-S->E0[q])*u1; Eiu9[q]=S->Ei0[q]+(Ei[q]-S->Ei0[q])*u1; } Eu=Eu9; Eiu=Eiu9; }
                hsBodyAt(u1);
                if(g_hsMode) hsStepX(S,dt/(float)g_hsSub,u1,sc,Eu,Eiu); else hsStep(S,dt/(float)g_hsSub,(float)k/(float)g_hsSub,u1,sc,Eu,Eiu); }
            if(g_hsMode){ hsClampV(S,sc); hsCohere(S,sc); }
            if(g_hsDumpDir){ memcpy(S->tg0,S->tg1,(size_t)n*12); if(Ep){ memcpy(S->E0,E,64); memcpy(S->Ei0,Ei,64); } hsDump(S,sc,(const float*)S->x,(const float*)S->v); }   /* (11-11) REVA_STRANDDUMP, the processor's */
            if(g_hsDbg) hsKicks(S,sc); }
        if(Ep){ memcpy(S->E0,E,64); memcpy(S->Ei0,Ei,64); S->eHave=1; S->hc[0]=Ei[12]; S->hc[1]=Ei[13]; S->hc[2]=Ei[14]; } else S->eHave=0;
        if(hangAny9||carry9){ S->cutWait=1; if(S->d0) memset(S->d0,0,(size_t)S->ns*12); } else S->cutWait=0;   /* (11-11) the cut rule starts over after a hang */
        memcpy(S->tg0,S->tg1,(size_t)n*12);
        if(!S->gpu){ hsRibbons(S);
        if(!S->vbo[0]){ gfxBuffersNew(2,S->vbo); for(int k=0;k<2;k++) gfxBufferData(S->vbo[k],(long)(S->verts*12),NULL,1); }
        gfxBufferSub(S->vbo[0],0,(long)(S->verts*12),S->vp); gfxBufferSub(S->vbo[1],0,(long)(S->verts*12),S->vn);
        if(S->nh){ clock_t h0=clock(); hsHairs(S,sc); const long pv=(long)S->nh*(long)S->hrp*24L; gfxBufferData(S->hvbo[0],pv,S->hp,1); gfxBufferData(S->hvbo[1],pv,S->hn,1); S->hms=1000.0*(double)(clock()-h0)/(double)CLOCKS_PER_SEC; } }
        S->ms=1000.0*(double)(clock()-t0)/(double)CLOCKS_PER_SEC;
        if(g_hsDbg&&(g_frameStamp%60u)==0u){
            if(S->gpu) printf("[strands] %s: %d locks and %d hairs on the GPU; the processor's part %.1f ms a frame (the haircut's places, her body and its grid), %d body points\n",P->label,S->ns,S->nh,S->ms,g_hsNB);
            else printf("[strands] %s: %d locks, %.1f ms a frame (%d substeps; %d hairs %.1f ms of it), %d body points, %d contacts, deepest push %.1f mm, worst stretch %.4f\n",
            P->label,S->ns,S->ms,g_hsSub,S->nh,S->hms,g_hsNB,S->contacts,1000.f*S->deep/(sc>1e-6f?sc:1.f),S->stretch); fflush(stdout); } }
    g_skVbo[0]=S->vbo[0]; g_skVbo[1]=S->vbo[1];
    if(S->shStamp!=g_frameStamp||1){ S->shStamp=g_frameStamp; sunStashW(P,(int)mi,S->vbo[0],S->vbo[1],(int)M->boneCount,model); }
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    for(unsigned b9=0;b9<M->boneCount&&b9<MAXB;b9++) memcpy(pal+b9*16,I9,64);
    if(S->nh){ g_hsHook.P=P; g_hsHook.mi=mi; g_hsHook.role=(int)mi==S->miDraw?1:2; memcpy(g_hsHook.vb,S->hvbo,sizeof g_hsHook.vb);   /* (11-06) the draw that follows: its hairs (body.inc, vkMesh) */
        g_hsHook.nIndex=(unsigned)S->nh*(unsigned)(S->hrp-1)*6u; g_hsHook.nVerts=(unsigned)S->nh*(unsigned)S->hrp*2u;
        hsHairColour(P,g_hsHook.tint); g_hsHook.rough=P->meshMat?P->meshMat[mi][0]:0.5f; memcpy(g_hsHook.c,S->hc,12); }
    return 2; }
