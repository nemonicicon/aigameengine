"""patch_stage.py -- ctl stage=x,z,yaw: the conversation leaves the black stage.

The scene system and the set renderer already coexist -- proven on pixels: `scene=the_spot
set=parsons` played its lines, drew its subtitle, and rendered the whole street behind it.
The only thing missing was that the pair stood where the lab puts them (0, -150) while the
street was somewhere else entirely.

The hero's model matrix is built from g_rvx / g_rvz / g_rvHead, the partner is placed
RELATIVE to that matrix, and both head world positions -- which is what the scene camera
frames -- fall out of it. So one stage mark moves the conversation, the second body, the
eyelines and the camera together; nothing in the two-body system that made THE SPOT has to
change. That is the whole point of doing it here rather than generalising hero/partner
into an array.

⛔ It must be applied AFTER the lab's floor clamps (g_rvx to +-FLOOR_X, g_rvz to 2.0),
which otherwise drag her straight back off the street.
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


# ---------------------------------------------------------------- the globals
a = """static float g_camPos[3]={0,0,0}, g_camAim[3]={0,0,0}, g_camFov=-1.f;"""
b = """static float g_camPos[3]={0,0,0}, g_camAim[3]={0,0,0}, g_camFov=-1.f;
/* ⭐ THE STAGE MARK. ctl stage=x,z,yaw stands the conversation pair at a place in the
 * WORLD instead of the lab's own spot, so a scene can be played on the street the set
 * renderer just built. Everything else follows for free: the partner is placed relative
 * to the hero's matrix, and the scene camera frames the two head positions that come out
 * of it. stage=off returns her to the lab. */
static float g_stage[3]={0,0,0}; static int g_stageOn=0;"""
sub(a, b, "stage globals")

# ---------------------------------------------------------------- apply it
a = """            if(g_rvx<-(FLOOR_X-1.5f))g_rvx=-(FLOOR_X-1.5f); if(g_rvx>FLOOR_X-1.5f)g_rvx=FLOOR_X-1.5f;
            if(g_rvz<FLOOR_ZN+1.5f)g_rvz=FLOOR_ZN+1.5f; if(g_rvz>2.0f)g_rvz=2.0f;"""
b = a + """
            /* ⛔ AFTER the floor clamps, or the lab's own dance-floor bounds drag the
             * pair straight back off the street they were staged on. */
            if(g_stageOn){ g_rvx=g_stage[0]; g_rvz=g_stage[1]; g_rvHead=g_stage[2]; }"""
sub(a, b, "the stage mark overrides the lab's position")

# ---------------------------------------------------------------- the ctl
a = """                        if((q=strstr(buf,"jaw="))) g_jawForce=(float)atof(q+4);"""
b = ("""                        if((q=strstr(buf,"stage="))){ float sx,sz,sy;
                            if(!strncmp(q+6,"off",3)){ g_stageOn=0; printf("[stage] off@N"); }
                            else if(sscanf(q+6,"%f,%f,%f",&sx,&sz,&sy)==3){
                                g_stage[0]=sx; g_stage[1]=sz; g_stage[2]=sy*0.0174532925f;
                                g_stageOn=1;
                                printf("[stage] pair at (%.2f, %.2f) facing %.0f deg@N",sx,sz,sy); }
                            fflush(stdout); }
                        if((q=strstr(buf,"jaw="))) g_jawForce=(float)atof(q+4);""").replace("@N", BS_N)
sub(a, b, "ctl stage=")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
