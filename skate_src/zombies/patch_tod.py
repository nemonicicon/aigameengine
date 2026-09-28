"""patch_tod.py -- Z6: THE SUN GOES DOWN. A time of day, and lights that survive it.

SEVENTY-FOUR runs from a wet 7am to a boarded-up window after dark; the arc IS
the film. Until now the sun was one static direction set by hand per shot, which means
the day could only be cut, never played.

  ctl  tod=<hours>            0..24; 7.25 is quarter past seven in the morning
       tod=off                back to whatever sun=/sky= last set
       todrate=<hoursPerSec>  let the clock run -- a 15-minute dusk in 30 seconds

One curve drives sun elevation, azimuth, intensity, colour AND the sky ambient together,
because they are not independent: a low sun is a WARM sun and a warm sun sits under a
sky that has gone violet. Keyframed and interpolated, not computed from an ephemeris --
this is a film, and the golden hour lasts exactly as long as the cutting needs it to.

⭐ NIGHT IS NOT sun=off. `uSun.w == 0` is the club's own dark-room constant path, which
is right for a nightclub and useless outdoors -- it has no sky and no direction. Night
here is a WEAK BLUE DIRECTIONAL over a near-black sky ambient, which is what moonlight
actually is, so the same shader branch carries noon and midnight and the practicals read
against both. `sun=off` still means the dark room; the two do not collide.

⭐ AND THE PRACTICALS ARE THE THREE LIGHTS THE SHADER ALREADY HAS. Every mesh program
carries lp[3]/lc[3] with falloff 1/(1+dd*0.055) -- the club's band lights. A set file can
now claim them:

       light x y z r g b [power]

which is a porch light, a lamp behind a curtain, and the flashlight susan is holding.
Three is the cap and the cap is the shader's, not a choice.
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


# ------------------------------------------------------- 1. the day curve
a = """static void sunInit(void){ g_sun[0]=0.f; g_sun[1]=1.f; g_sun[2]=0.f; g_sun[3]=0.f;
    g_sunC[0]=1.f; g_sunC[1]=0.97f; g_sunC[2]=0.90f;
    g_sky[0]=0.30f; g_sky[1]=0.34f; g_sky[2]=0.42f; }"""
b = a + """
/* ================= Z6 THE DAY ======================================================
 * hour, elevation, azimuth, intensity, sun rgb, sky rgb. One table, because these are
 * not independent quantities: a low sun is a warm sun and a warm sun sits under a sky
 * that has gone violet. Keyframed rather than computed from an ephemeris -- this is a
 * film, and the golden hour lasts as long as the cutting needs it to.
 * ⭐ NIGHT IS A WEAK BLUE DIRECTIONAL over a near-black sky, not sun=off: w==0 is the
 * club's dark-room path, which has no sky and no direction and is useless outdoors. */
typedef struct { float h, el, az, it, r,g,b, sr,sg,sb; } TodKey;
static const TodKey TOD[]={
 /*  h    el    az     I     sun r,g,b            sky r,g,b            */
 { 0.0f,  42.f, 320.f, 0.10f, 0.55f,0.62f,0.88f,  0.030f,0.036f,0.062f },  /* deep night */
 { 4.6f,  26.f, 350.f, 0.10f, 0.55f,0.62f,0.88f,  0.036f,0.042f,0.072f },
 { 5.6f,  -3.f,  76.f, 0.06f, 0.80f,0.60f,0.62f,  0.105f,0.098f,0.150f },  /* first light */
 { 6.6f,   6.f,  84.f, 0.55f, 1.00f,0.63f,0.40f,  0.215f,0.200f,0.245f },  /* dawn */
 { 7.4f,  14.f,  93.f, 0.95f, 1.00f,0.80f,0.60f,  0.315f,0.350f,0.450f },  /* the film opens */
 { 9.0f,  30.f, 112.f, 1.20f, 1.00f,0.90f,0.76f,  0.400f,0.455f,0.570f },
 {12.0f,  66.f, 178.f, 1.38f, 1.00f,0.98f,0.94f,  0.460f,0.520f,0.640f },  /* noon */
 {15.0f,  40.f, 238.f, 1.24f, 1.00f,0.94f,0.84f,  0.440f,0.495f,0.605f },
 {17.4f,  16.f, 262.f, 0.92f, 1.00f,0.80f,0.56f,  0.380f,0.390f,0.470f },
 {18.6f,   6.f, 270.f, 0.62f, 1.00f,0.62f,0.34f,  0.310f,0.270f,0.300f },  /* golden */
 {19.4f,   0.5f,276.f, 0.28f, 1.00f,0.44f,0.24f,  0.190f,0.155f,0.200f },  /* sunset */
 {20.2f,  -6.f, 282.f, 0.08f, 0.72f,0.46f,0.52f,  0.088f,0.086f,0.135f },  /* twilight */
 {21.2f,  18.f, 298.f, 0.10f, 0.55f,0.62f,0.88f,  0.038f,0.044f,0.075f },  /* moon up */
 {24.0f,  42.f, 320.f, 0.10f, 0.55f,0.62f,0.88f,  0.030f,0.036f,0.062f }};
static float g_tod=-1.f;        /* <0: the clock is off and sun=/sky= own the light */
static float g_todRate=0.f;     /* hours per real second, for a timelapse */
static void todApply(float h){
    int n=(int)(sizeof TOD/sizeof TOD[0]);
    h=fmodf(h,24.f); if(h<0.f) h+=24.f;
    int i=0; while(i<n-2&&TOD[i+1].h<=h) i++;
    const TodKey*A=&TOD[i],*B=&TOD[i+1];
    float span=B->h-A->h, u=(span>1e-4f)?(h-A->h)/span:0.f;
    if(u<0.f) u=0.f; if(u>1.f) u=1.f;
    u=u*u*(3.f-2.f*u);                                  /* ease, so a cut never snaps */
    #define L(f) (A->f+(B->f-A->f)*u)
    float e=L(el)*0.0174532925f, a2=L(az)*0.0174532925f;
    g_sun[0]=cosf(e)*sinf(a2); g_sun[1]=sinf(e); g_sun[2]=cosf(e)*cosf(a2);
    g_sun[3]=L(it);
    g_sunC[0]=L(r); g_sunC[1]=L(g); g_sunC[2]=L(b);
    g_sky[0]=L(sr); g_sky[1]=L(sg); g_sky[2]=L(sb);
    #undef L
    /* the sun never goes below the horizon as a LIGHT -- a directional from underneath
     * lights faces from the chin up. Below the horizon it is the moon, already blue and
     * already weak, so lift the direction and leave the colour alone. */
    if(g_sun[1]<0.06f){ g_sun[1]=0.06f;
        float l9=sqrtf(g_sun[0]*g_sun[0]+g_sun[1]*g_sun[1]+g_sun[2]*g_sun[2]);
        if(l9>1e-5f){ g_sun[0]/=l9; g_sun[1]/=l9; g_sun[2]/=l9; } } }"""
sub(a, b, "the day curve + todApply")

# ------------------------------------------------------- 2. tick the clock
a = """        actorsTick((float)((g_fixedStepMs>0.0)?g_fixedStepMs*0.001:0.0166));"""
b = """        actorsTick((float)((g_fixedStepMs>0.0)?g_fixedStepMs*0.001:0.0166));
        if(g_tod>=0.f){   /* Z6: the day runs, and the light is recomputed every frame */
            if(g_todRate!=0.f) g_tod+=g_todRate*(float)((g_fixedStepMs>0.0)?g_fixedStepMs*0.001:0.0166);
            todApply(g_tod); }"""
sub(a, b, "the clock ticks with the frame")

# ------------------------------------------------------- 3. the ctl
a = """                        if((q=strstr(buf,"sky="))) sscanf(q+4,"%f,%f,%f",&g_sky[0],&g_sky[1],&g_sky[2]);"""
b = ("""                        if((q=strstr(buf,"tod="))){
                            if(!strncmp(q+4,"off",3)){ g_tod=-1.f; g_todRate=0.f; printf("[tod] off@N"); }
                            else { g_tod=(float)atof(q+4); todApply(g_tod);
                                printf("[tod] %05.2f  sun dir %.2f,%.2f,%.2f I %.2f  rgb %.2f,%.2f,%.2f  sky %.3f,%.3f,%.3f@N",
                                       g_tod,g_sun[0],g_sun[1],g_sun[2],g_sun[3],
                                       g_sunC[0],g_sunC[1],g_sunC[2],g_sky[0],g_sky[1],g_sky[2]); }
                            fflush(stdout); }
                        if((q=strstr(buf,"todrate="))) g_todRate=(float)atof(q+8);
                        if((q=strstr(buf,"sky="))) sscanf(q+4,"%f,%f,%f",&g_sky[0],&g_sky[1],&g_sky[2]);""").replace("@N", BS_N)
sub(a, b, "ctl tod= / todrate=")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
