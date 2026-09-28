"""patch_ground.py -- THE ACTORS STAND ON THE GROUND.

jacob: "the actors ... dont seem to be in the same world as the background, they are not
really walking on the surfaces".

⛔ AND NOTHING EVER GROUNDED THEM. A body is placed at `A->y - P->mny*eff`, where mny is
the lowest bone of the REST POSE. That plants a T-pose perfectly and then never looks
again: whatever the clip does to the legs, the body's height is still answering to a pose
it is not in. A retargeted walk -- which is most of the film, and which arrives
rotation-only -- therefore slides across the surface at rest height instead of standing on
it. Only the SKATERS were ever planted, because a rider who is not on the deck is
obviously wrong, and a pedestrian who is two centimetres into the pavement is only subtly
wrong. Subtly wrong, on a hundred bodies, is what "not in the same world" looks like.

THE FIX IS THE ONE THE SKATER ALREADY USES, generalised. After FK, measure the lowest
foot; the model is then lifted by exactly how far that foot has moved from the rest sole,
so the planted foot lands on the ground whatever the pose is doing:

    mdl[13] += (P->mny - lowestFootNow) * eff

⛔ THREE POSES MUST NOT GET IT, and each one is a real clip in this film:
  * A CRAWL OR A LAY. donorHipDrop already lowers those bodies onto the floor, and a foot
    plant on top of it would fight that correction -- the "feet" of a body on its front
    are above its pelvis. Gated off wherever hipDrop is doing its job.
  * ANYTHING AIRBORNE. A->air is a deliberate lift; planting cancels it exactly, the same
    way the skater's plant had to release over the deck.
  * A WILD FRAME. The correction is clamped to a third of the body's own height, so one
    bad frame of a retarget cannot teleport a citizen.

⭐ AND IT IS MEASURED, NOT TUNED: mny and the foot both come out of the same FK, in the
same model units, so there is no number here to get wrong on a new body.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"

s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# actorDraw takes a "plant me on the ground" flag
sub("""                      WmsState*wms,float wdt,float skSig){""",
    """                      WmsState*wms,float wdt,float skSig,int groundIt){""",
    "actorDraw takes the grounding flag")

sub("""    M4 mdl; memcpy(mdl,model,64);
    if(sk&&restFoot<1e8f){""",
    """    M4 mdl; memcpy(mdl,model,64);
    /* ⭐ STAND ON THE GROUND. The model was placed so the REST pose's lowest bone sits at
     * the actor's y; nothing ever looked again, so an animated body floated or sank by
     * however far its legs had moved since. Lift by exactly that difference and the
     * planted foot lands on the surface, whatever the pose is doing. Same mechanism the
     * skater's deck plant uses -- it was simply never given to anybody else. */
    if(groundIt&&!sk&&P->footL>=0&&P->footR>=0){
        float fy9=(W[P->footL][13]<W[P->footR][13])?W[P->footL][13]:W[P->footR][13];
        float d9=(P->mny-fy9);
        /* a third of her height. The rest span is not stored, but P->scale was DERIVED
         * from it at load (scale = 3.15/(span*1.13)), so the span comes back out of it --
         * a measured number rather than a new constant to get wrong on the next body. */
        float span9=(P->scale>1e-6f)?(3.15f/(P->scale*1.13f)):3.f;
        float lim9=span9*0.33f;
        if(d9> lim9) d9= lim9;
        if(d9<-lim9) d9=-lim9;
        float eff9=sqrtf(model[0]*model[0]+model[1]*model[1]+model[2]*model[2]);
        mdl[13]+=d9*eff9; }
    if(sk&&restFoot<1e8f){""",
    "the general ground plant")

sub("""        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix,
                  &A->wms,g_actorDt,""",
    """        /* ⛔ NOT ON A GROUND CLIP AND NOT IN THE AIR. donorHipDrop already puts a crawl
         * or a lay on the floor -- the "feet" of a body on its front are above its
         * pelvis, and planting them would fight the correction that is already right.
         * A->air is a deliberate lift and a plant cancels it exactly. */
        int gnd=(!isk && A->air<=0.001f && hipDrop>-0.001f && hipDrop<0.001f);
        actorDraw(P,isk?-1:A->clip,isk?NULL:A->dnP,isk?-1:A->dnC,
                  A->phase,m,mModel,mPal,mBind,pal,isk?&sp:NULL,ft,pmix,
                  &A->wms,g_actorDt,""",
    "gate the plant")

sub("""                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f);""",
    """                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f,
                  gnd);""",
    "pass the flag")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
