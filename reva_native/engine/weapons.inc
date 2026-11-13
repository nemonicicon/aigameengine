/* ================= THE WEAPON LAYER (OVERVIEW #1; campaign S1) ======================
 * One system for BR / arena / ARPG / the bash: every combatant -- the hero AND the
 * slots -- carries a weapon state. Melee is the birthright, guns are floor pickups,
 * projectiles ride the gm line batch, and an ARMED slot hunts instead of fleeing.
 * Kills go through the same g_slotHide door the bolt and the storm already use.
 * slots/packs/NS live inside main, so main lends them out via g_slots/g_packs/g_NS. */
enum { WPN_NONE=0, WPN_MELEE, WPN_PISTOL, WPN_SPREAD, WPN_BEAM };
static const char*WPN_NAME[]={"BARE","MELEE","PISTOL","SPREAD","BEAM"};
static const float WPN_CD[] ={0.5f,0.55f,0.45f,0.95f,1.20f};
static const int   WPN_AMMO[]={0,0,12,8,6};
static const float WPN_COL[][3]={{0.5f,0.5f,0.5f},{0.8f,0.8f,0.8f},{0.3f,1.2f,1.3f},{1.4f,0.8f,0.2f},{0.9f,0.4f,1.4f}};
static Slot*g_slots=NULL; static Pack*g_packs=NULL; static int g_NS=0;
static int g_slotWpn[1024]; static float g_slotWCd[1024]; static short g_slotWAmmo[1024];
#define WPICK_MAX 24
static struct { float x,z; int kind,def,on; } g_wpick[WPICK_MAX];   /* def: WHICH gun -- it travels to the taker */
static int g_slotWvar[1024];   /* the exact weapon this contestant carries */
#define WPROJ_MAX 96
static struct { float x,y,z,dx,dz,ttl; int owner,kind,on; } g_wproj[WPROJ_MAX];   /* owner -1 = the hero */
static char g_feed[3][96]; static float g_feedT[3];   /* the kill feed: newest first */
/* ================= S5a WEAPONS YOU CAN SEE =========================================
 * GUNS: the layer had them, the hands did not. Real meshes now:
 * AssetsvilleTown's rack out of CivicDefenseV2, exported headless through UE 5.8's
 * glTF plugin (skate_src/guns/) and baked as prop rpacks. One deferred draw list, so
 * every armed body in the club costs a single program switch. */
#define WPROP_N 5
/* THE ARSENAL. Sixteen meshes out of AssetsvilleTown, each tagged with the weapon class
 * it serves; a class holds a SET, and a body picks one and keeps it, so a floor of armed
 * contestants carries bats, katanas, shovels and a chainsaw instead of one cloned prop.
 * UE's material graph did not survive the export (0 images in the GLBs), so each carries
 * its own tint. Every mesh runs along its own +X (measured: pistol 0.33m, bat 1.24m) and
 * the club's forward is +Z, so all of them are yawed a quarter turn. */
typedef struct { const char*file; int kind; float tint[3]; } WpnDef;
static const WpnDef WPNDEF[]={
    {"props/v1_wpn_baseballbat.rpack",1,{0.74f,0.52f,0.28f}},   /* ash */
    {"props/v1_wpn_policebat.rpack",  1,{0.20f,0.21f,0.26f}},   /* black polymer */
    {"props/v1_wpn_katana.rpack",     1,{0.74f,0.76f,0.82f}},   /* bright steel */
    {"props/v1_wpn_golfclub.rpack",   1,{0.56f,0.58f,0.63f}},   /* chrome shaft */
    {"props/v1_wpn_knife.rpack",      1,{0.72f,0.74f,0.78f}},   /* honed edge */
    {"props/v1_wpn_shovel.rpack",     1,{0.46f,0.40f,0.31f}},   /* rusted spade */
    {"props/v1_wpn_chainsaw.rpack",   1,{0.88f,0.46f,0.12f}},   /* safety orange */
    {"props/v1_wpn_pistol.rpack",     2,{0.56f,0.58f,0.64f}},
    {"props/v1_wpn_pistol2.rpack",    2,{0.48f,0.50f,0.57f}},
    {"props/v1_wpn_shotgun.rpack",    3,{0.60f,0.49f,0.38f}},   /* steel and walnut */
    {"props/v1_wpn_rifle.rpack",      3,{0.52f,0.46f,0.40f}},
    {"props/v1_wpn_smg.rpack",        4,{0.45f,0.48f,0.56f}},   /* blued steel */
    {"props/v1_wpn_rifle2.rpack",     4,{0.43f,0.46f,0.53f}},
    {"props/v1_wpn_rifle3.rpack",     4,{0.41f,0.44f,0.51f}},
    {"props/v1_wpn_machinegun.rpack", 4,{0.39f,0.42f,0.49f}} };
#define WPNDEF_N ((int)(sizeof WPNDEF/sizeof WPNDEF[0]))
static Pack g_wpnPack[WPNDEF_N]; static unsigned char g_wpnPackOK[WPNDEF_N];
static int g_wpnByKind[WPROP_N][8], g_wpnNByKind[WPROP_N]; static int g_wpnAnyProp=0;
static int wpnPickDef(int kind,unsigned seed){   /* which weapon of that class this body carries */
    if(kind<=0||kind>=WPROP_N||g_wpnNByKind[kind]<=0) return -1;
    return g_wpnByKind[kind][seed%(unsigned)g_wpnNByKind[kind]]; }
#define WDRAW_MAX 160
static struct { float x,y,z,yaw; int def; } g_wdraw[WDRAW_MAX]; static int g_wdrawN=0;
static void wpnPropAt(float x,float y,float z,float yaw,int def){
    if(g_wdrawN>=WDRAW_MAX||def<0||def>=WPNDEF_N||!g_wpnPackOK[def]) return;
    /* only what the lens can actually see: 400 armed bodies would be 400 prop draws */
    if(hypotf(x-g_lastE[0],z-g_lastE[2])>46.f) return;
    g_wdraw[g_wdrawN].x=x; g_wdraw[g_wdrawN].y=y; g_wdraw[g_wdrawN].z=z;
    g_wdraw[g_wdrawN].yaw=yaw; g_wdraw[g_wdrawN].def=def; g_wdrawN++; }
static void wpnGripPush(const float*model,const float*handWorld,int def,float bodyX,float bodyZ){
    if(!g_wpnAnyProp||def<0) return;
    float hx=handWorld[12],hy=handWorld[13],hz=handWorld[14];
    float wx=model[0]*hx+model[4]*hy+model[8]*hz+model[12];
    float wy=model[1]*hx+model[5]*hy+model[9]*hz+model[13];
    float wz=model[2]*hx+model[6]*hy+model[10]*hz+model[14];
    /* SANITY: a rig whose "forearm" is not one (the animal packs) can put the grip
     * anywhere -- and a bat rendered at the lens is a brown bar across the whole screen.
     * A real hand is within arm's reach of its body and above the floor. */
    if(hypotf(wx-bodyX,wz-bodyZ)>2.5f||wy<0.1f||wy>5.f) return;
    wpnPropAt(wx,wy,wz,atan2f(model[8],model[0]),def); }
/* panic-layer + corpse data lives up here so wpnReset/wpnKillSlot can reach it;
 * the behavior functions live below, after the weapon helpers they lean on */
#define DNG_MAX 12
static struct { float x,z,t; } g_dng[DNG_MAX]; static int g_dngHead=0;
#define CRP_MAX 32
static struct { float x,z,t; int team; } g_crp[CRP_MAX]; static int g_crpHead=0;
static unsigned char g_slotExempt[1024]; static float g_slotFleeT[1024];
static int g_slotFoe[1024];   /* RTS readability: who this soldier is locked with */
/* S4a THE BEHAVIOR CHANNEL: the game layer names what a slot is DOING, the anim layer
 * picks the clip (authored role if the pack carries one, procedural fill if not). */
enum { BHV_NONE=0, BHV_RUN, BHV_ATK, BHV_DOWN };
static unsigned char g_slotBhv[1024]; static float g_slotBhvT[1024];   /* T counts UP from the set */
