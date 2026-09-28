"""patch_collide.py -- CAPABILITY A: RUNTIME COLLISION.

jacob has said this world becomes a playable 3D space. Nothing in it stops a body walking
through a house, a fence, a parked van or another person. `clear_spot` in street.py is
AUTHORING-TIME separation between props -- it stops the generator stacking two things in
one cubic metre and has no bearing on anything that moves.

⭐ THE SET ALREADY KNOWS THE SHAPE OF THE WORLD. Every prop instance carries a matrix, and
every prop pack was measured at load for the bone cloud's extent. A collider set is those
two facts multiplied together, built once when the set is parsed -- no new data, no new
pipeline, no authoring.

    circle (x, z, r)  per solid prop instance
    push a mover out  when it ends a frame inside one

⛔ CIRCLES, NOT BOXES, AND ONLY IN XZ. A wall is not a circle and a fence is not a circle;
this will not hold a body against a flat surface convincingly at a grazing angle. It is
what a swept capsule against real geometry would do properly, and that is a bigger job.
What this DOES do is the thing that was actually happening on screen: reva walking through
a parked van. Written down so nobody discovers the limit as a surprise.

⛔ AND MOST OF A SET IS NOT SOLID. Ground, road, sidewalk, grass, dirt, roof, carpet and
the wall/window/door tiles the room shell is made of must NOT collide -- a room built out
of colliding walls is a room nobody can stand in, and a road that pushes you off it is
worse than no collision at all. Solidity is decided by NAME, from the same vocabulary the
texture audit uses, and the list is in one place.

⛔ AND THE HERO IS NOT PUSHED. She is placed by `stage=` and by a scene's own marks, which
are authored positions -- shoving her out of them would fight the direction. Actors on
paths are resolved; the player mover is where this earns its keep later.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- the pack remembers its own footprint ------------------------------------------
sub("    float effH,avoidR; /* rendered height (mesh-aware, giantess-correct) -> personal-space radius */",
    """    float effH,avoidR; /* rendered height (mesh-aware, giantess-correct) -> personal-space radius */
    float footR;       /* XZ radius of the rest bone cloud -- what a collider is built from */""",
    "Pack carries a footprint radius")

sub("""    P.cx=(mnx+mxx)*0.5f; P.cz=(mnz+mxz)*0.5f; P.mny=mny;""",
    """    P.cx=(mnx+mxx)*0.5f; P.cz=(mnz+mxz)*0.5f; P.mny=mny;
    /* ⭐ AND ITS FOOTPRINT, from the same pass. Half the larger XZ span: a circle that
     * contains the thing, which is what the collider set is built out of. Costs nothing --
     * the extents were already being computed two lines up and thrown away. */
    { float ex=(mxx-mnx)*0.5f, ez=(mxz-mnz)*0.5f;
      P.footR=(ex>ez?ex:ez); if(!(P.footR>0.f)) P.footR=0.f; }""",
    "measure the footprint at load")

# ---- the collider set ---------------------------------------------------------------
sub("static SetInst g_setI[SET_MAXI]; static int g_setNI=0;",
    """static SetInst g_setI[SET_MAXI]; static int g_setNI=0;

/* ================= RUNTIME COLLISION ==========================================
 * ⭐ The set already knows the shape of the world: every instance has a matrix and every
 * pack was measured for its extent at load. A collider set is those two multiplied, built
 * once at parse. No new data, no authoring, no pipeline.
 *
 * ⛔ CIRCLES, IN XZ ONLY. A wall is not a circle. This will not hold a body against a flat
 * surface at a grazing angle -- that wants a swept capsule against real geometry. What it
 * DOES stop is the thing that was on screen: walking through a parked van.
 *
 * ⛔ AND MOST OF A SET IS NOT SOLID. A room built out of colliding walls is a room nobody
 * can stand in, and a road that pushes you off it is worse than none. */
#define SET_MAXCOL 640
typedef struct { float x,z,r; } SetCol;
static SetCol g_col[SET_MAXCOL]; static int g_nCol=0;
static int g_colOff=0;                     /* REVA_NOCOLLIDE=1 */

/* Solid by NAME, from the same vocabulary the texture audit uses. Everything a body can
 * walk on, or that is a surface of the room they are standing in, is NOT solid. */
static int propSolid(const char*stem){
    static const char*OPEN[]={"ground","road","sidewalk","grass","dirt","floor","roof",
        "carpet","parcel","wall","window","walldoor","wallgarage","wallattic","cornice",
        "chimney","powercable","mountains","sewercover","doorframe","drainpipe",
        "newspaper","grassvertical","grasscutter",NULL};
    for(int i=0;OPEN[i];i++) if(strstr(stem,OPEN[i])) return 0;
    return 1; }

static void colAdd(float x,float z,float r){
    if(g_nCol>=SET_MAXCOL||!(r>0.02f)) return;
    g_col[g_nCol].x=x; g_col[g_nCol].z=z; g_col[g_nCol].r=r; g_nCol++; }

/* Push a mover of radius `mr` out of anything it is inside. Returns how far it moved,
 * which is what a caller uses to decide whether it is stuck. */
static float colResolve(float*x,float*z,float mr){
    if(g_colOff||!g_nCol) return 0.f;
    float ox=*x, oz=*z;
    for(int pass=0;pass<2;pass++)
      for(int i=0;i<g_nCol;i++){
        float dx=*x-g_col[i].x, dz=*z-g_col[i].z;
        float need=g_col[i].r+mr;
        float d2=dx*dx+dz*dz;
        if(d2>=need*need||d2<1e-9f) continue;
        float d=sqrtf(d2);
        float push=(need-d)/d;
        *x+=dx*push; *z+=dz*push; }
    float mx=*x-ox, mz=*z-oz;
    return sqrtf(mx*mx+mz*mz); }""",
    "the collider set")

# ---- built as instances are placed --------------------------------------------------
sub("""        SetInst*I=&g_setI[g_setNI++];""",
    """        SetInst*I=&g_setI[g_setNI++];
        /* ⭐ AND A COLLIDER, IF IT IS SOLID. The instance's own scale times the pack's
         * measured footprint -- both already known, neither authored. */
        { Pack*CP=&g_setPk[pi];
          if(propSolid(stem)){
              float sc9=sqrtf(I->m[0]*I->m[0]+I->m[1]*I->m[1]+I->m[2]*I->m[2]);
              colAdd(I->m[12],I->m[14],CP->footR*sc9*0.78f); } }""",
    "build colliders as props are placed")

sub("""    g_setNI=0; g_setNA=0; g_setNMk=0; g_setCur[0]=0;""",
    """    g_setNI=0; g_setNA=0; g_setNMk=0; g_nCol=0; g_setCur[0]=0;
    { static int once=-1; if(once<0){ once=1; g_colOff=getenv("REVA_NOCOLLIDE")?1:0; } }""",
    "clear colliders with the set")

# ---- and actors on paths obey it ----------------------------------------------------
sub("""            A->cx=x0+(x1-x0)*u; A->cy=y0+(y1-y0)*u; A->cz=z0+(z1-z0)*u;""",
    """            A->cx=x0+(x1-x0)*u; A->cy=y0+(y1-y0)*u; A->cz=z0+(z1-z0)*u;
            /* ⭐ AND SHE DOES NOT WALK THROUGH THE VAN. The path is authored in a world
             * that has things in it; this is the first time anything has checked. */
            colResolve(&A->cx,&A->cz,0.42f);""",
    "actors on paths collide")

sub("""        else { px=A->x+sinf(a)*A->travel; pz=A->z+cosf(a)*A->travel; }""",
    """        else { px=A->x+sinf(a)*A->travel; pz=A->z+cosf(a)*A->travel;
               if(!A->skate) colResolve(&px,&pz,0.42f); }""",
    "actors on a heading collide")

# ---- report it -------------------------------------------------------------------
_k = s.index('printf("[set] %s: %d props')
_line0 = s.rindex(chr(10), 0, _k) + 1
s = (s[:_line0]
     + '            printf("[set] collision: %d solid colliders%s' + chr(92) + 'n",g_nCol,' + chr(10)
     + '                   g_colOff?" (DISABLED by REVA_NOCOLLIDE)":"");' + chr(10)
     + s[_line0:])
print("   ok: report the collider count")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
