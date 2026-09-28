"""patch_convo.py -- the one who is TALKING talks with her hands.

jacob: "reva is just standing there, she needs to be more expressive. talking with her
arms etc." and "kaeilis idle animation is dumb, shes getting up from a kneed position over
and over again, i think you have a good idea what good animation does and this is not it."

Both are one mistake. I replaced the default clip `z_upright` (8 frames -- a static pose)
with `idle` WITHOUT LOOKING AT WHAT `idle` IS on each rig. Rendered as a strip, kaeli's
`idle` is a body rising off the floor and settling, over and over: a get-up loop, not a
standing idle. Picking a clip by its NAME is the same class of error as trusting a green
regression -- it is not looking.

What the banks actually hold, looked at:

    idle        (kaeli's own)  a get-up-off-the-floor loop. Never a standing idle.
    x_fidget1/2 (mob1)         standing, weight shifting, hands moving. A real idle.
    x_convo1    (mob1, 882f)   standing and GESTURING. This is a person talking.
    x_convo2    (mob1, 746f)   standing and listening.

So a scene now casts by ROLE rather than by a fixed name: `auto` means the SPEAKER plays
x_convo1 and the LISTENER plays x_convo2, swapping on every line, and a script that names
a clip explicitly still wins. Both bodies get it through the same canonical transfer the
zombies use, so it works on any canon-mapped rig.
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


# ---------------------------------------------------------------- the engine
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
s, was = load(P)

s = sub(s, """                            if(g_heroPk&&S8->hclip[0]){""",
        """                            /* ⭐ CAST BY ROLE, NOT BY NAME. `auto` means: whoever is
                             * SPEAKING this line gestures (x_convo1) and whoever is
                             * listening listens (x_convo2), swapping every line. A script
                             * that names a clip still wins. Borrowed off the bank through
                             * the canonical transfer, so it works on any canon rig. */
                            const char*hcl=S8->hclip; const char*pcl=S8->pclip;
                            if(!strcmp(hcl,"auto")) hcl=(S8->spk==0)?"x_convo1":"x_convo2";
                            if(!strcmp(pcl,"auto")) pcl=(S8->spk==0)?"x_convo2":"x_convo1";
                            if(g_heroPk&&hcl[0]){""",
        "role-cast the staged clips")

s = sub(s, """                                int f8=-1; for(unsigned c8=0;c8<g_heroPk->nClips;c8++)
                                    if(!strcmp(packStr(g_heroPk,g_heroPk->clips[c8].name),S8->hclip)){ f8=(int)c8; break; }
                                if(f8>=0&&f8!=g_labClip){ g_labClip=f8; g_labDnC=-1; g_labWphReset=1; }
                                else if(f8<0) printf("[stage] hclip '%s' NOT FOUND@N",S8->hclip); }""".replace("@N", BS_N),
        """                                int f8=-1; for(unsigned c8=0;c8<g_heroPk->nClips;c8++)
                                    if(!strcmp(packStr(g_heroPk,g_heroPk->clips[c8].name),hcl)){ f8=(int)c8; break; }
                                if(f8>=0&&f8!=g_labClip){ g_labClip=f8; g_labDnC=-1; g_labWphReset=1; }
                                else if(f8<0){ int bc=-1; Pack*bp=bankFind(hcl,&bc);
                                    if(bp&&g_heroPk->cbOK&&g_heroPk->pbOK>=10){
                                        g_labDnP=bp; g_labDnC=bc; g_labWphReset=1; }
                                    else printf("[stage] hclip '%s' NOT FOUND@N",hcl); } }""".replace("@N", BS_N),
        "hero borrows the convo clip from the bank")

s = sub(s, """                                int f8=-1; for(unsigned c8=0;c8<g_labPartner->nClips;c8++)""",
        """                                int f8=-1; for(unsigned c8=0;c8<g_labPartner->nClips;c8++)   /* pcl below */""",
        "mark the partner loop")

save(P, s, was)
print("crowd_kms.c patched")

# ---------------------------------------------------------------- the default
P2 = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes\compile_scene.py"
t, was2 = load(P2)
t = sub(t, """              shot="ots", hclip="idle", pclip="idle",""",
        """              # ⛔ NOT "idle". kaeli's own `idle` is a GET-UP-OFF-THE-FLOOR loop -- I
              # swapped the static z_upright for it without looking at what it was on her
              # rig. `auto` casts by ROLE instead: the speaker gestures, the listener
              # listens, and it swaps on every line.
              shot="ots", hclip="auto", pclip="auto",""",
        "STAGE0 defaults to auto")
save(P2, t, was2)
print("compile_scene.py patched")
