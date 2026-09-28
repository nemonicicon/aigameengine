"""patch_partnerclip.py -- THE PARTNER MUST END UP WITH A CLIP.

⛔ labDrawPartner returns immediately on `clip<0` and DRAWS NOTHING. So a partner whose
clip never resolved is simply absent from the scene -- and because her head mark is
published by that draw, `g_partHeadOK` stays 0, and the conversation camera (gated on BOTH
marks) never runs at all. Every scene then falls back to a static wide and none of the
shot grammar -- ots, tight, two, wide -- ever fires.

That is exactly what removing the ctl's `pclip=idle` did two patches ago, and nothing said
a word about it: the take renders, the file is the right size, the subtitles are correct,
and one of the two women is not there.

Her own stand, else her first clip. A WRONG clip is a bug you can see; an ABSENT body is
one you cannot.
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

# the pclip resolve ends with its own NOT FOUND print; splice right after that line
marker = "[stage] pclip '%s' NOT FOUND"
i = s.index(marker)
j = s.index(LF, i)          # end of that line, which closes: ... } }
line = s[s.rindex(LF, 0, i) + 1:j]
assert line.rstrip().endswith("} }"), "unexpected shape: %r" % line

new_line = line.rstrip()[:-2]      # drop the final "}" that closed `if(g_labPartner...)`
add = LF.join([
    "",
    "                                /* ⛔ AND SHE MUST END UP WITH SOMETHING. labDrawPartner",
    "                                 * returns on clip<0 and DRAWS NOTHING -- so a partner",
    "                                 * whose clip never resolved is simply ABSENT, her head",
    "                                 * mark is never published, and the conversation solver",
    "                                 * (gated on BOTH marks) never runs: every scene falls",
    "                                 * back to a static wide and no shot grammar fires. That",
    "                                 * is what removing the ctl's `pclip=idle` did, and the",
    "                                 * take still rendered at the right size with the right",
    "                                 * subtitles and one of the two women missing.",
    "                                 * Her own stand, else her first clip: a WRONG clip is a",
    "                                 * bug you can see, an ABSENT body is one you cannot. */",
    "                                if(g_labPClip<0&&!g_labPDnP&&g_labPartner->nClips){",
    "                                    g_labPClip=(g_labPartner->idleIdx>=0)?g_labPartner->idleIdx:0;",
    "                                    g_labPPhase=0.0;",
    "                                    static int said8=0;",
    "                                    if(!said8){ said8=1;",
    '                                        printf("[stage] partner had NO clip -- falling back to %s so she is drawn at all' + BSN + '",',
    "                                               packStr(g_labPartner,g_labPartner->clips[g_labPClip].name));",
    "                                        fflush(stdout); } } }",
])
s = s[:s.rindex(LF, 0, i) + 1] + new_line + add + s[j:]

io.open(P, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)
print("partner fallback clip in")
