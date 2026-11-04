/* ---- THE WALKED WORLD (11-03, engine plan Phase 6) -------------------------------------------------------------------------------------------------------------------------
 * A set line `climb` says its surfaces are walked on, not only its floor. Then every prop's real triangles go into columns over the floor plan, each column a list of solid spans, the way Recast does it:
 * a ramp, a stair, a bridge, a second floor and a wall are all the same data, and there's nothing to author but the geometry. A body stands on the highest walkable top within a step of its feet that has head
 * room over it, and it's stopped by anything between a step above its feet and its head. A top is walkable if the triangle that made it isn't too steep. Rays walk the same columns. There's always ground at y 0.
 * The set's own props make one grid, and each streamed cell brings its own (engine/stream.inc), built off the main thread; a question about a point asks every grid that covers it.
 * A set without the line doesn't pay for any of this and doesn't change: it keeps the circles and the distance field. On a set with it, those stand down, and a game uses E->walk and E->ray. A pack marked Open is never solid here either. */
#define SP_CELL 0.2f
#define SP_STEP 0.36f      /* what a body just walks up: 20 cm, at 1.75 units a metre */
#define SP_HEAD 3.0f       /* the head room a place to stand needs */
#define SP_WALKNY 0.64f    /* the steepest walkable surface: about 50 degrees */
#define SP_MERGE 0.02f
#define SP_GRAV 17.2f      /* 9.8 m/s/s at 1.75 units a metre */
#define SP_MAXCOLS 6000000
#define SP_MAXLIVE 384
#define SP_JUMP 2.f        /* in one tick, that's 120 units a second: nothing walks that fast */
typedef struct { float y0,y1; int next,walk; } SpSpan;
typedef struct { int W,H; float x0,z0; int*col; SpSpan*sp; int n,cap,freeSp,soft; } SpGrid;   /* soft: it can be knocked aside: something loose, asleep */
static SpGrid g_sp;                                     /* the set's own props */
static SpGrid* g_spLive[SP_MAXLIVE]; static int g_spNLive=0;   /* the streamed cells near the hero */
static int g_spBuilt=0;   /* goes up each time a set's columns are made */
static unsigned g_spStamp=0;   /* goes up whenever what's solid changes, so what's asleep needn't keep asking */
typedef struct { char name[24]; float c[3],h[3]; } SpTrig;
#define SP_MAXTRIG 64
static SpTrig g_spTrig[SP_MAXTRIG]; static int g_spNT=0;
static struct { float y,vy; int ground; } g_feet={0.f,0.f,1};   /* the hero's height */

static void spGridFree(SpGrid*G){ free(G->col); free(G->sp); memset(G,0,sizeof *G); }
static void spClear(void){ spGridFree(&g_sp); for(int i=0;i<g_spNLive;i++){ spGridFree(g_spLive[i]); free(g_spLive[i]); } g_spNLive=0; g_spOn=0; g_spNT=0; g_feet.y=g_feet.vy=0.f; g_feet.ground=1; }
/* one solid piece of a column. The list stays in order, and pieces that touch become one: the higher top decides whether it can be stood on */
static void spAdd(SpGrid*G,int c,float y0,float y1,int walk){
    int prev=-1,cur=G->col[c];
    while(cur>=0&&G->sp[cur].y1<y0-SP_MERGE){ prev=cur; cur=G->sp[cur].next; }
    while(cur>=0&&G->sp[cur].y0<=y1+SP_MERGE){ SpSpan*S=&G->sp[cur];
        if(S->y1>y1+SP_MERGE){ walk=S->walk; y1=S->y1; }
        else if(S->y1>=y1-SP_MERGE){ walk|=S->walk; if(S->y1>y1) y1=S->y1; }
        if(S->y0<y0) y0=S->y0;
        int nx=S->next; S->next=G->freeSp; G->freeSp=cur; cur=nx; }
    int k=G->freeSp;
    if(k>=0) G->freeSp=G->sp[k].next;
    else { if(G->n>=G->cap){ G->cap*=2; G->sp=(SpSpan*)realloc(G->sp,(size_t)G->cap*sizeof(SpSpan)); } k=G->n++; }
    SpSpan*N=&G->sp[k]; N->y0=y0; N->y1=y1; N->walk=walk; N->next=cur;
    if(prev<0) G->col[c]=k; else G->sp[prev].next=k; }
/* keep what's on one side of a line in x or z */
static int spCut(float(*in)[3],int n,float(*out)[3],int axis,float v,int keepAbove){ int m=0;
    for(int i=0;i<n;i++){ const float*a=in[i],*b=in[(i+1)%n]; float da=a[axis]-v,db=b[axis]-v; if(!keepAbove){ da=-da; db=-db; }
        if(da>=0.f){ out[m][0]=a[0]; out[m][1]=a[1]; out[m][2]=a[2]; m++; }
        if((da>=0.f)!=(db>=0.f)){ float t=da/(da-db); for(int k=0;k<3;k++) out[m][k]=a[k]+(b[k]-a[k])*t; m++; } }
    return m; }
static void spTri(SpGrid*G,const float*a,const float*b,const float*c){
    float ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2],vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
    float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx,l=sqrtf(nx*nx+ny*ny+nz*nz); if(l<1e-9f) return;
    int walk=fabsf(ny)/l>=SP_WALKNY;
    float mnx=a[0],mxx=a[0],mnz=a[2],mxz=a[2];
    if(b[0]<mnx) mnx=b[0];
    if(b[0]>mxx) mxx=b[0];
    if(c[0]<mnx) mnx=c[0];
    if(c[0]>mxx) mxx=c[0];
    if(b[2]<mnz) mnz=b[2];
    if(b[2]>mxz) mxz=b[2];
    if(c[2]<mnz) mnz=c[2];
    if(c[2]>mxz) mxz=c[2];
    int cx0=(int)floorf((mnx-G->x0)/SP_CELL),cx1=(int)floorf((mxx-G->x0)/SP_CELL),cz0=(int)floorf((mnz-G->z0)/SP_CELL),cz1=(int)floorf((mxz-G->z0)/SP_CELL);
    if(cx0<0) cx0=0;
    if(cz0<0) cz0=0;
    if(cx1>=G->W) cx1=G->W-1;
    if(cz1>=G->H) cz1=G->H-1;
    float T[3][3]; memcpy(T[0],a,12); memcpy(T[1],b,12); memcpy(T[2],c,12);
    for(int cz=cz0;cz<=cz1;cz++){ float R[8][3],R2[8][3]; float z0=G->z0+(float)cz*SP_CELL;
        int nr=spCut(T,3,R,2,z0,1); if(nr<3) continue;
        nr=spCut(R,nr,R2,2,z0+SP_CELL,0); if(nr<3) continue;
        for(int cx=cx0;cx<=cx1;cx++){ float Q[8][3],Q2[8][3]; float x0=G->x0+(float)cx*SP_CELL;
            int nq=spCut(R2,nr,Q,0,x0,1); if(nq<3) continue;
            nq=spCut(Q,nq,Q2,0,x0+SP_CELL,0); if(nq<3) continue;
            float lo=Q2[0][1],hi=Q2[0][1]; for(int i=1;i<nq;i++){ if(Q2[i][1]<lo) lo=Q2[i][1]; if(Q2[i][1]>hi) hi=Q2[i][1]; }
            spAdd(G,cz*G->W+cx,lo,hi,walk); } } }
/* the columns' extent is known: make them. 0 if it's too many */
static int spGridAlloc(SpGrid*G,float bx0,float bx1,float bz0,float bz1){
    G->x0=bx0-1.f; G->z0=bz0-1.f; G->W=(int)((bx1-bx0+2.f)/SP_CELL)+1; G->H=(int)((bz1-bz0+2.f)/SP_CELL)+1;
    if((double)G->W*(double)G->H>(double)SP_MAXCOLS){ G->W=G->H=0; return 0; }
    G->col=(int*)malloc((size_t)G->W*(size_t)G->H*sizeof(int)); G->cap=1<<12; G->sp=(SpSpan*)malloc((size_t)G->cap*sizeof(SpSpan)); G->freeSp=-1; G->n=0;
    for(size_t k=0;k<(size_t)G->W*(size_t)G->H;k++) G->col[k]=-1;
    return 1; }
/* a mesh's triangles through a matrix: pass 0 widens the extent, pass 1 goes into the grid */
static unsigned spMesh(SpGrid*G,int pass,const float*Mw,const float*pos,unsigned verts,const unsigned*idx,unsigned idxCount,float*ext){
    float*wv=(float*)malloc((size_t)verts*12); if(!wv) return 0;
    for(unsigned v=0;v<verts;v++){ const float*p=pos+(size_t)v*3; float*o=wv+(size_t)v*3;
        o[0]=Mw[0]*p[0]+Mw[4]*p[1]+Mw[8]*p[2]+Mw[12]; o[1]=Mw[1]*p[0]+Mw[5]*p[1]+Mw[9]*p[2]+Mw[13]; o[2]=Mw[2]*p[0]+Mw[6]*p[1]+Mw[10]*p[2]+Mw[14];
        if(pass==0){ if(o[0]<ext[0]) ext[0]=o[0]; if(o[0]>ext[1]) ext[1]=o[0]; if(o[2]<ext[2]) ext[2]=o[2]; if(o[2]>ext[3]) ext[3]=o[2]; } }
    unsigned nt=idxCount?idxCount/3:verts/3;
    if(pass==1) for(unsigned t=0;t<nt;t++){ unsigned i0=idx?idx[t*3]:t*3,i1=idx?idx[t*3+1]:t*3+1,i2=idx?idx[t*3+2]:t*3+2;
        if(i0<verts&&i1<verts&&i2<verts) spTri(G,wv+(size_t)i0*3,wv+(size_t)i1*3,wv+(size_t)i2*3); }
    free(wv); return nt; }
/* at the end of a set's load, once its props are in */
static void spBuild(const char*name){ g_spBuilt++; if(!g_spOn) return; clock_t c0=clock();
    float ext[4]={1e9f,-1e9f,1e9f,-1e9f}; unsigned tris=0; int nProp=0;
    for(int pass=0;pass<2;pass++){
        for(int i=0;i<g_setNI;i++){ SetInst*I=&g_setI[i]; Pack*Q=&g_setPk[I->pk]; if(Q->open) continue;
            if(pass==0) nProp++;
            for(unsigned mi=0;mi<Q->nMeshes;mi++){ PMesh*Mh=&Q->meshes[mi]; if(!(Mh->flags&4)||Mh->boneCount>MAXB||Mh->verts<3||MOFF(Q,mi)) continue;
                M4 bind,Mw; memcpy(bind,Q->pk+Mh->offBind,64); m4_mul(Mw,I->m,bind);
                unsigned nt=spMesh(&g_sp,pass,Mw,(const float*)(Q->pk+Mh->offPos),Mh->verts,Mh->idxCount?(const unsigned*)(Q->pk+Mh->offIdx):NULL,Mh->idxCount,ext);
                if(pass==1) tris+=nt; } }
        if(pass==0){ if(!(ext[1]>ext[0])||!(ext[3]>ext[2])){ printf("[climb] %s: nothing solid of its own\n",name); fflush(stdout); return; }
            if(!spGridAlloc(&g_sp,ext[0],ext[1],ext[2],ext[3])){ printf("[climb] %s: too many columns: only the ground\n",name); fflush(stdout); return; } } }
    printf("[climb] %s: %d props, %u triangles, %d x %d columns of %.1f u, %d spans, %.0f ms\n",name,nProp,tris,g_sp.W,g_sp.H,SP_CELL,g_sp.n,(double)(clock()-c0)*1000.0/CLOCKS_PER_SEC); fflush(stdout); }
/* a streamed cell's own grid, from its pack's bytes and where it was put. It touches nothing but what it's given, so the stream's thread can do it */
static SpGrid* spGridOfPack(const unsigned char*pk,const float*M){
    const unsigned*Hd=(const unsigned*)(pk+4); unsigned nMeshes=Hd[2]; const PMesh*Ms=(const PMesh*)(pk+Hd[7]);
    SpGrid*G=(SpGrid*)calloc(1,sizeof(SpGrid)); if(!G) return NULL;
    float ext[4]={1e9f,-1e9f,1e9f,-1e9f};
    for(int pass=0;pass<2;pass++){
        for(unsigned mi=0;mi<nMeshes;mi++){ const PMesh*Mh=&Ms[mi]; if(!(Mh->flags&4)||Mh->boneCount>MAXB||Mh->verts<3) continue;
            M4 bind,Mw,M9; memcpy(bind,pk+Mh->offBind,64); memcpy(M9,M,64); m4_mul(Mw,M9,bind);
            spMesh(G,pass,Mw,(const float*)(pk+Mh->offPos),Mh->verts,Mh->idxCount?(const unsigned*)(pk+Mh->offIdx):NULL,Mh->idxCount,ext); }
        if(pass==0&&(!(ext[1]>ext[0])||!(ext[3]>ext[2])||!spGridAlloc(G,ext[0],ext[1],ext[2],ext[3]))){ free(G); return NULL; } }
    return G; }
static void spLiveAdd(SpGrid*G){ if(G&&g_spNLive<SP_MAXLIVE){ g_spLive[g_spNLive++]=G; g_spStamp++; } }
static void spLiveDrop(SpGrid*G){ for(int i=0;i<g_spNLive;i++) if(g_spLive[i]==G){ g_spLive[i]=g_spLive[--g_spNLive]; g_spStamp++; break; } }
/* the order of the grids must not matter, and it doesn't: every question below is a highest, a lowest or an any */
static int spCol(const SpGrid*G,float x,float z){ if(!G->col) return -1; int cx=(int)floorf((x-G->x0)/SP_CELL),cz=(int)floorf((z-G->z0)/SP_CELL);
    return (cx<0||cz<0||cx>=G->W||cz>=G->H)?-1:cz*G->W+cx; }
static float spGround1In(const SpGrid*G,float x,float z,float y,float best){ int c=spCol(G,x,z); if(c<0) return best;
    for(int s=G->col[c];s>=0;s=G->sp[s].next){ const SpSpan*S=&G->sp[s]; if(S->y1>y+SP_STEP) break; if(!S->walk) continue;
        if(S->next>=0&&G->sp[S->next].y0-S->y1<SP_HEAD) continue;
        if(S->y1>best) best=S->y1; }
    return best; }
static float spGround1(float x,float z,float y){ float best=spGround1In(&g_sp,x,z,y,0.f); for(int i=0;i<g_spNLive;i++) best=spGround1In(g_spLive[i],x,z,y,best); return best; }
/* where to stand at this point: a foot's width of tolerance, so an edge doesn't drop a body the moment its middle is over it */
static float spGround(float x,float z,float y){ static const float D[4][2]={{0.14f,0.f},{-0.14f,0.f},{0.f,0.14f},{0.f,-0.14f}}; float best=spGround1(x,z,y);
    for(int i=0;i<4;i++){ float g=spGround1(x+D[i][0],z+D[i][1],y); if(g>best) best=g; }
    return best; }
static int spBlock1In(const SpGrid*G,float x,float z,float y){ int c=spCol(G,x,z); if(c<0) return 0;
    for(int s=G->col[c];s>=0;s=G->sp[s].next){ const SpSpan*S=&G->sp[s]; if(S->y0>=y+SP_HEAD) return 0; if(S->y1>y+SP_STEP) return 1; }
    return 0; }
static int spBlock1(float x,float z,float y){ if(spBlock1In(&g_sp,x,z,y)) return 1; for(int i=0;i<g_spNLive;i++) if(spBlock1In(g_spLive[i],x,z,y)) return 1; return 0; }
static int spBlocked(float x,float z,float y,float r){ static const float D[8][2]={{1.f,0.f},{-1.f,0.f},{0.f,1.f},{0.f,-1.f},{0.7071f,0.7071f},{-0.7071f,0.7071f},{0.7071f,-0.7071f},{-0.7071f,-0.7071f}};
    if(spBlock1(x,z,y)) return 1;
    for(int i=0;i<8;i++) if(spBlock1(x+D[i][0]*r,z+D[i][1]*r,y)) return 1;
    return 0; }
/* how much of a body's circle is in one grid's solid: its middle and eight points round its edge */
static int spBlockCount(const SpGrid*G,float x,float z,float y,float r){ static const float D[8][2]={{1.f,0.f},{-1.f,0.f},{0.f,1.f},{0.f,-1.f},{0.7071f,0.7071f},{-0.7071f,0.7071f},{0.7071f,-0.7071f},{-0.7071f,-0.7071f}};
    int n=spBlock1In(G,x,z,y); for(int i=0;i<8;i++) n+=spBlock1In(G,x+D[i][0]*r,z+D[i][1]*r,y); return n; }
/* would going there put a body further into anything than it is where it stands? Grid by grid, so being inside one thing is no way into another */
static int spWorse(float x,float z,float ox,float oz,float y,float r){
    if(spBlockCount(&g_sp,x,z,y,r)>spBlockCount(&g_sp,ox,oz,y,r)) return 1;
    for(int i=0;i<g_spNLive;i++) if(spBlockCount(g_spLive[i],x,z,y,r)>spBlockCount(g_spLive[i],ox,oz,y,r)) return 1;
    return 0; }
/* a body going from one place towards another: it slides along whatever stops it. Returns the height to stand at where it ended up. Without `climb`, it's the old colliders and the ground.
 * Something may already be in its circle: it fell beside a wall, or a lift came down on it. It can walk along that or out of it, and never further in (11-03: it used to be let go anywhere, so falling off a ramp beside a wall let her walk straight through the wall). */
static void spWalkStep(float ox,float oz,float*x,float*z,float y,float r){
    if(spBlocked(*x,*z,y,r)&&spWorse(*x,*z,ox,oz,y,r)){
        if(!spWorse(*x,oz,ox,oz,y,r)) *z=oz;
        else if(!spWorse(ox,*z,ox,oz,y,r)) *x=ox;
        else { *x=ox; *z=oz; } } }
static float spWalk(float ox,float oz,float*x,float*z,float y,float r){
    if(!g_spOn){ colResolve(x,z,r); return 0.f; }
    /* ⛔ a surface in these columns is one cell thick, and a tall box is hollow inside, so a move longer than a cell could step clean over a wall: it's walked a cell at a time. Past SP_JUMP it isn't a walk at all, it's a game putting someone somewhere, and no wall on the way gets a say */
    const float tx=*x,tz=*z,dx=tx-ox,dz=tz-oz,d=fabsf(dx)>fabsf(dz)?fabsf(dx):fabsf(dz);
    if(d>SP_CELL&&d<SP_JUMP){ const int n=(int)(d/SP_CELL)+1; const float sx=dx/(float)n,sz=dz/(float)n; float fx=ox,fz=oz; int free9=1;
        for(int i=1;i<=n;i++){ const float wx=(i==n&&free9)?tx:fx+sx,wz=(i==n&&free9)?tz:fz+sz; float nx=wx,nz=wz;   /* on from where the last step ended: a body that's been stopped stays stopped */
            spWalkStep(fx,fz,&nx,&nz,y,r); if(nx!=wx||nz!=wz) free9=0;
            fx=nx; fz=nz; }
        *x=fx; *z=fz; }
    else spWalkStep(ox,oz,x,z,y,r);
    return spGround(*x,*z,y); }
/* a straight line through one grid's columns: the first thing it hits, if that's before best */
static void spRayIn(const SpGrid*G,const float*a,const float*b,float*best){ if(!G->col) return; float dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2];
    float gx1=G->x0+(float)G->W*SP_CELL,gz1=G->z0+(float)G->H*SP_CELL;
    if((a[0]<G->x0&&b[0]<G->x0)||(a[0]>gx1&&b[0]>gx1)||(a[2]<G->z0&&b[2]<G->z0)||(a[2]>gz1&&b[2]>gz1)) return;
    /* it starts where it comes into this grid's columns: the steps below are only enough to cross the grid, and a small one can be a long way along a ray (11-03: it used to count them from the ray's start, and go straight through a cell or an object further off than that) */
    float t0=0.f;
    if(a[0]<G->x0&&dx>0.f){ float t=(G->x0-a[0])/dx; if(t>t0) t0=t; } else if(a[0]>gx1&&dx<0.f){ float t=(gx1-a[0])/dx; if(t>t0) t0=t; }
    if(a[2]<G->z0&&dz>0.f){ float t=(G->z0-a[2])/dz; if(t>t0) t0=t; } else if(a[2]>gz1&&dz<0.f){ float t=(gz1-a[2])/dz; if(t>t0) t0=t; }
    if(t0>=1.f||t0>=*best) return;
    float fx=(a[0]+dx*t0-G->x0)/SP_CELL,fz=(a[2]+dz*t0-G->z0)/SP_CELL; int cx=(int)floorf(fx),cz=(int)floorf(fz); int sx=dx>0.f?1:-1,sz=dz>0.f?1:-1;
    float dtx=fabsf(dx)>1e-7f?fabsf(SP_CELL/dx):1e9f,dtz=fabsf(dz)>1e-7f?fabsf(SP_CELL/dz):1e9f;
    float tx=fabsf(dx)>1e-7f?t0+((float)(sx>0?cx+1:cx)-fx)*SP_CELL/dx:1e9f,tz=fabsf(dz)>1e-7f?t0+((float)(sz>0?cz+1:cz)-fz)*SP_CELL/dz:1e9f;
    for(int i=G->W+G->H+4;i>0&&t0<*best&&t0<1.f;i--){ float t1=tx<tz?tx:tz; if(t1>1.f) t1=1.f;
        if(cx>=0&&cz>=0&&cx<G->W&&cz<G->H){ float ya=a[1]+dy*t0,yb=a[1]+dy*t1,lo=ya<yb?ya:yb,hi=ya<yb?yb:ya;
            for(int s=G->col[cz*G->W+cx];s>=0;s=G->sp[s].next){ const SpSpan*S=&G->sp[s]; if(S->y0>hi) break; if(S->y1<lo) continue;
                float t=t0; if(fabsf(yb-ya)>1e-7f){ if(ya>S->y1) t=t0+(S->y1-ya)/(yb-ya)*(t1-t0); else if(ya<S->y0) t=t0+(S->y0-ya)/(yb-ya)*(t1-t0); }
                if(t<*best) *best=t;
                break; } }
        if(tx<tz){ tx+=dtx; cx+=sx; } else { tz+=dtz; cz+=sz; }
        t0=t1; } }
/* does a straight line hit anything? t is how far along it, 0..1 */
static int spRay(const float*a,const float*b,float*tOut){ float best=2.f;
    if(a[1]>0.f&&b[1]<0.f) best=a[1]/(a[1]-b[1]);   /* the ground */
    spRayIn(&g_sp,a,b,&best); for(int i=0;i<g_spNLive;i++) spRayIn(g_spLive[i],a,b,&best);
    if(best>1.f) return 0;
    if(tOut) *tOut=best;
    return 1; }
/* for something falling, which isn't a body: the highest top it would land on at this height, walkable or not. A wall's span isn't one: its top is too far above. skip: a grid to leave out, its own */
static float spTopBelowIn(const SpGrid*G,float x,float z,float y,float best){ int c=spCol(G,x,z); if(c<0) return best;
    for(int s=G->col[c];s>=0;s=G->sp[s].next){ const SpSpan*S=&G->sp[s]; if(S->y0>y+SP_STEP) break; if(S->y1<=y+SP_STEP&&S->y1>best) best=S->y1; }
    return best; }
static float spTopBelow(float x,float z,float y,const SpGrid*skip){ float best=spTopBelowIn(&g_sp,x,z,y,0.f);
    for(int i=0;i<g_spNLive;i++) if(g_spLive[i]!=skip) best=spTopBelowIn(g_spLive[i],x,z,y,best);
    return best; }
/* is any of this stretch of height, at this point, in something? */
static int spHitsIn(const SpGrid*G,float x,float z,float y0,float y1){ int c=spCol(G,x,z); if(c<0) return 0;
    for(int s=G->col[c];s>=0;s=G->sp[s].next){ const SpSpan*S=&G->sp[s]; if(S->y0>y1) return 0; if(S->y1>=y0) return 1; }
    return 0; }
static int spSolidAt(float x,float z,float y){ if(spHitsIn(&g_sp,x,z,y,y)) return 1; for(int i=0;i<g_spNLive;i++) if(spHitsIn(g_spLive[i],x,z,y,y)) return 1; return 0; }
/* the same for something loose, which isn't stopped by what it can knock aside */
static int spHits(float x,float z,float y0,float y1){ if(spHitsIn(&g_sp,x,z,y0,y1)) return 1; for(int i=0;i<g_spNLive;i++) if(!g_spLive[i]->soft&&spHitsIn(g_spLive[i],x,z,y0,y1)) return 1; return 0; }
/* the hero, once a tick: she walks, falls and lands. A step is eased, so stairs don't jolt the picture */
static void spHeroStep(float*x,float*z,float dt){ static int first=1; static float ox,oz; if(first){ first=0; ox=*x; oz=*z; }
    float t=spWalk(ox,oz,x,z,g_feet.y,0.42f); ox=*x; oz=*z;
    if(g_feet.vy>0.f||g_feet.y>t+SP_STEP){ g_feet.vy-=SP_GRAV*dt; g_feet.y+=g_feet.vy*dt; g_feet.ground=0;
        if(g_feet.vy<=0.f&&g_feet.y<=t){ g_feet.y=t; g_feet.vy=0.f; g_feet.ground=1; } }
    else { g_feet.y+=(t-g_feet.y)*fminf(1.f,dt*16.f); g_feet.vy=0.f; g_feet.ground=1; } }
static int spHeroJump(float speed){ if(!g_feet.ground) return 0; g_feet.vy=speed; g_feet.ground=0; return 1; }
static void spTrigAdd(const char*ln){ if(g_spNT>=SP_MAXTRIG){ printf("[set] more than %d triggers\n",SP_MAXTRIG); return; } SpTrig*T=&g_spTrig[g_spNT]; float y,h;
    if(sscanf(ln,"trigger %23s %f %f %f %f %f %f",T->name,&T->c[0],&y,&T->c[2],&T->h[0],&h,&T->h[2])!=7){ printf("[set] a trigger needs: name x y z width height depth\n"); return; }
    T->h[0]*=0.5f; T->h[2]*=0.5f; T->h[1]=h*0.5f; T->c[1]=y+T->h[1]; g_spNT++; }
static int spInTrigger(const char*name,float x,float y,float z){ for(int i=0;i<g_spNT;i++){ const SpTrig*T=&g_spTrig[i];
        if(!strcmp(T->name,name)&&fabsf(x-T->c[0])<=T->h[0]&&fabsf(y-T->c[1])<=T->h[1]&&fabsf(z-T->c[2])<=T->h[2]) return 1; }
    return 0; }
