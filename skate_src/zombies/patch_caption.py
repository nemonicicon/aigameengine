"""patch_caption.py -- the CAPTION has to know who is speaking too.

`cast 0=kaeli 1=susan` already tells say_piper which VOICE each slot gets. The engine's
subtitle was still hard-wired -- `rv9 ? "REVA" : "KAELI"` -- so susan's first line in
`dogwalk` came up on screen over KAELI's name. Speaker indices are POSITIONAL (0 is the
hero the lab loaded), and four of this film's seven scenes are between the two people who
are not reva, so nothing keyed on the index can name anybody.

The cast now rides in the `.scn` beside the header, and the engine reads it.
"""
import io, sys

NL = chr(10)
BS_N = chr(92) + "n"          # the two characters backslash + n, for a source literal
CR, LF = chr(13), chr(10)
CRLF = CR + LF


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


# ------------------------------------------------- compile_scene.py
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes\compile_scene.py"
s, was = load(P)

s = sub(s, """    lines = []
    stage = dict(STAGE0)""",
        """    lines = []
    cast_names = {"0": "REVA", "1": "KAELI"}
    stage = dict(STAGE0)""", "cast_names default")

s = sub(s, """        if s.startswith("cast "): continue""",
        """        if s.startswith("cast "):
            # The cast reaches the SCREEN as well as the voices: the engine's caption was
            # hard-wired to REVA/KAELI by speaker index, so susan's dialogue came up over
            # kaeli's name.
            for bb in s.split()[1:]:
                if "=" in bb:
                    sl, who = bb.split("=", 1)
                    cast_names[sl] = who.upper()
            continue""", "read the cast into names")

a3 = '        f.write("scene 1 %s.wav %.3f %d@N" % (out, total, len(tracks)))'.replace("@N", BS_N)
b3 = (a3 + NL +
      '        f.write("cast %s %s@N" % (cast_names.get("0", "REVA"), cast_names.get("1", "KAELI")))'
      .replace("@N", BS_N))
s = sub(s, a3, b3, "write the cast line into the .scn")
save(P, s, was)
print("compile_scene.py patched")

# ------------------------------------------------- crowd_kms.c
P2 = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
t, was2 = load(P2)

t = sub(t, """static int   g_scnStaged=-1;            /* the line whose clips are already loaded */""",
        """static int   g_scnStaged=-1;            /* the line whose clips are already loaded */
/* WHO IS IN THIS SCENE, for the caption. Speaker indices are POSITIONAL -- 0 is the hero
 * the lab loaded -- so a name cannot be keyed on them, and four of this film's seven
 * scenes are between the two people who are not reva. */
static char  g_scnWho[2][16]={"REVA","KAELI"};""", "caption-name globals")

t = sub(t, """        if(!strncmp(ln,"scene ",6)){ int nl=0; sscanf(ln,"scene %*d %127s %f %d",wav,&g_scnLen,&nl); }""",
        """        if(!strncmp(ln,"scene ",6)){ int nl=0; sscanf(ln,"scene %*d %127s %f %d",wav,&g_scnLen,&nl);
            snprintf(g_scnWho[0],16,"REVA"); snprintf(g_scnWho[1],16,"KAELI"); }
        else if(!strncmp(ln,"cast ",5)) sscanf(ln,"cast %15s %15s",g_scnWho[0],g_scnWho[1]);""",
        "parse the cast line")

t = sub(t, """                  const char*who=rv9?"REVA":"KAELI";""",
        """                  const char*who=rv9?g_scnWho[0]:g_scnWho[1];""",
        "caption from the cast")
save(P2, t, was2)
print("crowd_kms.c patched")
