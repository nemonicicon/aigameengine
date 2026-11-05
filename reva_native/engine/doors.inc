/* ================= THE MODES (engine plan, Phase 3) ==========================================
 * The engine runs a game behind engine.h, the lab, a floor of bodies, or the skate bench. The old game modes ("doors") are in legacy_doors/, in reva only.
 * A build without that folder, or with -DREVA_NODOORS, has none of them, and kit/kit_sync.py strips every #ifdef REVA_DOORS block from what it sends. Numbers stay as they were: tools name them. */
enum { MODE_DANCE=0, MODE_SKATE=1, MODE_GAME=9, MODE_ANIM=11, MODE_N=12 };
#define MODE_DEFAULT MODE_GAME
static const char*MODE_NAME[MODE_N]={ [MODE_DANCE]="DANCER SHOWCASE", [MODE_SKATE]="SKATE PARK", [MODE_GAME]="GAME", [MODE_ANIM]="ANIM LAB" };
static const char*MODE_VAR[MODE_N][6]={{0}};
#define GAME_ON (g_game&&g_mode==MODE_GAME)   /* a game behind engine.h is playing: the engine's own systems ask this, not which game */
#define DOOR_ON_WHEELS 0
#define DOOR_ON_THE_PAD 0
#define DOOR_OFF_THE_PAD 0
#define DOOR_SWINGS 0
#define DOOR_FIRES 0
#define DOOR_NO_BOLT 0
#define DOOR_PLAT 0
#define DOOR_OWNS_LENS 0
#define WORLD_OFF (GAME_ON&&g_game->flat)   /* a flat game: no floor, no sky, no bodies to speak of */
#define MODE_PLAYS (g_mode>=2)   /* past the floor and the skate bench: a game or the lab, with the game layers running */
static int g_mode=MODE_DEFAULT;
static int g_modeVariant=0;
static int g_cabAttract=0;   /* no live input for 30 s: the cab plays itself (set once a frame by the input policy) */
static struct { int on; float e[3],a[3]; float shake,shakeT,shakeLen; } g_gameLens;   /* E->lens, E->shake */
static float g_tapeMusicIn=-1.f;   /* the music clock's reading, off a tape */
static float musicClockRead(void);   /* engine/gameworld.inc */
static float g_heroFaceY=0.f; static int g_heroFaceOn=0;   /* E->heroFace: which way the hero's asked to turn, this tick */
static float g_timeScale=1.f;   /* E->timeScale */
static struct { int n; float vp[16][16],eye[16][3]; int x[16],y[16],w[16],h[16]; } g_wallV;   /* the lab's multi-camera wall, this frame */
static struct { int n; float e[4][3],a[4][3]; } g_gameViews;   /* E->views: split screen. The first view's lens is also the lens everything else goes by */
/* the Z4 bank BY NAME (cached; one NOT FOUND line per name). The cab's dead walk z_walk_a/b/c (z_run past 3 u/s) and
 * stand in z_idle_a/b -- the clips the film's `zombie` casting note plays, through the same canonical transfer. */
static int zombClip(const char*nm){
    static char seen[8][16]; static int idx[8], n=0;
    if(!g_zombP) return -1;
    for(int i=0;i<n;i++) if(!strcmp(seen[i],nm)) return idx[i];
    int f=-1; for(unsigned c=0;c<g_zombP->nClips;c++) if(!strcmp(packStr(g_zombP,g_zombP->clips[c].name),nm)){ f=(int)c; break; }
    if(n<8){ snprintf(seen[n],16,"%s",nm); idx[n]=f; n++; }
    if(f<0) printf("[zamn] zombie clip '%s' NOT FOUND in the Z4 bank (packs/v1_zmoves.rpack)\n",nm);
    return f; }
static int zombLoco(int si,float spd,Pack*tp){
    int c=zombClip(g_game->bodyLoco(si,spd));
    return (c>=0&&donorOK(g_zombP,c,tp))?c:-1; }
static int zombIdle(int si,Pack*tp){
    int c=zombClip(g_game->bodyIdle(si));
    return (c>=0&&donorOK(g_zombP,c,tp))?c:-1; }
static float g_btF=0.f;   /* beat clock mirrored for the game layer (set from btF0 each frame) */
/* THE START MENU (jacob 08-25: "we need a start menu... make select/back do what start
 * does now"). Start toggles the pause menu anywhere in-game; Back/Select hands the show
 * back to the host (the old Start behavior). "MAIN MENU" is a clean self-relaunch --
 * fresh state by construction, the same trick the console menu plays. */
static int g_btnStart=0, g_btnBack=0, g_startEdge=0;
static int g_pause=0, g_pauseSel=0;
static float g_gameT=0.f;   /* game-layer clock: stops while the start menu is up */
static char**g_argv=NULL;   /* for the MAIN MENU relaunch */
static float g_slotSpdMul[1024];       /* game speed factor per slot; <=0 means 1.0 */
static float g_slotFeet[1024];         /* E->feet: how high a game has walked this body */
__attribute__((unused)) static float g_gmT[1024];              /* per-slot game timer (storm exposure, cooldowns) */
static int   g_slotTeam[1024];         /* 0 none / 1 blue / 2 red (rts tint override) */
static unsigned char g_slotSel[1024];  /* rts: currently selected units pulse */
typedef struct {
    int on;                 /* a mode is initialized and running */
    long score; int kills, wave, lvl, xp, deaths;
    float hp, hpMax, ifr;   /* hero health + post-hit grace */
    float t0, tA, tB;       /* per-mode clocks */
    float ax, az, bx, bz;   /* per-mode anchor points (ring center, pickup, cursor...) */
    float r;                /* ring radius / arena radius */
    int stage, flag, count; /* per-mode small state */
    char msg[96]; float msgT;           /* center banner */
    char l1[96], l2[96], l3[96];        /* HUD lines, rebuilt every frame */
    int npcSlot[6]; float npcX[6], npcZ[6];
    int selN;
    float px, py, vx, vy; int onG, dj;  /* platformer body */
    int seq[8]; float seqBt[8]; int seqN, seqHead; float lastSpawnBt;  /* arcade lane */
    int wKind, wAmmo, wVar; float wCd;  /* THE WEAPON LAYER: the hero's carry (wVar = which mesh) */
} Game;
static Game GM;   /* it starts with engine.h's EngineRun, which is what a game sees of it */
static void gmBanner(const char*s,float t){
    if(strcmp(GM.msg,s)) sfxPlay("banner",0.35f,NULL,0);   /* a NEW banner pings; a repeat does not */
    snprintf(GM.msg,sizeof GM.msg,"%s",s); GM.msgT=t; }
/* ---- neon overlay lines: the game layer's own geometry (rings, markers, cursors,
 * platforms, hit arcs). One line batch (gfxLines3D), flushed once per frame. ---- */
#define GMV_MAX 4096
static float gmV[GMV_MAX*6]; static int gmN=0;
static void gmLine(float x1,float y1,float z1,float x2,float y2,float z2,float r,float g,float b){
    if(gmN+2>GMV_MAX) return;
    float*v=gmV+gmN*6; v[0]=x1;v[1]=y1;v[2]=z1;v[3]=r;v[4]=g;v[5]=b;
    v+=6; v[0]=x2;v[1]=y2;v[2]=z2;v[3]=r;v[4]=g;v[5]=b; gmN+=2; }
static void gmRing(float cx,float cz,float rad,float y,float r,float g,float b,int segs){
    if(segs<8)segs=8;
    if(segs>128)segs=128;
    for(int i=0;i<segs;i++){ float a0=(float)i/segs*6.28318f, a1=(float)(i+1)/segs*6.28318f;
        gmLine(cx+sinf(a0)*rad,y,cz+cosf(a0)*rad, cx+sinf(a1)*rad,y,cz+cosf(a1)*rad, r,g,b); } }
__attribute__((unused)) static void gmMarker(float x,float z,float h,float r,float g,float b){
    gmRing(x,z,1.6f,0.12f,r,g,b,24);
    gmLine(x,0.1f,z,x,h,z,r,g,b);
    gmLine(x-0.6f,h*0.8f,z,x,h,z,r,g,b); gmLine(x,h,z,x+0.0f,h*0.8f,z+0.6f,r,g,b); }
static void gmBurst(float x,float y,float z,float r,float g,float b,int n,float rad){
    for(int i=0;i<n;i++){ float a=(float)i/n*6.28318f, e=((i*37)%100)/100.f*2.4f;
        gmLine(x,y,z, x+sinf(a)*rad*cosf(e),y+sinf(e)*rad,z+cosf(a)*rad*cosf(e), r,g,b); } }
/* PAD 0 (jacob: run around and fly), which every mode reads: engine/input.inc fills these */
static float g_lx=0,g_ly=0,g_rx=0,g_ry=0; static int g_btnA=0,g_btnB=0;
static int g_btnY=0,g_btnLB=0,g_btnRB=0;   /* skate mode: heelflip, and the two grab shoulders */
static int g_manual=0; static double g_inT=0; static float g_manAlt=0, g_manSpd=0;
/* ⭐ FIRST PERSON (09-11). Named missing in OVERVIEW.md's "not yet" list since 08-25 and ranked
 * in the audit; the arena design asks for it. g_fpv is the wish (key V, ctl fpv=, REVA_FPV=1);
 * g_fpvNow is whether the director actually took THIS frame from her eyes -- the projection
 * and the hero draw read that, never the wish, so a mode the lens refuses stays third person. */
static int g_fpv=0, g_fpvNow=0;

/* ================= THE AUDIO MIXER (09-11) lives in club_audio.c since 2026-09-30 ==================== */
