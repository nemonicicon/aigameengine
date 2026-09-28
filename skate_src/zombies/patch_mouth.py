"""patch_mouth.py -- a mouth opens from the MIDDLE, not all at once across its width.

jacob: "revas mouth is messedup."

MEASURED off the face rig at jaw 0.0 / 0.35 / 0.75. The aperture's two axes do not scale
together:

    float aH = uOpen*0.20;                       // vertical half-height: scales with open
    float ax = vFace.y/0.78;                     // LATERAL: a constant 0.78 half-widths
    float ay = (vFace.x + aH*0.60)/(aH*1.20);

So the hole is always the full width of her mouth and only its HEIGHT grows. At uOpen 0.35
that is a lens 0.78 half-widths wide and 0.084 tall -- a letterbox slit right across her
face, which is exactly what a lipless slash looks like. And speech lives in that range:
the envelope spends most of a line between 0.2 and 0.5, so the slit IS her talking face.

A real mouth parts at the centre first and the corners are the LAST thing to open --
that is what the orbicularis does. So the lateral extent scales too, from just over half
at a whisper to full at a shout, and the two axes grow together.

Also: the ellipse sat 0.6*aH below the lip line and reached 1.2*aH each way, so it
extended ABOVE the lip line at every opening. The upper lip does not move when a jaw
drops -- only the lower one travels -- so the hole is biased further down and its top edge
now stays at the seam instead of climbing into her top lip.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)

Q = chr(34)


def line(t):
    return "      " + Q + t + Q + chr(10)


a = (line("    float aH=uOpen*0.20;" + chr(92) + "n") +
     line("    float ax=vFace.y/0.78;" + chr(92) + "n") +
     line("    float ay=(vFace.x+aH*0.60)/max(1e-4,aH*1.20);" + chr(92) + "n"))

b = ("""      /* ⛔ BOTH AXES SCALE, OR IT IS A LETTERBOX. The lateral extent used to be a
       * constant 0.78 half-widths while only the height grew with uOpen, so at a
       * conversational 0.35 the hole was the full width of her mouth and a twentieth of
       * a half-width tall -- a slit straight across her face. Speech sits in exactly that
       * range. A mouth parts at the CENTRE first and the corners open last, so the width
       * runs from just over half at a whisper to full at a shout.
       * And the bias is deeper: only the LOWER jaw travels, so the opening must not climb
       * into the upper lip. It sits below the seam and stays there. */
""" +
     line("    float aH=uOpen*0.22;" + chr(92) + "n") +
     line("    float aW=0.78*mix(0.52,1.0,clamp(uOpen*1.25,0.0,1.0));" + chr(92) + "n") +
     line("    float ax=vFace.y/max(1e-4,aW);" + chr(92) + "n") +
     line("    float ay=(vFace.x+aH*0.92)/max(1e-4,aH*1.06);" + chr(92) + "n"))

# ⛔ THE APERTURE SOURCE IS DUPLICATED. The engine builds three mesh fragment shaders
# (the crowd's, the aperture variant fsO, and the plain fsC) and the mouth block is
# pasted into more than one of them -- so a fix applied once leaves the other copy to
# render the old slit on whichever program a mesh happens to pick. Replace them all.
n = s.count(a)
if n < 1:
    print("!! anchor not found")
    sys.exit(1)
s = s.replace(a, b)
print("   patched %d copies of the aperture" % n)
io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("patched: the aperture opens from the middle and stays below the seam")
