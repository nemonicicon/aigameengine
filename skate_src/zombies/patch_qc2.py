"""patch_qc2.py -- the lenses, the b-roll, and the street's population.

#4 ACT IV'S CAMERA. `framecheck` now measures how big a body is in the frame and it
convicts the shot at once: the A lens put a standing woman at **6% of frame height**, 31
units away, for 112 seconds -- the payoff of the whole film shot from across the road.
Moved in to a real wide that still shows the ground they have to cross. And porch's B lens
was at 11%, which is a street with two people somewhere in it.

#3 THE B-ROLL WAS AIMED AT THE WRONG SIDE OF THE ROAD. `parsons.set` holds its actors at
z ~ +19 to +21 and every b-roll camera looked at z ~ -6 to -20. Twelve lenses, all pointed
away from the entire cast. Re-aimed onto the side the people are on.

⛔ AND ELEVEN ACTORS IS NOT A BLOCK PARTY. The lull's own script says "forty bodies doing
forty things"; the set had five of its own plus six inherited. Twenty-two more go in --
citizens on both verges, at the tables, by the grill, on the road -- using the clip
vocabulary that was already on disk and that nothing has ever asked for: w_look, w_text,
w_cheer, m_drink, x_fidget1, x_fidget2, v_walk1..5.
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


# ---- act IV: in, and lower ---------------------------------------------------------
sub('''     "55,3.4,3.5,35,1.5,-22,52", "43.5,1.65,-7.5,36.0,1.40,-20.5,42",''',
    '''     # ⛔ WAS "55,3.4,3.5,35,1.5,-22,52" -- 31 units back, a standing woman at SIX PER
     # CENT of frame height, for 112 seconds, on the payoff of the entire film.
     # framecheck's size test convicts it. In to 12 units: still a wide, still shows the
     # ground they have to cross, and they are people rather than specks.
     "44.5,2.30,-9.5,35.5,1.45,-20.5,46", "40.0,1.55,-14.0,35.5,1.30,-21.0,40",''',
    "act IV moves in")

# ---- porch B: 11% is a street with two people somewhere in it ------------------------
sub('''     "", "45.5,3.30,-3.0,31.0,1.55,-19.0,40",''',
    '''     "", "38.0,2.55,-9.5,30.6,1.55,-19.6,42",''',
    "porch B moves in")

# ---- the b-roll turns round ---------------------------------------------------------
sub('''    "open_street":  ("parsons", "0715", "-26,2.05,-5.5,92,2.60,5.0,40", 10.0),''',
    '''    # ⛔ EVERY ONE OF THESE LOOKED AT z ~ -6..-20 AND THE ACTORS ARE ALL AT z ~ +19..+21.
    # Twelve lenses aimed at the opposite side of the road from the entire cast, which is
    # why the b-roll measured 0.01 -- a photograph. Turned round.
    "open_street":  ("parsons", "0715", "-4.0,2.05,10.5,66.0,2.10,20.0,40", 10.0),''',
    "open_street turns round")

for old, new, why in [
    ('"block_alive":  ("parsons", "0900", "14,8.60,7.0,86,2.40,7.0,46",   7.0),',
     '"block_alive":  ("parsons", "0900", "6.0,7.20,6.0,64.0,2.20,19.5,50",  7.0),', "block_alive"),
    ('"mailbox_hold": ("parsons", "1200", "33.0,2.05,9.0,30.3,1.60,20.6,26", 6.5),',
     '"mailbox_hold": ("parsons", "1200", "30.6,1.95,14.6,30.3,1.58,20.4,28", 6.5),', "mailbox_hold"),
    ('"lull1_street": ("parsons", "1200", "8.0,7.40,9.0,44.0,2.20,-6.0,52",  12.0),',
     '"lull1_street": ("parsons", "1200", "2.0,6.60,7.5,60.0,2.10,20.0,52",  12.0),', "lull1"),
    ('"lull2_grill":  ("parsons", "1200", "21.5,2.10,-9.5,27.5,1.70,-20.5,40", 10.0),',
     '"lull2_grill":  ("parsons", "1200", "70.0,2.10,14.0,74.0,1.70,20.4,40", 10.0),', "lull2 -> dale"),
    ('"lull3_pool":   ("parsons", "1200", "48.0,2.05,-8.0,44.0,1.20,-18.5,42", 10.0),',
     '"lull3_pool":   ("parsons", "1200", "56.0,2.05,14.0,59.6,1.30,20.8,42", 10.0),', "lull3"),
    ('"lull4_tables": ("parsons", "1200", "40.0,2.60,-6.0,33.6,1.55,-19.4,34", 12.0),',
     '"lull4_tables": ("parsons", "1200", "14.0,2.40,14.5,18.4,1.55,19.8,38", 12.0),', "lull4 -> susan"),
    ('"lull5_bikes":  ("parsons", "1200", "14.0,1.95,-7.0,26.0,1.35,-13.0,46", 10.0),',
     '"lull5_bikes":  ("parsons", "1200", "40.0,1.95,12.0,48.0,1.35,20.0,46", 10.0),', "lull5"),
    ('"lull6_gate":   ("parsons", "1200", "30.0,1.85,-10.5,34.2,1.45,-20.8,36", 10.0),',
     '"lull6_gate":   ("parsons", "1200", "24.0,1.85,13.5,30.0,1.45,20.6,36", 10.0),', "lull6"),
    ('"lull7_parsons":("parsons", "1200", "33.0,1.95,9.5,30.3,1.55,20.4,30", 14.0),',
     '"lull7_parsons":("parsons", "1200", "30.2,1.90,15.2,30.2,1.55,20.6,26", 14.0),', "lull7"),
    ('"night_house":  ("parsons", "2200", "39.5,2.45,-9.0,31.5,2.30,-24.5,34", 6.0),',
     '"night_house":  ("parsons", "2200", "34.0,2.20,13.0,30.4,1.90,20.4,36", 6.0),', "night_house"),
]:
    sub(old, new, why)

io.open(P, "w", encoding="utf-8").write(s)
print("shoot.py: the b-roll faces the people")

# ------------------------------------------------------------------ populate the street
P2 = r"C:\MyVeryOwnVideoGames\reva\reva_native\sets\parsons.set"
t = io.open(P2, encoding="utf-8").read()
add = """
# ================= THE BLOCK PARTY (added 08-30, the QC pass) =====================
# ⛔ ELEVEN ACTORS IS NOT "FORTY BODIES DOING FORTY THINGS". The lull's script asks for a
# street that is alive and the set held five of its own plus six from `block`, which is
# why every b-roll shot measured as a still photograph even after the lenses were turned
# round to face them.
#
# ⭐ AND NONE OF THESE CLIPS ARE NEW. w_look, w_text, w_cheer, m_drink, z_greet, x_fidget1,
# x_fidget2 and v_walk1..5 were all already on disk, in v1_moves and v1_mob1, and nothing
# in this film had ever asked for one of them by name -- because v1_moves was not in the
# bank until today.
#
# Everything is on the +z verge, which is the side the cameras now look at.

# --- around the tables -----------------------------------------------------------
actor v1_Character_01          w_text        16.40  0.00   20.10   200.0  1.000  0.90
actor v1_Character_07          z_greet       20.80  0.00   19.40   240.0  1.000  0.95
actor v1_Character_11          w_look        23.60  0.00   20.60   190.0  1.000  0.85
actor v1_Character_15          x_fidget1     12.20  0.00   19.80   210.0  1.000  0.90

# --- the grill, and Dale's end of the street -------------------------------------
actor v1_Character_18          m_drink       70.40  0.00   19.90   250.0  1.000  0.95
actor v1_Character_22          w_cheer       77.20  0.00   20.70   230.0  1.000  0.90
actor v1_Character_25          x_fidget2     67.60  0.00   21.10   200.0  1.000  0.85

# --- the pool end -----------------------------------------------------------------
actor v1_Character_28          w_look        55.00  0.00   20.20   180.0  1.000  0.80
actor v1_Character_31          w_cheer       58.80  0.00   19.60   220.0  1.000  0.78
actor v1_Character_33          x_fidget1     61.40  0.00   21.00   195.0  1.000  0.82

# --- walking the length of it -- these TRAVEL, which is the point ----------------
actor v1_Character_02          v_walk1       10.00  0.00   18.40    90.0  1.000  1.00  1.60  60.00
actor v1_Character_05          v_walk3       64.00  0.00   18.90   270.0  1.000  1.00  1.45  52.00
actor v1_Character_09          v_walk2       34.00  0.00   17.80    90.0  1.000  1.00  1.75  40.00
actor v1_Character_13          v_walk5       50.00  0.00   19.10   270.0  1.000  1.00  1.30  44.00
actor v1_Character_17          w_walk        26.00  0.00   18.20    90.0  1.000  1.00  1.55  36.00

# --- the far verge and the road ---------------------------------------------------
actor v1_Character_20          w_music       44.20  0.00   20.80   210.0  1.000  0.88
actor v1_Character_24          z_greet       47.80  0.00   19.50   170.0  1.000  0.92
actor v1_Character_27          w_text        38.60  0.00   21.20   205.0  1.000  0.86
actor v1_Character_30          x_fidget2     41.00  0.00   19.30   185.0  1.000  0.90
actor v1_Character_35          w_look         7.20  0.00   20.90   215.0  1.000  0.84
actor v1_Character_37          m_drink       53.20  0.00   21.30   235.0  1.000  0.88
actor v1_Character_39          w_cheer       31.20  0.00   20.40   225.0  1.000  0.86
"""
if "THE BLOCK PARTY (added 08-30" not in t:
    io.open(P2, "w", encoding="utf-8").write(t.rstrip() + "\n" + add)
    print("parsons.set: +22 actors on the side the cameras face")
else:
    print("parsons.set already populated")
