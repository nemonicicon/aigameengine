/* ---- SHARED POSES (engine plan, Phase 2) -----------------------------------------------------------------------------------------------------------------------------------
 * "Instancing with shared poses is the crowd setting for when counts go up." A crowd's far bodies on one clip stand in a small set of poses, so each pose is sampled, posed and turned into bones once however many
 * bodies stand in it, and its bones go in the frame once. A game asks with E->tune->poseRate: how many poses a second of clip there are. 0 is the default, and every body has its own. E->tune->poseNear: the nearest
 * this many always keep their own. REVA_POSERATE and REVA_POSENEAR override the game for an A/B. A far body in a shared pose changes clip with a cut, and moves its limbs at that rate; its place on the floor is untouched. Only what's drawn
 * changes: [simhash] holds, and [packet] parts. Its memory is one fixed arena: past it a body just poses itself, so nothing draws wrong. */
#define POSE_ROWS 4096
#define POSE_MEM (32u<<20)
typedef struct { const Pack*P; const void*src; int clip,step; unsigned stamp; M4*world; float**pal; } PoseRow;
static PoseRow g_poseRow[POSE_ROWS]; static unsigned g_poseStamp=0; static unsigned char*g_poseMem=NULL; static size_t g_poseUsed=0;
static int poseRate(void){ static int env=-2; if(env==-2){ const char*e=getenv("REVA_POSERATE"); env=(e&&e[0])?atoi(e):-1; } return env>=0?env:(GAME_ON?g_tune.poseRate:0); }
static int poseNear(void){ static int env=-2; if(env==-2){ const char*e=getenv("REVA_POSENEAR"); env=(e&&e[0])?atoi(e):-1; } return env>=0?env:g_tune.poseNear; }
/* one pass through the crowd: what's here was for the last one */
static int g_poseMade=0,g_poseStood=0;
static void poseTick(void){ g_poseStamp++; g_poseUsed=0; g_poseMade=g_poseStood=0; }
static void posePrint(void){ if(g_poseStood) printf("[poses] %d bodies stood in %d shared poses, %zu KB\n",g_poseStood,g_poseMade,g_poseUsed/1024); }
static void*poseAlloc(size_t n){ n=(n+63)&~(size_t)63; if(!g_poseMem) g_poseMem=(unsigned char*)malloc(POSE_MEM);
    if(!g_poseMem||g_poseUsed+n>POSE_MEM){ static int said=0; if(!said){ said=1; printf("[poses] past %u MB in one frame: the rest pose themselves\n",POSE_MEM>>20); fflush(stdout); } return NULL; }
    void*p=g_poseMem+g_poseUsed; g_poseUsed+=n; return p; }
/* this clip at this moment, moved back to the start of its step. A row comes back: with its world filled, a body has already stood in this pose. Empty, this body fills it in */
static PoseRow*poseFind(Pack*P,const void*src,int clip,const PClip*c,double*fp,int rate){
    if(!c->frames) return NULL;
    double t=fmod(*fp,(double)c->frames); if(t<0) t+=c->frames;
    double per=(double)c->fps/(double)rate; if(per<=0.0) return NULL;
    int st=(int)(t/per); *fp=(double)st*per;
    size_t h=((size_t)P>>4)*31u+((size_t)src>>4)*17u+(size_t)clip*131u+(size_t)st*2654435761u; unsigned k=(unsigned)(h^(h>>13))&(POSE_ROWS-1);
    for(int q=0;q<64;q++,k=(k+1)&(POSE_ROWS-1)){ PoseRow*r=&g_poseRow[k];
        if(r->stamp!=g_poseStamp){ r->stamp=g_poseStamp; r->P=P; r->src=src; r->clip=clip; r->step=st; r->world=NULL; r->pal=NULL; return r; }
        if(r->P==P&&r->src==src&&r->clip==clip&&r->step==st) return r; }
    return NULL; }
/* the first body to stand in a pose leaves it for the rest */
static int poseKeep(PoseRow*r,const Pack*P,M4*world){ M4*w=(M4*)poseAlloc((size_t)P->nNodes*sizeof(M4)); float**pl=(float**)poseAlloc((size_t)P->nMeshes*sizeof(float*));
    if(!w||!pl) return 0;
    memcpy(w,world,(size_t)P->nNodes*sizeof(M4)); memset(pl,0,(size_t)P->nMeshes*sizeof(float*)); r->world=w; r->pal=pl; g_poseMade++; return 1; }
static const float*poseKeepPal(PoseRow*r,unsigned mi,unsigned bones,const float*pal){ float*o=(float*)poseAlloc((size_t)bones*64); if(!o) return pal; memcpy(o,pal,(size_t)bones*64); r->pal[mi]=o; return o; }
/* a clip on this body, the way this path of the crowd samples it, unless the pose is already there. own: it's this body's clip and not a donor's */
#define POSE_CLIP(src,ci,c,ph,own) do{ double fp9=(ph); if(sh9) pr9=poseFind(P,(src),(ci),(c),&fp9,poseR9); if(!pr9||!pr9->world){ if(own) clipSampleF(P,(c),fp9,T,Q,S); else donorPoseF((src),(ci),fp9,P,Q); } }while(0)
