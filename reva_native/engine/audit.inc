/* ================= THE CLIPPING AUDIT ============================
 * Nothing may clip through anything. A `room` is never a
 * collider on purpose (a scan is a place), so nothing stopped a body going through a ledge
 * or a pillar -- and nothing SAID so either. This makes it a number, per line and per body.
 *   REVA_CLIPAUDIT=1 or ctl clipaudit=1      REVA_CLIPAUDIT_OUT=<csv> every bad sample
 *   REVA_CLIPAUDIT_OBMIN=<u> how tall a bump must be to be an obstacle (default 0.05)
 * THE WORLD: a 0.10 u grid over every `room` and every solid prop, sampled off their REAL
 * triangles through the draw's own matrices (model x bind). Per cell: the FLOOR (the lowest
 * walkable surface in y -1.5..0.25), the TOP of anything standing on it up to floor+3.0 (a
 * ledge, bench, stair, pillar, wall), and the UNDERSIDE of anything overhead. A scan is a
 * SHELL -- a pillar has no inside -- so what a flood from the open floor never reaches is
 * filled solid.
 * THE BODIES: every cast slot as drawn (the same W and model the skinning used). Its joints --
 * hips, chest, neck, head and crown, shoulders, elbows, hands, thighs, knees, ankles, toes, soles,
 * wheels -- are BAD past 0.03 u under the floor or into ANOTHER body's capsules (torso 0.07 x
 * height, head 0.05, limbs 0.035); every LIMB is also sampled every 0.05 u with its flesh (foot
 * 0.015 x height .. torso 0.06) and is BAD past 0.03 u into an obstacle, after one cell of
 * tolerance for the grid. clipAuditPoint()/clipAuditBox() test anything else as bare points.
 * ⛔ WHAT IT CANNOT SEE: flesh is a capsule, not the mesh; a solid thinner than a cell can fall
 * between grid samples; set `actor`s and the crowd are not tested; props named as ground/road/
 * floor/vegetation are not obstacles; a body is not tested against itself or against a board
 * (a board is points, not a volume). */
/* ---- clipaudit core { */
#define CA_FLOOR 1
#define CA_OB    2
#define CA_OH    4
#define CA_FILL  8
#define CA_REACH 16
#define CA_YLO   -1.5f     /* the sample band, absolute: a room's floor lands near y 0 */
#define CA_YHI    3.5f
#define CA_FLOORHI 0.25f   /* a walkable surface above this is a ledge/bench/step TOP, not the floor */
#define CA_BAND   3.0f     /* obstacle geometry counts up to floor+3.0 (reva is 3.45: her crown clears it) */
typedef struct { int W,H; float x0,z0,cell,obMin; float*fy,*ty,*by; unsigned char*fl,*nd,*ns; } CaGrid;
static void caGridFree(CaGrid*G){
    free(G->fy); free(G->ty); free(G->by); free(G->fl); free(G->nd); free(G->ns);
    memset(G,0,sizeof*G); }
static int caGridAlloc(CaGrid*G,float x0,float z0,float x1,float z1,float cell,double maxCells){
    caGridFree(G);
    if(!(x1>x0)||!(z1>z0)||!(cell>0.f)) return 0;
    double area=(double)(x1-x0)*(double)(z1-z0);
    if(area/((double)cell*(double)cell)>maxCells) cell=(float)(sqrt(area/maxCells)*1.001);
    G->cell=cell; G->x0=x0; G->z0=z0; G->obMin=0.05f;
    G->W=(int)ceil((x1-x0)/cell)+1; G->H=(int)ceil((z1-z0)/cell)+1;
    size_t n=(size_t)G->W*(size_t)G->H;
    G->fy=malloc(n*sizeof(float)); G->ty=malloc(n*sizeof(float)); G->by=malloc(n*sizeof(float));
    G->fl=calloc(n,1); G->nd=calloc(n,1); G->ns=calloc(n,1);
    if(!G->fy||!G->ty||!G->by||!G->fl||!G->nd||!G->ns){ caGridFree(G); return 0; }
    for(size_t k=0;k<n;k++){ G->fy[k]=1e9f; G->ty[k]=-1e9f; G->by[k]=1e9f; }
    return 1; }
/* One mesh through its world matrix. Every triangle in the band is SAMPLED at half a cell, so
 * a cell learns the heights of the surface actually over it, not a bounding box's. Returns
 * how many triangles reached the band. solid=1 (a prop): every sample is simply HEIGHT. */
static unsigned caGridMesh(CaGrid*G,const float*M,const float*pos,unsigned nv,const unsigned*idx,unsigned ni,int solid){
    if(!G->fl||!pos||nv<3) return 0;
    float det=M[0]*(M[5]*M[10]-M[6]*M[9])-M[4]*(M[1]*M[10]-M[2]*M[9])+M[8]*(M[1]*M[6]-M[2]*M[5]);
    float wsg=(det<0.f)?-1.f:1.f;        /* a mirrored matrix flips the winding, and so which way is up */
    float*wv=malloc((size_t)nv*12); if(!wv) return 0;
    for(unsigned v=0;v<nv;v++){ const float*p=pos+(size_t)v*3; float*o=wv+(size_t)v*3;
        o[0]=M[0]*p[0]+M[4]*p[1]+M[8]*p[2]+M[12];
        o[1]=M[1]*p[0]+M[5]*p[1]+M[9]*p[2]+M[13];
        o[2]=M[2]*p[0]+M[6]*p[1]+M[10]*p[2]+M[14]; }
    unsigned nt=ni?ni/3:nv/3, used=0;
    const float inv=1.f/G->cell, half=G->cell*0.5f;
    const float gx1=G->x0+(float)G->W*G->cell, gz1=G->z0+(float)G->H*G->cell;
    for(unsigned t=0;t<nt;t++){
        unsigned ia=ni?idx[t*3]:t*3, ib=ni?idx[t*3+1]:t*3+1, ic=ni?idx[t*3+2]:t*3+2;
        if(ia>=nv||ib>=nv||ic>=nv) continue;
        const float*a=wv+(size_t)ia*3,*b=wv+(size_t)ib*3,*c=wv+(size_t)ic*3;
        float ylo=fminf(a[1],fminf(b[1],c[1])), yhi=fmaxf(a[1],fmaxf(b[1],c[1]));
        if(yhi<CA_YLO||ylo>CA_YHI) continue;
        float xlo=fminf(a[0],fminf(b[0],c[0])), xhi=fmaxf(a[0],fmaxf(b[0],c[0]));
        float zlo=fminf(a[2],fminf(b[2],c[2])), zhi=fmaxf(a[2],fmaxf(b[2],c[2]));
        if(xhi<G->x0||xlo>gx1||zhi<G->z0||zlo>gz1) continue;
        float e1[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]}, e2[3]={c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        float nx=e1[1]*e2[2]-e1[2]*e2[1], ny=e1[2]*e2[0]-e1[0]*e2[2], nz=e1[0]*e2[1]-e1[1]*e2[0];
        float nl=sqrtf(nx*nx+ny*ny+nz*nz);
        if(!(nl>1e-12f)) continue;
        float up=wsg*ny/nl;
        int cls=(up>0.7f)?0:((up<-0.5f)?2:1);      /* 0 walkable, 1 steep, 2 faces down */
        float l1=e1[0]*e1[0]+e1[1]*e1[1]+e1[2]*e1[2], l2=e2[0]*e2[0]+e2[1]*e2[1]+e2[2]*e2[2];
        float e3x=c[0]-b[0], e3y=c[1]-b[1], e3z=c[2]-b[2], l3=e3x*e3x+e3y*e3y+e3z*e3z;
        int n=(int)ceil(sqrtf(fmaxf(l1,fmaxf(l2,l3)))/half);
        if(n<1) n=1;
        if(n>256) n=256;
        float sn=1.f/(float)n;
        used++;
        for(int i=0;i<=n;i++) for(int j=0;j<=n-i;j++){
            float u=(float)i*sn, w=(float)j*sn;
            float px=a[0]+e1[0]*u+e2[0]*w, py=a[1]+e1[1]*u+e2[1]*w, pz=a[2]+e1[2]*u+e2[2]*w;
            if(py<CA_YLO||py>CA_YHI) continue;
            int cx=(int)floor((px-G->x0)*inv), cz=(int)floor((pz-G->z0)*inv);
            if(cx<0||cz<0||cx>=G->W||cz>=G->H) continue;
            size_t k=(size_t)cz*(size_t)G->W+(size_t)cx;
            if(solid){ if(py>G->ty[k]) G->ty[k]=py; if(G->ns[k]<255) G->ns[k]++; }
            else if(cls==0&&py<=CA_FLOORHI){ if(py<G->fy[k]) G->fy[k]=py; G->fl[k]|=CA_FLOOR; }
            else if(cls==2){ if(py>CA_FLOORHI){ if(py<G->by[k]) G->by[k]=py; if(G->nd[k]<255) G->nd[k]++; } }
            else { if(py>G->ty[k]) G->ty[k]=py; if(G->ns[k]<255) G->ns[k]++; } } }
    free(wv); return used; }
/* ⛔ A SOLID PROP IS STAMPED, NOT SAMPLED INTO THE ROOM. The room's floor runs on UNDER a prop,
 * so the flood from the open floor starts inside a hollow prop and never fills it: the harness
 * on the real skatepark set showed the CRT as a ring with open floor in the middle and a body
 * standing in the cabinet scored clean. Each prop is solidified on its own little grid, flooded
 * from ITS border, and its footprint raised into the world grid before the world is finished. */
typedef struct { float M[16]; const float*pos; unsigned nv; const unsigned*idx; unsigned ni; } CaMesh;
static unsigned caGridProp(CaGrid*G,const CaMesh*ms,int n){
    if(!G->fl||n<=0) return 0;
    float x0=1e9f, x1=-1e9f, z0=1e9f, z1=-1e9f;
    for(int m=0;m<n;m++){ const float*M=ms[m].M;
        for(unsigned v=0;v<ms[m].nv;v++){ const float*p=ms[m].pos+(size_t)v*3;
            float y=M[1]*p[0]+M[5]*p[1]+M[9]*p[2]+M[13];
            if(y<CA_YLO||y>CA_YHI) continue;
            float x=M[0]*p[0]+M[4]*p[1]+M[8]*p[2]+M[12], z=M[2]*p[0]+M[6]*p[1]+M[10]*p[2]+M[14];
            if(x<x0) x0=x;
            if(x>x1) x1=x;
            if(z<z0) z0=z;
            if(z>z1) z1=z; } }
    if(!(x1>=x0)) return 0;
    const float cl=G->cell;
    int ox=(int)floor((x0-G->x0)/cl)-2, oz=(int)floor((z0-G->z0)/cl)-2;   /* on the world's own lattice */
    int ex=(int)floor((x1-G->x0)/cl)+3, ez=(int)floor((z1-G->z0)/cl)+3;
    CaGrid L; memset(&L,0,sizeof L);
    if(!caGridAlloc(&L,G->x0+(float)ox*cl,G->z0+(float)oz*cl,G->x0+(float)ex*cl-0.5f*cl,G->z0+(float)ez*cl-0.5f*cl,cl,1e12)) return 0;
    unsigned tris=0;
    for(int m=0;m<n;m++) tris+=caGridMesh(&L,ms[m].M,ms[m].pos,ms[m].nv,ms[m].idx,ms[m].ni,1);
    const int W=L.W, H=L.H; const size_t nn=(size_t)W*(size_t)H;
    float topMax=-1e9f;
    for(int cz=0;cz<H;cz++) for(int cx=0;cx<W;cx++){ int k=cz*W+cx, gx=cx+ox, gz=cz+oz; float base=0.f;
        if(gx>=0&&gz>=0&&gx<G->W&&gz<G->H){ int gk=gz*G->W+gx; if(G->fl[gk]&CA_FLOOR) base=G->fy[gk]; }
        if(L.ty[k]>base+G->obMin){ L.fl[k]|=CA_OB; if(L.ty[k]>topMax) topMax=L.ty[k]; } }
    int*q=malloc(nn*sizeof(int));
    if(q){ size_t qh=0, qt=0;
        for(int cz=0;cz<H;cz++) for(int cx=0;cx<W;cx++){ int k=cz*W+cx;
            if((cx==0||cz==0||cx==W-1||cz==H-1)&&!(L.fl[k]&CA_OB)){ L.fl[k]|=CA_REACH; q[qt++]=k; } }
        while(qh<qt){ int k=q[qh++], cx=k%W, cz=k/W;
            int nb[4]={ cx>0?k-1:-1, cx<W-1?k+1:-1, cz>0?k-W:-1, cz<H-1?k+W:-1 };
            for(int m=0;m<4;m++){ int j=nb[m];
                if(j<0||(L.fl[j]&(CA_OB|CA_REACH))) continue;
                L.fl[j]|=CA_REACH; q[qt++]=j; } }
        free(q);
        for(size_t k=0;k<nn;k++) if(!(L.fl[k]&(CA_OB|CA_REACH))){ L.fl[k]|=CA_FILL; L.ty[k]=-1e9f; }
        for(int pass=0;pass<24;pass++){ int changed=0;
            for(int dir=0;dir<2;dir++) for(size_t s=0;s<nn;s++){ int k=dir?(int)(nn-1-s):(int)s;
                if(!(L.fl[k]&CA_FILL)) continue;
                int cx=k%W, cz=k/W;
                int nb[4]={ cx>0?k-1:-1, cx<W-1?k+1:-1, cz>0?k-W:-1, cz<H-1?k+W:-1 };
                for(int m=0;m<4;m++){ int j=nb[m];
                    if(j<0||!(L.fl[j]&(CA_OB|CA_FILL))) continue;
                    if(L.ty[j]>L.ty[k]+1e-4f){ L.ty[k]=L.ty[j]; changed=1; } } }
            if(!changed) break; } }
    for(int cz=0;cz<H;cz++) for(int cx=0;cx<W;cx++){ int k=cz*W+cx, gx=cx+ox, gz=cz+oz;
        if(!(L.fl[k]&(CA_OB|CA_FILL))||gx<0||gz<0||gx>=G->W||gz>=G->H) continue;
        float t=(L.ty[k]<-1e8f)?topMax:L.ty[k];
        int gk=gz*G->W+gx;
        if(t>G->ty[gk]) G->ty[gk]=t;
        G->ns[gk]=255; }                       /* a prop is an obstacle, never an overhang */
    caGridFree(&L);
    return tris; }
static int caCmpF(const void*a,const void*b){ float x=*(const float*)a, y=*(const float*)b; return (x>y)-(x<y); }
static void caGridFinish(CaGrid*G,int*nOb,int*nFill,int*nOh,int*nFloor,float*p5,float*p95){
    const int W=G->W, H=G->H; const size_t n=(size_t)W*(size_t)H;
    int ob=0, oh=0, nf=0, fill=0;
    for(size_t k=0;k<n;k++){ unsigned char f=G->fl[k];
        if(!(f&CA_FLOOR)) G->fy[k]=0.f; else nf++;
        float fy=G->fy[k];
        if(G->by[k]>fy+0.5f&&G->by[k]<fy+CA_BAND&&(int)G->nd[k]*2>=(int)G->ns[k]){ f|=CA_OH; oh++; }
        else if(G->ty[k]>fy+G->obMin){ f|=CA_OB; ob++; if(G->ty[k]>fy+CA_BAND) G->ty[k]=fy+CA_BAND; }
        G->fl[k]=f; }
    /* THE HOLLOW SHELL. Flood from the open floor; a cell the flood never reaches that is not a
     * surface itself is INSIDE something, and takes the top and floor of what surrounds it.
     * ⛔ Only with a floor to flood from: with none, everything would be "inside". */
    int*q=(nf>0)?malloc(n*sizeof(int)):NULL;
    if(q){ size_t qh=0, qt=0;
        for(size_t k=0;k<n;k++) if((G->fl[k]&CA_FLOOR)&&!(G->fl[k]&CA_OB)){ G->fl[k]|=CA_REACH; q[qt++]=(int)k; }
        while(qh<qt){ int k=q[qh++], cx=k%W, cz=k/W;
            int nb[4]={ cx>0?k-1:-1, cx<W-1?k+1:-1, cz>0?k-W:-1, cz<H-1?k+W:-1 };
            for(int m=0;m<4;m++){ int j=nb[m];
                if(j<0||(G->fl[j]&(CA_OB|CA_REACH))) continue;
                G->fl[j]|=CA_REACH; q[qt++]=j; } }
        free(q);
        for(size_t k=0;k<n;k++) if(!(G->fl[k]&(CA_OB|CA_REACH))){
            G->ty[k]=-1e9f; G->fl[k]=(unsigned char)((G->fl[k]|CA_FILL)&~CA_OH); fill++; }
        for(int pass=0;pass<24;pass++){ int changed=0;
            for(int dir=0;dir<2;dir++) for(size_t s=0;s<n;s++){ int k=dir?(int)(n-1-s):(int)s;
                if(!(G->fl[k]&CA_FILL)) continue;
                int cx=k%W, cz=k/W;
                int nb[4]={ cx>0?k-1:-1, cx<W-1?k+1:-1, cz>0?k-W:-1, cz<H-1?k+W:-1 };
                for(int m=0;m<4;m++){ int j=nb[m];
                    if(j<0||!(G->fl[j]&(CA_OB|CA_FILL))) continue;
                    if(G->ty[j]>G->ty[k]+1e-4f){ G->ty[k]=G->ty[j]; G->fy[k]=G->fy[j]; changed=1; } } }
            if(!changed) break; }
        for(size_t k=0;k<n;k++) if(G->fl[k]&CA_FILL){
            if(G->ty[k]<-1e8f) G->ty[k]=G->fy[k]+CA_BAND;
            G->fl[k]|=CA_OB; } }
    float*smp=malloc(65536*sizeof(float)); int ns=0;
    if(smp){ size_t stp=(nf>65536)?(size_t)nf/65536+1:1, cnt=0;
        for(size_t k=0;k<n&&ns<65536;k++) if(G->fl[k]&CA_FLOOR){ if((cnt++%stp)==0) smp[ns++]=G->fy[k]; }
        if(ns) qsort(smp,(size_t)ns,sizeof(float),caCmpF); }
    *p5=(smp&&ns)?smp[ns*5/100]:0.f; *p95=(smp&&ns)?smp[ns*95/100]:0.f;
    free(smp);
    *nOb=ob+fill; *nFill=fill; *nOh=oh; *nFloor=nf; }
/* How far (x,y,z) -- a point on a limb's centreline with `rad` of flesh around it -- is INTO the
 * world: under the floor (the point itself, no flesh), or inside an obstacle or an overhang. The
 * obstacle depth is the shortest way out -- up (or down) to the surface, or sideways to the
 * nearest open cell -- plus the flesh, less ONE CELL for the grid's own rasterization.
 * ⛔ IT USED TO SCORE 0 ANYWHERE WITHIN A CELL OF AN EDGE, and only joint centres were tested. On
 * a skatepark fixture reva stood with both shins through the CRT's cabinet (0.66 u deep) and scored
 * CLEAN: her ankles were within a cell of its face and her knees were level with its top.
 * *kind: 0 floor, 1 obstacle. No grid: the floor is y 0. */
static float caGridDepth(const CaGrid*G,float x,float y,float z,float rad,int*kind){
    *kind=0;
    if(!G->fl) return -y;
    const int W=G->W, H=G->H; const float cl=G->cell;
    int cx=(int)floor((x-G->x0)/cl), cz=(int)floor((z-G->z0)/cl);
    if(cx<0||cz<0||cx>=W||cz>=H) return -y;
    int k=cz*W+cx; float fy=G->fy[k], best=fy-y;
    unsigned char f=G->fl[k];
    if(!(f&(CA_OB|CA_OH))) return best;
    unsigned char want=(f&CA_OB)?CA_OB:CA_OH;
    float lim=(want==CA_OB)?1e9f:-1e9f;             /* over a slope: the lowest solid top around */
    for(int dz=-1;dz<=1;dz++) for(int dx=-1;dx<=1;dx++){ int ax=cx+dx, az=cz+dz;
        if(ax<0||az<0||ax>=W||az>=H) continue;
        int j=az*W+ax;
        if(!(G->fl[j]&want)) continue;
        if(want==CA_OB){ if(G->ty[j]<lim) lim=G->ty[j]; } else { if(G->by[j]>lim) lim=G->by[j]; } }
    float dv=(want==CA_OB)?((y>fy&&y<lim)?lim-y:0.f):((y>lim&&y<fy+CA_BAND)?y-lim:0.f);
    if(!(dv>0.f)) return best;
    float d2=1e9f; const int R=8;
    for(int r=1;r<=R;r++){
        for(int dz=-r;dz<=r;dz++) for(int dx=-r;dx<=r;dx++){
            if(dx!=-r&&dx!=r&&dz!=-r&&dz!=r) continue;
            int ax=cx+dx, az=cz+dz;
            if(ax>=0&&az>=0&&ax<W&&az<H&&(G->fl[az*W+ax]&want)) continue;
            float ox=G->x0+((float)ax+0.5f)*cl-x, oz=G->z0+((float)az+0.5f)*cl-z, e=ox*ox+oz*oz;
            if(e<d2) d2=e; }
        if(d2<(float)(r*r)*cl*cl) break; }              /* a farther ring cannot hold a nearer cell */
    float dh=(d2<1e9f)?sqrtf(d2)-0.5f*cl:(float)R*cl;
    if(dh<0.f) dh=0.f;
    float d=((dv<dh)?dv:dh)+rad-cl;
    if(d>best){ best=d; *kind=1; }
    return best; }
/* ---- } clipaudit core */
#define CA_CELL   0.10f
#define CA_MAXCELLS 6.0e6
#define CA_TOL    0.03f
#define CA_SLOTS  SCN_MAXCAST
#define CA_MAXPT  32
#define CA_MAXCAP 20
#define CA_MAXATT 128
#define CA_MAXSMP 384
typedef struct { float p[3]; const char*nm; } CaPt;
typedef struct {
    int drawn; char label[64];
    int np; CaPt pt[CA_MAXPT];
    int nc; float ca[CA_MAXCAP][3], cb[CA_MAXCAP][3], cr[CA_MAXCAP];
    int nsm; float sm[CA_MAXSMP][4]; const char*smn[CA_MAXSMP];   /* limb samples: x y z + flesh radius */
    int lf,lb; float lw,lt,lp[3]; char lwhat[28], linto[12];        /* this line */
    int tf,tb,tl; float tw,tt,tp[3]; char twhat[28], tinto[12];     /* this scene */
    /* ⭐ WHAT THE BODY RESTS ON -- the furniture under its raised mark, this frame (caSupport) */
    int nsup; float sup[6][4]; char supNm[6][12]; float supY;          /* x0 z0 x1 z1 footprints; the mark's height */
} CaSlot;
typedef struct { int slot; char what[28]; float p[3]; } CaAtt;
typedef struct { Pack*P; unsigned char*pk; int toe[2]; float ankH[2], headH, whH[8]; } CaRest;
static int g_caOn=-1;                 /* -1: REVA_CLIPAUDIT not read yet */
static CaGrid g_caG; static unsigned g_caSig=0; static int g_caSigOK=0;
static CaSlot g_caS[CA_SLOTS];
static CaAtt g_caAtt[CA_MAXATT]; static int g_caNAtt=0;
static CaRest g_caRest[16]; static int g_caNRest=0;
static int g_caInScene=0, g_caLine=-1, g_caScenes=0; static float g_caLastT=0.f;
/* ⛔ A FRAME BELONGS TO THE LINE IT WAS DRAWN UNDER. The loop draws the bodies, THEN advances the
 * scene clock (which lands the next line's marks for the NEXT draw), THEN this tests -- so reading
 * the line at test time filed the last frame at the old spot under the NEW line (the skatepark fixture:
 * one bad frame "at the pillar" on line 3, one "inside the co-star" on line 5). The first body or point
 * fed in a frame latches the line and the time the picture was actually drawn at. */
static int g_caFed=0, g_caFLine=-1; static float g_caFT=0.f, g_caT=0.f;
static void caLatch(void){ if(!g_caFed){ g_caFed=1; g_caFLine=scnStageLine(); g_caFT=g_scnT; } }
static FILE*g_caCsv=NULL;
static char g_caGridSay[200]="no set loaded (the floor is y 0)";
static void clipAuditExit(void);
static void caEnable(int on,const char*why);
static void caInit(void){
    if(g_caOn>=0) return;
    g_caOn=0;
    const char*e=getenv("REVA_CLIPAUDIT");
    if(e&&e[0]&&e[0]!='0') caEnable(1,"REVA_CLIPAUDIT"); }
static void caEnable(int on,const char*why){
    static int reg=0;
    if(g_caOn<0) caInit();
    g_caOn=on?1:0;
    if(on&&!reg){ reg=1; atexit(clipAuditExit); }
    if(on&&!g_caCsv){ const char*o=getenv("REVA_CLIPAUDIT_OUT");
        if(o&&o[0]){ g_caCsv=fopen(o,"w");
            if(g_caCsv) fprintf(g_caCsv,"t,line,slot,pack,what,into,depth,x,y,z\n");
            else printf("[clipaudit] could NOT open REVA_CLIPAUDIT_OUT %s\n",o); } }
    printf("[clipaudit] %s (%s): cast bodies vs floor, set and each other while a scene plays; tolerance %.2f u\n",
           on?"ON":"OFF",why,CA_TOL);
    fflush(stdout); }
static int caVeg(const char*s){
    static const char*V[]={"tree","bush","hedge","plant","flower","foliage","leaf","leaves","vine","ivy",NULL};
    for(int i=0;V[i];i++) if(strstr(s,V[i])) return 1;
    return 0; }
/* ⭐ EVERY SOLID PROP'S WORLD BOX (x0 y0 z0 x1 y1 z1), measured with the grid -- what caSupport
 * asks "is this under her mark". Walls, rooms and vegetation keep an empty box: they are never furniture. */
static float (*g_caIBox)[6]=NULL; static int g_caNIBox=0;
static void caGridBuild(void){
    caGridFree(&g_caG);
    clock_t c0=clock();
    int nRoom=0, nProp=0, nVeg=0; unsigned tris=0;
    float bx0=1e9f, bx1=-1e9f, bz0=1e9f, bz1=-1e9f;
    free(g_caIBox); g_caIBox=NULL; g_caNIBox=0;
    if(g_setNI>0&&(g_caIBox=malloc((size_t)g_setNI*sizeof *g_caIBox))){ g_caNIBox=g_setNI;
        for(int i=0;i<g_setNI;i++){ g_caIBox[i][0]=g_caIBox[i][1]=g_caIBox[i][2]=1e9f; g_caIBox[i][3]=g_caIBox[i][4]=g_caIBox[i][5]=-1e9f; } }
    for(int pass=0;pass<2;pass++){
        for(int i=0;i<g_setNI;i++){ SetInst*I=&g_setI[i]; const char*stem=g_setNm[I->pk];
            int wall=(strstr(stem,"wall")||strstr(stem,"window"))&&!strstr(stem,"attic");
            int solid=I->room||wall||propSolid(stem);
            /* ⭐ (10-22) A ROOM BUILT BY CODE IS NOT FURNITURE: a pack marked NoCast AND Open is a room's shell, its glow,
             * its glass (build_sickbay / build_rooms) -- floor, walls and ceiling in one prop, named for the room, so the name
             * test called it solid and the contact pass threw a body standing anywhere in the cabin up toward the ceiling */
            if(!I->room&&g_setPk[I->pk].open&&g_setPk[I->pk].nocast) solid=0;
            if(!solid) continue;
            if(!I->room&&caVeg(stem)){ if(pass==0) nVeg++; continue; }
            float*IB=(g_caIBox&&i<g_caNIBox&&!I->room&&!wall)?g_caIBox[i]:NULL;
            if(pass==0){ if(I->room) nRoom++; else nProp++; }
            Pack*Q=&g_setPk[I->pk];
            CaMesh ms[64]; int nm=0;
            for(unsigned mi=0;mi<Q->nMeshes;mi++){ PMesh*Mh=&Q->meshes[mi];
                if(!(Mh->flags&4)||Mh->boneCount>MAXB||Mh->verts<3||MOFF(Q,mi)) continue;
                M4 bind, Mw; memcpy(bind,Q->pk+Mh->offBind,64); m4_mul(Mw,I->m,bind);   /* what the shader does: model * bind * p */
                const float*pos=(const float*)(Q->pk+Mh->offPos);
                const unsigned*idx=Mh->idxCount?(const unsigned*)(Q->pk+Mh->offIdx):NULL;
                if(pass==0){ unsigned stp=(Mh->verts>4096)?Mh->verts/4096:1;
                    for(unsigned v=0;v<Mh->verts;v+=stp){ const float*p=pos+(size_t)v*3;
                        float x=Mw[0]*p[0]+Mw[4]*p[1]+Mw[8]*p[2]+Mw[12];
                        float y=Mw[1]*p[0]+Mw[5]*p[1]+Mw[9]*p[2]+Mw[13];
                        float z=Mw[2]*p[0]+Mw[6]*p[1]+Mw[10]*p[2]+Mw[14];
                        if(y<CA_YLO||y>CA_YHI) continue;
                        if(x<bx0) bx0=x;
                        if(x>bx1) bx1=x;
                        if(z<bz0) bz0=z;
                        if(z>bz1) bz1=z;
                        if(IB){ IB[0]=fminf(IB[0],x); IB[1]=fminf(IB[1],y); IB[2]=fminf(IB[2],z);
                                IB[3]=fmaxf(IB[3],x); IB[4]=fmaxf(IB[4],y); IB[5]=fmaxf(IB[5],z); } } }
                else if(I->room) tris+=caGridMesh(&g_caG,Mw,pos,Mh->verts,idx,Mh->idxCount,0);
                else { memcpy(ms[nm].M,Mw,64); ms[nm].pos=pos; ms[nm].nv=Mh->verts; ms[nm].idx=idx; ms[nm].ni=Mh->idxCount;
                       if(++nm==64){ tris+=caGridProp(&g_caG,ms,nm); nm=0; } } }
            if(pass==1&&nm) tris+=caGridProp(&g_caG,ms,nm); }
        if(pass==0){
            if(!(bx1>bx0)){
                snprintf(g_caGridSay,sizeof g_caGridSay,"NO room or solid prop in set %s (the floor is y 0, nothing else)",g_setCur);
                printf("[clipaudit] %s\n",g_caGridSay); fflush(stdout); return; }
            if(!caGridAlloc(&g_caG,bx0-0.5f,bz0-0.5f,bx1+0.5f,bz1+0.5f,CA_CELL,CA_MAXCELLS)){
                snprintf(g_caGridSay,sizeof g_caGridSay,"NO GRID: could not allocate one for set %s (the floor is y 0)",g_setCur);
                printf("[clipaudit] %s\n",g_caGridSay); fflush(stdout); return; }
            { const char*om=getenv("REVA_CLIPAUDIT_OBMIN"); if(om) g_caG.obMin=(float)atof(om); } } }
    int nOb=0, nFill=0, nOh=0, nFloor=0; float p5=0.f, p95=0.f;
    caGridFinish(&g_caG,&nOb,&nFill,&nOh,&nFloor,&p5,&p95);
    double sec=(double)(clock()-c0)/(double)CLOCKS_PER_SEC;
    printf("[clipaudit] room grid %d x %d cells, %d obstacle cells, floor y p5..p95 %.3f..%.3f"
           " -- cell %.2f u over x %.1f..%.1f z %.1f..%.1f; %d filled inside shells, %d overhead; %d floor cells;"
           " %u tris from %d room(s) + %d solid prop(s) (%d vegetation props not counted) in %.2f s\n",
           g_caG.W,g_caG.H,nOb,p5,p95,g_caG.cell,g_caG.x0,g_caG.x0+(float)g_caG.W*g_caG.cell,
           g_caG.z0,g_caG.z0+(float)g_caG.H*g_caG.cell,nFill,nOh,nFloor,tris,nRoom,nProp,nVeg,sec);
    if(!nFloor) printf("[clipaudit] WARNING: no walkable surface in y %.1f..%.2f -- every cell's floor is ASSUMED y 0\n",CA_YLO,CA_FLOORHI);
    snprintf(g_caGridSay,sizeof g_caGridSay,"set %s (grid %dx%d, %d obstacle cells, %d rooms + %d props)",
             g_setCur,g_caG.W,g_caG.H,nOb,nRoom,nProp);
    fflush(stdout); }
static unsigned caSetSig(void){
    unsigned h=2166136261u; h=(h^(unsigned)g_setNI)*16777619u;
    for(int i=0;i<g_setNI;i++){ unsigned b[4]={0,0,0,0}; memcpy(b,&g_setI[i].m[12],12); b[3]=(unsigned)g_setI[i].pk;
        for(int k=0;k<4;k++) h=(h^b[k])*16777619u; }
    return h; }
static void caGridSync(void){
    unsigned sig=caSetSig();
    if(g_caSigOK&&sig==g_caSig) return;
    g_caSig=sig; g_caSigOK=1;
    if(!g_setNI){ caGridFree(&g_caG); snprintf(g_caGridSay,sizeof g_caGridSay,"no set loaded (the floor is y 0)"); return; }
    caGridBuild(); }
static void clipAuditSetLoaded(void){ if(g_caOn<0) caInit(); if(g_caOn>0) caGridSync(); }
static float caRestY(Pack*P,int node){
    int ch[128], n=0;
    for(int a=node;a>=0&&(unsigned)a<P->nNodes&&n<128;a=P->nodes[a].parent) ch[n++]=a;
    M4 w; memset(w,0,64); w[0]=w[5]=w[10]=w[15]=1.f;
    for(int i=n-1;i>=0;i--){ M4 loc; float t[3],q[4],s[3];
        memcpy(t,P->nodes[ch[i]].t,12); memcpy(q,P->nodes[ch[i]].q,16); memcpy(s,P->nodes[ch[i]].s,12);
        m4_trs(loc,t,q,s); m4_mul(w,w,loc); }
    return w[13]; }
/* The rest pose, measured once per body: how far each ankle and wheel sits above the sole, and
 * the head's height -- the numbers that turn joints into a sole and a body height. */
static CaRest*caRestOf(Pack*P){
    for(int i=0;i<g_caNRest;i++) if(g_caRest[i].P==P&&g_caRest[i].pk==P->pk) return &g_caRest[i];
    CaRest*R=&g_caRest[(g_caNRest<16)?g_caNRest++:15];
    memset(R,0,sizeof*R); R->P=P; R->pk=P->pk; R->toe[0]=R->toe[1]=-1;
    int ft[2]={P->footL,P->footR};
    for(int s=0;s<2;s++){ if(ft[s]<0||(unsigned)ft[s]>=P->nNodes) continue;
        R->ankH[s]=caRestY(P,ft[s])-P->mny;
        if(R->ankH[s]<0.f) R->ankH[s]=0.f;
        for(unsigned n=0;n<P->nNodes;n++) if(P->nodes[n].parent==ft[s]&&P->nodes[n].kind==1){ R->toe[s]=(int)n; break; } }
    R->headH=(P->pb[5]>=0)?caRestY(P,P->pb[5])-P->mny:0.f;
    for(int w=0;w<P->nWheel&&w<8;w++){
        float h=(P->wheel[w]>=0&&(unsigned)P->wheel[w]<P->nNodes)?caRestY(P,P->wheel[w])-P->mny:0.f;
        R->whH[w]=(h>0.f)?h:0.f; }
    return R; }
/* ⭐ THE FURNITURE UNDER A BODY'S MARK IS ITS FLOOR. The co-star lies on the bed by a `mark` with a
 * height, and the bed is a solid prop -- stamped into the grid as an obstacle up to its mattress top -- so
 * every sample of a woman lying IN the bedding (the mark is 0.12 under the duvet on purpose) read as a limb
 * through the bed, every frame of every bed take.
 * A body on a RAISED mark (y > CA_FLOORHI) rests on whatever is under the mark: every solid prop whose
 * box holds the mark's x,z and starts below it, and every `solid` box (the invisible mattress under a
 * simulated sheet) that holds it. Over those footprints, for THAT body only, a cell's top is not an
 * obstacle but a SURFACE: it may sink CA_SINK into it (a mattress under a duvet, a cushion) and a sample
 * deeper than that fires, named for the furniture -- "into bed_01". A top more than CA_SUPHI above the mark
 * (a headboard, a canopy post) is still an OBSTACLE. Everyone else, and this body anywhere else -- through
 * a wall, the armchair, the bed from the floor -- is tested exactly as before. */
#define CA_SINK  0.25f
#define CA_SUPHI 0.60f
static void caSupport(int slot,CaSlot*S){
    S->nsup=0; S->supY=0.f;
    SetMark*MK=(slot==0)?g_heroMk:markFind(g_castMark[slot]);
    if(!MK||!(MK->y>CA_FLOORHI)) return;
    S->supY=MK->y;
    for(int i=0;i<g_caNIBox&&i<g_setNI&&S->nsup<6;i++){ const float*B=g_caIBox[i];
        if(!(B[3]>=B[0])||MK->x<B[0]||MK->x>B[3]||MK->z<B[2]||MK->z>B[5]||B[1]>MK->y) continue;
        float*r=S->sup[S->nsup]; r[0]=B[0]; r[1]=B[2]; r[2]=B[3]; r[3]=B[5];
        snprintf(S->supNm[S->nsup],12,"%s",g_setNm[g_setI[i].pk]); S->nsup++; }
    for(int q=0;q<g_nSclBox&&S->nsup<6;q++){ const float*B=g_sclBox[q];
        if(fabsf(MK->x-B[0])>B[3]||fabsf(MK->z-B[2])>B[5]||B[1]-B[4]>MK->y) continue;
        float*r=S->sup[S->nsup]; r[0]=B[0]-B[3]; r[1]=B[2]-B[5]; r[2]=B[0]+B[3]; r[3]=B[2]+B[5];
        snprintf(S->supNm[S->nsup],12,"solid box"); S->nsup++; }
    static char said9[CA_SLOTS][32];
    if(strcmp(said9[slot],MK->name)){ snprintf(said9[slot],32,"%s",MK->name);
        printf("[clipaudit] slot %d rests on mark '%s' (y %.2f):",slot,MK->name,MK->y);
        if(!S->nsup) printf(" NOTHING under it in the grid -- tested as before");
        for(int k=0;k<S->nsup;k++) printf(" %s",S->supNm[k]);
        if(S->nsup) printf(" -- its top is her floor there (sink %.2f u), anything over %.2f u above the mark still an obstacle",CA_SINK,CA_SUPHI);
        printf("\n"); fflush(stdout); } }
/* the surface this body rests on at p, or -1e9 if p is not over its furniture */
