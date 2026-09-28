"""patch_sceneclip.py -- A SCENE OWNS ITS CLIPS, and three real framing bugs.

Found by framecheck.py and one diagnostic take, BEFORE shooting forty minutes of film.

⛔ 1. KAELI CRAWLED THROUGH HER OWN SCENE ON ALL FOURS, and here is the mechanism, which
two earlier attempts never found. driver.py writes `clip=idle` into the scene ctl. The
engine's per-line staging resolves the hero's clip (`hclip=auto` -> `x_convo2`) at one
point in the frame, and the ctl re-parse that honours `clip=` runs LATER IN THE SAME
FUNCTION -- so `idle` overwrote the staging's choice on every single poll. And kaeli's
`idle` is a get-up-off-the-floor loop.

    the staging resolve  ~line 9241
    the ctl clip= parse  ~line 9405     <-- runs after, wins every frame

Fixed on both sides, because either alone would leave the trap armed for the next caller:
the ctl parse now IGNORES clip=/pclip= while a scene is playing and says so once, and the
driver stops writing them for a scene at all.

⛔ 2. THE three_lawns PAIR WERE STAGED 300 UNITS OUTSIDE THEIR OWN SET. The row said
stage "240,-240,0"; the set's props run x 47..53 and its actors z -4..-21, and BOTH
cameras aim at ~(35,-22). The lenses were right and the mark was wrong -- sixty-eight
seconds of correctly-lit empty lawn.

⛔ 3. A FIXED A CAMERA LOSES A PAIR THAT WALKS. dogwalk's A lens shoots an empty street:
the two of them are out of the left edge inside the first second and the remaining fifty
seconds are a sidewalk with subtitles over it. The conversation lens (camA="") frames the
two HEAD POSITIONS, so it cannot lose them -- that is what it is for. dogwalk and theleash
both walk; both go back to it.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
BS = chr(92)


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


# ------------------------------------------------------------------ 1a. the engine
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
s, was = load(P)
s = sub(s,
    '''                        if((q=strstr(buf,"once="))) g_labOnce=atoi(q+5)?1:0;   /* P1: one-shot playback */''',
    '''                        if((q=strstr(buf,"once="))) g_labOnce=atoi(q+5)?1:0;   /* P1: one-shot playback */
                        /* ⛔ A SCENE OWNS ITS CLIPS. The per-line staging resolves the
                         * hero's and partner's clips ~160 lines above this, and this
                         * parse ran afterwards -- so a ctl `clip=idle` overwrote the
                         * staging's choice on EVERY poll, and kaeli spent an entire scene
                         * playing her get-up-off-the-floor loop on all fours. Two earlier
                         * attempts looked at the staging (which was correct, and said so
                         * in the log) and never at what came after it. */
                        if(g_scnN>0&&(strstr(buf,"clip=")||strstr(buf,"pclip="))){
                            static int said=0;
                            if(!said){ said=1;
                                printf("[scene] ctl clip=/pclip= IGNORED while a scene plays -- the script owns them");
                                printf("%c",10); fflush(stdout); } }''',
    "the engine ignores ctl clip= during a scene")

# and make the two clip= parsers bail while a scene is up
s = sub(s,
    '''                            if(!k2||!strcmp(nm2,"off")){ g_labClip=-1; g_labDnC=-1; printf("[lab] clip override OFF''',
    '''                            if(g_scnN>0){ /* the script owns it -- see the note above */ }
                            else if(!k2||!strcmp(nm2,"off")){ g_labClip=-1; g_labDnC=-1; printf("[lab] clip override OFF''',
    "clip= bails during a scene")
save(P, s, was)
print("crowd_kms.c patched")

# ------------------------------------------------------------------ 1b. the driver
P2 = r"C:\MyVeryOwnVideoGames\reva\.claude\skills\run-reva\driver.py"
t, was2 = load(P2)
t = sub(t,
    '''    ctl = ("face=0 grid=0 dqs=0 clip=%s partner=%s pclip=%s pyaw=%s pgap=%s pside=%s "
           "jaw=-1 look=%d scene=%s" % (a.clip, a.partner, a.pclip, a.pyaw, a.pgap,
                                        a.pside, 1 if a.look else 0, a.name))''',
    '''    # ⛔ NO clip=/pclip= HERE. A scene's own per-line staging resolves both, and writing
    # them into the ctl overwrote that resolution on every poll -- which is how kaeli spent
    # a whole take crawling through her get-up-off-the-floor loop. The engine ignores them
    # during a scene now too; this just stops sending them.
    ctl = ("face=0 grid=0 dqs=0 partner=%s pyaw=%s pgap=%s pside=%s "
           "jaw=-1 look=%d scene=%s" % (a.partner, a.pyaw, a.pgap,
                                        a.pside, 1 if a.look else 0, a.name))''',
    "driver stops writing clip= for a scene")
save(P2, t, was2)
print("driver.py patched")

# ------------------------------------------------------------------ 2 + 3. the shot list
P3 = r"C:\MyVeryOwnVideoGames\reva\skate_src\film\shoot.py"
u, was3 = load(P3)
u = sub(u,
    '''    ("dogwalk", "I", "kmoves", "crowd_susan", "walkies", "26,-18,14", "0900",
     "27.5,2.25,-8.5,26,1.45,-18.5,40", "12.0,2.70,-15.0,33.0,1.50,-19.0,46",''',
    '''    # ⛔ A FIXED A LENS LOSES A PAIR THAT WALKS -- this one shot an empty street, the two
    # of them out of the left edge in the first second. The conversation lens frames the
    # two HEAD POSITIONS and cannot lose them; that is what it is for.
    ("dogwalk", "I", "kmoves", "crowd_susan", "walkies", "26,-18,14", "0900",
     "", "12.0,2.70,-15.0,33.0,1.50,-19.0,46",''',
    "dogwalk A -> the conversation lens")
u = sub(u,
    '''    ("theleash", "II", "kmoves", "crowd_susan", "leash", "44,-19,22", "1330",
     "48.9,1.90,-20.8,44.45,1.50,-18.25,42", "43.0,2.10,-15.6,44.5,1.10,-22.5,46",''',
    '''    ("theleash", "II", "kmoves", "crowd_susan", "leash", "44,-19,22", "1330",
     "", "43.0,2.10,-15.6,44.5,1.10,-22.5,46",''',
    "theleash A -> the conversation lens")
u = sub(u,
    '''    ("three_lawns", "IV", "r2moves", "kmoves", "three_lawns", "240,-240,0", "1840",''',
    '''    # ⛔ STAGED 300 UNITS OUTSIDE ITS OWN SET. "240,-240" put the pair nowhere near the
    # lawns; the set's props run x 47..53 and both lenses aim at ~(35,-22). The cameras
    # were right and the mark was wrong -- 68 seconds of correctly-lit empty grass.
    ("three_lawns", "IV", "r2moves", "kmoves", "three_lawns", "35.5,-21,10", "1840",''',
    "three_lawns staged inside its own set")
save(P3, u, was3)
print("shoot.py patched")
