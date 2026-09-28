"""patch_cast2.py -- stage two: LOAD, CLOCK AND DRAW the rest of the cast.

Stage one gave the scene system a cast array whose slot 1 is the old partner. This wires
the rest of it:

  * castInit() at startup, because a zero-filled struct would stand every extra INSIDE
    the hero, facing the same way, playing clip 0.
  * ctl `partner2=<name>` loads slot 2 the same way `partner=` loads slot 1 -- through
    the same code, by lending it g_castI.
  * the whole cast runs on the HERO'S CLOCK, which is the rule the two-body system already
    had and the reason a two-body move reads as one performance instead of two.
  * the draw is a LOOP over the slots. Every existing line inside labDrawPartner resolves
    through g_castI, so drawing a third body is the same function called again.

⛔ AND SLOT 2 MUST NOT DEFAULT TO SLOT 1'S MARK. gap 1.60 / side 0 / yaw 180 is directly in
front of the hero, which is exactly where slot 1 is standing. A third body inheriting it
is a body inside another body. Slot 2 defaults to a third point of a triangle instead --
which is also just correct blocking for three people talking.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
BSN = chr(92) + "n"
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


# ---------------------------------------------------------------- defaults per slot
sub("""        g_cast[i].gap=1.60f; g_cast[i].side=0.0f; g_cast[i].yaw=180.0f;""",
    """        /* ⛔ NOT ALL THE SAME MARK. 1.60/0/180 is directly in front of the hero, which
         * is where slot 1 stands -- a third body inheriting it is a body inside another
         * body. Later slots take the other points of a triangle, which is also simply
         * correct blocking for three people in a conversation. */
        g_cast[i].gap =(i<=1)?1.60f:1.45f;
        g_cast[i].side=(i<=1)?0.00f:((i&1)?-1.25f:1.25f);
        g_cast[i].yaw =(i<=1)?180.0f:((i&1)?214.0f:146.0f);""",
    "per-slot placement defaults")

# ---------------------------------------------------------------- init at startup
sub("""        g_wmsOff = getenv("REVA_NOWMS")?1:0;""",
    """        castInit();   /* ⛔ before anything can load a partner: a zero struct is a body inside a body */
        g_wmsOff = getenv("REVA_NOWMS")?1:0;""",
    "castInit at startup")

# ---------------------------------------------------------------- the whole cast is clocked
sub("""            if(g_labPartner&&g_labPClip>=0){
                if(g_labWphReset) g_labPPhase=0.0;
                else if(!g_labFreeze) g_labPPhase+=dt*g_labPartner->clips[g_labPClip].fps*rate; }""",
    """            /* ⭐ AND THE WHOLE CAST RUNS ON IT, not just the partner. A scene is one
             * performance; give any body its own clock and it drifts out of the take
             * within seconds. The loop lends each slot g_castI and the existing lines
             * resolve to it unchanged. */
            { int sv7=g_castI;
              for(g_castI=1;g_castI<SCN_MAXCAST;g_castI++)
                if(g_labPartner&&g_labPClip>=0){
                    if(g_labWphReset) g_labPPhase=0.0;
                    else if(!g_labFreeze) g_labPPhase+=dt*g_labPartner->clips[g_labPClip].fps*rate; }
              g_castI=sv7; }""",
    "the cast runs on the hero's clock")

# ---------------------------------------------------------------- draw them all
sub("""            if(g_mode==MODE_ANIM&&g_labPartner&&(g_labPClip>=0||g_labPDnP)&&skSolid)
                labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,mModel,mPal,mBind,pal,labDq,mDqS);""",
    """            /* ⭐ THE CAST, DRAWN. Every line inside labDrawPartner resolves through
             * g_castI, so a third body is the same function called again -- which is the
             * whole point of the array and the reason nothing else had to change. */
            if(g_mode==MODE_ANIM&&skSolid){
                int sv8=g_castI;
                for(g_castI=1;g_castI<SCN_MAXCAST;g_castI++)
                    if(g_labPartner&&(g_labPClip>=0||g_labPDnP))
                        labDrawPartner(g_labPartner,g_labPClip,g_labPPhase,model,mModel,mPal,mBind,pal,labDq,mDqS);
                g_castI=sv8; }""",
    "draw the whole cast")

# ---------------------------------------------------------------- ctl: partner2=
old_ld = 'if((q=strstr(buf,"partner="))){ char nm3[64]; int k3=0; q+=8;'
new_ld = ('/* ⭐ partner2= loads slot 2 THROUGH THE SAME CODE, by lending it g_castI --\n'
          '                         * including the hero-style mesh sizing below, which is the thing that\n'
          '                         * makes two bodies share a frame at the same scale. */\n'
          '                        { const char*q2=strstr(buf,"partner2=");\n'
          '                          g_castI = q2 ? 2 : 1;\n'
          '                          if(q2) q=(char*)q2+9; else q=strstr(buf,"partner="), q=q?q+8:NULL; }\n'
          '                        if(q&&*(q-1)!=0){ char nm3[64]; int k3=0;')
sub("""                        if((q=strstr(buf,"partner="))){ char nm3[64]; int k3=0; q+=8;""",
    """                        /* ⭐ partner2= LOADS SLOT 2 THROUGH THE SAME CODE, by lending it
                         * g_castI -- including the hero-style mesh sizing below, which is
                         * what lets two bodies share a frame at the same scale. Slot 1 is
                         * restored at the end of the block. */
                        g_castI = strstr(buf,"partner2=") ? 2 : 1;
                        if((q=strstr(buf,"partner2="))?(q+=9,1):((q=strstr(buf,"partner="))?(q+=8,1):0)){ char nm3[64]; int k3=0;""",
    "partner2= loads slot 2")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched (stage 2)")
