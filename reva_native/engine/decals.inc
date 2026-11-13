/* ---- DECALS, PROJECTED (queue step 24, 11-05) ----------------------------------------------------------------------------------------------------------------------------
 * A game's decal was a soft round quad lying flat (engine/gamefx.inc, E->decal): right on a floor, and floating in the air over the edge of a step. This is the other kind:
 * a mark THROWN at the world. It's a box, and whatever of the scene lies inside the box takes the mark, a floor, a stair, a wall, a prop, so a splat folds over a kerb and a
 * scorch goes up the wall it's against. The renderer does it by reading the scene's depth mid-frame (gfxset `decals`, a PC's); where a machine doesn't, the same decal is a flat
 * mark across the box's middle, so the packet's the same on every machine and the Pi keeps the cheap one.
 *
 *   E->decalAt(&d, sizeof d)     a game's: an EngineDecal (engine.h)
 *   decal <kind> x y z size [dx dy dz] [along=L] [deep=D] [turn=DEG] [rgb=r,g,b] [a=A] [pic=<prop>]      a set's, there for as long as the set is
 *       kind: mark splat scorch wet stripe crack picture. dx dy dz: the way it's thrown, into the surface (0 -1 0, at a floor, if it's left out)
 *
 * KINDS. mark, splat, scorch, wet and crack STAIN: they multiply what's under them, so they're lit and shadowed exactly as it is, and a colour near white hardly shows.
 * A stain's colour is what a white floor would look like under it: 0.65 is wet stone, 0.3 is nearly black, because it's the light that's multiplied.
 * stripe and picture are PAINT: laid over, lit by the sun and the sky with the sun's shadow, and not by the set's lamps (not built). A picture shows a prop's first texture.
 * A fixed pool, the oldest of a game's making way; a set's own stay until the set goes. They roll their own dice. ⛔ Picture, never rules: nothing here is in [simhash]. */
#define DC_MAX 192
typedef struct { int on,fromSet,kind; float m[16],c[4],t,life,seed; unsigned tex; } Dc;
static Dc g_dc[DC_MAX]; static int g_dcAt=0; static unsigned g_dcDice=7u;
static const char*const dcKinds[]={"mark","splat","scorch","wet","stripe","crack","picture"};
static float dcRnd(void){ g_dcDice=g_dcDice*1103515245u+12345u; return (float)((g_dcDice>>16)&0x7fffu)/32768.f; }
/* the box: p its middle, on the surface; d the way it was thrown; how far across and along the surface, how deep through it each way, and a turn about the throw.
 * Its own z is the top of a picture: up a wall, and away from someone at +z looking at a floor. Its x is that person's right. ⛔ The first frame here took any two
 * axes across the throw, and a picture on a wall came out on its side and mirrored */
static void dcBox(float*m,const float*p,const float*d,float across,float along,float deep,float turnDeg){
    float y[3]={-d[0],-d[1],-d[2]}, l=sqrtf(y[0]*y[0]+y[1]*y[1]+y[2]*y[2]); if(l<1e-6f){ y[0]=0.f; y[1]=1.f; y[2]=0.f; l=1.f; } y[0]/=l; y[1]/=l; y[2]/=l;
    float r[3]={0.f,1.f,0.f}; if(fabsf(y[1])>0.9f){ r[1]=0.f; r[2]=-1.f; }
    const float ry=r[0]*y[0]+r[1]*y[1]+r[2]*y[2]; float z[3]={r[0]-y[0]*ry,r[1]-y[1]*ry,r[2]-y[2]*ry}; l=sqrtf(z[0]*z[0]+z[1]*z[1]+z[2]*z[2]); z[0]/=l; z[1]/=l; z[2]/=l;
    float x[3]={z[1]*y[2]-z[2]*y[1],z[2]*y[0]-z[0]*y[2],z[0]*y[1]-z[1]*y[0]};
    const float a=turnDeg*0.0174532925f, ca=cosf(a), sa=sinf(a);
    memset(m,0,64); m[15]=1.f;
    for(int k=0;k<3;k++){ m[k]=(x[k]*ca+z[k]*sa)*across; m[4+k]=y[k]*deep*2.f; m[8+k]=(z[k]*ca-x[k]*sa)*along; m[12+k]=p[k]; } }
static Dc* dcNew(int fromSet){
    for(int k=0;k<DC_MAX;k++){ Dc*D=&g_dc[(g_dcAt+k)%DC_MAX]; if(!D->on){ g_dcAt=(g_dcAt+k+1)%DC_MAX; return D; } }
    if(fromSet) return NULL;                                   /* full of what's still there: a set's line is said and left out */
    for(int k=0;k<DC_MAX;k++){ Dc*D=&g_dc[(g_dcAt+k)%DC_MAX]; if(!D->fromSet){ g_dcAt=(g_dcAt+k+1)%DC_MAX; return D; } }   /* the oldest of a game's makes way */
    return NULL; }
static int dcPut(int fromSet,int kind,const float*p,const float*d,float size,float along,float deep,float turn,const float*rgba,float seconds,const char*pic,unsigned tex){
    if(kind<0||kind>6||!(size>0.f)) return 0;
    Dc*D=dcNew(fromSet); if(!D) return 0;
    memset(D,0,sizeof *D); D->on=1; D->fromSet=fromSet; D->kind=kind; D->seed=dcRnd()*8.f; memcpy(D->c,rgba,16); D->life=seconds>0.f?seconds:-1.f;
    dcBox(D->m,p,d,size,along>0.f?along:size,deep>0.f?deep:size*0.5f,turn);
    if(kind==6&&pic&&pic[0]){ const int pi=setPack(pic); if(pi>=0&&g_setPk[pi].nTex>0) D->tex=g_setPk[pi].tex[0]; }
    if(kind==6&&tex) D->tex=tex;   /* (11-12) a game's own image: what it paints into it later shows here */
    return 1; }
static unsigned sprImageTex(int im);   /* engine/sprite.inc: a game image's texture */
static void apiDecalAt(const EngineDecal*d,size_t size){
    if(!d) return;
    EngineDecal q; memset(&q,0,sizeof q); memcpy(&q,d,size<sizeof q?size:sizeof q);
    const float p[3]={q.x,q.y,q.z}, w[3]={q.dx,q.dy,q.dz}, c[4]={q.r,q.g,q.b,q.a};
    const unsigned tx=q.image>0?sprImageTex(q.image):0u;
    (void)dcPut(0,q.kind,p,w,q.size,q.along,q.deep,q.turn,c,q.seconds,q.picture,tx); }
static void apiDecalsClear(void){ for(int i=0;i<DC_MAX;i++) if(!g_dc[i].fromSet) g_dc[i].on=0; }
static void dcSetClear(void){ for(int i=0;i<DC_MAX;i++) g_dc[i].on=0; }   /* a set going takes every decal with it, a game's too: they were marks on that set */
/* `decal <kind> x y z size [dx dy dz] [along= deep= turn= rgb=r,g,b a= pic=<prop>]` */
static void dcSetLine(const char*ln){
    char kn[32]; float v[7]={0,0,0,0,0,-1,0}; int n=sscanf(ln,"decal %31s %f %f %f %f %f %f %f",kn,v,v+1,v+2,v+3,v+4,v+5,v+6), kind=-1;
    for(int i=0;i<7;i++) if(n>=1&&!strcmp(kn,dcKinds[i])) kind=i;
    if(n<5||kind<0){ printf("[set] decal: wants `decal <kind> x y z size [dx dy dz] [along= deep= turn= rgb=r,g,b a= pic=<prop>]`, and a kind is one of:"); for(int i=0;i<7;i++) printf(" %s",dcKinds[i]); printf(" -- %s",ln); if(!strchr(ln,'\n')) printf("\n"); return; }
    if(n<8){ v[4]=0.f; v[5]=-1.f; v[6]=0.f; }
    float along=0.f,deep=0.f,turn=0.f,c[4]={0.05f,0.04f,0.04f,0.85f}; char pic[64]=""; const char*q;
    if(kind==4||kind==6){ c[0]=c[1]=c[2]=0.92f; c[3]=1.f; } if(kind==3){ c[0]=0.60f; c[1]=0.62f; c[2]=0.67f; c[3]=1.f; }
    if((q=strstr(ln," along="))) sscanf(q+7,"%f",&along);
    if((q=strstr(ln," deep="))) sscanf(q+6,"%f",&deep);
    if((q=strstr(ln," turn="))) sscanf(q+6,"%f",&turn);
    if((q=strstr(ln," rgb="))) sscanf(q+5,"%f,%f,%f",c,c+1,c+2);
    if((q=strstr(ln," a="))) sscanf(q+3,"%f",c+3);
    if((q=strstr(ln," pic="))) sscanf(q+5,"%63s",pic);
    if(!dcPut(1,kind,v,v+4,v[3],along,deep,turn,c,0.f,pic,0u)) printf("[set] decal: the set has more than %d, the rest are left out\n",DC_MAX);
    else printf("[set] decal %s at %.2f,%.2f,%.2f, %.2f across\n",kn,v[0],v[1],v[2],v[3]); }
static void dcStepAll(float dt){
    for(int i=0;i<DC_MAX;i++){ Dc*D=&g_dc[i]; if(!D->on||D->life<=0.f) continue; D->t+=dt; if(D->t>=D->life) D->on=0; } }
static void dcDrawAll(void){
    for(int i=0;i<DC_MAX;i++){ const Dc*D=&g_dc[i]; if(!D->on) continue;
        GfxDecal g; memset(&g,0,sizeof g); memcpy(g.m,D->m,64); memcpy(g.col,D->c,16); g.kind=(float)D->kind; g.seed=D->seed; g.tex=D->tex;
        g.fade=1.f; if(D->life>0.f){ const float rest=(D->life-D->t)/(D->life*0.25f); g.fade=rest<1.f?(rest>0.f?rest:0.f):1.f; }
        gfxDecal(&g); } }
