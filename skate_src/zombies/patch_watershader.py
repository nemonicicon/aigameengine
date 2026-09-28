"""patch_watershader.py -- water that REFLECTS, and a procedural sky for it to reflect.

jacob, on the first pool strip: "looks like you also need add the capability for
transparencies and reflections... and we need a real skybox."

⭐ HE IS RIGHT, AND IT IS THE CORRECT DIAGNOSIS OF A WORKING SYSTEM. The height field is
genuinely simulating -- the crater bug proved the disturbance reached it, the settle proved
the springback -- and it renders as a flat blue mat. Water does not look like water because
of its shape. It looks like water because it MIRRORS THE SKY, and the club's sky is a flat
clear colour with no geometry at all: there has never been anything for a reflection to
sample.

⭐ SO THE SKY BECOMES A FUNCTION BEFORE IT BECOMES A BOX. skyColor(dir) is one GLSL
function -- horizon-to-zenith gradient, sun disc, sun glow, ground haze below the horizon --
driven by the sun rig the whole engine already shares (g_sun / g_sunC / g_sky, which `tod=`
already animates across a whole day). Water can reflect that ANALYTICALLY without any
skybox being drawn, which is why the reflection lands first and the skybox pass reuses the
identical function afterwards. One source of truth for what the sky looks like.

⭐ AND FRESNEL IS WHAT MAKES A RIPPLE VISIBLE. Straight down through water you see the
bottom; at a glancing angle you see the sky. That ratio changes with the SURFACE NORMAL, so
a ripple two centimetres high swings the reflected ray far enough to change the colour
completely -- which is why real water shows its waves and a diffuse blue plane does not,
however accurately it is simulated.

⛔ WATER GETS ITS OWN PROGRAM. It was borrowing the mesh shader, which has no eye vector,
no reflection and no Fresnel, and could only ever have produced a tinted diffuse plane.
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


WATERGL = r'''
/* ---- THE SKY, AS A FUNCTION ---------------------------------------------------------
 * jacob: "we need a real skybox". It becomes a FUNCTION before it becomes a box, because
 * water has to reflect it and a reflection samples a direction, not a mesh -- so this one
 * string is shared by the water shader and (later) the sky pass, and there is exactly one
 * description of what the sky looks like. Driven by the sun rig the engine already has,
 * which `tod=` animates across a whole day. */
static const char*SKY_GLSL=
  "uniform vec3 uSunDir; uniform vec3 uSunCol; uniform float uSunI;\n"
  "uniform vec3 uSkyZen; uniform vec3 uSkyHz; uniform vec3 uGround;\n"
  "vec3 skyColor(vec3 d){\n"
  "  d=normalize(d);\n"
  "  float t=clamp(d.y,0.0,1.0);\n"
  "  vec3 c=mix(uSkyHz,uSkyZen,pow(t,0.55));\n"
  /* the sun: a hard disc inside a soft glow, both tinted by the sun's own colour so a low
   * orange sun gives an orange horizon for free */
  "  float sd=max(dot(d,normalize(uSunDir)),0.0);\n"
  "  c+=uSunCol*uSunI*(pow(sd,1200.0)*9.0+pow(sd,26.0)*0.42+pow(sd,4.0)*0.10);\n"
  /* and below the horizon it is ground, not more sky -- a reflection that looks DOWN must
   * not come back with blue in it */
  "  c=mix(uGround,c,smoothstep(-0.10,0.015,d.y));\n"
  "  return c; }\n";

static GLuint g_watProg=0;
static GLint  g_wVP=-1,g_wEye=-1,g_wTint=-1,g_wSunDir=-1,g_wSunCol=-1,g_wSunI=-1,
              g_wZen=-1,g_wHz=-1,g_wGnd=-1;

static void watProgInit(void){
    if(g_watProg) return;
    const char*vs=
      "attribute vec3 p; attribute vec3 n;\n"
      "uniform mat4 vp;\n"
      "varying vec3 vn; varying vec3 vwp;\n"
      "void main(){ vn=n; vwp=p; gl_Position=vp*vec4(p,1.0); }\n";
    static char fs[4096];
    snprintf(fs,sizeof fs,
      "precision mediump float;\n"
      "varying vec3 vn; varying vec3 vwp;\n"
      "uniform vec3 uEye; uniform vec3 uTint;\n"
      "%s"
      "void main(){\n"
      "  vec3 N=normalize(vn);\n"
      "  vec3 V=normalize(uEye-vwp);\n"
      "  if(dot(N,V)<0.0) N=-N;\n"
      /* ⭐ FRESNEL IS WHY A RIPPLE IS VISIBLE. Looking straight down you see through the
       * water; at a glancing angle you see the sky. A two-centimetre wave swings the
       * reflected ray far enough to change the colour completely, which is exactly what a
       * diffuse blue plane cannot do however well it is simulated. */
      "  float F=0.02+0.98*pow(1.0-clamp(dot(N,V),0.0,1.0),5.0);\n"
      "  vec3 R=reflect(-V,N);\n"
      "  vec3 refl=skyColor(R);\n"
      "  vec3 deep=uTint*(0.42+0.58*max(dot(N,normalize(uSunDir)),0.0));\n"
      "  vec3 c=mix(deep,refl,F);\n"
      /* a glint where the reflected ray finds the sun itself */
      "  c+=uSunCol*uSunI*pow(max(dot(R,normalize(uSunDir)),0.0),300.0)*1.8;\n"
      "  gl_FragColor=vec4(c,mix(0.58,0.97,F)); }\n", SKY_GLSL);
    GLuint pr=glCreateProgram();
    glAttachShader(pr,sh(GL_VERTEX_SHADER,vs));
    glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
    glBindAttribLocation(pr,0,"p"); glBindAttribLocation(pr,1,"n");
    glLinkProgram(pr);
    GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
    if(!ok){ char log[1024]; glGetProgramInfoLog(pr,1024,NULL,log);
             printf("[water] SHADER LINK FAILED: %s%c",log,10); fflush(stdout); return; }
    g_watProg=pr;
    g_wVP=glGetUniformLocation(pr,"vp");        g_wEye=glGetUniformLocation(pr,"uEye");
    g_wTint=glGetUniformLocation(pr,"uTint");   g_wSunDir=glGetUniformLocation(pr,"uSunDir");
    g_wSunCol=glGetUniformLocation(pr,"uSunCol");g_wSunI=glGetUniformLocation(pr,"uSunI");
    g_wZen=glGetUniformLocation(pr,"uSkyZen");  g_wHz=glGetUniformLocation(pr,"uSkyHz");
    g_wGnd=glGetUniformLocation(pr,"uGround");
    printf("[water] reflection shader ready%c",10); fflush(stdout); }
'''

sub("static void watAdd(float x,float y,float z,float w,float d,const float*rgb){",
    WATERGL + "\nstatic void watAdd(float x,float y,float z,float w,float d,const float*rgb){",
    "the sky function and the water program")

# --- watDraw uses its own program ----------------------------------------------------
sub("""static void watDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,GLint uAlpha){
    if(!g_nWat) return;
    static const float I9[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static int stamp=-1;""",
    """static void watDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,GLint uAlpha){
    (void)uModel;(void)uPal;(void)uBind;(void)uUse;(void)uTint;(void)uAlpha;
    if(!g_nWat) return;
    static int stamp=-1;""",
    "watDraw drops the mesh uniforms")

sub("""        glUniformMatrix4fv(uModel,1,GL_FALSE,I9);
        glUniformMatrix4fv(uPal,1,GL_FALSE,I9);
        glUniformMatrix4fv(uBind,1,GL_FALSE,I9);
        glUniform1f(uUse,0.f); glUniform3f(uTint,W->tint[0],W->tint[1],W->tint[2]);
        if(uAlpha>=0) glUniform1f(uAlpha,0.72f);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib);
        glDrawElements(GL_TRIANGLES,(GLsizei)W->nIdx,GL_UNSIGNED_SHORT,0); }
    if(uAlpha>=0) glUniform1f(uAlpha,1.f);
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f); }""",
    """        glUniform3f(g_wTint,W->tint[0],W->tint[1],W->tint[2]);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,W->ib);
        glDrawElements(GL_TRIANGLES,(GLsizei)W->nIdx,GL_UNSIGNED_SHORT,0); }
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    glUseProgram(g_watPrev); }""",
    "watDraw draws with the water program")

sub("""    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for(int q=0;q<g_nWat;q++){""",
    """    watProgInit();
    if(!g_watProg) return;
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    /* ⛔ THE WATER OWNS THE PROGRAM FOR THIS PASS, so the caller's program has to come
     * back afterwards or every mesh drawn after the water renders with a water shader. */
    GLint g_watPrev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&g_watPrev);
    glUseProgram(g_watProg);
    glUniformMatrix4fv(g_wVP,1,GL_FALSE,g_watVP);
    glUniform3fv(g_wEye,1,g_camPos);
    glUniform3fv(g_wSunDir,1,g_sun);
    glUniform3fv(g_wSunCol,1,g_sunC);
    glUniform1f(g_wSunI,g_sun[3]>0.f?g_sun[3]:0.15f);
    /* the zenith/horizon pair is derived from the ambient the sun rig already publishes,
     * so a sunset sky and a sunset reflection cannot disagree */
    glUniform3f(g_wZen,g_sky[0]*1.35f,g_sky[1]*1.45f,g_sky[2]*1.75f);
    glUniform3f(g_wHz ,g_sky[0]*1.85f+0.10f,g_sky[1]*1.80f+0.11f,g_sky[2]*1.70f+0.12f);
    glUniform3f(g_wGnd,g_sky[0]*0.55f,g_sky[1]*0.58f,g_sky[2]*0.50f);
    for(int q=0;q<g_nWat;q++){""",
    "bind the water program and its sky")

# --- the water needs the view-projection ---------------------------------------------
sub("static Water g_wat[WAT_MAX]; static int g_nWat=0;",
    """static Water g_wat[WAT_MAX]; static int g_nWat=0;
/* ⛔ THE WATER HAS ITS OWN PROGRAM, so it needs its own copy of the view-projection --
 * drawSet is handed uniform LOCATIONS, not the matrix, and a location belongs to the
 * program it came from. Published by whoever sets up the scene pass. */
static float g_watVP[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};""",
    "publish the VP for the water")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
