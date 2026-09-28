"""patch_sunall.py -- the sun must reach EVERY program.

THE BUG, visible in every street wide since the sun landed: the near skater fully lit and
coloured, four more down the road as BLACK SILHOUETTES. It reads as a distance or shadow
problem and is neither.

`uSun`/`uSunC`/`uSky` were uploaded once per frame to the single program `AP` points at --
PR[1] or PRD[1]. But USEPROG(i) switches to PR[i] for every mesh, choosing index 0 or 1
off `meshMatFlags & 4`, and a uniform is PER-PROGRAM state. Any body whose material does
not set that bit therefore drew through a program that had never seen uSun, evaluated
`uSun.w > 0.0` as false, and took the club's DARK-ROOM constant branch:

    base*(l*0.42 + 0.13 + li)

which with no point light nearby is a body at roughly an eighth of its brightness. On a
black stage that is invisible; on a sunlit street it is a silhouette standing next to a
person.

So it is not per-distance, it is PER-PACK -- which is why it looked like fog. Push the
light to all four programs once per frame, right where the day curve recomputes it.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
CR, LF = chr(13), chr(10)
CRLF = CR + LF
s = io.open(P, encoding="utf-8", newline="").read()
WAS_CRLF = CRLF in s
if WAS_CRLF:
    s = s.replace(CRLF, LF)
orig = s


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


a = """        if(g_tod>=0.f){   /* Z6: the day runs, and the light is recomputed every frame */
            if(g_todRate!=0.f) g_tod+=g_todRate*(float)((g_fixedStepMs>0.0)?g_fixedStepMs*0.001:0.0166);
            todApply(g_tod); }"""
b = a + """
        /* ⛔ AND IT HAS TO REACH EVERY PROGRAM. A uniform is per-program state, the sun
         * was only ever uploaded to the one AP points at (index 1), and USEPROG picks
         * index 0 for any mesh whose material flags lack bit 2 -- that program evaluated
         * `uSun.w > 0.0` as FALSE and drew the body with the club's dark-room constants.
         * On a black stage that is invisible; on a sunlit street it is the four black
         * skaters standing down the road from a fully lit one. */
        { GLint wasP=0; glGetIntegerv(GL_CURRENT_PROGRAM,&wasP);
          for(int pi9=0;pi9<2;pi9++){
              Prog*Q9[2]={&PR[pi9],&PRD[pi9]};
              for(int k9=0;k9<2;k9++){ Prog*R9=Q9[k9];
                  if(!R9->prog) continue;
                  glUseProgram(R9->prog);
                  if(R9->uSun >=0) glUniform4fv(R9->uSun ,1,g_sun);
                  if(R9->uSunC>=0) glUniform3fv(R9->uSunC,1,g_sunC);
                  if(R9->uSky >=0) glUniform3fv(R9->uSky ,1,g_sky); } }
          if(wasP) glUseProgram((GLuint)wasP); curProg=-1; }"""
sub(a, b, "the sun reaches every program, every frame")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
