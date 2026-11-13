/* ================= THE SET =========================================================
 * A world made of placed props. `sets/<name>.set` is a text file:
 *
 *     prop <rpack-stem> <x> <y> <z> <yawDeg> <scale> [r g b]
 *
 * Each distinct stem is loaded once from props/v1_town_<stem>.rpack (or props/<stem>
 * .rpack) and every line becomes an instance with its own matrix. An optional r,g,b
 * replaces the pack's texture with a flat tint, which is how a kit of grey primitives
 * becomes a painted lawn.
 *
 * Deliberately dumb: no culling, no batching, no LOD. A street is a few hundred draws
 * of a few hundred triangles, which this engine does not notice, and the shape of the
 * FILE is the thing that has to be right -- it is the level format. */
#define SET_MAXP 384
#define SET_MAXI 4096
/* ⭐ A NAMED MARK IN THE WORLD -- `mark <name> x y z yaw` in a .set. It carries a
 * HEIGHT, which is the whole point: a body sent to a mark sits ON the chair rather than
 * beside it, and lies ON the bed rather than through it. It carries a YAW too, because a
 * chair faces a direction and a body in it has no choice about that. */
/* ⛔ this was 24, and the 25th mark onward was DROPPED IN SILENCE -- a skatepark's set carries 29, so
 * an earlier scene's own test marks b1/r2/r3/k2/kb never existed and a partner sent to k2 stood inside the hero. */
#define SET_MAXMARK 256
/* ⭐ CONTACT (09-11). A mark's height is where the body TOUCHES the furniture, and what
 * touches depends on the pose: a lying or standing body meets it with its lowest point
 * (the default), a SEATED body meets it with its hips -- feet dangle below the seat. An
 * optional sixth token `hips` says which; `mark bedside x y z yaw hips`. */
typedef struct { char name[32]; float x,y,z,yaw; int hips; } SetMark;
static SetMark g_setMk[SET_MAXMARK]; static int g_setNMk=0;
static SetMark* markFind(const char*nm){
    if(!nm||!nm[0]||!strcmp(nm,"off")) return NULL;
    for(int i=0;i<g_setNMk;i++) if(!strcmp(g_setMk[i].name,nm)) return &g_setMk[i];
    /* ⛔ an unknown mark was silent -- the body just kept standing wherever it was. Say it, once a name. */
    if(g_setNMk>0){ static char miss9[32][32]; static int nMiss9=0; int seen9=0;
        for(int k=0;k<nMiss9;k++) if(!strcmp(miss9[k],nm)){ seen9=1; break; }
        if(!seen9){ if(nMiss9<32) snprintf(miss9[nMiss9++],32,"%s",nm);
            printf("[mark] '%s' NOT FOUND among the set's %d marks -- the body keeps its old placement\n",nm,g_setNMk); fflush(stdout); } }
    return NULL; }

typedef struct { int pk; float m[16]; float tint[3]; int tinted; int room; int mover; int hid; float wash,fade; int pic; float sway,swayPh; } SetInst;   /* room: draws self-lit. From the timeline (step 21): wash, how much of tint goes over its own texture or colour, 0..1; fade, how far gone it is, 0 solid to 1 not there; pic, which picture of its sheet, counted from 1, and 0 is its own texture */
static float g_roomLit=1.0f;   /* ⭐ a room's photographed light, as a gain (ctl roomlit=, REVA_ROOMLIT) */
static Pack g_setPk[SET_MAXP]; static char g_setNm[SET_MAXP][64]; static int g_setNP=0; static int g_setFree[SET_MAXP],g_setNFree=0;   /* places in the table that something's given back */
static SetInst g_setI[SET_MAXI]; static int g_setNI=0;
/* ⭐ LEVEL OF DETAIL (queue step 24). A prop can have lighter copies beside it: props/<stem>~lod1.rpack, ~lod2, and ~far, which is a picture of it on two crossed
 * cards (skate_src/author/lod.py makes all three from the prop). The set draws whichever suits how big the prop is in the picture, and the machine's `lod`
 * setting says how soon it lets go: 0 soonest (a Pi), 2 as made, 4 holds the full prop twice as far. The prop itself is the top detail, so film's meshes are
 * never thinned unless a set is seen from far off. They're looked for when the prop is loaded (setPack), so nothing is read from disk in the middle of a frame, and a
 * prop with none costs three looks for a file. A SET ACTOR'S BODY has them the same way (its pack is a set pack): ~lod1 and ~lod2 on the same skeleton, drawn on the
 * actor's own pose when it's small in the picture (engine/williams.inc, actorLod). They live in props/ whatever folder the body is in: a file in packs/ is in the crowd. */
static short g_setLod[SET_MAXP][3];   /* a prop's ~lod1, ~lod2 and ~far, as places in g_setPk: -2 not looked for yet, -1 none */
static int setPack(const char*stem);
static int setLod(int pi,int level){
    if(pi<0||pi>=SET_MAXP||level<0||level>2) return -1;
    if(g_setLod[pi][level]==-2){ static const char*const tag[3]={"~lod1","~lod2","~far"}; g_setLod[pi][level]=-1;
        if(!strchr(g_setNm[pi],'~')){ char st[96],pp[256]; snprintf(st,sizeof st,"%s%s",g_setNm[pi],tag[level]); snprintf(pp,sizeof pp,"props/%s.rpack",st);
            FILE*t=fopen(pp,"rb"); if(t){ fclose(t); const int got=setPack(st); if(got>=0){ g_setLod[pi][level]=(short)got; printf("[lod] %s has a %s\n",g_setNm[pi],tag[level]+1); } } } }
    return g_setLod[pi][level]; }
/* the set's timeline (settrack.inc): `track` plays lights, beams and props against the scene's clock; `mover` is a prop it places */
static float*g_stk=NULL; static float g_stkFps=30.f,g_stkOff=0.f; static int g_stkFrames=0,g_stkStride=0,g_stkNL=0,g_stkNB=0,g_stkNM=0,g_stkLast=-1,g_nMover=0;
static void trackLoad(const char*path,float off); static void trackApply(void);
static void artLoad(const char*path,float off); static void artDrop(void); static int g_artExact;   /* (11-11) the set's drawings (setart.inc): `art <file> [seconds]` */
/* (step 21, 11-04) what the timeline also gives: a place for each scene body to look, a mover's pictures, and where every z_rm_ clip starts */
#define STK_LOOKS 8
#define STK_SHEETS 16
static float g_stkScrub=-1.f; static double g_stkPlay0=0.0;   /* (step 25) ctl `trackt=<seconds>`: the timeline held at that moment by hand, with no scene playing; `trackt=play` runs it round and round on the lab's own clock; `trackt=off` gives it back to the scene's */
static int g_stkNS=0; static float g_stkSpot[32][3];   /* (11-11) TRK5: this frame's colour on each of the set's spots, by the order of their lines */
static int g_stkNC=0,g_stkNP=0; static float g_stkCam[9],g_stkPost[12];   /* (step 25) the timeline's lens and post rows, as they stand this frame */
static int g_stkNK=0; static float g_stkLook[STK_LOOKS][4];             /* x y z and whether it's on, by scene slot */
/* (11-11) A SCENE'S BODY WASHED WITH A MOVER'S COLOUR: a set line `slotwash <slot> <mover> <amount>` (slot 0 the hero, 1.. the cast; the mover by its
 * number in the timeline). Each frame that body is drawn mixed <amount> of the way toward the colour the timeline gives that mover, so a body before a
 * card that changes colour on the beat changes with it. The music video's chorus */
#define SLOTWASH_N 8
#define SLOTWASH_MOV 64
static int g_slotWashOn=0, g_slotWashM[SLOTWASH_N]; static float g_slotWashA[SLOTWASH_N], g_stkMovCol[SLOTWASH_MOV][3];
static void slotWashFor(int slot){
    if(g_slotWashOn&&slot>=0&&slot<SLOTWASH_N&&g_slotWashA[slot]>0.f&&g_slotWashM[slot]>=0&&g_slotWashM[slot]<SLOTWASH_MOV){
        memcpy(g_bodyWash,g_stkMovCol[g_slotWashM[slot]],12); g_bodyWash[3]=g_slotWashA[slot]; }
    else g_bodyWash[3]=0.f; }
static struct { int pk,n; unsigned*tex; } g_stkSheet[STK_SHEETS]; static int g_stkNSheet=0;   /* a mover's sheet of pictures: its texture cut into cells, each a texture of its own */
static float g_clipStartS=0.f;                                          /* the set's `clipstart <seconds>`: a take that starts part of the way into its performances */
static int stkSheet(int pk,int cols,int rows); static void stkSheetsDrop(void);
/* how one instance is drawn: its colour, how far it's faded and which picture it shows. Off again after, so nothing else is drawn that way */
static void setInstLook(const SetInst*I){
    if(I->tinted){ g_vkUse=0.f; memcpy(g_vkTint,I->tint,12); }
    else if(I->wash>0.f){ g_vkUse=1.f-I->wash; memcpy(g_vkTint,I->tint,12); }
    else { g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; }
    g_instFade=I->fade; g_instTex=0;
    if(I->pic>0) for(int s=0;s<g_stkNSheet;s++) if(g_stkSheet[s].pk==I->pk){ g_instTex=g_stkSheet[s].tex[(I->pic-1)%g_stkSheet[s].n]; break; } }
static void setInstLookOff(void){ g_instFade=0.f; g_instTex=0; g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; }   /* (11-11) its colour too: a held prop drawn after the set was drawn in the last washed mover's colour (the music video's chorus jar went the columns' colours on the beat) */

/* ================= RUNTIME COLLISION ==========================================
 * ⭐ The set already knows the shape of the world: every instance has a matrix and every
 * pack was measured for its extent at load. A collider set is those two multiplied, built
 * once at parse. No new data, no authoring, no pipeline.
 *
 * ⛔ CIRCLES, IN XZ ONLY. A wall is not a circle. This will not hold a body against a flat
 * surface at a grazing angle -- that wants a swept capsule against real geometry. What it
 * DOES stop is the thing that was on screen: walking through a parked van.
 *
 * ⛔ AND MOST OF A SET IS NOT SOLID. A room built out of colliding walls is a room nobody
 * can stand in, and a road that pushes you off it is worse than none. */
#define SET_MAXCOL 4096
typedef struct { float x,z,r; } SetCol;
static SetCol g_col[SET_MAXCOL]; static int g_nCol=0;
static int g_colOff=0;                     /* REVA_NOCOLLIDE=1 */

/* Solid by NAME, from the same vocabulary the texture audit uses. Everything a body can
 * walk on, or that is a surface of the room they are standing in, is NOT solid. */
static int propSolid(const char*stem){
    static const char*OPEN[]={"ground","road","sidewalk","grass","dirt","floor","roof",
        "carpet","parcel","wall","window","walldoor","wallgarage","wallattic","cornice",
        "chimney","powercable","mountains","sewercover","doorframe","drainpipe",
        "newspaper","grassvertical","grasscutter",NULL};
    for(int i=0;OPEN[i];i++) if(strstr(stem,OPEN[i])) return 0;
    return 1; }

static void colAdd(float x,float z,float r){
    if(g_nCol>=SET_MAXCOL||!(r>0.02f)) return;
    g_col[g_nCol].x=x; g_col[g_nCol].z=z; g_col[g_nCol].r=r; g_nCol++; }

/* Push a mover of radius `mr` out of anything it is inside. Returns how far it moved,
 * which is what a caller uses to decide whether it is stuck. */
static int colSdfResolve(float*x,float*z,float mr,float*moved);   /* ⭐ the real geometry: defined after the audit grid */
static int g_spOn=0;   /* this set said `climb`: engine/spans.inc is its collision */
static int g_objLive=0,g_objNSolid=0;   /* engine/objects.inc */
static float objCollide(float*x,float*z,float mr);
static void objClear(void);
static void rideClear(void);   /* engine/ride.inc */
static void liqClear(void);    /* engine/liquid.inc */
static float colResolve(float*x,float*z,float mr){
    if(g_colOff||g_spOn) return 0.f;
    const float obj9=g_objNSolid?objCollide(x,z,mr):0.f;   /* what a game has put in the world */
    { float mv=0.f; if(colSdfResolve(x,z,mr,&mv)) return mv+obj9; }   /* ⭐ a set with a grid collides against its TRIANGLES */
    if(!g_nCol) return obj9;
    float ox=*x, oz=*z;
    for(int pass=0;pass<2;pass++)
      for(int i=0;i<g_nCol;i++){
        float dx=*x-g_col[i].x, dz=*z-g_col[i].z;
        float need=g_col[i].r+mr;
        float d2=dx*dx+dz*dz;
        if(d2>=need*need||d2<1e-9f) continue;
        float d=sqrtf(d2);
        float push=(need-d)/d;
        *x+=dx*push; *z+=dz*push; }
    float mx=*x-ox, mz=*z-oz;
    return sqrtf(mx*mx+mz*mz)+obj9; }
static char g_setCur[64]={0};
/* (11-12, the Godot proof of concept's step 14) NO SET AT ALL, as the default room asks it: its starfield, dance floor, lamps, lasers and dark
 * clear colour are drawn where nothing else is. For a game a loaded set is a set even with no props of its own (Create a Map's: the whole world
 * is the game's objects), and the default room isn't drawn in it. The lab and film keep the old rule: a set of only actors is still the default room */
#define SET_NONE (!g_setNI&&!(GAME_ON&&g_setCur[0]))
/* ---- ACTORS: a body, a clip, a place in the world, and somewhere to be going ------ */
#define SET_MAXA 128
/* Z7a: an actor's timeline. Eight of each is a shot, not a screenplay -- a body that
 * needs more than eight beats or eight marks in one take wants to be two actors. */
#define SET_MAXCUE 8
#define SET_MAXWP  8
typedef struct { float t; char clip[32]; } SetCue;
typedef struct { float t, x, y, z; } SetWp;
/* ⭐ (11-12) A SET ACTOR PUT SOMEWHERE AT A TIME, with no walk between: `place <t> <x> <y> <z> [yaw]` after her line. The last place whose
 * time has passed is where she stands, her line's own place before the first. A path walks her there (it turns her the way she goes and paces
 * her clip to the speed); a place doesn't: the music video's copies hop one column on every beat, a cut on the beat, as the original video's strip of
 * faces slides. On the scene's clock when her clip is (`sceneclock`), else the set's. */
typedef struct { float t, x, y, z, yaw; int hasYaw; } SetPlace;
#define SET_MAXPLACE 96
/* where the leashed actor's collar is this frame, published by actorsDraw and consumed
 * by the draw that knows where the partner is -- the two live in different systems. */
#define LEAD_MAX 4
static float g_leadA[LEAD_MAX][3], g_leadB[LEAD_MAX][3]; static int g_leadN=0;
static float g_setT=0.f;      /* THE SET CLOCK. g_scnT only exists while dialogue plays,
                               * and half these shots have none over them. */
static float gaitRate(Pack*P,int clip,float spd);   /* defined far below; a path needs the stride */
/* ⛔ A SHOT CLOCK STARTS WHEN THE CAMERA ROLLS. g_setT used to run from the moment the set
 * loaded, and a capture does not begin until REVA_SHOT's frame -- at 30fps that is
 * FOURTEEN SECONDS of the shot gone before the first frame, so a `cue 12.4` had already
 * fired off-camera. When a capture is armed the clock waits for it. */
static int g_capBegun=0;
typedef struct {
    int   init;
    float u;          /* lagged contact/passing measure: 0 = passing, 1 = contact */
    float um;         /* ...and its slow mean, so the bounce cannot NET LIFT her */
    float act;        /* how hard the feet are actually swapping; 0 = standing still */
    float prevSep;
    float lq[3][4];   /* the smoothed local quats the dragged parts chase */
} WmsState;

static float g_wms=0.85f;   /* ctl wms= -- Ken Harris' note was to understate it */
static int   g_wmsOff=0;    /* REVA_NOWMS=1 */

typedef struct { int pk, clip, board;
                 float x,y,z, yaw, sc, rate, spd, len;
                 double phase; float travel;
                 /* ---- SKATER ---- */
                 int skate;                    /* 1: posed procedurally, not by a clip */
                 const PoseKey *act; int actI, actN; float actT;   /* a trick, playing */
                 float trickIn;                /* seconds until the next one */
                 float deckRoll, deckYaw;      /* the deck's own spin under the rider */
                 float air;                    /* how far off the ground, mid-trick */
                 float footY[2], footSpread;   /* MEASURED after FK, for the board and the log */
                 Spring ch[12]; int chInit;    /* one spring per pose channel: the lead-and-lag */
                 float kneeZ;                  /* ankle minus knee along forward: NEGATIVE is a real knee */
                 Pack *dnP; int dnC;           /* Z4: a clip borrowed from a bank, retargeted onto this body */
                 int leash;                    /* >=0: lead from THIS actor's neck to that actor's hand */
                 WmsState wms;                 /* the Williams layer's memory for this body */
                 float look[3]; int hasLook;   /* ⭐ a world point this body turns its head toward */
                 float neckW[3], handW[3];     /* measured after FK, in WORLD space */
                 /* ---- Z7a THE TIMELINE ---- */
                 SetCue cue[SET_MAXCUE]; int nCue, curCue;
                 SetWp  wp[SET_MAXWP];   int nWp;
                 float cx,cy,cz,cyaw,cspd;     /* where the path has her THIS frame */
                 int   onPath;
                 /* ---- A MIND (09-11) ---- */
                 int   mind;                   /* MIND_NONE/WANDER/REACT/HUNT */
                 int   mindOn;                 /* the mind owns cx/cz/cyaw this frame (a timed path wins while it runs) */
                 int   zombie;                 /* cast as one, or turned */
                 float mindR;                  /* wander radius / react radius / hunt radius */
                 float home[2], tmr, fleeT, replanT;
                 char  clip0[64];              /* what she was cast to play when standing */
                 char  over[32]; float overW;  /* ⭐ an upper-body layer (09-11): `over <clip> [w]` */
                 int   moving;                 /* the locomotion clip is up */
                 float navX[128], navZ[128]; int navN, navI;   /* the path the grid gave her */
                 int   threat;                 /* who she is fleeing / hunting (-1 none, -2 the hero) */
                 unsigned long long worn;      /* ⭐ (10-23) the pieces she wears (`outfit=`), put on as she is drawn */
                 float cueS,cueF; int cued;    /* (step 21) where her clip starts: `clip>12.5s` or `clip>0.33` on her line, -1 for neither; cued once it's been done */
                 float eye[4];                 /* (step 23) `eyes=r,g,b[,amount]` on her line: her eyes glow that colour */
                 int washM; float washA; int washHair;   /* washHair (11-13): `actorwash ... hair`, her hair washed too */
                        /* (11-11) `actorwash <actor> <mover> <amount>`: she's drawn washed toward that mover's colour this frame, as `slotwash` washes a scene's body */
                 int sceneClk; float sceneOff; /* (11-11) `sceneclock [seconds]` after her line: her clip's frame is the SCENE's clock (plus that offset), held on its first frame until the scene starts */
                 char prop[PROP_SPEC], prop2[PROP_SPEC];   /* (11-11) `prop=`/`prop2=` on her line: what she holds, a scene prop's spec (scene.inc, actorHeldSync) */
                 SetPlace pl[SET_MAXPLACE]; int nPl, curPl; float x0, y0, z0, yaw0;   /* (11-12) her `place` lines, the one she's at, and her line's own place */
                 } SetActor;
static SetActor g_setA[SET_MAXA]; static int g_setNA=0;

/* ================= THE NAV GRID AND THE ACTOR'S MIND (09-11) =========================
 * engine_capabilities.md F: "Set actors have no behaviour" -- an actor played one clip on
 * an authored path, could not react, look at, avoid, or change its mind. The street's people (09-03)
 * have to respond to events, and in time turn into zombies.
 * The audit ranked actor behaviour + navigation seventh.
 *   THE GRID: at set load, one cell per unit over the colliders' extent; a cell is blocked
 *   when any collider circle (plus a body's own radius) covers its centre. A* over it,
 *   8-connected, so a path goes round a house instead of through one.
 *   THE MIND, a line after an actor:   mind wander <r> | mind react <r> | mind hunt <r>
 *     wander: every few seconds pick a walkable spot within r of home and walk there.
 *     react : wander, and when a zombie comes within r, RUN for the walkable spot that
 *             puts the most distance on it, looking at it; settle after five seconds.
 *     hunt  : a zombie. Shamble toward the nearest non-zombie (or the hero) within r and
 *             re-plan every 0.7 s; on contact the victim TURNS -- it becomes a hunter, its
 *             clip becomes the zombie bank's -- and a hero is bitten (wpnHurtHero).
 *   A timed `path` still wins while it runs; the mind takes over when it ends, so a
 *   scene's blocking is untouched. Actors also keep apart from each other. REVA_NAVDBG=1
 *   prints every decision; `[mind] turned` is the one the film is about. */
enum { MIND_NONE=0, MIND_WANDER, MIND_REACT, MIND_HUNT };
static void actorClip(SetActor*A,Pack*AP,const char*nm,int idx,int moving);   /* defined below */
#define NAV_MAX 512
static unsigned char*g_nav=NULL; static int g_navW=0,g_navH=0; static float g_navX0=0,g_navZ0=0; static int g_navBlocked=0;
static int g_navDbg=-1;
static int navCell(float x,float z,int*cx,int*cz){ if(!g_nav) return 0;
    int a=(int)floorf(x-g_navX0), b=(int)floorf(z-g_navZ0);
    if(a<0||b<0||a>=g_navW||b>=g_navH) return 0;
    *cx=a; *cz=b; return 1; }
static int navFree(int a,int b){ return a>=0&&b>=0&&a<g_navW&&b<g_navH&&!g_nav[b*g_navW+a]; }
static void navBuild(void){
    if(g_nav){ free(g_nav); g_nav=NULL; } g_navW=g_navH=0; g_navBlocked=0;
    if(!g_nCol) return;
    float x0=1e9f,x1=-1e9f,z0=1e9f,z1=-1e9f;
    for(int i=0;i<g_nCol;i++){ SetCol*C=&g_col[i]; if(C->x-C->r<x0)x0=C->x-C->r; if(C->x+C->r>x1)x1=C->x+C->r;
                               if(C->z-C->r<z0)z0=C->z-C->r;
                               if(C->z+C->r>z1)z1=C->z+C->r; }
    x0-=24.f; z0-=24.f; x1+=24.f; z1+=24.f;
    /* (step 28) the extent is a size before it's a count of cells, and a collider's reach is walked only where it crosses the grid: one of a million units' radius used to be walked cell by cell, off the grid, for ever */
    if(!(x1-x0>0.f&&x1-x0<1e7f&&z1-z0>0.f&&z1-z0<1e7f)) return;
    int w=(int)ceilf(x1-x0), h=(int)ceilf(z1-z0);
    if(w>NAV_MAX){ w=NAV_MAX; } if(h>NAV_MAX){ h=NAV_MAX; }
    if(w<1) w=1;
    if(h<1) h=1;
    g_nav=(unsigned char*)calloc((size_t)w*h,1); if(!g_nav) return;
    g_navW=w; g_navH=h; g_navX0=x0; g_navZ0=z0;
    const float body=0.45f;
    for(int i=0;i<g_nCol;i++){ SetCol*C=&g_col[i]; float rr=C->r+body; if(!(rr>=0.f&&rr<1e7f)||!(C->x-C->x==0.f)||!(C->z-C->z==0.f)) continue;
        const float fa0=floorf(C->x-rr-x0), fa1=ceilf(C->x+rr-x0), fb0=floorf(C->z-rr-z0), fb1=ceilf(C->z+rr-z0);
        if(fa1<0.f||fb1<0.f||fa0>(float)(w-1)||fb0>(float)(h-1)) continue;
        const int a0=fa0<0.f?0:(int)fa0, a1=fa1>(float)(w-1)?w-1:(int)fa1, b0=fb0<0.f?0:(int)fb0, b1=fb1>(float)(h-1)?h-1:(int)fb1;
        for(int b=b0;b<=b1;b++) for(int a=a0;a<=a1;a++){ if(a<0||b<0||a>=w||b>=h) continue;
            float cx=x0+a+0.5f, cz=z0+b+0.5f, dx=cx-C->x, dz=cz-C->z;
            if(dx*dx+dz*dz<rr*rr&&!g_nav[b*w+a]){ g_nav[b*w+a]=1; g_navBlocked++; } } }
    printf("[nav] grid %dx%d cells from (%.0f,%.0f), %d blocked (%.0f%%)\n",w,h,x0,z0,g_navBlocked,100.f*g_navBlocked/(float)(w*h)); fflush(stdout); }
/* A*, 8-connected, binary heap; writes up to NAV_PATH cells (centres) into out. -1 = no route. */
static int navPath(float sx,float sz,float tx,float tz,float*ox,float*oz,int max){
    int a0,b0,a1,b1; if(!navCell(sx,sz,&a0,&b0)||!navCell(tx,tz,&a1,&b1)) return -1;
    /* a target inside a wall: walk to the nearest free cell round it */
    if(!navFree(a1,b1)){ int best=-1,bd=1<<30; for(int db=-3;db<=3;db++) for(int da=-3;da<=3;da++) if(navFree(a1+da,b1+db)){ int d=da*da+db*db; if(d<bd){ bd=d; best=(b1+db)*g_navW+(a1+da); } }
        if(best<0) return -1;
        a1=best%g_navW; b1=best/g_navW; }
    if(!navFree(a0,b0)){ int best=-1,bd=1<<30; for(int db=-3;db<=3;db++) for(int da=-3;da<=3;da++) if(navFree(a0+da,b0+db)){ int d=da*da+db*db; if(d<bd){ bd=d; best=(b0+db)*g_navW+(a0+da); } }
        if(best<0) return -1;
        a0=best%g_navW; b0=best/g_navW; }
    int N=g_navW*g_navH;
    static float*gs=NULL; static int*par=NULL; static unsigned char*closed=NULL; static int*heap=NULL; static float*hf=NULL; static int cap=0;
    if(cap<N){ cap=N; gs=(float*)realloc(gs,(size_t)N*4); par=(int*)realloc(par,(size_t)N*4); closed=(unsigned char*)realloc(closed,(size_t)N); heap=(int*)realloc(heap,(size_t)N*4); hf=(float*)realloc(hf,(size_t)N*4); }
    for(int i=0;i<N;i++){ gs[i]=1e30f; par[i]=-1; closed[i]=0; }
    int hn=0; int s=b0*g_navW+a0, t=b1*g_navW+a1;
    #define HPUSH(n,f) do{ int k=hn++; heap[k]=(n); hf[k]=(f); while(k>0){ int p=(k-1)/2; if(hf[p]<=hf[k]) break; int tn=heap[p]; float tf=hf[p]; heap[p]=heap[k]; hf[p]=hf[k]; heap[k]=tn; hf[k]=tf; k=p; } }while(0)
    gs[s]=0.f; HPUSH(s,0.f);
    int found=0, expanded=0;
    while(hn>0){ int n=heap[0]; hn--; if(hn>0){ heap[0]=heap[hn]; hf[0]=hf[hn]; int k=0; for(;;){ int l=2*k+1,r=l+1,m=k; if(l<hn&&hf[l]<hf[m]) m=l; if(r<hn&&hf[r]<hf[m]) m=r; if(m==k) break; int tn=heap[m]; float tf=hf[m]; heap[m]=heap[k]; hf[m]=hf[k]; heap[k]=tn; hf[k]=tf; k=m; } }
        if(closed[n]) continue;
        closed[n]=1; if(n==t){ found=1; break; }
        if(++expanded>60000) break;
        int a=n%g_navW, b=n/g_navW;
        for(int db=-1;db<=1;db++) for(int da=-1;da<=1;da++){ if(!da&&!db) continue;
            int na=a+da, nb=b+db; if(!navFree(na,nb)) continue;
            if(da&&db&&(!navFree(a+da,b)||!navFree(a,b+db))) continue;   /* no corner cutting */
            int m=nb*g_navW+na; if(closed[m]) continue;
            float ng=gs[n]+((da&&db)?1.4142f:1.f);
            if(ng<gs[m]){ gs[m]=ng; par[m]=n; float ha=(float)(na-a1), hb=(float)(nb-b1); HPUSH(m,ng+sqrtf(ha*ha+hb*hb)); } } }
    #undef HPUSH
    if(!found) return -1;
    int len=0; for(int n=t;n>=0;n=par[n]) len++;
    /* par walks target -> start; lay it down start -> target, and keep the FIRST max cells
     * (the ones nearest the start) when a route is longer than the caller's buffer */
    static int*tmp=NULL; static int tcap=0; if(tcap<len){ tcap=len; tmp=(int*)realloc(tmp,(size_t)len*4); }
    { int k=len-1; for(int n=t;n>=0;n=par[n]) tmp[k--]=n; }
    int cnt=len<max?len:max;
    for(int i=0;i<cnt;i++){ int n=tmp[i]; ox[i]=g_navX0+(float)(n%g_navW)+0.5f; oz[i]=g_navZ0+(float)(n/g_navW)+0.5f; }
    return cnt; }
static float mindRnd(void){ static unsigned s=0x9E3779B9u; s=s*1664525u+1013904223u; return (float)((s>>8)&0xFFFFFF)/16777216.f; }
static int mindGo(SetActor*A,float tx,float tz){
    float sx=A->mindOn?A->cx:A->x, sz=A->mindOn?A->cz:A->z;
    int n=navPath(sx,sz,tx,tz,A->navX,A->navZ,128);
    if(n<=0){ A->navN=0; return 0; }
    A->navN=n; A->navI=0; if(!A->mindOn){ A->mindOn=1; A->cx=A->x; A->cz=A->z; A->cyaw=A->yaw; }
    return n; }
static void mindClip(SetActor*A,Pack*P,int idx,int moving,int run){
    if(A->moving==(moving?(run?2:1):0)) return;
    A->moving=moving?(run?2:1):0;
    if(A->zombie) actorClip(A,P,"zombie",idx,moving);
    else actorClip(A,P,moving?(run?"w_run":"w_walk"):A->clip0,idx,moving);
    A->phase=0.0; }
static void wpnHurtHero(float dmg,const char*how);
static void mindTick(float dt){
    if(g_navDbg<0) g_navDbg=getenv("REVA_NAVDBG")?1:0;
    if(!g_nav) return;
    for(int i=0;i<g_setNA;i++){ SetActor*A=&g_setA[i]; if(A->mind==MIND_NONE||A->skate) continue;
        Pack*P=&g_setPk[A->pk];
        /* a timed path still owns her until it has run out */
        if(A->nWp&&g_setT<A->wp[A->nWp-1].t) continue;
        if(A->nWp){ A->nWp=0; A->x=A->cx; A->z=A->cz; A->yaw=A->cyaw; A->home[0]=A->x; A->home[1]=A->z; }
        float px=A->mindOn?A->cx:A->x, pz=A->mindOn?A->cz:A->z;
        A->tmr-=dt; A->fleeT-=dt; A->replanT-=dt;
        /* ---- what is near ---- */
        int nearest=-1; float nd=1e9f; int heroNear=0; float hd=1e9f;
        for(int j=0;j<g_setNA;j++){ if(j==i) continue; SetActor*B=&g_setA[j];
            if(A->mind==MIND_HUNT ? B->zombie : !B->zombie) continue;
            float bx=B->mindOn||B->nWp?B->cx:B->x, bz=B->mindOn||B->nWp?B->cz:B->z;
            float d=hypotf(bx-px,bz-pz); if(d<nd){ nd=d; nearest=j; } }
        if(A->mind==MIND_HUNT&&g_setNI){ hd=hypotf(g_rvx-px,g_rvz-pz); heroNear=(hd<A->mindR); }
        /* ---- decide ---- */
        if(A->mind==MIND_HUNT){
            int tgt=-1; float td=1e9f;
            if(nearest>=0&&nd<A->mindR){ tgt=nearest; td=nd; }
            if(heroNear&&hd<td){ tgt=-2; td=hd; }
            if(tgt!=-1){ float tx,tz; if(tgt==-2){ tx=g_rvx; tz=g_rvz; } else { SetActor*B=&g_setA[tgt]; tx=B->mindOn||B->nWp?B->cx:B->x; tz=B->mindOn||B->nWp?B->cz:B->z; }
                if(td<0.95f){   /* ⭐ CONTACT: the turn */
                    if(tgt==-2){ wpnHurtHero(20.f,"BITTEN"); A->replanT=1.2f; }
                    else { SetActor*B=&g_setA[tgt]; if(!B->zombie){ B->zombie=1; B->mind=MIND_HUNT; B->mindR=A->mindR; B->navN=0; B->fleeT=0.f;
                               B->moving=-1; mindClip(B,&g_setPk[B->pk],tgt,0,0);
                               printf("[mind] t=%.1f actor %d TURNED by %d at (%.1f,%.1f)\n",g_setT,tgt,i,tx,tz); fflush(stdout); } }
                    A->navN=0; }
                else if(A->replanT<=0.f||A->navN==0){ A->replanT=0.7f; if(A->threat!=tgt&&g_navDbg) printf("[mind] t=%.1f zombie %d hunts %s%d\n",g_setT,i,tgt==-2?"the hero ":"actor ",tgt==-2?0:tgt);
                    A->threat=tgt; mindGo(A,tx,tz); } }
            else if(A->navN==0&&A->tmr<=0.f){ A->threat=-1; A->tmr=4.f+mindRnd()*6.f;   /* nobody near: drift */
                float ang=mindRnd()*6.2832f, r=2.f+mindRnd()*6.f; mindGo(A,px+sinf(ang)*r,pz+cosf(ang)*r); } }
        else {
            if(A->mind==MIND_REACT&&nearest>=0&&nd<A->mindR){
                SetActor*B=&g_setA[nearest]; float bx=B->mindOn||B->nWp?B->cx:B->x, bz=B->mindOn||B->nWp?B->cz:B->z;
                A->look[0]=bx; A->look[1]=1.6f; A->look[2]=bz; A->hasLook=1;
                if(A->fleeT<=0.f||A->threat!=nearest||(A->navN==0&&A->replanT<=0.f)){
                    A->threat=nearest; A->fleeT=5.f; A->replanT=1.0f;
                    /* the walkable spot round the ring that puts the most distance on it */
                    float bestX=px,bestZ=pz,bestD=-1.f; float ax=px-bx, az=pz-bz, al=hypotf(ax,az); if(al<1e-3f){ ax=1; az=0; al=1; }
                    for(int k=0;k<12;k++){ float ang=k*0.5236f, rr=10.f+4.f*mindRnd(); float tx=px+sinf(ang)*rr, tz=pz+cosf(ang)*rr; int ca,cb;
                        if(!navCell(tx,tz,&ca,&cb)||!navFree(ca,cb)) continue;
                        float d=hypotf(tx-bx,tz-bz)+(ax*(tx-px)+az*(tz-pz))/al*0.5f;   /* far from it, and away from it */
                        if(d>bestD){ bestD=d; bestX=tx; bestZ=tz; } }
                    if(bestD>0.f&&mindGo(A,bestX,bestZ)&&g_navDbg) printf("[mind] t=%.1f actor %d FLEES zombie %d (%.1f away) -> (%.0f,%.0f), %d cells\n",g_setT,i,nearest,nd,bestX,bestZ,A->navN); } }
            else if(A->fleeT<=0.f){ if(A->hasLook==1&&A->threat>=0){ A->hasLook=0; A->threat=-1; }
                if(A->navN==0&&A->tmr<=0.f){ A->tmr=3.f+mindRnd()*7.f;
                    float ang=mindRnd()*6.2832f, r=A->mindR*(0.3f+0.7f*mindRnd());
                    if(mindGo(A,A->home[0]+sinf(ang)*r,A->home[1]+cosf(ang)*r)&&g_navDbg) printf("[mind] t=%.1f actor %d wanders -> (%.0f,%.0f), %d cells\n",g_setT,i,A->navX[A->navN-1],A->navZ[A->navN-1],A->navN); } } }
        /* ---- move along the path ---- */
        int run=(A->mind!=MIND_HUNT&&A->fleeT>0.f);
        float spd=A->zombie?1.1f:(run?4.2f:1.6f);
        if(A->navN>0){
            float tx=A->navX[A->navI], tz=A->navZ[A->navI]; float dx=tx-px, dz=tz-pz, d=hypotf(dx,dz);
            float step=spd*dt;
            if(d<=step||d<0.15f){ px=tx; pz=tz; A->navI++; if(A->navI>=A->navN){ A->navN=0; } }
            else { px+=dx/d*step; pz+=dz/d*step; }
            float want=atan2f(dx,dz)*57.2957795f; float dy=fmodf(want-A->cyaw+540.f,360.f)-180.f; A->cyaw+=dy*fminf(1.f,dt*6.f);
            A->cspd=spd; mindClip(A,P,i,1,run); }
        else { A->cspd=0.f; mindClip(A,P,i,0,0); }
        /* keep apart from the other bodies; then the world's colliders */
        for(int j=0;j<g_setNA;j++){ if(j==i) continue; SetActor*B=&g_setA[j]; float bx=B->mindOn||B->nWp?B->cx:B->x, bz=B->mindOn||B->nWp?B->cz:B->z;
            float dx=px-bx, dz=pz-bz, d2=dx*dx+dz*dz; if(d2<0.81f&&d2>1e-6f){ float d=sqrtf(d2); px+=dx/d*(0.9f-d)*0.5f; pz+=dz/d*(0.9f-d)*0.5f; } }
        if(A->mindOn){ colResolve(&px,&pz,0.42f); A->cx=px; A->cz=pz; A->cy=A->y; }
    } }
/* ⭐ ctl `actors=0` -- draw the WORLD without its cast.
 * A set's people are part of the set, which is right for a scene and wrong for an
 * establisher: the film's first image is a suburban street at 4:10 in the morning,
 * and `block` casts SIX SKATEBOARDERS down it. They cannot be dropped by editing the
 * shot, because the shot gets them through `include block` -- the houses and the cast
 * arrive together, and duplicating the props into a second file to lose six actors is
 * how a set and its copy drift apart.
 * Also the control an empty-world bench wanted: shoot the same street with and
 * without its people and the difference IS the people. */
static int g_setActorsOn=1;
/* A PLACED LENS: eye, aim, and a focal length. -1 fov = leave the projection alone. */
static float g_camPos[3]={0,0,0}, g_camAim[3]={0,0,0}, g_camFov=-1.f;
/* \u2b50 A MOVING LENS. ctl camto=<B> camsecs=<T> flies the placed lens from where it
 * stands to B over T seconds. One mechanism covers a descent, a push-in, a re-frame and
 * a zoom, because eye, aim and fov all interpolate -- which is exactly what an aerial
 * settling onto a window is. The film had NO camera moves at all before this. */
static float g_camA[3], g_camAimA[3], g_camFovA=-1.f;   /* snapshot at move start */
static float g_camB[3], g_camAimB[3], g_camFovB=-1.f;   /* the destination */
static float g_camMoveDur=0.f, g_camMoveT=0.f;
static int   g_camMoveOn=0, g_camEase=1;
/* ⭐ THE STAGE MARK. ctl stage=x,z,yaw stands the conversation pair at a place in the
 * WORLD instead of the lab's own spot, so a scene can be played on the street the set
 * renderer just built. Everything else follows for free: the partner is placed relative
 * to the hero's matrix, and the scene camera frames the two head positions that come out
 * of it. stage=off returns her to the lab. */
static float g_stage[3]={0,0,0}; static int g_stageOn=0;
/* how fast the stage mark is carrying the pair this frame, so the hero can put a
 * gait under it instead of gliding. The partner already had this. */
static float g_heroStageSpd=0.f;
/* Z6 THE PRACTICALS. The set's point lights, claimed by the set file:
 *     light x y z r g b [power]
 * SIX is the cap, and it is NLIGHT -- one name, because the count used to be a literal 3 in
 * four places and six C arrays that all had to agree by hand. Zeros mean "the sun and nothing
 * else", which is exactly right for a daylit street and honest about a dark one. */
/* ⛔ AND THE EXTRA SLOTS MUST START DARK. y=-99 with zero colour is what keeps an
 * unlit set unlit; three more slots of uninitialised memory is a room lit by the stack. */
static float g_setLp[NLIGHT*3]={0,-99,0, 0,-99,0, 0,-99,0, 0,-99,0, 0,-99,0, 0,-99,0};
static float g_setLc[NLIGHT*3]={0};
static int g_setNL=0;
/* (11-12) A SET'S LIGHTS PAST THE OLD FIVE (the Godot proof of concept's step 12: one of their levels has 19 lanterns, and 14 were dropped). The first
 * five stay in the practicals' slots above, as they always were, so a set with five or fewer is lit exactly as before; the rest go straight into
 * the frame's lights, which the renderer clusters by the hundred */
#define SETL_MORE 250
static float g_setLpX[SETL_MORE*3], g_setLcX[SETL_MORE*3], g_setLkX[SETL_MORE], g_setLrX[SETL_MORE]; static int g_setNLX=0;
/* ⭐ A LAMP HAS A REACH. Every light in the engine fell off as 1/(1+dd*0.055) -- half brightness at
 * 4.26 u for a streetlight and a night light alike -- so a lamp bright enough to find two faces on a
 * pillow lit the far wall of the bedroom to cream (ONE AM: too bright in the room as well).
 * `light x y z r g b [power] [reach]`: REACH is where that lamp is at HALF brightness, in engine units.
 * Stored as EXTRA falloff over the engine's 0.055, so a lamp nobody gave a reach to is unchanged. */
static float g_setLk[NLIGHT];
static float g_setLr[NLIGHT];   /* (11-12) a light's END: where it has faded to nothing (GfxLight.reach), the 9th number of a `light` line; 0 goes on as it always did.
                                 * The 8th, the lamp's reach, is where it's at HALF: a Godot light's range is where it ends, and written as the 8th its
                                 * lanterns still gave a fifth of their light at twice that (that level's start was lit evenly by lanterns 30 units off) */
static float lightReachK(float reach){
    if(reach<=0.f) return 0.f;
    float k=1.f/(reach*reach)-0.055f; if(k<-0.054f) k=-0.054f;
    return k; }
static float lightReachOf(float k){ return 1.f/sqrtf(0.055f+k); }
/* ⭐ ctl `lamps=<scale>` -- THE SHOT DIALS THE SET'S OWN PRACTICALS.
 * Every other lighting quantity is a ctl key -- sun=, sky=, tod=, tone=, fog=, shadow= --
 * and the practicals alone were welded into the .set file, so a set could only ever be lit
 * for the hour it was authored at. a street set was written for one scene at `tod=10.0`, where
 * its porch light (power 7.0, against the manual's "a lamp you are standing next to is about
 * 1.5") is a warm bounce nobody notices under a sky ambient of 0.42. Shot at the next scene's
 * `tod=21.5` the sky ambient is 0.038 and the SAME lamp delivers ~1.2 at nine units away --
 * THIRTY TIMES the ambient. The street came back in full daylight and sun=/tod= read as
 * having no effect at all, because next to an unscaled practical they have almost none.
 * Forking the set per scene is the wrong answer: a set is a place, and what time it is
 * belongs to the shot. 1.0 is exactly today's behaviour, so every existing take is
 * bit-identical. ⛔ It scales the SET's lamps only -- a held light already carries its own
 * power in the script (`light:<power>`), which is per-shot by construction. */
static float g_lampScale=1.f;
/* THE SUN. w is intensity and 0 means off -- the engine's dark-room rig is the default and
 * stays exactly as it was. */
/* g_sun / g_sunC / g_sky are declared up by clearOverride; seeded in main(). */
static int   g_camOn=0;

static int setPack(const char*stem){
    for(int i=0;i<g_setNP;i++) if(!strcmp(g_setNm[i],stem)) return i;
    if(g_setNP>=SET_MAXP&&!g_setNFree){ printf("[set] pack table full at %s\n",stem); return -1; }
    char pp[256]; FILE*t;
    snprintf(pp,sizeof pp,"props/v1_town_%s.rpack",stem);
    if(!(t=fopen(pp,"rb"))){ snprintf(pp,sizeof pp,"props/%s.rpack",stem);
    /* ...and a BODY is a pack like any other. Placed through the set it draws in
     * its bind pose, which is exactly what a casting sheet wants -- and it is the
     * first half of putting actors in the world. */
    if(!(t=fopen(pp,"rb"))){ snprintf(pp,sizeof pp,"packs/%s.rpack",stem);
                             if(!(t=fopen(pp,"rb"))){ printf("[set] MISSING '%s'\n",stem); return -1; } } }
    fclose(t);
    const int slot=g_setNFree?g_setFree[--g_setNFree]:g_setNP++;
    g_setPk[slot]=loadPack(pp);
    snprintf(g_setNm[slot],64,"%s",stem);
    g_setLod[slot][0]=g_setLod[slot][1]=g_setLod[slot][2]=-2;
    if(!strchr(stem,'~')) for(int l9=0;l9<3;l9++) (void)setLod(slot,l9);   /* its lighter copies, now, while it's loading */
    return slot; }

/* ⭐ ONE PLACE THAT TURNS A CLIP NAME INTO A CLIP. Used by the actor line at load AND by
 * every cue afterwards, so a name means the same thing whenever it is said: the body's own
 * clip if it has one, otherwise borrowed off a bank through the canonical transfer, and
 * `zombie` is a casting note that picks a variant by actor index. */
static Pack *g_movesPk;   /* tentative: defined with the lab's globals (P5); an actor borrows from it too */
static void actorClip(SetActor*A,Pack*AP,const char*nm,int idx,int moving){
    char acr[64]; snprintf(acr,sizeof acr,"%s",nm);
    if(!strcmp(nm,"zombie"))
        snprintf(acr,sizeof acr,moving?"z_walk_%c":"z_idle_%c",
                 (char)('a'+(idx%(moving?3:2))));
    A->dnP=NULL; A->dnC=-1; A->clip=-1;
    if(!strcmp(acr,"-")||!strcmp(acr,"skate")) return;
    for(unsigned c=0;c<AP->nClips;c++)
        if(!strcmp(packStr(AP,AP->clips[c].name),acr)){ A->clip=(int)c; return; }
    int bc=-1; Pack*bp=bankFind(acr,&bc);
    /* ⭐ ...and the AUTHORED-MOVES pack, which the lab's clip= and every hero could already borrow from
     * (P5) but a set actor could not: a crowd asked for w_cheer and 18 bodies stood in T-pose, NOT FOUND. */
    if(!bp&&g_movesPk&&g_movesPk!=AP)
        for(unsigned c=0;c<g_movesPk->nClips;c++)
            if(!strcmp(packStr(g_movesPk,g_movesPk->clips[c].name),acr)){ bp=g_movesPk; bc=(int)c; break; }
    if(bp&&bp!=AP){
        if(AP->cbOK&&AP->pbOK>=10){ A->dnP=bp; A->dnC=bc; }
        else printf("[set] '%s' is in a bank, but this rig has no canonical basis (pbOK=%d)\n",
                    acr,AP->pbOK); }
    else if(!bp) printf("[set] clip '%s' NOT FOUND (own pack, the banks and the moves pack)\n",acr); }

/* One wind, shared. Curtains luff in it and water raises waves from it -- if a set
 * declares a breeze, everything in that set has to agree about which way it blows. */
static float g_windV[3]={0.f,0.f,0.f}, g_windGust=0.f;

