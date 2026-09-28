"""Wire the silent B-roll shots into shoot.py's film order."""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\skate_src\film\shoot.py"
s = io.open(P, encoding="utf-8").read()
BS = chr(92)


def sub(a, b, why):
    global s
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


sub("import argparse, glob, os, subprocess, sys, time",
    "import argparse, glob, os, subprocess, sys, time\n"
    "sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))",
    "broll importable")

sub("def scnlen(nm, default=60.0):",
    '''# THE FILM IS NOT ONLY THE TALKING. jacob: "there needs to be more scenic shots
# especially in the beginning. just dumps me in to dialouge" and "this script is very
# disjointed" -- one fault, and it is that the shot list only ever held scenes with WORDS
# in them, so seven conversations were butt-spliced with nothing between them. The
# treatment opens on a street being ordinary for forty seconds before anybody speaks.
# name -> set, hour, cam, seconds
BROLL = {
    "open_street":  ("parsons", "0715", "-26,2.05,-5.5,92,2.60,5.0,40", 10.0),
    "block_alive":  ("parsons", "0900", "16,6.40,-30,74,2.20,14.0,46",   7.0),
    "mailbox_hold": ("parsons", "1200", "33.0,2.05,9.0,30.3,1.60,20.6,26", 6.5),
    "dusk_street":  ("three_lawns", "1840", "62,3.10,6.0,34,1.60,-20.0,48", 6.0),
    "night_house":  ("parsons", "2200", "34.5,2.30,-6.0,30.5,2.10,-24.0,40", 6.0),
}
# the CUT ORDER of the whole film: ("b", broll) or ("s", scene)
FILM = [("b", "open_street"), ("s", "porch"), ("s", "dogwalk"), ("b", "block_alive"),
        ("s", "denial"), ("b", "mailbox_hold"), ("s", "theleash"), ("s", "it_starts"),
        ("b", "dusk_street"), ("s", "three_lawns"), ("b", "night_house"), ("s", "boarding")]


def scnlen(nm, default=60.0):''', "BROLL + FILM order")

sub("""    made = []
    for row in rows:""",
    """    made = {}
    for row in rows:""", "made becomes a map")

sub("""        c = cut(a, row, ta, tb)
        if c:
            made.append(c)
        sys.stdout.flush()""",
    """        c = cut(a, row, ta, tb)
        if c:
            made[row[0]] = c
        sys.stdout.flush()

    # ---- the silent shots, in the same pass ----
    import broll as BR
    for key in [k for (_t, k) in FILM if k in BROLL]:
        setname, hour, cam, secs = BROLL[key]
        if a.only and a.only != key:
            continue
        sun, sky = LIGHT[hour]
        out = os.path.join(a.takes, "b_%s.mp4" % key)
        print("")
        print("=== BROLL   %-12s %5.1fs  set=%-12s %s ===" % (key, secs, setname, hour))
        sys.stdout.flush()
        if a.reuse and os.path.exists(out):
            made[key] = out
            print("   reusing")
            continue
        if BR.shoot(out, setname, cam, secs, a.fps, sun, sky, quiet=False):
            made[key] = out""", "shoot the b-roll")

old_lst = ('    lst = os.path.join(a.takes, "_film.txt")' + chr(10) +
           '    with open(lst, "w") as f:' + chr(10) +
           '        for m in made:' + chr(10) +
           '            f.write("file \'%s\'' + BS + 'n" % m.replace("' + BS + BS + '", "/"))')
new_lst = ('    # CUT IN THE FILM ORDER, not in shooting order: b-roll before the porch,' + chr(10) +
           '    # between the acts, and ahead of the night.' + chr(10) +
           '    order = [made[k] for (_t, k) in FILM if k in made]' + chr(10) +
           '    lst = os.path.join(a.takes, "_film.txt")' + chr(10) +
           '    with open(lst, "w") as f:' + chr(10) +
           '        for m in order:' + chr(10) +
           '            f.write("file \'%s\'' + BS + 'n" % m.replace("' + BS + BS + '", "/"))')
sub(old_lst, new_lst, "concat in film order")

sub("          % (a.out, os.path.getsize(a.out) / 1e6, len(made)))",
    "          % (a.out, os.path.getsize(a.out) / 1e6, len(order)))", "count the pieces")

io.open(P, "w", encoding="utf-8").write(s)
print("shoot.py patched")
