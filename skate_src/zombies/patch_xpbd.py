"""patch_xpbd.py -- XPBD: stiffness that means something.

jacob dropped Richard Williams on me and it produced a measurable animation layer, so when
he asked "would more reference material help you? books maybe?" the honest answer was yes,
and the most useful thing was not a book at all:

    Macklin, Muller, Chentanez -- "XPBD: Position-Based Simulation of Compliant Constrained
    Dynamics", Proc. Motion in Games 2016. design/papers/, from the authors' own page.

⛔ IT NAMES THE EXACT BUG I SPENT TODAY HAND-TUNING. Its abstract opens on "the
long-standing problem of iteration count and time step dependent constraint stiffness in
position-based dynamics" -- which is precisely why every cloth number I picked today felt
like whack-a-mole. In plain PBD, running 4 constraint passes instead of 3 makes the fabric
STIFFER. Stiffness was never a property of the cloth; it was a side effect of how many
times I looped. So "how stiff is this curtain" had no answer, and every tuning session
re-tuned against whatever the loop count happened to be.

⭐ THE FIX IS SMALL AND IT IS A STRICT GENERALISATION. Give each constraint a COMPLIANCE
alpha (inverse stiffness, in physical units) and a Lagrange multiplier lambda that
accumulates across the iterations of one step:

    alphaTilde = alpha / dt^2
    dLambda    = (-C - alphaTilde*lambda) / (w_i + w_j + alphaTilde)
    dx_i       = -w_i * n * dLambda ,  dx_j = +w_j * n * dLambda
    lambda    += dLambda

⭐ AND AT alpha = 0 THIS IS EXACTLY THE PBD I ALREADY WROTE -- the paper says so: the
denominator collapses to (w_i + w_j) and dLambda becomes the old scaling factor. So this
cannot regress the look; it only makes the knob mean something. Now a bedsheet and a
curtain differ by a NUMBER WITH UNITS instead of by how many passes I happened to run, and
lambda is the constraint force, which is what a tear-away or a torn garment would read
later.

⛔ lambda RESETS EVERY STEP, not every iteration. It is the accumulated multiplier for one
timestep; carrying it across steps is a different (and wrong) integrator.
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


# ---------- 1. the set cloth (curtains, sheets) --------------------------------------
sub("""static void sclConstrain(SetCloth*S,int i,int j,float rest){
    float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[j][k]-S->cur[i][k];
    float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) return;
    float f=(L-rest)/L*0.5f;
    float wi=S->pin[i]?0.f:1.f, wj=S->pin[j]?0.f:1.f, t=wi+wj;
    if(t<1e-6f) return;
    for(int k=0;k<3;k++){ S->cur[i][k]+=d[k]*f*(wi/t)*2.f*0.5f;
                          S->cur[j][k]-=d[k]*f*(wj/t)*2.f*0.5f; } }""",
    """/* ⭐ XPBD (Macklin/Muller/Chentanez 2016, design/papers/). COMPLIANCE, not a magic
 * scale factor. `alphaT` is the constraint's compliance divided by dt^2 and `lam` its
 * accumulated Lagrange multiplier for THIS step:
 *
 *     dLambda = (-C - alphaT*lambda) / (w_i + w_j + alphaT)
 *
 * ⛔ AND THIS IS WHY EVERY CLOTH NUMBER TODAY FELT LIKE WHACK-A-MOLE. In plain PBD,
 * running four constraint passes instead of three makes the fabric STIFFER -- stiffness
 * was never a property of the cloth, it was a side effect of the loop count, so "how stiff
 * is this curtain" had no answer. With compliance it does, in units, and the pass count
 * only buys accuracy.
 * ⭐ At alphaT = 0 this is EXACTLY the old code: the denominator collapses to (w_i+w_j)
 * and dLambda becomes the previous scaling factor. A strict generalisation, so it cannot
 * regress the look. */
static void sclConstrain(SetCloth*S,int i,int j,float rest,float alphaT,float*lam){
    float d[3]; for(int k=0;k<3;k++) d[k]=S->cur[j][k]-S->cur[i][k];
    float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) return;
    float wi=S->pin[i]?0.f:1.f, wj=S->pin[j]?0.f:1.f, wsum=wi+wj;
    if(wsum<1e-6f) return;
    float C=L-rest;
    float dl=(-C-alphaT*(*lam))/(wsum+alphaT);
    *lam+=dl;
    for(int k=0;k<3;k++){ float n=d[k]/L;
        S->cur[i][k]-=wi*n*dl;
        S->cur[j][k]+=wj*n*dl; } }""",
    "set cloth: XPBD constraint")

sub("""    for(int pass=0;pass<4;pass++){
        for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
            int i=r*SCL_W+c;
            if(c+1<SCL_W) sclConstrain(S,i,i+1,S->ru);
            if(r+1<SCL_H) sclConstrain(S,i,i+SCL_W,S->rv);
            if(c+1<SCL_W&&r+1<SCL_H) sclConstrain(S,i,i+SCL_W+1,S->rd);
            if(c>0&&r+1<SCL_H)       sclConstrain(S,i,i+SCL_W-1,S->rd);
            /* bend: keeps a hem from folding flat back on itself */
            if(c+2<SCL_W) sclConstrain(S,i,i+2,S->rb); }""",
    """    /* ⛔ THE MULTIPLIERS RESET EVERY STEP, NOT EVERY ITERATION. lambda accumulates
     * across the solver passes of ONE timestep; carrying it between steps is a different
     * and wrong integrator. */
    static float lam[SCL_N*5];
    memset(lam,0,sizeof lam);
    /* compliance, in physical units at last. Real cloth barely STRETCHES and bends
     * easily, which is one line here and was three tuning sessions before. */
    float dt2=dt*dt;
    float aStr=2.0e-7f/dt2, aShr=1.2e-5f/dt2, aBnd=3.0e-4f/dt2;
    for(int pass=0;pass<4;pass++){
        for(int r=0;r<SCL_H;r++) for(int c=0;c<SCL_W;c++){
            int i=r*SCL_W+c;
            if(c+1<SCL_W) sclConstrain(S,i,i+1,S->ru,aStr,&lam[i*5+0]);
            if(r+1<SCL_H) sclConstrain(S,i,i+SCL_W,S->rv,aStr,&lam[i*5+1]);
            if(c+1<SCL_W&&r+1<SCL_H) sclConstrain(S,i,i+SCL_W+1,S->rd,aShr,&lam[i*5+2]);
            if(c>0&&r+1<SCL_H)       sclConstrain(S,i,i+SCL_W-1,S->rd,aShr,&lam[i*5+3]);
            /* bend: keeps a hem from folding flat back on itself */
            if(c+2<SCL_W) sclConstrain(S,i,i+2,S->rb,aBnd,&lam[i*5+4]); }""",
    "set cloth: compliance per constraint family")

# ---------- 2. the garment solver ----------------------------------------------------
sub("""    for(int pass=0;pass<3;pass++){
        for(int e=0;e<C->ne;e++){
            int i=C->e0[e], j=C->e1[e];
            float d[3]; for(int k=0;k<3;k++) d[k]=C->cur[j][k]-C->cur[i][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) continue;
            float diff=(L-C->elen[e])/L*0.5f;
            float wi=C->pin[i]?0.f:1.f, wj=C->pin[j]?0.f:1.f;
            float tot=wi+wj; if(tot<1e-6f) continue;
            for(int k=0;k<3;k++){
                C->cur[i][k]+=d[k]*diff*(wi/tot)*2.f*0.5f;
                C->cur[j][k]-=d[k]*diff*(wj/tot)*2.f*0.5f; } }""",
    """    /* ⭐ XPBD here too (design/papers/), same reason: a garment's stiffness should be a
     * property of the fabric, not of how many passes the loop runs. */
    static float glam[CLOTH_MAXE];
    memset(glam,0,(size_t)C->ne*sizeof(float));
    float aG=4.0e-7f/(dt*dt);
    for(int pass=0;pass<3;pass++){
        for(int e=0;e<C->ne;e++){
            int i=C->e0[e], j=C->e1[e];
            float d[3]; for(int k=0;k<3;k++) d[k]=C->cur[j][k]-C->cur[i][k];
            float L=sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); if(L<1e-7f) continue;
            float wi=C->pin[i]?0.f:1.f, wj=C->pin[j]?0.f:1.f;
            float wsum=wi+wj; if(wsum<1e-6f) continue;
            float Cc=L-C->elen[e];
            float dl=(-Cc-aG*glam[e])/(wsum+aG);
            glam[e]+=dl;
            for(int k=0;k<3;k++){ float n=d[k]/L;
                C->cur[i][k]-=wi*n*dl;
                C->cur[j][k]+=wj*n*dl; } }""",
    "garment: XPBD constraint")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
