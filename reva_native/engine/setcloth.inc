/* ==================== SET CLOTH: curtains, sheets, comforters =======================
 * jacob: "blowing curtains ... lets see sheets on her bed, a comforter she pushes to the
 * side, and pillows."
 *
 * ⭐ THESE ARE THE EASY ONES, and doing them second was the mistake. A fitted garment is
 * the hardest cloth problem there is -- it has to agree with a body -- and it cost five
 * rounds and a wrong bone map. A curtain agrees with nothing. It is a flat grid pinned
 * along one edge, and it belongs in the LEVEL FILE, because that is where curtains live.
 *
 *     curtain x y z w h yaw [r g b]     top edge pinned, hangs, blows
 *     sheet   x y z w d yaw [r g b]     flat, falls, drapes over what is beneath it
 *     solid   cx cy cz hx hy hz         an invisible box for a sheet to land on
 *     wind    x y z [gust]              direction, and how much it gusts
 *
 * ⭐ NO RPACK AND NO BONES. The grid is generated at load and owns its VBOs; every joint
 * is bone 0 and the palette is the identity, so it rides the existing mesh shader with
 * nothing added to it. A curtain is one line in a .set.
 * ⛔ A SHEET IS NOT PINNED -- it is dropped and caught. Pinning its top row (which is what
 * the garment solver does, correctly, for a shirt) would hold a bedsheet in mid-air. */
#define SCL_MAX    10
#define SCL_W      17
#define SCL_H      17
#define SCL_N      (SCL_W*SCL_H)
#define SCL_MAXBOX 8
typedef struct {
    int used, kind;                    /* 0 = curtain (hangs), 1 = sheet (falls) */
    float cur[SCL_N][3], prv[SCL_N][3], nrm[SCL_N][3];
    unsigned char pin[SCL_N];
    float ru, rv, rd, rb;              /* rest lengths: along u, along v, diagonal, bend */
    float tint[3];
    float phase;                       /* so two curtains do not luff in lockstep */
    GLuint vb, nb, ib, jb, wb, ib32; unsigned nIdx; int gl;
} SetCloth;
static SetCloth g_scl[SCL_MAX]; static int g_nScl=0;
static float g_sclBox[SCL_MAXBOX][6]; static int g_nSclBox=0;
static float g_sclT=0.f;
static int   g_sclOff=0;

static void sclAdd(int kind,float x,float y,float z,float w,float h,float yawDeg,
                   const float*rgb){
    if(g_nScl>=SCL_MAX) return;
    SetCloth*S=&g_scl[g_nScl];
    memset(S->pin,0,sizeof S->pin);
    S->used=1; S->kind=kind; S->gl=0; S->nIdx=0;
    S->tint[0]=rgb[0]; S->tint[1]=rgb[1]; S->tint[2]=rgb[2];
    S->phase=(float)g_nScl*1.7f;
    float a=yawDeg*0.0174532925f, ca=cosf(a), sa=sinf(a);
    float ux[3]={ca,0.f,sa}, uv[3];
    if(kind==0){ uv[0]=0.f; uv[1]=-1.f; uv[2]=0.f; }      /* a curtain hangs DOWN */
    else       { uv[0]=-sa; uv[1]=0.f;  uv[2]=ca;  }      /* a sheet lies FLAT */
    for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
        float fu=(float)c/(float)(SCL_W-1)-0.5f;
        float fv=(kind==0)?((float)r/(float)(SCL_H-1))
                          :((float)r/(float)(SCL_H-1)-0.5f);
        int i=r*SCL_W+c;
        for(int k=0;k<3;k++){
            float p=(k==0?x:(k==1?y:z))+ux[k]*fu*w+uv[k]*fv*h;
            S->cur[i][k]=p; S->prv[i][k]=p; S->nrm[i][k]=(k==1)?1.f:0.f; }
        /* ⭐ ONLY A CURTAIN IS PINNED, and only its top row -- that IS the rail. */
        if(kind==0&&r==0) S->pin[i]=1; }
    S->ru=w/(float)(SCL_W-1);
    S->rv=h/(float)(SCL_H-1);
    S->rd=sqrtf(S->ru*S->ru+S->rv*S->rv);
    S->rb=2.f*S->ru;
    g_nScl++;
    printf("[cloth] %s %d: %gx%g at %.2f,%.2f,%.2f yaw %.0f  rgb %.2f,%.2f,%.2f%c",
           kind?"sheet":"curtain",g_nScl-1,w,h,x,y,z,yawDeg,rgb[0],rgb[1],rgb[2],10);
    fflush(stdout); }

/* ⭐ XPBD (Macklin/Muller/Chentanez 2016, design/papers/). COMPLIANCE, not a magic
 * scale factor. `alphaT` is the constraint's compliance divided by dt^2 and `lam` its
 * accumulated Lagrange multiplier for THIS step:
 *
 *     dLambda = (-C - alphaT*lambda) / (w_i + w_j + alphaT)
 *
 * ⛔ AND THIS IS WHY EVERY CLOTH NUMBER TODAY FELT LIKE WHACK-A-MOLE. In plain PBD,
 * running four constraint passes instead of three makes the fabric STIFFER -- stiffness
 * was never a property of the cloth, it was a side effect of the loop count, so "how stiff
 * is this curtain" had no answer. With compliance it does, in units, and the pass count
 * only buys accuracy.
 * ⭐ At alphaT = 0 this is EXACTLY the old code: the denominator collapses to (w_i+w_j)
 * and dLambda becomes the previous scaling factor. A strict generalisation, so it cannot
 * regress the look. */
static void sclConstrain(SetCloth*S,int i,int j,float rest,float alphaT,float*lam){
    float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[j][k]-S->cur[i][k];
    float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) return;
    float wi=S->pin[i]?0.f:1.f, wj=S->pin[j]?0.f:1.f, wsum=wi+wj;
    if(wsum<1e-6f) return;
    float C=L-rest;
    float dl=(-C-alphaT*(*lam))/(wsum+alphaT);
    *lam+=dl;
    for(int k=0;k<3;k++){ float n=d[k]/L;
        S->cur[i][k]-=wi*n*dl;
        S->cur[j][k]+=wj*n*dl; } }

/* ⭐ SMALL STEPS (Macklin et al. SCA 2019, design/papers/). n substeps of dt/n with ONE
 * solve each beat one step of dt with n solves -- same cost, less constraint error, and
 * markedly less DAMPING, which is what a curtain that refuses to luff was suffering from.
 * REVA_CLOTHITER now names the substep count; it used to name the iteration count, and the
 * paper's whole point is that those are not the same thing. */
static void sclStep(SetCloth*S,float dtFull){
    if(dtFull>0.033f) dtFull=0.033f;
    if(dtFull<1e-4f) dtFull=1e-4f;
    int NSUB=g_clothSub;
    { static int env9=-1; if(env9<0){ const char*e=getenv("REVA_CLOTHITER"); env9=e?atoi(e):0; } if(env9>0) NSUB=env9; }
    if(NSUB<1) NSUB=1;
    if(NSUB>16) NSUB=16;
    for(int ss=0;ss<NSUB;ss++){
    float dt=dtFull/(float)NSUB;
    /* club units are ~1.75x metric, so gravity is too */
    float g=-9.81f*1.75f*dt*dt;
    float gust=1.f+g_windGust*sinf(g_sclT*1.9f+S->phase)*0.6f
                  +g_windGust*sinf(g_sclT*0.7f+S->phase*2.f)*0.4f;
    for(int i=0;i<SCL_N;i++){
        if(S->pin[i]) continue;
        /* ⭐ WIND THROUGH THE NORMAL. A uniform shove slides a curtain sideways like a
         * board; force along (n·w)n means the parts FACING the wind catch it and the
         * parts edge-on do not, which is what luffing is. */
        float wv[3]={g_windV[0]*gust,g_windV[1]*gust,g_windV[2]*gust};
        float nd=S->nrm[i][0]*wv[0]+S->nrm[i][1]*wv[1]+S->nrm[i][2]*wv[2];
        float np[3];
        for(int k=0;k<3;k++){
            float vel=(S->cur[i][k]-S->prv[i][k])*0.985f;
            np[k]=S->cur[i][k]+vel+(k==1?g:0.f)+nd*S->nrm[i][k]*dt*dt*1.2f; }
        for(int k=0;k<3;k++){ S->prv[i][k]=S->cur[i][k]; S->cur[i][k]=np[k]; } }
    /* ⛔ lambda RESETS EVERY SUBSTEP, because a substep IS a time step now. */
    static float lam[SCL_N*5];
    memset(lam,0,sizeof lam);
    /* compliance, in physical units at last. Real cloth barely STRETCHES and bends
     * easily, which is one line here and was three tuning sessions before.
     * ⛔ AND alphaTilde = alpha/dt^2 USES THE SUBSTEP dt. Shrinking dt by n multiplies
     * alphaTilde by n^2, so forgetting this makes every fabric n^2 times softer the moment
     * substepping is turned on -- satisfied constraints, wrong stiffness. */
    float dt2=dt*dt;
    float aStr=2.0e-7f/dt2, aShr=1.2e-5f/dt2, aBnd=3.0e-4f/dt2;
    /* ⭐ THE CONTROL. Compliance zero IS plain PBD -- the paper's own reduction -- so
     * REVA_PBD=1 turns this back into the old solver exactly, and the difference
     * between the two under a changing iteration count is the paper's claim, measured. */
    { static int pbd=-1; if(pbd<0){ pbd=getenv("REVA_PBD")?1:0; }
      if(pbd){ aStr=aShr=aBnd=0.f; } }
    /* ⭐ REVA_CLOTHITER IS THE PAPER'S OWN CLAIM, MADE TESTABLE. XPBD says stiffness is
     * independent of the iteration count; under plain PBD, halving the passes visibly
     * slackens the cloth. Shoot the same curtain at 2 passes and at 6 and diff. */
    /* ⭐ ONE solve per substep. This used to be `for(pass=0;pass<NIT;pass++)`. */
    {
        for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
            int i=r*SCL_W+c;
            if(c+1<SCL_W) sclConstrain(S,i,i+1,S->ru,aStr,&lam[i*5+0]);
            if(r+1<SCL_H) sclConstrain(S,i,i+SCL_W,S->rv,aStr,&lam[i*5+1]);
            if(c+1<SCL_W&&r+1<SCL_H) sclConstrain(S,i,i+SCL_W+1,S->rd,aShr,&lam[i*5+2]);
            if(c>0&&r+1<SCL_H)       sclConstrain(S,i,i+SCL_W-1,S->rd,aShr,&lam[i*5+3]);
            /* bend: keeps a hem from folding flat back on itself */
            if(c+2<SCL_W) sclConstrain(S,i,i+2,S->rb,aBnd,&lam[i*5+4]); }
        for(int i=0;i<SCL_N;i++){
            if(S->pin[i]) continue;
            if(S->cur[i][1]<0.01f) S->cur[i][1]=0.01f;      /* the ground */
            for(int q=0;q<g_nSclBox;q++){                    /* declared solids */
                float*B=g_sclBox[q];
                float dx=S->cur[i][0]-B[0], dy=S->cur[i][1]-B[1], dz=S->cur[i][2]-B[2];
                if(fabsf(dx)>=B[3]||fabsf(dy)>=B[4]||fabsf(dz)>=B[5]) continue;
                /* push out along whichever face is nearest -- almost always the top */
                float px=B[3]-fabsf(dx), py=B[4]-fabsf(dy), pz=B[5]-fabsf(dz);
                if(py<=px&&py<=pz)      S->cur[i][1]=B[1]+(dy<0?-B[4]:B[4]);
                else if(px<=pz)         S->cur[i][0]=B[0]+(dx<0?-B[3]:B[3]);
                else                    S->cur[i][2]=B[2]+(dz<0?-B[5]:B[5]); } } }
    }   /* end substep */
    /* normals from the grid itself, every frame -- the shape changes, so they must */
    for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
        int i=r*SCL_W+c;
        int cl=(c>0)?i-1:i, cr=(c<SCL_W-1)?i+1:i;
        int ru=(r>0)?i-SCL_W:i, rd2=(r<SCL_H-1)?i+SCL_W:i;
        float a[3],b[3];
        for(int k=0;k<3;k++){ a[k]=S->cur[cr][k]-S->cur[cl][k]; b[k]=S->cur[rd2][k]-S->cur[ru][k]; }
        float n[3]={a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
        float L=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(L>1e-7f){
            for(int k=0;k<3;k++) S->nrm[i][k]=n[k]/L;
            /* ⭐ CLOTH HAS NO BACK. A grid has one winding, so half the time the normal
             * points away from the light and the curtain goes BLACK the moment the wind
             * turns it edge-on and past -- which is exactly what the six-frame strip
             * showed. Flip it toward the viewer: two-sided lighting for a dot product
             * per vertex, and no shader change. */
            float vx=g_camPos[0]-S->cur[i][0], vy=g_camPos[1]-S->cur[i][1], vz=g_camPos[2]-S->cur[i][2];
            if(S->nrm[i][0]*vx+S->nrm[i][1]*vy+S->nrm[i][2]*vz < 0.f)
                for(int k=0;k<3;k++) S->nrm[i][k]=-S->nrm[i][k]; } } }

static void sclDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){
    if(!g_nScl||g_sclOff) return;
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static int stamp=-1;
    int step=((int)g_frameStamp!=stamp); stamp=(int)g_frameStamp;
    if(step) g_sclT+=(float)g_chDt;
    for(int q=0;q<g_nScl;q++){
        SetCloth*S=&g_scl[q];
        if(!S->used) continue;
        if(step) sclStep(S,(float)g_chDt);
        if(!S->gl){
            unsigned short idx[(SCL_W-1)*(SCL_H-1)*6]; unsigned n=0;
            for(int r=0;r<SCL_H-1;r++) for(int c=0;c<SCL_W-1;c++){
                unsigned short i=(unsigned short)(r*SCL_W+c);
                idx[n++]=i; idx[n++]=(unsigned short)(i+1); idx[n++]=(unsigned short)(i+SCL_W);
                idx[n++]=(unsigned short)(i+1); idx[n++]=(unsigned short)(i+SCL_W+1); idx[n++]=(unsigned short)(i+SCL_W); }
            S->nIdx=n;
            unsigned char jz[SCL_N*4], wz[SCL_N*4];
            for(int i=0;i<SCL_N;i++){ jz[i*4]=jz[i*4+1]=jz[i*4+2]=jz[i*4+3]=0;
                wz[i*4]=255; wz[i*4+1]=wz[i*4+2]=wz[i*4+3]=0; }
            glGenBuffers(1,&S->vb); glGenBuffers(1,&S->nb); glGenBuffers(1,&S->ib);
            glGenBuffers(1,&S->jb); glGenBuffers(1,&S->wb);
            glBindBuffer(GL_ARRAY_BUFFER,S->vb); glBufferData(GL_ARRAY_BUFFER,sizeof S->cur,S->cur,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,S->nb); glBufferData(GL_ARRAY_BUFFER,sizeof S->nrm,S->nrm,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,S->jb); glBufferData(GL_ARRAY_BUFFER,sizeof jz,jz,GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,S->wb); glBufferData(GL_ARRAY_BUFFER,sizeof wz,wz,GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,S->ib); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(n*2),idx,GL_STATIC_DRAW);
#ifdef REVA_PACKET
            { static unsigned ix9[(SCL_W-1)*(SCL_H-1)*6]; for(unsigned k9=0;k9<n;k9++) ix9[k9]=idx[k9]; glGenBuffers(1,&S->ib32); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,S->ib32); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(n*4),ix9,GL_STATIC_DRAW); }   /* the packet's indices are 32 bits */
#endif
            S->gl=1; }
        glBindBuffer(GL_ARRAY_BUFFER,S->vb); glBufferSubData(GL_ARRAY_BUFFER,0,sizeof S->cur,S->cur);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,S->nb); glBufferSubData(GL_ARRAY_BUFFER,0,sizeof S->nrm,S->nrm);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,S->jb); glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,S->wb); glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,0);
        glDisableVertexAttribArray(4);
        glUniformMatrix4fv(uModel,1,GL_FALSE,I9);
        glUniformMatrix4fv(uPal,1,GL_FALSE,I9);
        glUniformMatrix4fv(uBind,1,GL_FALSE,I9);
        glUniform1f(uUse,0.f); glUniform3f(uTint,S->tint[0],S->tint[1],S->tint[2]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,S->ib);
        glDrawElements(GL_TRIANGLES,(GLsizei)S->nIdx,GL_UNSIGNED_SHORT,0);
#ifdef REVA_PACKET
        if(!g_noDraw){ GfxMesh m9; memset(&m9,0,sizeof m9); m9.id=((unsigned)GFX_KIND_PROP<<28)|((0xf000u+(unsigned)q)<<12); m9.vb[0]=S->vb; m9.vb[1]=S->nb; m9.vb[4]=S->ib32; m9.nIndex=S->nIdx; m9.nVerts=SCL_N; m9.model=I9; memcpy(m9.tint,S->tint,12); m9.flags=g_shadowPass?GFX_SHADOW:0; gfxMesh(&m9); }
#endif
        }
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }

static int setParse(const char*name,int depth);
/* ⭐ ep 11: THE HALF-PIPE ----------------------------------------------------------------------------------
 *     halfpipe <name> x y z yaw W F R V D [r g b]
 * X-Games vert: two quarter pipes facing each other across a flat. (x,y,z) is the centre of the flat bottom and
 * yaw is the pipe's AXIS (the prop convention: forward = (sin Y, cos Y)); riders go ACROSS it. W runs along the
 * axis, F is the flat, R the transition (a quarter circle tangent to the flat and vertical at its top), V the
 * straight vert above it, D the deck behind each coping. Coping height = R + V.
 * The park's principle (parkH, 08-21) kept: ONE definition. pipeAt() is the cross-section by arc length -- the
 * mesh is sampled off it, the ride integrates along it, and the clip audit (pipeDepth) measures against the same
 * flat / arc / vert / deck numbers, so what she rolls on is exactly what is drawn and exactly what is judged. */
#define PIPE_MAX   4
#define PIPE_ARCN  32          /* transition segments per side: 2.8 degrees each */
#define PIPE_COPR  0.12f       /* coping radius, u (~7 cm at 1.75 u/m) */
typedef struct {
    char   name[32];
    float  c[3], yaw, W, F, R, V, D, col[3];
    float  ax[3], cr[3];       /* A: the axis (sin Y,0,cos Y); C: across it (cos Y,0,-sin Y). C x Y = A */
    GLuint vbo[2]; int nv, nSurf, nSide;   /* one buffer: riding surface + decks | sides + ends | coping */
    GLuint vk[6];   /* and each of those three on its own, positions and normals, for the packet */
} HalfPipe;
static HalfPipe g_pipe[PIPE_MAX]; static int g_nPipe=0;
static float pipeHalfLen(const HalfPipe*H){ return 0.5f*H->F+1.5707963f*H->R+H->V; }
/* THE ONE DEFINITION. s = signed arc length along the riding surface from the flat's centre line (+ toward +C).
 * Out: u, the signed offset across the axis; h, the height over the pipe's base; th, the signed slope -- the
 * surface's pitch about A (0 on the flat, +90 deg on the +C vert, -90 on the -C vert). The surface's up there is
 * -sin(th) C + cos(th) Y, and the way s grows along it is cos(th) C + sin(th) Y. |s| past the coping clamps. */
static void pipeAt(const HalfPipe*H,float s,float*u,float*h,float*th){
    float a=fabsf(s), sg=(s<0.f)?-1.f:1.f, hf=0.5f*H->F, arc=1.5707963f*H->R;
    if(a<=hf){ *u=s; *h=0.f; *th=0.f; return; }
    a-=hf;
    if(a<arc){ float t=a/H->R; *u=sg*(hf+H->R*sinf(t)); *h=H->R*(1.f-cosf(t)); *th=sg*t; return; }
    a-=arc; if(a>H->V) a=H->V;
    *u=sg*(hf+H->R); *h=H->R+a; *th=sg*1.5707963f; }
static void pipeW(const HalfPipe*H,float u,float h,float w,float*o){   /* a cross-section point in the world */
    o[0]=H->c[0]+H->cr[0]*u+H->ax[0]*w; o[1]=H->c[1]+h; o[2]=H->c[2]+H->cr[2]*u+H->ax[2]*w; }
static void pipeDir(const HalfPipe*H,float du,float dh,float*o){ o[0]=H->cr[0]*du; o[1]=dh; o[2]=H->cr[2]*du; }
static void pipeQuad(float*P,float*N,int*v,const float*a,const float*b,const float*c,const float*d,
                     const float*na,const float*nb,const float*nc,const float*nd){
    const float*q[6]={a,b,c,a,c,d}, *n[6]={na,nb,nc,na,nc,nd};
    for(int k=0;k<6;k++){ memcpy(P+(size_t)(*v)*3,q[k],12); memcpy(N+(size_t)(*v)*3,n[k],12); (*v)++; } }
/* THE MESH, sampled off pipeAt: the riding surface (smooth normals) and the decks, then the box sides and the
 * closed ends, then the coping -- three ranges of one buffer, so each takes its own tint. */
static void pipeBuild(HalfPipe*H){
    float S=pipeHalfLen(H), hf=0.5f*H->F, arc=1.5707963f*H->R, hw=0.5f*H->W, top=H->R+H->V, out=hf+H->R+H->D;
    float ps[2*PIPE_ARCN+8]; int np=0;
#define PIPE_PS(x) do{ float x9=(x); if(!np||fabsf(x9-ps[np-1])>1e-5f) ps[np++]=x9; }while(0)
    PIPE_PS(-S); PIPE_PS(-(hf+arc));
    if(H->R>1e-4f) for(int i=PIPE_ARCN-1;i>=1;i--) PIPE_PS(-(hf+arc*(float)i/(float)PIPE_ARCN));
    PIPE_PS(-hf); PIPE_PS(hf);
    if(H->R>1e-4f) for(int i=1;i<PIPE_ARCN;i++) PIPE_PS(hf+arc*(float)i/(float)PIPE_ARCN);
    PIPE_PS(hf+arc); PIPE_PS(S);
#undef PIPE_PS
    float pu[2*PIPE_ARCN+8], ph[2*PIPE_ARCN+8], pn[2*PIPE_ARCN+8][3];
    for(int k=0;k<np;k++){ float th; pipeAt(H,ps[k],&pu[k],&ph[k],&th); pipeDir(H,-sinf(th),cosf(th),pn[k]); }
    int cap=((np+4)*3+40)*6;
    float*P=(float*)malloc((size_t)cap*12), *N=(float*)malloc((size_t)cap*12);
    if(!P||!N){ free(P); free(N); printf("[halfpipe] %s: out of memory for its mesh\n",H->name); fflush(stdout); return; }
    int v=0; float a[3],b[3],c[3],d[3], n0[3];
    for(int k=0;k+1<np;k++){                                   /* the riding surface */
        pipeW(H,pu[k],ph[k],-hw,a); pipeW(H,pu[k+1],ph[k+1],-hw,b); pipeW(H,pu[k+1],ph[k+1],hw,c); pipeW(H,pu[k],ph[k],hw,d);
        pipeQuad(P,N,&v,a,b,c,d,pn[k],pn[k+1],pn[k+1],pn[k]); }
    for(int sg=-1;sg<=1;sg+=2){                                /* the decks, at the coping height */
        pipeW(H,sg*(hf+H->R),top,-hw,a); pipeW(H,sg*out,top,-hw,b); pipeW(H,sg*out,top,hw,c); pipeW(H,sg*(hf+H->R),top,hw,d);
        pipeDir(H,0.f,1.f,n0); pipeQuad(P,N,&v,a,b,c,d,n0,n0,n0,n0); }
    H->nSurf=v;
    for(int sg=-1;sg<=1;sg+=2){                                /* the box sides, deck edge to the ground */
        pipeW(H,sg*out,0.f,-hw,a); pipeW(H,sg*out,0.f,hw,b); pipeW(H,sg*out,top,hw,c); pipeW(H,sg*out,top,-hw,d);
        pipeDir(H,(float)sg,0.f,n0); pipeQuad(P,N,&v,a,b,c,d,n0,n0,n0,n0); }
    for(int se=-1;se<=1;se+=2){ float w=se*hw;                 /* the closed ends: a solid ramp from any lens */
        n0[0]=H->ax[0]*se; n0[1]=0.f; n0[2]=H->ax[2]*se;
        for(int k=0;k+1<np;k++){
            if(fabsf(pu[k+1]-pu[k])<1e-6f||(ph[k]<1e-6f&&ph[k+1]<1e-6f)) continue;   /* the vert and the flat have no area here */
            pipeW(H,pu[k],0.f,w,a); pipeW(H,pu[k+1],0.f,w,b); pipeW(H,pu[k+1],ph[k+1],w,c); pipeW(H,pu[k],ph[k],w,d);
            pipeQuad(P,N,&v,a,b,c,d,n0,n0,n0,n0); }
        for(int sg=-1;sg<=1;sg+=2){
            pipeW(H,sg*(hf+H->R),0.f,w,a); pipeW(H,sg*out,0.f,w,b); pipeW(H,sg*out,top,w,c); pipeW(H,sg*(hf+H->R),top,w,d);
            pipeQuad(P,N,&v,a,b,c,d,n0,n0,n0,n0); } }
    H->nSide=v-H->nSurf;
    for(int sg=-1;sg<=1;sg+=2){                                /* the coping: flush with the wall, proud of the deck */
        float uc=sg*(hf+H->R+PIPE_COPR), hc=top-0.3f*PIPE_COPR;
        for(int j=0;j<16;j++){ float a0=6.2831853f*(float)j/16.f, a1=6.2831853f*(float)(j+1)/16.f, m0[3], m1[3];
            pipeW(H,uc+PIPE_COPR*cosf(a0),hc+PIPE_COPR*sinf(a0),-hw,a); pipeW(H,uc+PIPE_COPR*cosf(a1),hc+PIPE_COPR*sinf(a1),-hw,b);
            pipeW(H,uc+PIPE_COPR*cosf(a1),hc+PIPE_COPR*sinf(a1),hw,c);  pipeW(H,uc+PIPE_COPR*cosf(a0),hc+PIPE_COPR*sinf(a0),hw,d);
            pipeDir(H,cosf(a0),sinf(a0),m0); pipeDir(H,cosf(a1),sinf(a1),m1);
            pipeQuad(P,N,&v,a,b,c,d,m0,m1,m1,m0); } }
    H->nv=v;
    glGenBuffers(2,H->vbo);
    glBindBuffer(GL_ARRAY_BUFFER,H->vbo[0]); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)v*12,P,GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER,H->vbo[1]); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)v*12,N,GL_STATIC_DRAW);
#ifdef REVA_PACKET
    { const int part9[4]={0,H->nSurf,H->nSurf+H->nSide,v}; glGenBuffers(6,H->vk); for(int k9=0;k9<3;k9++){ glBindBuffer(GL_ARRAY_BUFFER,H->vk[k9*2]); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(part9[k9+1]-part9[k9])*12,P+part9[k9]*3,GL_STATIC_DRAW); glBindBuffer(GL_ARRAY_BUFFER,H->vk[k9*2+1]); glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(part9[k9+1]-part9[k9])*12,N+part9[k9]*3,GL_STATIC_DRAW); } }
#endif
    free(P); free(N);
    printf("[halfpipe] %s: mesh %d tris (%d profile points: surface+decks %d, sides+ends %d, coping %d)\n",
           H->name,v/3,np,H->nSurf/3,H->nSide/3,(v-H->nSurf-H->nSide)/3); fflush(stdout); }
static void pipeClear(void){
    for(int i=0;i<g_nPipe;i++){ if(g_pipe[i].vbo[0]) glDeleteBuffers(2,g_pipe[i].vbo); if(g_pipe[i].vk[0]) glDeleteBuffers(6,g_pipe[i].vk); }
    memset(g_pipe,0,sizeof g_pipe); g_nPipe=0; }
static void pipeAdd(const char*nm,const float*v9,const float*col,int hasCol){
    if(g_nPipe>=PIPE_MAX){ printf("[halfpipe] %s REFUSED: PIPE_MAX (%d) is full\n",nm,PIPE_MAX); fflush(stdout); return; }
    for(int i=0;i<g_nPipe;i++) if(!strcmp(g_pipe[i].name,nm)){ printf("[halfpipe] %s REFUSED: the name is taken\n",nm); fflush(stdout); return; }
    HalfPipe*H=&g_pipe[g_nPipe];
    memset(H,0,sizeof*H);
    snprintf(H->name,sizeof H->name,"%s",nm);
    H->c[0]=v9[0]; H->c[1]=v9[1]; H->c[2]=v9[2]; H->yaw=v9[3];
    H->W=v9[4]; H->F=v9[5]; H->R=v9[6]; H->V=v9[7]; H->D=v9[8];
    if(!(H->W>0.5f)||!(H->F>=0.f)||!(H->R>=0.f)||!(H->V>=0.f)||!(H->R+H->V>0.2f)){
        printf("[halfpipe] %s REFUSED: W %.2f F %.2f R %.2f V %.2f -- it needs W > 0.5, F R V >= 0 and a wall (R + V > 0.2)\n",
               nm,H->W,H->F,H->R,H->V); fflush(stdout); return; }
    if(!(H->D>=0.3f)){ printf("[halfpipe] %s: deck %.2f is too thin to stand a coping on -- 0.30\n",nm,H->D); H->D=0.3f; }
    float yr=H->yaw*0.0174532925f;
    H->ax[0]=sinf(yr); H->ax[1]=0.f; H->ax[2]=cosf(yr);
    H->cr[0]=cosf(yr); H->cr[1]=0.f; H->cr[2]=-sinf(yr);
    if(hasCol) memcpy(H->col,col,12);
    else { H->col[0]=0.78f; H->col[1]=0.62f; H->col[2]=0.42f; }   /* plywood */
    g_nPipe++;
    float hf=0.5f*H->F, lip=hf+H->R, top=H->R+H->V, a9[3], b9[3];
    pipeW(H,-lip,top,0.f,a9); pipeW(H,lip,top,0.f,b9);
    printf("[halfpipe] %s: W %.1f x %.1f u across (decks in), coping y %.2f (%.2f over the flat) at (%.2f,%.2f) and (%.2f,%.2f), "
           "flat y %.2f x %.1f wide, R %.2f V %.2f D %.2f, axis yaw %.0f, %.1f u of riding surface coping to coping\n",
           nm,H->W,2.f*(lip+H->D),H->c[1]+top,top,a9[0],a9[2],b9[0],b9[2],H->c[1],H->F,H->R,H->V,H->D,H->yaw,2.f*pipeHalfLen(H));
    fflush(stdout); }
/* ⭐ THE CLIP AUDIT'S VIEW OF IT. How far p is INSIDE the pipe's solid -- the ramp under the riding surface, and
 * the deck blocks behind the copings -- to the nearest way out in the cross-section (up to the arc, across to the
 * vert face, up to the deck, out of the back, or out through an end). < 0: not inside. Over the flat the solid is
 * under the base, which is the floor's own business. *kind 0: a FLOOR (the point alone, no flesh -- the grid's
 * floor rule): the ramp for everyone, and the blocks for this pipe's own rider. 1: an OBSTACLE (plus the limb's
 * flesh -- the grid's obstacle rule): the decks and boxes to anyone NOT riding it. */
static float pipeDepth(const HalfPipe*H,const float*p,float rad,int riding,int*kind){
    float dx=p[0]-H->c[0], dz=p[2]-H->c[2];
    float u=fabsf(dx*H->cr[0]+dz*H->cr[2]), w=fabsf(dx*H->ax[0]+dz*H->ax[2]), b=p[1]-H->c[1];
    float hw=0.5f*H->W, hf=0.5f*H->F, R=H->R, top=H->R+H->V, out=hf+H->R+H->D, a=u-hf;
    *kind=0;
    if(w>=hw||u>=out||b<=0.f||b>=top||a<=0.f) return -1.f;
    float dEnd=hw-w, r=sqrtf(a*a+(b-R)*(b-R)), d;
    if(a<R){ if(b>=R||r<=R) return -1.f;          /* above the transition: open air */
             d=r-R; }                               /* under it: out to the arc */
    else { float face=(b>=R)?(a-R):(r-R);          /* the block: to the vert face (or the arc below it), */
           d=top-b; if(out-u<d) d=out-u; if(face<d) d=face;   /* the deck, the back */
           if(!riding) *kind=1; }
    if(dEnd<d) d=dEnd;
    return (*kind)?d+rad:d; }
/* ⭐ ep 10: A ROUTE -- `route <name> <mark> <mark> ...` in a .set NAMES a path through its marks, so a scene
 * line says `hpath=@loopA` instead of 28 mark names. A new verb: `path` is already the actor waypoint. The
 * marks are looked up when a path STARTS, so a route may name marks from an include or from further down.
 * A route whose last mark is its first is CLOSED: its laps join seamlessly, and hloop=1 keeps a body on it. */
#define SET_MAXROUTE 16
typedef struct { char name[32]; char spec[1024]; int n; } SetRoute;
static SetRoute g_setRt[SET_MAXROUTE]; static int g_setNRt=0;
static SetRoute* routeFind(const char*nm){
    for(int i=0;i<g_setNRt;i++) if(!strcmp(g_setRt[i].name,nm)) return &g_setRt[i];
    return NULL; }
/* the rigid bodies' storage lives up here because setLoad spawns and resets them; the solver follows drawSet */
#define RB_MAX 64
typedef struct { int on,pk; float p[3],v[3],q[4],w[3]; float r,rg,rest,fric,sc; float still; int hits; } RigidBody;   /* r: the sphere for walls and each other; rg: the half height the ground sees */
static RigidBody g_rb[RB_MAX]; static int g_nRb=0; static int g_rbDbg=-1;
static int rbSpawn(const char*stem,const float*p,const float*v,float sc);
static int g_setSnd[16], g_nSetSnd=0;   /* a set's ambient voices (09-11): stopped when the set changes */
static int setLoad(const char*name){
    for(int i=0;i<g_nSetSnd;i++) sfxStop(g_setSnd[i]);
    g_nSetSnd=0;
    for(int i=0;i<RB_MAX;i++) g_rb[i].on=0;   /* the loose objects go with the set (09-11) */
    g_nRb=0;
    objClear();   /* what a game made goes with the set, and first: its columns are about to go with spClear */
    g_setNI=0; g_setNA=0; g_setNMk=0; g_nCol=0; g_setCur[0]=0; stClear(); spClear(); setLookClear();
    { static int once=-1; if(once<0){ once=1; g_colOff=getenv("REVA_NOCOLLIDE")?1:0; } }
    g_setT=0.f;              /* Z7a: every load restarts the shot */
    g_setNRt=0;              /* ⭐ ep 10: a set's routes go with it */
    pipeClear();             /* ⭐ ep 11: and its half-pipes */
    g_setNL=0;
    g_setSpace=0; g_setFar=0.f; g_nStar=0; g_milky[3]=0.f; g_nBeam=0; g_nMir=0; g_gloss.on=0; if(g_stk){ free(g_stk); g_stk=NULL; } g_stkFrames=0; g_stkLast=-1; g_nMover=0;
    memset(&g_stb,0,sizeof g_stb);   /* ⭐ 10-22: and Wan-To's star */   /* ⭐ 10-22: and its space */
    /* ⛔ A SET OWNS ITS CLOTH. Leaving the last level's curtains up is the same class of
     * bug as the club's own lamp sprites hanging over the road in a day sheet. */
    for(int q=0;q<g_nScl;q++) g_scl[q].used=0;
    for(int q=0;q<g_nWat;q++) g_wat[q].used=0;
    g_nScl=0; g_nSclBox=0; g_nWat=0;
    g_windV[0]=g_windV[1]=g_windV[2]=0.f; g_windGust=0.f; g_sclT=0.f;
    { static int once9=-1; if(once9<0){ once9=1; g_sclOff=getenv("REVA_NOCLOTH")?1:0; } }
    g_setNL=g_setNL;
    memset(g_setLk,0,sizeof g_setLk);   /* ⭐ ep 12 */
    for(int k=0;k<NLIGHT;k++){ g_setLp[k*3]=0.f; g_setLp[k*3+1]=-99.f; g_setLp[k*3+2]=0.f;
                          g_setLc[k*3]=g_setLc[k*3+1]=g_setLc[k*3+2]=0.f; }
    if(!name||!name[0]||!strcmp(name,"off")){ printf("[set] off\n"); fflush(stdout); return 1; }
    int ok=setParse(name,0);
    if(ok){ snprintf(g_setCur,64,"%s",name);
            printf("[set] collision: %d solid colliders%s\n",g_nCol,
                   g_colOff?" (DISABLED by REVA_NOCOLLIDE)":"");
            navBuild();   /* ⭐ the walkable grid, from the colliders (09-11) */
            { const char*bk9=getenv("REVA_BAKE"); if(bk9&&bk9[0]){ g_spOn=1; spBuild(name); probeBake(name,bk9); } }
            spBuild(name);
#ifdef REVA_VK
            probesLoad(name);
#endif
            clipAuditSetLoaded();   /* ⭐ ep 10: the clipping audit's world grid, when the audit is on */
            printf("[set] %s: %d props + %d actors + %d cloth + %d water from %d packs\n",
                   name,g_setNI,g_setNA,g_nScl,g_nWat,g_setNP);
            /* ⛔ AND THE PRACTICALS ARE SAID OUT LOUD, AGAINST THE SKY THEY WILL BE SEEN UNDER.
             * This is the bug that cost ROBOT NEKO ep 5 a shooting day: other_side.set's lamps
             * were authored at `tod=10.0` and re-used at `tod=21.5`, where they deliver ~30x the
             * sky ambient and the street renders in full daylight however dark the sun is set.
             * NOTHING said a word -- [sun] and [tod] both reported correct values, because they
             * WERE correct. The ratio is the number that was missing, so here it is: brightness
             * at four units (falloff 0.53) against the sky ambient the frame will sit in. Over
             * ~8x and a practical is the light in the frame and the sun is decoration. */
            if(g_setNL>0){
                float amb=(g_sky[0]+g_sky[1]+g_sky[2])/3.f; if(amb<1e-4f) amb=1e-4f;
                printf("[set] %d practical(s), sky ambient %.3f:",g_setNL,amb);
                int hot=0;
                for(int k=0;k<g_setNL;k++){
                    float p=(g_setLc[k*3]+g_setLc[k*3+1]+g_setLc[k*3+2])/3.f;
                    float at4=p/(1.f+16.f*(0.055f+g_setLk[k])), ratio=at4/amb;
                    if(ratio>8.f) hot++;
                    printf("  L%d pow %.1f reach %.1f (%.0fx ambient at 4u)",k,p,lightReachOf(g_setLk[k]),ratio); }
                printf("%c",10);
                if(hot) printf("[set] WARNING: %d practical(s) will SWAMP this sky -- sun=/tod= will look "
                               "like they do nothing. The manual's sane lamp is ~1.5; take them "
                               "down with ctl `lamps=<scale>` rather than forking the set.%c",hot,10); }
            fflush(stdout); }
    return ok; }

static int setParse(const char*name,int depth){
    char pth[512]; snprintf(pth,sizeof pth,"sets/%s.set",name);
    FILE*f=fopen(pth,"rb");
    if(!f){ printf("[set] %s NOT FOUND\n",pth); fflush(stdout); return 0; }
    char ln[2048]; int nline=0, miss=0;   /* ⭐ ep 10: 512 -> 2048 -- a route of 28 marks is ~240 chars */
    while(fgets(ln,sizeof ln,f)){
        /* ⛔ ep 10: a line longer than the buffer used to come back as TWO lines, its tail parsed as a verb */
        if(!strchr(ln,'\n')&&!feof(f)){ int c9; while((c9=fgetc(f))!=EOF&&c9!='\n'){ }
            printf("[set] %s: a line longer than %d chars was CUT -- its tail is dropped\n",pth,(int)sizeof ln-1); fflush(stdout); }
        if(ln[0]=='#'||ln[0]=='\n'||ln[0]=='\r') continue;
        /* A STREET IS A FILE, A CAST IS ANOTHER. The film shoots many scenes on the same
         * 514 lines of suburbia; copying them per shot is how street.py would stop being
         * the source of truth. Depth-limited: a street plus a cast, and no cycles. */
        if(!strncmp(ln,"include ",8)){
            char inc[64]; if(sscanf(ln,"include %63s",inc)==1){
                if(depth>=2) printf("[set] include %s IGNORED (too deep)\n",inc);
                else setParse(inc,depth+1); }
            continue; }
        /* ⭐ ep 10: route <name> <mark> <mark> ... (commas allowed) -- a scene's hpath=@name. Said out loud. */
        if(!strncmp(ln,"route ",6)){
            char rn[32]; int used=0;
            if(sscanf(ln,"route %31s %n",rn,&used)>=1&&used>0){
                SetRoute*R=routeFind(rn);
                if(!R&&g_setNRt<SET_MAXROUTE) R=&g_setRt[g_setNRt++];
                if(!R){ printf("[set] route %s REFUSED: SET_MAXROUTE (%d) is full\n",rn,SET_MAXROUTE); fflush(stdout); }
                else { snprintf(R->name,sizeof R->name,"%s",rn); R->spec[0]=0; R->n=0;
                    const char*p=ln+used; size_t sl=0; char first[32]={0}, last[32]={0};
                    while(*p){
                        while(*p==' '||*p=='\t'||*p==',') p++;
                        if(!*p||*p=='\n'||*p=='\r'||*p=='#') break;
                        char mk[32]; int k=0;
                        while(*p&&*p!=' '&&*p!='\t'&&*p!=','&&*p!='\n'&&*p!='\r'){ if(k<31) mk[k++]=*p; p++; }
                        mk[k]=0;
                        size_t ml=strlen(mk);
                        if(sl+ml+2>=sizeof R->spec){ printf("[set] route %s: longer than %d chars -- '%s' and after DROPPED\n",rn,(int)sizeof R->spec-1,mk); break; }
                        if(sl) R->spec[sl++]=',';
                        memcpy(R->spec+sl,mk,ml); sl+=ml; R->spec[sl]=0; R->n++;
                        if(R->n==1) snprintf(first,sizeof first,"%s",mk);
                        snprintf(last,sizeof last,"%s",mk); }
                    printf("[set] route %s: %d marks%s\n",rn,R->n,(R->n>=3&&!strcmp(first,last))?" -- a CLOSED loop":"");
                    fflush(stdout); } }
            continue; }
        char stem[64]; float x,y,z,yaw,sc,r=0,g=0,b=0;
        /* Z7a: a cue and a waypoint attach to the actor declared above them. */
        /* ⛔ AN ACTOR INDEX IS NOT STABLE ACROSS AN `include`. `leash 0` in a set that
         * includes the street points at the street's FIRST SKATER, not at the actor the
         * author meant -- block.set contributes six before this file declares any. A bare
         * `leash` therefore means THE ACTOR DECLARED ABOVE ME, which is what a lead
         * between a pair actually is; an explicit index still works. */
        /* ⭐ `look x y z` (or `look camera`) on the line after an actor. Same shape as
         * `path` and `cue`: it belongs to the actor above it. */
        if(!strncmp(ln,"look ",5)&&g_setNA>0){
            SetActor*A=&g_setA[g_setNA-1];
            if(strstr(ln,"camera")){ A->hasLook=2; }
            else if(sscanf(ln,"look %f %f %f",&A->look[0],&A->look[1],&A->look[2])==3) A->hasLook=1;
            continue; }
        /* ⭐ CLOTH IN THE LEVEL FILE. A curtain is one line. */
        if(!strncmp(ln,"curtain ",8)||!strncmp(ln,"sheet ",6)){
            int kind=(ln[0]=='s');
            float cw,ch; float col[3]={0.82f,0.80f,0.74f};
            int nf=sscanf(ln+(kind?6:8),"%f %f %f %f %f %f %f %f %f",
                          &x,&y,&z,&cw,&ch,&yaw,&col[0],&col[1],&col[2]);
            if(nf>=6) sclAdd(kind,x,y,z,cw,ch,yaw,col);
            continue; }
        /* ⭐ ep 11: halfpipe <name> x y z yaw W F R V D [r g b] -- a vert ramp you can ride (hpipe=) */
        if(!strncmp(ln,"halfpipe ",9)){
            char pn[32]; float v9[9], col9[3]={0.f,0.f,0.f};
            int nf=sscanf(ln,"halfpipe %31s %f %f %f %f %f %f %f %f %f %f %f %f",pn,&v9[0],&v9[1],&v9[2],&v9[3],
                          &v9[4],&v9[5],&v9[6],&v9[7],&v9[8],&col9[0],&col9[1],&col9[2]);
            if(nf>=10) pipeAdd(pn,v9,col9,nf>=13);
            else { printf("[halfpipe] %s: need `halfpipe <name> x y z yaw W F R V D [r g b]` -- IGNORED: %s",pth,ln); fflush(stdout); }
            continue; }
        if(!strncmp(ln,"water ",6)){
            float ww,wd; float col[3]={0.16f,0.40f,0.52f};
            int nf=sscanf(ln,"water %f %f %f %f %f %f %f %f",
                          &x,&y,&z,&ww,&wd,&col[0],&col[1],&col[2]);
            if(nf>=5) watAdd(x,y,z,ww,wd,col);
            continue; }
        if(!strncmp(ln,"solid ",6)&&g_nSclBox<SCL_MAXBOX){
            float*B=g_sclBox[g_nSclBox];
            if(sscanf(ln,"solid %f %f %f %f %f %f",&B[0],&B[1],&B[2],&B[3],&B[4],&B[5])==6){
                printf("[cloth] solid box at %.2f,%.2f,%.2f half %.2f,%.2f,%.2f%c",
                       B[0],B[1],B[2],B[3],B[4],B[5],10);
                g_nSclBox++; }
            continue; }
        if(!strncmp(ln,"wind ",5)){
            float gz=0.f;
            if(sscanf(ln,"wind %f %f %f %f",&g_windV[0],&g_windV[1],&g_windV[2],&gz)>=3){
                g_windGust=gz;
                printf("[cloth] wind %.2f,%.2f,%.2f gust %.2f%c",
                       g_windV[0],g_windV[1],g_windV[2],g_windGust,10); }
            continue; }
        if(!strncmp(ln,"climb",5)&&(ln[5]==0||ln[5]==' '||ln[5]=='\n'||ln[5]=='\r')){ g_spOn=1; continue; }   /* its surfaces are walked on: engine/spans.inc */
        if(!strncmp(ln,"trigger ",8)){ spTrigAdd(ln); continue; }
        if(!strncmp(ln,"cell ",5)){ stCellLine(ln); continue; }   /* it comes and goes with the hero: engine/stream.inc */
        if(!strncmp(ln,"streaming ",10)){ stStreamingLine(ln); continue; }
        if(!strncmp(ln,"fardetail ",10)){ stFarLine(ln); continue; }
        if(!strncmp(ln,"mark ",5)&&g_setNMk>=SET_MAXMARK){
            static int full9=0;
            if(!full9++){ printf("[set] ⛔ MARK TABLE FULL (SET_MAXMARK %d): this mark and every later one are DROPPED -- %s",SET_MAXMARK,ln); fflush(stdout); }
            continue; }
        if(!strncmp(ln,"mark ",5)&&g_setNMk<SET_MAXMARK){
            SetMark*M=&g_setMk[g_setNMk];
            char ct[16]={0};
            int nm=sscanf(ln,"mark %31s %f %f %f %f %15s",M->name,&M->x,&M->y,&M->z,&M->yaw,ct);
            if(nm>=5){
                M->hips=(nm==6&&!strcmp(ct,"hips"))?1:0;
                if(nm==6&&!M->hips&&ct[0]!='#') printf("[set] mark %s: contact '%s' unknown (feet|hips) -- feet\n",M->name,ct);
                printf("[set] mark %s at %.2f,%.2f,%.2f yaw %.0f contact %s%c",M->name,M->x,M->y,M->z,M->yaw,M->hips?"hips":"feet",10);
                g_setNMk++; }
            continue; }
        if(!strncmp(ln,"leash",5)&&g_setNA>0){ int li=-1;
            if(sscanf(ln,"leash %d",&li)!=1||li<0) li=g_setNA-2;
            g_setA[g_setNA-1].leash=li; continue; }
        /* ⭐ over <clip> [w] -- the LAST actor gets an upper-body layer (09-11) */
        if(!strncmp(ln,"over ",5)&&g_setNA>0){ SetActor*A=&g_setA[g_setNA-1]; float w=1.f;
            if(sscanf(ln,"over %31s %f",A->over,&w)>=1){ A->overW=w; printf("[set] actor %d layer '%s' w %.2f\n",g_setNA-1,A->over,w); }
            continue; }
        /* ⭐ mind wander|react|hunt <radius> -- the LAST actor gets a mind (09-11) */
        if(!strncmp(ln,"mind ",5)&&g_setNA>0){ char mk[16]; float mr=8.f; SetActor*A=&g_setA[g_setNA-1];
            if(sscanf(ln,"mind %15s %f",mk,&mr)>=1){
                A->mind=!strcmp(mk,"wander")?MIND_WANDER:!strcmp(mk,"react")?MIND_REACT:!strcmp(mk,"hunt")?MIND_HUNT:MIND_NONE;
                if(A->mind==MIND_HUNT) A->zombie=1;
                A->mindR=mr; A->tmr=mindRnd()*2.f;
                if(A->mind==MIND_NONE) printf("[set] mind '%s' unknown (wander|react|hunt)\n",mk); }
            continue; }
        if(!strncmp(ln,"cue ",4)){
            float ct; char cc[32];
            if(sscanf(ln,"cue %f %31s",&ct,cc)==2&&g_setNA>0){
                SetActor*A=&g_setA[g_setNA-1];
                if(A->nCue<SET_MAXCUE){ A->cue[A->nCue].t=ct;
                    snprintf(A->cue[A->nCue].clip,32,"%s",cc); A->nCue++; }
                else printf("[set] cue cap is %d per actor\n",SET_MAXCUE); }
            continue; }
        if(!strncmp(ln,"path ",5)){
            float pt,px2,py2,pz2;
            if(sscanf(ln,"path %f %f %f %f",&pt,&px2,&py2,&pz2)==4&&g_setNA>0){
                SetActor*A=&g_setA[g_setNA-1];
                if(A->nWp<SET_MAXWP){ A->wp[A->nWp].t=pt; A->wp[A->nWp].x=px2;
                    A->wp[A->nWp].y=py2; A->wp[A->nWp].z=pz2; A->nWp++; A->onPath=1; }
                else printf("[set] path cap is %d per actor\n",SET_MAXWP); }
            continue; }
        /* ⭐ rigid <stem> x y z [scale] [vx vy vz] -- a LOOSE object (09-11): it falls, bounces, rests */
        if(!strncmp(ln,"rigid ",6)){ char st[64]; float p3[3],v3[3]={0,0,0},sc=1.f;
            int n=sscanf(ln,"rigid %63s %f %f %f %f %f %f %f",st,&p3[0],&p3[1],&p3[2],&sc,&v3[0],&v3[1],&v3[2]);
            if(n>=4){ if(n<5) sc=1.f; rbSpawn(st,p3,v3,sc); }
            continue; }
        /* ⭐ sound <name> x y z [gain] [loop]  -- an AMBIENCE in the world (09-11): a sprinkler
         * on a lawn, a car alarm down the street. Spatial, looping by default. */
        if(!strncmp(ln,"sound ",6)){ char nm[32]; float sx,sy,sz,sg=1.f; int lp=1;
            int n=sscanf(ln,"sound %31s %f %f %f %f %d",nm,&sx,&sy,&sz,&sg,&lp);
            if(n>=4){ float p3[3]={sx,sy,sz}; int id=sfxPlay(nm,sg,p3,lp);
                if(id>=0&&g_nSetSnd<16) g_setSnd[g_nSetSnd++]=id;
                printf("[set] sound %s at %.1f,%.1f,%.1f gain %.2f%s -> %s\n",nm,sx,sy,sz,sg,lp?" loop":"",id>=0?"playing":"NOT PLAYED"); }
            continue; }
        /* ⭐ 10-22: SPACE, its stars, its reach, its own light (see spaceSkyDraw) */
        if(!strncmp(ln,"space",5)&&(ln[5]==0||ln[5]==' '||ln[5]=='\n'||ln[5]=='\r')){
            g_setSpace=1; printf("[set] space: a black sky, the stars at infinity\n"); continue; }
        if(!strncmp(ln,"far ",4)){ float fr9=0.f;
            if(sscanf(ln,"far %f",&fr9)==1&&fr9>0.f){ g_setFar=fr9; printf("[set] far plane %.0f u\n",fr9); } continue; }
        if(!strncmp(ln,"milkyway ",9)){ float a9,b9,c9,s9=1.f; int n9=sscanf(ln,"milkyway %f %f %f %f",&a9,&b9,&c9,&s9);
            float l9=sqrtf(a9*a9+b9*b9+c9*c9);
            if(n9>=3&&l9>1e-6f){ g_milky[0]=a9/l9; g_milky[1]=b9/l9; g_milky[2]=c9/l9; g_milky[3]=(n9>=4)?s9:1.f; }
            continue; }
        if(!strncmp(ln,"star ",5)){ float dx,dy,dz,r9,g9,b9,sz9,hl9=1.f;
            int n9=sscanf(ln,"star %f %f %f %f %f %f %f %f",&dx,&dy,&dz,&r9,&g9,&b9,&sz9,&hl9);
            float l9=sqrtf(dx*dx+dy*dy+dz*dz);
            if(n9>=7&&l9>1e-6f&&g_nStar<8){
                g_starDir[g_nStar][0]=dx/l9; g_starDir[g_nStar][1]=dy/l9; g_starDir[g_nStar][2]=dz/l9;
                g_starDir[g_nStar][3]=sz9*0.0174533f;
                g_starCol[g_nStar][0]=r9; g_starCol[g_nStar][1]=g9; g_starCol[g_nStar][2]=b9; g_starCol[g_nStar][3]=hl9;
                g_starFl[g_nStar][0]=g_starFl[g_nStar][1]=g_starFl[g_nStar][2]=0.f;
                { const char*fq=strstr(ln," flare "); float fd=0.f,fr=1.f,fp=0.f;   /* (10-22) */
                  if(fq&&sscanf(fq+7,"%f %f %f",&fd,&fr,&fp)==3&&fp>0.f){ g_starFl[g_nStar][0]=fd; g_starFl[g_nStar][1]=fr; g_starFl[g_nStar][2]=fp;
                      printf("[set] star %d flares at +%.1fs over %.1fs, x%.1f\n",g_nStar,fd,fr,fp); } }
                g_nStar++; }
            else if(n9>=7&&g_nStar>=8) printf("[set] star cap is 8 -- ignoring\n");
            continue; }
        /* ⭐ 10-22: WAN-TO'S STAR (see starDraw) */
        if(!strncmp(ln,"starbody ",9)){ float v9[8]={0.f,0.f,0.f,0.f,6500.f,0.f,180.f,1.f};
            int n9=sscanf(ln,"starbody %f %f %f %f %f %f %f %f",v9,v9+1,v9+2,v9+3,v9+4,v9+5,v9+6,v9+7);
            if(n9>=4&&v9[3]>0.f){ g_stb.on=1; memcpy(g_stb.c,v9,12); g_stb.R=v9[3]; g_stb.T=v9[4];
                g_stb.st[0]=1.f; g_stb.st[1]=1.f; g_stb.st[2]=0.f;
                { const char*sq=strstr(ln," stretch "); float k9=1.f,r9=1.f,d9=0.f;   /* ⭐ (10-22) the particle rack */
                  if(sq&&sscanf(sq+9,"%f %f %f",&k9,&r9,&d9)>=2&&k9>0.f){ g_stb.st[0]=k9; g_stb.st[1]=r9; g_stb.st[2]=d9;
                      printf("[set] starbody stretched x%.2f over %.1fs from +%.1fs\n",k9,r9,d9); } }
                g_stb.cutW=v9[5]*0.0174533f; g_stb.cutA0=(v9[6]-0.5f*v9[5])*0.0174533f; g_stb.seed=v9[7]; g_stb.dirty=1;
                printf("[set] starbody at %.0f,%.0f,%.0f radius %.0f u, %.0f K%s\n",v9[0],v9[1],v9[2],v9[3],v9[4],v9[5]>0.f?", cut open":""); }
            else printf("[set] starbody: wants x y z radius [tempK] [cutdeg] [cutaz] [seed] -- IGNORED\n");
            continue; }
        if(!strncmp(ln,"starspot ",9)){ float v9[3]; if(sscanf(ln,"starspot %f %f %f",v9,v9+1,v9+2)==3&&g_stb.nSpot<STB_SPOTS){
                memcpy(g_stb.spot[g_stb.nSpot++],v9,12); } else printf("[set] starspot: wants lat lon size_deg, at most %d -- IGNORED\n",STB_SPOTS);
            continue; }
        if(!strncmp(ln,"starloop ",9)){ float v9[6]={0,0,0,0,0.1f,0.f};
            int n9=sscanf(ln,"starloop %f %f %f %f %f %f",v9,v9+1,v9+2,v9+3,v9+4,v9+5);
            if(n9>=5&&g_stb.nLoop<STB_LOOPS){ memcpy(g_stb.loop[g_stb.nLoop++],v9,24); g_stb.dirty=1; }
            else printf("[set] starloop: wants lat1 lon1 lat2 lon2 height [thick], at most %d -- IGNORED\n",STB_LOOPS);
            continue; }
        if(!strncmp(ln,"starweb ",8)){ float v9[3]={0.f,1.f,1.f}; int n9=sscanf(ln,"starweb %f %f %f",v9,v9+1,v9+2);
            if(n9>=1){ g_stb.webN=(int)v9[0]; if(g_stb.webN>STB_WEB) g_stb.webN=STB_WEB; g_stb.webSeed=v9[1]; g_stb.webX=(v9[2]!=0.f); g_stb.dirty=1;
                printf("[set] starweb: %d pieces of him%s\n",g_stb.webN,g_stb.webX?", seen through the star":""); }
            continue; }
        if(!strncmp(ln,"gloss ",6)){ float v9[4]={0.f,0.06f,0.35f,2.5f};   /* ⭐ 10-23: a glossy floor -- see glossFloorPass */
            int n9=sscanf(ln,"gloss %f %f %f %f",v9,v9+1,v9+2,v9+3);
            if(n9>=1){ const char*e9=getenv("REVA_NOGLOSS");
                if(e9&&e9[0]&&e9[0]!='0') printf("[set] gloss: REVA_NOGLOSS -- the floor stays matte\n");
                else { g_gloss.on=1; g_gloss.y=v9[0]; g_gloss.f0=v9[1]; g_gloss.fmax=v9[2]; g_gloss.fade=(v9[3]>0.05f)?v9[3]:2.5f;
                    Mirror*M9=&g_glossM; memset(M9,0,sizeof*M9); M9->p[1]=v9[0]; M9->n[1]=1.f;
                    float*R9=M9->R; R9[0]=1.f; R9[5]=-1.f; R9[10]=1.f; R9[13]=2.f*v9[0]; R9[15]=1.f;   /* y' = 2y0 - y */
                    printf("[set] gloss: the floor at y %.2f reflects (F0 %.2f, at most %.2f, fading over %.1f u)\n",v9[0],v9[1],v9[2],g_gloss.fade); } }
            else printf("[set] gloss: want `gloss y [F0] [max] [fade]` -- %s\n",ln);
            continue; }
        if(!strncmp(ln,"mirror ",7)){ float v9[6];   /* ⭐ 10-22: MIRRORS -- see mirInst */
            if(sscanf(ln,"mirror %f %f %f %f %f %f",v9,v9+1,v9+2,v9+3,v9+4,v9+5)==6&&g_nMir<4){
                Mirror*M9=&g_mir[g_nMir++]; float a9=v9[3]*3.14159265f/180.f;
                M9->p[0]=v9[0]; M9->p[1]=v9[1]; M9->p[2]=v9[2];
                M9->n[0]=sinf(a9); M9->n[1]=0.f; M9->n[2]=cosf(a9); M9->w=v9[4]; M9->h=v9[5];
                /* the reflection through the plane n.x = n.p: x' = x - 2 (n.x - d) n, column-major */
                float d9=M9->n[0]*M9->p[0]+M9->n[2]*M9->p[2]; float*R9=M9->R; memset(R9,0,sizeof M9->R);
                for(int i9=0;i9<3;i9++) for(int j9=0;j9<3;j9++) R9[j9*4+i9]=(i9==j9?1.f:0.f)-2.f*M9->n[i9]*M9->n[j9];
                for(int i9=0;i9<3;i9++) R9[12+i9]=2.f*d9*M9->n[i9];
                R9[15]=1.f;
                printf("[set] mirror %d at %.2f %.2f %.2f facing %.0f deg, %.2f x %.2f\n",g_nMir-1,v9[0],v9[1],v9[2],v9[3],v9[4],v9[5]); }
            else printf("[set] mirror: want `mirror x y z yaw w h` (and 4 at most) -- %s\n",ln);
            continue; }
        if(!strncmp(ln,"track ",6)){ char p9[256]; float o9=0.f;   /* ⭐ 11-03: the set's timeline (settrack.inc) */
            if(sscanf(ln,"track %255s %f",p9,&o9)>=1) trackLoad(p9,o9);
            continue; }
        if(!strncmp(ln,"mover ",6)){ char st9[64]; float c9[3]={1.f,1.f,1.f}; int n9=sscanf(ln,"mover %63s %f %f %f",st9,c9,c9+1,c9+2);
            int pi9=(n9>=1)?setPack(st9):-1;
            if(pi9>=0&&g_setNI<SET_MAXI){ SetInst*I9=&g_setI[g_setNI++]; memset(I9,0,sizeof *I9); I9->pk=pi9; I9->mover=g_nMover++; I9->hid=1; I9->m[15]=1.f;
                I9->tinted=(n9>=4); memcpy(I9->tint,c9,12); }
            else printf("[set] mover: no such prop, or the set is full -- %s\n",ln);
            continue; }
        if(!strncmp(ln,"beam ",5)){ float v9[15]; v9[13]=1.f; v9[14]=2.f;   /* ⭐ 10-22: a glowing volume (see beamDraw) */
            int n9=sscanf(ln,"beam %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",v9,v9+1,v9+2,v9+3,v9+4,v9+5,v9+6,v9+7,
                          v9+8,v9+9,v9+10,v9+11,v9+12,v9+13,v9+14);
            float l9=(n9>=6)?sqrtf(v9[3]*v9[3]+v9[4]*v9[4]+v9[5]*v9[5]):0.f;
            if(n9>=13&&l9>1e-6f&&v9[6]>0.f&&v9[7]>0.f&&v9[8]>0.f&&g_nBeam<BEAM_MAX){ int b9=g_nBeam++;
                for(int k=0;k<3;k++){ g_beamA[b9][k]=v9[k]; g_beamU[b9][k]=v9[3+k]/l9; g_beamC[b9][k]=v9[9+k]; }
                g_beamL[b9]=v9[6]; g_beamR[b9][0]=v9[7]; g_beamR[b9][1]=v9[8]; g_beamK[b9]=v9[12]; g_beamF[b9]=v9[13]; g_beamT[b9]=v9[14];
                printf("[set] beam %d from %.1f,%.1f,%.1f along %.2f,%.2f,%.2f for %.0f u, radius %.1f -> %.1f, k %.3f\n",
                       b9,v9[0],v9[1],v9[2],g_beamU[b9][0],g_beamU[b9][1],g_beamU[b9][2],v9[6],v9[7],v9[8],v9[12]); }
            else printf("[set] beam: wants x y z dx dy dz length r0 r1 r g b k [fade [thin]], at most %d -- IGNORED\n",BEAM_MAX);
            continue; }
        if(!strncmp(ln,"sun ",4)){ float el9=45.f,az9=30.f,it9=1.f,r9=1.f,g9=0.97f,b9=0.90f;
            int n9=sscanf(ln,"sun %f %f %f %f %f %f",&el9,&az9,&it9,&r9,&g9,&b9);
            if(n9>=3){ float e9=el9*0.0174532925f, a9=az9*0.0174532925f;
                g_sun[0]=cosf(e9)*sinf(a9); g_sun[1]=sinf(e9); g_sun[2]=cosf(e9)*cosf(a9); g_sun[3]=it9;
                if(n9>=6){ g_sunC[0]=r9; g_sunC[1]=g9; g_sunC[2]=b9; }
                printf("[set] sun elev %.1f azim %.1f I %.2f\n",el9,az9,it9); }
            continue; }
        if(!strncmp(ln,"sky ",4)){ sscanf(ln,"sky %f %f %f",&g_sky[0],&g_sky[1],&g_sky[2]); continue; }
        if(!strncmp(ln,"exposure ",9)){ sscanf(ln,"exposure %f",&g_exposure); continue; }
        if(!strncmp(ln,"haze ",5)){ sscanf(ln,"haze %f",&g_haze); continue; }
        if(!strncmp(ln,"ground ",7)){ sscanf(ln,"ground %f %f %f",&g_ground[0],&g_ground[1],&g_ground[2]); continue; }
        if(!strncmp(ln,"spot ",5)){ float x9,y9,z9,dx9,dy9,dz9,r9=1.f,g9=1.f,b9=1.f,pw9=4.f,ag9=30.f; int sh9=0; if(sscanf(ln,"spot %f %f %f %f %f %f %f %f %f %f %f %d",&x9,&y9,&z9,&dx9,&dy9,&dz9,&r9,&g9,&b9,&pw9,&ag9,&sh9)>=6&&g_setNSpot<32){ lightFill(&g_setSpot[g_setNSpot],x9,y9,z9,r9*pw9,g9*pw9,b9*pw9,0.055f,dx9,dy9,dz9,ag9); g_setSpot[g_setNSpot++].shadow=sh9; } continue; }
#ifdef REVA_VK
        if(!strncmp(ln,"grade ",6)){ char g9[64]; if(sscanf(ln,"grade %63s",g9)==1) gfxGradeLoad(g9); continue; }
#endif
        if(!strncmp(ln,"light ",6)){
            float lx,ly,lz,lr,lg,lb,lpw=1.f,lrc=0.f;
            int nl=sscanf(ln,"light %f %f %f %f %f %f %f %f",&lx,&ly,&lz,&lr,&lg,&lb,&lpw,&lrc);
            /* ⛔ THE TOP SLOT IS RESERVED FOR A HELD LIGHT (ep 4), so a set takes NLIGHT-1. */
            if(nl>=6&&g_setNL<NLIGHT-1){
                g_setLp[g_setNL*3]=lx; g_setLp[g_setNL*3+1]=ly; g_setLp[g_setNL*3+2]=lz;
                g_setLc[g_setNL*3]=lr*lpw; g_setLc[g_setNL*3+1]=lg*lpw; g_setLc[g_setNL*3+2]=lb*lpw;
                g_setLk[g_setNL]=(nl>=8)?lightReachK(lrc):0.f;   /* ⭐ ep 12: the lamp's reach */
                g_setNL++; }
            /* ⛔ THIS SAID "cap is 3 (the shader's lp[3])" for as long as NLIGHT has been 6. */
            else if(nl>=6) printf("[set] light cap is %d of the shader's %d practicals (the top slot is reserved for a held light) -- ignoring\n",NLIGHT-1,NLIGHT);
            continue; }
        if(!strncmp(ln,"actor ",6)){
            char ap[64],ac[64],ab[64]; float ax,ay,az,ayw,asc,art=1.f,asp=0.f,aln=0.f;
            ab[0]=0;
            /* ⭐ (10-23) `outfit=<name>` anywhere on the line is what this actor WEARS -- an extra wore her pack's first
             * outfit (Billy in his freezer shift beside Freddy in day clothes, 2.22). Taken out of the line before the
             * positional read, where an 11th token is the board's name. */
            char lb9[2048], of9[32]; of9[0]=0; snprintf(lb9,sizeof lb9,"%s",ln);
            { char*q9=strstr(lb9," outfit="); if(q9){ char*v9=q9+8; int k9=0;
                  while(v9[k9]&&v9[k9]!=' '&&v9[k9]!='\t'&&v9[k9]!='\n'&&v9[k9]!='\r'&&k9<31){ of9[k9]=v9[k9]; k9++; }
                  of9[k9]=0; for(char*w9=q9;w9<v9+k9;w9++) *w9=' '; } }
            int na=sscanf(lb9,"actor %63s %63s %f %f %f %f %f %f %f %f %63s",
                          ap,ac,&ax,&ay,&az,&ayw,&asc,&art,&asp,&aln,ab);
            if(na<7||g_setNA>=SET_MAXA) continue;
            int pi2=setPack(ap); if(pi2<0) continue;
            Pack*AP2=&g_setPk[pi2];
            SetActor*A=&g_setA[g_setNA++];
            A->nCue=0; A->curCue=-1; A->nWp=0; A->onPath=0; A->cspd=0.f; A->leash=-1;
            memset(&A->wms,0,sizeof A->wms);
            A->hasLook=0;
            A->mind=MIND_NONE; A->mindOn=0; A->zombie=!strcmp(ac,"zombie"); A->mindR=0.f; A->tmr=0.f; A->fleeT=0.f; A->replanT=0.f;
            A->navN=0; A->navI=0; A->threat=-1; A->moving=-1; snprintf(A->clip0,sizeof A->clip0,"%s",ac);
            A->over[0]=0; A->overW=1.f;
            A->home[0]=ax; A->home[1]=az;
            actorClip(A,AP2,ac,g_setNA-1,asp>0.f);
            A->pk=pi2; A->board=(na>=11&&ab[0])?setPack(ab):-1;
            /* her outfit: the pack's own (its first) unless the line names one */
            A->worn=AP2->worn;
            if(of9[0]&&AP2->meshOff){ int o9=wardOutfitFind(AP2,of9);
                if(o9<0){ printf("[set] actor %d (%s): no outfit '%s' --",g_setNA-1,ap,of9);
                          for(int i9=0;i9<AP2->nOutfit;i9++) printf(" %s",AP2->outfitName[i9]);
                          printf("\n"); }
                else { unsigned long long w9=0;
                       for(int i9=0;i9<AP2->nPiece;i9++) if(AP2->pieceOutfits[i9]&(1u<<o9)) w9|=1ull<<i9;
                       A->worn=w9; printf("[set] actor %d (%s) wears '%s'\n",g_setNA-1,ap,of9); }
                fflush(stdout); }
            A->x=ax; A->y=ay; A->z=az; A->yaw=ayw; A->sc=asc;
            A->rate=(na>=8)?art:1.f; A->spd=(na>=9)?asp:0.f; A->len=(na>=10)?aln:0.f;
            /* clip "skate" is not a clip: it is the procedural rider */
            A->skate=!strcmp(ac,"skate"); if(A->skate) A->clip=-1;
            A->act=NULL; A->actI=A->actN=0; A->actT=0.f;
            A->trickIn=1.0f+(float)(g_setNA%7)*0.6f;
            A->deckRoll=A->deckYaw=0.f; A->air=0.f;
            A->footY[0]=A->footY[1]=A->footSpread=0.f;
            /* stagger the phases, or a row of skaters pumps in lockstep like a chorus */
            A->phase=(double)(g_setNA*13%97); A->travel=(float)(g_setNA*7%23)*(aln>0.f?aln/23.f:0.f);
            /* ⭐ a z_rm_ clip starts at its first frame (10-18): it is a move with a beginning, and two bodies posed
             * TOGETHER (the boys' huddle: an arm over the other's back) must play in step */
            if(!strncmp(ac,"z_rm_",5)) A->phase=0.0;
            continue; }
        /* ⭐ screen x y z yaw pitch w h -- a live Holly panel in the world (ep 8) */
        if(!strncmp(ln,"screen ",7)){
            float s9[7]={0,0,0,0,0,1.f,0.75f};
            int ns9=sscanf(ln,"screen %f %f %f %f %f %f %f",
                           &s9[0],&s9[1],&s9[2],&s9[3],&s9[4],&s9[5],&s9[6]);
            if(ns9>=3){
                if(g_scrN>=SCR_MAX){ printf("[screen] REFUSED: %d screens is the cap (SCR_MAX)\n",SCR_MAX); }
                else { for(int k9=0;k9<7;k9++) g_scr[g_scrN][k9]=s9[k9];
                    g_scr[g_scrN][3]*=0.0174533f; g_scr[g_scrN][4]*=0.0174533f;
                    printf("[screen] %d at (%.2f %.2f %.2f) yaw %.0f pitch %.0f  %.2f x %.2f\n",
                           g_scrN,s9[0],s9[1],s9[2],s9[3],s9[4],s9[5],s9[6]);
                    g_scrN++; } }
            continue; }
        /* propr: the same prop with pitch and roll as well, for anything that leans,
         * lies flat or has been nailed across a window. */
        float pit=0.f, rol=0.f; int isr=0;
        int n;
        /* ⭐ room <stem> x y z yaw pitch roll scale (ROBOT NEKO ep 9) -- a propr that is a PLACE.
         * propSolid() decides by NAME and a photogrammetry scan is named for where it is: the
         * Southbank Undercroft came back SOLID, and its circle collider -- sized off the pack's
         * footprint -- would have been ~31 units across the middle of the park, ejecting every
         * body standing in it. A room draws exactly like propr and never becomes a circle. */
        int isRoom9=!strncmp(ln,"room ",5);
        if(!strncmp(ln,"propr ",6)||isRoom9){
            n=sscanf(ln+(isRoom9?5:6),"%63s %f %f %f %f %f %f %f %f %f %f",
                     stem,&x,&y,&z,&yaw,&pit,&rol,&sc,&r,&g,&b);
            if(n<8) continue;
            isr=1; n=(n>=11)?9:6;          /* re-map onto prop's field count for the tint test */
        } else {
            n=sscanf(ln,"prop %63s %f %f %f %f %f %f %f %f",stem,&x,&y,&z,&yaw,&sc,&r,&g,&b);
            if(n<6) continue; }
        (void)isr;
        nline++;
        int pi=setPack(stem); if(pi<0){ miss++; continue; }
        if(g_setNI>=SET_MAXI){ printf("[set] instance cap %d hit\n",SET_MAXI); break; }
        SetInst*I=&g_setI[g_setNI++];
        I->pk=pi; I->mover=-1; I->hid=0;
        /* The pack's OWN scale rides along -- a town prop is baked at club scale and
         * reports 1.0, but a character reports the factor that takes its rig units to
         * the world, and -mny*scale is what stands it on the floor instead of burying
         * or floating it. */
        Pack*Q=&g_setPk[pi];
        float eff=sc*((Q->scale>1e-6f)?Q->scale:1.f);
        if(pit!=0.f||rol!=0.f){
            float qy[4],qx[4],qz[4],q1[4],q2[4];
            qaxis(qy,0,1,0,yaw*0.0174532925f);
            qaxis(qx,1,0,0,pit*0.0174532925f);
            qaxis(qz,0,0,1,rol*0.0174532925f);
            qmul(q1,qy,qx); qmul(q2,q1,qz);
            float t3[3]={x,y-Q->mny*eff,z}, s3[3]={eff,eff,eff};
            m4_trs(I->m,t3,q2,s3); }
        else {
        float a=yaw*0.0174532925f, ca=cosf(a), sa=sinf(a);
        memset(I->m,0,64);
        I->m[0]=ca*eff; I->m[2]=-sa*eff; I->m[5]=eff; I->m[8]=sa*eff; I->m[10]=ca*eff; I->m[15]=1.f;
        I->m[12]=x; I->m[13]=y-Q->mny*eff; I->m[14]=z; }
        I->tinted=(n>=9); I->tint[0]=r; I->tint[1]=g; I->tint[2]=b;
        I->room=isRoom9;
        if(isRoom9){ const char*e9=getenv("REVA_ROOMLIT"); if(e9) g_roomLit=(float)atof(e9); }
        /* ⭐ AND A COLLIDER, IF IT IS SOLID -- the pack's measured footprint times this
         * instance's own scale. Both were already known and neither is authored.
         * ⛔ AFTER THE MATRIX, NOT BEFORE IT. The first version read I->m the line after
         * the instance was allocated and before anything was written into it, so every
         * radius was built from an uninitialised scale, failed the minimum, and a street
         * full of vans came back with ZERO colliders and no error. */
        /* \u2b50 WALLS AND WINDOWS ARE SOLID -- as a ROW along the module's long axis.
         * propSolid() leaves them OPEN on purpose (an interior's shell must not eject the
         * woman standing in it), which is also why the first street A/B walked straight
         * through the town. A circle has no side, so a thin row along the wall pushes an
         * indoor body back into the room and an outdoor one off the house alike.
         * Spacing 1.0 at r 0.6: a mover of r 0.42 cannot fit between them. */
        if(isRoom9){
            float sc9=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]);
            printf("[set] room %s: a place, not an obstacle -- no circle collider (it would have been r %.1f)\n",
                   stem,Q->propR*sc9*0.78f); fflush(stdout); }
        else if(Q->open){}   /* ⭐ the Open marker (10-18): never a collider */
        else if((strstr(stem,"wall")||strstr(stem,"window"))&&!strstr(stem,"attic")){
            float sc9=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]);
            int alongX=(Q->propEx>=Q->propEz);
            float ax9=alongX?I->m[0]:I->m[8], az9=alongX?I->m[2]:I->m[10];   /* the long local axis, in world xz */
            float al9=sqrtf(ax9*ax9+az9*az9); if(al9>1e-6f){ ax9/=al9; az9/=al9; }
            float half9=(alongX?Q->propEx:Q->propEz)*sc9;
            int n9=(int)(2.f*half9/1.0f)+1; if(n9<2) n9=2; if(n9>16) n9=16;
            for(int k9=0;k9<n9;k9++){ float t9=-half9+2.f*half9*(float)k9/(float)(n9-1);
                colAdd(I->m[12]+ax9*t9,I->m[14]+az9*t9,0.6f*sc9); } }
        else if(propSolid(stem)){
            float sc9=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]);
            colAdd(I->m[12],I->m[14],Q->propR*sc9*0.78f); } }
    fclose(f);
    printf("[set]   %-16s %d lines, %d missing\n",name,nline,miss);
    fflush(stdout);
    return 1; }


