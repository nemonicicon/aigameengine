/* ================= THE WORLD IS SOLID (routing round the furniture doesn't stop a body going into it: a player can walk
 * anywhere, so the collision itself has to be right) ================================================================================================
 * colResolve() pushed a walking body out of CIRCLES -- one per prop, its own comment said "a wall is not a circle"
 * -- so a bench, a bed or a wall let a player straight in at any grazing angle. Now every set with a geometry grid
 * (the audit's, built from the props' and walls' real triangles) carries a SIGNED DISTANCE FIELD over its floor plan:
 * a cell is BLOCKED where something stands over a step (0.30 u above the floor) or hangs under a head (an underside
 * below 2.9 u: a table top); every cell knows how far it is from the nearest blocked edge, negative inside. A body of
 * radius r is pushed along the field's slope until it is r clear -- it SLIDES along a wall at any angle, and a body
 * that starts inside furniture walks out of the nearest side. REVA_COLCIRCLES=1 is the control (the old circles). */
static float*g_sdf=NULL; static int g_sdfW=0,g_sdfH=0; static float g_sdfX0=0.f,g_sdfZ0=0.f,g_sdfCell=0.1f; static unsigned g_sdfSig=0; static int g_sdfOK=0;
static int g_sdfCircles=-1;
static void colSdfBuild(void){
    caGridSync();
    if(g_sdfOK&&g_sdfSig==g_caSig) return;
    g_sdfOK=1; g_sdfSig=g_caSig; free(g_sdf); g_sdf=NULL; g_sdfW=g_sdfH=0;
    const CaGrid*G=&g_caG; if(!G->fl) return;
    int W=G->W, H=G->H; size_t N=(size_t)W*H;
    unsigned char*blk=(unsigned char*)malloc(N); float*dout=(float*)malloc(N*sizeof(float)), *din=(float*)malloc(N*sizeof(float));
    g_sdf=(float*)malloc(N*sizeof(float));
    if(!blk||!dout||!din||!g_sdf){ free(blk); free(dout); free(din); free(g_sdf); g_sdf=NULL; return; }
    int nb=0;
    for(size_t k=0;k<N;k++){ unsigned char f=G->fl[k]; float fy=G->fy[k];
        int b=((f&CA_OB)&&G->ty[k]>fy+0.30f)||((f&CA_OH)&&G->by[k]<fy+2.9f);
        blk[k]=(unsigned char)b; nb+=b; }
    /* two chamfer passes (1, sqrt2): distance to the nearest blocked cell (outside) and to the nearest free one (inside) */
    for(int pass=0;pass<2;pass++){ float*D=pass?din:dout; unsigned char want=pass?0:1;
        for(size_t k=0;k<N;k++) D[k]=(blk[k]==want)?0.f:1e9f;
        for(int z=0;z<H;z++) for(int x=0;x<W;x++){ float*d=&D[(size_t)z*W+x]; if(*d==0.f) continue;
            if(x>0&&D[(size_t)z*W+x-1]+1.f<*d) *d=D[(size_t)z*W+x-1]+1.f;
            if(z>0){ if(D[(size_t)(z-1)*W+x]+1.f<*d) *d=D[(size_t)(z-1)*W+x]+1.f;
                     if(x>0&&D[(size_t)(z-1)*W+x-1]+1.41421f<*d) *d=D[(size_t)(z-1)*W+x-1]+1.41421f;
                     if(x<W-1&&D[(size_t)(z-1)*W+x+1]+1.41421f<*d) *d=D[(size_t)(z-1)*W+x+1]+1.41421f; } }
        for(int z=H-1;z>=0;z--) for(int x=W-1;x>=0;x--){ float*d=&D[(size_t)z*W+x]; if(*d==0.f) continue;
            if(x<W-1&&D[(size_t)z*W+x+1]+1.f<*d) *d=D[(size_t)z*W+x+1]+1.f;
            if(z<H-1){ if(D[(size_t)(z+1)*W+x]+1.f<*d) *d=D[(size_t)(z+1)*W+x]+1.f;
                       if(x<W-1&&D[(size_t)(z+1)*W+x+1]+1.41421f<*d) *d=D[(size_t)(z+1)*W+x+1]+1.41421f;
                       if(x>0&&D[(size_t)(z+1)*W+x-1]+1.41421f<*d) *d=D[(size_t)(z+1)*W+x-1]+1.41421f; } } }
    for(size_t k=0;k<N;k++) g_sdf[k]=blk[k]?-(din[k]-0.5f)*G->cell:(dout[k]-0.5f)*G->cell;   /* to the cell EDGE, not its centre */
    free(blk); free(dout); free(din);
    g_sdfW=W; g_sdfH=H; g_sdfX0=G->x0; g_sdfZ0=G->z0; g_sdfCell=G->cell;
    printf("[collide] the world is solid: a distance field %dx%d of %.2f u from %s, %d blocked cells (over a step, under a head)\n",
           W,H,G->cell,g_setCur,nb); fflush(stdout); }
static float colSdfAt(float x,float z){
    float fx=(x-g_sdfX0)/g_sdfCell-0.5f, fz=(z-g_sdfZ0)/g_sdfCell-0.5f;
    int ix=(int)floorf(fx), iz=(int)floorf(fz); float ux=fx-ix, uz=fz-iz;
    if(ix<0||iz<0||ix>=g_sdfW-1||iz>=g_sdfH-1) return 1e3f;          /* off the grid: open ground */
    const float*a=g_sdf+(size_t)iz*g_sdfW+ix;
    return (a[0]*(1-ux)+a[1]*ux)*(1-uz)+(a[g_sdfW]*(1-ux)+a[g_sdfW+1]*ux)*uz; }
static int colSdfResolve(float*x,float*z,float mr,float*moved){
    if(g_sdfCircles<0){ const char*e=getenv("REVA_COLCIRCLES"); g_sdfCircles=(e&&e[0]&&e[0]!='0')?1:0; }
    if(g_sdfCircles||!g_setNI) return 0;
    colSdfBuild(); if(!g_sdf) return 0;
    float ox=*x, oz=*z;
    for(int it=0;it<4;it++){ float d=colSdfAt(*x,*z); if(d>=mr) break;
        float e=g_sdfCell;
        float gx=colSdfAt(*x+e,*z)-colSdfAt(*x-e,*z), gz=colSdfAt(*x,*z+e)-colSdfAt(*x,*z-e);
        float gl=sqrtf(gx*gx+gz*gz); if(gl<1e-6f) break;
        float step=mr-d; if(step>1.0f) step=1.0f;
        *x+=gx/gl*step; *z+=gz/gl*step; }
    float mx=*x-ox, mz=*z-oz; *moved=sqrtf(mx*mx+mz*mz);
    return 1; }
/* ================= WALKS GO ROUND THE FURNITURE (after the contact pass: the reason characters
 * so often walked through things) =======================================================================================
 * A scene walk went in a straight line from where the body stood to its mark, through a bench, a bed, a wall. The
 * audit grid already knows every obstacle from its real triangles; this is a 0.2 u WALKING grid made from it -- a
 * cell is blocked where something stands more than 0.30 u off the floor (a rug is floor, a bench is not), grown by
 * a body's half-width -- and an A* over it, string-pulled to its corners. A walk whose straight line is clear is
 * untouched. REVA_NOROUTE=1 is the control. `[walk] slot N: routed round the furniture: K corners`. */
#define wrRT_CELL 0.20f
static unsigned char*wr_=NULL; static int wr_W=0,wr_H=0; static float wr_X0=0.f,wr_Z0=0.f; static unsigned wr_Sig=0; static int wr_OK=0;
static int wr_Off=-1;
static void wrrtBuild(void){
    caGridSync();
    const CaGrid*G=&g_caG;
    if(wr_OK&&wr_Sig==g_caSig) return;
    wr_OK=1; wr_Sig=g_caSig;
    free(wr_); wr_=NULL; wr_W=wr_H=0;
    if(!G->fl) return;
    int f=(int)(wrRT_CELL/G->cell+0.5f); if(f<1) f=1;
    int W=(G->W+f-1)/f, H=(G->H+f-1)/f;
    unsigned char*raw=(unsigned char*)calloc((size_t)W*H,1); wr_=(unsigned char*)calloc((size_t)W*H,1);
    if(!raw||!wr_){ free(raw); free(wr_); wr_=NULL; return; }
    for(int z=0;z<G->H;z++) for(int x=0;x<G->W;x++){ int k=z*G->W+x;
        if((G->fl[k]&CA_OB)&&G->ty[k]>G->fy[k]+0.30f) raw[(z/f)*W+(x/f)]=1; }
    const int R=(int)ceilf(0.34f/wrRT_CELL);
    int nb=0;
    for(int z=0;z<H;z++) for(int x=0;x<W;x++){ if(!raw[z*W+x]) continue;
        for(int dz=-R;dz<=R;dz++) for(int dx=-R;dx<=R;dx++){ if(dx*dx+dz*dz>R*R) continue;
            int a=x+dx, b=z+dz; if(a<0||b<0||a>=W||b>=H) continue;
            if(!wr_[b*W+a]){ wr_[b*W+a]=1; nb++; } } }
    free(raw);
    wr_W=W; wr_H=H; wr_X0=G->x0; wr_Z0=G->z0;
    printf("[walk] walking grid %dx%d wrcells of %.2f u from the furniture, %d blocked (a body's half-width round everything over 0.30 u)\n",W,H,wrRT_CELL,nb);
    fflush(stdout); }
static int wrrtFree(int a,int b){ return a>=0&&b>=0&&a<wr_W&&b<wr_H&&!wr_[b*wr_W+a]; }
static int wrrtCellOf(float x,float z,int*a,int*b){ *a=(int)floorf((x-wr_X0)/wrRT_CELL); *b=(int)floorf((z-wr_Z0)/wrRT_CELL);
    return *a>=0&&*b>=0&&*a<wr_W&&*b<wr_H; }
/* clear straight line between two points, cell by cell */
static int wrrtClear(float x0,float z0,float x1,float z1){
    float dx=x1-x0, dz=z1-z0, L=sqrtf(dx*dx+dz*dz); int n=(int)(L/(wrRT_CELL*0.5f))+1;
    for(int i=0;i<=n;i++){ float u=(float)i/(float)n; int a,b;
        if(!wrrtCellOf(x0+dx*u,z0+dz*u,&a,&b)) continue;
        if(!wrrtFree(a,b)) return 0; }
    return 1; }
static int wrrtNearestFree(int*a,int*b){
    if(wrrtFree(*a,*b)) return 1;
    for(int r=1;r<=12;r++) for(int db=-r;db<=r;db++) for(int da=-r;da<=r;da++){
        if(da!=-r&&da!=r&&db!=-r&&db!=r) continue;
        if(wrrtFree(*a+da,*b+db)){ *a+=da; *b+=db; return 1; } }
    return 0; }
static int wrrtRoute(int slot,float sx,float sz,float tx,float tz,float*ox,float*oz,int max){
    if(wr_Off<0){ const char*e=getenv("REVA_NOROUTE"); wr_Off=(e&&e[0]&&e[0]!='0')?1:0; }
    if(wr_Off) return 0;
    wrrtBuild();
    if(!wr_) return 0;
    if(wrrtClear(sx,sz,tx,tz)) return 0;                       /* the straight line is clear: an ordinary walk */
    int a0,b0,a1,b1;
    if(!wrrtCellOf(sx,sz,&a0,&b0)||!wrrtCellOf(tx,tz,&a1,&b1)) return 0;
    if(!wrrtNearestFree(&a0,&b0)||!wrrtNearestFree(&a1,&b1)){ printf("[walk] slot %d: no free floor near the start or the mark -- walking straight\n",slot); fflush(stdout); return 0; }
    int N=wr_W*wr_H;
    static float*wrgs=NULL; static int*wrpar=NULL; static unsigned char*wrcl=NULL; static int*wrhp=NULL; static int wrcap=0;
    if(wrcap<N){ wrcap=N; wrgs=(float*)realloc(wrgs,sizeof(float)*N); wrpar=(int*)realloc(wrpar,sizeof(int)*N);
               wrcl=(unsigned char*)realloc(wrcl,N); wrhp=(int*)realloc(wrhp,sizeof(int)*N*2); }
    for(int i=0;i<N;i++){ wrgs[i]=1e30f; wrpar[i]=-1; wrcl[i]=0; }
    int st=b0*wr_W+a0, tg=b1*wr_W+a1, wrhn=0;
    #define wrRT_H(n) (hypotf((float)((n)%wr_W-a1),(float)((n)/wr_W-b1)))
    #define wrRT_F(n) (wrgs[n]+wrRT_H(n))
    wrgs[st]=0.f; wrhp[wrhn++]=st;
    int wrfound=0, wrpops=0;
    while(wrhn>0&&wrpops<N*2){ wrpops++;
        int cur=wrhp[0]; wrhp[0]=wrhp[--wrhn];
        { int i=0; for(;;){ int l=2*i+1, r=l+1, m=i; if(l<wrhn&&wrRT_F(wrhp[l])<wrRT_F(wrhp[m])) m=l; if(r<wrhn&&wrRT_F(wrhp[r])<wrRT_F(wrhp[m])) m=r;
            if(m==i) break;
            int t=wrhp[i]; wrhp[i]=wrhp[m]; wrhp[m]=t; i=m; } }
        if(wrcl[cur]) continue;
        wrcl[cur]=1;
        if(cur==tg){ wrfound=1; break; }
        int ca=cur%wr_W, cb=cur/wr_W;
        for(int db=-1;db<=1;db++) for(int da=-1;da<=1;da++){ if(!da&&!db) continue;
            int na=ca+da, nbb=cb+db; if(!wrrtFree(na,nbb)) continue;
            if(da&&db&&(!wrrtFree(ca+da,cb)||!wrrtFree(ca,cb+db))) continue;   /* no corner cutting */
            int nn=nbb*wr_W+na; if(wrcl[nn]) continue;
            float g=wrgs[cur]+((da&&db)?1.41421356f:1.f);
            if(g<wrgs[nn]){ wrgs[nn]=g; wrpar[nn]=cur; if(wrhn<N*2){ wrhp[wrhn++]=nn;
                int i=wrhn-1; while(i>0){ int p=(i-1)/2; if(wrRT_F(wrhp[p])<=wrRT_F(wrhp[i])) break; int t=wrhp[p]; wrhp[p]=wrhp[i]; wrhp[i]=t; i=p; } } } } }
    #undef wrRT_H
    #undef wrRT_F
    if(!wrfound){ printf("[walk] slot %d: the furniture closes every way to the mark -- walking straight\n",slot); fflush(stdout); return 0; }
    /* the wrcells, start -> target */
    static int*wrcells=NULL; static int wrccap=0; int n=0;
    for(int c=tg;c>=0;c=wrpar[c]){ if(n>=wrccap){ wrccap=wrccap?wrccap*2:1024; wrcells=(int*)realloc(wrcells,sizeof(int)*wrccap); } wrcells[n++]=c; if(c==st) break; }
    /* string-pull: from where she stands, the farthest cell she can see; the last point is the mark itself */
    int k=0; float px=sx, pz=sz; int i=n-1;
    while(i>0&&k<max-1){
        int farj=i-1;
        for(int j=0;j<i;j++){ float cx=wr_X0+((float)(wrcells[j]%wr_W)+0.5f)*wrRT_CELL, cz=wr_Z0+((float)(wrcells[j]/wr_W)+0.5f)*wrRT_CELL;
            if(wrrtClear(px,pz,cx,cz)){ farj=j; break; } }
        if(farj==0&&wrrtClear(px,pz,tx,tz)) break;
        float cx=wr_X0+((float)(wrcells[farj]%wr_W)+0.5f)*wrRT_CELL, cz=wr_Z0+((float)(wrcells[farj]/wr_W)+0.5f)*wrRT_CELL;
        ox[k]=cx; oz[k]=cz; k++; px=cx; pz=cz; i=farj;
        if(farj==0) break; }
    float len=0.f, lx=sx, lz=sz; for(int j=0;j<k;j++){ len+=hypotf(ox[j]-lx,oz[j]-lz); lx=ox[j]; lz=oz[j]; } len+=hypotf(tx-lx,tz-lz);
    printf("[walk] slot %d: routed round the furniture: %d corner(s), %.2f u (the straight line was %.2f u and went through it)\n",
           slot,k,len,hypotf(tx-sx,tz-sz)); fflush(stdout);
    return k; }
