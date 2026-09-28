"""patch_substep.py -- SMALL STEPS: n substeps beat n iterations.

    Macklin, Storey, Lu, Terdiman, Chentanez, Jeschke, Muller -- "Small Steps in Physics
    Simulation", SCA 2019. design/papers/, free from the authors' own page.

⭐ THIS IS THE ANSWER TO A NUMBER I MEASURED YESTERDAY AND COULD NOT EXPLAIN. Landing XPBD
left a 3% residual between 2 and 6 solver passes on a settled curtain, and I wrote it down
honestly as convergence error without knowing what to do about it. The paper's abstract
says exactly what to do, and calls it surprising itself:

    "performing a single large time step with n constraint solver iterations is less
     effective than computing n smaller time steps, each with a single constraint solver
     iteration"

So the loop was the wrong SHAPE. Not

    integrate(dt);  for (4 passes) solve();

but

    for (4 substeps) { integrate(dt/4); solve once; }

⭐ IDENTICAL COST -- the same number of constraint solves per frame -- and the paper
reports significantly less constraint error AND significantly less damping. The damping
matters here as much as the error does: over-damped cloth is precisely what a curtain that
refuses to luff looks like.

⛔ AND THE COMPLIANCE MUST BE RESCALED PER SUBSTEP. alphaTilde = alpha/dt^2, so shrinking
dt by n multiplies alphaTilde by n^2. Miss that and every fabric silently becomes n^2 times
softer the moment substepping is switched on -- the constraint would still be "satisfied",
just against the wrong stiffness.
⛔ lambda resets every SUBSTEP, because each substep IS a time step now.
"""
import io

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
LF = chr(10)
CRLF = chr(13) + LF

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        raise SystemExit("!! anchor x%d for %s" % (n, why))
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- the set cloth: wrap the whole integrate+solve+collide in a substep loop --------
sub("""static void sclStep(SetCloth*S,float dt){
    if(dt>0.033f) dt=0.033f; if(dt<1e-4f) dt=1e-4f;""",
    """/* ⭐ SMALL STEPS (Macklin et al. SCA 2019, design/papers/). n substeps of dt/n with ONE
 * solve each beat one step of dt with n solves -- same cost, less constraint error, and
 * markedly less DAMPING, which is what a curtain that refuses to luff was suffering from.
 * REVA_CLOTHITER now names the substep count; it used to name the iteration count, and the
 * paper's whole point is that those are not the same thing. */
static void sclStep(SetCloth*S,float dtFull){
    if(dtFull>0.033f) dtFull=0.033f; if(dtFull<1e-4f) dtFull=1e-4f;
    static int NSUB=-1;
    if(NSUB<0){ const char*e=getenv("REVA_CLOTHITER"); NSUB=e?atoi(e):4;
                if(NSUB<1) NSUB=1; if(NSUB>16) NSUB=16; }
    for(int ss=0;ss<NSUB;ss++){
    float dt=dtFull/(float)NSUB;""",
    "set cloth: substep loop opens")

sub("""    /* ⛔ THE MULTIPLIERS RESET EVERY STEP, NOT EVERY ITERATION. lambda accumulates
     * across the solver passes of ONE timestep; carrying it between steps is a different
     * and wrong integrator. */
    static float lam[SCL_N*5];
    memset(lam,0,sizeof lam);
    /* compliance, in physical units at last. Real cloth barely STRETCHES and bends
     * easily, which is one line here and was three tuning sessions before. */
    float dt2=dt*dt;""",
    """    /* ⛔ lambda RESETS EVERY SUBSTEP, because a substep IS a time step now. */
    static float lam[SCL_N*5];
    memset(lam,0,sizeof lam);
    /* compliance, in physical units at last. Real cloth barely STRETCHES and bends
     * easily, which is one line here and was three tuning sessions before.
     * ⛔ AND alphaTilde = alpha/dt^2 USES THE SUBSTEP dt. Shrinking dt by n multiplies
     * alphaTilde by n^2, so forgetting this makes every fabric n^2 times softer the moment
     * substepping is turned on -- satisfied constraints, wrong stiffness. */
    float dt2=dt*dt;""",
    "set cloth: compliance uses the substep dt")

sub("""    static int NIT=-1;
    if(NIT<0){ const char*e=getenv("REVA_CLOTHITER"); NIT=e?atoi(e):4; if(NIT<1) NIT=1; if(NIT>16) NIT=16; }
    for(int pass=0;pass<NIT;pass++){
        for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){""",
    """    /* ⭐ ONE solve per substep. This used to be `for(pass=0;pass<NIT;pass++)`. */
    {
        for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){""",
    "set cloth: one solve per substep")

sub("""                else if(px<=pz)         S->cur[i][0]=B[0]+(dx<0?-B[3]:B[3]);
                else                    S->cur[i][2]=B[2]+(dz<0?-B[5]:B[5]); } } }
    /* normals from the grid itself, every frame -- the shape changes, so they must */""",
    """                else if(px<=pz)         S->cur[i][0]=B[0]+(dx<0?-B[3]:B[3]);
                else                    S->cur[i][2]=B[2]+(dz<0?-B[5]:B[5]); } } }
    }   /* end substep */
    /* normals from the grid itself, every frame -- the shape changes, so they must */""",
    "set cloth: substep loop closes")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
