"""patch_propr.py -- `propr`: a prop with all three angles.

    propr <stem> x y z yaw pitch roll scale [r g b]

`prop` carries yaw only, which is right for a kit that snaps to a grid -- a house does not
lean. But SEVENTY-FOUR ends with three women nailing Dale's unfinished fence across
a window, and a plank laid across a window is a fence section rolled ninety degrees. There
is no yaw that does that.

A separate keyword rather than more optional fields on `prop`: the tint is already
positional there (r,g,b at 7..9), so appending pitch and roll would have made the ANGLES
depend on whether the prop happened to be tinted.
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


a = """        int n=sscanf(ln,"prop %63s %f %f %f %f %f %f %f %f",stem,&x,&y,&z,&yaw,&sc,&r,&g,&b);
        if(n<6) continue;"""
b = """        /* propr: the same prop with pitch and roll as well, for anything that leans,
         * lies flat or has been nailed across a window. */
        float pit=0.f, rol=0.f; int isr=0;
        int n;
        if(!strncmp(ln,"propr ",6)){
            n=sscanf(ln,"propr %63s %f %f %f %f %f %f %f %f %f %f",
                     stem,&x,&y,&z,&yaw,&pit,&rol,&sc,&r,&g,&b);
            if(n<8) continue;
            isr=1; n=(n>=11)?9:6;          /* re-map onto prop's field count for the tint test */
        } else {
            n=sscanf(ln,"prop %63s %f %f %f %f %f %f %f %f",stem,&x,&y,&z,&yaw,&sc,&r,&g,&b);
            if(n<6) continue; }
        (void)isr;"""
sub(a, b, "parse propr")

a = """        float a=yaw*0.0174532925f, ca=cosf(a), sa=sinf(a);
        memset(I->m,0,64);
        I->m[0]=ca*eff; I->m[2]=-sa*eff; I->m[5]=eff; I->m[8]=sa*eff; I->m[10]=ca*eff; I->m[15]=1.f;
        I->m[12]=x; I->m[13]=y-Q->mny*eff; I->m[14]=z;"""
b = """        if(pit!=0.f||rol!=0.f){
            float qy[4],qx[4],qz[4],q1[4],q2[4];
            qaxis(qy,0,1,0,yaw*0.0174532925f);
            qaxis(qx,1,0,0,pit*0.0174532925f);
            qaxis(qz,0,0,1,rol*0.0174532925f);
            qmul(q1,qy,qx); qmul(q2,q1,qz);
            float t3[3]={x,y-Q->mny*eff,z}, s3[3]={eff,eff,eff};
            m4_trs(I->m,t3,q2,s3); }
        else {
        float a=yaw*0.0174532925f, ca=cosf(a), sa=sinf(a);
        memset(I->m,0,64);
        I->m[0]=ca*eff; I->m[2]=-sa*eff; I->m[5]=eff; I->m[8]=sa*eff; I->m[10]=ca*eff; I->m[15]=1.f;
        I->m[12]=x; I->m[13]=y-Q->mny*eff; I->m[14]=z; }"""
sub(a, b, "compose the full rotation")

if s != orig:
    out = s.replace(LF, CRLF) if WAS_CRLF else s
    io.open(P, "w", encoding="utf-8", newline="").write(out)
    print("patched crowd_kms.c (%s)" % ("CRLF" if WAS_CRLF else "LF"))
