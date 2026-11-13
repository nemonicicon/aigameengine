/* ---- PARTICLES, A GENERAL SYSTEM (queue step 24, 11-05) ------------------------------------------------------------------------------------------------------------------
 * The engine had three things that weren't one: film's cigarette smoke (engine/smoke.inc, a body's hand and mouth), a game's burst of round dots (engine/gamefx.inc) and line
 * tracers. Film asked for what it couldn't reach: "no real smoke, flame or sparks ... alpha and additive particles it can reach, and an emitter that can ride a mover: a rocket's
 * exhaust, pyro jets, fuse sparks". This is the one system under all of that.
 *
 * A KIND says what an emitter makes (EngineParticles, engine.h): how many a second, how long each lives, how fast and how wide it's thrown, how it falls and how the air stops
 * it, its size and colour at birth and at death, whether it's a round sprite or a streak along its own going, and whether it's laid over the picture (smoke, dust) or added
 * to it (fire, sparks). The engine has kinds of its own by name (ptKinds below), to use as they are or to copy and change.
 * AN EMITTER is a kind at a place, pointing a way. A game makes one (E->emitter), a set says one (`emitter`), and a set's emitter can ride a mover, so what the timeline
 * moves smokes where it goes. A BURST is n at once.
 *
 *   emitter <kind> x y z [dx dy dz] [rate=N] [size=S] [life=S] [speed=S] [spread=DEG] [rgb=r,g,b] [on=N]
 *       on=N: it rides the set's Nth `mover` (0 the first): x y z is then a place on the mover, and it stops while the mover's hidden
 *
 * ONE POOL, with a budget by the machine (gfxset `particles`): when it's full the oldest makes way, so nothing is allocated in a frame and no burst costs more than the
 * budget. They roll their own dice and are stepped by the tick (a game) or the exposure clock (film), so they're in the packet and a replay draws the same ones.
 * ⛔ They're picture, never rules: nothing here is in [simhash].
 * Drawn as quads facing the lens through the renderer's effect path, the laid-over ones far to near. SOFT (gfxset `softfx`, a PC's): one fades out where it meets a surface
 * instead of ending in a hard line across it. */
#define PT_MAX 16384
#define PE_MAX 96
typedef struct { float p[3],v[3],t,life,rot,spin,size0,size1,c0[4],c1[4],grav,drag,stretch,wind,curl; unsigned char add,soft; } Pt;
typedef struct { int on; EngineParticles k; float p[3],d[3],acc; int mover; float off[3],offd[3]; unsigned gen; int fromSet; } PtEmit;
static Pt *g_pt=NULL; static int g_ptAt=0, g_ptCap=0, g_ptLive=0;
static PtEmit g_pe[PE_MAX];
static unsigned g_ptDice=902u; static float g_ptClock=0.f;
static float ptRnd(void){ g_ptDice=g_ptDice*1103515245u+12345u; return (float)((g_ptDice>>16)&0x7fffu)/32768.f; }
/*                              rate  life      speed     spread grav  drag  size       colour at birth          colour at death          stretch spin wind curl jitter add soft */
static const struct { const char*name; EngineParticles k; } ptKinds[]={
    {"smoke",    { 28.f, 2.2f,3.6f, 0.25f,0.6f,  16.f, -0.55f,0.5f, 0.10f,0.55f, 0.42f,0.42f,0.44f,0.34f, 0.60f,0.60f,0.62f,0.f,  0.f, 0.6f, 1.0f,0.9f, 0.04f, 0,1}},
    {"fire",     { 70.f, 0.35f,0.8f, 0.6f,1.5f,  14.f, -2.2f, 1.2f, 0.16f,0.03f, 2.6f,1.25f,0.30f,0.90f, 1.6f,0.20f,0.02f,0.f,   0.f, 2.0f, 0.5f,0.6f, 0.06f, 1,1}},
    {"sparks",   { 60.f, 0.5f,1.3f,  2.5f,6.5f,  35.f,  9.8f, 0.6f, 0.014f,0.006f, 3.2f,2.2f,0.9f,1.f,    2.2f,0.5f,0.05f,0.f,   0.035f,0.f, 0.2f,0.f,  0.01f, 1,0}},
    {"steam",    { 36.f, 1.2f,2.4f,  0.8f,1.6f,  12.f, -0.9f, 1.4f, 0.07f,0.42f, 0.92f,0.94f,0.96f,0.30f, 1.f,1.f,1.f,0.f,       0.f, 0.4f, 1.0f,0.6f, 0.03f, 0,1}},
    {"dust",     { 22.f, 1.6f,3.0f,  0.2f,0.9f,  70.f, -0.05f,1.6f, 0.08f,0.50f, 0.62f,0.55f,0.44f,0.26f, 0.70f,0.64f,0.54f,0.f, 0.f, 0.3f, 1.0f,0.5f, 0.20f, 0,1}},
    {"embers",   { 14.f, 1.6f,3.4f,  0.3f,1.1f,  40.f, -0.7f, 0.5f, 0.016f,0.006f, 3.0f,1.3f,0.25f,1.f,   1.4f,0.25f,0.03f,0.f,  0.f, 0.f,  1.0f,1.4f, 0.10f, 1,0}},
    {"fountain", {220.f, 1.0f,1.6f,  4.2f,5.6f,   7.f,  9.8f, 0.2f, 0.022f,0.034f, 0.72f,0.84f,0.95f,0.70f, 0.86f,0.92f,1.f,0.f,  0.012f,0.f, 0.3f,0.f,  0.02f, 0,0}},
    {"snow",     { 90.f, 5.0f,8.0f,  0.2f,0.6f, 180.f,  0.9f, 1.2f, 0.016f,0.016f, 1.f,1.f,1.f,0.9f,       1.f,1.f,1.f,0.f,       0.f, 0.f,  1.0f,0.5f, 4.00f, 0,0}},
    {"exhaust",  {160.f, 0.5f,1.1f,  5.0f,9.0f,   9.f, -0.4f, 1.8f, 0.10f,0.48f, 3.0f,1.7f,0.5f,0.85f,     0.42f,0.42f,0.46f,0.f, 0.f, 1.0f, 0.4f,0.5f, 0.04f, 1,1}},
    {"pyro",     {260.f, 0.35f,0.7f, 7.0f,11.f,   8.f,  4.0f, 1.0f, 0.03f,0.012f, 3.4f,2.4f,1.3f,1.f,      2.0f,0.6f,0.1f,0.f,    0.022f,0.f, 0.1f,0.f,  0.02f, 1,0}},
    {"fuse",     { 90.f, 0.2f,0.5f,  0.8f,2.4f, 180.f,  6.0f, 2.0f, 0.010f,0.004f, 3.4f,2.6f,1.2f,1.f,     2.4f,0.6f,0.08f,0.f,   0.03f, 0.f, 0.1f,0.f,  0.004f,1,0}},
    {"rain",     {700.f, 0.45f,0.7f, 15.f,19.f,   3.f, 12.0f, 0.0f, 0.007f,0.007f, 0.72f,0.78f,0.88f,0.45f, 0.72f,0.78f,0.88f,0.35f, 0.045f,0.f, 0.5f,0.f,  5.00f, 0,0}},   /* thrown down from a box of air 10 across: put it overhead, pointing down */
};
static const EngineParticles* ptKind(const char*name){
    if(name) for(unsigned i=0;i<sizeof ptKinds/sizeof ptKinds[0];i++) if(!strcmp(name,ptKinds[i].name)) return &ptKinds[i].k;
    return NULL; }
static void ptPool(void){
    int want=g_gfxParticles>0?g_gfxParticles:1024; if(want>PT_MAX) want=PT_MAX;
    if(g_pt&&want==g_ptCap) return;
    Pt*n=(Pt*)calloc((size_t)want,sizeof(Pt)); if(!n) return;
    free(g_pt); g_pt=n; g_ptCap=want; g_ptAt=0; g_ptLive=0; }
/* a perpendicular pair to a direction, for a cone */
static void ptFrame(const float*d,float*u,float*w){
    float a[3]={0.f,1.f,0.f}; if(fabsf(d[1])>0.95f){ a[0]=1.f; a[1]=0.f; }
    u[0]=a[1]*d[2]-a[2]*d[1]; u[1]=a[2]*d[0]-a[0]*d[2]; u[2]=a[0]*d[1]-a[1]*d[0];
    float l=sqrtf(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]); if(l<1e-6f){ u[0]=1.f; u[1]=u[2]=0.f; l=1.f; } u[0]/=l; u[1]/=l; u[2]/=l;
    w[0]=d[1]*u[2]-d[2]*u[1]; w[1]=d[2]*u[0]-d[0]*u[2]; w[2]=d[0]*u[1]-d[1]*u[0]; }
static void ptSpawn(const EngineParticles*k,const float*p,const float*d,float age){
    if(!g_pt) ptPool();
    if(!g_pt) return;
    Pt*P=&g_pt[g_ptAt]; g_ptAt=(g_ptAt+1)%g_ptCap;
    float dl=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]), dn[3]={0.f,1.f,0.f}; if(dl>1e-6f){ dn[0]=d[0]/dl; dn[1]=d[1]/dl; dn[2]=d[2]/dl; }
    float u[3],w[3]; ptFrame(dn,u,w);
    /* a way inside the cone: even over its cap */
    float half=k->spread*0.0174532925f; if(half>3.14159f) half=3.14159f;
    float ct=1.f-ptRnd()*(1.f-cosf(half)), st=sqrtf(fmaxf(0.f,1.f-ct*ct)), ph=ptRnd()*6.2831853f, cx=cosf(ph)*st, sx=sinf(ph)*st;
    float sp=k->speedMin+(k->speedMax-k->speedMin)*ptRnd();
    for(int c=0;c<3;c++){ P->v[c]=(dn[c]*ct+u[c]*cx+w[c]*sx)*sp; P->p[c]=p[c]+(ptRnd()*2.f-1.f)*k->jitter+P->v[c]*age; }
    P->t=age; P->life=k->lifeMin+(k->lifeMax-k->lifeMin)*ptRnd(); if(P->life<0.02f) P->life=0.02f;
    P->rot=ptRnd()*6.2831853f; P->spin=k->spin*(ptRnd()*2.f-1.f);
    P->size0=k->size0; P->size1=k->size1; P->grav=k->grav; P->drag=k->drag; P->stretch=k->stretch; P->wind=k->wind; P->curl=k->curl;
    P->c0[0]=k->r0; P->c0[1]=k->g0; P->c0[2]=k->b0; P->c0[3]=k->a0; P->c1[0]=k->r1; P->c1[1]=k->g1; P->c1[2]=k->b1; P->c1[3]=k->a1;
    P->add=(unsigned char)(k->add?1:0); P->soft=(unsigned char)(k->soft?1:0); }
/* a kind from a game: only as much of it as the game's own struct has, so an older game's shorter one still reads */
static void ptTake(EngineParticles*o,const EngineParticles*k,size_t size){ memset(o,0,sizeof *o); if(k) memcpy(o,k,size<sizeof *o?size:sizeof *o); }
static int apiEmitter(const EngineParticles*kind,size_t size,float x,float y,float z,float dx,float dy,float dz){
    if(!kind) return 0;
    for(int i=0;i<PE_MAX;i++){ PtEmit*E9=&g_pe[i]; if(E9->on) continue;
        unsigned g=E9->gen+1u; memset(E9,0,sizeof *E9); E9->gen=g; E9->on=1; ptTake(&E9->k,kind,size);
        E9->p[0]=x; E9->p[1]=y; E9->p[2]=z; E9->d[0]=dx; E9->d[1]=dy; E9->d[2]=dz; E9->mover=-1;
        return (int)(((g&0xffffu)<<8)|(unsigned)i)+1; }
    return 0; }
static PtEmit* ptOf(int h){ if(h<=0) return NULL; unsigned v=(unsigned)(h-1), i=v&0xffu; if(i>=PE_MAX) return NULL; PtEmit*E9=&g_pe[i]; return (E9->on&&(E9->gen&0xffffu)==(v>>8))?E9:NULL; }
static void apiEmitterAt(int h,float x,float y,float z,float dx,float dy,float dz){ PtEmit*E9=ptOf(h); if(!E9) return; E9->p[0]=x; E9->p[1]=y; E9->p[2]=z; E9->d[0]=dx; E9->d[1]=dy; E9->d[2]=dz; }
static void apiEmitterRate(int h,float r){ PtEmit*E9=ptOf(h); if(E9) E9->k.rate=r<0.f?0.f:r; }
static void apiEmitterGone(int h){ PtEmit*E9=ptOf(h); if(E9) E9->on=0; }
static void apiBurst(const EngineParticles*kind,size_t size,float x,float y,float z,float dx,float dy,float dz,int n){
    if(!kind||n<=0) return;
    EngineParticles k; ptTake(&k,kind,size); const float p[3]={x,y,z},d[3]={dx,dy,dz};
    if(n>2048) n=2048;
    for(int i=0;i<n;i++) ptSpawn(&k,p,d,0.f); }
static const EngineParticles* apiParticleKind(const char*name){ return ptKind(name); }
/* a set's own emitters go when the set does */
static void ptSetClear(void){ for(int i=0;i<PE_MAX;i++) if(g_pe[i].on&&g_pe[i].fromSet) g_pe[i].on=0; }
/* `emitter <kind> x y z [dx dy dz] [key=value ...]` */
static void ptSetLine(const char*ln){
    char kn[32]; float v[6]={0,0,0,0,1,0}; int n=sscanf(ln,"emitter %31s %f %f %f %f %f %f",kn,v,v+1,v+2,v+3,v+4,v+5);
    const EngineParticles*k0=(n>=4)?ptKind(kn):NULL;
    if(!k0){ printf("[set] emitter: wants `emitter <kind> x y z [dx dy dz] [rate= size= life= speed= spread= rgb=r,g,b on=N]`, and a kind is one of:"); for(unsigned i=0;i<sizeof ptKinds/sizeof ptKinds[0];i++) printf(" %s",ptKinds[i].name); printf(" -- %s",ln); if(!strchr(ln,'\n')) printf("\n"); return; }
    if(n<7){ v[3]=0.f; v[4]=1.f; v[5]=0.f; }
    int h=apiEmitter(k0,sizeof *k0,v[0],v[1],v[2],v[3],v[4],v[5]); PtEmit*E9=ptOf(h);
    if(!E9){ printf("[set] emitter: the set has more than %d, the rest are ignored\n",PE_MAX); return; }
    E9->fromSet=1; EngineParticles*k=&E9->k; const char*q; float f,r,g,b; int m;
    if((q=strstr(ln," rate="))&&sscanf(q+6,"%f",&f)==1) k->rate=f<0.f?0.f:f;
    if((q=strstr(ln," size="))&&sscanf(q+6,"%f",&f)==1&&k->size0>1e-6f){ float s=f/fmaxf(k->size0,k->size1); k->size0*=s; k->size1*=s; k->jitter*=s; }   /* its biggest, in world units */
    if((q=strstr(ln," life="))&&sscanf(q+6,"%f",&f)==1&&f>0.f){ float s=f/k->lifeMax; k->lifeMin*=s; k->lifeMax=f; }
    if((q=strstr(ln," speed="))&&sscanf(q+7,"%f",&f)==1&&f>=0.f){ float s=k->speedMax>1e-6f?f/k->speedMax:0.f; k->speedMin*=s; k->speedMax=f; }
    if((q=strstr(ln," spread="))&&sscanf(q+8,"%f",&f)==1) k->spread=f;
    if((q=strstr(ln," rgb="))&&sscanf(q+5,"%f,%f,%f",&r,&g,&b)==3){ k->r0*=r; k->g0*=g; k->b0*=b; k->r1*=r; k->g1*=g; k->b1*=b; }
    if((q=strstr(ln," on="))&&sscanf(q+4,"%d",&m)==1&&m>=0){ E9->mover=m; memcpy(E9->off,E9->p,12); memcpy(E9->offd,E9->d,12); }
    printf("[set] emitter %s at %.2f,%.2f,%.2f: %.0f a second%s\n",kn,v[0],v[1],v[2],k->rate,E9->mover>=0?", riding a mover":""); }
/* the air: the set's wind, and a slow swirl that has no source or sink in it, so smoke curls and doesn't pile up */
static void ptCurl(const float*p,float t,float*o){
    o[0]=sinf(p[1]*1.7f+t*0.9f)+cosf(p[2]*1.3f-t*0.7f); o[1]=sinf(p[2]*1.9f+t*0.6f)*0.5f; o[2]=cosf(p[0]*1.5f+t*0.8f)+sinf(p[1]*1.1f-t*0.5f); }
static void ptStepAll(float dt){
    if(dt<=0.f) return;
    if(dt>0.1f) dt=0.1f;
    g_ptClock+=dt;
    /* emitters first: what one makes this step is spread across it, so a fast one leaves a line and not a row of clumps */
    for(int i=0;i<PE_MAX;i++){ PtEmit*E9=&g_pe[i]; if(!E9->on) continue;
        if(E9->mover>=0){ const SetInst*I=NULL; for(int s=0;s<g_setNI;s++) if(g_setI[s].mover==E9->mover){ I=&g_setI[s]; break; }
            if(!I||I->hid){ E9->acc=0.f; continue; }
            const float*m=I->m, *o=E9->off, *d=E9->offd;
            for(int c=0;c<3;c++){ E9->p[c]=m[c]*o[0]+m[4+c]*o[1]+m[8+c]*o[2]+m[12+c]; E9->d[c]=m[c]*d[0]+m[4+c]*d[1]+m[8+c]*d[2]; } }
        if(E9->k.rate<=0.f) continue;
        E9->acc+=E9->k.rate*dt; int n=(int)E9->acc; if(n<=0) continue; E9->acc-=(float)n; if(n>512) n=512;
        for(int j=0;j<n;j++) ptSpawn(&E9->k,E9->p,E9->d,dt*((float)j+0.5f)/(float)n); }
    if(!g_pt) return;
    int live=0;
    for(int i=0;i<g_ptCap;i++){ Pt*P=&g_pt[i]; if(P->life<=0.f) continue;
        P->t+=dt; if(P->t>=P->life){ P->life=0.f; continue; }
        live++;
        float k=expf(-P->drag*dt);
        P->v[0]*=k; P->v[1]=P->v[1]*k-P->grav*dt; P->v[2]*=k;
        if(P->wind>0.f) for(int c=0;c<3;c++) P->v[c]+=(g_windV[c]-P->v[c])*fminf(1.f,P->wind*dt*1.5f)*(c==1?0.f:1.f);
        if(P->curl>0.f){ float cu[3]; ptCurl(P->p,g_ptClock,cu); float a=P->curl*dt*0.6f*fminf(1.f,P->t*2.f); for(int c=0;c<3;c++) P->v[c]+=cu[c]*a; }
        for(int c=0;c<3;c++) P->p[c]+=P->v[c]*dt;
        P->rot+=P->spin*dt; }
    g_ptLive=live; }
static float g_ptSortKey[PT_MAX]; static int g_ptOrder[PT_MAX];
static int ptCmp(const void*a,const void*b){ float x=g_ptSortKey[*(const int*)a],y=g_ptSortKey[*(const int*)b]; return x<y?1:x>y?-1:(*(const int*)a-*(const int*)b); }
/* every live one as a quad facing a lens. rt, up and fw are that lens's; how: what's laid over (far to near), then what's added */
static int ptQuads(float*o,const float*eye,const float*rt,const float*up,const float*fw,int add,int soft){
    if(!g_pt) return 0;
    int n=0;
    for(int i=0;i<g_ptCap;i++){ const Pt*P=&g_pt[i]; if(P->life<=0.f||P->add!=add||(soft>=0&&P->soft!=soft)) continue;
        g_ptSortKey[i]=(P->p[0]-eye[0])*fw[0]+(P->p[1]-eye[1])*fw[1]+(P->p[2]-eye[2])*fw[2];
        if(g_ptSortKey[i]<0.05f) continue;
        g_ptOrder[n++]=i; }
    if(!add&&n>1) qsort(g_ptOrder,(size_t)n,sizeof(int),ptCmp);
    static const float Q[6][2]={{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    for(int j=0;j<n;j++){ const Pt*P=&g_pt[g_ptOrder[j]];
        float u=P->t/P->life, s=P->size0+(P->size1-P->size0)*u, c[4];
        for(int k=0;k<4;k++) c[k]=P->c0[k]+(P->c1[k]-P->c0[k])*u;
        c[3]*=fminf(1.f,u*12.f);                                   /* it comes in over its first twelfth: nothing pops into being */
        float ax[3],ay[3];
        if(P->stretch>0.f){                                        /* a streak: along its own going, as the lens sees it */
            float vr=P->v[0]*rt[0]+P->v[1]*rt[1]+P->v[2]*rt[2], vu=P->v[0]*up[0]+P->v[1]*up[1]+P->v[2]*up[2], vl=sqrtf(vr*vr+vu*vu);
            float dr=1.f,du=0.f; if(vl>1e-5f){ dr=vr/vl; du=vu/vl; }
            float len=s+vl*P->stretch;
            for(int k=0;k<3;k++){ ax[k]=(rt[k]*dr+up[k]*du)*len; ay[k]=(-rt[k]*du+up[k]*dr)*s; } }
        else { float cr=cosf(P->rot), sr=sinf(P->rot); for(int k=0;k<3;k++){ ax[k]=(rt[k]*cr+up[k]*sr)*s; ay[k]=(-rt[k]*sr+up[k]*cr)*s; } }
        for(int q=0;q<6;q++){ float a=Q[q][0],b=Q[q][1];
            o[0]=P->p[0]+ax[0]*a+ay[0]*b; o[1]=P->p[1]+ax[1]*a+ay[1]*b; o[2]=P->p[2]+ax[2]*a+ay[2]*b; o[3]=c[0]; o[4]=c[1]; o[5]=c[2]; o[6]=c[3]; o[7]=a; o[8]=b; o+=9; } }
    return n*6; }
static void ptDrawView(const float*eye,const float*rt,const float*up,const float*fw,int view){
    static float *v=NULL; if(!v) v=(float*)malloc((size_t)PT_MAX*6u*9u*sizeof(float)); if(!v) return;
    /* a kind that's soft asks to fade out where it meets a surface, on every machine: the renderer does it where it reads the scene's depth (softfx) and draws it plain where it
     * doesn't, so the packet's the same everywhere */
    for(int add=0;add<2;add++) for(int soft=0;soft<2;soft++){
        int n=ptQuads(v,eye,rt,up,fw,add,soft); if(n<=0) continue;
        gfxFxTrisView(v,n,soft?(add?GFX_FX_GLOWZ:GFX_FX_SOFTZ):(add?GFX_FX_GLOW:GFX_FX_SOFT),0,view); } }
static void ptDrawAll(const float*view){
    if(!g_pt||g_ptLive<=0) return;
    if(g_gameViews.n>1){
        for(int k=0;k<g_gameViews.n;k++){ const float*e=g_gameViews.e[k],*a=g_gameViews.a[k]; float fw[3]={a[0]-e[0],a[1]-e[1],a[2]-e[2]},l=sqrtf(fw[0]*fw[0]+fw[1]*fw[1]+fw[2]*fw[2]); if(l<1e-5f) continue; fw[0]/=l; fw[1]/=l; fw[2]/=l;
            float rt[3]={-fw[2],0.f,fw[0]},r=sqrtf(rt[0]*rt[0]+rt[2]*rt[2]); if(r<1e-5f){ rt[0]=1.f; rt[2]=0.f; r=1.f; } rt[0]/=r; rt[2]/=r;
            const float up[3]={rt[1]*fw[2]-rt[2]*fw[1],rt[2]*fw[0]-rt[0]*fw[2],rt[0]*fw[1]-rt[1]*fw[0]};
            ptDrawView(e,rt,up,fw,k); }
        return; }
    const float rt[3]={view[0],view[4],view[8]},up[3]={view[1],view[5],view[9]},fw[3]={-view[2],-view[6],-view[10]};
    ptDrawView(g_camEye,rt,up,fw,-1); }
