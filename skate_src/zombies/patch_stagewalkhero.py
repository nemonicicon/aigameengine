"""patch_stagewalkhero.py -- the pair can WALK somewhere during a scene.

jacob: "characters are seemingly unable to move".

Fair. Only set ACTORS ever went anywhere; the conversation pair stood on one mark for a
whole take, because `stage=` was a single static point and the only thing that moved was
the partner's small `pgap` ease. So a scene line can now carry a MARK of its own:

    0 rate=0.98 hx=34 hz=-17 shot=two|Something is happening at fourteen.

`hx`/`hz` move the pair's world mark, easing over ~1.4s. The partner follows the hero's
matrix as she always has, and she already walks when her mark moves; the hero gets the
same treatment here, through the SAME hook she already uses to borrow a run -- set hdP/hdC
to the bank's walk and let gaitRate() pick the stride for the measured speed. Nobody
slides, because the stride comes from the speed rather than from a guess.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
BS_N = chr(92) + "n"


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    was = CRLF in s
    return (s.replace(CRLF, LF) if was else s), was


def save(p, s, was):
    io.open(p, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)


def sub(s, a, b, why):
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    print("   ok: %s" % why)
    return s.replace(a, b)


P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
s, was = load(P)

# ---- the line's own mark, and the hero's measured speed
s = sub(s, "                 float pgap,pyaw,pside; int hlook,plook,shot;",
        "                 float pgap,pyaw,pside; int hlook,plook,shot;\n"
        "                 float hx,hz; int hasMark;   /* the pair's world mark for this line */",
        "ScnLine carries a mark")

s = sub(s, "static float g_stage[3]={0,0,0}; static int g_stageOn=0;",
        "static float g_stage[3]={0,0,0}; static int g_stageOn=0;\n"
        "/* how fast the stage mark is carrying the pair this frame, so the hero can put a\n"
        " * gait under it instead of gliding. The partner already had this. */\n"
        "static float g_heroStageSpd=0.f;",
        "hero stage speed global")

s = sub(s, '            if((q=strstr(ln,"pgap=")))  L->pgap =(float)atof(q+5);',
        '            if((q=strstr(ln,"pgap=")))  L->pgap =(float)atof(q+5);\n'
        '            if((q=strstr(ln,"hx=")))  { L->hx=(float)atof(q+3); L->hasMark|=1; }\n'
        '            if((q=strstr(ln,"hz=")))  { L->hz=(float)atof(q+3); L->hasMark|=2; }',
        "parse hx/hz")

# ---- ease the mark and measure the speed, beside the partner's ease
s = sub(s, """                          /* HER SPEED, from how far the marks actually moved her. */""",
        """                          /* ⭐ AND THE PAIR'S OWN MARK. A line may carry hx/hz; the stage
                           * eases toward it and the hero walks the distance. */
                          if(g_stageOn&&S8->hasMark){
                              float tx9=(S8->hasMark&1)?S8->hx:g_stage[0];
                              float tz9=(S8->hasMark&2)?S8->hz:g_stage[1];
                              float kk=1.f-expf(-st8/1.40f);
                              float ox=g_stage[0], oz=g_stage[1];
                              g_stage[0]+=(tx9-g_stage[0])*kk;
                              g_stage[1]+=(tz9-g_stage[1])*kk;
                              if(st8>1e-5f){ float dx9=g_stage[0]-ox, dz9=g_stage[1]-oz;
                                  float v9=sqrtf(dx9*dx9+dz9*dz9)/st8;
                                  g_heroStageSpd+=(v9-g_heroStageSpd)*(1.f-expf(-st8/0.12f)); } }
                          else g_heroStageSpd*=expf(-st8/0.20f);
                          /* HER SPEED, from how far the marks actually moved her. */""",
        "ease the pair's mark and measure the hero's speed")

# ---- the hero puts a gait under it, through the hook she already uses for a run
s = sub(s, """            { int key=(hdC>=0)?(9000+hdC):want; if(key!=rvCM){ rvCM=key; rvXf=pInit?0.f:1.f; } }""",
        """            /* ⭐ A GAIT UNDER THE STAGE MOVE. Same hook the hero already uses to borrow
             * a run she does not own: point hdP/hdC at the bank's walk and let gaitRate()
             * pick the stride for the speed the mark is actually carrying her at. */
            if(g_stageOn&&g_heroStageSpd>0.22f&&P->cbOK&&P->pbOK>=10){
                int bc9=-1; Pack*bp9=bankFind("w_walk",&bc9);
                if(bp9){ hdP=bp9; hdC=bc9; want=-1;
                         rate=gaitRate(bp9,bc9,g_heroStageSpd); } }
            { int key=(hdC>=0)?(9000+hdC):want; if(key!=rvCM){ rvCM=key; rvXf=pInit?0.f:1.f; } }""",
        "the hero walks her mark")
save(P, s, was)
print("crowd_kms.c patched")

# ---- the compiler has to pass hx/hz through
P2 = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes\compile_scene.py"
t, was2 = load(P2)
t = sub(t, '              shot="ots", hclip="auto", pclip="auto",',
        '              shot="ots", hclip="auto", pclip="auto", hx="", hz="",',
        "STAGE0 knows the mark keys")
save(P2, t, was2)
print("compile_scene.py patched")
