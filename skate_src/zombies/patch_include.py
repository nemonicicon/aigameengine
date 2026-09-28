"""patch_include.py -- `include <set>` in the set format.

A fifteen-minute film shot on one street needs many sets over the SAME 514 lines of
suburbia: the mailbox beat, the driveway beat, the night interior. Copying the street
into each one makes street.py stop being the source of truth the moment anybody edits a
copy. One directive fixes it:

    include block          # everything in sets/block.set, then carry on

Depth-limited to 2, which is a street plus a cast and no more -- an include that can
nest arbitrarily is a way to hang the loader on a cycle.
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
BS_N = chr(92) + "n"


def sub(a, b, why):
    global s
    n = s.count(a)
    if n != 1:
        print("!! anchor x%d for %s" % (n, why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# setLoad -> setParse(name,depth), with setLoad the resetting wrapper
a = """static int setLoad(const char*name){
    g_setNI=0; g_setNA=0; g_setCur[0]=0;
    if(!name||!name[0]||!strcmp(name,"off")){ printf("[set] off@N"); fflush(stdout); return 1; }
    char pth[512]; snprintf(pth,sizeof pth,"sets/%s.set",name);
    FILE*f=fopen(pth,"rb");
    if(!f){ printf("[set] %s NOT FOUND@N",pth); fflush(stdout); return 0; }
    char ln[512]; int nline=0, miss=0;
    while(fgets(ln,sizeof ln,f)){
        if(ln[0]=='#'||ln[0]=='\\n'||ln[0]=='\\r') continue;""".replace("@N", BS_N)
b = """static int setParse(const char*name,int depth);
static int setLoad(const char*name){
    g_setNI=0; g_setNA=0; g_setCur[0]=0;
    if(!name||!name[0]||!strcmp(name,"off")){ printf("[set] off@N"); fflush(stdout); return 1; }
    int ok=setParse(name,0);
    if(ok){ snprintf(g_setCur,64,"%s",name);
            printf("[set] %s: %d props + %d actors from %d packs@N",
                   name,g_setNI,g_setNA,g_setNP); fflush(stdout); }
    return ok; }
static int setParse(const char*name,int depth){
    char pth[512]; snprintf(pth,sizeof pth,"sets/%s.set",name);
    FILE*f=fopen(pth,"rb");
    if(!f){ printf("[set] %s NOT FOUND@N",pth); fflush(stdout); return 0; }
    char ln[512]; int nline=0, miss=0;
    while(fgets(ln,sizeof ln,f)){
        if(ln[0]=='#'||ln[0]=='\\n'||ln[0]=='\\r') continue;
        /* A STREET IS A FILE, A CAST IS ANOTHER. The film shoots many scenes on the same
         * 514 lines of suburbia; copying them per shot is how street.py would stop being
         * the source of truth. Depth-limited: a street plus a cast, and no cycles. */
        if(!strncmp(ln,"include ",8)){
            char inc[64]; if(sscanf(ln,"include %63s",inc)==1){
                if(depth>=2) printf("[set] include %s IGNORED (too deep)@N",inc);
                else setParse(inc,depth+1); }
            continue; }""".replace("@N", BS_N)
sub(a, b, "setLoad splits into setParse with include")

# the old tail printed the summary and set g_setCur -- setLoad does that now
a = """    fclose(f);
    snprintf(g_setCur,64,"%s",name);
    printf("[set] %s: %d props + %d actors from %d packs (%d lines, %d missing)@N",
           name,g_setNI,g_setNA,g_setNP,nline,miss);""".replace("@N", BS_N)
b = """    fclose(f);
    printf("[set]   %-16s %d lines, %d missing@N",name,nline,miss);""".replace("@N", BS_N)
sub(a, b, "setParse reports its own file; setLoad reports the total")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
