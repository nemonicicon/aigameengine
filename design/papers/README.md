# The reference shelf

jacob: *"you know these books, are they available for free?"* → *"yes, go for it, pull what
you can, and consider free alternatives to paid ones."*

Precedent: he dropped Richard Williams' *The Animator's Survival Kit* with no message, and
it produced a measurable animation layer (`wmsApply`, head-top travel 2px → 14px on
`anim_bench/weightcheck.py`). So reference material is not decoration here — it lands as
code and gets measured.

---

## Pulled, and applied

### `XPBD_Macklin_Muller_Chentanez_2016.pdf`
Macklin, Müller, Chentanez — *XPBD: Position-Based Simulation of Compliant Constrained
Dynamics*, Motion in Games 2016. Free from the authors' own page.

⛔ **It names the exact bug I spent a day hand-tuning.** Its abstract opens on "the
long-standing problem of iteration count and time step dependent constraint stiffness in
position-based dynamics" — which is why every cloth number felt like whack-a-mole. In plain
PBD, four constraint passes instead of three makes the fabric *stiffer*. Stiffness was
never a property of the cloth; it was a side effect of the loop count.

⭐ **Applied** to both solvers (`sclConstrain` for curtains/sheets, `clothStep` for the
garment). Compliance α (inverse stiffness) and a per-constraint Lagrange multiplier λ:

    alphaTilde = alpha / dt^2
    dLambda    = (-C - alphaTilde*lambda) / (w_i + w_j + alphaTilde)
    dx_i = -w_i*n*dLambda ;  dx_j = +w_j*n*dLambda ;  lambda += dLambda

⭐ **Verified, not assumed.** `REVA_PBD=1` sets α=0, which the paper says reduces exactly
to PBD — and it does: **PBD@6 vs XPBD@6 = 0.051% of pixels.** That is the implementation
being correct.

⛔ **And the honest result of the iteration test.** On `sets/clothiter.set` (one curtain,
dead air, settled):

| solver | 2 passes vs 6 passes |
|---|---|
| PBD (α=0)   | 3.463% |
| XPBD (α>0)  | 3.114% |

Barely a win — because I chose *near-inextensible* compliance, and at α≈0 both solvers are
simply under-converged at 2 passes. **That 3% is convergence error, not the stiffness/
iteration coupling the paper targets.** The real gain is that compliance is now a physical
constant per fabric instead of a number that moves when the loop count changes, and λ is
the constraint force — which is what a *tearing* garment would read.

⛔ **The first version of that test measured the wrong thing.** It ran on `clothtest.set`,
which gusts, and reported 10.0%. A gusty cloth is chaotic: any two runs diverge, whatever
the solver. `clothiter.set` exists because stiffness is a property of the *settled* shape.

### `Stam_RealTimeFluidDynamicsForGames_GDC2003.pdf`
Jos Stam, GDC 2003. Free from the author's page.

⛔ **NOT applied, and it should not be.** Stam solves a *velocity field* — advect, diffuse,
project — which is the right model for smoke, dust and fire. A swimming pool is a
**surface**: you never see inside the volume, so a height field solving the wave equation
is both cheaper and more accurate for what the camera sees. Kept on the shelf for the day
the film wants smoke or fog, which the zombie material plausibly will.

---

## Free, not yet pulled

- **Ten Minute Physics** — Müller's own free video + source series (cloth, XPBD, fluids).
  Code, not prose; probably the highest-value thing left on this list.
- **GPU Gems 1–3** — NVIDIA put all three online free. Water and cloth chapters.
- **PBRT (*Physically Based Rendering*)** — the whole book, free and official, at
  pbr-book.org.
- **Íñigo Quílez's articles**, **Scratchapixel**, **The Book of Shaders** — free, and iq's
  material is directly on point for the procedural `skyColor()` just written.

## Paid — and the legitimate free route

*The Five C's of Cinematography* (Mascelli), *In the Blink of an Eye* (Murch), *Film
Directing Shot by Shot* (Katz) and *The Animator's Survival Kit* (Williams) are in
copyright and in print.

⭐ **The Internet Archive lends them.** Controlled digital lending is a real library loan —
free with an account, one borrower at a time, legitimate. `archive.org/details/
fivecsofcinemato0000masc` is Mascelli. Libraries carry all four; Murch's is ~130 pages.

Free and genuinely good in the same space: **Every Frame a Painting** (video essays on
staging and cutting), **No Film School's** free curriculum, **Class Central's** free
cinematography course listings.

⛔ **This is the half where the work is weakest.** The physics side is entirely free and
papers beat books there anyway — a 2006 paper with pseudocode is more actionable than a
chapter of prose. The film-craft side is the paid half, and `design/406_qc_pass.md` already
showed coverage and cutting are the thinnest layer in the whole film.
