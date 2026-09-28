"""patch_actorlook.py -- CAPABILITY F: a set actor can look at something.

A set actor plays one clip along a path with cues and cannot react, notice, or turn
toward anything. Forty background bodies all face the direction they were placed in, for
the whole take, whatever happens in front of them -- which is why the street reads as a
diorama rather than a place. In act III the mail carrier goes down in full sun and not one
of the eleven people watching turns their head, because there is no mechanism for a head
to turn.

    actor <pack> <clip> x y z yaw scale ...
    look <x> <y> <z>          # the line AFTER an actor, like `path` and `cue`
    look camera               # ...or at the lens, wherever it is

⭐ IT IS THE SAME LAYER THE SCENE PAIR ALREADY USES. scnHeadLayer turns the hero's skull at
whoever is speaking; this is that, aimed at a world point instead of a person, on a body
that has no scene. Neck and head split the angle the way a real neck does -- the head
carries about two thirds and the neck the rest, so it reads as a person looking rather
than a turret slewing.

⛔ CLAMPED, AND THE CLAMP IS THE POINT. A head has about 70 degrees of yaw before the
shoulders have to follow. Past that an actor should TURN, and turning is a path, not a
look -- so the aim saturates instead of unscrewing.
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


sub("                 WmsState wms;                 /* the Williams layer's memory for this body */",
    """                 WmsState wms;                 /* the Williams layer's memory for this body */
                 float look[3]; int hasLook;   /* ⭐ a world point this body turns its head toward */""",
    "SetActor carries a look target")

sub("            memset(&A->wms,0,sizeof A->wms);",
    """            memset(&A->wms,0,sizeof A->wms);
            A->hasLook=0;""",
    "no look by default")

sub("""        if(!strncmp(ln,"mark ",5)&&g_setNMk<SET_MAXMARK){""",
    """        /* ⭐ `look x y z` (or `look camera`) on the line after an actor. Same shape as
         * `path` and `cue`: it belongs to the actor above it. */
        if(!strncmp(ln,"look ",5)&&g_setNA>0){
            SetActor*A=&g_setA[g_setNA-1];
            if(strstr(ln,"camera")){ A->hasLook=2; }
            else if(sscanf(ln,"look %f %f %f",&A->look[0],&A->look[1],&A->look[2])==3) A->hasLook=1;
            continue; }
        if(!strncmp(ln,"mark ",5)&&g_setNMk<SET_MAXMARK){""",
    "parse look lines")

# ---- the layer, applied where the clip has already been sampled ---------------------
sub("""    /* ⭐ THE WILLIAMS LAYER needs the pose MEASURED before it can comment on it""",
    """    /* ⭐ SHE LOOKS AT IT. The same idea scnHeadLayer uses for the scene pair, aimed at a
     * world point instead of a person, on a body that has no scene. Neck and head split
     * the angle the way a real neck does -- the head takes about two thirds.
     * ⛔ CLAMPED, AND THE CLAMP IS THE POINT: a head has ~70 degrees before the shoulders
     * must follow, and past that an actor should TURN, which is a path and not a look. So
     * it saturates rather than unscrewing. */
    if(lookAt){
        float bx=model[12], bz=model[14];
        float dx=lookAt[0]-bx, dz=lookAt[2]-bz;
        float want=atan2f(dx,dz);
        float body=atan2f(model[8],model[10]);       /* the body's own facing */
        float d=want-body;
        while(d> 3.14159265f) d-=6.28318531f;
        while(d<-3.14159265f) d+=6.28318531f;
        const float MAXY=1.22f;                       /* ~70 degrees */
        if(d> MAXY) d= MAXY; if(d<-MAXY) d=-MAXY;
        int nk=P->pb[4], hd=P->pb[5];
        float off[4];
        if(hd>=0){ qaxis(off,0,1,0,d*0.66f); qmul(Q[hd],Q[hd],off); }
        if(nk>=0){ qaxis(off,0,1,0,d*0.34f); qmul(Q[nk],Q[nk],off); } }
    /* ⭐ THE WILLIAMS LAYER needs the pose MEASURED before it can comment on it""",
    "the actor look layer")

sub("""                      WmsState*wms,float wdt,float skSig,int groundIt){""",
    """                      WmsState*wms,float wdt,float skSig,int groundIt,
                      const float*lookAt){""",
    "actorDraw takes a look target")

sub("""                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f,
                  gnd);""",
    """                  (float)(((i*2654435761u)^((unsigned)A->pk*40503u))%1000u)/1000.f,
                  gnd,
                  /* `look camera` resolves to wherever the lens is THIS frame */
                  A->hasLook==2 ? g_camPos : (A->hasLook==1 ? A->look : NULL));""",
    "pass the look target")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("crowd_kms.c patched")
