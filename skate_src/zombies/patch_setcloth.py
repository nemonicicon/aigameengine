"""patch_setcloth.py -- CLOTH IN THE SET: curtains, sheets, comforters.

jacob: "i would like for our cloth physics to be very well done.. blowing curtains, torn
clothing exposing wounds underneath dainty dresses showing every curve... lets see sheets
on her bed, a comforter she pushes to the side, and pillows."

⭐ AND THESE ARE THE EASY ONES. A fitted garment is the HARDEST cloth problem -- it has to
match a body, which is what cost five rounds and a wrong bone map. A curtain and a bedsheet
match nothing. They are flat grids that hang or fall, and they belong in the LEVEL FILE
rather than bolted to a character, because that is where they live:

    curtain <x> <y> <z> <w> <h> <yaw> [r g b]    top edge pinned, hangs down, blows
    sheet   <x> <y> <z> <w> <d> <yaw> [r g b]    flat, falls, drapes over what is under it
    solid   <cx> <cy> <cz> <hx> <hy> <hz>        an invisible box for a sheet to land on
    wind    <x> <y> <z> [gust]                   a direction and how much it gusts

⭐ NO RPACK, NO AUTHORING TOOL, NO BONES. The grid is generated at load and owns its own
VBOs; joints are all bone 0 and the palette is the identity, so it rides the existing mesh
shader untouched. A curtain is one line in a .set file.

⭐ WIND IS APPLIED THROUGH THE NORMAL -- force along (n·w)n rather than a uniform shove.
That is what makes a curtain BILLOW instead of sliding sideways as a rigid sheet: the parts
facing the wind catch it and the parts edge-on do not, so it luffs.

⛔ SPHERES AND BOXES, NOT RAY CASTS. Same honest limit as the garment solver: a sheet will
land on a declared box and on the ground, and it will not discover a chair it was not told
about.
⛔ A SHEET IS NOT PINNED. It is dropped and caught; pinning its top row would hold it in
mid-air. The curtain is the pinned one.
"""
import io

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
LF = chr(10)
CRLF = chr(13) + LF

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        raise SystemExit("!! anchor x%d for %s" % (n, why))
    s = s.replace(a, b)
    print("   ok: %s" % why)


BS = chr(92)
SET_REPORT_OLD = ('            printf("[set] %s: %d props + %d actors from %d packs' + BS + 'n",'
                  + LF + "                   name,g_setNI,g_setNA,g_setNP); fflush(stdout); }")
SET_REPORT_NEW = ('            printf("[set] %s: %d props + %d actors + %d cloth from %d packs' + BS + 'n",'
                  + LF + "                   name,g_setNI,g_setNA,g_nScl,g_setNP); fflush(stdout); }")

BLOCK = r'''
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
    GLuint vb, nb, ib, jb, wb; unsigned nIdx; int gl;
} SetCloth;
static SetCloth g_scl[SCL_MAX]; static int g_nScl=0;
static float g_sclBox[SCL_MAXBOX][6]; static int g_nSclBox=0;
static float g_windV[3]={0.f,0.f,0.f}, g_windGust=0.f, g_sclT=0.f;
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

static void sclConstrain(SetCloth*S,int i,int j,float rest){
    float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[j][k]-S->cur[i][k];
    float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) return;
    float f=(L-rest)/L*0.5f;
    float wi=S->pin[i]?0.f:1.f, wj=S->pin[j]?0.f:1.f, t=wi+wj;
    if(t<1e-6f) return;
    for(int k=0;k<3;k++){ S->cur[i][k]+=d[k]*f*(wi/t)*2.f*0.5f;
                          S->cur[j][k]-=d[k]*f*(wj/t)*2.f*0.5f; } }

static void sclStep(SetCloth*S,float dt){
    if(dt>0.033f) dt=0.033f; if(dt<1e-4f) dt=1e-4f;
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
    for(int pass=0;pass<4;pass++){
        for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
            int i=r*SCL_W+c;
            if(c+1<SCL_W) sclConstrain(S,i,i+1,S->ru);
            if(r+1<SCL_H) sclConstrain(S,i,i+SCL_W,S->rv);
            if(c+1<SCL_W&&r+1<SCL_H) sclConstrain(S,i,i+SCL_W+1,S->rd);
            if(c>0&&r+1<SCL_H)       sclConstrain(S,i,i+SCL_W-1,S->rd);
            /* bend: keeps a hem from folding flat back on itself */
            if(c+2<SCL_W) sclConstrain(S,i,i+2,S->rb); }
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
    /* normals from the grid itself, every frame -- the shape changes, so they must */
    for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
        int i=r*SCL_W+c;
        int cl=(c>0)?i-1:i, cr=(c<SCL_W-1)?i+1:i;
        int ru=(r>0)?i-SCL_W:i, rd2=(r<SCL_H-1)?i+SCL_W:i;
        float a[3],b[3];
        for(int k=0;k<3;k++){ a[k]=S->cur[cr][k]-S->cur[cl][k]; b[k]=S->cur[rd2][k]-S->cur[ru][k]; }
        float n[3]={a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]};
        float L=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(L>1e-7f){ for(int k=0;k<3;k++) S->nrm[i][k]=n[k]/L; } } }

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
        glDrawElements(GL_TRIANGLES,(GLsizei)S->nIdx,GL_UNSIGNED_SHORT,0); }
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }

'''

sub("static int setParse(const char*name,int depth){", BLOCK + "static int setParse(const char*name,int depth){",
    "the set-cloth system")

# --- reset on load ------------------------------------------------------------------
sub("""    g_setT=0.f;              /* Z7a: every load restarts the shot */
    g_setNL=0;""",
    """    g_setT=0.f;              /* Z7a: every load restarts the shot */
    g_setNL=0;
    /* ⛔ A SET OWNS ITS CLOTH. Leaving the last level's curtains up is the same class of
     * bug as the club's own lamp sprites hanging over the road in a day sheet. */
    for(int q=0;q<g_nScl;q++) g_scl[q].used=0;
    g_nScl=0; g_nSclBox=0;
    g_windV[0]=g_windV[1]=g_windV[2]=0.f; g_windGust=0.f; g_sclT=0.f;
    { static int once9=-1; if(once9<0){ once9=1; g_sclOff=getenv("REVA_NOCLOTH")?1:0; } }
    g_setNL=g_setNL;""",
    "reset cloth per set")

# --- parse ---------------------------------------------------------------------------
sub("""        if(!strncmp(ln,"mark ",5)&&g_setNMk<SET_MAXMARK){""",
    """        /* ⭐ CLOTH IN THE LEVEL FILE. A curtain is one line. */
        if(!strncmp(ln,"curtain ",8)||!strncmp(ln,"sheet ",6)){
            int kind=(ln[0]=='s');
            float cw,ch; float col[3]={0.82f,0.80f,0.74f};
            int nf=sscanf(ln+(kind?6:8),"%f %f %f %f %f %f %f %f %f",
                          &x,&y,&z,&cw,&ch,&yaw,&col[0],&col[1],&col[2]);
            if(nf>=6) sclAdd(kind,x,y,z,cw,ch,yaw,col);
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
        if(!strncmp(ln,"mark ",5)&&g_setNMk<SET_MAXMARK){""",
    "parse curtain / sheet / solid / wind")

# --- draw ----------------------------------------------------------------------------
sub("""        drawProp(&g_setPk[I->pk],I->m,uModel,uPal,uBind); }
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }""",
    """        drawProp(&g_setPk[I->pk],I->m,uModel,uPal,uBind); }
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
    sclDraw(uModel,uPal,uBind,uUse,uTint); }""",
    "draw the set cloth")

# --- report -------------------------------------------------------------------------
sub(SET_REPORT_OLD, SET_REPORT_NEW, "report the cloth count")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
