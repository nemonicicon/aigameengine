/* club_math.c -- the club's 4x4s. Split out of crowd_kms.c 2026-09-30 (stage 1 of the split):
 * pure functions, no globals, the first translation unit that could leave without a fight. */
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
