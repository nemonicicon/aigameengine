/* ---- WORN CLOTH ON A GAME'S BODY, BY THE ENGINE'S OWN SOLVER (11-16). reva_native/clubcloth is the engine's solver for a whole garment on a body that moves
 * fast (clubcloth.h says what it is and why). The skate game's Godot copy has run it on the alien's tee and shorts since 11-02, and the port of that game
 * here drew the same clothes stiff, as skinned meshes, with the body's chest and stomach in the shirt.
 * A game asks it of a body with E->bodyCloth(i, "<file>"): what each piece is made of, what's worn over what, which bones make a part, and the
 * capsules under a slot (a rider profile's cloth_* tables, as text: cwSpecRead says the lines). A garment is cloth when its pack carries the tailor's
 * numbers for it (a CLTH chunk, engine/load.inc packTailor) and the body has it on. Everything else in the pack that's skinned is the body under it,
 * the skin hidden under the clothes included: that skin is the backstop.
 * Each picture, once a body (cwStep): its pose before any mirroring as the palette (pose x inverse bind, metres, its own space); the air past it and
 * the push on it from how its origin moves, eased as their rider's tick_cloth eases them; how it's turning, as club_cloth.gd works that out; then every
 * garment posed, layered (the one under first) and solved. Each garment's mesh is drawn from the solver's points as the skirt is (cloth.inc: buffers
 * of its own holding world positions, drawn with cloth==2) and casts from them. A mirrored body (E->bodyMirror) is the reflection of its plain pose,
 * so the cloth's solved on the plain pose, and drawn and cast reflected.
 * Picture, not rules: nothing of it is in [simhash], and it steps on the tick clock between the pictures it's drawn in.
 * Not done: the board as a box it keeps out of (cc_set_boxes), the finer mesh the solver draws (cc_drawn_*: here the garment's own triangles are drawn,
 * from its points and their normals), the garments on threads of their own, their look's nudge of the shadow lookup out of the cloth (sun_out). */
#include "../clubcloth/clubcloth.c"

#define CW_BODIES 8
#define CW_GARMENTS 8
#define CW_CAPS 16
#define CW_UPM 1.75f   /* the engine's units a metre: the solver works in metres */
typedef struct { char piece[32]; float f[11]; int n; } CwFab;
typedef struct { char slot[24], a[48], b[48]; int press; } CwCap;
typedef struct {
    int on, slot, built, failed; Pack*pk; unsigned long long worn; char path[160];
    /* the spec */
    int nFab; CwFab fab[16]; int nOver; char over[8][2][24]; int nPart; char part[16][4][48]; int partN[16]; int nCap; CwCap cap[48];
    /* the palette: a pack node's slot in it, each slot's node, inverse bind (3x4 rows, metres), parent */
    int nb, *nodeOf, *palNode, *parent; float *bind, *pose, *pal;
    /* the body under the clothes */
    int bnv, bni, *bbon, *bidx; float *bpos, *bwgt;
    /* the garments: the solver's, the pack mesh each is, its slot, the one it's over, its capsules, and the buffers it's drawn from */
    int ng; CCGarment *g[CW_GARMENTS]; int mesh[CW_GARMENTS], under[CW_GARMENTS]; char gslot[CW_GARMENTS][24];
    int ncap[CW_GARMENTS], capA[CW_GARMENTS][CW_CAPS], capB[CW_GARMENTS][CW_CAPS], capP[CW_GARMENTS][CW_CAPS]; float capR[CW_GARMENTS][CW_CAPS];
    unsigned vbo[CW_GARMENTS][2]; float *vpos[CW_GARMENTS], *vnrm[CW_GARMENTS]; unsigned upStamp[CW_GARMENTS];
    /* the picture before */
    unsigned stamp; double lastMs; int fromOk, rotOk; float from[3], air[3], acc[3], rotWas[9], turn[3], turnAcc[3];
} CwBody;
static CwBody g_cw[CW_BODIES];
static int g_cwSay=-1;   /* REVA_CWSAY=1: what each body's clothes were built from, and once a second how long a solve took */

static void cwFree(CwBody*B){
    for(int k=0;k<B->ng;k++){ if(B->g[k]) cc_free(B->g[k]); free(B->vpos[k]); free(B->vnrm[k]); if(B->vbo[k][0]) gfxBuffersDrop(2,B->vbo[k]); }
    free(B->nodeOf); free(B->palNode); free(B->parent); free(B->bind); free(B->pose); free(B->pal);
    free(B->bbon); free(B->bidx); free(B->bpos); free(B->bwgt);
    { int on=B->on, slot=B->slot; char p[160]; memcpy(p,B->path,sizeof p); CwFab f[16]; memcpy(f,B->fab,sizeof f); int nf=B->nFab, no=B->nOver, np=B->nPart, nc=B->nCap;
      char ov[8][2][24]; memcpy(ov,B->over,sizeof ov); char pt[16][4][48]; memcpy(pt,B->part,sizeof pt); int pn[16]; memcpy(pn,B->partN,sizeof pn); CwCap cp[48]; memcpy(cp,B->cap,sizeof cp);
      memset(B,0,sizeof *B); B->on=on; B->slot=slot; memcpy(B->path,p,sizeof p); memcpy(B->fab,f,sizeof f); B->nFab=nf; B->nOver=no; B->nPart=np; B->nCap=nc;
      memcpy(B->over,ov,sizeof ov); memcpy(B->part,pt,sizeof pt); memcpy(B->partN,pn,sizeof pn); memcpy(B->cap,cp,sizeof cp); } }

/* The spec, a line each ('#' a comment), names as the pack has them:
 *   fabric <piece or slot> <kg/m^2> <bending length, cm> <shear, N/m> [drive damp flex_damp hem substep slack friction lag]   (cc_fabric, then CCParams)
 *   over <slot> <the slot it's worn over>         a top over the legs: it stays outside them (cc_set_under)
 *   part <bone> [bone...]                         bones that make a part of a garment; parts keep out of each other (cc_set_parts)
 *   capsule <slot> <bone> <bone at its other end> <press 0|1>   a limb that comes up under that slot's clothes; its radius is measured off the skin */
static int cwSpecRead(CwBody*B,const char*path){
    FILE*f=fopen(path,"r"); if(!f){ printf("[cloth] no %s: the body's clothes stay as they're skinned\n",path); fflush(stdout); return 0; }
    char ln[512]; int n=0;
    while(fgets(ln,sizeof ln,f)){ char*h=strchr(ln,'#'); if(h) *h=0; char w[16][64]; int k=0;
        for(char*t=strtok(ln," \t\r\n");t&&k<16;t=strtok(NULL," \t\r\n")) snprintf(w[k++],64,"%s",t);
        if(!k) continue;
        if(!strcmp(w[0],"fabric")&&k>=5&&B->nFab<16){ CwFab*F=&B->fab[B->nFab++]; snprintf(F->piece,sizeof F->piece,"%.31s",w[1]); F->n=0;
            for(int i=2;i<k&&F->n<11;i++){ const float v=(float)atof(w[i]); F->f[F->n++]=(v==v)?v:0.f; } n++; }
        else if(!strcmp(w[0],"over")&&k>=3&&B->nOver<8){ snprintf(B->over[B->nOver][0],24,"%.23s",w[1]); snprintf(B->over[B->nOver][1],24,"%.23s",w[2]); B->nOver++; n++; }
        else if(!strcmp(w[0],"part")&&k>=2&&B->nPart<16){ int m=0; for(int i=1;i<k&&m<4;i++) snprintf(B->part[B->nPart][m++],48,"%.47s",w[i]); B->partN[B->nPart++]=m; n++; }
        else if(!strcmp(w[0],"capsule")&&k>=5&&B->nCap<48){ CwCap*C=&B->cap[B->nCap++]; snprintf(C->slot,sizeof C->slot,"%.23s",w[1]); snprintf(C->a,sizeof C->a,"%.47s",w[2]); snprintf(C->b,sizeof C->b,"%.47s",w[3]); C->press=atoi(w[4])!=0; n++; }
        else { printf("[cloth] %s: what's '%s'?\n",path,w[0]); fflush(stdout); } }
    fclose(f); return n; }

static int cwNode(Pack*P,const char*name){ for(unsigned i=0;i<P->nNodes;i++){ const char*s=packStr(P,P->nodes[i].name); if(s&&!strcmp(s,name)) return (int)i; } return -1; }
/* a 4x4 (columns) as the solver's 3x4 by rows, its move in metres */
static void cwRows(const float*m,float*o){ for(int r=0;r<3;r++){ o[r*4]=m[r]; o[r*4+1]=m[4+r]; o[r*4+2]=m[8+r]; o[r*4+3]=m[12+r]/CW_UPM; } }
/* a 3x4's inverse's move: where the bone is at rest, from its inverse bind */
static void cwRestOrigin(const float*m,float*o){
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g); if(fabsf(det)<1e-12f){ o[0]=o[1]=o[2]=0.f; return; }
    const float inv[9]={(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det,(f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det,(d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det};
    const float tx=m[3],ty=m[7],tz=m[11];
    o[0]=-(inv[0]*tx+inv[1]*ty+inv[2]*tz); o[1]=-(inv[3]*tx+inv[4]*ty+inv[5]*tz); o[2]=-(inv[6]*tx+inv[7]*ty+inv[8]*tz); }
static int cwIsGarment(Pack*P,unsigned mi){ const char*n=packStr(P,P->meshes[mi].name); return n&&!strncmp(n,"wear~",5); }
/* a mesh's rest positions in the skeleton's space, metres, its weights and its bones as palette slots */
static void cwMeshRead(CwBody*B,Pack*P,unsigned mi,float*pos,float*wgt,int*bon,int*idx){
    const PMesh*M=&P->meshes[mi]; float bindm[16]; memcpy(bindm,P->pk+M->offBind,64);
    for(unsigned v=0;v<M->verts;v++){ float p[3]; memcpy(p,P->pk+M->offPos+(size_t)v*12,12);
        for(int k=0;k<3;k++) pos[v*3+k]=(bindm[k]*p[0]+bindm[4+k]*p[1]+bindm[8+k]*p[2]+bindm[12+k])/CW_UPM;
        const unsigned char*jp=P->pk+M->offJoints+(size_t)v*4, *wp=P->pk+M->offWeights+(size_t)v*4; float s=0.f;
        for(int q=0;q<4;q++){ unsigned short nd=0; const unsigned j=jp[q]; if(j<M->boneCount) memcpy(&nd,P->pk+M->offBoneMap+(size_t)j*2,2);   /* a bone map is 16 bits a joint (body.inc bodyPalette) */
            const int sl=(j<M->boneCount&&nd<P->nNodes)?B->nodeOf[nd]:-1; bon[v*4+q]=sl<0?0:sl; wgt[v*4+q]=sl<0?0.f:wp[q]/255.f; s+=wgt[v*4+q]; }
        if(s>1e-6f) for(int q=0;q<4;q++) wgt[v*4+q]/=s; else { wgt[v*4]=1.f; } }
    if(idx) for(unsigned t=0;t<M->idxCount;t++){ unsigned x; memcpy(&x,P->pk+M->offIdx+(size_t)t*4,4); idx[t]=(int)(x<M->verts?x:0); } }

static int cwBuild(CwBody*B,Pack*P){
    if(!P->meshTail){ if(!B->failed){ printf("[cloth] %s has no tailor's numbers on any garment (no CLTH chunk): its clothes stay as they're skinned\n",P->label); fflush(stdout); } B->failed=1; return 0; }
    /* the palette: every node a skinned mesh's bones name, in the order they're first named; and the capsules' bones */
    B->nodeOf=(int*)malloc(sizeof(int)*P->nNodes); for(unsigned i=0;i<P->nNodes;i++) B->nodeOf[i]=-1;
    B->palNode=(int*)malloc(sizeof(int)*P->nNodes); B->bind=(float*)calloc((size_t)P->nNodes*12,sizeof(float)); B->nb=0;
    for(unsigned mi=0;mi<P->nMeshes;mi++){ const PMesh*M=&P->meshes[mi]; if(!(M->flags&4)||M->boneCount==0||M->boneCount>MAXB) continue;
        for(unsigned j=0;j<M->boneCount;j++){ unsigned short nd; memcpy(&nd,P->pk+M->offBoneMap+(size_t)j*2,2); if(nd>=P->nNodes||B->nodeOf[nd]>=0) continue;
            float ibm[16]; memcpy(ibm,P->pk+M->offIBM+(size_t)j*64,64); B->nodeOf[nd]=B->nb; B->palNode[B->nb]=(int)nd; cwRows(ibm,B->bind+B->nb*12); B->nb++; } }
    if(!B->nb){ B->failed=1; return 0; }
    B->parent=(int*)malloc(sizeof(int)*B->nb); B->pose=(float*)calloc((size_t)B->nb*12,sizeof(float)); B->pal=(float*)calloc((size_t)B->nb*12,sizeof(float));
    for(int i=0;i<B->nb;i++){ int up=P->nodes[B->palNode[i]].parent;   /* a spring chain's bones count as the bone they hang from, as club_cloth.gd has it */
        while(up>=0&&(unsigned)up<P->nNodes){ const char*s=packStr(P,P->nodes[up].name); if(!s||strncmp(s,"Cloth_",6)) break; up=P->nodes[up].parent; }
        B->parent[i]=(up>=0&&(unsigned)up<P->nNodes)?B->nodeOf[up]:-1; }
    /* the body: every skinned mesh that isn't a garment, the hidden skin under the clothes included */
    int bnv=0,bni=0; for(unsigned mi=0;mi<P->nMeshes;mi++){ const PMesh*M=&P->meshes[mi]; if(!(M->flags&4)||M->boneCount==0||M->boneCount>MAXB||cwIsGarment(P,mi)) continue; bnv+=(int)M->verts; bni+=(int)M->idxCount; }
    B->bpos=(float*)malloc(sizeof(float)*3*(size_t)(bnv?bnv:1)); B->bwgt=(float*)malloc(sizeof(float)*4*(size_t)(bnv?bnv:1)); B->bbon=(int*)malloc(sizeof(int)*4*(size_t)(bnv?bnv:1)); B->bidx=(int*)malloc(sizeof(int)*(size_t)(bni?bni:1));
    { int v0=0,i0=0; for(unsigned mi=0;mi<P->nMeshes;mi++){ const PMesh*M=&P->meshes[mi]; if(!(M->flags&4)||M->boneCount==0||M->boneCount>MAXB||cwIsGarment(P,mi)) continue;
          cwMeshRead(B,P,mi,B->bpos+v0*3,B->bwgt+v0*4,B->bbon+v0*4,B->bidx+i0); for(unsigned t=0;t<M->idxCount;t++) B->bidx[i0+t]+=v0; v0+=(int)M->verts; i0+=(int)M->idxCount; } }
    B->bnv=bnv; B->bni=bni;
    const CCBody body={B->bnv,B->bpos,B->bwgt,B->bbon,B->bni,B->bidx};
    /* which part each bone is on: the nearest bone above it (itself first) that a part names */
    int*part=(int*)calloc((size_t)B->nb,sizeof(int));
    for(int i=0;i<B->nb;i++){ int up=B->palNode[i], found=0;
        while(up>=0&&(unsigned)up<P->nNodes&&!found){ const char*s=packStr(P,P->nodes[up].name);
            for(int p=0;p<B->nPart&&!found;p++) for(int m=0;m<B->partN[p];m++) if(s&&!strcmp(s,B->part[p][m])){ part[i]=p+1; found=1; break; }
            up=P->nodes[up].parent; } }
    /* the garments: each worn mesh named wear~<slot>~<piece>~... with the tailor's numbers */
    for(unsigned mi=0;mi<P->nMeshes&&B->ng<CW_GARMENTS;mi++){ const PMesh*M=&P->meshes[mi];
        if(!(M->flags&4)||M->boneCount==0||M->boneCount>MAXB||!cwIsGarment(P,mi)||!P->meshTail[mi]||MOFF(P,mi)) continue;
        char nm[160]; snprintf(nm,sizeof nm,"%s",packStr(P,M->name)); char*f1=nm+5, *f2=strchr(f1,'~'); if(!f2) continue; *f2++=0; char*f3=strchr(f2,'~'); if(f3) *f3=0;
        const char*slot=f1,*piece=f2;
        const int nv=(int)M->verts; float*pos=(float*)malloc(sizeof(float)*3*(size_t)nv),*wgt=(float*)malloc(sizeof(float)*4*(size_t)nv),*lim=(float*)malloc(sizeof(float)*(size_t)nv),*nea=(float*)malloc(sizeof(float)*(size_t)nv);
        int*bon=(int*)malloc(sizeof(int)*4*(size_t)nv),*idx=(int*)malloc(sizeof(int)*(size_t)(M->idxCount?M->idxCount:1));
        cwMeshRead(B,P,mi,pos,wgt,bon,idx);
        for(int v=0;v<nv;v++){ lim[v]=P->meshTail[mi][v*2]; nea[v]=P->meshTail[mi][v*2+1]; }
        /* what it's made of: the piece's own fabric, or its slot's (club_cloth.gd _fabric), on the defaults theirs has */
        float q[11]={0.2f,2.5f,4.0f,0.5f,0.3f,2e-6f,0.08f,1.f/480.f,0.012f,0.25f,0.03f}; const CwFab*F=NULL;
        for(int i=0;i<B->nFab;i++) if(!strcmp(B->fab[i].piece,piece)) F=&B->fab[i];
        if(!F) for(int i=0;i<B->nFab;i++) if(!strcmp(B->fab[i].piece,slot)) F=&B->fab[i];
        if(F) for(int i=0;i<F->n;i++) q[i]=F->f[i];
        CCParams cp; cc_default_params(&cp); cc_fabric(&cp,q[0],q[1]*0.01f,q[2]);
        cp.drive=q[3]; cp.damp=q[4]; cp.flex_damp=q[5]; cp.hem=q[6]; cp.substep=q[7]; cp.slack=q[8]; cp.friction=q[9]; cp.lag=q[10];
        CCGarmentDesc d; memset(&d,0,sizeof d); d.nv=nv; d.pos=pos; d.wgt=wgt; d.bon=bon; d.maxd=lim; d.near_=nea; d.ni=(int)M->idxCount; d.idx=idx; d.nbones=B->nb; d.parent=B->parent;
        const int k=B->ng; B->g[k]=cc_create(&d,&body,&cp);
        free(pos); free(wgt); free(lim); free(nea); free(bon); free(idx);
        if(!B->g[k]){ printf("[cloth] %s: the solver wouldn't take %s\n",P->label,packStr(P,M->name)); fflush(stdout); continue; }
        B->mesh[k]=(int)mi; snprintf(B->gslot[k],24,"%.23s",slot); B->under[k]=-1; B->ng++;
        /* the limbs under this slot's clothes, each measured off the skin between its two bones' places at rest */
        for(int c=0;c<B->nCap&&B->ncap[k]<CW_CAPS;c++){ const CwCap*C=&B->cap[c]; if(strcmp(C->slot,slot)) continue;
            const int na=cwNode(P,C->a), nb2=cwNode(P,C->b); const int a=na>=0?B->nodeOf[na]:-1, b=nb2>=0?B->nodeOf[nb2]:-1;
            if(a<0||b<0){ printf("[cloth] %s: no capsule %s to %s (a bone no skin names)\n",P->label,C->a,C->b); fflush(stdout); continue; }
            float ra[3],rb[3]; cwRestOrigin(B->bind+a*12,ra); cwRestOrigin(B->bind+b*12,rb);
            const float r=cc_measure_radius(&body,a,ra,rb); const int c2=B->ncap[k]++;
            B->capA[k][c2]=a; B->capB[k][c2]=b; B->capP[k][c2]=C->press; B->capR[k][c2]=r; cc_set_capsule_rest(B->g[k],c2,ra,rb,r); }
        /* its parts keep out of each other: a pair of legs further than the rest (club_cloth.gd LEGS_PART_GAP, PART_GAP) */
        cc_set_parts(B->g[k],part,!strcmp(slot,"legs")?0.03f:0.006f);
        B->vpos[k]=(float*)calloc((size_t)nv*3,sizeof(float)); B->vnrm[k]=(float*)calloc((size_t)nv*3,sizeof(float));
        if(g_cwSay) { printf("[cloth] %s: %s (%s, %s) is whole cloth: %d vertices, %d points, %d capsules, %s %.2f kg/m^2 %.1f cm %.1f N/m\n",P->label,packStr(P,M->name),slot,piece,nv,cc_point_count(B->g[k]),B->ncap[k],F?"fabric":"no fabric of its own,",q[0],q[1],q[2]); fflush(stdout); } }
    free(part);
    /* what's worn over what: a garment of a slot another is said to be over stays outside it (club_cloth.gd LAYER_GAP) */
    for(int k=0;k<B->ng;k++) for(int o=0;o<B->nOver;o++) if(!strcmp(B->over[o][0],B->gslot[k]))
        for(int u=0;u<B->ng;u++) if(u!=k&&!strcmp(B->gslot[u],B->over[o][1])){ B->under[k]=u; cc_set_under(B->g[k],B->g[u],0.008f); }
    B->worn=P->worn; B->pk=P; B->built=1; B->fromOk=B->rotOk=0; B->lastMs=now_ms();
    if(!B->ng){ printf("[cloth] %s: nothing it has on carries the tailor's numbers\n",P->label); fflush(stdout); }
    return B->ng>0; }

static void cwLimit(float*v,float most){ const float l=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if(l>most&&l>0.f){ const float s=most/l; v[0]*=s; v[1]*=s; v[2]*=s; } }
/* a picture: pose, palette, the world's push and air, the turning, and every garment solved. W: the plain pose (before a mirror); model: as drawn */
static void cwStep(CwBody*B,Pack*P,M4*W,const float*model,int mirror){
    const double nowT=now_ms(); float dt=(float)((nowT-B->lastMs)/1000.0); B->lastMs=nowT;
    if(dt>1.f/15.f) dt=1.f/15.f;
    for(int i=0;i<B->nb;i++){ const float*w=W[B->palNode[i]]; float*p=B->pose+i*12; cwRows(w,p); const float*q=B->bind+i*12; float*m=B->pal+i*12;
        for(int r=0;r<3;r++){ const float*pr=p+r*4; m[r*4]=pr[0]*q[0]+pr[1]*q[4]+pr[2]*q[8]; m[r*4+1]=pr[0]*q[1]+pr[1]*q[5]+pr[2]*q[9]; m[r*4+2]=pr[0]*q[2]+pr[1]*q[6]+pr[2]*q[10]; m[r*4+3]=pr[0]*q[3]+pr[1]*q[7]+pr[2]*q[11]+pr[3]; } }
    if(dt<=1e-5f) return;   /* a picture with no tick since: the cloth stays where it is */
    /* its frame through the world: the model's turn (its scale out) and where its origin is, in metres */
    float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); if(sc<1e-6f) sc=1.f;
    float R[9]; for(int c=0;c<3;c++) for(int r=0;r<3;r++) R[r*3+c]=model[c*4+r]/sc;   /* rows: world = R * own */
    const float at[3]={model[12]/CW_UPM,model[13]/CW_UPM,model[14]/CW_UPM};
    /* the air past it and the push on it, as their rider's tick_cloth has them: its speed through the world held to 15 m/s and eased in at 8 a
     * second, and the change in that held to 12 m/s^2 and eased in at 10 */
    float going[3]={0,0,0}; if(B->fromOk) for(int k=0;k<3;k++) going[k]=(at[k]-B->from[k])/dt;
    cwLimit(going,15.f); memcpy(B->from,at,12); B->fromOk=1;
    float wasAir[3]; memcpy(wasAir,B->air,12); const float ea=1.f-expf(-8.f*dt), ec=1.f-expf(-10.f*dt);
    for(int k=0;k<3;k++) B->air[k]+=(going[k]-B->air[k])*ea;
    float dA[3]; for(int k=0;k<3;k++) dA[k]=(B->air[k]-wasAir[k])/dt; cwLimit(dA,12.f);
    for(int k=0;k<3;k++) B->acc[k]+=(dA[k]-B->acc[k])*ec;
    /* into its own space (the transpose, the scale out), and reflected for a mirrored body's plain pose */
    float grav[3],acc[3],air[3]; const float gw[3]={0.f,-9.8f,0.f}, aw[3]={-B->air[0],-B->air[1],-B->air[2]};
    for(int r=0;r<3;r++){ grav[r]=R[0*3+r]*gw[0]+R[1*3+r]*gw[1]+R[2*3+r]*gw[2]; acc[r]=(R[0*3+r]*B->acc[0]+R[1*3+r]*B->acc[1]+R[2*3+r]*B->acc[2])/sc; air[r]=(R[0*3+r]*aw[0]+R[1*3+r]*aw[1]+R[2*3+r]*aw[2])/sc; }
    /* how it's turning, in its own space (club_cloth.gd _follow_turning): the step from the last picture's turn to this one's, as an axis and an
     * angle a second, held to 20 rad/s, eased at 20 a second; a jump of a radian or more is a teleport, not a turn */
    float want[3]={0,0,0};
    if(B->rotOk){ float S3[9]; for(int i=0;i<3;i++) for(int j=0;j<3;j++){ float s=0.f; for(int k=0;k<3;k++) s+=B->rotWas[k*3+i]*R[k*3+j]; S3[i*3+j]=s; }
        const float tr=S3[0]+S3[4]+S3[8]; float ca=(tr-1.f)*0.5f; if(ca>1.f) ca=1.f; if(ca<-1.f) ca=-1.f; const float ang=acosf(ca);
        float ax[3]={S3[7]-S3[5],S3[2]-S3[6],S3[3]-S3[1]}; const float al=sqrtf(ax[0]*ax[0]+ax[1]*ax[1]+ax[2]*ax[2]);
        if(al>1e-8f&&ang<1.f) for(int k=0;k<3;k++) want[k]=ax[k]/al*ang/dt;
        cwLimit(want,20.f); }
    memcpy(B->rotWas,R,sizeof R); B->rotOk=1;
    { float was[3]; memcpy(was,B->turn,12); const float et=1.f-expf(-20.f*dt);
      for(int k=0;k<3;k++) B->turn[k]+=(want[k]-B->turn[k])*et;
      for(int k=0;k<3;k++) B->turnAcc[k]=(B->turn[k]-was[k])/dt;
      cwLimit(B->turnAcc,120.f); }
    float om[3]={B->turn[0],B->turn[1],B->turn[2]}, al3[3]={B->turnAcc[0],B->turnAcc[1],B->turnAcc[2]};
    if(mirror){ grav[0]=-grav[0]; acc[0]=-acc[0]; air[0]=-air[0]; om[1]=-om[1]; om[2]=-om[2]; al3[1]=-al3[1]; al3[2]=-al3[2]; }   /* a turn is turned the other way in a mirror */
    const double t0=g_cwSay?wall_ms():0.0;
    for(int k=0;k<B->ng;k++){ cc_set_turning(B->g[k],om,al3); cc_pose(B->g[k],B->pal); }
    for(int pass=0;pass<2;pass++) for(int k=0;k<B->ng;k++) if((B->under[k]<0)==(pass==0)) cc_layer(B->g[k]);   /* the one under first */
    for(int k=0;k<B->ng;k++){ CCCapsule caps[CW_CAPS];
        for(int c=0;c<B->ncap[k];c++){ const float*pa=B->pose+B->capA[k][c]*12,*pb=B->pose+B->capB[k][c]*12;
            caps[c].a[0]=pa[3]; caps[c].a[1]=pa[7]; caps[c].a[2]=pa[11]; caps[c].b[0]=pb[3]; caps[c].b[1]=pb[7]; caps[c].b[2]=pb[11];
            caps[c].r=B->capR[k][c]; caps[c].bone=B->capA[k][c]; caps[c].press=B->capP[k][c]; }
        cc_solve(B->g[k],dt,grav,acc,air,caps,B->ncap[k]); }
    if(g_cwSay&&(g_frameStamp%60u)==0u){ printf("[cloth] %s: %d garments solved in %.2f ms, air %.1f m/s\n",P->label,B->ng,wall_ms()-t0,sqrtf(B->air[0]*B->air[0]+B->air[1]*B->air[1]+B->air[2]*B->air[2])); fflush(stdout); } }

/* the crowd's draw asks for each mesh: 2 when it's a garment drawn from the solver, and its buffers are g_skVbo (cloth.inc's skirt hands them over
 * the same way); 0 to draw it as it's skinned. Once a picture a body steps first. W: the body's plain pose; model: as it's drawn */
static int cwPrep(int si,Pack*P,unsigned mi,M4*W,const float*model,int mirror){
    if(g_cwSay<0){ const char*e=getenv("REVA_CWSAY"); g_cwSay=(e&&e[0]=='1')?1:0; }
    CwBody*B=NULL; for(int i=0;i<CW_BODIES;i++) if(g_cw[i].on&&g_cw[i].slot==si){ B=&g_cw[i]; break; }
    if(!B||!cwIsGarment(P,mi)) return 0;
    if(B->built&&(B->pk!=P||B->worn!=P->worn)) cwFree(B);   /* another body, or other clothes: built again */
    if(!B->built){ if(B->failed&&B->pk==P) return 0; B->pk=P; if(!cwBuild(B,P)){ B->failed=1; return 0; } }
    int k=-1; for(int j=0;j<B->ng;j++) if(B->mesh[j]==(int)mi){ k=j; break; }
    if(k<0) return 0;
    if(B->stamp!=g_frameStamp){ B->stamp=g_frameStamp; cwStep(B,P,W,model,mirror); }
    if(B->upStamp[k]!=g_frameStamp){ B->upStamp[k]=g_frameStamp;
        const int nv=cc_vertex_count(B->g[k]); const int*vp=cc_vertex_points(B->g[k]); const float*sp=cc_shown(B->g[k]),*sn=cc_normals(B->g[k]);
        float sc=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]); if(sc<1e-6f) sc=1.f;
        for(int v=0;v<nv;v++){ const int p=vp[v]; float x=sp[p*3]*CW_UPM, y=sp[p*3+1]*CW_UPM, z=sp[p*3+2]*CW_UPM, nx=sn[p*3], ny=sn[p*3+1], nz=sn[p*3+2];
            if(mirror){ x=-x; nx=-nx; }
            float*o=B->vpos[k]+v*3,*n=B->vnrm[k]+v*3;
            for(int c=0;c<3;c++){ o[c]=model[c]*x+model[4+c]*y+model[8+c]*z+model[12+c]; n[c]=(model[c]*nx+model[4+c]*ny+model[8+c]*nz)/sc; } }
        if(!B->vbo[k][0]){ gfxBuffersNew(2,B->vbo[k]); for(int c=0;c<2;c++) gfxBufferData(B->vbo[k][c],(long)nv*12,NULL,1); }
        gfxBufferSub(B->vbo[k][0],0,(long)nv*12,B->vpos[k]); gfxBufferSub(B->vbo[k][1],0,(long)nv*12,B->vnrm[k]);
        sunStashW(P,(int)mi,B->vbo[k][0],B->vbo[k][1],(int)P->meshes[mi].boneCount,model); }
    g_skVbo[0]=B->vbo[k][0]; g_skVbo[1]=B->vbo[k][1];
    return 2; }

/* E->bodyCloth(i, path): a game's body i wears whole cloth from now on, made as the file says; NULL or "" takes it off (its clothes are skinned again) */
static int apiBodyCloth(int i,const char*path){
    if(g_cwSay<0){ const char*e=getenv("REVA_CWSAY"); g_cwSay=(e&&e[0]=='1')?1:0; }
    { const char*e=getenv("REVA_NOCWEAR"); if(e&&e[0]=='1') return 0; }   /* the control: the clothes as they're skinned */
    CwBody*B=NULL; for(int k=0;k<CW_BODIES;k++) if(g_cw[k].on&&g_cw[k].slot==i){ B=&g_cw[k]; break; }
    if(!path||!path[0]){ if(B){ cwFree(B); B->on=0; } return 1; }
    if(!B) for(int k=0;k<CW_BODIES;k++) if(!g_cw[k].on){ B=&g_cw[k]; break; }
    if(!B){ printf("[cloth] more than %d bodies in whole cloth\n",CW_BODIES); fflush(stdout); return 0; }
    if(B->on) cwFree(B);
    memset(B,0,sizeof *B); B->on=1; B->slot=i; snprintf(B->path,sizeof B->path,"%s",path);
    if(!cwSpecRead(B,path)){ B->on=0; return 0; }
    return 1; }
