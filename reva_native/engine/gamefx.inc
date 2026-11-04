/* ---- PARTICLES AND DECALS (11-03, engine plan Phase 6) ---------------------------------------------------------------------------------------------------------------------
 * Two fixed pools: when one's full the oldest makes way, so a big explosion can't cost more than the budget, and nothing is ever allocated in a frame. They're soft quads through the renderer's cheap effect path: a pixel of one costs next to nothing, so it's the count that's budgeted, and the count is small enough for the Pi.
 * They roll their own dice, so a burst can't change what the game's dice say next, and they're not in the sim. They do go in the packet, stepped by the tick, so a replay draws the same ones at any frame rate. */
#ifdef REVA_PACKET
#define FXP_MAX 512
#define FXD_MAX 64
typedef struct { float p[3],v[3],c[3],t,life,size,grav; } FxPart;
typedef struct { float p[3],c[4],t,life,r; } FxDecal;
static FxPart g_fxP[FXP_MAX]; static int g_fxPN=0;
static FxDecal g_fxD[FXD_MAX]; static int g_fxDN=0;
static unsigned g_fxDice=12345u;
static float fxRnd(void){ g_fxDice=g_fxDice*1103515245u+12345u; return (float)((g_fxDice>>16)&0x7fffu)/32768.f; }
/* n of them, thrown out from a point at up to this speed; they fade over life seconds and fall by grav */
static void apiParticles(float x,float y,float z,int n,float speed,float life,float size,float r,float g,float b,float grav){
    if(n>FXP_MAX) n=FXP_MAX;
    for(int i=0;i<n;i++){ FxPart*P=&g_fxP[g_fxPN]; g_fxPN=(g_fxPN+1)%FXP_MAX;
        float a=fxRnd()*6.28318f,u=fxRnd()*2.f-1.f,h=sqrtf(1.f-u*u),s=speed*(0.3f+0.7f*fxRnd());
        P->p[0]=x; P->p[1]=y; P->p[2]=z; P->v[0]=cosf(a)*h*s; P->v[1]=u*s; P->v[2]=sinf(a)*h*s;
        P->c[0]=r; P->c[1]=g; P->c[2]=b; P->t=0.f; P->life=life*(0.6f+0.4f*fxRnd()); P->size=size; P->grav=grav; } }
/* a mark on a surface, lying flat. It stays for this long and fades over the last quarter of it */
static void apiDecal(float x,float y,float z,float radius,float r,float g,float b,float a,float seconds){
    FxDecal*D=&g_fxD[g_fxDN]; g_fxDN=(g_fxDN+1)%FXD_MAX;
    D->p[0]=x; D->p[1]=y+0.03f; D->p[2]=z; D->c[0]=r; D->c[1]=g; D->c[2]=b; D->c[3]=a; D->t=0.f; D->life=seconds>0.f?seconds:0.001f; D->r=radius; }
static void fxStep(float dt){
    for(int i=0;i<FXP_MAX;i++){ FxPart*P=&g_fxP[i]; if(P->life<=0.f) continue;
        P->t+=dt; if(P->t>=P->life){ P->life=0.f; continue; }
        P->v[1]-=P->grav*dt; for(int k=0;k<3;k++) P->p[k]+=P->v[k]*dt; }
    for(int i=0;i<FXD_MAX;i++){ FxDecal*D=&g_fxD[i]; if(D->life<=0.f) continue; D->t+=dt; if(D->t>=D->life) D->life=0.f; } }
static float* fxQuad(float*o,const float*c,const float*ax,const float*ay,float r,float g,float b,float a){
    static const float Q[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    for(int k=0;k<6;k++){ float u=Q[k][0],w=Q[k][1];
        o[0]=c[0]+ax[0]*u+ay[0]*w; o[1]=c[1]+ax[1]*u+ay[1]*w; o[2]=c[2]+ax[2]*u+ay[2]*w; o[3]=r; o[4]=g; o[5]=b; o[6]=a; o[7]=u; o[8]=w; o+=9; }
    return o; }
/* decals lie flat. Particles face the lens, so under split screen each picture gets its own copy, facing its own */
static float* fxParticles(float*o,const float*rt,const float*up){
    for(int i=0;i<FXP_MAX;i++){ const FxPart*P=&g_fxP[i]; if(P->life<=0.f) continue;
        float s=P->size,ax[3]={rt[0]*s,rt[1]*s,rt[2]*s},ay[3]={up[0]*s,up[1]*s,up[2]*s};
        o=fxQuad(o,P->p,ax,ay,P->c[0],P->c[1],P->c[2],1.f-P->t/P->life); }
    return o; }
static void fxDrawAll(const float*view){
    static float v[(FXP_MAX+FXD_MAX)*6*9]; float*o=v;
    for(int i=0;i<FXD_MAX;i++){ const FxDecal*D=&g_fxD[i]; if(D->life<=0.f) continue;
        float rest=(D->life-D->t)/(D->life*0.25f),a=D->c[3]*(rest<1.f?rest:1.f),ax[3]={D->r,0.f,0.f},ay[3]={0.f,0.f,D->r};
        o=fxQuad(o,D->p,ax,ay,D->c[0],D->c[1],D->c[2],a); }
    if(g_gameViews.n>1){ int n=(int)((o-v)/9); if(n>0) gfxFxTris(v,n,GFX_FX_SOFT,0);
        for(int k=0;k<g_gameViews.n;k++){ const float*e=g_gameViews.e[k],*a=g_gameViews.a[k]; float fw[3]={a[0]-e[0],a[1]-e[1],a[2]-e[2]},l=sqrtf(fw[0]*fw[0]+fw[1]*fw[1]+fw[2]*fw[2]); if(l<1e-5f) continue; fw[0]/=l; fw[1]/=l; fw[2]/=l;
            float rt[3]={-fw[2],0.f,fw[0]},r=sqrtf(rt[0]*rt[0]+rt[2]*rt[2]); if(r<1e-5f){ rt[0]=1.f; rt[2]=0.f; r=1.f; } rt[0]/=r; rt[2]/=r;
            const float up[3]={rt[1]*fw[2]-rt[2]*fw[1],rt[2]*fw[0]-rt[0]*fw[2],rt[0]*fw[1]-rt[1]*fw[0]};
            n=(int)((fxParticles(v,rt,up)-v)/9); if(n>0) gfxFxTrisView(v,n,GFX_FX_SOFT,0,k); }
        return; }
    const float rt[3]={view[0],view[4],view[8]},up[3]={view[1],view[5],view[9]};
    o=fxParticles(o,rt,up);
    int n=(int)((o-v)/9); if(n>0) gfxFxTris(v,n,GFX_FX_SOFT,0); }
#else
static void apiParticles(float x,float y,float z,int n,float speed,float life,float size,float r,float g,float b,float grav){ (void)x;(void)y;(void)z;(void)n;(void)speed;(void)life;(void)size;(void)r;(void)g;(void)b;(void)grav; }
static void apiDecal(float x,float y,float z,float radius,float r,float g,float b,float a,float seconds){ (void)x;(void)y;(void)z;(void)radius;(void)r;(void)g;(void)b;(void)a;(void)seconds; }
#endif
