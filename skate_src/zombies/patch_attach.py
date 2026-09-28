"""patch_attach.py -- CAPABILITY D: a prop can hang off a bone, not just the hand.

Held props were drawn at the GRIP, which is one bone. A hat, a bag over a shoulder, a
board carried against the chest, a torch clipped to a belt -- none of those had a
mechanism, and the leash between reva and kaeli is a hand-built special case that reads
two named bones directly because nothing general existed.

⭐ FOUR ATTACH POINTS, published from the same FK that already publishes the grip:

    hand   the grip node -- the real hand, found by name at load   (the default)
    head   canon 5
    chest  the top spine bone that resolved
    hip    canon 0

    p2prop=v1_x_torch@1.0@0@hand
    pprop=v1_x_board@1.0@90@chest

⛔ FOUR, NOT ARBITRARY. Publishing every bone's world matrix per body per frame to make one
of them addressable is a lot of copying for a feature whose whole use is "on a person
somewhere obvious". These four are the ones a prop actually hangs from. If a fifth is ever
needed it is one line, and that is a better trade than a general mechanism nobody uses.
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


sub("static float g_castGripM[SCN_MAXCAST][16];",
    """static float g_castGripM[SCN_MAXCAST][16];
/* ⭐ AND THREE MORE PLACES A PROP CAN HANG FROM. Published from the same FK as the grip.
 * FOUR, not arbitrary: copying every bone's world matrix per body per frame to make one
 * addressable is a lot of work for a feature whose whole use is "on a person somewhere
 * obvious", and these are the four things actually hang off. A fifth is one line. */
enum { ATT_HAND=0, ATT_HEAD, ATT_CHEST, ATT_HIP, ATT_N };
static float g_castAttM[SCN_MAXCAST][ATT_N][16];
static int   g_castAttOK[SCN_MAXCAST][ATT_N];
static int attOf(const char*nm){
    if(!nm||!nm[0]) return ATT_HAND;
    if(!strcmp(nm,"head"))  return ATT_HEAD;
    if(!strcmp(nm,"chest")) return ATT_CHEST;
    if(!strcmp(nm,"hip"))   return ATT_HIP;
    return ATT_HAND; }
/* publish all four for whichever body is being drawn */
static void attPublish(Pack*P9,M4*W9,const float*model,int slot){
    int chest=-1; for(int c=3;c>=1;c--) if(P9->pb[c]>=0){ chest=P9->pb[c]; break; }
    int nodes[ATT_N]={ (P9->gripNode>=0)?P9->gripNode:P9->pb[11], P9->pb[5], chest, P9->pb[0] };
    for(int k=0;k<ATT_N;k++){
        g_castAttOK[slot][k]=0;
        if(nodes[k]<0) continue;
        m4_mul(g_castAttM[slot][k],(float*)model,W9[nodes[k]]);
        g_castAttOK[slot][k]=1; } }""",
    "the attach points")

sub("""      if(gb>=0){ ptWorld(W[gb],model,g_castGripW[g_castI]); g_castGripOK[g_castI]=1;
                 m4_mul(g_castGripM[g_castI],(float*)model,W[gb]); } }""",
    """      if(gb>=0){ ptWorld(W[gb],model,g_castGripW[g_castI]); g_castGripOK[g_castI]=1;
                 m4_mul(g_castGripM[g_castI],(float*)model,W[gb]); }
      attPublish(P,W,model,g_castI); }""",
    "the partner publishes all four")

sub("""                  if(gb0>=0){ ptWorld(world[gb0],model,g_castGripW[0]); g_castGripOK[0]=1;
                              m4_mul(g_castGripM[0],(float*)model,world[gb0]); } }""",
    """                  if(gb0>=0){ ptWorld(world[gb0],model,g_castGripW[0]); g_castGripOK[0]=1;
                              m4_mul(g_castGripM[0],(float*)model,world[gb0]); }
                  attPublish(P,world,model,0); }""",
    "the hero publishes all four")

sub("""        char stem9[32]; float hs=0.30f, hp=0.f;
        { const char*at=strchr(g_castProp[i],'@');
          if(at){ int n=(int)(at-g_castProp[i]); if(n>31) n=31;
                  memcpy(stem9,g_castProp[i],n); stem9[n]=0; hs=(float)atof(at+1);
                  const char*at2=strchr(at+1,'@'); if(at2) hp=(float)atof(at2+1)*0.0174533f; }
          else snprintf(stem9,32,"%s",g_castProp[i]); }""",
    """        /* stem@scale@pitch@where -- `where` is hand (default), head, chest or hip. */
        char stem9[32], where9[16]={0}; float hs=0.30f, hp=0.f;
        { const char*at=strchr(g_castProp[i],'@');
          if(at){ int n=(int)(at-g_castProp[i]); if(n>31) n=31;
                  memcpy(stem9,g_castProp[i],n); stem9[n]=0; hs=(float)atof(at+1);
                  const char*at2=strchr(at+1,'@');
                  if(at2){ hp=(float)atof(at2+1)*0.0174533f;
                           const char*at3=strchr(at2+1,'@');
                           if(at3){ int k=0; at3++;
                             while(*at3&&*at3!='@'&&k<15) where9[k++]=*at3++; where9[k]=0; } } }
          else snprintf(stem9,32,"%s",g_castProp[i]); }
        int att9=attOf(where9);
        if(!g_castAttOK[i][att9]) att9=ATT_HAND;
        if(!g_castAttOK[i][att9]) continue;""",
    "parse the attach point")

sub("""        M4 g; memcpy(g,g_castGripM[i],64);""",
    """        M4 g; memcpy(g,g_castAttM[i][att9],64);""",
    "hang it off the chosen bone")

sub("""        m[12]=g_castGripW[i][0]; m[13]=g_castGripW[i][1]; m[14]=g_castGripW[i][2];
        drawProp(&g_setPk[pk],m,uModel,uPal,uBind); } }""",
    """        m[12]=g_castAttM[i][att9][12];
        m[13]=g_castAttM[i][att9][13];
        m[14]=g_castAttM[i][att9][14];
        drawProp(&g_setPk[pk],m,uModel,uPal,uBind); } }""",
    "and place it there")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
