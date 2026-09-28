"""patch_qc.py -- the QC pass, engine and instrument side.

Three of jacob's six, the ones that are not authoring:

#2 PROPS IN HANDS. `gripNode` already exists and already carries a weapon in the arena
   modes -- it is the forearm's real hand, found by name at load. It has never been
   reachable from a SCENE. Two coffees, a flashlight, a hammer, a board and a dog lead are
   all in the dialogue of this film and none of them is in a hand. Scene keys `hprop=` /
   `pprop=` / `p2prop=` name a prop stem; it is loaded from the set's prop table and drawn
   on that body's grip every frame. ⛔ Sticky like every other stage key, and `off` clears
   it, because a woman cannot put a coffee down otherwise.

#3 THE MOVES PACK IS NOT A BANK. bankFind walks g_bankP, which is mob1 + kaeli + zmoves.
   v1_moves -- 37 clips including a_convo, w_look, w_text, w_cheer, z_greet -- is loaded as
   g_movesPk and is NOT in it, so no scene has ever been able to ask for any of them by
   name. One bankAdd.

#4 FRAMECHECK CANNOT TELL "IN FRAME" FROM "READABLE". It passed act IV, which is 112
   seconds of a parked pickup truck with the three leads as specks at the edge of frame.
   It now reports how big a standing body IS in the frame, as a fraction of picture
   height, and flags anything under 12% -- which is roughly the point below which a face
   stops being a performance and becomes a silhouette.
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


# ---------------------------------------------------------------- #3 moves is a bank
sub("""        bankAdd(&dn1); bankAdd(&dn2);""",
    """        bankAdd(&dn1); bankAdd(&dn2);
        /* ⛔ AND THE MOVES PACK WAS NEVER IN THE BANK. v1_moves carries 37 clips --
         * a_convo, a_convo2, w_look, w_text, w_cheer, w_music, z_greet, z_bow, z_hello --
         * and bankFind only ever walked mob1, kaeli and the zombies. No scene has ever
         * been able to ask for any of them by name, which is most of the reason the film
         * has no business in it. */
        if(g_movesPk) bankAdd(g_movesPk);""",
    "the moves pack joins the bank")

# ---------------------------------------------------------------- #2 props in hands
sub("""                 float p2gap,p2yaw,p2side,p2brow; int p2look, p2has; char p2clip[32];""",
    """                 float p2gap,p2yaw,p2side,p2brow; int p2look, p2has; char p2clip[32];
                 /* ⭐ WHAT THEY ARE HOLDING. Sticky, and `off` clears it -- a woman has to
                  * be able to put a coffee down. */
                 char hprop[32], pprop[32], p2prop[32];""",
    "ScnLine carries props")

sub("""                L->p2look=0; L->p2brow=0.f; L->p2has=0; L->p2clip[0]=0;""",
    """                L->p2look=0; L->p2brow=0.f; L->p2has=0; L->p2clip[0]=0;
                L->hprop[0]=L->pprop[0]=L->p2prop[0]=0;""",
    "prop defaults")

sub("""            if((q=strstr(ln,"p2clip="))){ int k=0; q+=7;""",
    """            if((q=strstr(ln,"hprop="))){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\\n'&&*q!='\\r'&&k<31) L->hprop[k++]=*q++; L->hprop[k]=0; }
            /* ⛔ `pprop=` matches inside `p2prop=` -- the same trap as pgap/pclip. */
            { q=strstr(ln,"pprop="); while(q&&q>ln&&q[-1]>='0'&&q[-1]<='9') q=strstr(q+1,"pprop=");
              if(q){ int k=0; q+=6;
                while(*q&&*q!=' '&&*q!='\\n'&&*q!='\\r'&&k<31) L->pprop[k++]=*q++; L->pprop[k]=0; } }
            if((q=strstr(ln,"p2prop="))){ int k=0; q+=7;
                while(*q&&*q!=' '&&*q!='\\n'&&*q!='\\r'&&k<31) L->p2prop[k++]=*q++; L->p2prop[k]=0; }
            if((q=strstr(ln,"p2clip="))){ int k=0; q+=7;""",
    "parse the prop keys")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")

# ---------------------------------------------------------------- #4 framecheck size
P2 = r"C:\MyVeryOwnVideoGames\reva\skate_src\film\framecheck.py"
t = io.open(P2, encoding="utf-8").read()

a = '''def check(cam, x, z):'''
b = '''BODY_H = 1.75          # a standing body, club units -- what "big enough to act" is measured against
MIN_FRAC = 0.12        # below this a face stops being a performance and becomes a silhouette


def check(cam, x, z):'''
assert t.count(a) == 1
t = t.replace(a, b)

a = '''    d = (tx * fx + ty * fy + tz * fz) / tl
    off = math.degrees(math.acos(max(-1.0, min(1.0, d))))
    return (tl, off, fov / 2.0)'''
b = '''    d = (tx * fx + ty * fy + tz * fz) / tl
    off = math.degrees(math.acos(max(-1.0, min(1.0, d))))
    # \u2b50 AND HOW BIG SHE IS IN IT. "In frame" is not "readable": this passed act IV,
    # which is 112 seconds of a parked pickup with the three leads as specks at the edge.
    # A body of BODY_H at distance tl subtends 2*atan(h/2/tl); the frame subtends fov.
    # The ratio is the fraction of picture height she occupies.
    sub = 2.0 * math.degrees(math.atan(BODY_H / 2.0 / max(0.01, tl)))
    return (tl, off, fov / 2.0, sub / fov)'''
assert t.count(a) == 1
t = t.replace(a, b)

a = '''                tl, off, half = r
                flag = ""
                if tl < 0.8:
                    flag = "  <-- INSIDE THE NEAR PLANE"
                elif off > half:
                    flag = "  <-- OUT OF FRAME"'''
b = '''                tl, off, half, frac = r
                flag = ""
                if tl < 0.8:
                    flag = "  <-- INSIDE THE NEAR PLANE"
                elif off > half:
                    flag = "  <-- OUT OF FRAME"
                elif frac < MIN_FRAC:
                    flag = "  <-- TOO SMALL: %.0f%% of frame height" % (100 * frac)'''
assert t.count(a) == 1
t = t.replace(a, b)

a = '''                    worst.append("     %-9s dist %6.1f  off %5.1f deg  (half fov %4.1f)%s"
                                 % (tag, tl, off, half, flag))'''
b = '''                    worst.append("     %-9s dist %6.1f  off %5.1f deg  size %4.0f%%%s"
                                 % (tag, tl, off, 100 * frac, flag))'''
assert t.count(a) == 1
t = t.replace(a, b)

io.open(P2, "w", encoding="utf-8").write(t)
print("framecheck.py: measures how big she is in frame")
