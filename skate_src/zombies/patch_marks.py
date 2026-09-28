"""patch_marks.py -- CAPABILITY 1: A BODY CAN BE PUT ON A PROP.

jacob: "well obviously that capability needs to be added to our engine here."

⛔ THE GAP. Every body in the club stands on the ground plane. The scene pair is placed by
`stage=` at y=0 and a partner is placed relative to the hero's matrix, also at y=0. So a
chair is a thing you stand next to and a bed is a thing you lie THROUGH. Act V has susan
sitting in a chair that is not under her, and the cold open had to sink a bed frame into
the floorboards until its mattress met the woman lying on them.

⭐ THE FIX IS A NAMED MARK IN THE SET, which is what a level format wants anyway:

    mark <name> x y z yaw          # in a .set, beside the prop it belongs to
    pmark=<name>  p2mark=<name>    # in a scene line: put THAT body on THAT mark

A mark carries a HEIGHT, so `mark seat_susan 3.10 0.62 4.75 34` is the seat of the
armchair at 3.10,4.75 -- and a body sent there sits ON it, not beside it. The mark also
carries a yaw, because a chair faces a direction and a body in it has no choice.

⛔ AND A MARK OVERRIDES THE PAIR RELATION, WHICH IS THE POINT. A partner is normally placed
from the hero's matrix by gap/side/yaw -- that is what makes a two-hander read as a
conversation. A body on a mark is placed in the WORLD instead: it is furniture-relative,
not person-relative, and the two cannot both be true. Clearing the mark (`pmark=off`) hands
her back to the pair relation.

⛔ SLOTS 1..3 ONLY, deliberately. The hero carries a whole movement system -- paths, the
stage walk, foot planting, the state tree -- and putting her on a mark means fighting all
of it. Both cases this film actually needs (kaeli in a bed, susan in a chair) are partners.
Written down rather than discovered later.
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


# ---------------------------------------------------------------- the mark table
sub("typedef struct { int pk; float m[16]; float tint[3]; int tinted; } SetInst;",
    """/* ⭐ A NAMED MARK IN THE WORLD -- `mark <name> x y z yaw` in a .set. It carries a
 * HEIGHT, which is the whole point: a body sent to a mark sits ON the chair rather than
 * beside it, and lies ON the bed rather than through it. It carries a YAW too, because a
 * chair faces a direction and a body in it has no choice about that. */
#define SET_MAXMARK 24
typedef struct { char name[32]; float x,y,z,yaw; } SetMark;
static SetMark g_setMk[SET_MAXMARK]; static int g_setNMk=0;
static SetMark* markFind(const char*nm){
    if(!nm||!nm[0]||!strcmp(nm,"off")) return NULL;
    for(int i=0;i<g_setNMk;i++) if(!strcmp(g_setMk[i].name,nm)) return &g_setMk[i];
    return NULL; }

typedef struct { int pk; float m[16]; float tint[3]; int tinted; } SetInst;""",
    "the mark table")

# ---------------------------------------------------------------- parse `mark`
sub("""        if(!strncmp(ln,"leash",5)&&g_setNA>0){ int li=-1;""",
    """        if(!strncmp(ln,"mark ",5)&&g_setNMk<SET_MAXMARK){
            SetMark*M=&g_setMk[g_setNMk];
            if(sscanf(ln,"mark %31s %f %f %f %f",M->name,&M->x,&M->y,&M->z,&M->yaw)==5){
                printf("[set] mark %s at %.2f,%.2f,%.2f yaw %.0f%c",M->name,M->x,M->y,M->z,M->yaw,10);
                g_setNMk++; }
            continue; }
        if(!strncmp(ln,"leash",5)&&g_setNA>0){ int li=-1;""",
    "parse mark lines")

sub("""    g_setNI=0; g_setNA=0; g_setCur[0]=0;""",
    """    g_setNI=0; g_setNA=0; g_setNMk=0; g_setCur[0]=0;""",
    "clear marks with the set")

# ---------------------------------------------------------------- the scene keys
sub("""                 char hprop[32], pprop[32], p2prop[32];""",
    """                 char hprop[32], pprop[32], p2prop[32];
                 /* ⭐ ON A PROP. A named mark in the set, which overrides the pair
                  * relation for that body -- furniture-relative instead of
                  * person-relative, and the two cannot both be true. */
                 char pmark[32], p2mark[32];""",
    "ScnLine carries marks")

sub("""                L->hprop[0]=L->pprop[0]=L->p2prop[0]=0;""",
    """                L->hprop[0]=L->pprop[0]=L->p2prop[0]=0;
                L->pmark[0]=L->p2mark[0]=0;""",
    "mark defaults")

sub("""            if((q=strstr(ln,"p2prop="))){ int k=0; q+=7;""",
    """            /* ⛔ `pmark=` matches inside `p2mark=` -- the pgap/pclip/pprop trap again. */
            { q=strstr(ln,"pmark="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pmark=");
              if(q){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\\n'&&*q!='\\r'&&k<31) L->pmark[k++]=*q++; L->pmark[k]=0; } }
            if((q=strstr(ln,"p2mark="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\\n'&&*q!='\\r'&&k<31) L->p2mark[k++]=*q++; L->p2mark[k]=0; }
            if((q=strstr(ln,"p2prop="))){ int k=0; q+=7;""",
    "parse the mark keys")

# ---------------------------------------------------------------- staging assigns them
sub("""                            if(S8->hprop[0])  snprintf(g_castProp[0],32,"%s",S8->hprop);""",
    """                            if(S8->pmark[0])  snprintf(g_castMark[1],32,"%s",S8->pmark);
                            if(S8->p2mark[0]) snprintf(g_castMark[2],32,"%s",S8->p2mark);
                            if(S8->hprop[0])  snprintf(g_castProp[0],32,"%s",S8->hprop);""",
    "staging assigns marks")

sub("""static char  g_castProp[SCN_MAXCAST][32];        /* what each body is holding, by slot */""",
    """static char  g_castProp[SCN_MAXCAST][32];        /* what each body is holding, by slot */
static char  g_castMark[SCN_MAXCAST][32];        /* ...and which set mark she is ON, if any */""",
    "the per-slot mark")

# ---------------------------------------------------------------- and the placement
sub("""    model[12]=heroModel[12]+sinf(g_rvHead)*g_labPGap+cosf(g_rvHead)*g_labPSide;
    model[14]=heroModel[14]+cosf(g_rvHead)*g_labPGap-sinf(g_rvHead)*g_labPSide;
    model[13]=-P->mny*sc;""",
    """    model[12]=heroModel[12]+sinf(g_rvHead)*g_labPGap+cosf(g_rvHead)*g_labPSide;
    model[14]=heroModel[14]+cosf(g_rvHead)*g_labPGap-sinf(g_rvHead)*g_labPSide;
    model[13]=-P->mny*sc;
    /* ⭐ UNLESS SHE IS ON A MARK. A body on a chair is placed by the FURNITURE, not by the
     * person she is talking to -- world position, world yaw, and a HEIGHT, which is the
     * thing the ground plane could never give her. This is what lets susan sit in the
     * chair the script has put her in for two drafts, and kaeli lie on a bed instead of
     * through it. `pmark=off` hands her back to the pair relation. */
    { SetMark*MK=markFind(g_castMark[g_castI]);
      if(MK){ float ma=MK->yaw*0.0174533f, mc=cosf(ma), ms=sinf(ma);
              g_partYaw=ma;
              memset(model,0,64);
              model[0]=mc*sc; model[2]=-ms*sc; model[5]=sc; model[8]=ms*sc; model[10]=mc*sc; model[15]=1.f;
              model[12]=MK->x; model[14]=MK->z; model[13]=MK->y-P->mny*sc; } }""",
    "a marked body is placed by the furniture")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
