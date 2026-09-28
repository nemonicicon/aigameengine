"""patch_props.py -- #2: PUT THE THING IN HER HAND.

Two coffees, a flashlight, a hammer, a board, a dog lead, a trestle table. Every one is in
this film's dialogue and not one of them has ever been in a hand.

⭐ AND EVERY PIECE ALREADY EXISTED, pointed somewhere else:
  * `gripNode` -- the real hand, found by name at load, already carrying weapons in the
    arena modes.
  * `drawProp` -- draws a prop pack at a matrix, already drawing 550 of them per frame.
  * the set's prop loader -- already turning a stem into a loaded pack.
None of the three had ever been introduced to the other two in a SCENE.

So: the bodies publish their grip in world space the same way they already publish their
head and eye marks, a tiny table holds what each of them is carrying, and the props are
drawn on those grips after the bodies.

⛔ THE GRIP IS A POINT, NOT A FRAME. This hangs the prop AT the hand with the body's own
yaw, which is right for a mug, a torch and a hammer held at rest, and wrong for anything
whose orientation has to read (a rifle down a sightline). That is a deliberate stop: the
film needs things carried, not things aimed, and a full grip basis is a bigger job than
this pass. Written down so nobody discovers it as a surprise.

⛔ AND A PROP MUST NOT BE SEARCHED FOR EVERY FRAME. Stems resolve once into a small cache;
a miss is remembered as a miss, so a typo costs one failed fopen rather than sixty a
second.
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


# ---------------------------------------------------------------- the held-prop table
sub("static int   g_scnPrevSpk=1;   /* who the speaker is answering: the last person to talk */",
    """static int   g_scnPrevSpk=1;   /* who the speaker is answering: the last person to talk */

/* ================= WHAT THEY ARE CARRYING ======================================
 * ⭐ Every piece of this already existed and was pointed somewhere else: gripNode is the
 * real hand (found by name at load, already carrying weapons in the arena modes),
 * drawProp already draws a prop pack at a matrix 550 times a frame, and the set's loader
 * already turns a stem into a loaded pack. None of the three had ever been introduced to
 * the other two inside a scene.
 *
 * ⛔ THE GRIP IS A POINT, NOT A FRAME. The prop hangs AT the hand with the body's own yaw
 * -- right for a mug, a torch or a hammer at rest, wrong for anything whose orientation
 * has to read down a sightline. A deliberate stop: this film needs things CARRIED. */
#define HELD_MAX 8
typedef struct { char stem[32]; int pk; } HeldProp;
static HeldProp g_held[HELD_MAX]; static int g_nHeld=0;
static char  g_castProp[SCN_MAXCAST][32];        /* what each body is holding, by slot */
static float g_castGripW[SCN_MAXCAST][3];        /* their grips, measured, in world space */
static int   g_castGripOK[SCN_MAXCAST];

/* ⛔ RESOLVE ONCE. A miss is remembered AS a miss, so a typo costs one failed fopen
 * instead of sixty a second. Returns an index into g_setPk, or -1. */
static int heldFind(const char*stem){
    if(!stem||!stem[0]||!strcmp(stem,"off")) return -1;
    for(int i=0;i<g_nHeld;i++) if(!strcmp(g_held[i].stem,stem)) return g_held[i].pk;
    if(g_nHeld>=HELD_MAX||g_setNP>=SET_MAXP) return -1;
    char pp[512]; snprintf(pp,sizeof pp,"props/%s.rpack",stem);
    FILE*f=fopen(pp,"rb");
    int idx=-1;
    if(f){ fclose(f); g_setPk[g_setNP]=loadPack(pp); idx=g_setNP++; }
    else printf("[prop] held '%s' NOT FOUND (props/%s.rpack)%c",stem,stem,10);
    snprintf(g_held[g_nHeld].stem,32,"%s",stem); g_held[g_nHeld].pk=idx; g_nHeld++;
    fflush(stdout);
    return idx; }""",
    "the held-prop table")

# ---------------------------------------------------------------- publish the grips
sub("""    if(P->pb[5]>=0){ float hx=W[P->pb[5]][12],hy=W[P->pb[5]][13],hz=W[P->pb[5]][14];
        g_partHeadW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];""",
    """    /* ⭐ HER GRIP, in world space, published exactly like the head mark below it and
     * measured from the same FK. This is what a coffee cup hangs on. */
    { int gb=(P->gripNode>=0)?P->gripNode:P->pb[11];
      if(gb>=0){ ptWorld(W[gb],model,g_castGripW[g_castI]); g_castGripOK[g_castI]=1; } }
    if(P->pb[5]>=0){ float hx=W[P->pb[5]][12],hy=W[P->pb[5]][13],hz=W[P->pb[5]][14];
        g_partHeadW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];""",
    "the partner publishes her grip")

sub("""                g_heroHeadW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];""",
    """                { int gb0=(P->gripNode>=0)?P->gripNode:P->pb[11];
                  if(gb0>=0){ ptWorld(world[gb0],model,g_castGripW[0]); g_castGripOK[0]=1; } }
                g_heroHeadW[0]=model[0]*hx+model[4]*hy+model[8]*hz+model[12];""",
    "the hero publishes her grip")

# ---------------------------------------------------------------- the staging sets them
sub("""                            if(g_cast[2].pk&&S8->p2clip[0]){""",
    """                            /* ⭐ WHAT EACH OF THEM IS CARRYING. Sticky, and `off` clears
                             * it, because a woman has to be able to put a coffee down. */
                            if(S8->hprop[0])  snprintf(g_castProp[0],32,"%s",S8->hprop);
                            if(S8->pprop[0])  snprintf(g_castProp[1],32,"%s",S8->pprop);
                            if(S8->p2prop[0]) snprintf(g_castProp[2],32,"%s",S8->p2prop);
                            if(g_cast[2].pk&&S8->p2clip[0]){""",
    "the staging assigns props")

# ---------------------------------------------------------------- draw them
sub("""static void drawSet(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){
    if(!g_setNI) return;""",
    """/* ⭐ THE THINGS IN THEIR HANDS, drawn after the bodies so the grips are measured. One
 * prop per cast slot, hung at the grip with the body's own yaw. */
static void drawHeld(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){
    glActiveTexture(GL_TEXTURE0);
    glUniform1f(uUse,1.f); glUniform3f(uTint,1.f,1.f,1.f);
    for(int i=0;i<SCN_MAXCAST;i++){
        if(!g_castGripOK[i]||!g_castProp[i][0]) continue;
        int pk=heldFind(g_castProp[i]); if(pk<0) continue;
        float a=(i==0)?g_rvHead:(g_rvHead+g_labPYaw*0.0174533f);
        float ca=cosf(a), sa=sinf(a), e=1.f;
        M4 m; memset(m,0,64);
        m[0]=ca*e; m[2]=-sa*e; m[5]=e; m[8]=sa*e; m[10]=ca*e; m[15]=1.f;
        m[12]=g_castGripW[i][0]; m[13]=g_castGripW[i][1]; m[14]=g_castGripW[i][2];
        drawProp(&g_setPk[pk],m,uModel,uPal,uBind); } }

static void drawSet(GLint uModel,GLint uPal,GLint uBind,GLint uUse,GLint uTint){
    if(!g_setNI) return;""",
    "drawHeld")

sub("""                if(g_mode==MODE_ANIM&&g_setNA){
                    actorsDraw(mModel,mPal,mBind,pal,g_labUse,g_labTint);
                    leadsCollect(); }""",
    """                if(g_mode==MODE_ANIM&&g_setNA){
                    actorsDraw(mModel,mPal,mBind,pal,g_labUse,g_labTint);
                    leadsCollect(); }
                if(g_mode==MODE_ANIM&&g_scnN>0)
                    drawHeld(mModel,mPal,mBind,g_labUse,g_labTint);""",
    "draw the held props each frame")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
