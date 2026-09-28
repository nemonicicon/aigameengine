"""patch_skybox.py -- P2: the sky becomes a THING, not just a function.

jacob: "we need a real skybox" and "we can see the end of the envirmonet".

⭐ TWO PROBLEMS, ONE PASS. The ground is 28x28 tiles with a single tile of run-off, so any
wide shot runs out of world and shows the void behind it. And the water reflects a sky the
audience cannot see, because `skyColor(dir)` existed only inside the water shader. Drawing
that same function behind everything fixes both at once -- the horizon gets something to be,
and the reflection finally matches what is above it.

⭐ A BOX AROUND THE CAMERA, NOT A FULLSCREEN QUAD. A fullscreen pass needs the INVERSE
view-projection to recover a ray per pixel, and drawSet is handed uniform LOCATIONS rather
than matrices. A cube centred on the eye needs no inversion at all: the interpolated world
position minus the eye IS the ray, and it rides the view-projection the rest of the frame
already uses. Twelve triangles.

⛔ DEPTH WRITE OFF, DEPTH TEST OFF, DRAWN FIRST. The sky is infinitely far away; if it
writes depth it occludes the world, and if it tests depth it fights the clear.

⛔ AND IT IS GATED ON A SET BEING LOADED. The club's eleven doors have their own deliberate
backdrops -- a black room, a star field, a dance floor -- and a daylight sky behind them
would be wrong. It also means shotsweep's lab and face-rig shots are untouched, which is
the difference between a rendering change and a rendering regression.
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


SKYBOX = r'''
/* ---- THE SKYBOX PASS ---------------------------------------------------------------
 * jacob: "we need a real skybox" and "we can see the end of the envirmonet".
 * ⭐ ONE PASS, BOTH PROBLEMS. The ground runs out (28x28 tiles plus one of run-off), and
 * the water was reflecting a sky nobody could see. Drawing the SAME skyColor() the water
 * samples means the horizon exists and the reflection matches what is above it.
 * ⭐ A BOX AROUND THE EYE, not a fullscreen quad: a fullscreen pass needs the INVERSE
 * view-projection to recover a ray, and drawSet is handed uniform locations, not matrices.
 * On a cube centred on the camera the interpolated world position minus the eye IS the
 * ray, and it rides the view-projection the frame already has. */
static GLuint g_skyProg=0, g_skyVB=0;
static GLint  g_skVP=-1,g_skEye=-1,g_skSunDir=-1,g_skSunCol=-1,g_skSunI=-1,
              g_skZen=-1,g_skHz=-1,g_skGnd=-1;

static void skyDraw(void){
    if(!g_skyProg){
        const char*vs=
          "attribute vec3 p;\n"
          "uniform mat4 vp; uniform vec3 uEye;\n"
          "varying vec3 vdir;\n"
          "void main(){ vec3 wp=uEye+p*90.0; vdir=p; gl_Position=vp*vec4(wp,1.0); }\n";
        static char fs[3072];
        snprintf(fs,sizeof fs,
          "precision mediump float;\n varying vec3 vdir;\n"
          "%s"
          "void main(){ gl_FragColor=vec4(skyColor(vdir),1.0); }\n", SKY_GLSL);
        GLuint pr=glCreateProgram();
        glAttachShader(pr,sh(GL_VERTEX_SHADER,vs));
        glAttachShader(pr,sh(GL_FRAGMENT_SHADER,fs));
        glBindAttribLocation(pr,0,"p");
        glLinkProgram(pr);
        GLint ok=0; glGetProgramiv(pr,GL_LINK_STATUS,&ok);
        if(!ok){ char log[512]; glGetProgramInfoLog(pr,512,NULL,log);
                 printf("[sky] LINK FAILED: %s%c",log,10); fflush(stdout); g_skyProg=(GLuint)-1; return; }
        g_skyProg=pr;
        g_skVP=glGetUniformLocation(pr,"vp");        g_skEye=glGetUniformLocation(pr,"uEye");
        g_skSunDir=glGetUniformLocation(pr,"uSunDir");g_skSunCol=glGetUniformLocation(pr,"uSunCol");
        g_skSunI=glGetUniformLocation(pr,"uSunI");   g_skZen=glGetUniformLocation(pr,"uSkyZen");
        g_skHz=glGetUniformLocation(pr,"uSkyHz");    g_skGnd=glGetUniformLocation(pr,"uGround");
        static const float C[]={
            -1,-1,-1,  1,-1,-1,  1, 1,-1, -1,-1,-1,  1, 1,-1, -1, 1,-1,
            -1,-1, 1, -1, 1, 1,   1, 1, 1, -1,-1, 1,  1, 1, 1,  1,-1, 1,
            -1,-1,-1, -1, 1,-1,  -1, 1, 1, -1,-1,-1, -1, 1, 1, -1,-1, 1,
             1,-1,-1,  1,-1, 1,   1, 1, 1,  1,-1,-1,  1, 1, 1,  1, 1,-1,
            -1, 1,-1,  1, 1,-1,   1, 1, 1, -1, 1,-1,  1, 1, 1, -1, 1, 1,
            -1,-1,-1, -1,-1, 1,   1,-1, 1, -1,-1,-1,  1,-1, 1,  1,-1,-1 };
        glGenBuffers(1,&g_skyVB);
        glBindBuffer(GL_ARRAY_BUFFER,g_skyVB);
        glBufferData(GL_ARRAY_BUFFER,sizeof C,C,GL_STATIC_DRAW);
        printf("[sky] skybox ready%c",10); fflush(stdout); }
    if(g_skyProg==(GLuint)-1) return;
    GLint prev=0; glGetIntegerv(GL_CURRENT_PROGRAM,&prev);
    /* ⛔ NO DEPTH AT ALL. The sky is infinitely far: writing depth would occlude the world,
     * testing it would fight the clear. Drawn first, painted over by everything. */
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_BLEND);
    glUseProgram(g_skyProg);
    glUniformMatrix4fv(g_skVP,1,GL_FALSE,g_watVP);
    glUniform3fv(g_skEye,1,g_camPos);
    glUniform3fv(g_skSunDir,1,g_sun);
    glUniform3fv(g_skSunCol,1,g_sunC);
    glUniform1f(g_skSunI,g_sun[3]>0.f?g_sun[3]:0.15f);
    glUniform3f(g_skZen,g_sky[0]*1.35f,g_sky[1]*1.45f,g_sky[2]*1.75f);
    glUniform3f(g_skHz ,g_sky[0]*1.85f+0.10f,g_sky[1]*1.80f+0.11f,g_sky[2]*1.70f+0.12f);
    glUniform3f(g_skGnd,g_sky[0]*0.55f,g_sky[1]*0.58f,g_sky[2]*0.50f);
    glBindBuffer(GL_ARRAY_BUFFER,g_skyVB);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,0);
    glDrawArrays(GL_TRIANGLES,0,36);
    glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    glUseProgram((GLuint)prev); }
'''

sub("static void watDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,GLint uAlpha){",
    SKYBOX + "\nstatic void watDraw(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint,GLint uAlpha){",
    "the skybox pass")

# ---- draw it FIRST, and only when a set is loaded -----------------------------------
sub("""    glActiveTexture(GL_TEXTURE0);
    for(int i=0;i<g_setNI;i++){
        SetInst*I=&g_setI[i];""",
    """    /* ⭐ THE SKY GOES DOWN FIRST, and only for a SET. The club's eleven doors have their
     * own deliberate backdrops -- a black room, a star field, a dance floor -- and a
     * daylight sky behind them would be wrong. Gating here also leaves shotsweep's lab and
     * face-rig shots byte-identical, which is what separates a rendering change from a
     * rendering regression. */
    skyDraw();
    glActiveTexture(GL_TEXTURE0);
    for(int i=0;i<g_setNI;i++){
        SetInst*I=&g_setI[i];""",
    "draw the sky before the set")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
