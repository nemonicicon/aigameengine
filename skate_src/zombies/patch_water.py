"""patch_water.py -- WATER, actually simulated.

jacob: "there is a swimming pool scene but i never see a swimming pool, i think we need
water physics too" and "absolutley we need real water physics, these things are why we are
making our engine, by reducing uneeded overhead we gain access to previously too heavy
systems."

He is right about the trade, and this is the system that trade buys. A SWIMMING POOL does
not want particles -- SPH or FLIP would be slower, harder and would look WORSE at this
scale, because a pool is a surface, not a volume you ever see inside of.

⭐ WHAT A POOL WANTS IS A HEIGHT FIELD, and that is a genuine simulation, not a sine wave:
every cell carries a height and a vertical velocity, and each step solves the 2D wave
equation against its neighbours.

    a  = (mean of my four neighbours - me) * stiffness
    v += a*dt ;  v *= damping ;  h += v*dt

Ripples PROPAGATE outward at a real speed, they REFLECT off the pool walls, they INTERFERE
with each other, and they die away. Nothing is keyframed and nothing loops. Drop something
in one corner and the far corner finds out about it a moment later, which is the whole
difference between water and a wobbling texture.

⭐ AND IT KNOWS WHEN SOMEBODY IS IN IT. waterDisturb() pushes the surface down under a
world position; every body drawn inside a pool's footprint calls it, so wading, falling in
and swimming all make their own wake for free -- the sim does not care what moved.

    water <x> <y> <z> <w> <d> [r g b]

⛔ THE SURFACE NEEDED AN ALPHA, and the mesh shader had none: it ends `gl_FragColor =
vec4(lit, 1.0)`, hardcoded. Opaque water is a sheet of blue plastic. So both mesh fragment
shaders take a `uAlpha` uniform defaulting to 1.0 -- byte-identical for everything that
does not set it, and glass and ghosts get it for free later.

⛔ IT IS A HEIGHT FIELD, so it cannot splash ABOVE itself, break into droplets, or pour.
Written down so the limit is known rather than discovered.
"""
import io

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
LF = chr(10)
CRLF = chr(13) + LF

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


LF = chr(10)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        raise SystemExit("!! anchor x%d for %s" % (n, why))
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- 1. the shaders learn an alpha -------------------------------------------------
# There are THREE mesh fragment shaders. The first belongs to a separate program that is
# never handed a uAlpha, and an unset float uniform in GL is ZERO -- declaring it there
# would have turned everything that program draws invisible. Only fsO and fsC, which are
# the two the Prog loops compile, and BOTH loops (PR linear-blend, PRD dual-quaternion)
# must look it up and default it to 1.


def nth(hay, needle, k):
    """index of the k-th (1-based) occurrence"""
    i = -1
    for _ in range(k):
        i = hay.index(needle, i + 1)
    return i


def replace_nth(hay, needle, new, ks):
    for k in sorted(ks, reverse=True):
        i = nth(hay, needle, k)
        hay = hay[:i] + new + hay[i + len(needle):]
    return hay


ALPHA_DECL = chr(32)*6 + chr(34) + "uniform float uAlpha;" + chr(92) + "n" + chr(34)

FC = '      "  gl_FragColor=vec4(lit,1.0); }' + chr(92) + 'n";'
if s.count(FC) != 3:
    raise SystemExit("!! expected 3 mesh fragment shaders, found %d" % s.count(FC))
s = replace_nth(s, FC, '      "  gl_FragColor=vec4(lit,uAlpha); }' + chr(92) + 'n";', [2, 3])
print("   ok: fsO and fsC output uAlpha (the third shader untouched)")

DECL = '      "uniform vec3 tint; uniform float useTex; uniform sampler2D smp;' + chr(92) + 'n"'
if s.count(DECL) != 3:
    raise SystemExit("!! expected 3 tint/useTex declarations, found %d" % s.count(DECL))
s = replace_nth(s, DECL, DECL + LF + ALPHA_DECL, [2, 3])
print("   ok: uAlpha declared in fsO and fsC")

# ---- 2. the water system, next to the set cloth --------------------------------------
WATER = r'''
/* ======================== WATER =====================================================
 * jacob: "i never see a swimming pool, i think we need water physics too" and "absolutley
 * we need real water physics, these things are why we are making our engine, by reducing
 * uneeded overhead we gain access to previously too heavy systems."
 *
 * ⭐ A POOL DOES NOT WANT PARTICLES. SPH or FLIP would be slower, far harder, and would
 * look WORSE at this scale, because a pool is a SURFACE -- you never see inside the volume.
 * What a surface wants is a HEIGHT FIELD, and that is a real simulation and not a sine
 * wave: every cell carries a height and a vertical velocity and each step solves the 2D
 * wave equation against its neighbours.
 *
 *     a  = (mean of my four neighbours - me) * stiffness
 *     v += a*dt ;   v *= damping ;   h += v*dt
 *
 * Ripples PROPAGATE at a real speed, REFLECT off the walls, INTERFERE, and die away.
 * Nothing is keyframed and nothing loops: drop something in one corner and the far corner
 * finds out a moment later. That is the whole difference between water and a wobbling
 * texture.
 *
 * ⛔ AND IT IS A HEIGHT FIELD, so it cannot splash above itself, break into droplets or
 * pour. Written down so the limit is known rather than discovered. */
#define WAT_MAX 4
#define WAT_G   48
#define WAT_N   (WAT_G*WAT_G)
typedef struct {
    int used;
    float x, z, w, d, y;               /* centre, footprint, rest height */
    float h[WAT_N], v[WAT_N];
    float pos[WAT_N][3], nrm[WAT_N][3];
    float tint[3];
    GLuint vb, nb, ib, jb, wb; unsigned nIdx; int gl;
} Water;
static Water g_wat[WAT_MAX]; static int g_nWat=0;

static void watAdd(float x,float y,float z,float w,float d,const float*rgb){
    if(g_nWat>=WAT_MAX) return;
    Water*W=&g_wat[g_nWat];
    memset(W,0,sizeof *W);
    W->used=1; W->x=x; W->y=y; W->z=z; W->w=w; W->d=d;
    for(int k=0;k<3;k++) W->tint[k]=rgb[k];
    g_nWat++;
    printf("[water] pool %d: %gx%g at %.2f,%.2f,%.2f  %dx%d cells%c",
           g_nWat-1,w,d,x,y,z,WAT_G,WAT_G,10);
    fflush(stdout); }

/* ⭐ SOMETHING IS IN THE WATER. Any body drawn inside a pool's footprint pushes the
 * surface down; the sim does not care what moved, so wading, falling in and swimming all
 * make their own wake with no special case for any of them. */
static void waterDisturb(float wx,float wy,float wz,float rad,float amt){
    for(int q=0;q<g_nWat;q++){
        Water*W=&g_wat[q];
        if(!W->used) continue;
        if(wy>W->y+2.0f||wy<W->y-3.0f) continue;              /* not at the surface */
        float hw=W->w*0.5f, hd=W->d*0.5f;
        if(wx<W->x-hw-rad||wx>W->x+hw+rad) continue;
        if(wz<W->z-hd-rad||wz>W->z+hd+rad) continue;
        for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
            float px=W->x-hw+W->w*(float)c/(float)(WAT_G-1);
            float pz=W->z-hd+W->d*(float)r/(float)(WAT_G-1);
            float dx=px-wx, dz=pz-wz, dd=sqrtf(dx*dx+dz*dz);
            if(dd>=rad) continue;
            float f=1.f-dd/rad;
            W->v[r*WAT_G+c]-=amt*f*f; } } }

static void watStep(Water*W,float dt){
    if(dt>0.033f) dt=0.033f; if(dt<1e-4f) dt=1e-4f;
    const float STIFF=42.f, DAMP=0.985f;
    static float acc[WAT_N];
    for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
        int i=r*WAT_G+c;
        /* ⛔ CLAMPED NEIGHBOURS ARE A WALL, and that is exactly right for a pool: a wave
         * that runs out of grid reflects instead of vanishing, which is what the tiled
         * side of a pool does to it. */
        int l=(c>0)?i-1:i, rr=(c<WAT_G-1)?i+1:i;
        int u=(r>0)?i-WAT_G:i, dn=(r<WAT_G-1)?i+WAT_G:i;
        float avg=(W->h[l]+W->h[rr]+W->h[u]+W->h[dn])*0.25f;
        acc[i]=(avg-W->h[i])*STIFF; }
    for(int i=0;i<WAT_N;i++){
        W->v[i]=(W->v[i]+acc[i]*dt)*DAMP;
        W->h[i]+=W->v[i]*dt;
        if(W->h[i]> 0.60f){ W->h[i]= 0.60f; W->v[i]*=-0.4f; }
        if(W->h[i]<-0.60f){ W->h[i]=-0.60f; W->v[i]*=-0.4f; } }
    float hw=W->w*0.5f, hd=W->d*0.5f;
    for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
        int i=r*WAT_G+c;
        W->pos[i][0]=W->x-hw+W->w*(float)c/(float)(WAT_G-1);
        W->pos[i][1]=W->y+W->h[i];
        W->pos[i][2]=W->z-hd+W->d*(float)r/(float)(WAT_G-1); }
    float sx=W->w/(float)(WAT_G-1), sz=W->d/(float)(WAT_G-1);
    for(int r=0;r<WAT_G;r++) for(int c=0;c<WAT_G;c++){
        int i=r*WAT_G+c;
        int l=(c>0)?i-1:i, rr=(c<WAT_G-1)?i+1:i;
        int u=(r>0)?i-WAT_G:i, dn=(r<WAT_G-1)?i+WAT_G:i;
        /* the gradient IS the normal -- this is what makes light travel across a ripple */
        float gx=(W->h[rr]-W->h[l])/(2.f*sx), gz=(W->h[dn]-W->h[u])/(2.f*sz);
        float n[3]={-gx,1.f,-gz};
        float L=sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        for(int k=0;k<3;k++) W->nrm[i][k]=n[k]/L; } }

static void watDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,GLint uAlpha){
    if(!g_nWat) return;
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static int stamp=-1;
    int step=((int)g_frameStamp!=stamp); stamp=(int)g_frameStamp;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for(int q=0;q<g_nWat;q++){
        Water*W=&g_wat[q];
        if(!W->used) continue;
        if(step) watStep(W,(float)g_chDt);
        if(!W->gl){
            static unsigned short idx[(WAT_G-1)*(WAT_G-1)*6]; unsigned n=0;
            for(int r=0;r<WAT_G-1;r++) for(int c=0;c<WAT_G-1;c++){
                unsigned short i=(unsigned short)(r*WAT_G+c);
                idx[n++]=i; idx[n++]=(unsigned short)(i+1); idx[n++]=(unsigned short)(i+WAT_G);
                idx[n++]=(unsigned short)(i+1); idx[n++]=(unsigned short)(i+WAT_G+1); idx[n++]=(unsigned short)(i+WAT_G); }
            W->nIdx=n;
            static unsigned char jz[WAT_N*4], wz[WAT_N*4];
            for(int i=0;i<WAT_N;i++){ jz[i*4]=jz[i*4+1]=jz[i*4+2]=jz[i*4+3]=0;
                wz[i*4]=255; wz[i*4+1]=wz[i*4+2]=wz[i*4+3]=0; }
            glGenBuffers(1,&W->vb); glGenBuffers(1,&W->nb); glGenBuffers(1,&W->ib);
            glGenBuffers(1,&W->jb); glGenBuffers(1,&W->wb);
            glBindBuffer(GL_ARRAY_BUFFER,W->vb); glBufferData(GL_ARRAY_BUFFER,sizeof W->pos,W->pos,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,W->nb); glBufferData(GL_ARRAY_BUFFER,sizeof W->nrm,W->nrm,GL_DYNAMIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,W->jb); glBufferData(GL_ARRAY_BUFFER,sizeof jz,jz,GL_STATIC_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER,W->wb); glBufferData(GL_ARRAY_BUFFER,sizeof wz,wz,GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib); glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(n*2),idx,GL_STATIC_DRAW);
            W->gl=1; }
        glBindBuffer(GL_ARRAY_BUFFER,W->vb); glBufferSubData(GL_ARRAY_BUFFER,0,sizeof W->pos,W->pos);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,W->nb); glBufferSubData(GL_ARRAY_BUFFER,0,sizeof W->nrm,W->nrm);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,W->jb); glEnableVertexAttribArray(2); glVertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_FALSE,0,0);
        glBindBuffer(GL_ARRAY_BUFFER,W->wb); glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_UNSIGNED_BYTE,GL_TRUE,0,0);
        glDisableVertexAttribArray(4);
        glUniformMatrix4fv(uModel,1,GL_FALSE,I9);
        glUniformMatrix4fv(uPal,1,GL_FALSE,I9);
        glUniformMatrix4fv(uBind,1,GL_FALSE,I9);
        glUniform1f(uUse,0.f); glUniform3f(uTint,W->tint[0],W->tint[1],W->tint[2]);
        if(uAlpha>=0) glUniform1f(uAlpha,0.72f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib);
        glDrawElements(GL_TRIANGLES,(GLsizei)W->nIdx,GL_UNSIGNED_SHORT,0); }
    if(uAlpha>=0) glUniform1f(uAlpha,1.f);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }

'''
sub("/* ==================== SET CLOTH: curtains, sheets, comforters ===",
    WATER + "/* ==================== SET CLOTH: curtains, sheets, comforters ===",
    "the water system")

# ---- 3. parse, reset, draw ----------------------------------------------------------
sub("""    for(int q=0;q<g_nScl;q++) g_scl[q].used=0;
    g_nScl=0; g_nSclBox=0;""",
    """    for(int q=0;q<g_nScl;q++) g_scl[q].used=0;
    for(int q=0;q<g_nWat;q++) g_wat[q].used=0;
    g_nScl=0; g_nSclBox=0; g_nWat=0;""",
    "reset water per set")

sub("""        if(!strncmp(ln,"solid ",6)&&g_nSclBox<SCL_MAXBOX){""",
    """        if(!strncmp(ln,"water ",6)){
            float ww,wd; float col[3]={0.16f,0.40f,0.52f};
            int nf=sscanf(ln,"water %f %f %f %f %f %f %f %f",
                          &x,&y,&z,&ww,&wd,&col[0],&col[1],&col[2]);
            if(nf>=5) watAdd(x,y,z,ww,wd,col);
            continue; }
        if(!strncmp(ln,"solid ",6)&&g_nSclBox<SCL_MAXBOX){""",
    "parse water")

sub("""    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
    sclDraw(uModel,uPal,uBind,uUse,uTint); }""",
    """    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
    sclDraw(uModel,uPal,uBind,uUse,uTint);
    /* water LAST: it is transparent, so everything it should be seen through has to be in
     * the depth buffer before it draws */
    watDraw(uModel,uPal,uBind,uUse,uTint,g_labAlpha); }""",
    "draw the water")

sub("static GLint g_labUse=-1, g_labTint=-1;",
    "static GLint g_labUse=-1, g_labTint=-1, g_labAlpha=-1;",
    "publish the alpha uniform")

sub("                         g_labUse=AP->uUse; g_labTint=AP->uTint;",
    "                         g_labUse=AP->uUse; g_labTint=AP->uTint; g_labAlpha=AP->uAlpha;",
    "capture the alpha location")

sub('''            printf("[set] %s: %d props + %d actors + %d cloth from %d packs''' + chr(92) + '''n",
                   name,g_setNI,g_setNA,g_nScl,g_setNP); fflush(stdout); }''',
    '''            printf("[set] %s: %d props + %d actors + %d cloth + %d water from %d packs''' + chr(92) + '''n",
                   name,g_setNI,g_setNA,g_nScl,g_nWat,g_setNP); fflush(stdout); }''',
    "report the water count")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
