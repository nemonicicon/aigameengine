/* ---------- rpack (RPK1 v2) ---------- */
typedef struct { unsigned name; int parent; unsigned kind; float t[3],q[4],s[3]; } PNode;
_Static_assert(sizeof(PNode)==52,"a node is 52 bytes in a pack");
/* ⛔ 11-04: a pack doesn't promise its tables start on a 4-byte boundary (design/pack_format.md: 191 of 404 don't), and the math takes float pointers into the rest pose, which the compiler may assume are aligned. So this table gets a copy in memory that is */
static PNode* packNodes(const unsigned char*pk,unsigned off,unsigned n){ PNode*o=(PNode*)malloc((size_t)(n?n:1)*sizeof(PNode)); if(o) memcpy(o,pk+off,(size_t)n*sizeof(PNode)); return o; }
typedef struct { unsigned name,node,verts,idxCount,boneCount,flags,offPos,offNorm,offUV,offJoints,offWeights,offIdx,offBoneMap,offIBM,offBind; } PMesh;
/* the transparent pass, defined beside meshAlphaOf -- declared here because the draw
 * loops that use it all come earlier in the file than its definition. */

typedef struct { unsigned name; float fps; unsigned frames,nTracks,offTracks; } __attribute__((packed)) PClip;
typedef struct {
    unsigned char *pk; size_t len;
    unsigned nNodes,nMeshes,nClips,nMats,nTex;
    PNode *nodes; PMesh *meshes; PClip *clips; unsigned char *mats;
    float scale; unsigned *vbo; unsigned *tex; int *meshTex; unsigned *meshMatFlags;
    int *meshNTex;     /* ⭐ per mesh: the normal map's texture slot, or -1 (09-11) */
    float (*meshMat)[6];   /* (step 22) per mesh, what it's made of: how rough, how much a metal, how much fine detail, how deep for parallax; and (step 23) its kind for a body: 1 skin, 2 hair, 3 an eye, 4 cloth, 5 its texture's alpha says. One spare. From its MATL chunk, or the tables by name (engine/load.inc, packMaterials) */
    float cx,cz,mny;   /* rest bone-cloud center + floor (model units): every rig grounds/centers the SAME way */
    float effH,avoidR; /* rendered height (mesh-aware, giantess-correct) -> personal-space radius */
    /* ⛔ NOT `footR` -- that is the RIGHT FOOT's bone index, twenty lines down, and
     * shadowing it turned every W[tp->footR] into a float subscript. */
    float propR;       /* XZ radius of the rest bone cloud -- what a collider is built from */
    float propEx,propEz;   /* the two half-extents behind propR: a wall is long in one and thin in the other */
    float propCx,propCz;   /* ⭐ the vertex bbox CENTRE in model units (09-11): a ground tile is anchored at a corner */
    float propY0,propY1;   /* ⭐ the vertex height range (09-11); effH below is a 3.15 floor meant for bodies */
    int walkIdx;   /* first walk-ish clip (w_ prefix or contains 'alk'), -1 if none: walkers WALK */
    int runIdx, atkIdx, downIdx, idleIdx;   /* S4a/d behavior roles: sprint / strike / go down / stand (name-mined at load) */
    short*nodeCanon;               /* S4b: node index -> canon bone (-1 unmapped), for donor transfer */
    int gripNode;                  /* S5a: the hand -- first child of the right forearm; where a weapon rides */
    int gripL;                     /* P4: the LEFT hand, same election; the pose dump's grpL */
    /* P5 CANONICAL BASIS -- per-bone frames for ABSOLUTE cross-rig transfer.
     * A[c]: the bone's canonical frame at rest (X = bone direction toward the
     * canonical child, Z = rig-forward projected off X); W[c]: rest world rotation;
     * Wpar[c]: rest world rotation of the bone's parent node; anc[c]: nearest canon
     * ancestor; pre[c]: rest rotation from that ancestor's node to this bone's parent;
     * G: the rig's character frame (X = its own forward). Proven in Python first
     * (skate_src/author/retarget.py): kaeli and reva hit authored arm targets to 0.1
     * degree through this exact algebra. */
    float cbA[NCANON][4], cbW[NCANON][4], cbWpar[NCANON][4], cbPre[NCANON][4];
    int   cbAnc[NCANON]; float cbG[4]; int cbV[NCANON]; int cbOK;
    float cbLeg;                   /* P5: canonical leg length (hip->knee->ankle), rig units */
    int footL, footR;              /* S6: the feet -- first child of each lower leg; the contact test needs them */
    /* ⭐ WHEELS ARE CONTACTS TOO (ROBOT NEKO ep 9). A skater's deck stands on its wheels, so
     * grounding a rider by the lowest ANKLE buries the board: Kickflip's bench showed only the
     * nose and tail poking through the road, 0.58 rig units of it under. Every node named for
     * a wheel is elected at load and counted with the feet wherever a body is grounded. */
    int nWheel, wheel[8]; float wheelYaw;   /* ...and which way its board runs: 0 along rig Z, -pi/2 along X */
    float*gaitUPS;                 /* S6d: authored speed per clip (u/s) from the .gait sidecar, 0 = unknown */
    char label[64]; char artist[64];   /* showcase credit: model name (from pack) + artist (credits.txt) */
    int fly;   /* SKY LAYER (jacob: sea animals + birds fly through the space) */
    int drawable;   /* any mesh actually renderable? (bone-heavy skips = invisible dancers got spotlights) */
    int pb[NCANON]; int pbOK;   /* canonical dance-bone map for the procedural layer; pbOK = how many resolved */
    int danceIdx[48]; int nDance;   /* clips named m_* = the dance vocabulary (stance sources) */
    /* H1 (homecoming): SECONDARY-MOTION CHAINS -- tails, ponytails, braids, ears --
     * mined by name at load, simmed by verlet at runtime. A chain is a linear run of
     * matching bones; chB = node indices root-out, chLen = rest link lengths. */
    /* ⭐ 8 -> 32 (09-29, REVA): her shag's five chains, her ponytail's three, her jacket's five and her bandana's
     * three are sixteen -- and the 8 was a silent cap (a busy rig just lost its later chains). The miner says so
     * now when a body reaches it. */
    #define CHMAXC 32
    int nCh; short chB[CHMAXC][14]; signed char chN[CHMAXC]; float chLen[CHMAXC][14];
    int nJg; short jgRoot[4], jgTip[4];   /* ⭐ JIGGLE BONES (jacob: "kaeli is supposed to have breast jiggle physics") */
    /* ⭐ STRETCH BONES (09-29): a garment HELD BETWEEN TWO MOVING PARTS -- REVA's off-shoulder jacket, its body a band
     * from one elbow round her back to the other. Weighted to the arms and the spine by blending, linear-blend
     * skinning put the band half way between where each would carry it: a flap stood out of her back in every pose
     * (jacob's friend: "its sticking to the rig its not the model its the rig thats messing it up"). A bone named
     * Stretch_<x> keeps its head where its parent carries it (an elbow), turns to point at the head of Anchor_<x> (a
     * point on her back, on the spine), and STRETCHES along that line by the distance over its rest distance -- a
     * Stretch To constraint, the rig of choice for a drape between two limbs. LBS only (DQS cannot scale). */
    int nSt; short stB[8], stA[8]; float stLen[8], stDir[8][3];

/* ⭐ THE MORPH CAP WAS 8, chosen when the only morphs were a breath and a jaw.
 * Anima's HiPoly body arrives with a full ARKit/Character Creator face: 107 shape keys
 * (six CC visemes, 8 brow shapes, per-eye look and blink, jawOpen, smile/frown). glTF
 * also repeats every target on every PRIMITIVE, and her body splits into 15 by material,
 * so even a curated 28 shapes lands well past 8 once the non-empty pairs are counted. */
#define MORPH_MAX 128

    /* H6 (homecoming): MORPH TARGETS -- rpack v3. Header slot 14 points at a block of
     * sparse position deltas per morph; clip track type 3 drives the weights. */
    int nMorph; unsigned morphMesh[MORPH_MAX], morphSparse[MORPH_MAX], morphOff[MORPH_MAX];
    unsigned morphName[MORPH_MAX], morphLo[MORPH_MAX], morphHi[MORPH_MAX];   /* name id + the vertex RANGE each target touches */
    int fmMouth, fmEyeL, fmEyeR, fmChin;   /* face landmarks (skate_src/author/face_marks.py) */
    int eyesRig;   /* ⛔ are the eye marks SKINNED? A mark with no weights rotates nothing. */
    /* ⭐ HOW THICK THIS BODY IS, PER CANONICAL BONE, measured from its own vertices. The
     * cloth solver used six spheres and six guessed numbers and nothing at all on the arms
     * or legs, which is why a garment went through her. */
    float boneR[NCANON]; int boneRok;
    int fmMouthL, fmMouthR;                /* THE MOUTH CORNERS -- the aperture's real width */
    int fmJaw;                             /* THE JAW BONE (skate_src/author/jaw.py) */
    float faceEyeH; int faceEyeOK;   /* how high the eyes are at rest, in the world's units, worked out the first time it's asked (engine/face.inc) */
    int fmInterior;                        /* the mouth-interior MESH (teeth/tongue/cavity) */
    float mouthBind[6];   /* lip line in BIND space (x,y,z) + half-width + face height + TILT */
    /* ⭐ THE WARDROBE (09-29, jacob: "id like the clothing to be modular, reva is also a seamstress"). A body built
     * by the atelier (skate_src/reva902) tags its meshes: `wear~<slot>~<piece>~<outfit>+<outfit>` is a piece of
     * clothing or a hairstyle, `skin~h=a,b` is skin that pieces a and b cover. meshOff[] is what the pack's outfit
     * leaves undrawn: pieces not worn, and skin under pieces that are (nothing pokes through a worn garment, and
     * nothing is missing when it comes off). NULL on every body without tags. One outfit per PACK: two slots
     * drawing the same pack wear the same clothes. */
    unsigned char*meshOff, *meshKind, *meshPiece; unsigned long long*meshHiders;
    int nPiece; char pieceName[64][32]; unsigned pieceOutfits[64];
    int nOutfit; char outfitName[16][24]; int outfit; unsigned long long worn;
    /* ⭐ THE TOON PASS (10-18, THE WORLD AT THE END OF TIME): a pack carrying a node named ToonStyle (a zero-weight
     * bone the people builder adds) is drawn in the show's look -- a cel ramp into a cool shadow tint and an ink
     * line (an inverted hull). Every other body in the club is untouched. REVA_NOTOON=1 is the control. */
    int toon;
    /* ⭐ THE SHIP'S SETS (10-18, THE WORLD AT THE END OF TIME): markers a SET PROP carries (skate_src/ship/
     * build_sickbay.py -> glb2rpack.py --marker). ToonSet: the toon look lit as a ROOM is lit -- from above, every
     * wall in the light, every underside in the shadow tone, cast shadows from the sun's map (toon = 2). Glow: drawn
     * unlit at its own colour (a light panel, a screen, an emitter), no ink. NoCast: never in the sun's shadow map
     * (a ceiling over everyone, a room's walls -- an interior's key is a cheat from the lens's side, and a wall that
     * cast it would black out the room). Open: never a collider -- a shell or a run of fittings built in ROOM
     * coordinates measures as one circle as wide as the room, at its centre (propSolid decides by name, and
     * "sb_fixtures" is not a name it knows to leave open). */
    int glow, nocast, open;
    int mirOnly;   /* ⭐ (10-22) marker `MirrorOnly`: drawn only in reflections (a mirror's backing, seen in another) */
    int floor;     /* ⭐ (10-23) marker `Floor`: a floor that a set's `gloss` line makes glossy (glossFloorPass) */
    /* ⭐ (10-22) SHEER: a set prop carrying a marker node `AlphaNN` is drawn NN% opaque, blended over what is behind it
     * (a martini glass): set props were opaque only -- the blend existed for a body's meshes, never a prop's. 0 = opaque */
    float sheer;
    /* ⭐ TRUE SIZE (10-18): a pack carrying a node named TrueScale was built in METRES at its real size, and keeps
     * it -- the club's 1.75 units a metre -- instead of being normalised to the crowd's 3.15 (which blew THE WORLD
     * AT THE END OF TIME's five-year-olds up to the height of the twelve-year-old: "kids are too big lmao"). */
    int trueScale; float trueH;   /* trueH: its true height in club units (10-22: the gait test of a child) */
    /* ⭐ PER-VERTEX COLLISION (10-18): a pack carrying a node named VertexCollide has its skirts (~skirt~/~cloth
     * meshes) pushed out of its legs and its hair (wear~hair~ meshes) out of its head and neck, EVERY VERTEX (the
     * old renderer did it in its vertex shader; since step 23 the hair's rule is done on the processor, engine/body.inc hairFitPos, and a skirt is simulated); its hair chain joints out of the head too. The head is an ellipsoid measured from
     * the pack's own head vertices in the head bone's frame (headC/headR, measureHead). */
    int vcol; int headOK; float headC[3], headR[3]; float headW0[16];
} Pack;
static int toonOn(const void*Pv,int toon);


/* ⭐ (10-22) MIRRORS: "half the walls on the ship were mirrored to make the rooms seem more spacious" (Pohl) --
 * a set's `mirror x y z yaw w h`; see mirInst */
typedef struct { float p[3], n[3], w, h, R[16]; } Mirror;
static Mirror g_mir[4]; static int g_nMir=0, g_mirOff=-1;
/* ⭐ (10-23) THE GLOSSY FLOOR: a set's `gloss y [F0] [max] [fade]` -- a mirror lying down (g_glossM), see glossFloorPass */
static struct { int on; float y, f0, fmax, fade; } g_gloss={0,0.f,0.06f,0.35f,2.5f};
static Mirror g_glossM; static int g_glossDraw=0;


static int g_skirtLive=0; static float g_skirtBC[3]={0,0,0}; static unsigned g_skVbo[2]={0,0};   /* THE HANGING CLOTH (10-18) */
#define MOFF(P,mi) ((P)->meshOff&&(P)->meshOff[mi])
static int wardFind(char tab[][32],int n,const char*nm){ for(int i=0;i<n;i++) if(!strcmp(tab[i],nm)) return i; return -1; }
static int wardPiece(Pack*P,const char*nm){ int i=wardFind(P->pieceName,P->nPiece,nm); if(i>=0) return i;
    if(P->nPiece>=64) return 63;
    snprintf(P->pieceName[P->nPiece],32,"%s",nm); P->pieceOutfits[P->nPiece]=0; return P->nPiece++; }
static int wardOutfitFind(Pack*P,const char*nm){ for(int i=0;i<P->nOutfit;i++) if(!strcmp(P->outfitName[i],nm)) return i; return -1; }
static int wardOutfit(Pack*P,const char*nm){ int i=wardOutfitFind(P,nm); if(i>=0) return i;
    if(P->nOutfit>=16) return 15;
    snprintf(P->outfitName[P->nOutfit],24,"%s",nm); return P->nOutfit++; }
static void wardApply(Pack*P){ if(!P||!P->meshOff) return;
    for(unsigned m=0;m<P->nMeshes;m++)
        P->meshOff[m]=(unsigned char)(P->meshKind[m]==2 ? (!((P->worn>>P->meshPiece[m])&1ull)||(P->meshHiders[m]&P->worn)!=0)
                                     : P->meshKind[m]==1 ? ((P->meshHiders[m]&P->worn)!=0) : 0); }
/* put on an outfit: every piece tagged with it, nothing else. `bare` is every body's: nothing on, and all its skin drawn. A body is modelled bare first
 * and dressed after (jacob, 11-04: "we need to model her without clothes first so that when we add clothes to her they fit right"), and once it has a
 * piece its first outfit is what it wears, so its bare body needs a name to be asked for by */
static int wardWear(Pack*P,const char*outfit){ if(!P||!P->meshOff) return 0;
    if(!strcmp(outfit,"bare")&&wardOutfitFind(P,outfit)<0){ P->outfit=-1; P->worn=0; wardApply(P);
        printf("[wardrobe] %s wears nothing ('bare')\n",P->label); fflush(stdout); return 1; }
    int o=wardOutfitFind(P,outfit); if(o<0){ printf("[wardrobe] %s has no outfit '%s' --",P->label,outfit);
        for(int i=0;i<P->nOutfit;i++) printf(" %s",P->outfitName[i]);
        printf("\n"); fflush(stdout); return 0; }
    P->outfit=o; P->worn=0;
    for(int i=0;i<P->nPiece;i++) if(P->pieceOutfits[i]&(1u<<o)) P->worn|=1ull<<i;
    wardApply(P); int n=0; for(int i=0;i<P->nPiece;i++) n+=(int)((P->worn>>i)&1ull);
    printf("[wardrobe] %s wears '%s' (%d pieces)\n",P->label,outfit,n); fflush(stdout); return 1; }
/* wear=+piece,-piece: one piece on or off, over whatever she has on */
static void wardToggle(Pack*P,const char*list){ if(!P||!P->meshOff) return;
    char tmp[256]; snprintf(tmp,sizeof tmp,"%s",list);
    for(char*t=strtok(tmp,",");t;t=strtok(NULL,",")){ int on=1; if(*t=='-'){ on=0; t++; } else if(*t=='+') t++;
        int i=wardFind(P->pieceName,P->nPiece,t); if(i<0){ printf("[wardrobe] %s has no piece '%s'\n",P->label,t); continue; }
        if(on) P->worn|=1ull<<i; else P->worn&=~(1ull<<i);
        printf("[wardrobe] %s: %s %s\n",P->label,on?"puts on":"takes off",t); }
    fflush(stdout); wardApply(P); }
static float g_heroMorphW[MORPH_MAX];          /* the hero's live morph weights, fed by clipSampleF */
static float*g_morphSink=0; static int g_morphSinkN=0;
/* quat helpers for the procedural layer */
static void qmul(float*r,const float*a,const float*b){
    float x=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
    float y=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    float z=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
    float w=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
    r[0]=x;r[1]=y;r[2]=z;r[3]=w; }
static void qaxis(float*q,float ax,float ay,float az,float ang){
    float s=sinf(ang*0.5f); q[0]=ax*s; q[1]=ay*s; q[2]=az*s; q[3]=cosf(ang*0.5f); }
/* Shortest-arc rotation taking unit vector a onto unit vector b. Used by the foot IK to
 * aim a limb without ever needing a matrix inverse. */
static void qarc(float*q,const float*a,const float*b){
    float d=a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
    float c[3]={a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
    float cl=sqrtf(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
    if(cl<1e-6f){
        if(d>0.f){ q[0]=q[1]=q[2]=0.f; q[3]=1.f; return; }
        float ax[3]={1.f,0.f,0.f}; if(fabsf(a[0])>0.9f){ ax[0]=0.f; ax[1]=1.f; }
        c[0]=a[1]*ax[2]-a[2]*ax[1]; c[1]=a[2]*ax[0]-a[0]*ax[2]; c[2]=a[0]*ax[1]-a[1]*ax[0];
        cl=sqrtf(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]); if(cl<1e-6f) cl=1.f;
        q[0]=c[0]/cl; q[1]=c[1]/cl; q[2]=c[2]/cl; q[3]=0.f; return; }
    float ang=atan2f(cl,d), s=sinf(ang*0.5f);
    q[0]=c[0]/cl*s; q[1]=c[1]/cl*s; q[2]=c[2]/cl*s; q[3]=cosf(ang*0.5f);
}

static void qconj(float*r,const float*q){ r[0]=-q[0]; r[1]=-q[1]; r[2]=-q[2]; r[3]=q[3]; }
/* ---- H1: VERLET CHAINS (design/homecoming.md) --------------------------------------
 * Tails, ponytails, braids and ears get physics: each mined chain integrates points
 * with damping and a spring toward the ANIMATED pose (so authored motion leads and the
 * sim adds lag and settle on top -- an animated tail is followed, never overwritten),
 * then distance constraints re-rigidify the links and the chain bones' world matrices
 * are re-aimed at the simulated points before skinning. State is per body slot (hero,
 * partner); pack switches re-seed from the animated pose so nothing explodes. */
static int   g_chainsOn=1;      /* ctl chains=0/1 */
static float g_chStiff=0.22f;   /* per-frame spring toward the animated pose (at 60fps) */
static float g_chDamp=0.88f;    /* velocity kept per 1/60 s (see chainSim: it is raised to the step) */
/* \u2b50 GRAVITY, MEASURED, NOT STYLED. The club is ~1.75x metric -- a 1.75 m woman
 * stands ~3.06 club units -- so one club unit is ~0.572 m and real gravity is
 * 9.81/0.572 = 17.1 club units/s^2. The old constant delivered 8.4x that, which is why
 * a braid thrashed instead of swinging (jacob: "they dance on her like medusa snakes").
 * ctl `chaingrav=` scales it: 1.0 is real, and a dancefloor may want more. */
static float g_chGrav=1.25f;    /* multiples of real gravity; ctl chaingrav= */
#define CLUB_UNITS_PER_S2 (9.81f/0.572f)
static double g_chDt=1.0/60.0;  /* the frame dt, published by the main loop */
static int    g_stepHeld=0;     /* STOP MOTION: 1 on a frame that re-exposes the last pose (animTick) */
/* ⛔ THE HERO IS DRAWN TWICE when she is not solid (a depth pass, then colour), and a
 * simulation that runs in the draw would take two steps of gravity per frame. One stamp
 * per frame, and the solver refuses to advance twice on the same one. */
static unsigned g_frameStamp=0;
/* ⭐ SET ACTORS SWING TOO (x3d, 09-27). The chains ran for the hero and the cast only, so a body with hair
 * physics standing in a set -- a crowd, the bench's four-angle sheet -- had rigid hair. An actor takes its
 * own state slot after the cast's, one per actor up to CH_ACTORS (past that it says so, once);
 * REVA_ACTORCHAINS=0 is the control. */
#define CH_ACTORS 16
#define CH_SLOTS (SCN_MAXCAST+CH_ACTORS)
static int g_actorChainSlot=-1;   /* the slot of the actor being drawn, for the length of its actorDraw call */
/* ⭐ CHAINS FEEL THE WORLD (x3d, 09-27). The solver lived in the body's own frame, so hair only answered POSE
 * changes: a skater rolling at 3 m/s, carving, popping, landing -- her hair could not know. Measured on x3d as
 * the scene hero: tip lag mean 1 cm, max 7 cm over a whole run of tricks, i.e. rigid. Each body now PUBLISHES
 * the rig->world matrix it is drawn with, and the solver carries its state through the body's motion between
 * the last two published frames (one frame late: invisible, and no draw path had to be reordered). Inertia
 * then comes for free -- the hair swings on a carve, lifts on a landing -- and the verlet damping, now applied
 * to WORLD velocity, is air drag: moving hair trails. Gravity is the world's down in her frame, so a rider
 * pitched on a pipe wall hangs toward the ground, not toward her feet. REVA_CHWORLD=0 is the control. */
static float g_chM[CH_SLOTS][2][16]; static unsigned g_chMSt[CH_SLOTS][2];
static void chainPublish(int slot,const float*m){
    if(slot<0||slot>=CH_SLOTS) return;
    if(g_chMSt[slot][1]!=g_frameStamp){           /* a new frame: the last one becomes the previous */
        memcpy(g_chM[slot][0],g_chM[slot][1],64); g_chMSt[slot][0]=g_chMSt[slot][1]; g_chMSt[slot][1]=g_frameStamp; }
    memcpy(g_chM[slot][1],m,64); }
static float g_chLagMax=0.f; static double g_chLagAcc=0; static long g_chLagN=0;  /* tip lag telemetry */
/* ⭐ CLOTH MEETS THE LEGS (10-18, THE WORLD AT THE END OF TIME's robes). A cloth chain (its root named Cloth*)
 * is pushed out of a capsule round each thigh (hip -> knee) and shin (knee -> ankle), each as thick as the body
 * measured itself (measureBoneR: the mean distance of its own vertices from the bone's segment, x1.15 -- on the
 * skin). The fabrication note that parked REVA 902's coat said exactly this was owed: "the coat's skirt on cloth
 * chains with LEG CAPSULE collision". Hair does not collide (the robe is what meets a knee). REVA_CHCOLLIDE=0 is
 * the control run; REVA_CHCOLR scales the capsules; [chdbg] reports the pushes. */
static int g_chCol=-1; static float g_chColR=1.0f; static long g_chColN=0;
static int g_vcolOn; static int headMats(Pack*P,M4*W,float*Mo,float*MI);
static void measureBoneR(Pack*P); static const char*packStr(void*Pv,unsigned off);
static void chainArc(float*m,const float*from,const float*to){
    /* rotate m's 3x3 by the shortest arc taking direction from -> to */
    float lf=sqrtf(from[0]*from[0]+from[1]*from[1]+from[2]*from[2]);
    float lt=sqrtf(to[0]*to[0]+to[1]*to[1]+to[2]*to[2]);
    if(lf<1e-8f||lt<1e-8f) return;
    float f[3]={from[0]/lf,from[1]/lf,from[2]/lf}, t[3]={to[0]/lt,to[1]/lt,to[2]/lt};
    float d=f[0]*t[0]+f[1]*t[1]+f[2]*t[2];
    float q[4];
    if(d<-0.9999f){ q[0]=1; q[1]=0; q[2]=0; q[3]=0; }
    else { float c[3]={f[1]*t[2]-f[2]*t[1], f[2]*t[0]-f[0]*t[2], f[0]*t[1]-f[1]*t[0]};
        q[0]=c[0]; q[1]=c[1]; q[2]=c[2]; q[3]=1.f+d;
        float l=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
        q[0]/=l; q[1]/=l; q[2]/=l; q[3]/=l; }
    for(int col=0;col<3;col++){ float*v=&m[col*4];
        float uv[3]={q[1]*v[2]-q[2]*v[1], q[2]*v[0]-q[0]*v[2], q[0]*v[1]-q[1]*v[0]};
        float uuv[3]={q[1]*uv[2]-q[2]*uv[1], q[2]*uv[0]-q[0]*uv[2], q[0]*uv[1]-q[1]*uv[0]};
        v[0]+=2.f*(q[3]*uv[0]+uuv[0]); v[1]+=2.f*(q[3]*uv[1]+uuv[1]); v[2]+=2.f*(q[3]*uv[2]+uuv[2]); } }
static int g_stOff=-1;
static void ctRotateTree(Pack*P,M4*W,int j,const float*axis,float ang);   /* defined with the contact pass */
static void stretchTick(void*Pv,M4*W){
    Pack*P=(Pack*)Pv;
    if(g_stOff<0){ const char*e=getenv("REVA_NOSTRETCH"); g_stOff=(e&&e[0]&&e[0]!='0')?1:0; }
    if(g_stOff||!P||!P->nSt) return;
    for(int q=0;q<P->nSt;q++){ float*m=W[P->stB[q]]; const float*an=W[P->stA[q]];
        const float*ld=P->stDir[q];
        float cur[3], to[3]={an[12]-m[12],an[13]-m[13],an[14]-m[14]};
        for(int r=0;r<3;r++) cur[r]=m[r]*ld[0]+m[4+r]*ld[1]+m[8+r]*ld[2];      /* where its rest line points now */
        float lc=sqrtf(cur[0]*cur[0]+cur[1]*cur[1]+cur[2]*cur[2]), lt=sqrtf(to[0]*to[0]+to[1]*to[1]+to[2]*to[2]);
        if(lc<1e-8f||lt<1e-8f) continue;
        /* turned to the anchor -- and everything hung from it (a drape's cloth chains) turns WITH it, unscaled */
        float ax[3]={cur[1]*to[2]-cur[2]*to[1], cur[2]*to[0]-cur[0]*to[2], cur[0]*to[1]-cur[1]*to[0]};
        float sn=sqrtf(ax[0]*ax[0]+ax[1]*ax[1]+ax[2]*ax[2]), cs=cur[0]*to[0]+cur[1]*to[1]+cur[2]*to[2];
        if(sn>1e-9f){ ax[0]/=sn; ax[1]/=sn; ax[2]/=sn; ctRotateTree(P,W,P->stB[q],ax,atan2f(sn,cs)); }
        float k=lt/lc; if(k<0.2f) k=0.2f; if(k>5.f) k=5.f;   /* and stretched to reach it: this bone only */
        float u[3]={to[0]/lt,to[1]/lt,to[2]/lt};
        for(int c=0;c<3;c++){ float*v=&m[c*4]; float dp=(u[0]*v[0]+u[1]*v[1]+u[2]*v[2])*(k-1.f);
            v[0]+=u[0]*dp; v[1]+=u[1]*dp; v[2]+=u[2]*dp; } } }
static void chainSim(void*Pv,M4*world,int who){
    Pack*P=(Pack*)Pv;
    /* ⛔ ONE STATE PER CAST SLOT, not per hero/partner (ROBOT NEKO ep 2). With [2] every partner
     * passed who=1, so in a three-hander slot 2 reseeded slot 1's braids on every call and
     * neither body's hair ever simulated. Indexed by g_castI; sized to SCN_MAXCAST (asserted). */
    static float sp[CH_SLOTS][CHMAXC][14][3], pp[CH_SLOTS][CHMAXC][14][3];
    static void* seeded[CH_SLOTS];
    static unsigned stampOf[CH_SLOTS];
    if(!g_chainsOn||!P||!P->nCh||who<0||who>=CH_SLOTS) return;
    if(g_chCol<0){ const char*e=getenv("REVA_CHCOLLIDE"); g_chCol=!(e&&e[0]=='0');
        const char*r=getenv("REVA_CHCOLR"); if(r&&*r) g_chColR=(float)atof(r);
        printf("[chains] cloth vs legs: %s (capsules x%.2f)\n",g_chCol?"ON":"OFF (REVA_CHCOLLIDE=0)",g_chColR); fflush(stdout); }
    /* ⛔ ONE STEP PER FRAME PER SLOT. An actor is drawn once per PANEL -- the bench's four-angle sheet draws
     * it four times a frame -- and every call used to be a verlet step, so the hair would have swung four
     * times as fast on a sheet as in a take. A second call in the same frame poses from the state it has. */
    int adv=(stampOf[who]!=g_frameStamp); stampOf[who]=g_frameStamp;
    static int wOn=-1; if(wOn<0){ const char*e9=getenv("REVA_CHWORLD"); wOn=!(e9&&e9[0]=='0'); }
    float gdir[3]={0.f,-1.f,0.f}, wsc=0.f, D9[12]; int haveD=0;
    if(wOn&&g_chMSt[who][1]&&g_chMSt[who][1]+1u>=g_frameStamp){
        const float*m1=g_chM[who][1];
        float s2=m1[0]*m1[0]+m1[1]*m1[1]+m1[2]*m1[2];
        if(s2>1e-12f){ wsc=sqrtf(s2);
            for(int r=0;r<3;r++) gdir[r]=-m1[r*4+1]/wsc;                   /* world down, in her frame */
            if(adv&&g_chMSt[who][1]+1u==g_frameStamp&&g_chMSt[who][0]+2u==g_frameStamp){
                const float*m0=g_chM[who][0];
                /* D = inverse(M_last) * M_before: where a point that stayed put in the WORLD now sits in her
                 * frame. The uniform scale cancels; a jump past half a unit or 45 degrees is a cut, not motion. */
                for(int c=0;c<3;c++) for(int r=0;r<3;r++)
                    D9[c*3+r]=(m1[r*4]*m0[c*4]+m1[r*4+1]*m0[c*4+1]+m1[r*4+2]*m0[c*4+2])/s2;
                float dt9[3]={m0[12]-m1[12],m0[13]-m1[13],m0[14]-m1[14]};
                for(int r=0;r<3;r++) D9[9+r]=(m1[r*4]*dt9[0]+m1[r*4+1]*dt9[1]+m1[r*4+2]*dt9[2])/s2;
                float tl=sqrtf(D9[9]*D9[9]+D9[10]*D9[10]+D9[11]*D9[11]);
                float tr=D9[0]+D9[4]+D9[8];                                  /* 1+2cos(angle) */
                haveD=(tl*wsc<0.35f&&tr>1.f+2.f*0.866f); } } }      /* a cut, not motion: skating is <0.2 u a frame even at 30 fps;
                                                                  * a scripted snap under 1.5 u would otherwise whip the hair */
    float dtf=(float)g_chDt; if(dtf>0.033f)dtf=0.033f; if(dtf<1e-4f)dtf=1e-4f;
    float sK=g_chStiff*dtf*60.f; if(sK>0.9f)sK=0.9f;
    /* \u26d4 DAMPING WAS PER FRAME, SO IT MEANT DIFFERENT THINGS AT DIFFERENT RATES.
     * The live window runs at 60 and every capture runs at REVA_FIXEDSTEP 30, so the
     * hair in the film was never the hair on screen -- tuning it by eye in the window
     * could not have worked. g_chDamp is defined per 1/60 s; raise it to the step. */
    float dmp=powf(g_chDamp,dtf*60.f);
    /* a*dt^2, in RIG units: the pack's scale maps rig->world, so divide by it. */
    float grav=-CLUB_UNITS_PER_S2*g_chGrav*dtf*dtf/(wsc>1e-6f?wsc:(P->scale>1e-6f?P->scale:1.f));
    for(int c=0;c<P->nCh;c++){
        int n=P->chN[c]; if(n<2) continue;
        /* ⭐ STIFF HAIR (10-18): a chain whose root is named *Stiff* springs to its pose 3x harder (a bob) */
        float sKc=sK; { const char*rn9=packStr(P,P->nodes[P->chB[c][0]].name);
            if(rn9&&(strstr(rn9,"Stiff")||strstr(rn9,"stiff"))){ sKc=sK*3.f; if(sKc>0.9f) sKc=0.9f; } }
        float a[14][3];
        for(int i=0;i<n;i++){ float*w=&world[P->chB[c][i]][12]; a[i][0]=w[0]; a[i][1]=w[1]; a[i][2]=w[2]; }
        if(seeded[who]!=(void*)P){
            for(int cc=0;cc<P->nCh;cc++) for(int i=0;i<14;i++)
                for(int k=0;k<3;k++){ sp[who][cc][i][k]=0; pp[who][cc][i][k]=0; }
        }
        if(seeded[who]!=(void*)P||
           (sp[who][c][0][0]==0&&sp[who][c][0][1]==0&&sp[who][c][0][2]==0)){
            for(int i=0;i<n;i++) for(int k=0;k<3;k++){ sp[who][c][i][k]=a[i][k]; pp[who][c][i][k]=a[i][k]; } }
        if(haveD&&seeded[who]==(void*)P) for(int i=1;i<n;i++){      /* carry the state through her motion */
            float*v9[2]={sp[who][c][i],pp[who][c][i]};
            for(int q9=0;q9<2;q9++){ float x9=v9[q9][0],y9=v9[q9][1],z9=v9[q9][2];
                for(int r=0;r<3;r++) v9[q9][r]=D9[r]*x9+D9[3+r]*y9+D9[6+r]*z9+D9[9+r]; } }
        sp[who][c][0][0]=a[0][0]; sp[who][c][0][1]=a[0][1]; sp[who][c][0][2]=a[0][2];
        /* ⭐ A HELD EXPOSURE DOES NOT INTEGRATE. dt is zero on a held frame and the clamp
         * above lifts it to 1e-4 -- but verlet's inertia term does not care about dt, so the
         * hair would keep swinging between exposures while the body stood still. Skip the
         * step and the solve; the write-back below still poses the bones from where it is. */
        if(!g_stepHeld&&adv) for(int i=1;i<n;i++){ float np[3], step[3];
            for(int k=0;k<3;k++){
                float vel=(sp[who][c][i][k]-pp[who][c][i][k])*dmp;
                step[k]=vel+(a[i][k]-sp[who][c][i][k])*sKc-gdir[k]*grav; }
            /* \u26d4 CLAMP THE STEP TO THE LINK. gravity alone is ~0.165 rig units per frame
             * (-2.4/scale * dt^2 * 60) and Anima's braid links are ~0.04, so a node was being
             * thrown four segment-lengths in one step -- past the point where a distance
             * constraint can recover the SHAPE rather than just the length, which is the
             * difference between hair that swings and hair that snaps somewhere else.
             * jacob saw it as one braid flying loose over a parked van. */
            { float L9=P->chLen[c][i]; if(L9>1e-7f){
                float sl=sqrtf(step[0]*step[0]+step[1]*step[1]+step[2]*step[2]);
                float mx=0.5f*L9;
                if(sl>mx){ float r9=mx/sl; step[0]*=r9; step[1]*=r9; step[2]*=r9; } } }
            for(int k=0;k<3;k++) np[k]=sp[who][c][i][k]+step[k];
            for(int k=0;k<3;k++){ pp[who][c][i][k]=sp[who][c][i][k]; sp[who][c][i][k]=np[k]; } }
        /* \u2b50 PASSES SCALE WITH LENGTH. Gauss-Seidel moves a correction ONE LINK per pass
         * from the pinned root, so a fixed 2 could never reach link 8 of an eight-bone braid
         * -- the tip solved against stale neighbours every frame. The rig this solver shipped
         * against had 2-3 bone tails and the longest anything else carries is six, so two was
         * never measured against a braid. n passes for an n-link chain; hers is 8x8. */
        if(!g_stepHeld&&adv){ int npass=n; if(npass<2) npass=2; if(npass>14) npass=14;
        for(int pass=0;pass<npass;pass++)
            for(int i=1;i<n;i++){ float d[3]; float L=P->chLen[c][i]; if(L<1e-7f) continue;
                for(int k=0;k<3;k++) d[k]=sp[who][c][i][k]-sp[who][c][i-1][k];
                float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-8f) continue;
                float r=L/dl;
                for(int k=0;k<3;k++) sp[who][c][i][k]=sp[who][c][i-1][k]+d[k]*r; } }
        /* ⭐ HAIR NEVER INWARD (10-18): a VertexCollide pack's hair joints never nearer the head's centre (in the
         * ellipsoid's normalised frame) than the animated pose puts them -- the pose carries hair rigid on the head */
        if(!g_stepHeld&&adv&&P->vcol&&g_vcolOn!=0){
            const char*rn=packStr(P,P->nodes[P->chB[c][0]].name);
            float Mo[16],MI[16];
            if(rn&&(strstr(rn,"Hair")||strstr(rn,"hair"))&&headMats(P,world,Mo,MI)){ int pushed=0;
                for(int i=1;i<n;i++){ float*x=sp[who][c][i]; float u[3], ua[3];
                    for(int r=0;r<3;r++){ u[r]=MI[r]*x[0]+MI[4+r]*x[1]+MI[8+r]*x[2]+MI[12+r];
                                          ua[r]=MI[r]*a[i][0]+MI[4+r]*a[i][1]+MI[8+r]*a[i][2]+MI[12+r]; }
                    float L=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]), La=0.995f*sqrtf(ua[0]*ua[0]+ua[1]*ua[1]+ua[2]*ua[2]);
                    if(L<La&&L>1e-5f){ for(int r=0;r<3;r++) u[r]*=La/L;
                        for(int r=0;r<3;r++) x[r]=Mo[r]*u[0]+Mo[4+r]*u[1]+Mo[8+r]*u[2]+Mo[12+r];
                        pushed++; } }
                if(pushed){ g_chColN+=pushed;
                    for(int i=1;i<n;i++){ float d[3]; float L=P->chLen[c][i]; if(L<1e-7f) continue;
                        for(int k=0;k<3;k++) d[k]=sp[who][c][i][k]-sp[who][c][i-1][k];
                        float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-8f) continue;
                        float r=L/dl;
                        for(int k=0;k<3;k++) sp[who][c][i][k]=sp[who][c][i-1][k]+d[k]*r; } } } }
        /* ⭐ CLOTH MEETS THE LEGS (10-18): out of the thigh and shin capsules, then the lengths put back.
         * ⛔ NOT on a VertexCollide pack: there the vertex shader drapes every vertex, and a joint pushed out here
         * swung the chain's fixed length up like a flap hinged at the hip -- the hem rose to the knee and left a flat
         * underside (a line across the leg, a box from the side) the shader could never pull back down */
        if(!g_stepHeld&&adv&&g_chCol&&!(P->vcol&&g_vcolOn!=0)){
            const char*rn=packStr(P,P->nodes[P->chB[c][0]].name);
            if(rn&&(strstr(rn,"Cloth")||strstr(rn,"cloth"))){
                if(!P->boneRok) measureBoneR(P);
                int sa[4],sb[4]; float sr[4]; int ns=0;
                const int segs[4][3]={{12,13,-1},{13,-2,-1},{14,15,-1},{15,-3,-1}};
                for(int q=0;q<4;q++){ int A=P->pb[segs[q][0]];
                    int B=segs[q][1]>=0?P->pb[segs[q][1]]:(segs[q][1]==-2?P->footL:P->footR);
                    float rr=P->boneR[segs[q][0]];
                    if(A>=0&&B>=0&&(unsigned)A<P->nNodes&&(unsigned)B<P->nNodes&&rr>0.f){
                        sa[ns]=A; sb[ns]=B; sr[ns]=rr*g_chColR; ns++; } }
                int pushed=0;
                for(int i=1;i<n;i++){ float*x=sp[who][c][i];
                    for(int q=0;q<ns;q++){ const float*A=&world[sa[q]][12], *B=&world[sb[q]][12];
                        float e[3]={B[0]-A[0],B[1]-A[1],B[2]-A[2]};
                        float el=e[0]*e[0]+e[1]*e[1]+e[2]*e[2], t=0.f;
                        if(el>1e-12f){ t=((x[0]-A[0])*e[0]+(x[1]-A[1])*e[1]+(x[2]-A[2])*e[2])/el;
                            if(t<0.f) t=0.f;
                            if(t>1.f) t=1.f; }
                        float cpt[3]={A[0]+e[0]*t,A[1]+e[1]*t,A[2]+e[2]*t};
                        float d[3]={x[0]-cpt[0],x[1]-cpt[1],x[2]-cpt[2]};
                        float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
                        if(dl<sr[q]){
                            /* THE TENT (10-18): out along the joint's own way out of the body, not radially */
                            int hp=P->pb[0]; float nv[3]={0.f,0.f,0.f}, nl=0.f;
                            if(hp>=0&&(unsigned)hp<P->nNodes){ nv[0]=x[0]-world[hp][12]; nv[2]=x[2]-world[hp][14];
                                nl=sqrtf(nv[0]*nv[0]+nv[2]*nv[2]); }
                            if(nl<1e-6f){ if(dl>1e-7f){ float kk=sr[q]/dl; for(int k=0;k<3;k++) x[k]=cpt[k]+d[k]*kk; } }
                            else { nv[0]/=nl; nv[2]/=nl;
                                float eel=sqrtf(el>1e-12f?el:1e-12f), eu[3]={e[0]/eel,e[1]/eel,e[2]/eel};
                                float dq=d[0]*eu[0]+d[1]*eu[1]+d[2]*eu[2], dn=nv[0]*eu[0]+nv[2]*eu[2];
                                float qp[3]={d[0]-eu[0]*dq,d[1]-eu[1]*dq,d[2]-eu[2]*dq};
                                float np[3]={nv[0]-eu[0]*dn,-eu[1]*dn,nv[2]-eu[2]*dn};
                                float a9=np[0]*np[0]+np[1]*np[1]+np[2]*np[2], b9=qp[0]*np[0]+qp[1]*np[1]+qp[2]*np[2];
                                float c9=qp[0]*qp[0]+qp[1]*qp[1]+qp[2]*qp[2]-sr[q]*sr[q];
                                float s9=(a9>1e-4f)?(-b9+sqrtf(fmaxf(b9*b9-a9*c9,0.f)))/a9:(sr[q]-dl);
                                if(s9>0.f){ x[0]+=nv[0]*s9; x[2]+=nv[2]*s9; } }
                            pushed++; } } }
                if(pushed){ g_chColN+=pushed;
                    for(int i=1;i<n;i++){ float d[3]; float L=P->chLen[c][i]; if(L<1e-7f) continue;
                        for(int k=0;k<3;k++) d[k]=sp[who][c][i][k]-sp[who][c][i-1][k];
                        float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(dl<1e-8f) continue;
                        float r=L/dl;
                        for(int k=0;k<3;k++) sp[who][c][i][k]=sp[who][c][i-1][k]+d[k]*r; } } } }
        { float dx=a[n-1][0]-sp[who][c][n-1][0], dy=a[n-1][1]-sp[who][c][n-1][1], dz=a[n-1][2]-sp[who][c][n-1][2];
          float lag=sqrtf(dx*dx+dy*dy+dz*dz);
          if(lag>g_chLagMax) g_chLagMax=lag;
          g_chLagAcc+=lag; g_chLagN++; }
        for(int i=0;i<n;i++){ int b=P->chB[c][i];
            float fromD[3],toD[3]; int j=(i<n-1)?i+1:i, j0=(i<n-1)?i:i-1;
            for(int k=0;k<3;k++){ fromD[k]=a[j][k]-a[j0][k]; toD[k]=sp[who][c][j][k]-sp[who][c][j0][k]; }
            chainArc(world[b],fromD,toD);
            world[b][12]=sp[who][c][i][0]; world[b][13]=sp[who][c][i][1]; world[b][14]=sp[who][c][i][2]; } }
    { static int dbg=-1; static double lastP=0;
      if(dbg<0){ const char*e=getenv("REVA_CHDBG"); dbg=(e&&e[0]=='1')?1:0; }
      if(dbg&&P->nCh){ static double acc=0; acc+=g_chDt;
        if(acc-lastP>1.0){ lastP=acc;
          printf("[chdbg] tip lag: max %.4f mean %.4f rig-u over %ld samples; cloth pushed off the legs %ld times\n",
                 g_chLagMax,(float)(g_chLagN?g_chLagAcc/g_chLagN:0),g_chLagN,g_chColN);
          fflush(stdout); g_chLagMax=0; g_chLagAcc=0; g_chLagN=0; g_chColN=0; } } }
    seeded[who]=(void*)P; }
/* column frame [X Y Z] -> quaternion (xyzw), Shepperd -- P5 canonical frames */
static void m3q(const float*X,const float*Y,const float*Z,float*q){
    float m0=X[0],m1=Y[0],m2=Z[0],m3=X[1],m4=Y[1],m5=Z[1],m6=X[2],m7=Y[2],m8=Z[2];
    float tr=m0+m4+m8,s;
    if(tr>0){ s=sqrtf(tr+1.f)*2.f; q[0]=(m7-m5)/s; q[1]=(m2-m6)/s; q[2]=(m3-m1)/s; q[3]=s/4.f; }
    else if(m0>m4&&m0>m8){ s=sqrtf(1.f+m0-m4-m8)*2.f; q[0]=s/4.f; q[1]=(m1+m3)/s; q[2]=(m2+m6)/s; q[3]=(m7-m5)/s; }
    else if(m4>m8){ s=sqrtf(1.f+m4-m0-m8)*2.f; q[0]=(m1+m3)/s; q[1]=s/4.f; q[2]=(m5+m7)/s; q[3]=(m2-m6)/s; }
    else { s=sqrtf(1.f+m8-m0-m4)*2.f; q[0]=(m2+m6)/s; q[1]=(m5+m7)/s; q[2]=s/4.f; q[3]=(m3-m1)/s; }
    float l=sqrtf(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]); if(l>1e-6f){ q[0]/=l; q[1]/=l; q[2]/=l; q[3]/=l; } }
static void qnlerp(float*r,const float*a,const float*b,float t){
    float d=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]; float s=d<0?-t:t;
    for(int k=0;k<4;k++) r[k]=a[k]*(1-t)+b[k]*s;
    float l=sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]+r[3]*r[3]);
    if(l>1e-3f){ for(int k=0;k<4;k++) r[k]/=l; }
    else memcpy(r,b,16);   /* SQUID CURE: near-antipodal nlerp cancels to ~zero -> a collapsed
        matrix for ONE frame = triangles flash across the screen (jacob's wave-race squid). */ }
/* canonical dance bones: 0 hips 1 spine 2 spine1 3 spine2 4 neck 5 head
 * 6 Lsho 7 Larm 8 Lfore 9 Rsho 10 Rarm 11 Rfore 12 LupLeg 13 Lleg 14 RupLeg 15 Rleg */
static int canonOf(const char*raw){
    char n[96]; int len=0;
    for(const char*p=raw;*p&&len<95;p++){ char c=*p; if(c>='A'&&c<='Z')c+=32; if(c!=':'&&c!=' '&&c!='.') n[len++]=c; }   /* '.' skipped: rigify shoulder.L -> shoulderl (cast expansion) */
    n[len]=0;
    if(!strncmp(n,"mixamorig",9)) memmove(n,n+9,len-9+1), len-=9;
    /* ⭐ FINGERS (09-11). The canon was 16 bones and no finger ever transferred: every held
     * mug sat in a flat hand and a zombie's claw arrived open. Thirty more slots -- five
     * fingers, three joints, two sides -- parsed BEFORE the digit strip below, because the
     * joint number is the digit it strips. Four families:
     *   UE        thumb_01_l  index_02_r  (metacarpals and _04 tips are not joints)
     *   mixamo    lefthandthumb1  righthandindex3  (4 is the tip)
     *   CC        cc_base_l_thumb1  cc_base_r_index2
     *   Daz G9    l_thumb1  r_mid2  r_pinky3
     * REVA_NOFINGERS=1 puts the canon back to 16. */
    { static int nof=-1; if(nof<0){ const char*e=getenv("REVA_NOFINGERS"); nof=(e&&e[0]!='0')?1:0; }
      if(!nof){
        static const char*FN[5]={"thumb","index","middle","ring","pinky"};
        const char*p=NULL; int fi=-1, side=-1, joint=0;
        for(int f=0;f<5&&fi<0;f++){ const char*q=strstr(n,FN[f]); if(q){ p=q; fi=f; } }
        if(fi==3&&(strstr(n,"earring")||strstr(n,"string")||strstr(n,"spring"))) fi=-1;   /* ⛔ Anima's earring_01_l is not a ring finger */
        if(fi<0){ const char*q=strstr(n,"mid"); if(q&&(q[3]>='0'&&q[3]<='9')){ p=q; fi=2; } }   /* Daz: l_mid1 */
        if(fi>=0&&p){
            const char*after=p+strlen(fi==2&&!strncmp(p,"mid",3)&&strncmp(p,"middle",6)?"mid":FN[fi]);
            if(!strncmp(after,"_metacarpal",11)||!strncmp(after,"metacarpal",10)) return -1;
            /* the joint number: right after the finger name, or after one '_' (UE writes _01) */
            const char*d=after; if(*d=='_') d++;
            if(*d>='0'&&*d<='9'){ joint=*d-'0'; if(d[1]>='0'&&d[1]<='9'){ joint=joint*10+(d[1]-'0'); d++; } d++;
                /* the side: UE trails _l/_r; mixamo/CC/Daz lead with left/right or l_/r_ */
                if(!strncmp(d,"_l",2)||!strncmp(d,"l",1)) side=0; else if(!strncmp(d,"_r",2)||!strncmp(d,"r",1)) side=1;
                if(side<0){ if(strstr(n,"left")||!strncmp(n,"l_",2)||strstr(n,"_l_")) side=0;
                            else if(strstr(n,"right")||!strncmp(n,"r_",2)||strstr(n,"_r_")) side=1; }
                if(side>=0&&joint>=1&&joint<=3) return 16+side*15+fi*3+(joint-1);
                return -1; }   /* a tip, a metacarpal, or a control: not a joint */
            return -1; } } }
    while(len>0&&((n[len-1]>='0'&&n[len-1]<='9')||n[len-1]=='_'||n[len-1]=='.')) n[--len]=0;
    static const struct{const char*k;int c;} EX[]={   /* exact: UE / CC / alt-rig families */
        {"pelvis",0},{"spine_",1},{"neck_",4},{"clavicle_l",6},{"upperarm_l",7},{"lowerarm_l",8},
        {"clavicle_r",9},{"upperarm_r",10},{"lowerarm_r",11},{"thigh_l",12},{"calf_l",13},{"thigh_r",14},{"calf_r",15},
        {"cc_base_hip",0},{"cc_base_waist",1},{"cc_base_spine",2},{"cc_base_necktwist",4},{"cc_base_head",5},
        {"cc_base_l_clavicle",6},{"cc_base_l_upperarm",7},{"cc_base_l_forearm",8},
        {"cc_base_r_clavicle",9},{"cc_base_r_upperarm",10},{"cc_base_r_forearm",11},
        {"cc_base_l_thigh",12},{"cc_base_l_calf",13},{"cc_base_r_thigh",14},{"cc_base_r_calf",15},
        {"left_arm",7},{"left_elbow",8},{"right_arm",10},{"right_elbow",11},
        {"left_leg",12},{"left_knee",13},{"right_leg",14},{"right_knee",15},{"chest",2},
        {"shoulderl",6},{"upper_arml",7},{"forearml",8},{"shoulderr",9},{"upper_armr",10},{"forearmr",11},
        /* ⭐ DAZ GENESIS 9 puts the side on the LEFT of the name, where every other family
         * this table knows puts it on the right. Without these, every limb on a 405-bone
         * Daz rig resolved to -1 and she could not receive a clip; only her spine and
         * neck came through, by accident, via the digit-stripping mixamo suffixes. */
        {"hip",0},{"l_collar",6},{"l_upperarm",7},{"l_forearm",8},
        {"r_collar",9},{"r_upperarm",10},{"r_forearm",11},
        {"l_thigh",12},{"l_shin",13},{"r_thigh",14},{"r_shin",15},
        {"thighl",12},{"shinl",13},{"thighr",14},{"shinr",15},   /* snake_case, side trailing (the DontGetDropped alien): */ {"shoulder_l",6},{"upper_arm_l",7},{"forearm_l",8},{"shoulder_r",9},{"upper_arm_r",10},{"forearm_r",11},{"shin_l",13},{"shin_r",15},{NULL,0}};
    for(int i=0;EX[i].k;i++) if(!strcmp(n,EX[i].k)) return EX[i].c;
    /* UE spine_01/02/03 lost their digits above -- disambiguate by original */
    if(!strncmp(n,"spine_",6)) return 1;
    static const struct{const char*k;int c;} SUF[]={   /* mixamo-family suffixes, longest first */
        {"leftshoulder",6},{"rightshoulder",9},{"leftforearm",8},{"rightforearm",11},
        {"leftupleg",12},{"rightupleg",14},{"leftarm",7},{"rightarm",10},
        {"leftleg",13},{"rightleg",15},{"spine2",3},{"spine1",2},{"spine",1},
        {"neck",4},{"head",5},{"hips",0},{NULL,0}};
    for(int i=0;SUF[i].k;i++){ size_t sl=strlen(SUF[i].k);
        if((size_t)len>=sl&&!strcmp(n+len-sl,SUF[i].k)) return SUF[i].c; }
    return -1; }
/* one BC1 or BC3 level to RGBA, for a path that can't sample them. fmt 1 or 3 */
static unsigned char* bcDecode(const unsigned char*d,int w,int h,int fmt){
    unsigned char*out=(unsigned char*)malloc((size_t)w*h*4); if(!out) return NULL;
    if(fmt==7){   /* a PC's cook on something that can't sample BC7: the only mode the cook writes is 6 */
        static const int W4[16]={0,4,9,13,17,21,26,30,34,38,43,47,51,55,60,64};
        for(int by=0;by<(h+3)/4;by++) for(int bx=0;bx<(w+3)/4;bx++,d+=16){ unsigned long long lo,hi; memcpy(&lo,d,8); memcpy(&hi,d+8,8);
            int c0[4],c1[4]; int p0=(int)(lo>>63),p1=(int)(hi&1); for(int k=0;k<4;k++){ c0[k]=(int)((lo>>(7+k*14))&127)*2+p0; c1[k]=(int)((lo>>(14+k*14))&127)*2+p1; }
            for(int i=0;i<16;i++){ int ix=i==0?(int)((hi>>1)&7):(int)((hi>>(i*4))&15); int x=bx*4+i%4,y=by*4+i/4; if(x>=w||y>=h) continue; unsigned char*o=out+((size_t)y*w+x)*4; for(int k=0;k<4;k++) o[k]=(unsigned char)(((64-W4[ix])*c0[k]+W4[ix]*c1[k]+32)>>6); } }
        return out; }
    int bw=(w+3)/4, bh=(h+3)/4, sz=(fmt==3)?16:8;
    for(int by=0;by<bh;by++) for(int bx=0;bx<bw;bx++,d+=sz){ const unsigned char*c=d+sz-8;
        unsigned c0=(unsigned)c[0]|((unsigned)c[1]<<8), c1=(unsigned)c[2]|((unsigned)c[3]<<8);
        unsigned bits=(unsigned)c[4]|((unsigned)c[5]<<8)|((unsigned)c[6]<<16)|((unsigned)c[7]<<24);
        int p[4][4];
        for(int e=0;e<2;e++){ unsigned v=e?c1:c0; unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31;
            p[e][0]=(int)((r<<3)|(r>>2)); p[e][1]=(int)((g<<2)|(g>>4)); p[e][2]=(int)((b<<3)|(b>>2)); p[e][3]=255; }
        int four=(c0>c1)||fmt==3;
        for(int k=0;k<3;k++){
            if(four){ p[2][k]=(2*p[0][k]+p[1][k])/3; p[3][k]=(p[0][k]+2*p[1][k])/3; }
            else { p[2][k]=(p[0][k]+p[1][k])/2; p[3][k]=0; } }
        p[2][3]=255; p[3][3]=four?255:0;
        int al[8]; unsigned long long ab=0;
        if(fmt==3){ int a0=d[0],a1=d[1]; al[0]=a0; al[1]=a1;
            if(a0>a1){ for(int k=1;k<7;k++) al[k+1]=((7-k)*a0+k*a1)/7; }
            else { for(int k=1;k<5;k++) al[k+1]=((5-k)*a0+k*a1)/5; al[6]=0; al[7]=255; }
            for(int k=0;k<6;k++) ab|=(unsigned long long)d[2+k]<<(8*k); }
        for(int y=0;y<4;y++) for(int x=0;x<4;x++){ int px=bx*4+x, py=by*4+y; if(px>=w||py>=h) continue;
            const int*q=p[(bits>>(2*(y*4+x)))&3]; unsigned char*o=out+((size_t)py*w+px)*4;
            o[0]=(unsigned char)q[0]; o[1]=(unsigned char)q[1]; o[2]=(unsigned char)q[2];
            o[3]=(unsigned char)((fmt==3)?al[(ab>>(3*(y*4+x)))&7]:q[3]); } }
    return out; }
static const char* packStr(void*Pv,unsigned idx){
    unsigned char*pk=((Pack*)Pv)->pk;
    unsigned offStr=((unsigned*)(pk+4))[11], nStr=*(unsigned*)(pk+offStr);
    static char buf[128]; if(idx>=nStr) return "?";
    unsigned so=*(unsigned*)(pk+offStr+4+idx*8), sl=*(unsigned*)(pk+offStr+4+idx*8+4);
    if(sl>127) sl=127;
    memcpy(buf,(char*)pk+offStr+4+nStr*8+so,sl); buf[sl]=0; return buf; }
typedef struct { int pack; int clip; unsigned phase; float x,z,rotY;
    /* SOCIAL ROAM (ported from the page's updateDancerPool): 0 mill / 1 wander / 2 meet / 3 chat */
    int state,mate,moving; float tT,stateT,tx,tz,h,spd,pgT,pgD,blk;
    /* PROCEDURAL GENOME (jacob: all of them independently dancing): base stance = a frozen frame
     * of a real dance clip (rig-correct, no T-pose zombies), beat-locked oscillators on top.
     * Every dancer rolls unique amplitudes/phases/subdivisions; moves re-roll every few bars. */
    int baseClip; unsigned baseFrame; int nextClip; unsigned nextFrame; float blend;
    float moveT; float gHip,gSpine,gHead,gKnee,gBounce,gSub,gPh,gArmA[4],gArmPh[2],gArmSub;
    float wph;   /* walk-clip phase accumulator: playback rate rides the slot's ACTUAL speed (jacob: feet were moonwalking) */
    int mode; float clipT; float altT; } Slot;   /* altT: flyer's wandering altitude target */   /* mode 1 = MOCAP DANCER: plays the (now correctly retargeted) m_* clips raw; ~30% of the floor (jacob: more variation) */
static void rollGenome(Slot*s,int nClips,unsigned(*frameOf)(int),int keepBase){
    if(!keepBase){ s->nextClip=s->baseClip; s->nextFrame=s->baseFrame; }
    /* jacob round 4 ("more variation"): ranges widened, faster move turnover -- the floor
     * should read as 98 individuals, not one genome with jitter */
    s->gHip=0.08f+frnd()*0.34f; s->gSpine=0.45f+frnd()*0.55f; s->gHead=0.025f+frnd()*0.045f;
    s->gKnee=0.04f+frnd()*0.15f; s->gBounce=frnd()<0.5f?(0.03f+frnd()*0.07f):0.f;
    s->gSub=frnd()<0.2f?0.5f:(frnd()<0.7f?1.f:2.f); s->gPh=(frnd()*2-1)*0.14f;
    for(int k=0;k<4;k++) s->gArmA[k]=0.10f+frnd()*0.55f;
    s->gArmPh[0]=frnd()*6.28f; s->gArmPh[1]=frnd()*6.28f;
    s->gArmSub=frnd()<0.3f?0.5f:1.f;
    s->moveT=(5.f+frnd()*9.f); (void)nClips; (void)frameOf; }

static void die(const char*m){ fprintf(stderr,"FATAL: %s\n",m); exit(1); }
