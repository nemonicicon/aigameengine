"""patch_cast3.py -- stage three: THE CAMERA KNOWS THE WHOLE CAST, and the script can block it.

Stage two put a third body on screen. It could stand there and nothing else: the shot
solver framed slots 0 and 1 by name, so the film could not cut to her, and the staging had
no keys for her, so she stood on her load-time mark for the whole scene.

⭐ THE MARKS BECOME AN INDEX LOOKUP. castHead(i) / castEye(i) / castHeadOK(i) -- slot 0 is
the hero (her marks live in g_heroHeadW, not in the array, because she is drawn by the main
path), everyone else is g_cast[i]. Four accessors, and the solver stops naming names.

⭐ AND THE LISTENER IS THE PREVIOUS SPEAKER. With two people the listener was "the other
one" and needed no thought. With three it is a real question, and the answer that needs no
authoring is: whoever last spoke. That is what an over-the-shoulder is FOR -- it puts the
person being answered in the foreground -- and it falls out of the script for free.

⛔ AND THE EYELINE RULE HAS TO SURVIVE IT. The old side was `spk==0 ? +1 : -1`, which is
only meaningful for one pair. Keyed on the ORDER of the pair instead -- lower index on one
side, higher on the other -- every pair keeps its own consistent axis and no cut crosses
the line, which is the whole grammar the two-body version was written to protect.

Per-slot staging: p2gap / p2yaw / p2side / p2clip / p2look / p2brow. Additive, so nothing
that exists changes, and they are STICKY like every other stage key.
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


# ---------------------------------------------------------------- 1. the accessors
sub("static int g_heroHeadOK=0;",
    """static int g_heroHeadOK=0;
/* ⭐ THE CAST'S MARKS, BY INDEX. Slot 0 is the HERO and is deliberately not in the array:
 * she is drawn by the main path and her marks live in g_heroHeadW. Everyone else is
 * g_cast[i], measured by her own draw. These four are what let the shot solver stop
 * naming names -- with two bodies it could say "hero or partner"; with three it cannot. */
static const float* castHead(int i){ return (i<=0)?g_heroHeadW:g_cast[i].headW; }
static const float* castEye (int i){ return (i<=0)?g_heroEyeW :g_cast[i].eyeW ; }
static int   castHeadOK(int i){ return (i<=0)?g_heroHeadOK:g_cast[i].headOK; }
static int   castFaceOK(int i){ return (i<=0)?g_heroFaceOK:g_cast[i].faceOK; }
static float castIOD   (int i){ return (i<=0)?g_heroIOD   :g_cast[i].iod;    }
static int   g_scnPrevSpk=1;   /* who the speaker is answering: the last person to talk */""",
    "the cast mark accessors")

# ---------------------------------------------------------------- 2. the solver
sub("""            const float*SH=(L9->spk==0)?g_heroHeadW:g_partHeadW;   /* speaker */
            const float*LH=(L9->spk==0)?g_partHeadW:g_heroHeadW;   /* listener */""",
    """            /* ⭐ SPEAKER AND LISTENER BY CAST INDEX. The listener is whoever spoke last
             * -- with two people that was "the other one" and needed no thought; with
             * three it is a real question and this is the answer that needs no authoring.
             * It is also what an over-the-shoulder is for: the person being ANSWERED goes
             * in the foreground. */
            int spk9=L9->spk; if(spk9<0||spk9>=SCN_MAXCAST) spk9=0;
            int lis9=g_scnPrevSpk; if(lis9==spk9||lis9<0||lis9>=SCN_MAXCAST) lis9=(spk9==0)?1:0;
            if(!castHeadOK(lis9)) lis9=(spk9==0)?1:0;
            const float*SH=castHead(spk9);   /* speaker */
            const float*LH=castHead(lis9);   /* listener */""",
    "solver picks by index")

sub("""            float sd9=(L9->spk==0)?1.f:-1.f;""",
    """            /* ⛔ THE EYELINE MUST SURVIVE A THIRD PERSON. `spk==0 ? +1 : -1` is only
             * meaningful for one pair. Keyed on the ORDER of the pair instead, every pair
             * keeps its own consistent axis and no cut crosses the line -- which is the
             * grammar the two-body version was written to protect. */
            float sd9=(spk9<lis9)?1.f:-1.f;""",
    "eyeline rule generalised")

sub("""            const float*SE=(g_heroFaceOK&&g_partFaceOK)?((L9->spk==0)?g_heroEyeW:g_partEyeW):SH;
            const float*LE=(g_heroFaceOK&&g_partFaceOK)?((L9->spk==0)?g_partEyeW:g_heroEyeW):LH;""",
    """            int fok9=(castFaceOK(spk9)&&castFaceOK(lis9));
            const float*SE=fok9?castEye(spk9):SH;
            const float*LE=fok9?castEye(lis9):LH;""",
    "eye marks by index")

sub("""            float sio9=(L9->spk==0)?g_heroIOD:g_partIOD;""",
    """            float sio9=castIOD(spk9);""",
    "interocular by index")

# the gate: the two people this SHOT needs, not "hero and partner"
sub("""        if(g_scnT>=0.f&&g_scnN>0&&g_heroHeadOK&&g_partHeadOK){
            static int held9=0; int li9=scnActive(); if(li9>=0) held9=li9;""",
    """        if(g_scnT>=0.f&&g_scnN>0&&castHeadOK(0)&&castHeadOK(1)){
            static int held9=0; int li9=scnActive(); if(li9>=0) held9=li9;""",
    "solver gate by accessor")

# ---------------------------------------------------------------- 3. per-slot staging
sub("""                 float hx,hz; int hasMark;   /* the pair's world mark for this line */""",
    """                 float hx,hz; int hasMark;   /* the pair's world mark for this line */
                 /* slot 2's blocking. Additive: nothing that exists changes, and they are
                  * STICKY like every other stage key, so a scene sets them once. */
                 float p2gap,p2yaw,p2side,p2brow; int p2look, p2has; char p2clip[32];""",
    "ScnLine carries slot 2")

sub("""                L->pgap=1.15f; L->pyaw=180.f; L->pside=0.12f;""",
    """                L->pgap=1.15f; L->pyaw=180.f; L->pside=0.12f;
                L->p2gap=1.45f; L->p2yaw=214.f; L->p2side=-1.25f;
                L->p2look=0; L->p2brow=0.f; L->p2has=0; L->p2clip[0]=0;""",
    "slot 2 defaults")

sub("""            if((q=strstr(ln,"pgap=")))  L->pgap =(float)atof(q+5);""",
    """            if((q=strstr(ln,"p2gap=")))  { L->p2gap =(float)atof(q+6); L->p2has=1; }
            if((q=strstr(ln,"p2yaw=")))  { L->p2yaw =(float)atof(q+6); L->p2has=1; }
            if((q=strstr(ln,"p2side="))) { L->p2side=(float)atof(q+7); L->p2has=1; }
            if((q=strstr(ln,"p2look=")))   L->p2look=atoi(q+7);
            if((q=strstr(ln,"p2brow=")))   L->p2brow=(float)atof(q+7);
            if((q=strstr(ln,"p2clip="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\\n'&&*q!='\\r'&&k<31) L->p2clip[k++]=*q++; L->p2clip[k]=0; }
            /* ⛔ `pgap=` matches inside `p2gap=`. Skip any hit whose preceding character is
             * a digit -- the same trap that let `pclip=` set the HERO's clip. */
            { q=strstr(ln,"pgap="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pgap=");
              if(q) L->pgap=(float)atof(q+5); }""",
    "parse slot 2 keys")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched (stage 3)")
