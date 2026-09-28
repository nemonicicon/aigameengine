"""patch_cast4.py -- stage four: WHO IS BEING ANSWERED, and slot 2's blocking.

Stage three taught the shot solver to look up marks by cast index and to take the listener
from `g_scnPrevSpk`. Nothing ever set it, and nothing ever moved slot 2 off her load-time
mark. Both live in the same place -- the per-line staging block -- so they go in together.

⭐ THE PREVIOUS SPEAKER IS TRACKED ON THE LINE CHANGE, not per frame. A silence belongs to
whoever just spoke (that rule already exists here), so the listener must not change during
a beat either -- otherwise the camera would swing mid-pause, which is the one thing a held
shot cannot do.

⭐ AND SLOT 2 EASES TO HER MARK exactly as slot 1 does, through the same one-pole, because
a body that teleports between blocking positions reads as a cut even when there isn't one.
Her keys are sticky: a three-hander sets p2side once and she stays there.

⛔ THE MINIMUM SEPARATION HAS TO INCLUDE HER. The two-body clamp kept the pair from
standing inside one another; with three, slot 2 can be dropped inside EITHER of them. She
is pushed off both.
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


# ---- who is being answered, tracked on the LINE CHANGE ------------------------------
sub("""                    { static int held8=-1; int la8=scnActive();
                      if(la8>=0) held8=la8;""",
    """                    { static int held8=-1; int la8=scnActive();
                      /* ⭐ WHO IS BEING ANSWERED. Updated only when the LINE changes, never
                       * per frame: a silence belongs to whoever just spoke, so the listener
                       * must not change during a beat either -- a held shot cannot swing
                       * mid-pause. */
                      if(la8>=0&&la8!=held8&&held8>=0&&g_scnL[held8].spk!=g_scnL[la8].spk)
                          g_scnPrevSpk=g_scnL[held8].spk;
                      if(la8>=0) held8=la8;""",
    "track the previous speaker")

# ---- slot 2 eases to her mark, and is pushed out of the other two -------------------
sub("""                          g_labPGap += (tg      -g_labPGap )*k8;
                          g_labPYaw += (S8->pyaw-g_labPYaw )*k8;
                          g_labPSide+= (ts      -g_labPSide)*k8;""",
    """                          g_labPGap += (tg      -g_labPGap )*k8;
                          g_labPYaw += (S8->pyaw-g_labPYaw )*k8;
                          g_labPSide+= (ts      -g_labPSide)*k8;
                          /* ⭐ AND SLOT 2 EASES TO HERS, through the same one-pole -- a body
                           * that teleports between blocking marks reads as a cut when there
                           * is not one. Her keys are sticky, so a three-hander sets p2side
                           * once and she stays there.
                           * ⛔ AND THE SEPARATION CLAMP HAS TO INCLUDE HER: the two-body
                           * version kept the pair out of each other, and with three she can
                           * be dropped inside EITHER. She is pushed off both. */
                          if(g_cast[2].pk){
                              float t2g=S8->p2gap, t2s=S8->p2side;
                              if(g_stageMinSep>0.f){
                                  float r2=sqrtf(t2g*t2g+t2s*t2s);              /* off the hero */
                                  if(r2<g_stageMinSep&&r2>1e-4f){ float f=g_stageMinSep/r2; t2g*=f; t2s*=f; }
                                  else if(r2<=1e-4f) t2g=g_stageMinSep;
                                  { float dg=t2g-tg, ds=t2s-ts;                 /* off slot 1 */
                                    float r3=sqrtf(dg*dg+ds*ds);
                                    if(r3<g_stageMinSep&&r3>1e-4f){ float f=(g_stageMinSep-r3)/r3;
                                        t2g+=dg*f; t2s+=ds*f; }
                                    else if(r3<=1e-4f) t2s=ts+g_stageMinSep; } }
                              g_cast[2].gap += (t2g      -g_cast[2].gap )*k8;
                              g_cast[2].yaw += (S8->p2yaw-g_cast[2].yaw )*k8;
                              g_cast[2].side+= (t2s      -g_cast[2].side)*k8; }""",
    "slot 2 eases to her mark, clamped off both")

# ---- and her clip, resolved the same way slot 1's is --------------------------------
sub("""                            if(g_labPartner&&pcl[0]){""",
    """                            /* ⭐ SLOT 2'S CLIP, resolved exactly as slot 1's is -- her own
                             * pack first, then the bank through the canonical transfer --
                             * by lending her g_castI for the length of the lookup. */
                            if(g_cast[2].pk&&S8->p2clip[0]){
                                int sv6=g_castI; g_castI=2;
                                const char*c2=S8->p2clip;
                                int f6=-1; for(unsigned c6=0;c6<g_labPartner->nClips;c6++)
                                    if(!strcmp(packStr(g_labPartner,g_labPartner->clips[c6].name),c2)){ f6=(int)c6; break; }
                                if(f6>=0){ if(f6!=g_labPClip){ g_labPClip=f6; g_labPPhase=0.0; }
                                           g_labPDnP=NULL; g_labPDnC=-1; }
                                else { int bc6=-1; Pack*bp6=bankFind(c2,&bc6);
                                    if(bp6&&g_labPartner->cbOK&&g_labPartner->pbOK>=10){ g_labPDnP=bp6; g_labPDnC=bc6; } }
                                g_castI=sv6; }
                            if(g_labPartner&&pcl[0]){""",
    "slot 2's clip resolves like slot 1's")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched (stage 4)")
