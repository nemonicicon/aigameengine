"""patch_cast5.py -- THE CAPTION KNOWS THREE PEOPLE TOO.

The scene system takes three bodies now, and every one of them can speak -- but the
subtitle plumbing was still a two-hander in three separate places:

  * `static char g_scnWho[2][16]` -- room for two names.
  * `sscanf(ln,"cast %15s %15s", ...)` -- reads two names off the .scn.
  * `const char*who = rv9 ? g_scnWho[0] : g_scnWho[1];` -- and picks between two.

So slot 2's dialogue would have come up over KAELI's name, which is the exact bug that
already happened once when susan's lines were captioned as kaeli's. It is worse now,
because in act V the wrong name over "...Tell me what." would make the ending unreadable.

⭐ AND THE COLOUR PICKS BY SLOT, NOT BY "IS IT REVA". Three speakers need three colours:
reva cyan (she has been cyan since CLUB NIGHT), kaeli warm, susan green. The audience
tracks who is talking in a dark room by hue before they read a word of it.
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


sub('static char  g_scnWho[2][16]={"REVA","KAELI"};',
    'static char  g_scnWho[SCN_MAXCAST][16]={"REVA","KAELI","SUSAN","-"};',
    "room for the whole cast")

sub('            snprintf(g_scnWho[0],16,"REVA"); snprintf(g_scnWho[1],16,"KAELI"); }',
    '            snprintf(g_scnWho[0],16,"REVA"); snprintf(g_scnWho[1],16,"KAELI");\n'
    '            snprintf(g_scnWho[2],16,"SUSAN"); snprintf(g_scnWho[3],16,"-"); }',
    "reset all four")

sub('        else if(!strncmp(ln,"cast ",5)) sscanf(ln,"cast %15s %15s",g_scnWho[0],g_scnWho[1]);',
    '        else if(!strncmp(ln,"cast ",5))\n'
    '            sscanf(ln,"cast %15s %15s %15s %15s",g_scnWho[0],g_scnWho[1],g_scnWho[2],g_scnWho[3]);',
    "read up to four names")

sub("""                  int rv9=(g_scnL[li9].spk==0);
                  drawText(x9+2.f,y9+2.f,sc9,tx,0.f,0.f,0.f,0.75f);
                  if(rv9) drawText(x9,y9,sc9,tx,0.55f,0.95f,1.0f,0.98f);      /* reva: cyan */
                  else    drawText(x9,y9,sc9,tx,1.0f,0.78f,0.42f,0.98f);      /* kaeli: warm */
                  const char*who=rv9?g_scnWho[0]:g_scnWho[1];
                  drawText(x9,y9-30.f,1.7f,who,rv9?0.35f:0.85f,rv9?0.75f:0.55f,rv9?0.85f:0.30f,0.85f); } }""",
    """                  /* ⭐ BY SLOT, NOT BY "IS IT REVA". Three speakers need three colours --
                   * the audience tracks who is talking in a dark room by hue before they
                   * read a word of it. reva cyan (hers since CLUB NIGHT), kaeli warm,
                   * susan green. ⛔ And the NAME is indexed too: slot 2's dialogue used to
                   * come up over kaeli's name, which is the exact bug that already
                   * happened once to susan and would make act V unreadable. */
                  int sp9=g_scnL[li9].spk; if(sp9<0||sp9>=SCN_MAXCAST) sp9=0;
                  static const float SPKC[SCN_MAXCAST][3]={
                      {0.55f,0.95f,1.00f},   /* reva  -- cyan */
                      {1.00f,0.78f,0.42f},   /* kaeli -- warm */
                      {0.62f,0.94f,0.58f},   /* susan -- green */
                      {0.86f,0.86f,0.90f}};  /* anyone else */
                  const float*cc9=SPKC[sp9];
                  drawText(x9+2.f,y9+2.f,sc9,tx,0.f,0.f,0.f,0.75f);
                  drawText(x9,y9,sc9,tx,cc9[0],cc9[1],cc9[2],0.98f);
                  drawText(x9,y9-30.f,1.7f,g_scnWho[sp9],cc9[0]*0.62f,cc9[1]*0.72f,cc9[2]*0.70f,0.85f); } }""",
    "caption colour and name by slot")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")

# ---------------------------------------------------------------- the compiler
P2 = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes\compile_scene.py"
t = io.open(P2, encoding="utf-8").read()
a = '''        f.write("cast %s %s\\n" % (cast_names.get("0", "REVA"), cast_names.get("1", "KAELI")))'''
if t.count(a) != 1:
    print("!! compile_scene cast line x%d" % t.count(a))
    sys.exit(1)
b = '''        # ⭐ UP TO FOUR NAMES. The engine captions by SLOT now, so a third speaker's
        # dialogue no longer comes up over the second speaker's name.
        f.write("cast %s\\n" % " ".join(
            cast_names.get(str(i), ("REVA", "KAELI", "SUSAN", "-")[i]) for i in range(4)))'''
io.open(P2, "w", encoding="utf-8").write(t.replace(a, b))
print("compile_scene.py: four cast names")
