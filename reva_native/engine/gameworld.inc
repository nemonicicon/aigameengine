/* ---- WHAT A BIGGER GAME NEEDS (queue step 16, 11-04): changing level, finding a way, props on any bone, and a music clock ------------------------------------------------- */

/* ---- CHANGING LEVEL. E->level loads another set while the game runs, behind a fade if it's asked for one. What the old level had goes: what a game spawned and the set's props, and a few ticks on (the frame before still has them in its lists) the memory of any pack nothing's using now. GameExport.levelIn is where a game puts things in the new one. */
static struct { char want[64]; float fade,t; int phase,sweepIn; } g_lvl;   /* phase: 0 nothing, 1 going dark, 2 load it, 3 coming back */
static int apiLevel(const char*name,float fadeSeconds){ if(!name||!name[0]) return 0; char p[96]; snprintf(p,sizeof p,"sets/%.80s.set",name); FILE*f=fopen(p,"rb"); if(!f){ printf("[level] no set called %s\n",p); fflush(stdout); return 0; } fclose(f);
    snprintf(g_lvl.want,sizeof g_lvl.want,"%s",name); g_lvl.fade=fadeSeconds>0.f?fadeSeconds:0.f; g_lvl.t=0.f; g_lvl.phase=g_lvl.fade>0.f?1:2; return 1; }
static const char* apiLevelName(void){ return g_setCur; }
static void levelSweep(void){ int freed=0; for(int i=0;i<g_setNP;i++){ if(!g_setNm[i][0]||!g_setPk[i].pk) continue; int used=0; for(int k=0;k<g_setNI&&!used;k++) if(g_setI[k].pk==i) used=1; for(int k=0;k<g_setNA&&!used;k++) if(g_setA[k].pk==i||g_setA[k].board==i) used=1; for(int k=0;k<RB_MAX&&!used;k++) if(g_rb[k].on&&g_rb[k].pk==i) used=1; for(int k=0;k<g_objHi&&!used;k++) if(g_obj[k].on&&g_obj[k].pk==i) used=1; if(g_st.farPk==i) used=1; for(int k=0;k<g_st.n&&!used;k++) if(g_st.c[k].slot==i) used=1; if(used) continue;
        for(int k=0;k<g_nObjProp;k++) if(g_objProp[k].pk==i) g_objProp[k].pk=-1;
        packFree(&g_setPk[i]); g_setNm[i][0]=0; g_setFree[g_setNFree++]=i; freed++; }
    if(freed){ printf("[level] %d packs let go\n",freed); fflush(stdout); } }
/* once a tick, before anything's drawn */
static void levelStep(void){ if(g_lvl.sweepIn>0&&--g_lvl.sweepIn==0) levelSweep();
    if(!g_lvl.phase) return;
    const float dt=simStepS();
    if(g_lvl.phase==1){ g_lvl.t+=dt; if(g_lvl.t<g_lvl.fade) return; g_lvl.phase=2; }
    if(g_lvl.phase==2){ if(g_talk.on) apiTalkStop();
        setLoad(g_lvl.want);
        if(g_game->tod){ g_tod=g_game->tod(); todApply(g_tod); }
        g_lvl.sweepIn=8; g_lvl.t=0.f; g_lvl.phase=g_lvl.fade>0.f?3:0; if(g_game->levelIn) g_game->levelIn(g_lvl.want); printf("[level] %s\n",g_lvl.want); fflush(stdout); return; }
    if(g_lvl.phase==3){ g_lvl.t+=dt; if(g_lvl.t>=g_lvl.fade) g_lvl.phase=0; } }
/* the fade, over everything */
static void levelHud(void){ if(g_lvl.phase!=1&&g_lvl.phase!=3&&!(g_lvl.phase==2)) return; float a=g_lvl.phase==2||g_lvl.fade<=0.f?1.f:g_lvl.t/g_lvl.fade; if(g_lvl.phase==3) a=1.f-a; if(a<=0.f) return; if(a>1.f) a=1.f; float v[12]; tvRect(v,0.f,0.f,(float)g_tvW,(float)g_tvH); hudTris(v,6,(float)g_tvW,(float)g_tvH,0.f,0.f,0.f,a); }

/* ---- FINDING A WAY. E->path on a set that says `climb` is a search over the walked world's real surfaces, so it goes up the ramp and the stairs to get to the upper floor and round what's in the way, at any height. On any other set it's the nav grid that actors use. It gives points to walk to in a straight line, the first of them where it started. If there's no way, it gives the way to as near as it could get.
 * The first time it's asked on a level it makes a map of where a person can stand and step, which takes a moment, so ask once while the screen's dark (levelIn, or start). After that a search only reads the map, and it's quick at any distance, for a crowd too. What's moved or been spawned since is asked about on the way. A streaming world, or a walker a lot wider or narrower than a person, has no map: that one's a search of the columns themselves, with a budget, and a long way round gives what it has by then. */
#define PATH_NODES 24000
#define PATH_HASH 32768
#define PATH_STEP 0.5f
#define PATH_LEAN 1.3f   /* how far it trusts going toward the goal: it's a way, not the shortest way, and it finds one much sooner */
typedef struct { float x,y,z,g,f; int parent; short cx,cz,yq; int next; } PathNode;
static PathNode g_pn[PATH_NODES]; static int g_pnHash[PATH_HASH],g_pnHeap[PATH_NODES];
static int pathFind(int cx,int cz,int yq,unsigned*hOut){ const unsigned h=((unsigned)(cx*73856093)^(unsigned)(cz*19349663)^(unsigned)(yq*83492791))&(PATH_HASH-1u); *hOut=h; for(int i=g_pnHash[h];i>=0;i=g_pn[i].next) if(g_pn[i].cx==cx&&g_pn[i].cz==cz&&g_pn[i].yq==yq) return i; return -1; }
/* could a body walk this? It's E->walk itself, in short steps, its feet rising as it goes, so what the map says is what a body will do. ⛔ Asking whether what's ahead is in the way doesn't do: a body on a stair has the next step's riser in its circle all the way up, and it walks up it */
static int wnStep(float x0,float z0,float y0,float x1,float z1,float r,float*yOut){ float px=x0,pz=z0,y=y0; const int n=4; for(int i=1;i<=n;i++){ const float tx=x0+(x1-x0)*(float)i/(float)n,tz=z0+(z1-z0)*(float)i/(float)n; float x=tx,z=tz; const float g=spWalk(px,pz,&x,&z,y,r); if(fabsf(x-tx)>1e-3f||fabsf(z-tz)>1e-3f) return 0; px=x; pz=z; y=g; } if(y0-y>2.5f) return 0; *yOut=y; return 1; }
/* the map: a point every half a unit, on each surface there, and the eight ways on from it */
typedef struct { float x,y,z,g,f; int nb[8],parent,next,heapMark; short cx,cz,yq; unsigned stamp; } WalkNode;
static WalkNode*g_wn=NULL; static int g_wnN=0,g_wnCap=0,g_wnFor=-1,*g_wnHash=NULL,*g_wnQ=NULL,*g_wnHeap=NULL; static float g_wnX0=0.f,g_wnZ0=0.f; static unsigned g_wnStamp=0;
#define WN_HASH 65536
#define WN_MAX 262144
static int wnFind(int cx,int cz,int yq,unsigned*hOut){ const unsigned h=((unsigned)(cx*73856093)^(unsigned)(cz*19349663)^(unsigned)(yq*83492791))&(WN_HASH-1u); if(hOut) *hOut=h; for(int i=g_wnHash[h];i>=0;i=g_wn[i].next) if(g_wn[i].cx==cx&&g_wn[i].cz==cz&&g_wn[i].yq==yq) return i; return -1; }
static int wnAdd(int cx,int cz,float y){ const short yq=(short)floorf(y*4.f+0.5f); unsigned h; const int e=wnFind(cx,cz,yq,&h); if(e>=0) return e; if(g_wnN>=WN_MAX) return -1;
    if(g_wnN>=g_wnCap){ g_wnCap=g_wnCap?g_wnCap*2:8192; g_wn=(WalkNode*)realloc(g_wn,(size_t)g_wnCap*sizeof(WalkNode)); g_wnQ=(int*)realloc(g_wnQ,(size_t)g_wnCap*sizeof(int)); g_wnHeap=(int*)realloc(g_wnHeap,(size_t)g_wnCap*2u*sizeof(int)); }
    const int i=g_wnN++; WalkNode*W=&g_wn[i]; memset(W,0,sizeof *W); W->x=g_wnX0+(float)cx*PATH_STEP; W->y=y; W->z=g_wnZ0+(float)cz*PATH_STEP; W->cx=(short)cx; W->cz=(short)cz; W->yq=yq; W->next=g_wnHash[h]; g_wnHash[h]=i; for(int d=0;d<8;d++) W->nb[d]=-1; return i; }
static void wnBuild(void){ const double t0=wall_ms(); if(!g_wnHash) g_wnHash=(int*)malloc(sizeof(int)*WN_HASH); for(int i=0;i<WN_HASH;i++) g_wnHash[i]=-1; g_wnN=0; g_wnFor=g_spBuilt; const SpGrid*G=&g_sp; if(!G->col) return; const float r=0.42f; g_wnX0=floorf(G->x0/PATH_STEP)*PATH_STEP; g_wnZ0=floorf(G->z0/PATH_STEP)*PATH_STEP; const int nx=(int)((float)G->W*SP_CELL/PATH_STEP)+1,nz=(int)((float)G->H*SP_CELL/PATH_STEP)+1;
    /* everywhere a person could stand: the ground, and the top of anything walkable */
    for(int cz=0;cz<nz;cz++) for(int cx=0;cx<nx;cx++){ const float x=g_wnX0+(float)cx*PATH_STEP,z=g_wnZ0+(float)cz*PATH_STEP; const float g=spGround(x,z,0.f); if(!spBlocked(x,z,g,r)) wnAdd(cx,cz,g); const int c=spCol(G,x,z); if(c<0) continue;
        for(int s=G->col[c];s>=0;s=G->sp[s].next){ const SpSpan*S=&G->sp[s]; if(!S->walk) continue; const float t=spGround(x,z,S->y1); if(fabsf(t-S->y1)<0.3f&&!spBlocked(x,z,t,r)) wnAdd(cx,cz,t); } }
    /* and where you can step from each, the way a body walks it. A step can find somewhere new, so the list grows as it goes */
    static const signed char D[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    for(int i=0;i<g_wnN;i++){ const float y0=g_wn[i].y,x0=g_wn[i].x,z0=g_wn[i].z; const int c0x=g_wn[i].cx,c0z=g_wn[i].cz; for(int d=0;d<8;d++){ const float nxx=x0+(float)D[d][0]*PATH_STEP,nzz=z0+(float)D[d][1]*PATH_STEP; const int c1x=c0x+D[d][0],c1z=c0z+D[d][1];
            if(c1x<0||c1z<0||c1x>=nx||c1z>=nz) continue;   /* there's always ground at 0, so it would go on for ever */
            float t; if(!wnStep(x0,z0,y0,nxx,nzz,r,&t)) continue;
            const int n=wnAdd(c1x,c1z,t); g_wn[i].nb[d]=n; } }
    printf("[path] %s: a map of %d places, %.0f ms\n",g_setCur,g_wnN,wall_ms()-t0); fflush(stdout); }
/* the nearest place on the map, at about this height */
static int wnNear(float x,float y,float z){ const int cx=(int)floorf((x-g_wnX0)/PATH_STEP+0.5f),cz=(int)floorf((z-g_wnZ0)/PATH_STEP+0.5f); int best=-1; float bd=1e30f; for(int rad=0;rad<6&&best<0;rad++) for(int dz=-rad;dz<=rad;dz++) for(int dx=-rad;dx<=rad;dx++){ if(rad&&abs(dx)!=rad&&abs(dz)!=rad) continue; for(int yq=(int)floorf(y*4.f)-16;yq<=(int)floorf(y*4.f)+16;yq++){ const int i=wnFind(cx+dx,cz+dz,yq,NULL); if(i<0) continue; const float ex=g_wn[i].x-x,ey=(g_wn[i].y-y)*2.f,ez=g_wn[i].z-z,e=ex*ex+ey*ey+ez*ez; if(e<bd){ bd=e; best=i; } } } return best; }
/* can a body walk straight from here to there, at its own height? */
static int pathLine(const float*a,const float*b,float r){ const float dx=b[0]-a[0],dz=b[2]-a[2],d=sqrtf(dx*dx+dz*dz); const int n=(int)(d/0.2f)+1; float y=a[1],px=a[0],pz=a[2]; for(int i=1;i<=n;i++){ const float t=(float)i/(float)n,tx=a[0]+dx*t,tz=a[2]+dz*t; float x=tx,z=tz; const float g=spWalk(px,pz,&x,&z,y,r); if(fabsf(x-tx)>1e-3f||fabsf(z-tz)>1e-3f) return 0; if(y-g>SP_STEP*1.5f) return 0; px=x; pz=z; y=g; } return fabsf(y-b[1])<0.5f; }
static int pathLineLive(const float*a,const float*b,float r){ return pathLine(a,b,r); }
/* a search of the map */
static int wnSearch(float x0,float y0,float z0,float x1,float y1,float z1,float r,float*xyz,int max){ const double t0=wall_ms(); if(g_wnFor!=g_spBuilt) wnBuild(); const int s=wnNear(x0,y0,z0),e=wnNear(x1,y1,z1); xyz[0]=x0; xyz[1]=y0; xyz[2]=z0; if(s<0||e<0) return 1;
    g_wnStamp++; int hn=0,best=s,goal=-1,seen=0; float bestH=1e30f; WalkNode*W=g_wn; const WalkNode*E0=&W[e]; { WalkNode*A=&W[s]; A->stamp=g_wnStamp; A->g=0.f; A->parent=-1; const float dx=E0->x-A->x,dy=E0->y-A->y,dz=E0->z-A->z; A->f=sqrtf(dx*dx+dy*dy+dz*dz); g_wnHeap[hn++]=s; }
    while(hn>0){ const int cur=g_wnHeap[0]; hn--; if(hn>0){ g_wnHeap[0]=g_wnHeap[hn]; int k=0; for(;;){ const int l=2*k+1,rr=l+1; int m=k; if(l<hn&&(W[g_wnHeap[l]].f<W[g_wnHeap[m]].f||(W[g_wnHeap[l]].f==W[g_wnHeap[m]].f&&g_wnHeap[l]<g_wnHeap[m]))) m=l; if(rr<hn&&(W[g_wnHeap[rr]].f<W[g_wnHeap[m]].f||(W[g_wnHeap[rr]].f==W[g_wnHeap[m]].f&&g_wnHeap[rr]<g_wnHeap[m]))) m=rr; if(m==k) break; const int t=g_wnHeap[m]; g_wnHeap[m]=g_wnHeap[k]; g_wnHeap[k]=t; k=m; } }
        seen++; const WalkNode*C=&W[cur]; { const float dx=E0->x-C->x,dy=E0->y-C->y,dz=E0->z-C->z,h=sqrtf(dx*dx+dy*dy+dz*dz); if(h<bestH){ bestH=h; best=cur; } } if(cur==e){ goal=cur; break; } const float gC=C->g;
        for(int d=0;d<8;d++){ const int n=C->nb[d]; if(n<0) continue; WalkNode*Q=&W[n]; const float dy=C->y-Q->y; const float ng=gC+(d>=4?1.4142f:1.f)*PATH_STEP+(dy>0.f?dy:-dy)*0.5f; if(Q->stamp==g_wnStamp&&ng>=Q->g) continue;
            /* what's been spawned or has moved since the map was made */
            { int stop=0; for(int g2=0;g2<g_spNLive&&!stop;g2++) if(!g_spLive[g2]->soft&&spBlockCount(g_spLive[g2],Q->x,Q->z,Q->y,r)>0) stop=1; if(stop) continue; }
            const float ex=E0->x-Q->x,ey=E0->y-Q->y,ez=E0->z-Q->z; Q->stamp=g_wnStamp; Q->g=ng; Q->f=ng+sqrtf(ex*ex+ey*ey+ez*ez); Q->parent=cur; if(hn<g_wnCap*2){ int k=hn++; g_wnHeap[k]=n; while(k>0){ const int p=(k-1)/2; if(W[g_wnHeap[p]].f<=W[g_wnHeap[k]].f) break; const int t2=g_wnHeap[p]; g_wnHeap[p]=g_wnHeap[k]; g_wnHeap[k]=t2; k=p; } } } }
    const int end=goal>=0?goal:best; static int chain[WN_MAX]; int nC=0;
    for(int i=end;i>=0&&nC<WN_MAX;i=W[i].parent) chain[nC++]=i;
    float A[3]={x0,W[s].y,z0};
    xyz[0]=A[0]; xyz[1]=A[1]; xyz[2]=A[2]; int out=1,at=nC-1;
    while(at>0&&out<max){ int nextI=at-1; for(int j=at>64?at-64:0;j<at;j++){ const WalkNode*Q=&W[chain[j]]; const float B[3]={Q->x,Q->y,Q->z}; if(pathLineLive(A,B,r)){ nextI=j; break; } } const WalkNode*Q=&W[chain[nextI]]; xyz[out*3]=Q->x; xyz[out*3+1]=Q->y; xyz[out*3+2]=Q->z; out++; A[0]=Q->x; A[1]=Q->y; A[2]=Q->z; at=nextI; }
    if(goal>=0&&out<max){ const float B[3]={x1,W[e].y,z1}; if(pathLineLive(A,B,r)){ xyz[out*3]=B[0]; xyz[out*3+1]=B[1]; xyz[out*3+2]=B[2]; out++; } }
    { static const char*dbg=(const char*)1; if(dbg==(const char*)1) dbg=getenv("REVA_TALKDBG"); if(dbg){ printf("[path] %d points, %d places looked at, %.2f ms, %s\n",out,seen,wall_ms()-t0,goal>=0?"there":"as near as it could get"); fflush(stdout); } }
    return out; }
static int apiPath(float x0,float y0,float z0,float x1,float y1,float z1,float radius,float*xyz,int max){ if(!xyz||max<1) return 0; const float r=radius>0.f?radius:0.42f; const double t0=wall_ms();
    if(g_spOn&&!g_st.on&&fabsf(r-0.42f)<0.1f) return wnSearch(x0,y0,z0,x1,y1,z1,r,xyz,max);
    if(!g_spOn){ static float ox[256],oz[256]; const int n=navPath(x0,z0,x1,z1,ox,oz,max<256?max:256); if(n<=0){ xyz[0]=x0; xyz[1]=0.f; xyz[2]=z0; return 1; } for(int i=0;i<n;i++){ xyz[i*3]=ox[i]; xyz[i*3+1]=0.f; xyz[i*3+2]=oz[i]; } return n; }
    for(int i=0;i<PATH_HASH;i++) g_pnHash[i]=-1;
    int nN=0,hn=0,best=0,goal=-1; float bestH=1e30f; const float y00=spGround(x0,z0,y0);
    { PathNode*P=&g_pn[0]; P->x=x0; P->y=y00; P->z=z0; P->g=0.f; const float dx=x1-x0,dz=z1-z0,dy=y1-y00; P->f=PATH_LEAN*sqrtf(dx*dx+dz*dz+dy*dy); P->parent=-1; P->cx=0; P->cz=0; P->yq=(short)floorf(y00*4.f+0.5f); unsigned h; pathFind(0,0,P->yq,&h); P->next=-1; g_pnHash[h]=0; nN=1; g_pnHeap[hn++]=0; bestH=P->f/PATH_LEAN; }
    static const signed char D[8][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    while(hn>0){ const int cur=g_pnHeap[0]; hn--; if(hn>0){ g_pnHeap[0]=g_pnHeap[hn]; int k=0; for(;;){ const int l=2*k+1,rr=l+1; int m=k; if(l<hn&&(g_pn[g_pnHeap[l]].f<g_pn[g_pnHeap[m]].f||(g_pn[g_pnHeap[l]].f==g_pn[g_pnHeap[m]].f&&g_pnHeap[l]<g_pnHeap[m]))) m=l; if(rr<hn&&(g_pn[g_pnHeap[rr]].f<g_pn[g_pnHeap[m]].f||(g_pn[g_pnHeap[rr]].f==g_pn[g_pnHeap[m]].f&&g_pnHeap[rr]<g_pnHeap[m]))) m=rr; if(m==k) break; const int t=g_pnHeap[m]; g_pnHeap[m]=g_pnHeap[k]; g_pnHeap[k]=t; k=m; } }
        const PathNode C=g_pn[cur]; const float hx=x1-C.x,hz=z1-C.z,hy=y1-C.y,here=sqrtf(hx*hx+hz*hz+hy*hy); if(here<bestH){ bestH=here; best=cur; }
        if(fabsf(hx)<=PATH_STEP&&fabsf(hz)<=PATH_STEP&&fabsf(hy)<2.f){ goal=cur; break; }
        if(nN>=PATH_NODES-8) break;
        for(int d=0;d<8;d++){ const int ncx=C.cx+D[d][0],ncz=C.cz+D[d][1]; const float nx=x0+(float)ncx*PATH_STEP,nz=z0+(float)ncz*PATH_STEP,mx=0.5f*(C.x+nx),mz=0.5f*(C.z+nz);
            if(spBlocked(mx,mz,C.y,r)) continue;   /* ⛔ what stops a body is asked at the height of its feet before the step, as E->walk does: asked after, the side of a ramp's low end looks like a way on */
            const float gm=spGround(mx,mz,C.y); if(spBlocked(nx,nz,gm,r)) continue; const float t=spGround(nx,nz,gm); const float drop=C.y-t; if(drop>2.5f) continue;
            if(d>=4){ const float a1=C.x+(float)D[d][0]*PATH_STEP,b1=C.z+(float)D[d][1]*PATH_STEP; if(spBlocked(a1,C.z,C.y,r)||spBlocked(C.x,b1,C.y,r)) continue; }
            const short yq=(short)floorf(t*4.f+0.5f); unsigned h; const int e=pathFind(ncx,ncz,yq,&h); const float step=(d>=4?1.4142f:1.f)*PATH_STEP+(drop>0.f?drop:-drop)*0.5f,ng=C.g+step;
            if(e>=0){ if(ng>=g_pn[e].g) continue; const float hh=g_pn[e].f-g_pn[e].g; g_pn[e].g=ng; g_pn[e].f=ng+hh; g_pn[e].parent=cur; if(hn<PATH_NODES){ int k=hn++; g_pnHeap[k]=e; while(k>0){ const int p=(k-1)/2; if(g_pn[g_pnHeap[p]].f<=g_pn[g_pnHeap[k]].f) break; const int t2=g_pnHeap[p]; g_pnHeap[p]=g_pnHeap[k]; g_pnHeap[k]=t2; k=p; } } continue; }
            PathNode*N=&g_pn[nN]; N->x=nx; N->y=t; N->z=nz; N->g=ng; const float ex=x1-nx,ez=z1-nz,ey=y1-t; N->f=ng+PATH_LEAN*sqrtf(ex*ex+ez*ez+ey*ey); N->parent=cur; N->cx=(short)ncx; N->cz=(short)ncz; N->yq=yq; N->next=g_pnHash[h]; g_pnHash[h]=nN;
            if(hn>=PATH_NODES) continue;
            int k=hn++; g_pnHeap[k]=nN; while(k>0){ const int p=(k-1)/2; if(g_pn[g_pnHeap[p]].f<=g_pn[g_pnHeap[k]].f) break; const int t2=g_pnHeap[p]; g_pnHeap[p]=g_pnHeap[k]; g_pnHeap[k]=t2; k=p; } nN++; } }
    /* back from the end, and the corners only: a point is left out if you can walk straight past it */
    const int end=goal>=0?goal:best; static int chain[PATH_NODES]; int nC=0;
    for(int i=end;i>=0&&nC<PATH_NODES;i=g_pn[i].parent) chain[nC++]=i;
    float A[3]={x0,y00,z0};
    xyz[0]=A[0]; xyz[1]=A[1]; xyz[2]=A[2]; int out=1,at=nC-1;
    while(at>0&&out<max){ int nextI=at-1; for(int j=at>64?at-64:0;j<at;j++){ const PathNode*Q=&g_pn[chain[j]]; const float B[3]={Q->x,Q->y,Q->z}; if(pathLine(A,B,r)){ nextI=j; break; } } const PathNode*Q=&g_pn[chain[nextI]]; xyz[out*3]=Q->x; xyz[out*3+1]=Q->y; xyz[out*3+2]=Q->z; out++; A[0]=Q->x; A[1]=Q->y; A[2]=Q->z; at=nextI; }
    if(goal>=0&&out<max&&fabsf(xyz[(out-1)*3]-x1)+fabsf(xyz[(out-1)*3+2]-z1)>1e-3f){ const float B[3]={x1,spGround(x1,z1,y1),z1}; if(pathLine(A,B,r)){ xyz[out*3]=B[0]; xyz[out*3+1]=B[1]; xyz[out*3+2]=B[2]; out++; } }
    { static const char*dbg=(const char*)1; if(dbg==(const char*)1) dbg=getenv("REVA_TALKDBG"); if(dbg){ printf("[path] %d points from %d nodes in %.2f ms, %s\n",out,nN,wall_ms()-t0,goal>=0?"there":"as near as it could get"); fflush(stdout); } }
    return out; }

/* ---- PROPS ON ANY BONE. E->attach puts a model on a bone of a body, by the bone's name in that body's pack or a plain word: head, hand_l, hand_r, spine, hips, foot_l, foot_r. It goes with the bone wherever the clips, a face or a ragdoll take it, and with the body's size: an offset and a scale are for a body of ordinary height, so the same crown fits a child. As many as the pool holds, each one on and off whenever the game says, and nothing's added to the pack. It's what's drawn and not the rules. */
#define ATT_MAX 256
typedef struct { int on,body,prop,node; Pack*pk; char bone[40]; float off[3],q[4],scale; } Att;
static Att g_att[ATT_MAX]; static int g_attN=0,g_attHi=0;
__attribute__((unused)) static int attBoneOf(Pack*P,const char*nm){ if(!P) return -1; static const struct { const char*w; int pb; } W[]={{"hips",0},{"spine",2},{"chest",3},{"neck",4},{"head",5},{"foot_l",-1},{"foot_r",-1},{"hand_l",-1},{"hand_r",-1}}; for(size_t i=0;i<sizeof W/sizeof W[0];i++) if(!strcmp(nm,W[i].w)&&W[i].pb>=0&&P->pb[W[i].pb]>=0) return P->pb[W[i].pb];
    const char*need=NULL; if(!strcmp(nm,"hand_l")) need="LeftHand"; else if(!strcmp(nm,"hand_r")) need="RightHand"; else if(!strcmp(nm,"foot_l")) need="LeftFoot"; else if(!strcmp(nm,"foot_r")) need="RightFoot"; const char*q=need?need:nm;
    for(unsigned i=0;i<P->nNodes;i++) if(P->nodes[i].kind==1&&!strcmp(packStr(P,P->nodes[i].name),q)) return (int)i;
    for(unsigned i=0;i<P->nNodes;i++) if(P->nodes[i].kind==1&&strstr(packStr(P,P->nodes[i].name),q)) return (int)i;
    if(!strcmp(nm,"hand_r")&&P->gripNode>=0) return P->gripNode;
    return -1; }
static int apiAttach(int body,int prop,const char*bone,float x,float y,float z,float yaw,float pitch,float roll,float scale){ if(!bone||prop<0||prop>=g_nObjProp) return 0; int k=-1; for(int i=0;i<ATT_MAX;i++) if(!g_att[i].on){ k=i; break; } if(k<0){ printf("[attach] more than %d at once\n",ATT_MAX); fflush(stdout); return 0; }
    Att*A=&g_att[k]; memset(A,0,sizeof *A); A->on=1; A->body=body; A->prop=prop; A->node=-1; snprintf(A->bone,sizeof A->bone,"%s",bone); A->off[0]=x; A->off[1]=y; A->off[2]=z; A->scale=scale>0.f?scale:1.f; float qa[4],qb[4],qc[4],t[4]; qaxis(qa,0,1,0,yaw); qaxis(qb,1,0,0,pitch); qaxis(qc,0,0,1,roll); qmul(t,qa,qb); qmul(A->q,t,qc); g_attN++; if(k>=g_attHi) g_attHi=k+1; return k+1; }
static void apiDetach(int h){ if(h<1||h>ATT_MAX||!g_att[h-1].on) return; g_att[h-1].on=0; g_attN--; }
static void apiDetachAll(int body){ for(int i=0;i<g_attHi;i++) if(g_att[i].on&&g_att[i].body==body){ g_att[i].on=0; g_attN--; } }
#ifdef REVA_PACKET
/* a body's been posed: what's on it, at the bone, this frame */
static void attDraw(int body,Pack*P,M4*W,const float*model){ if(!g_attN||g_noDraw) return; for(int i=0;i<g_attHi;i++){ Att*A=&g_att[i]; if(!A->on||A->body!=body) continue;
        if(A->pk!=P){ A->pk=P; A->node=attBoneOf(P,A->bone); if(A->node<0){ static int said=0; if(said++<8){ printf("[attach] %s has no bone called %s\n",P->label,A->bone); fflush(stdout); } } } if(A->node<0) continue;
        const int pk=objPropLoad(A->prop); if(pk<0) continue; Pack*Q=&g_setPk[pk]; const float sc=P->scale>1e-6f?P->scale:1.f,k=faceEyeHeight(P)/2.75f,S=A->scale*k/sc,T=k/sc; const float zero[3]={A->off[0]*T,A->off[1]*T,A->off[2]*T},one[3]={S,S,S}; M4 L,B,F; m4_trs(L,zero,A->q,one); m4_mul(B,W[A->node],L); m4_mul(F,(float*)model,B);
        const unsigned owner=g_vkOwner; const float use=g_vkUse; float tint[3]; memcpy(tint,g_vkTint,12); g_vkOwner=((unsigned)GFX_KIND_OBJECT<<28)|((0x8000u+(unsigned)i)<<12); g_vkUse=1.f; g_vkTint[0]=g_vkTint[1]=g_vkTint[2]=1.f; Face*fc=g_faceCur; g_faceCur=NULL;
        for(unsigned mi=0;mi<Q->nMeshes;mi++){ const PMesh*M=&Q->meshes[mi]; if(!(M->flags&4)) continue; g_vkMirror=1|2; vkMesh(Q,mi,F,(float*)(Q->pk+M->offBind),NULL,0,g_vkTint,g_vkUse,0,NULL); g_vkMirror=0; if(g_shadowOn) vkMesh(Q,mi,F,(float*)(Q->pk+M->offBind),NULL,0,g_vkTint,g_vkUse,GFX_SHADOW,NULL); }
        g_faceCur=fc; g_vkOwner=owner; g_vkUse=use; memcpy(g_vkTint,tint,12); } }
#else
static void attDraw(int body,Pack*P,M4*W,const float*model){ (void)body; (void)P; (void)W; (void)model; }
#endif

/* ---- A MUSIC CLOCK. E->musicClock says the music that's just started has this many beats a minute, its first beat this many seconds in, and so many beats to a bar. E->musicBeat says where it is, in beats, and GameExport.beat falls on the tick each one comes round. In a live game it follows the sound itself, because a late frame slows the game by a hair and the sound doesn't stop for it, and a tape keeps what it read, so a replay's the same session. In anything measured it's the tick clock, and no sound's involved. */
static struct { int on,bar,last,hasNext; float bpm,first,t,next; double dev0; } g_mclk;
static void apiMusicClock(float bpm,float firstBeat,int beatsPerBar){ if(bpm<=0.f){ g_mclk.on=0; return; } g_mclk.on=1; g_mclk.bpm=bpm; g_mclk.first=firstBeat; g_mclk.bar=beatsPerBar>0?beatsPerBar:4; g_mclk.t=0.f; g_mclk.last=-1000000; g_mclk.dev0=musicSeconds(); }
static float apiMusicTime(void){ return g_mclk.on?g_mclk.t:0.f; }
static float apiMusicBeat(void){ return g_mclk.on?(g_mclk.t-g_mclk.first)*g_mclk.bpm/60.f:0.f; }
/* what it'll read at the next tick. A tape's written before the tick that uses it, so this is worked out then, and kept */
static float musicClockRead(void){ if(!g_mclk.on) return -1.f; float t=g_mclk.t+simStepS();
    if(g_fixedStepMs<=0.0&&!g_tapeRep&&sfxReady()){ const float dev=(float)(musicSeconds()-g_mclk.dev0),err=dev-t; if(dev>0.f){ if(err>0.25f||err<-0.25f) t=dev; else t+=err*0.05f; } }
    g_mclk.next=t; g_mclk.hasNext=1; return t; }
static void musicClockStep(void){ if(!g_mclk.on) return; const float t=g_tapeMusicIn>=0.f?g_tapeMusicIn:(g_mclk.hasNext?g_mclk.next:musicClockRead()); g_mclk.hasNext=0;
    g_mclk.t=t; const int b=(int)floorf((t-g_mclk.first)*g_mclk.bpm/60.f); if(b!=g_mclk.last){ g_mclk.last=b; if(b>=0&&g_game->beat) g_game->beat(b); } }
