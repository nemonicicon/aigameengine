/* club_math.c -- the engine's 4x4s. Split out of crowd_kms.c 2026-09-30 (stage 1 of the split):
 * pure functions, no globals, the first translation unit that could leave without a fight. */
#define CLUB_MATH_IMPL   /* this file uses the C library's floor, sqrt and so on, and defines the rest */
#include "club.h"
void m4_mul(M4 r,const M4 a,const M4 b){ M4 t; for(int c=0;c<4;c++)for(int rw=0;rw<4;rw++){ t[c*4+rw]=0; for(int k=0;k<4;k++) t[c*4+rw]+=a[k*4+rw]*b[c*4+k]; } memcpy(r,t,64); }
void m4_trs(M4 m,const float*t,const float*q,const float*s){
    float x=q[0],y=q[1],z=q[2],w=q[3];
    float xx=x*x,yy=y*y,zz=z*z,xy=x*y,xz=x*z,yz=y*z,wx=w*x,wy=w*y,wz=w*z;
    m[0]=(1-2*(yy+zz))*s[0]; m[1]=(2*(xy+wz))*s[0]; m[2]=(2*(xz-wy))*s[0]; m[3]=0;
    m[4]=(2*(xy-wz))*s[1]; m[5]=(1-2*(xx+zz))*s[1]; m[6]=(2*(yz+wx))*s[1]; m[7]=0;
    m[8]=(2*(xz+wy))*s[2]; m[9]=(2*(yz-wx))*s[2]; m[10]=(1-2*(xx+yy))*s[2]; m[11]=0;
    m[12]=t[0]; m[13]=t[1]; m[14]=t[2]; m[15]=1; }
void m4_persp(M4 m,float fovyDeg,float aspect,float zn,float zf){
    float f=1.0f/tanf(fovyDeg*(float)M_PI/360.0f); memset(m,0,64);
    m[0]=f/aspect; m[5]=f; m[10]=(zf+zn)/(zn-zf); m[11]=-1; m[14]=2*zf*zn/(zn-zf); }
void m4_lookat(M4 m,float ex,float ey,float ez,float cx,float cy,float cz){
    float fz[3]={ex-cx,ey-cy,ez-cz}; float l=sqrtf(fz[0]*fz[0]+fz[1]*fz[1]+fz[2]*fz[2]); for(int i=0;i<3;i++)fz[i]/=l;
    float up[3]={0,1,0};
    float sx[3]={ up[1]*fz[2]-up[2]*fz[1], up[2]*fz[0]-up[0]*fz[2], up[0]*fz[1]-up[1]*fz[0] };
    l=sqrtf(sx[0]*sx[0]+sx[1]*sx[1]+sx[2]*sx[2]); for(int i=0;i<3;i++)sx[i]/=l;
    float uy[3]={ fz[1]*sx[2]-fz[2]*sx[1], fz[2]*sx[0]-fz[0]*sx[2], fz[0]*sx[1]-fz[1]*sx[0] };
    m[0]=sx[0];m[1]=uy[0];m[2]=fz[0];m[3]=0; m[4]=sx[1];m[5]=uy[1];m[6]=fz[1];m[7]=0;
    m[8]=sx[2];m[9]=uy[2];m[10]=fz[2];m[11]=0;
    m[12]=-(sx[0]*ex+sx[1]*ey+sx[2]*ez); m[13]=-(uy[0]*ex+uy[1]*ey+uy[2]*ez); m[14]=-(fz[0]*ex+fz[1]*ey+fz[2]*ez); m[15]=1; }

/* ---- THE ENGINE'S OWN MATH (11-02, engine plan Phase 1) ---------------------------------------
 * Each machine's C library rounds sin, cos, atan2, exp, log and pow its own way, within its own
 * accuracy. Measured: same source, same packs, and the hero's x is one bit apart between Windows
 * and the Pi at tick 247, which is one bit more than "same rules everywhere" allows. So there is
 * one set, here, in plain IEEE double arithmetic, which every machine does the same way (the build
 * passes -ffp-contract=off so no fused multiply-add can sneak in). club.h points the engine's calls
 * here. floor, ceil, fabs, sqrt and fmod stay the library's: those have one right answer by definition.
 * Accuracy is a couple of units in the last place of a double, and a float's result is the double's,
 * rounded. anim_bench/mathcheck.c measures it against the C library.
 *
 * The polynomials are FreeBSD msun's (fdlibm):
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this software is freely granted, provided that
 * this notice is preserved. */
#define CM_PI 3.14159265358979311600e+00
#define CM_PIO2 1.57079632679489655800e+00
static double cm_ksin(double x,double y){ const double z=x*x,v=z*x;
    const double r=8.33333333332248946124e-03+z*(-1.98412698298579493134e-04+z*(2.75573137070700676789e-06+z*(-2.50507602534068634195e-08+z*1.58969099521155010221e-10)));
    return x-((z*(0.5*y-v*r)-y)-v*-1.66666666666666324348e-01); }
static double cm_kcos(double x,double y){ const double z=x*x,w2=z*z;
    const double r=z*(4.16666666666666019037e-02+z*(-1.38888888888741095749e-03+z*2.48015872894767294178e-05))+w2*w2*(-2.75573143513906633035e-07+z*(2.08757232129817482790e-09+z*-1.13596475577881948265e-11));
    const double hz=0.5*z,w=1.0-hz; return w+(((1.0-w)-hz)+(z*r-x*y)); }
/* x = n*(pi/2) + y0 + y1. Past a million radians the phase is meaningless anyway, and fmod keeps it deterministic. */
static int cm_rem(double x,double*y0,double*y1){
    if(fabs(x)<=7.85398163397448278999e-01){ *y0=x; *y1=0.0; return 0; }
    if(fabs(x)>1.0e6) x=fmod(x,6.28318530717958647692);
    const double fn=floor(x*6.36619772367581382433e-01+0.5);
    const double r=x-fn*1.57079632673412561417e+00,w=fn*6.07710050650619224932e-11;
    *y0=r-w; *y1=(r-*y0)-w; return (int)fmod(fn,4.0)&3; }
double club_sin(double x){ double a,b; switch(cm_rem(x,&a,&b)){ case 0: return cm_ksin(a,b); case 1: return cm_kcos(a,b); case 2: return -cm_ksin(a,b); default: return -cm_kcos(a,b); } }
double club_cos(double x){ double a,b; switch(cm_rem(x,&a,&b)){ case 0: return cm_kcos(a,b); case 1: return -cm_ksin(a,b); case 2: return -cm_kcos(a,b); default: return cm_ksin(a,b); } }
double club_tan(double x){ double a,b; const int n=cm_rem(x,&a,&b); const double s=cm_ksin(a,b),c=cm_kcos(a,b); return (n&1)?-c/s:s/c; }
double club_atan(double x){
    static const double hi[4]={4.63647609000806093515e-01,7.85398163397448278999e-01,9.82793723247329054082e-01,1.57079632679489655800e+00};
    static const double lo[4]={2.26987774529616870924e-17,3.06161699786838301793e-17,1.39033110312309984516e-17,6.12323399573676603587e-17};
    const int neg=signbit(x); double a=fabs(x); int id=-1;
    if(a!=a) return x;
    if(a>=7.378697629483821e19) return neg?-(hi[3]+lo[3]):(hi[3]+lo[3]);
    if(a<0.4375){ if(a<7.450580596923828e-09) return x; a=x; }
    else if(a<1.1875){ if(a<0.6875){ id=0; a=(2.0*a-1.0)/(2.0+a); } else { id=1; a=(a-1.0)/(a+1.0); } }
    else { if(a<2.4375){ id=2; a=(a-1.5)/(1.0+1.5*a); } else { id=3; a=-1.0/a; } }
    const double z=a*a,w=z*z;
    const double s1=z*(3.33333333333329318027e-01+w*(1.42857142725034663711e-01+w*(9.09088713343650656196e-02+w*(6.66107313738753120669e-02+w*(4.97687799461593236017e-02+w*1.62858201153657823623e-02)))));
    const double s2=w*(-1.99999999998764832476e-01+w*(-1.11111104054623557880e-01+w*(-7.69187620504482999495e-02+w*(-5.83357013379057348645e-02+w*-3.65315727442169155270e-02))));
    if(id<0) return a-a*(s1+s2);
    { const double r=hi[id]-((a*(s1+s2)-lo[id])-a); return neg?-r:r; } }
double club_atan2(double y,double x){
    if(y!=y||x!=x) return x+y;
    const int ny=signbit(y),nx=signbit(x);
    if(y==0.0) return nx?(ny?-CM_PI:CM_PI):y;
    if(x==0.0) return ny?-CM_PIO2:CM_PIO2;
    if(isinf(y)&&isinf(x)){ const double r=nx?3.0*CM_PI/4.0:CM_PI/4.0; return ny?-r:r; }
    const double a=club_atan(fabs(y/x)),r=nx?CM_PI-a:a; return ny?-r:r; }
double club_asin(double x){ if(fabs(x)>1.0) return (x-x)/(x-x); return club_atan2(x,sqrt((1.0-x)*(1.0+x))); }
double club_acos(double x){ if(fabs(x)>1.0) return (x-x)/(x-x); return club_atan2(sqrt((1.0-x)*(1.0+x)),x); }
double club_exp(double x){
    if(x!=x) return x;
    if(x>7.09782712893383973096e+02) return HUGE_VAL;
    if(x<-7.45133219101941108420e+02) return 0.0;
    const int k=(int)(1.44269504088896338700e+00*x+(x<0.0?-0.5:0.5));
    const double hi=x-k*6.93147180369123816490e-01,lo=k*1.90821492927058770002e-10,r=hi-lo,t=r*r;
    const double c=r-t*(1.66666666666666019037e-01+t*(-2.77777777770155933842e-03+t*(6.61375632143793436117e-05+t*(-1.65339022054652515390e-06+t*4.13813679705723846039e-08))));
    if(k==0) return 1.0-((r*c)/(c-2.0)-r);
    return ldexp(1.0-((lo-(r*c)/(2.0-c))-hi),k); }
double club_log(double x){
    if(x!=x) return x;
    if(x<0.0) return (x-x)/(x-x);
    if(x==0.0) return -HUGE_VAL;
    if(isinf(x)) return x;
    int k; double m=frexp(x,&k); if(m<7.07106781186547524401e-01){ m*=2.0; k--; }
    const double f=m-1.0,s=f/(2.0+f),z=s*s,w=z*z,dk=k;
    const double t1=w*(3.999999999940941908e-01+w*(2.222219843214978396e-01+w*1.531383769920937332e-01));
    const double t2=z*(6.666666666666735130e-01+w*(2.857142874366239149e-01+w*(1.818357216161805012e-01+w*1.479819860511658591e-01)));
    const double R=t2+t1,hfsq=0.5*f*f;
    return dk*6.93147180369123816490e-01-((hfsq-(s*(hfsq+R)+dk*1.90821492927058770002e-10))-f); }
double club_pow(double x,double y){
    if(y==0.0||x==1.0) return 1.0;
    if(x!=x||y!=y) return x+y;
    const int yint=(y==floor(y)); double sign=1.0;
    if(yint&&fabs(y)<=64.0){ double r=1.0,b=x; int n=(int)fabs(y); while(n){ if(n&1) r*=b; b*=b; n>>=1; } return y<0.0?1.0/r:r; }   /* a whole power stays exact where it can be */
    if(x==0.0) return y>0.0?0.0:HUGE_VAL;
    if(x<0.0){ if(!yint) return (x-x)/(x-x); if(fmod(y,2.0)!=0.0) sign=-1.0; x=-x; }
    return sign*club_exp(y*club_log(x)); }
