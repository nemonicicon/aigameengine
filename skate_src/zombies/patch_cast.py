"""patch_cast.py -- THE SCENE SYSTEM TAKES MORE THAN TWO PEOPLE.

jacob: "our scene system needs to be capable of handling more than 2 people."

⛔ AND IT WAS A TWO-HANDER TO THE BONE. One `g_labPartner`, one `g_labPClip`, one
`g_partHeadW` -- 74 references to a second body that could only ever be a second body.
Act V had to be split in half to be shootable, and act III has three women in a street
with only two of them able to speak.

⛔ BUT A STRAIGHT REFACTOR IS THE WRONG MOVE, and this project has already written that
down: turning hero/partner into an array "would have rewritten the system that made THE
SPOT." Seventy-four hand-edited call sites in a nine-thousand-line file is how a working
scene system stops working.

⭐ SO: THE GLOBALS BECOME SLOT 1 OF A CAST ARRAY, ADDRESSED THROUGH #define. Every existing
reference resolves to `g_cast[g_castI]` without being touched, `g_castI` is 1 everywhere it
used to be implicit, and the two-body path is bit-identical by construction. Slots 2 and 3
are the same struct, drawn by the same function, in a loop.

    static CastSlot g_cast[SCN_MAXCAST];   /* [0] is the hero, drawn by the main path */
    static int g_castI = 1;                /* which slot labDrawPartner is currently on */
    #define g_labPartner  (g_cast[g_castI].pk)
    ...

This is stage one: LOAD AND DRAW a third body, and harvest its marks. The camera still
frames slots 0 and 1. Stage two gives the shot solver the whole cast.
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


# ---------------------------------------------------------------- 1. the cast slot
CAST = '''
/* ======================= THE CAST: MORE THAN TWO PEOPLE =========================
 * jacob: "our scene system needs to be capable of handling more than 2 people."
 *
 * ⛔ The scene system was a two-hander to the bone -- one g_labPartner, one g_labPClip,
 * one g_partHeadW, and 74 references to a second body that could only ever be a second
 * body. Act V had to be split in half to be shootable.
 *
 * ⛔ AND A STRAIGHT REFACTOR IS THE WRONG MOVE. This project already wrote that down:
 * turning hero/partner into an array "would have rewritten the system that made THE
 * SPOT." Hand-editing 74 call sites is how a working scene system stops working.
 *
 * ⭐ So the globals BECOME slot 1 of this array, addressed through #define. Every
 * existing reference resolves to g_cast[g_castI] without being touched; g_castI is 1
 * everywhere it used to be implicit; the two-body path is bit-identical by construction.
 * Slots 2 and 3 are the same struct, drawn by the same function, in a loop.
 *
 * ⛔ SLOT 0 IS THE HERO and is NOT stored here -- she is the lab's own body, drawn by the
 * main path, and her marks live in g_heroHeadW/g_heroEyeW. The cast array is everyone
 * ELSE. Slot 0's entries stay zero and must never be drawn from. */
#define SCN_MAXCAST 4
typedef struct {
    Pack  *pk;  int clip;  double phase;      /* who, what she is playing, and where in it */
    Pack  *dnP; int dnC;                      /* ...or a clip borrowed from a bank */
    float  gap, side, yaw;                    /* placement relative to the HERO's frame */
    float  headW[3], eyeW[3], mouthW[3];      /* measured by her draw, in world space */
    int    headOK, faceOK;  float iod;
    Pack   storage;                           /* the loaded pack itself */
} CastSlot;
static CastSlot g_cast[SCN_MAXCAST];
static int g_castI = 1;      /* the slot labDrawPartner is working on right now */
static int g_scnNCast = 2;   /* how many bodies the scene has, hero included */

#define g_labPartner   (g_cast[g_castI].pk)
#define g_labPClip     (g_cast[g_castI].clip)
#define g_labPPhase    (g_cast[g_castI].phase)
#define g_labPDnP      (g_cast[g_castI].dnP)
#define g_labPDnC      (g_cast[g_castI].dnC)
#define g_labPGap      (g_cast[g_castI].gap)
#define g_labPSide     (g_cast[g_castI].side)
#define g_labPYaw      (g_cast[g_castI].yaw)
#define g_labPartnerPk (g_cast[g_castI].storage)
#define g_partHeadW    (g_cast[g_castI].headW)
#define g_partEyeW     (g_cast[g_castI].eyeW)
#define g_partMouthW   (g_cast[g_castI].mouthW)
#define g_partHeadOK   (g_cast[g_castI].headOK)
#define g_partFaceOK   (g_cast[g_castI].faceOK)
#define g_partIOD      (g_cast[g_castI].iod)

/* ⛔ THE DEFAULTS ARE NOT ZERO. gap 1.60 / yaw 180 / clip -1 used to be static
 * initialisers; a struct array zero-fills, which would stand every extra INSIDE the hero
 * facing the same way with clip 0. Called once at startup. */
static void castInit(void){
    for(int i=0;i<SCN_MAXCAST;i++){
        g_cast[i].pk=NULL; g_cast[i].clip=-1; g_cast[i].phase=0.0;
        g_cast[i].dnP=NULL; g_cast[i].dnC=-1;
        g_cast[i].gap=1.60f; g_cast[i].side=0.0f; g_cast[i].yaw=180.0f;
        g_cast[i].headOK=0; g_cast[i].faceOK=0; g_cast[i].iod=0.f; } }
'''

sub("static Pack *g_labPartner=NULL;                /* the second body, loaded on demand */",
    CAST + "\n/* (slot 1's fields are the old partner globals; see the cast note above) */",
    "the cast array and its defines")

# ---------------------------------------------------------------- 2. drop the old decls
for dead, why in [
    ("static int   g_labPClip=-1;                    /* her clip index in her own pack */\n", "g_labPClip decl"),
    ("static Pack  g_labPartnerPk;                   /* storage for the loaded partner pack */\n", "g_labPartnerPk decl"),
    ("static double g_labPPhase=0.0;                 /* her frame cursor, advanced with the hero's */\n", "g_labPPhase decl"),
    ("static Pack *g_labPDnP=NULL; static int g_labPDnC=-1;\n", "donor decls"),
    ("static float g_labPGap=1.60f;                  /* how far in front of the hero she stands, WORLD units */\n", "gap decl"),
    ("static float g_labPSide=0.0f;                  /* P8: lateral offset along the hero's LEFT -- side-by-side moves (hand in hand) */\n", "side decl"),
    ("static float g_labPYaw=180.0f;                 /* her facing relative to the hero's: 180 = face to face */\n", "yaw decl"),
]:
    sub(dead, "", why)

sub("static float g_heroHeadW[3]={0,0,0}, g_partHeadW[3]={0,0,0};",
    "static float g_heroHeadW[3]={0,0,0};", "split headW decl")
sub("static float g_heroIOD=0.f, g_partIOD=0.f;",
    "static float g_heroIOD=0.f;", "split iod decl")
sub("static float g_partMouthW[3]={0,0,0}, g_partEyeW[3]={0,0,0};",
    "", "drop mouth/eye decls")
sub("static int   g_heroFaceOK=0, g_partFaceOK=0;",
    "static int   g_heroFaceOK=0;", "split faceOK decl")
sub("static int g_heroHeadOK=0, g_partHeadOK=0;",
    "static int g_heroHeadOK=0;", "split headOK decl")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched (stage 1: the cast array exists)")
