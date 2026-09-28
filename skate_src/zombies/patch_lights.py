"""patch_lights.py -- CAPABILITY G: six practicals instead of three.

`lp[3]/lc[3]` was the shader's cap and therefore the SET's cap: three lights in a room.
Act V is one lamp, one torch and one uncovered window and that is the whole budget spent;
kaeli_house wanted a night light, its bounce, and the street through the glass, and had
nothing left for anything else.

⛔ AND THIS IS THE RISKIEST EDIT IN THE FILE, which is why it is done alone and with a
regression either side. `lp[3]` is declared in THREE separate shader strings and the loop
that reads it lives in a shared LITE macro, so the count exists in four places that must
agree; on the C side six more arrays are sized [9] and six glUniform3fv calls pass a
literal 3. Miss any one of them and the shader reads uninitialised uniforms -- which does
not fail, it just lights every door wrongly.

⭐ SO THE COUNT BECOMES ONE NAME. NLIGHT in C, "6" spliced into the shader source, and
every array sized NLIGHT*3. There is nothing left to keep in agreement by hand.

⛔ AND THE EXTRA LIGHTS MUST START DARK. The set clears them to y=-99 with zero colour,
which is what keeps an unlit set unlit -- three more slots of uninitialised memory in a
uniform array is a room lit by whatever was on the stack.
"""
import io, re, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)

n_before = s.count("lp[3]")


def sub(a, b, why, count=1):
    global s
    n = s.count(a)
    if n != count:
        print("!! anchor x%d (wanted %d) for %s" % (n, count, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s (x%d)" % (why, n))


# ---- one name for the count ---------------------------------------------------------
sub("#define MAXB 64",
    """#define MAXB 64
/* ⭐ THE PRACTICAL COUNT, IN ONE PLACE. It used to be the literal 3 in four shader
 * locations and six C arrays, all of which had to agree by hand -- and a disagreement
 * does not fail, it lights every door wrongly. */
#define NLIGHT 6""",
    "NLIGHT")

# ---- the shaders --------------------------------------------------------------------
sub('"uniform vec3 lp[3]; uniform vec3 lc[3];\\n"',
    '"uniform vec3 lp[6]; uniform vec3 lc[6];\\n"',
    "shader light declarations", count=3)

# ⛔ The loop appears TWICE -- once plain and once with a macro continuation. Both.
_pat = '"  for(int i=0;i<3;i++){ vec3 D=lp[i]-vwp; float dd=dot(D,D);' + chr(92) + 'n"'
_n = s.count(_pat)
assert _n == 2, "light loop x%d" % _n
s = s.replace(_pat, _pat.replace("i<3", "i<6"))
print("   ok: shader loop (x%d, plain and LITE macro)" % _n)

# ---- the C arrays -------------------------------------------------------------------
sub("static float g_setLp[9]={0,-99,0, 0,-99,0, 0,-99,0}, g_setLc[9]={0,0,0, 0,0,0, 0,0,0};",
    """/* ⛔ AND THE EXTRA SLOTS MUST START DARK. y=-99 with zero colour is what keeps an
 * unlit set unlit; three more slots of uninitialised memory is a room lit by the stack. */
static float g_setLp[NLIGHT*3]={0,-99,0, 0,-99,0, 0,-99,0, 0,-99,0, 0,-99,0, 0,-99,0};
static float g_setLc[NLIGHT*3]={0};""",
    "the set's light arrays")

sub("            if(nl>=6&&g_setNL<3){",
    "            if(nl>=6&&g_setNL<NLIGHT){",
    "the set parser's cap")

sub("    for(int k=0;k<3;k++){ g_setLp[k*3]=0.f; g_setLp[k*3+1]=-99.f; g_setLp[k*3+2]=0.f;",
    "    for(int k=0;k<NLIGHT;k++){ g_setLp[k*3]=0.f; g_setLp[k*3+1]=-99.f; g_setLp[k*3+2]=0.f;",
    "clearing them all")

# every fixed-size light array grows, padded with dark slots
for old, new, why in [
    ("float lpv[9]={px,3.3f,1.8f,  -4.5f,2.5f,3.0f,  4.5f,2.5f,3.0f};",
     "float lpv[NLIGHT*3]={px,3.3f,1.8f,  -4.5f,2.5f,3.0f,  4.5f,2.5f,3.0f,\n"
     "                                 0,-99,0, 0,-99,0, 0,-99,0};", "charSelect positions"),
    ("float lpv[9],lcv[9];", "float lpv[NLIGHT*3]={0},lcv[NLIGHT*3]={0};", "the main loop's pair"),
    ("float llp[9]={kx+2.6f,3.4f,kz+2.2f,  kx-3.0f,2.2f,kz+1.4f,  kx-0.6f,2.8f,kz-3.2f};",
     "float llp[NLIGHT*3]={kx+2.6f,3.4f,kz+2.2f,  kx-3.0f,2.2f,kz+1.4f,  kx-0.6f,2.8f,kz-3.2f,\n"
     "                                        0,-99,0, 0,-99,0, 0,-99,0};", "lab positions"),
    ("float llc[9]={2.6f,2.6f,2.6f,        1.15f,1.15f,1.25f,      1.5f,1.5f,1.45f};",
     "float llc[NLIGHT*3]={2.6f,2.6f,2.6f,        1.15f,1.15f,1.25f,      1.5f,1.5f,1.45f,\n"
     "                                        0,0,0, 0,0,0, 0,0,0};", "lab colours"),
]:
    sub(old, new, why)

# the scene rig's two arrays
s = s.replace("float lp3[9]={", "float lp3[NLIGHT*3]={", 2)
s = s.replace("float lc3[9]={", "float lc3[NLIGHT*3]={", 2)
print("   ok: the scene rig's arrays")
# ...and they are only three long, so pad the initialisers
s = re.sub(r"(float lp3\[NLIGHT\*3\]=\{[^;]*?)\};", r"\1, 0,-99,0, 0,-99,0, 0,-99,0 };", s)
s = re.sub(r"(float lc3\[NLIGHT\*3\]=\{[^;]*?)\};", r"\1, 0,0,0, 0,0,0, 0,0,0 };", s)
print("   ok: padded the scene rig with dark slots")

# ---- every upload counts NLIGHT ------------------------------------------------------
n = len(re.findall(r"glUniform3fv\(([^,]+),3,(l[a-z0-9]+)\)", s))
s = re.sub(r"glUniform3fv\(([^,]+),3,(l[a-z0-9]+)\)", r"glUniform3fv(\1,NLIGHT,\2)", s)
print("   ok: %d uploads now pass NLIGHT" % n)

# the shader's own clamp on how many it reads, if any literal 3 remains near lp[
left = s.count("lp[3]") + s.count("i<3;i++){ vec3 D=lp")
print("   check: %d literal-3 light references left (was %d)" % (left, n_before))

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
