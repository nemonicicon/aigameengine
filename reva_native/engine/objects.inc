/* ---- THINGS A GAME MAKES AND TAKES AWAY (11-03, engine plan Phase 6) --------------------------------------------------------------------------------------------------------
 * A set says what's in the world when it loads. These are what a game puts in and takes out as it runs: a crate, a door, a pickup, a lift. E->prop names a model, once. E->spawn puts one in the world
 * and gives a handle, E->place moves it, E->paint gives it a flat colour, and E->despawn takes it out. A fixed pool, and handles that are never given out twice: a stale one is safe, calls on it do nothing, and E->object gives NULL.
 * It draws the way a set's prop does, so it casts a shadow, it's culled, and its copies merge into one call. OBJ_SOLID makes it something a body can't walk through and a ray stops at: its real triangles in a grid of
 * its own (engine/spans.inc), the same as a streamed cell, so on a set that says `climb` it can be stood on too, and a circle in the old colliders on any other set. Moving a solid object along costs next to nothing, because its
 * columns just move with it. Turning one builds them again, and so does making one, so what it costs is its triangles. A pack marked Open is never solid.
 * ⛔ All of this feeds the rules, so a game does it in tick, and the table goes in [simhash]. A run that never makes one mixes nothing, so nobody else's hash moves.
 * The bodies are the other half: E->bodyNew wakes a free one from the pool, and E->bodyGone puts it back. The pool's size is set at boot. Models stay loaded until the run ends. */
#define OBJ_BITS 10
#define OBJ_HIBITS 3   /* (11-12, the Godot proof of concept's step 14: their Create a Map holds 5000 pieces) a slot's high bits, at 28 to 30 of a handle:
                         8192 at once, and a handle of a slot under 1024 is the number it always was, so no game's state reads differently */
#define OBJ_MAX (1<<(OBJ_BITS+OBJ_HIBITS))
#define OBJ_MAXPROP 512   /* (11-12) their library is 173 pieces */
#define OBJ_GENMAX (1u<<(28-OBJ_BITS))
static int objHandle(int i, unsigned gen) { return (int)((((unsigned)i >> OBJ_BITS) << 28) | (gen << OBJ_BITS) | ((unsigned)i & ((1u << OBJ_BITS) - 1u))); }
typedef struct { EngineObject pub; int on,pk; unsigned gen; float m[16],tint[3],r; int tinted; SpGrid*grid;
    float p[3],q[4],w[3],half[3],ext[3],slope[2],mass,radius,up,bounce,grip,still,restTop; int lastBody,restOn,touch,turning; } Obj;   /* a loose one: p its middle, q how it's turned, w its spin, half its own half sizes, ext how far it reaches along each of the world's axes as it's turned now, up from its lowest point to its middle */
static int g_objNLoose=0,g_objNoLoose=0; static unsigned g_objSeen=0;
static void rdGameClear(void);
_Static_assert(offsetof(Obj,pub)==0,"E->object hands out the start of this");
static Obj g_obj[OBJ_MAX]; static int g_objFree[OBJ_MAX],g_objNFree=0,g_objHi=0,g_objEver=0;
static struct { char stem[56]; int pk,own; } g_objProp[OBJ_MAXPROP]; static int g_nObjProp=0,g_objReady=0;

static void objSay(const char*what){ static const char*said[8]; static int n=0; for(int i=0;i<n;i++) if(!strcmp(said[i],what)) return;   /* once each. They're all string literals */
    if(n<8) said[n++]=what;
    printf("[objects] %s\n",what); fflush(stdout); }
/* a handle's slot, if it's still that one */
static Obj* objOf(int h){ if(h<=0) return NULL; const unsigned u=(unsigned)h; const int i=(int)((u&((1u<<OBJ_BITS)-1u))|(((u>>28)&((1u<<OBJ_HIBITS)-1u))<<OBJ_BITS)); Obj*O=&g_obj[i];
    return (i<g_objHi&&O->on&&O->gen==((u>>OBJ_BITS)&(OBJ_GENMAX-1u)))?O:NULL; }
/* (11-11) what this machine doesn't show this frame (E->bodyUnseen, E->objectUnseen): asked in draw, cleared before the next */
static unsigned char g_objUnseen[OBJ_MAX], g_bodyUnseen[1024];
/* (11-12) E->bodyPlace: where this frame draws a game's body, and turned how: its place (units) and its yaw, pitch and roll as E->placeTurned has them.
 * Picture, not rules, like bodyUnseen: forgotten at the next draw. The body's pose, its shadow and what's on its bones go with it */
static float g_bodyPlace[1024][6]; static unsigned char g_bodyPlaceOn[1024];
static void unseenClear(void){ memset(g_objUnseen,0,sizeof g_objUnseen); memset(g_bodyUnseen,0,sizeof g_bodyUnseen); memset(g_bodyPlaceOn,0,sizeof g_bodyPlaceOn); }
static void apiBodyUnseen(int i){ if(i>=0&&i<1024) g_bodyUnseen[i]=1; }
static void apiBodyPlace(int i,float x,float y,float z,float yaw,float pitch,float roll){ if(i<0||i>=1024) return;
    const float v[6]={x,y,z,yaw,pitch,roll}; for(int k=0;k<6;k++) if(!(v[k]-v[k]==0.f)) return;   /* nothing that isn't a number */
    memcpy(g_bodyPlace[i],v,sizeof v); g_bodyPlaceOn[i]=1; }
static void bodyPlaceModel(int si,float*model){ const float*p=g_bodyPlace[si];
    const float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); float qy[4],qx[4],qz[4],qt[4],q[4],t[3]={p[0],p[1],p[2]},s3[3]={sc,sc,sc};
    qaxis(qy,0,1,0,p[3]); qaxis(qx,1,0,0,p[4]); qaxis(qz,0,0,1,p[5]); qmul(qt,qy,qx); qmul(q,qt,qz); m4_trs(model,t,q,s3); }
static void apiObjectUnseen(int h){ const Obj*O=objOf(h); if(O) g_objUnseen[O-g_obj]=1; }
static int objHandleOf(const Obj*O){ return objHandle((int)(O-g_obj),O->gen); }
/* a model: read now if the renderer's up, and otherwise once it is. Read in play, it holds the frame up for as long as that takes, and says so */
static int objPropLoad(int i){ if(g_objProp[i].pk!=-1) return g_objProp[i].pk;
    int fresh=1; for(int k=0;k<g_setNP;k++) if(!strcmp(g_setNm[k],g_objProp[i].stem)) fresh=0;
    double t0=wall_ms(); const int inPlay=g_tickTotal>1;
#ifdef REVA_VK
    const int quiet=inPlay&&!getenv("REVA_FIXEDSTEP");
    if(quiet) gfxUploadAsync(1);
#endif
    int pk=setPack(g_objProp[i].stem);
#ifdef REVA_VK
    if(quiet) gfxUploadAsync(0);
#endif
    g_objProp[i].pk=pk<0?-2:pk; g_objProp[i].own=fresh;
    if(pk>=0) shdPropIn(pk);   /* (11-12, step 14) the set's `shader` lines that name it: a prop asked for after the set came in takes them now */
    if(pk<0){ printf("[objects] no prop called %s\n",g_objProp[i].stem); fflush(stdout); }
    else if(inPlay&&fresh){ printf("[objects] %s was read during play, %.1f ms: ask for it in boot and it won't be\n",g_objProp[i].stem,wall_ms()-t0); fflush(stdout); }
    return g_objProp[i].pk; }
static int apiProp(const char*stem){ for(int i=0;i<g_nObjProp;i++) if(!strcmp(g_objProp[i].stem,stem)) return i;
    if(g_nObjProp>=OBJ_MAXPROP){ objSay("more props than the table holds"); return -1; }
    int i=g_nObjProp++; snprintf(g_objProp[i].stem,sizeof g_objProp[i].stem,"%s",stem); g_objProp[i].pk=-1;
    if(g_objReady) objPropLoad(i);
    return i; }
/* a model you've finished with: its memory goes, and its number still works, because the next spawn reads it again. Not while anything has it out, or if the set was using it before you asked */
static int apiPropDone(int prop){ if(prop<0||prop>=g_nObjProp) return 0; const int pk=g_objProp[prop].pk; if(pk<0) return 1; if(!g_objProp[prop].own) return 0;
    for(int i=0;i<g_objHi;i++) if(g_obj[i].on&&g_obj[i].pk==pk) return 0;
    for(int i=0;i<g_setNI;i++) if(g_setI[i].pk==pk) return 0;
    packFree(&g_setPk[pk]); g_setNm[pk][0]=0; g_setFree[g_setNFree++]=pk; g_objProp[prop].pk=-1; return 1; }
/* the renderer's up and the set's loaded: what a game asked for in boot, before it starts */
static void objReady(void){ g_objReady=1; for(int i=0;i<g_nObjProp;i++) objPropLoad(i); }
/* the matrix, the way a set's prop line makes one: y is where its lowest point goes */
static void objMatrix(Obj*O){ const Pack*Q=&g_setPk[O->pk]; float eff=O->pub.scale*((Q->scale>1e-6f)?Q->scale:1.f),ca=cosf(O->pub.yaw),sa=sinf(O->pub.yaw);
    const float my=(O->pub.flags&OBJ_ORIGIN)?0.f:Q->mny;   /* (11-12) OBJ_ORIGIN: its own origin at y, as the editor it was made in puts it */
    memset(O->m,0,64); O->m[0]=ca*eff; O->m[2]=-sa*eff; O->m[5]=eff; O->m[8]=sa*eff; O->m[10]=ca*eff; O->m[15]=1.f; O->m[12]=O->pub.x; O->m[13]=O->pub.y-my*eff; O->m[14]=O->pub.z; O->pub.top=O->pub.y+(Q->propY1-my)*eff; }
/* ---- LOOSE ONES: OBJ_LOOSE. They fall, bounce, slide, roll and come to rest, on what's really there: the walked world's surfaces, so one lands on a deck and rolls down a ramp, and it stands on any other solid thing. The solver is the 09-11 rigid layer's way of doing it, grown up: one point with an orientation, not a set of corners. A box is held by how far it reaches as it's turned now, so its lowest corner is what touches, and it turns onto the face its weight is over, so something tall goes over easily and something wide doesn't. Two loose things meet as the boxes around them, so they stack and knock each other about. ⛔ It's no contact solver: nothing here works out the forces at each corner, so a box doesn't rock, and a stack doesn't lean before it goes.
 * Asleep, it costs a look at what's under it, and only when something's changed. If it's solid it has columns then, like any solid thing, so it can be stood on and walked into. Awake it has none: a tumbling thing's triangles aren't worth building every tick. */
#define LOOSE_QUIET 1.5f    /* slower than this, a hit doesn't bounce and nobody hears it */
#define LOOSE_WAKE 0.5f     /* slower than this, what's asleep stays asleep, and what hit it stops */
#define LOOSE_MAXV 300.f    /* 170 metres a second, and a step has to stay under a column's width */
/* its own axes, in the world */
static void looseAxes(const float*q,float ax[3][3]){ const float x=q[0],y=q[1],z=q[2],w=q[3];
    ax[0][0]=1-2*(y*y+z*z); ax[0][1]=2*(x*y+z*w); ax[0][2]=2*(x*z-y*w);
    ax[1][0]=2*(x*y-z*w); ax[1][1]=1-2*(x*x+z*z); ax[1][2]=2*(y*z+x*w);
    ax[2][0]=2*(x*z+y*w); ax[2][1]=2*(y*z-x*w); ax[2][2]=1-2*(x*x+y*y); }
/* how far it reaches along a direction: from all three axes for a box, and its radius for a ball */
static float looseReach(const Obj*O,const float*n){ if(O->pub.flags&OBJ_ROUND) return O->radius; float ax[3][3],d=0.f; looseAxes(O->q,ax);
    for(int k=0;k<3;k++) d+=fabsf(ax[k][0]*n[0]+ax[k][1]*n[1]+ax[k][2]*n[2])*O->half[k];
    return d; }
static void looseExtent(Obj*O){ if(O->pub.flags&OBJ_ROUND){ O->ext[0]=O->ext[1]=O->ext[2]=O->radius; return; } float ax[3][3]; looseAxes(O->q,ax);
    for(int c=0;c<3;c++) O->ext[c]=fabsf(ax[0][c])*O->half[0]+fabsf(ax[1][c])*O->half[1]+fabsf(ax[2][c])*O->half[2]; }
static void looseMatrix(Obj*O){ const Pack*Q=&g_setPk[O->pk]; const float sc=O->pub.scale*((Q->scale>1e-6f)?Q->scale:1.f); float ax[3][3]; looseAxes(O->q,ax);
    const float lx=Q->propCx,ly=(Q->propY0+Q->propY1)*0.5f,lz=Q->propCz; float*m=O->m; memset(m,0,64);
    for(int k=0;k<3;k++) for(int c=0;c<3;c++) m[k*4+c]=ax[k][c]*sc;
    m[15]=1.f; m[12]=O->p[0]-(m[0]*lx+m[4]*ly+m[8]*lz); m[13]=O->p[1]-(m[1]*lx+m[5]*ly+m[9]*lz); m[14]=O->p[2]-(m[2]*lx+m[6]*ly+m[10]*lz); }
/* what a game reads */
static void looseSync(Obj*O){ float ax[3][3]; looseAxes(O->q,ax); looseExtent(O); looseMatrix(O);
    O->pub.x=O->p[0]; O->pub.y=O->p[1]-O->ext[1]; O->pub.top=O->p[1]+O->ext[1]; O->pub.z=O->p[2];
    if(fabsf(ax[2][1])<0.95f) O->pub.yaw=atan2f(ax[2][0],ax[2][2]); }
static void looseStart(Obj*O){ const Pack*Q=&g_setPk[O->pk]; const float sc=O->pub.scale*((Q->scale>1e-6f)?Q->scale:1.f),ha=O->pub.yaw*0.5f;
    O->half[0]=fmaxf(0.05f,Q->propEx*sc); O->half[1]=fmaxf(0.05f,(Q->propY1-Q->propY0)*0.5f*sc); O->half[2]=fmaxf(0.05f,Q->propEz*sc);
    O->radius=fmaxf(O->half[0],fmaxf(O->half[1],O->half[2])); O->mass=8.f*O->half[0]*O->half[1]*O->half[2];
    O->up=((Q->propY0+Q->propY1)*0.5f-Q->mny)*sc; O->p[0]=O->pub.x; O->p[1]=O->pub.y+O->up; O->p[2]=O->pub.z;
    O->q[0]=0.f; O->q[1]=sinf(ha); O->q[2]=0.f; O->q[3]=cosf(ha); O->w[0]=O->w[1]=O->w[2]=0.f; O->slope[0]=O->slope[1]=0.f;
    O->still=0.f; O->restTop=0.f; O->lastBody=-2; O->restOn=0; O->touch=0; O->pub.vx=O->pub.vy=O->pub.vz=0.f; O->pub.asleep=0; looseExtent(O); }
/* asleep where it is. It stays that way until what's under it changes, or something hits it, so a game can hang something in the air */
static void looseRests(Obj*O){ O->pub.asleep=1; O->restTop=spTopBelow(O->p[0],O->p[2],O->p[1]-O->ext[1],O->grid); }
static void objGridDrop(Obj*O){ if(!O->grid) return; spLiveDrop(O->grid); spGridFree(O->grid); free(O->grid); O->grid=NULL; }
static void objGridMake(Obj*O){ if(g_spNLive>=SP_MAXLIVE){ objSay("too many solid things at once: more of them are only circles now"); return; }
    O->grid=spGridOfPack(g_setPk[O->pk].pk,O->m); if(O->grid){ O->grid->soft=(O->pub.flags&OBJ_LOOSE)!=0; spLiveAdd(O->grid); } }
/* (11-12) A GAME'S BODY SHOWN MIRRORED, LEFT FOR RIGHT (E->bodyMirror; the skate game's rider in switch, and riding backward: SKATE_SPEC_2.md 7.6
 * and 9). Each bone takes its twin's matrix by name (_L and _R, .L and .R, Left and Right) reflected across the pack's own x, the body's middle:
 * for a rig whose rest is symmetric, that's its clip played by the other side. What's on its bones goes with it. Picture, not rules */
static unsigned char g_bodyMirror[1024];
static void apiBodyMirror(int i,int on){ if(i>=0&&i<1024) g_bodyMirror[i]=(unsigned char)(on!=0); }
static int boneTwinName(const char*n,char*o,size_t cap){   /* 1 if the name says a side, the other side's name in o */
    static const char*const A[4]={"Left","Right","left","right"}, *const B[4]={"Right","Left","right","left"};
    for(int k=0;k<4;k++){ const char*p=strstr(n,A[k]); if(p){ snprintf(o,cap,"%.*s%s%s",(int)(p-n),n,B[k],p+strlen(A[k])); return 1; } }
    const size_t l=strlen(n);
    if(l>2&&(n[l-2]=='_'||n[l-2]=='.')&&strchr("LRlr",n[l-1])){ snprintf(o,cap,"%s",n); o[l-1]=n[l-1]=='L'?'R':n[l-1]=='R'?'L':n[l-1]=='l'?'r':'l'; return 1; }
    if(l>2&&(n[1]=='_'||n[1]=='.')&&strchr("LRlr",n[0])){ snprintf(o,cap,"%s",n); o[0]=n[0]=='L'?'R':n[0]=='R'?'L':n[0]=='l'?'r':'l'; return 1; }
    return 0; }
#define TWIN_PACKS 32
static struct { const Pack*P; unsigned n; short*t; } g_twin[TWIN_PACKS]; static int g_twinN=0;
static const short* packTwins(Pack*P){
    for(int i=0;i<g_twinN;i++) if(g_twin[i].P==P&&g_twin[i].n==P->nNodes) return g_twin[i].t;
    if(g_twinN>=TWIN_PACKS) return NULL;
    short*t=(short*)malloc(sizeof(short)*(P->nNodes?P->nNodes:1)); if(!t) return NULL;
    int paired=0;
    for(unsigned a=0;a<P->nNodes;a++){ t[a]=(short)a; char tw[96]; const char*nm=packStr(P,P->nodes[a].name);
        if(nm&&boneTwinName(nm,tw,sizeof tw)) for(unsigned b=0;b<P->nNodes;b++){ const char*m=packStr(P,P->nodes[b].name); if(m&&!strcmp(m,tw)){ t[a]=(short)b; paired++; break; } } }
    g_twin[g_twinN].P=P; g_twin[g_twinN].n=P->nNodes; g_twin[g_twinN].t=t; g_twinN++;
    printf("[mirror] a pack of %u bones: %d of them have a twin by name\n",P->nNodes,paired); fflush(stdout);
    return t; }
static M4 g_mirW[1024];
static M4* poseMirror(Pack*P,M4*W){
    const short*t=packTwins(P); if(!t||P->nNodes>1024) return W;
    for(unsigned n=0;n<P->nNodes;n++){ const float*s=(const float*)W[t[n]]; float*d=(float*)g_mirW[n]; memcpy(d,s,64);
        d[1]=-s[1]; d[2]=-s[2]; d[4]=-s[4]; d[8]=-s[8]; d[12]=-s[12]; }   /* S M S with S the reflection of x: what has one x in it changes sign */
    return g_mirW; }
static int apiSpawn(int prop,float x,float y,float z,float yaw,float scale,unsigned flags){
    if(!g_objReady){ objSay("spawn in start or tick: in boot the world isn't loaded yet"); return 0; }
    if(prop<0||prop>=g_nObjProp){ objSay("spawn with a prop nobody asked for"); return 0; }
    int pk=objPropLoad(prop); if(pk<0) return 0;
    int i=g_objNFree?g_objFree[--g_objNFree]:(g_objHi<OBJ_MAX?g_objHi++:-1);
    if(i<0){ objSay("all 8192 objects are in use: spawn gave back 0"); return 0; }
    Obj*O=&g_obj[i]; const unsigned gen=O->gen?O->gen:1u; memset(O,0,sizeof *O); O->on=1; O->pk=pk; O->gen=gen;
    O->pub.x=x; O->pub.y=y; O->pub.z=z; O->pub.yaw=yaw; O->pub.scale=scale>0.f?scale:1.f; O->pub.prop=prop; O->pub.flags=flags;
    objMatrix(O);
    if(g_setPk[pk].open) O->pub.flags&=~(unsigned)OBJ_SOLID;
    if(O->pub.flags&OBJ_LOOSE){ looseStart(O); looseMatrix(O); O->bounce=0.32f; O->grip=0.55f; g_objNLoose++; if(O->pub.flags&OBJ_RESTING) looseRests(O); }
    if(O->pub.flags&OBJ_SOLID){ O->r=g_setPk[pk].propR*O->pub.scale*((g_setPk[pk].scale>1e-6f)?g_setPk[pk].scale:1.f)*0.78f; if(!(O->pub.flags&OBJ_LOOSE)||O->pub.asleep) objGridMake(O); g_objNSolid++; }
    g_objLive++; g_objEver=1;
    return objHandle(i,gen); }
static void apiDespawn(int h){ Obj*O=objOf(h); if(!O) return; int i=(int)(O-g_obj);
    objGridDrop(O); if(O->pub.flags&OBJ_SOLID) g_objNSolid--;
    if(O->pub.flags&OBJ_LOOSE) g_objNLoose--;
    O->on=0; g_objLive--;
    if(++O->gen<OBJ_GENMAX) g_objFree[g_objNFree++]=i; }   /* a slot that's used every handle it has retires, so none is ever given out twice */
/* move it. A solid one's columns go with it */
static void apiPlace(int h,float x,float y,float z,float yaw){ Obj*O=objOf(h); if(!O) return; O->pub.pitch=O->pub.roll=0.f;
    if(O->pub.flags&OBJ_LOOSE){ objGridDrop(O); O->pub.x=x; O->pub.y=y; O->pub.z=z; O->pub.yaw=yaw; objMatrix(O); looseStart(O); looseMatrix(O); g_spStamp++;   /* put somewhere, it starts from there, at no speed */
        if(O->pub.flags&OBJ_RESTING){ looseRests(O); if(O->pub.flags&OBJ_SOLID) objGridMake(O); }
        return; }
    const float dx=x-O->pub.x,dy=y-O->pub.y,dz=z-O->pub.z; const int turned=yaw!=O->pub.yaw;
    O->pub.x=x; O->pub.y=y; O->pub.z=z; O->pub.yaw=yaw; objMatrix(O);
    if(O->pub.flags&OBJ_SOLID){ if(turned){ objGridDrop(O); objGridMake(O); }
        else if(O->grid){ SpGrid*G=O->grid; G->x0+=dx; G->z0+=dz; if(dy!=0.f) for(int s=0;s<G->n;s++){ G->sp[s].y0+=dy; G->sp[s].y1+=dy; } g_spStamp++; } } }
/* (11-05) turned any way: yaw, pitch, roll. A ride's model is one; so is a scooter laid over in the lane */
static void apiPlaceTurned(int h,float x,float y,float z,float yaw,float pitch,float roll){ Obj*O=objOf(h); if(!O||(O->pub.flags&OBJ_LOOSE)) return;
    if(!(pitch-pitch==0.f)||!(roll-roll==0.f)){ pitch=0.f; roll=0.f; }
    if(pitch==0.f&&roll==0.f){ O->pub.pitch=O->pub.roll=0.f; apiPlace(h,x,y,z,yaw); return; }
    const Pack*Q=&g_setPk[O->pk]; const float eff=O->pub.scale*((Q->scale>1e-6f)?Q->scale:1.f); float qy[4],qx[4],qz[4],qt[4],q[4],t[3]={x,0.f,z},s3[3]={eff,eff,eff};
    O->pub.x=x; O->pub.y=y; O->pub.z=z; O->pub.yaw=yaw; O->pub.pitch=pitch; O->pub.roll=roll;
    qaxis(qy,0,1,0,yaw); qaxis(qx,1,0,0,pitch); qaxis(qz,0,0,1,roll); qmul(qt,qy,qx); qmul(q,qt,qz); m4_trs(O->m,t,q,s3);
    const float my=(O->pub.flags&OBJ_ORIGIN)?0.f:Q->mny;
    O->m[13]=y-(O->m[5]*my); O->m[12]-=O->m[4]*my; O->m[14]-=O->m[6]*my;   /* its lowest point, along its own up, at y (OBJ_ORIGIN: its origin) */
    O->pub.top=y+(Q->propY1-my)*eff;
    if(O->pub.flags&OBJ_SOLID){ objGridDrop(O); objGridMake(O); } }
static void apiPaint(int h,float r,float g,float b){ Obj*O=objOf(h); if(!O) return; O->tinted=r>=0.f; O->tint[0]=r; O->tint[1]=g; O->tint[2]=b; }
/* add this to its speed, and it wakes. A push that lifts it turns it over as it goes, and one along the ground slides it flat */
static void looseWake(Obj*O){ if(!O->pub.asleep) return; O->pub.asleep=0; O->still=0.f; O->restOn=0; objGridDrop(O); g_spStamp++; }
static void apiPush(int h,float vx,float vy,float vz){ Obj*O=objOf(h); if(!O||!(O->pub.flags&OBJ_LOOSE)) return; looseWake(O); O->pub.vx+=vx; O->pub.vy+=vy; O->pub.vz+=vz;
    if(vy>0.5f){ const float k=0.35f/O->radius; O->w[0]+=vz*k; O->w[2]-=vx*k; } }
/* the same, at a place on it: a push high up on something tall sends it over */
static void apiPushAt(int h,float x,float y,float z,float vx,float vy,float vz){ Obj*O=objOf(h); if(!O||!(O->pub.flags&OBJ_LOOSE)) return; looseWake(O); O->pub.vx+=vx; O->pub.vy+=vy; O->pub.vz+=vz;
    const float rx=x-O->p[0],ry=y-O->p[1],rz=z-O->p[2],I=0.67f*O->radius*O->radius;
    O->w[0]+=(ry*vz-rz*vy)/I; O->w[1]+=(rz*vx-rx*vz)/I; O->w[2]+=(rx*vy-ry*vx)/I;
    const float l=sqrtf(O->w[0]*O->w[0]+O->w[1]*O->w[1]+O->w[2]*O->w[2]); if(l>30.f){ const float s=30.f/l; O->w[0]*=s; O->w[1]*=s; O->w[2]*=s; } }
static void apiFeel(int h,float bounce,float grip){ Obj*O=objOf(h); if(!O||!(O->pub.flags&OBJ_LOOSE)) return; O->bounce=bounce<0.f?0.f:bounce>0.95f?0.95f:bounce; O->grip=grip<0.f?0.f:grip>1.f?1.f:grip; }
static const EngineObject* apiObject(int h){ Obj*O=objOf(h); return O?&O->pub:NULL; }
/* a new set, or a game starting over: everything goes, and no old handle comes back to life */
static void objClear(void){ for(int i=0;i<g_objHi;i++){ Obj*O=&g_obj[i]; if(O->on){ objGridDrop(O); O->on=0; O->gen++; } }
    g_objNFree=0; for(int i=g_objHi-1;i>=0;i--) if(g_obj[i].gen<OBJ_GENMAX) g_objFree[g_objNFree++]=i;
    g_objLive=g_objNSolid=g_objNLoose=0; rdGameClear(); }
/* the old colliders' half of OBJ_SOLID, for a set with no `climb` */
static float objCollide(float*x,float*z,float mr){ float ox=*x,oz=*z;
    for(int pass=0;pass<2;pass++) for(int i=0;i<g_objHi;i++){ const Obj*O=&g_obj[i]; if(!O->on||!(O->pub.flags&OBJ_SOLID)||((O->pub.flags&OBJ_LOOSE)&&(!O->pub.asleep||g_objNoLoose))) continue;
        float dx=*x-O->pub.x,dz=*z-O->pub.z,need=O->r+mr,d2=dx*dx+dz*dz; if(d2>=need*need||d2<1e-9f) continue;
        float d=sqrtf(d2),push=(need-d)/d; *x+=dx*push; *z+=dz*push; }
    float mx=*x-ox,mz=*z-oz; return sqrtf(mx*mx+mz*mz); }
/* one thing hit another: the game hears, and may take either away */
static void looseTell(const Obj*O,int other,int body,float speed){ if(g_game&&g_game->objectHit&&speed>0.8f) g_game->objectHit(objHandleOf(O),other,body,speed); }
/* is anything hard in the way of this face of it? Places along the face, from a step above its bottom to its top. A slope it's on isn't in the way, and what's asleep and loose isn't hard: looseMeet has that */
static int looseSide(const Obj*O,int a,float s){ const int b=a?0:2; const float y1=O->p[1]+O->ext[1]; float y0=O->p[1]-O->ext[1]+SP_STEP+2.f*(fabsf(O->slope[0])*O->ext[0]+fabsf(O->slope[1])*O->ext[2]); if(y0>O->p[1]) y0=O->p[1];
    const float at=O->p[a]+s*O->ext[a],span=O->ext[b]*0.8f; int n=(int)(span/0.25f)+1; if(n>8) n=8;
    for(int k=-n;k<=n;k++){ const float along=O->p[b]+span*(float)k/(float)n; if(spHits(a?along:at,a?at:along,y0,y1)) return 1; }
    return 0; }
/* move it along one axis, if that isn't into something */
static void looseNudge(Obj*O,int a,float d){ if(d==0.f) return; O->p[a]+=d; if(looseSide(O,a,d>0.f?1.f:-1.f)) O->p[a]-=d; }
/* what's in its way */
static void looseWalls(Obj*O,const float*was){ float*v=&O->pub.vx; const int h0=objHandleOf(O);
    for(int pass=0;pass<2;pass++){ const int a=pass?2:0; const float s=v[a]>0.f?1.f:-1.f;
        if(fabsf(v[a])>1e-5f&&looseSide(O,a,s)){ const float imp=fabsf(v[a]); O->p[a]=was[a]; v[a]*=imp<LOOSE_QUIET?0.f:-O->bounce; if(imp>=LOOSE_QUIET){ looseTell(O,0,-2,imp); if(objOf(h0)!=O) return; } }
        /* it fell beside something, or it was left in something: out, the way that's clear */
        const int lo=looseSide(O,a,-1.f),hi=looseSide(O,a,1.f); if(lo!=hi) O->p[a]+=hi?-0.05f:0.05f; }
    if(!g_spOn){ float wx=O->p[0],wz=O->p[2]; g_objNoLoose=1; const float moved=colResolve(&wx,&wz,fmaxf(O->ext[0],O->ext[2])); g_objNoLoose=0;
        if(moved>0.f){ float dx=wx-O->p[0],dz=wz-O->p[2],l=sqrtf(dx*dx+dz*dz); O->p[0]=wx; O->p[2]=wz;
            if(l>1e-6f){ dx/=l; dz/=l; const float vn=v[0]*dx+v[2]*dz; if(vn<0.f){ const float e=-vn<LOOSE_QUIET?0.f:O->bounce; v[0]-=(1.f+e)*vn*dx; v[2]-=(1.f+e)*vn*dz; if(-vn>=LOOSE_QUIET) looseTell(O,0,-2,-vn); } } } } }
/* the ground's slope under it, from the heights either side, as far out as it reaches. A drop steeper than that is an edge, not a slope */
static void looseNormal(const Obj*O,float x,float z,float y,float*n){ float d=fmaxf(O->ext[0],O->ext[2])*0.7f; if(d<0.25f) d=0.25f; if(d>1.f) d=1.f; const float lim=d*1.4f,c=spTopBelow(x,z,y,NULL);
    float a=spTopBelow(x-d,z,y,NULL)-c,b=spTopBelow(x+d,z,y,NULL)-c,e=spTopBelow(x,z-d,y,NULL)-c,f=spTopBelow(x,z+d,y,NULL)-c;
    if(fabsf(a)>lim) a=0.f;
    if(fabsf(b)>lim) b=0.f;
    if(fabsf(e)>lim) e=0.f;
    if(fabsf(f)>lim) f=0.f;
    n[0]=a-b; n[1]=2.f*d; n[2]=e-f; const float l=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); n[0]/=l; n[1]/=l; n[2]/=l; }
/* it's pressing on the ground. Friction is what the push against the ground allows, so it stops in a time you can work out, and a box stays put on a slope its grip can hold. A ball only has what rolling takes */
static void looseContact(Obj*O,const float*n){ float*v=&O->pub.vx; const float vn=v[0]*n[0]+v[1]*n[1]+v[2]*n[2]; if(vn>=0.f) return;
    const float imp=-vn,e=imp<LOOSE_QUIET?0.f:O->bounce; const int round=(O->pub.flags&OBJ_ROUND)!=0;
    for(int k=0;k<3;k++) v[k]-=(1.f+e)*vn*n[k];
    const float vd=v[0]*n[0]+v[1]*n[1]+v[2]*n[2],vt[3]={v[0]-vd*n[0],v[1]-vd*n[1],v[2]-vd*n[2]},l=sqrtf(vt[0]*vt[0]+vt[1]*vt[1]+vt[2]*vt[2]),dv=(round?O->grip*0.1f:O->grip)*(1.f+e)*imp;
    if(l<=dv){ for(int k=0;k<3;k++) v[k]=vd*n[k]; }
    else { const float s=dv/l; for(int k=0;k<3;k++) v[k]-=vt[k]*s; }
    if(round){ const float r=O->radius; O->w[0]=(n[1]*v[2]-n[2]*v[1])/r; O->w[1]=(n[2]*v[0]-n[0]*v[2])/r; O->w[2]=(n[0]*v[1]-n[1]*v[0])/r; }   /* it rolls */
    else if(imp>=LOOSE_QUIET){ const float r=O->radius; O->w[0]=O->w[0]*0.6f+(n[1]*vt[2]-n[2]*vt[1])/r*0.35f; O->w[1]=O->w[1]*0.6f+(n[2]*vt[0]-n[0]*vt[2])/r*0.35f; O->w[2]=O->w[2]*0.6f+(n[0]*vt[1]-n[1]*vt[0])/r*0.35f; }   /* a hit turns it over, the way it was going */
    if(imp>=LOOSE_QUIET) looseTell(O,0,-2,imp); }
/* a box comes to rest on a face: the one its weight is over, which is the face a line straight down from its middle comes out of. From high up it goes slowly, and it comes down hard. Its foot stays on the ground as it goes */
static void looseSettle(Obj*O,float h,const float*n){ float ax[3][3]; looseAxes(O->q,ax); int k=0; float best=-1.f,dk=0.f;
    for(int i=0;i<3;i++){ const float c=ax[i][0]*n[0]+ax[i][1]*n[1]+ax[i][2]*n[2],r=fabsf(c)/O->half[i]; if(r>best){ best=r; k=i; dk=c; } }
    const float s=dk<0.f?-1.f:1.f,a[3]={s*ax[k][0],s*ax[k][1],s*ax[k][2]},c[3]={a[1]*n[2]-a[2]*n[1],a[2]*n[0]-a[0]*n[2],a[0]*n[1]-a[1]*n[0]},sn=sqrtf(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]);
    if(sn<1e-4f) return;
    const float ang=atan2f(sn,fabsf(dk)),turn=fminf(ang,(1.f+4.5f*(1.f-fminf(1.f,ang/1.4f)))*h)*0.5f,sh=sinf(turn)/sn,dq[4]={c[0]*sh,c[1]*sh,c[2]*sh,cosf(turn)}; float r9[4]; qmul(r9,dq,O->q);
    const float l=sqrtf(r9[0]*r9[0]+r9[1]*r9[1]+r9[2]*r9[2]+r9[3]*r9[3]); if(l>1e-6f) for(int i=0;i<4;i++) O->q[i]=r9[i]/l;
    const float e0=O->ext[1]; looseExtent(O); O->p[1]-=e0-O->ext[1]; O->turning=1; }
static int apiNear(float x,float z,float radius,int*out,int max);
/* the bodies: it bounces off them and they hear about it, once a touch. What it does to them is the game's. A body walking into something awake shoves it along, and one asleep and solid is a wall */
static void looseBodies(Obj*O){ if(!GAME_ON) return; float*v=&O->pub.vx; const int h0=objHandleOf(O),heroHere=!(g_game&&g_game->flat); int nb[16],nn=apiNear(O->p[0],O->p[2],fmaxf(O->ext[0],O->ext[2])+0.6f,nb,16),was=O->lastBody,now=-2;
    for(int k=heroHere?-1:0;k<nn;k++){ const int who=k<0?-1:nb[k]; float bx=g_rvx,bz=g_rvz,by=g_spOn?g_feet.y:0.f;
        if(who>=0){ if(g_slotHide[who]) continue; bx=g_slots[who].x; bz=g_slots[who].z; by=g_slotFeet[who]; }
        if(O->p[1]-O->ext[1]>by+3.2f||O->p[1]+O->ext[1]<by) continue;
        const float cx=fminf(fmaxf(bx,O->p[0]-O->ext[0]),O->p[0]+O->ext[0]),cz=fminf(fmaxf(bz,O->p[2]-O->ext[2]),O->p[2]+O->ext[2]),dx=cx-bx,dz=cz-bz,d2=dx*dx+dz*dz; float nx,nz,pen;
        if(d2>=0.42f*0.42f) continue;
        if(d2>1e-8f){ const float d=sqrtf(d2); nx=dx/d; nz=dz/d; pen=0.42f-d; }
        else { const float ox=O->ext[0]-fabsf(bx-O->p[0]),oz=O->ext[2]-fabsf(bz-O->p[2]); if(ox<oz){ nx=bx<O->p[0]?1.f:-1.f; nz=0.f; pen=ox+0.42f; } else { nx=0.f; nz=bz<O->p[2]?1.f:-1.f; pen=oz+0.42f; } }
        now=who; const float sp=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]),vn=v[0]*nx+v[2]*nz; if(pen>0.2f) pen=0.2f;
        looseNudge(O,0,nx*pen); looseNudge(O,2,nz*pen);
        if(vn<0.f){ const float e=-vn<LOOSE_QUIET?0.f:O->bounce; v[0]-=(1.f+e)*vn*nx; v[2]-=(1.f+e)*vn*nz; }
        if(who!=was){ O->lastBody=now; looseTell(O,0,who,sp); if(objOf(h0)!=O) return; } }
    O->lastBody=now; }
/* one tick of one loose thing, in steps no longer than a column's width, so a thrown one doesn't go through a wall */
static void looseStep(Obj*O,float dt){ const float G9=CLUB_UNITS_PER_S2; float*v=&O->pub.vx; const int h0=objHandleOf(O),wasTouch=O->touch; O->touch=0; O->restOn=0; O->turning=0;
    { const float sp=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(sp>LOOSE_MAXV){ const float k=LOOSE_MAXV/sp; v[0]*=k; v[1]*=k; v[2]*=k; } }
    int ns=(int)((sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2])+G9*dt)*dt/0.18f)+1; if(ns>32) ns=32; const float h=dt/(float)ns;
    for(int it=0;it<ns;it++){ const float was[3]={O->p[0],O->p[1],O->p[2]}; float n[3]={0.f,1.f,0.f}; int touched=0;
        v[1]-=G9*h; { const float ad=1.f-0.03f*h; v[0]*=ad; v[1]*=ad; v[2]*=ad; }
        for(int k=0;k<3;k++) O->p[k]+=v[k]*h;
        /* the ground, and what it's sloping like. It's the lowest corner that touches, measured along the slope */
        const float bot=O->p[1]-O->ext[1],top=spTopBelow(O->p[0],O->p[2],bot,NULL);
        if(bot<top+0.5f){ looseNormal(O,O->p[0],O->p[2],bot,n); const float need=looseReach(O,n)/n[1];
            if(O->p[1]-need<top){ O->p[1]=top+need; touched=1; O->touch=1; looseContact(O,n); if(objOf(h0)!=O) return; } }
        O->slope[0]=touched?-n[0]/n[1]:0.f; O->slope[1]=touched?-n[2]/n[1]:0.f;
        looseWalls(O,was); if(objOf(h0)!=O) return;
        /* its spin, which what it's touching soon takes */
        { const int contact=touched||wasTouch; const float wd=1.f-fminf(1.f,h*(contact?8.f:0.3f)); O->w[0]*=wd; O->w[1]*=wd; O->w[2]*=wd;
          const float w2=O->w[0]*O->w[0]+O->w[1]*O->w[1]+O->w[2]*O->w[2];
          if(w2>1e-10f){ const float wq[4]={O->w[0]*0.5f*h,O->w[1]*0.5f*h,O->w[2]*0.5f*h,0.f}; float dq[4]; qmul(dq,wq,O->q);
              for(int k=0;k<4;k++) O->q[k]+=dq[k];
              const float l=sqrtf(O->q[0]*O->q[0]+O->q[1]*O->q[1]+O->q[2]*O->q[2]+O->q[3]*O->q[3]); if(l>1e-6f) for(int k=0;k<4;k++) O->q[k]/=l; }
          if(contact&&!(O->pub.flags&OBJ_ROUND)&&w2<0.25f) looseSettle(O,h,n); }
        looseExtent(O); }
    looseBodies(O); }
/* two loose things, one of them awake. Balls meet as balls. Anything else meets as the boxes around them, out along whichever way is shortest, and one rests on the other only if its middle is over it: off the edge, it goes */
static void looseMeet(Obj*A,Obj*B){ const float d[3]={B->p[0]-A->p[0],B->p[1]-A->p[1],B->p[2]-A->p[2]}; float n[3]={0.f,0.f,0.f},pen; int vert=0;
    if(A->pub.flags&B->pub.flags&OBJ_ROUND){ const float need=A->radius+B->radius,d2=d[0]*d[0]+d[1]*d[1]+d[2]*d[2]; if(d2>=need*need||d2<1e-8f) return;
        const float l=sqrtf(d2); n[0]=d[0]/l; n[1]=d[1]/l; n[2]=d[2]/l; pen=need-l; vert=fabsf(n[1])>0.7f; }
    else { const float ox=A->ext[0]+B->ext[0]-fabsf(d[0]),oy=A->ext[1]+B->ext[1]-fabsf(d[1]),oz=A->ext[2]+B->ext[2]-fabsf(d[2]); if(ox<=0.f||oy<=0.f||oz<=0.f) return;
        const Obj*Lo=d[1]>=0.f?A:B;
        if(oy<=ox&&oy<=oz&&fabsf(d[0])<Lo->ext[0]&&fabsf(d[2])<Lo->ext[2]){ n[1]=d[1]>=0.f?1.f:-1.f; pen=oy; vert=1; }
        else if(ox<=oz){ n[0]=d[0]>=0.f?1.f:-1.f; pen=ox; }
        else { n[2]=d[2]>=0.f?1.f:-1.f; pen=oz; }
        if(!vert&&pen>0.12f) pen=0.12f; }   /* they ease apart, rather than jump */
    float ia=1.f/A->mass,ib=1.f/B->mass;
    const float rv=(B->pub.vx-A->pub.vx)*n[0]+(B->pub.vy-A->pub.vy)*n[1]+(B->pub.vz-A->pub.vz)*n[2];
    Obj*U=NULL,*L=NULL; if(vert){ U=n[1]>0.f?B:A; L=n[1]>0.f?A:B; }
    if(vert&&(L->pub.asleep||L->touch)){ if(L==A) ia=0.f; else ib=0.f; }   /* what's underneath and standing on something takes none of it: that's the ground's */
    if(A->pub.asleep&&ia>0.f){ if(-rv<LOOSE_WAKE) ia=0.f; else looseWake(A); }
    if(B->pub.asleep&&ib>0.f){ if(-rv<LOOSE_WAKE) ib=0.f; else looseWake(B); }
    if(ia<=0.f&&ib<=0.f) return;
    const float sum=ia+ib,sa=pen*ia/sum,sb=pen*ib/sum;
    looseNudge(A,0,-n[0]*sa); looseNudge(A,2,-n[2]*sa); A->p[1]-=n[1]*sa;
    looseNudge(B,0,n[0]*sb); looseNudge(B,2,n[2]*sb); B->p[1]+=n[1]*sb;
    if(rv<0.f){ const float e=-rv<LOOSE_QUIET?0.f:(A->bounce+B->bounce)*0.5f,j=-(1.f+e)*rv/sum; float*va=&A->pub.vx,*vb=&B->pub.vx;
        for(int k=0;k<3;k++){ va[k]-=j*ia*n[k]; vb[k]+=j*ib*n[k]; }
        /* and they drag on each other */
        float t[3]={vb[0]-va[0],vb[1]-va[1],vb[2]-va[2]}; const float vd=t[0]*n[0]+t[1]*n[1]+t[2]*n[2]; for(int k=0;k<3;k++) t[k]-=vd*n[k];
        const float l=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);
        if(l>1e-5f){ const float jt=fminf((A->grip+B->grip)*0.5f*j,l/sum); for(int k=0;k<3;k++){ va[k]+=t[k]/l*jt*ia; vb[k]-=t[k]/l*jt*ib; } } }
    if(vert){ U->touch=1; U->restOn=objHandleOf(L); }
    if(-rv>=LOOSE_QUIET){ const int ha=objHandleOf(A),hb=objHandleOf(B); looseTell(A,hb,-2,-rv); if(objOf(hb)==B) looseTell(B,ha,-2,-rv); } }
/* is what it's resting on still there? */
static int looseStays(const Obj*O){ if(O->restOn){ const Obj*L=objOf(O->restOn); return L&&L->pub.asleep; }
    return fabsf(spTopBelow(O->p[0],O->p[2],O->p[1]-O->ext[1],O->grid)-O->restTop)<=0.06f; }
/* once a tick: what's awake moves, what's asleep looks at what's under it if anything's changed, loose things meet each other, and whatever's stopped for long enough goes to sleep */
static void objStep(float dt){ if(!g_objNLoose||dt<=0.f) return; const int look=g_objSeen!=g_spStamp; g_objSeen=g_spStamp;
    for(int i=0;i<g_objHi;i++){ Obj*O=&g_obj[i]; if(!O->on||!(O->pub.flags&OBJ_LOOSE)) continue;
        if(O->pub.asleep){ if(!look||looseStays(O)) continue; looseWake(O); }
        looseStep(O,dt); }
    for(int i=0;i<g_objHi;i++){ Obj*A=&g_obj[i]; if(!A->on||!(A->pub.flags&OBJ_LOOSE)||A->pub.asleep) continue;
        for(int j=0;j<g_objHi&&A->on;j++){ Obj*B=&g_obj[j]; if(j==i||!B->on||!(B->pub.flags&OBJ_LOOSE)||(!B->pub.asleep&&j<i)) continue; looseMeet(A,B); } }
    for(int i=0;i<g_objHi;i++){ Obj*O=&g_obj[i]; if(!O->on||!(O->pub.flags&OBJ_LOOSE)||O->pub.asleep) continue; float*v=&O->pub.vx;
        if(O->touch&&!O->turning&&v[0]*v[0]+v[1]*v[1]+v[2]*v[2]<0.0144f){ O->still+=dt;
            if(O->still>0.6f){ v[0]=v[1]=v[2]=0.f; O->w[0]=O->w[1]=O->w[2]=0.f; looseSync(O); looseRests(O); if(O->pub.flags&OBJ_SOLID) objGridMake(O); continue; } }
        else O->still=0.f;
        looseSync(O); } }
/* the box around it, in the world */
static void objBox(const Obj*O,float*c,float*e){ if(O->pub.flags&OBJ_LOOSE){ for(int k=0;k<3;k++){ c[k]=O->p[k]; e[k]=O->ext[k]; } return; }
    const Pack*Q=&g_setPk[O->pk]; const float*m=O->m; const float hx=Q->propEx,hy=(Q->propY1-Q->propY0)*0.5f,hz=Q->propEz,lx=Q->propCx,ly=(Q->propY0+Q->propY1)*0.5f,lz=Q->propCz;
    for(int k=0;k<3;k++){ c[k]=m[12+k]+m[k]*lx+m[4+k]*ly+m[8+k]*lz; e[k]=fabsf(m[k])*hx+fabsf(m[4+k])*hy+fabsf(m[8+k])*hz; } }
/* the first thing a straight line meets, solid or not, by the box around it */
static int apiObjectOnRay(const float*a,const float*b,float*tOut){ float best=2.f; int who=0;
    for(int i=0;i<g_objHi;i++){ const Obj*O=&g_obj[i]; if(!O->on||(O->pub.flags&OBJ_HIDDEN)) continue; float c[3],e[3],t0=0.f,t1=1.f; int out=0; objBox(O,c,e);
        for(int k=0;k<3&&!out;k++){ const float d=b[k]-a[k],lo=c[k]-e[k],hi=c[k]+e[k];
            if(fabsf(d)<1e-7f){ if(a[k]<lo||a[k]>hi) out=1; continue; }
            float ta=(lo-a[k])/d,tb=(hi-a[k])/d; if(ta>tb){ const float s=ta; ta=tb; tb=s; }
            if(ta>t0) t0=ta;
            if(tb<t1) t1=tb;
            if(t0>t1) out=1; }
        if(!out&&t0<best){ best=t0; who=objHandleOf(O); } }
    if(who&&tOut) *tOut=best;
    return who; }
/* what isn't here, from its stand-in. Only in the picture: not in the sun's pass, and never solid */
static void stFarDraw(void){ if(!g_st.on||!g_st.farStem[0]||g_shadowPass) return;
    if(g_st.farPk==-2) stFarReady();
    if(g_st.farPk<0) return;
    static const M4 ident={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; Pack*Q=&g_setPk[g_st.farPk]; const float far2=g_st.farDist*g_st.farDist; float pl[6][4]; const int cullOn=g_cullOn&&g_gameViews.n<=1;
    if(cullOn){ const float*m=g_watVP;
        for(int k=0;k<3;k++) for(int c=0;c<4;c++){ pl[k*2][c]=m[c*4+3]+m[c*4+k]; pl[k*2+1][c]=m[c*4+3]-m[c*4+k]; }
        for(int k=0;k<6;k++){ float l=sqrtf(pl[k][0]*pl[k][0]+pl[k][1]*pl[k][1]+pl[k][2]*pl[k][2]); if(l>1e-6f) for(int c=0;c<4;c++) pl[k][c]/=l; } }
    g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; g_vkOwner=(unsigned)GFX_KIND_FAR<<28;
    for(int i=0;i<g_st.n;i++){ StCell*C=&g_st.c[i]; if(C->farMesh<0||SDL_AtomicGet(&C->state)==ST_HERE) continue; const float dx=C->fx-g_rvx,dz=C->fz-g_rvz; if(dx*dx+dz*dz>far2) continue;
        if(cullOn){ int out=0; for(int k=0;k<6&&!out;k++) if(pl[k][0]*C->fx+pl[k][1]*C->y+pl[k][2]*C->fz+pl[k][3]<-C->fr) out=1;
            if(out) continue; }
        g_propOnly=C->farMesh; drawProp(Q,ident); }
    g_propOnly=-1; }
/* in both passes. The same tests a set's prop gets */
static void objDraw(void){ if(!g_objLive) return;
    float pl[6][4]; const int cullOn=g_cullOn&&!g_shadowPass&&g_gameViews.n<=1;
    if(cullOn){ const float*m=g_watVP;
        for(int k=0;k<3;k++) for(int c=0;c<4;c++){ pl[k*2][c]=m[c*4+3]+m[c*4+k]; pl[k*2+1][c]=m[c*4+3]-m[c*4+k]; }
        for(int k=0;k<6;k++){ float l=sqrtf(pl[k][0]*pl[k][0]+pl[k][1]*pl[k][1]+pl[k][2]*pl[k][2]); if(l>1e-6f) for(int c=0;c<4;c++) pl[k][c]/=l; } }
    for(int i=0;i<g_objHi;i++){ Obj*O=&g_obj[i]; if(!O->on||(O->pub.flags&OBJ_HIDDEN)) continue; Pack*Q=&g_setPk[O->pk];
        if(g_objUnseen[i]&&!g_shadowPass) continue;   /* (11-11) not on this machine's picture */
        if(g_shadowPass){ float dx=O->m[12]-g_sunFocus[0],dz=O->m[14]-g_sunFocus[2],lim=g_shadowExt*1.7f*g_shadowFar;
            if(dx>lim||dx<-lim||dz>lim||dz<-lim||Q->nocast||(O->pub.flags&OBJ_NOCAST)) continue; }
        else if(cullOn){ float sc=sqrtf(O->m[0]*O->m[0]+O->m[1]*O->m[1]+O->m[2]*O->m[2]); if(sc<1e-6f) sc=1.f;
            float hh=Q->effH*0.5f,rr=sqrtf(Q->propR*Q->propR+hh*hh)*sc*1.25f+1.0f,cx=O->m[12]+O->m[0]*Q->propCx+O->m[8]*Q->propCz,cy=O->m[13]+hh*sc,cz=O->m[14]+O->m[2]*Q->propCx+O->m[10]*Q->propCz; int out=0;
            for(int k=0;k<6&&!out;k++) if(pl[k][0]*cx+pl[k][1]*cy+pl[k][2]*cz+pl[k][3]<-rr) out=1;
            if(out) continue; }
        if(O->tinted){   }
        else         {   }
        if(O->tinted){ g_vkUse=0.f; memcpy(g_vkTint,O->tint,12); } else { g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; }
        g_vkOwner=((unsigned)GFX_KIND_OBJECT<<28)|(((unsigned)i&0xffffu)<<12);   /* its slot is its id for as long as it lives, whatever comes and goes around it */
        drawProp(Q,O->m); }
    g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f;
}
/* what the receipt sees. Once any has been made, the count goes in too, so taking the last one out still shows */
static void objHash(void){ if(!g_objEver) return; { float n9=(float)g_objLive; g_shDumpTag="objects"; simHashMix(&n9,1); }
    for(int i=0;i<g_objHi;i++){ const Obj*O=&g_obj[i]; if(!O->on) continue; float f9[8]={O->pub.x,O->pub.y,O->pub.z,O->pub.yaw,O->pub.scale,(float)O->pub.prop,(float)O->pub.flags,(float)O->gen}; g_shDumpTag="object"; simHashMix(f9,8);
        if(O->pub.pitch!=0.f||O->pub.roll!=0.f){ float t9[2]={O->pub.pitch,O->pub.roll}; g_shDumpTag="turned"; simHashMix(t9,2); }
        if(O->pub.flags&OBJ_LOOSE){ float l9[8]={O->pub.vx,O->pub.vy,O->pub.vz,(float)O->pub.asleep,O->q[0],O->q[1],O->q[2],O->q[3]}; g_shDumpTag="loose"; simHashMix(l9,8); } } }

/* a body from the pool: one that's hidden and on no team, the lowest first, so a replay wakes the same one. A graph it has starts again */
static void agBodyReset(int i);
static void apiBodyRise(int i);
static int apiBodyNew(float x,float z,int team){
    for(int i=0;i<g_NS&&i<1024;i++) if(g_slotHide[i]&&!g_slotTeam[i]&&!g_slotBan[i]&&g_slotDrawable[i]){ Slot*s=&g_slots[i];
        s->x=s->tx=x; s->z=s->tz=z; s->rotY=0.f; s->h=0.f; s->spd=0.f; s->state=0; s->moving=0; s->mate=-1; s->tT=0.f; s->stateT=0.f;
        g_slotHide[i]=0; g_slotTeam[i]=team; g_slotFeet[i]=0.f; g_slotAlt[i]=0.f; if(i<BT_MAX) g_bodyTintW[i][0]=0; if(i<1024) g_bodyMirror[i]=0; g_slotBhv[i]=BHV_NONE; g_slotBhvT[i]=0.f; agBodyReset(i); apiBodyRise(i);
        return i; }
    objSay("every body in the pool is awake: bodyNew gave back -1");
    return -1; }
static void faceBodyGone(int i);   /* engine/face.inc */
static void apiDetachAll(int body);   /* engine/gameworld.inc */
static void apiBodyGone(int i){ if(i<0||i>=g_NS||i>=1024) return; g_slotHide[i]=1; g_slotTeam[i]=0; apiBodyRise(i); faceBodyGone(i); apiDetachAll(i); }
/* ---- BODIES THAT STREAM (queue step 12): a pack for a body, asked for by name while the game runs, so a game can have more kinds of people than fit in memory. Its number is good for the whole run. The file's read off the main thread, and at most one goes in a tick. When a game says it's done with one, it's freed once no body has it on, and asking again reads it again */
static int g_heroWear=-1,g_heroWorn=-1;   /* E->heroWear: the pack she's to put on, and the one she has */
static int g_packN=0,g_packBoot=0,g_poseNodes=0;   /* how many packs there are, how many of those were there at boot, and the most nodes the pose loop has room for */
static void bodyPackRead(StBody*B,int k){ stStart(); B->want=0;
    if(g_st.sync){ B->bytes=stBodyBytes(B,&B->len); SDL_AtomicSet(&B->state,ST_READ); return; }
    const int wr=SDL_AtomicGet(&g_st.wr); if(wr-SDL_AtomicGet(&g_st.rd)>=ST_MAXQ){ B->want=1; return; }
    g_st.q[wr%ST_MAXQ]=k|ST_BODYJOB; SDL_AtomicSet(&B->state,ST_READING); SDL_AtomicSet(&g_st.wr,wr+1); }
static void bodyPackInstall(StBody*B){ if(!B->bytes){ objSay("a body's pack couldn't be read"); SDL_AtomicSet(&B->state,ST_AWAY); return; }
    g_packMem=B->bytes; g_packMemLen=B->len;
#ifdef REVA_VK
    if(!g_st.sync) gfxUploadAsync(1);
#endif
    g_loadQuiet=B->said||B->built; B->said=1;
    g_packs[B->pack]=loadPack(B->path);
    g_loadQuiet=0;
#ifdef REVA_VK
    gfxUploadAsync(0);
#endif
    g_packMem=NULL; B->bytes=NULL; SDL_AtomicSet(&B->state,ST_HERE); }
/* once a tick: one in, and one that's finished with out */
static void bodyPackStep(void){ int in=0,out=0;
    { static const char*m=(const char*)1; if(m==(const char*)1) m=getenv("REVA_MEMSAY"); if(m&&g_tickTotal%atoi(m)==0) gfxMemSay(); }
    for(int k=0;k<g_stNBody;k++){ StBody*B=&g_stBody[k]; const int s=SDL_AtomicGet(&B->state);
        if(s==ST_AWAY&&B->want) bodyPackRead(B,k);
        else if(s==ST_READ&&!in){ in=1; bodyPackInstall(B); }
        else if(s==ST_HERE&&B->done&&!out){ int used=(B->pack==g_heroWear||B->pack==g_heroWorn); for(int i=0;i<g_NS&&!used;i++) if(g_slots[i].pack==B->pack&&!g_slotHide[i]) used=1;
            if(used) B->idle=0;
            else if(++B->idle>=8){ out=1; B->done=0; B->idle=0;   /* a few ticks after the last body left it: the frame before still has it in its lists */
                for(int i=0;i<g_NS;i++) if(g_slots[i].pack==B->pack){ g_slots[i].pack=g_packBoot?i%g_packBoot:0; g_slots[i].clip=0; g_slots[i].baseClip=0; g_slots[i].nextClip=0; g_slots[i].baseFrame=0; g_slots[i].nextFrame=0; g_slotDrawable[i]=g_packs[g_slots[i].pack].drawable; }   /* whoever's hidden and had it on goes back to what it had at boot */
                packFree(&g_packs[B->pack]); SDL_AtomicSet(&B->state,ST_AWAY); } } } }
static int apiBodyPack(const char*stem){ if(!stem||!stem[0]||!g_packs) return -1;
    for(int k=0;k<g_stNBody;k++){ StBody*B=&g_stBody[k]; if(strcmp(B->stem,stem)) continue; B->done=0;
        if(SDL_AtomicGet(&B->state)==ST_AWAY) bodyPackRead(B,k);
        if(g_st.sync&&SDL_AtomicGet(&B->state)==ST_READ) bodyPackInstall(B);
        return B->pack; }
    if(g_stNBody>=ST_MAXBODY||g_packN>=1024){ objSay("too many kinds of body asked for: bodyPack gave back -1"); return -1; }
    char p[128]; FILE*t; snprintf(p,sizeof p,"bodies/%.55s.rpack",stem);
    if(!(t=fopen(p,"rb"))){ snprintf(p,sizeof p,"packs/%.55s.rpack",stem); if(!(t=fopen(p,"rb"))){ static int said=0; if(said++<8){ printf("[bodies] no pack called %s\n",stem); fflush(stdout); } return -1; } }
    fclose(t);
    StBody*B=&g_stBody[g_stNBody]; const int k=g_stNBody++; memset(B,0,sizeof *B); snprintf(B->stem,sizeof B->stem,"%s",stem); snprintf(B->path,sizeof B->path,"%s",p); B->pack=g_packN++; memset(&g_packs[B->pack],0,sizeof(Pack));
    bodyPackRead(B,k);
    if(g_st.sync) bodyPackInstall(B);
    return B->pack; }
/* (11-06) a game's own bank of clips (E->bankLoad): read once, and a body graph's borrowing (bankFind) looks there after the engine's own banks */
static int apiBankLoad(const char*stem){ if(!stem||!stem[0]) return 0;
    static Pack bk9[4]; static char bkStem9[4][64]; static int bkN9=0;
    for(int i=0;i<bkN9;i++) if(!strcmp(bkStem9[i],stem)) return (int)bk9[i].nClips;
    if(bkN9>=4||g_nBank>=NBANK){ printf("[bank] no room for %s: it isn't read\n",stem); fflush(stdout); return 0; }
    char p[128]; FILE*t; snprintf(p,sizeof p,"bodies/%.55s.rpack",stem);
    if(!(t=fopen(p,"rb"))){ snprintf(p,sizeof p,"packs/%.55s.rpack",stem); if(!(t=fopen(p,"rb"))){ printf("[bank] no pack called %s\n",stem); fflush(stdout); return 0; } }
    fclose(t);
    bk9[bkN9]=loadPack(p);
    if(!bk9[bkN9].nClips){ printf("[bank] %s has no clips\n",p); fflush(stdout); return 0; }
    snprintf(bkStem9[bkN9],64,"%s",stem); bankAdd(&bk9[bkN9]);
    printf("[bank] %s: %u clips, a bank any body's graph can borrow from\n",p,bk9[bkN9].nClips); fflush(stdout);
    return (int)bk9[bkN9++].nClips; }
/* a person built from measurements, while the game runs (engine/people.inc). The number's like bodyPack's: ask bodyPackReady, put it on with bodyWear, and let it go with bodyPackDone, and after that the number's given out again */
static int apiBodyBuild(const EngineBodySpec*spec,size_t size){ if(!spec||!g_packs) return -1; StBody*B=NULL; int k=-1;
    for(int i=0;i<g_stNBody;i++) if(g_stBody[i].built&&SDL_AtomicGet(&g_stBody[i].state)==ST_AWAY&&!g_stBody[i].want){ B=&g_stBody[i]; k=i; break; }
    if(!B){ if(g_stNBody>=ST_MAXBODY||g_packN>=1024){ objSay("too many kinds of body at once: bodyBuild gave back -1"); return -1; } k=g_stNBody++; B=&g_stBody[k]; memset(B,0,sizeof *B); snprintf(B->stem,sizeof B->stem,"built:%d",k); snprintf(B->path,sizeof B->path,"person"); B->pack=g_packN++; memset(&g_packs[B->pack],0,sizeof(Pack)); B->built=1; }
    memset(&B->spec,0,sizeof B->spec); memcpy(&B->spec,spec,size<sizeof B->spec?size:sizeof B->spec); B->done=0; B->idle=0;
    bodyPackRead(B,k);
    if(g_st.sync) bodyPackInstall(B);
    return B->pack; }
/* the hero puts it on, next frame. She keeps her place and starts again on her clips */
static int apiBodyPackReady(int pack);
static void apiHeroWear(int pack){ if(pack>=0&&pack<g_packN&&apiBodyPackReady(pack)) g_heroWear=pack; }
static const StBody* bodyPackOf(int pack){ for(int k=0;k<g_stNBody;k++) if(g_stBody[k].pack==pack) return &g_stBody[k]; return NULL; }
static int apiBodyPackReady(int pack){ if(pack<0||pack>=g_packN) return 0; const StBody*B=bodyPackOf(pack); return !B||SDL_AtomicGet(&((StBody*)B)->state)==ST_HERE; }
static void apiBodyPackDone(int pack){ StBody*B=(StBody*)bodyPackOf(pack); if(B) B->done=1; }
/* a body puts it on, and starts again on its clips. 0 if it isn't ready yet */
static int apiBodyWear(int i,int pack){ if(i<0||i>=g_NS||i>=1024||!apiBodyPackReady(pack)) return 0; Pack*Pi=&g_packs[pack];
    if(Pi->nNodes>(unsigned)g_poseNodes){ objSay("a body's pack has more bones than the engine has room to pose"); return 0; }
    Slot*s=&g_slots[i]; s->pack=pack; s->clip=0; s->phase=0; s->baseClip=Pi->nDance?Pi->danceIdx[0]:0; s->baseFrame=0; s->nextClip=s->baseClip; s->nextFrame=0;
    g_slotDrawable[i]=Pi->drawable; g_slotBan[i]=0; g_slotNoShow[i]=strstr(Pi->label,"Character")?1:0; agBodyReset(i); apiBodyRise(i);
    return 1; }
