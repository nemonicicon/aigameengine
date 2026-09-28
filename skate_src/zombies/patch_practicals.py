"""patch_practicals.py -- Z6b: a set owns the screen, and it can carry its own lamps.

TWO THINGS, both of which the film's night needs.

1. THE CLUB'S STAGE DECORATION STEPS OFF. Two soft glowing orbs sat over the road in
   every frame of the day sheet. They are not the sky-layer flyers -- REVA_NOSKY=1 left
   them exactly where they were -- they are the club's own lamp sprites and light ORBS,
   "soft halo + bright core riding the same sprite program", along with the flame snake.
   They were ALREADY suppressed while a scene plays (`if(g_scnT<0.f)`) because a
   conversation wants a dark stage. A street wants the same thing for the same reason, so
   the gate now also asks whether a set is loaded.

2. `light x y z r g b [power]` IN THE SET FILE. Every mesh program already carries
   lp[3]/lc[3] with falloff 1/(1+dd*0.055); those three are the club's band lights and a
   set can now claim them. Three is the cap and the cap is the shader's, not a choice.
   At night they are the whole picture: a porch light, a lamp behind a curtain, and the
   flashlight susan is holding while the windows go up.

   A set with NO light lines binds ZEROS, which is right: a daylit street is lit by the
   sun and nothing else, and a night one is dark until somebody turns something on.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
CR, LF = chr(13), chr(10)
CRLF = CR + LF
s = io.open(P, encoding="utf-8", newline="").read()
WAS_CRLF = CRLF in s
if WAS_CRLF:
    s = s.replace(CRLF, LF)
orig = s
BS_N = chr(92) + "n"


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ------------------------------------------------- 1. the globals
a = """static float g_stage[3]={0,0,0}; static int g_stageOn=0;"""
b = a + """
/* Z6 THE PRACTICALS. The shader's three point lights, claimed by the set file:
 *     light x y z r g b [power]
 * Three is the cap because lp[3]/lc[3] is the cap. Zeros mean "the sun and nothing
 * else", which is exactly right for a daylit street and honest about a dark one. */
static float g_setLp[9]={0,-99,0, 0,-99,0, 0,-99,0}, g_setLc[9]={0,0,0, 0,0,0, 0,0,0};
static int g_setNL=0;"""
sub(a, b, "practical globals")

# ------------------------------------------------- 2. reset them per load
a = """static int setLoad(const char*name){
    g_setNI=0; g_setNA=0; g_setCur[0]=0;"""
b = """static int setLoad(const char*name){
    g_setNI=0; g_setNA=0; g_setCur[0]=0;
    g_setNL=0;
    for(int k=0;k<3;k++){ g_setLp[k*3]=0.f; g_setLp[k*3+1]=-99.f; g_setLp[k*3+2]=0.f;
                          g_setLc[k*3]=g_setLc[k*3+1]=g_setLc[k*3+2]=0.f; }"""
sub(a, b, "practicals reset on load")

# ------------------------------------------------- 3. parse the light line
a = """        char stem[64]; float x,y,z,yaw,sc,r=0,g=0,b=0;
        if(!strncmp(ln,"actor ",6)){"""
b = """        char stem[64]; float x,y,z,yaw,sc,r=0,g=0,b=0;
        if(!strncmp(ln,"light ",6)){
            float lx,ly,lz,lr,lg,lb,lpw=1.f;
            int nl=sscanf(ln,"light %f %f %f %f %f %f %f",&lx,&ly,&lz,&lr,&lg,&lb,&lpw);
            if(nl>=6&&g_setNL<3){
                g_setLp[g_setNL*3]=lx; g_setLp[g_setNL*3+1]=ly; g_setLp[g_setNL*3+2]=lz;
                g_setLc[g_setNL*3]=lr*lpw; g_setLc[g_setNL*3+1]=lg*lpw; g_setLc[g_setNL*3+2]=lb*lpw;
                g_setNL++; }
            else if(nl>=6) printf("[set] light cap is 3 (the shader's lp[3]) -- ignoring@N");
            continue; }
        if(!strncmp(ln,"actor ",6)){""".replace("@N", BS_N)
sub(a, b, "the light line")

# ------------------------------------------------- 4. bind them for the set + actors
a = """                if(g_mode==MODE_ANIM&&g_setNI)
                    drawSet(mModel,mPal,mBind,g_labUse,g_labTint);"""
b = """                /* THE SET'S OWN LAMPS, bound once for the props AND the actors that
                 * follow -- same program, so one bind carries both. A set with no light
                 * lines binds zeros and is lit by the sun alone. */
                if(g_mode==MODE_ANIM&&g_setNI){
                    GLint pcur=0; glGetIntegerv(GL_CURRENT_PROGRAM,&pcur);
                    if(pcur){ GLint ulp=glGetUniformLocation(pcur,"lp");
                              GLint ulc=glGetUniformLocation(pcur,"lc");
                              if(ulp>=0) glUniform3fv(ulp,3,g_setLp);
                              if(ulc>=0) glUniform3fv(ulc,3,g_setLc); } }
                if(g_mode==MODE_ANIM&&g_setNI)
                    drawSet(mModel,mPal,mBind,g_labUse,g_labTint);"""
sub(a, b, "bind the practicals before the set draws")

# ------------------------------------------------- 5. the club's decoration steps off
a = """        if(g_scnT<0.f){
        glBlendFunc(GL_SRC_ALPHA,GL_ONE);"""
b = """        /* ⭐ ...AND A SET DOES TOO. Two soft orbs hung over the road in every frame of
         * the first day sheet. Not the sky-layer flyers -- REVA_NOSKY left them exactly
         * where they were -- but the club's own lamp sprites, light orbs and flame snake,
         * which a scene already switches off because a conversation wants a dark stage.
         * A street wants it for the same reason. */
        if(g_scnT<0.f&&!g_setNI){
        glBlendFunc(GL_SRC_ALPHA,GL_ONE);"""
sub(a, b, "no club lamps/orbs/snake over a set")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
