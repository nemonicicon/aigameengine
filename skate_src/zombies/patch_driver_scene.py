import io, sys
P = r"C:\MyVeryOwnVideoGames\reva\.claude\skills\run-reva\driver.py"
s = io.open(P, encoding="utf-8", newline="").read()
CR, LF = chr(13), chr(10); CRLF = CR+LF
WAS = CRLF in s
if WAS: s = s.replace(CRLF, LF)
orig = s
def sub(a, b, why):
    global s
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why)); sys.exit(1)
    s = s.replace(a, b); print("   ok: " + why)

sub('''    ctl = ("face=0 grid=0 dqs=0 clip=%s partner=%s pclip=%s pyaw=%s pgap=%s pside=%s "
           "jaw=-1 look=%d scene=%s" % (a.clip, a.partner, a.pclip, a.pyaw, a.pgap,
                                        a.pside, 1 if a.look else 0, a.name))''',
    '''    ctl = ("face=0 grid=0 dqs=0 clip=%s partner=%s pclip=%s pyaw=%s pgap=%s pside=%s "
           "jaw=-1 look=%d scene=%s" % (a.clip, a.partner, a.pclip, a.pyaw, a.pgap,
                                        a.pside, 1 if a.look else 0, a.name))
    # A SCENE CAN HAPPEN IN THE WORLD. The set renderer and the scene system coexist --
    # `set=` draws the street, `stage=` stands the pair in it, and the scene camera keeps
    # working because it frames the two HEAD POSITIONS, which move with them.
    if a.set:
        ctl += " set=%s" % a.set
        if a.stage: ctl += " stage=%s" % a.stage
        if a.sun:   ctl += " sun=%s" % a.sun
        if a.sky:   ctl += " sky=%s" % a.sky''',
    "scene ctl carries set/stage/sun/sky")

sub('''    sp.add_argument("--tmp"); sp.add_argument("--new", action="store_true")
    sp.set_defaults(fn=scene)''',
    '''    sp.add_argument("--set", default="", help="play the scene inside this .set")
    sp.add_argument("--stage", default="", help="x,z,yawDeg -- where the pair stands in it")
    sp.add_argument("--sun", default="", help="elev,azim,intensity[,r,g,b]")
    sp.add_argument("--sky", default="", help="r,g,b")
    sp.add_argument("--tmp"); sp.add_argument("--new", action="store_true")
    sp.set_defaults(fn=scene)''',
    "scene args")

if s != orig:
    io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if WAS else s)
    print("patched driver.py")
