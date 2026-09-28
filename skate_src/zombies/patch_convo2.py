"""patch_convo2.py -- let BOTH bodies in a scene borrow a clip from a bank.

The role-cast (`auto` -> speaker gestures, listener listens) needs x_convo1/x_convo2, and
neither principal owns them: they live in the mob1 bank. But the scene path had no way to
borrow --

  * the HERO's donor branch is hard-wired to `g_movesPk`, the authored-moves pack;
  * the PARTNER had no donor branch at all, only her own pack's clips.

So both get one, through the same canonical transfer the zombie bank uses. The hero's
existing behaviour is preserved by falling back to g_movesPk when no bank is named, so
every `ctl clip=` that worked before still works.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF
BS_N = chr(92) + "n"
P = r"C:\MyVeryOwnVideoGames\reva\reva_native\crowd_kms.c"
s = io.open(P, encoding="utf-8", newline="").read()
was = CRLF in s
if was:
    s = s.replace(CRLF, LF)


def sub(a, b, why):
    global s
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


sub("""static int   g_labDnC=-1;                      /* P5: ctl clip= found the clip in the MOVES pack, not the subject */""",
    """static int   g_labDnC=-1;                      /* P5: ctl clip= found the clip in the MOVES pack, not the subject */
/* ...or in a BANK. NULL keeps the old behaviour (the authored-moves pack), so every
 * ctl clip= that worked before still resolves exactly as it did. */
static Pack *g_labDnP=NULL;
/* and the PARTNER can borrow too -- she had no donor path at all, only her own clips,
 * which is why a scene could not put a conversation gesture on her. */
static Pack *g_labPDnP=NULL; static int g_labPDnC=-1;""",
    "hero + partner donor globals")

sub("""            else if(g_mode==MODE_ANIM&&g_labDnC>=0&&g_movesPk&&(unsigned)g_labDnC<g_movesPk->nClips){""",
    """            else if(g_mode==MODE_ANIM&&g_labDnC>=0&&(g_labDnP?g_labDnP:g_movesPk)&&
                    (unsigned)g_labDnC<(g_labDnP?g_labDnP:g_movesPk)->nClips){""",
    "hero donor honours the bank")

sub("""                if(g_labOnce&&!g_labWphReset&&rvWph>=(double)g_movesPk->clips[g_labDnC].frames-1.0){""",
    """                Pack*dnp9=g_labDnP?g_labDnP:g_movesPk;
                if(g_labOnce&&!g_labWphReset&&rvWph>=(double)dnp9->clips[g_labDnC].frames-1.0){""",
    "hero donor frame count")

sub("""                else { hdP=g_movesPk; hdC=g_labDnC; want=-1; rate=1.0f; skHold=-1.f;""",
    """                else { hdP=dnp9; hdC=g_labDnC; want=-1; rate=1.0f; skHold=-1.f;""",
    "hero plays the borrowed clip")

# the partner: a donor branch in labDrawPartner, right where the walk borrow already lives
sub("""    /* ⭐ SHE WALKS TO HER MARK. Above this the staged clip has already been sampled into""",
    """    /* A STAGED CLIP SHE DOES NOT OWN. x_convo1/x_convo2 live in the mob1 bank, not in
     * her pack, so the scene's role-cast reaches her the same way the zombie clips reach
     * a citizen: pose the bank's rig, transfer the canonical bones. */
    if(g_labPDnP&&g_labPDnC>=0&&(unsigned)g_labPDnC<g_labPDnP->nClips)
        donorPoseF(g_labPDnP,g_labPDnC,phase,P,Q);
    /* ⭐ SHE WALKS TO HER MARK. Above this the staged clip has already been sampled into""",
    "partner donor branch")

# and set them from the staging
sub("""                                else if(f8<0){ int bc=-1; Pack*bp=bankFind(hcl,&bc);
                                    if(bp&&g_heroPk->cbOK&&g_heroPk->pbOK>=10){
                                        g_labDnP=bp; g_labDnC=bc; g_labWphReset=1; }
                                    else printf("[stage] hclip '%s' NOT FOUND@N",hcl); } }""".replace("@N", BS_N),
    """                                else if(f8<0){ int bc=-1; Pack*bp=bankFind(hcl,&bc);
                                    if(bp&&g_heroPk->cbOK&&g_heroPk->pbOK>=10){
                                        g_labDnP=bp; g_labDnC=bc; g_labClip=-1; g_labWphReset=1; }
                                    else printf("[stage] hclip '%s' NOT FOUND@N",hcl); }
                                else { g_labDnP=NULL; } }""".replace("@N", BS_N),
    "hero staging sets the bank")

sub("""                            if(g_labPartner&&S8->pclip[0]){
                                int f8=-1; for(unsigned c8=0;c8<g_labPartner->nClips;c8++)   /* pcl below */
                                    if(!strcmp(packStr(g_labPartner,g_labPartner->clips[c8].name),S8->pclip)){ f8=(int)c8; break; }
                                if(f8>=0&&f8!=g_labPClip){ g_labPClip=f8; g_labPPhase=0.0; }
                                else if(f8<0) printf("[stage] pclip '%s' NOT FOUND@N",S8->pclip); }""".replace("@N", BS_N),
    """                            if(g_labPartner&&pcl[0]){
                                int f8=-1; for(unsigned c8=0;c8<g_labPartner->nClips;c8++)
                                    if(!strcmp(packStr(g_labPartner,g_labPartner->clips[c8].name),pcl)){ f8=(int)c8; break; }
                                if(f8>=0){ if(f8!=g_labPClip){ g_labPClip=f8; g_labPPhase=0.0; }
                                           g_labPDnP=NULL; g_labPDnC=-1; }
                                else { int bc=-1; Pack*bp=bankFind(pcl,&bc);
                                    if(bp&&g_labPartner->cbOK&&g_labPartner->pbOK>=10){
                                        g_labPDnP=bp; g_labPDnC=bc; }
                                    else printf("[stage] pclip '%s' NOT FOUND@N",pcl); } }""".replace("@N", BS_N),
    "partner staging sets the bank")

sub("""                                   S8->hclip,S8->pclip); fflush(stdout); }""",
    """                                   hcl,pcl); fflush(stdout); }""",
    "log the resolved names")

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("patched crowd_kms.c")
