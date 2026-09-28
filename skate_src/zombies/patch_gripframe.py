"""patch_gripframe.py -- CAPABILITY 2: THE GRIP IS A FRAME, NOT A POINT.

Held props hung AT the hand with the BODY's yaw. That is fine for a mug sitting in a fist
and wrong for everything else: a torch cannot point, a hammer cannot swing, a bat cannot
be shouldered, and nothing rotates when the wrist does. It also meant a prop stayed level
while the arm that "held" it moved through a whole clip.

⭐ AND THE FIX WAS ALREADY MEASURED. The grip node's full world matrix falls out of the
same FK that already published the grip POSITION -- the rotation was being thrown away one
line after it was computed. Publishing the whole 4x4 instead of three floats is the entire
change, and the prop now inherits the hand's orientation for free: it swings with the arm,
turns with the wrist, and follows any clip without anything being authored.

⛔ THE HAND'S AXES ARE NOT THE PROP'S AXES. A UE forearm points down the bone; a torch
points out of the fist. `stem@scale@pitch` lets a prop be turned into the hand once, in the
scene file, rather than every asset being re-baked to a convention nobody wrote down.
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


sub("static float g_castGripW[SCN_MAXCAST][3];        /* their grips, measured, in world space */",
    """static float g_castGripW[SCN_MAXCAST][3];        /* their grips, measured, in world space */
/* ⭐ AND THE WHOLE FRAME, not just the point. The rotation fell out of the same FK and was
 * being thrown away one line later -- so a torch could not point and a hammer could not
 * swing. With the basis, a held prop inherits the hand: it turns with the wrist and
 * follows any clip with nothing authored. */
static float g_castGripM[SCN_MAXCAST][16];""",
    "the grip matrix")

sub("""    { int gb=(P->gripNode>=0)?P->gripNode:P->pb[11];
      if(gb>=0){ ptWorld(W[gb],model,g_castGripW[g_castI]); g_castGripOK[g_castI]=1; } }""",
    """    { int gb=(P->gripNode>=0)?P->gripNode:P->pb[11];
      if(gb>=0){ ptWorld(W[gb],model,g_castGripW[g_castI]); g_castGripOK[g_castI]=1;
                 m4_mul(g_castGripM[g_castI],(float*)model,W[gb]); } }""",
    "partner publishes the frame")

sub("""                { int gb0=(P->gripNode>=0)?P->gripNode:P->pb[11];
                  if(gb0>=0){ ptWorld(world[gb0],model,g_castGripW[0]); g_castGripOK[0]=1; } }""",
    """                { int gb0=(P->gripNode>=0)?P->gripNode:P->pb[11];
                  if(gb0>=0){ ptWorld(world[gb0],model,g_castGripW[0]); g_castGripOK[0]=1;
                              m4_mul(g_castGripM[0],(float*)model,world[gb0]); } }""",
    "hero publishes the frame")

sub("""        char stem9[32]; float hs=0.30f;
        { const char*at=strchr(g_castProp[i],'@');
          if(at){ int n=(int)(at-g_castProp[i]); if(n>31) n=31;
                  memcpy(stem9,g_castProp[i],n); stem9[n]=0; hs=(float)atof(at+1); }
          else snprintf(stem9,32,"%s",g_castProp[i]); }
        if(hs<0.01f) hs=0.30f;
        int pk=heldFind(stem9); if(pk<0) continue;
        float a=(i==0)?g_rvHead:(g_rvHead+g_labPYaw*0.0174533f);
        float ca=cosf(a), sa=sinf(a), e=hs;
        M4 m; memset(m,0,64);
        m[0]=ca*e; m[2]=-sa*e; m[5]=e; m[8]=sa*e; m[10]=ca*e; m[15]=1.f;
        m[12]=g_castGripW[i][0]; m[13]=g_castGripW[i][1]; m[14]=g_castGripW[i][2];
        drawProp(&g_setPk[pk],m,uModel,uPal,uBind); } }""",
    """        /* stem@scale@pitch -- ⛔ THE HAND'S AXES ARE NOT THE PROP'S. A UE forearm runs
         * down the bone and a torch points out of the fist, so a prop gets turned into the
         * hand here, once, in the scene file, rather than every asset being re-baked to a
         * convention nobody ever wrote down. */
        char stem9[32]; float hs=0.30f, hp=0.f;
        { const char*at=strchr(g_castProp[i],'@');
          if(at){ int n=(int)(at-g_castProp[i]); if(n>31) n=31;
                  memcpy(stem9,g_castProp[i],n); stem9[n]=0; hs=(float)atof(at+1);
                  const char*at2=strchr(at+1,'@'); if(at2) hp=(float)atof(at2+1)*0.0174533f; }
          else snprintf(stem9,32,"%s",g_castProp[i]); }
        if(hs<0.01f) hs=0.30f;
        int pk=heldFind(stem9); if(pk<0) continue;
        /* ⭐ THE HAND'S OWN FRAME -- ⛔ NORMALISED FIRST. g_castGripM is model * W[bone] and
         * the model carries the RIG's scale, which on kmoves is 0.018. Multiplying the
         * held size on top of that would have shrunk every prop by the body's scale
         * factor and left a torch the size of a grain of rice. Take the basis, drop its
         * length, then apply the held scale. */
        M4 g; memcpy(g,g_castGripM[i],64);
        for(int c9=0;c9<3;c9++){
            float*col=g+c9*4;
            float L=sqrtf(col[0]*col[0]+col[1]*col[1]+col[2]*col[2]);
            if(L>1e-6f){ col[0]/=L; col[1]/=L; col[2]/=L; } }
        M4 rot; { float cp=cosf(hp), sp=sinf(hp);
          memset(rot,0,64);
          rot[0]=hs; rot[5]=cp*hs; rot[6]=sp*hs; rot[9]=-sp*hs; rot[10]=cp*hs; rot[15]=1.f; }
        M4 m; m4_mul(m,g,rot);
        m[12]=g_castGripW[i][0]; m[13]=g_castGripW[i][1]; m[14]=g_castGripW[i][2];
        drawProp(&g_setPk[pk],m,uModel,uPal,uBind); } }""",
    "held props inherit the hand")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
