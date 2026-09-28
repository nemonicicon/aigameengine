"""patch_shotlist.py -- the shot list catches up with the script.

The script grew a cold open, a third pairing, a wordless lull and three three-handers.
The shot list still described the old seven-scene, two-body film.

  * SHOTS rows gain a `p2` field -- who stands in slot 2. Empty for a two-hander, which
    is every row that had one before, so nothing that worked changes.
  * `coldopen` and `tables` are new rows.
  * THE LULL is seven b-roll entries, not a scene, because nobody speaks in it. Its order
    is the argument: widest and most alive first, one still person last, and each shot
    quieter than the one before.
  * FILM is re-cut in story order with the new pieces in it.

⛔ AND THE HOURS ARE REAL. coldopen is 04:10 and there is no such entry in LIGHT -- night
is not "sun=off", it is a weak blue directional over a near-black sky, which the tod curve
already knows. 0410 is added from the same curve the day arc uses.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\skate_src\film\shoot.py"
s = io.open(P, encoding="utf-8").read()


def sub(a, b, why):
    global s
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- an hour before dawn ------------------------------------------------------------
sub('''    "0715": ("14,93,0.95,1.00,0.80,0.60",   "0.315,0.350,0.450"),''',
    '''    # ⛔ NIGHT IS NOT sun=off -- that is the club's dark-room constant path, with no sky
    # and no direction. Night is a WEAK BLUE DIRECTIONAL over a near-black sky, and the sun
    # is never let below y=0.06 as a light because a directional from underneath lights a
    # face from the chin up. Taken from the same tod curve the day arc runs on.
    "0410": ("10,300,0.07,0.48,0.58,0.86",  "0.030,0.036,0.062"),
    "0715": ("14,93,0.95,1.00,0.80,0.60",   "0.315,0.350,0.450"),''',
    "an hour for the cold open")

# ---- the rows -----------------------------------------------------------------------
sub('''# name, act, hero, partner, set, stage, hour, camA ("" = conversation lens), camB, EDL''',
    '''# name, act, hero, partner, P2, set, stage, hour, camA ("" = conversation lens), camB, EDL
# P2 is who stands in slot 2 -- "" for a two-hander, which is every row that had one
# before, so nothing that already worked changes shape.''',
    "row format comment")

sub('''    ("porch", "I", "r2moves", "kmoves", "parsons", "30,-20,0", "0715",''',
    '''    # ⭐ THE COLD OPEN. Interior, before dawn, and the only scene in the film with no
    # street in it. The conversation lens does the whole thing: it is two people in a dark
    # room and there is nothing else to look at, which is the point.
    ("coldopen", "0", "r2moves", "kmoves", "", "boarded", "6.4,9.8,4", "0410",
     "", "10.2,2.30,6.2,3.6,1.95,13.6,46",
     [(0, 6, "B")]),

    ("porch", "I", "r2moves", "kmoves", "", "parsons", "30,-20,0", "0715",''',
    "the cold open row")

sub('''    ("dogwalk", "I", "kmoves", "crowd_susan", "walkies", "26,-18,14", "0900",''',
    '''    # ⭐ THE THIRD PAIRING. reva and susan, alone, which the film had never done -- and
    # where the small kind lie is. B is the tables themselves, so the scene has something
    # to be ABOUT other than two faces.
    ("tables", "I", "r2moves", "crowd_susan", "", "parsons", "33,-19,110", "0900",
     "", "38.5,2.35,-12.0,33.4,1.50,-19.2,44",
     [(0, 5, "B"), (44, 52, "B")]),

    ("dogwalk", "I", "kmoves", "crowd_susan", "", "walkies", "26,-18,14", "0900",''',
    "the tables row")

for old, new, why in [
    ('("denial", "II", "r2moves", "kmoves", "parsons"',
     '("denial", "II", "r2moves", "kmoves", "", "parsons"', "denial p2 field"),
    ('("theleash", "II", "kmoves", "crowd_susan", "leash"',
     '("theleash", "II", "kmoves", "crowd_susan", "", "leash"', "theleash p2 field"),
    ('("it_starts", "III", "r2moves", "kmoves", "the_turn"',
     '("it_starts", "III", "r2moves", "kmoves", "crowd_susan", "the_turn"', "it_starts is a three-hander"),
    ('("three_lawns", "IV", "r2moves", "kmoves", "three_lawns"',
     '("three_lawns", "IV", "r2moves", "kmoves", "crowd_susan", "three_lawns"', "three_lawns is a three-hander"),
    ('("boarding", "V", "r2moves", "kmoves", "boarded"',
     '("boarding", "V", "r2moves", "kmoves", "crowd_susan", "boarded"', "boarding is a three-hander"),
]:
    sub(old, new, why)

# ---- the lull, as seven held shots --------------------------------------------------
sub('''    "night_house":  ("parsons", "2200", "39.5,2.45,-9.0,31.5,2.30,-24.5,34", 6.0),
}''',
    '''    "night_house":  ("parsons", "2200", "39.5,2.45,-9.0,31.5,2.30,-24.5,34", 6.0),

    # ⭐⭐ THE LULL -- act II-and-a-half, and its script IS this table. Seven held shots,
    # no moves, no cuts inside a shot. THE ORDER IS THE ARGUMENT: it runs from the widest
    # and most alive to one still person, and each shot is quieter than the one before it.
    # Nobody says a word and by shot seven the audience has done the arithmetic on its own.
    "lull1_street": ("parsons", "1200", "8.0,7.40,9.0,44.0,2.20,-6.0,52",  12.0),
    "lull2_grill":  ("parsons", "1200", "21.5,2.10,-9.5,27.5,1.70,-20.5,40", 10.0),
    "lull3_pool":   ("parsons", "1200", "48.0,2.05,-8.0,44.0,1.20,-18.5,42", 10.0),
    "lull4_tables": ("parsons", "1200", "40.0,2.60,-6.0,33.6,1.55,-19.4,34", 12.0),
    # ⛔ shot 5 is the only time in the film the camera declines to follow something: a kid
    # does laps and leaves frame and the lens stays where it is.
    "lull5_bikes":  ("parsons", "1200", "14.0,1.95,-7.0,26.0,1.35,-13.0,46", 10.0),
    "lull6_gate":   ("parsons", "1200", "30.0,1.85,-10.5,34.2,1.45,-20.8,36", 10.0),
    # ⛔ the longest hold in the film so far, on a woman who does not move.
    "lull7_parsons":("parsons", "1200", "33.0,1.95,9.5,30.3,1.55,20.4,30", 14.0),
}''',
    "the lull as seven b-roll shots")

# ---- the cut order ------------------------------------------------------------------
sub('''FILM = [("b", "open_street"), ("s", "porch"), ("s", "dogwalk"), ("b", "block_alive"),
        ("s", "denial"), ("b", "mailbox_hold"), ("s", "theleash"), ("s", "it_starts"),
        ("b", "dusk_street"), ("s", "three_lawns"), ("b", "night_house"), ("s", "boarding")]''',
    '''FILM = [("s", "coldopen"),
        ("b", "open_street"), ("s", "porch"), ("s", "tables"), ("s", "dogwalk"),
        ("b", "block_alive"),
        ("s", "denial"),
        # act II-and-a-half: the street, one last time, and nobody speaks
        ("b", "lull1_street"), ("b", "lull2_grill"), ("b", "lull3_pool"),
        ("b", "lull4_tables"), ("b", "lull5_bikes"), ("b", "lull6_gate"),
        ("b", "lull7_parsons"),
        ("s", "theleash"), ("s", "it_starts"),
        ("b", "dusk_street"), ("s", "three_lawns"), ("b", "night_house"), ("s", "boarding")]''',
    "the cut order")

# ---- unpack the new field everywhere ------------------------------------------------
sub('''    (name, act, subj, partner, setname, stage, hour, camA, camB, _edl) = row''',
    '''    (name, act, subj, partner, p2, setname, stage, hour, camA, camB, _edl) = row''',
    "take() unpacks p2")

sub('''           "--set", setname, "--stage", stage, "--sun", sun, "--sky", sky]''',
    '''           "--set", setname, "--stage", stage, "--sun", sun, "--sky", sky]
    if p2:
        cmd += ["--partner2", p2]''',
    "take() passes partner2")

sub('''    (name, act, _s, _p, _st, _sg, _h, _ca, _cb, edl) = row''',
    '''    (name, act, _s, _p, _p2, _st, _sg, _h, _ca, _cb, edl) = row''',
    "cut() unpacks p2")

# (framecheck.py is a separate file and is patched on its own)

io.open(P, "w", encoding="utf-8").write(s)
print("shoot.py patched")
