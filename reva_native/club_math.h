/* club_math.h -- THE ENGINE'S OWN MATH (11-02), in club_math.c: one answer on every machine, because each C library rounds its own way.
 * The engine's calls come here, and so do a game's: engine.h includes this, and nothing else of the engine's, so a game's rules are the same on any machine (step 20, 11-04).
 * floor, ceil, fabs, sqrt and fmod have one right answer already. */
#ifndef CLUB_MATH_H
#define CLUB_MATH_H
#include <math.h>
double club_sin(double x); double club_cos(double x); double club_tan(double x); double club_atan(double x); double club_atan2(double y,double x);
double club_asin(double x); double club_acos(double x); double club_exp(double x); double club_log(double x); double club_pow(double x,double y);
#ifndef CLUB_MATH_IMPL
#define sin(x) club_sin(x)
#define cos(x) club_cos(x)
#define tan(x) club_tan(x)
#define atan(x) club_atan(x)
#define atan2(y,x) club_atan2(y,x)
#define asin(x) club_asin(x)
#define acos(x) club_acos(x)
#define exp(x) club_exp(x)
#define log(x) club_log(x)
#define pow(x,y) club_pow(x,y)
#define sinf(x) ((float)club_sin((double)(x)))
#define cosf(x) ((float)club_cos((double)(x)))
#define tanf(x) ((float)club_tan((double)(x)))
#define atanf(x) ((float)club_atan((double)(x)))
#define atan2f(y,x) ((float)club_atan2((double)(y),(double)(x)))
#define asinf(x) ((float)club_asin((double)(x)))
#define acosf(x) ((float)club_acos((double)(x)))
#define expf(x) ((float)club_exp((double)(x)))
#define logf(x) ((float)club_log((double)(x)))
#define powf(x,y) ((float)club_pow((double)(x),(double)(y)))
#define hypotf(x,y) club_hypotf(x,y)
static inline float club_hypotf(float x,float y){ const double a=x,b=y; return (float)sqrt(a*a+b*b); }
#endif
#endif
