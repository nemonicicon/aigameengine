/* mathcheck.c -- the club's own math (club_math.c) against this machine's C library, over the ranges a game uses. Prints the largest error in units in the last place, the double result
 * and the float result, for each function. Run it on any machine; the numbers should be small on all of them, and that's all it's for: the engine's contract is that club_math gives the
 * same bits everywhere, which hashcheck.py proves.
 * Build (from reva_native): gcc -O2 -std=gnu17 -ffp-contract=off -o mathcheck anim_bench/mathcheck.c club_math.c -lSDL2 -lm    (on Windows: -Dmain=SDL_main -lmingw32 -lSDL2main -lSDL2 -lm) */
#define CLUB_MATH_IMPL
#include "../club.h"
#include <stdint.h>

static double ulpd(double a, double b) { if (a == b) return 0; if (a != a && b != b) return 0; if (a != a || b != b) return 1e30; int64_t x, y; memcpy(&x, &a, 8); memcpy(&y, &b, 8); if ((x < 0) != (y < 0)) return fabs(a - b) < 1e-300 ? 1 : 1e30; return (double)llabs(x - y); }
static double ulpf(float a, float b) { if (a == b) return 0; if (a != a && b != b) return 0; if (a != a || b != b) return 1e30; int32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); if ((x < 0) != (y < 0)) return fabsf(a - b) < 1e-37f ? 1 : 1e30; return fabs((double)x - (double)y); }
static unsigned g_r = 12345u;
static double rnd(double lo, double hi) { g_r = g_r * 1664525u + 1013904223u; return lo + (hi - lo) * (double)(g_r >> 8) / 16777216.0; }
typedef double (*F1)(double); typedef double (*F2)(double, double); typedef float (*G1)(float); typedef float (*G2)(float, float);
static void one(const char *n, F1 mine, F1 lib, G1 libf, double lo, double hi) {
    double wd = 0, wf = 0, xd = 0, xf = 0; long same = 0, N = 2000000;
    for (long i = 0; i < N; i++) { double x = rnd(lo, hi); double u = ulpd(mine(x), lib(x)); if (u > wd) { wd = u; xd = x; } float fx = (float)x; float mf = (float)mine((double)fx); double v = ulpf(mf, libf(fx)); if (v > wf) { wf = v; xf = fx; } if (v == 0) same++; }
    printf("%-7s [%9.4g,%9.4g]  double worst %5.0f ulp at %.6g   float worst %3.0f ulp at %.6g   float identical %.2f%%\n", n, lo, hi, wd, xd, wf, xf, 100.0 * same / N);
}
static void two(const char *n, F2 mine, F2 lib, G2 libf, double a0, double a1, double b0, double b1) {
    double wd = 0, wf = 0, ad = 0, bd = 0, af = 0, bf = 0; long same = 0, N = 2000000;
    for (long i = 0; i < N; i++) { double a = rnd(a0, a1), b = rnd(b0, b1); double u = ulpd(mine(a, b), lib(a, b)); if (u > wd) { wd = u; ad = a; bd = b; } float fa = (float)a, fb = (float)b; float mf = (float)mine((double)fa, (double)fb); double v = ulpf(mf, libf(fa, fb)); if (v > wf) { wf = v; af = fa; bf = fb; } if (v == 0) same++; }
    printf("%-7s [%.4g,%.4g]x[%.4g,%.4g]  double worst %5.0f ulp at %.6g,%.6g   float worst %3.0f ulp at %.6g,%.6g   float identical %.2f%%\n", n, a0, a1, b0, b1, wd, ad, bd, wf, af, bf, 100.0 * same / N);
}
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    one("sin", club_sin, sin, sinf, -7, 7); one("sin", club_sin, sin, sinf, -5000, 5000);
    one("cos", club_cos, cos, cosf, -7, 7); one("cos", club_cos, cos, cosf, -5000, 5000);
    one("tan", club_tan, tan, tanf, -1.5, 1.5);
    one("atan", club_atan, atan, atanf, -50, 50);
    one("asin", club_asin, asin, asinf, -1, 1); one("acos", club_acos, acos, acosf, -1, 1);
    one("exp", club_exp, exp, expf, -30, 30); one("log", club_log, log, logf, 1e-4, 1e4);
    two("atan2", club_atan2, atan2, atan2f, -100, 100, -100, 100);
    two("pow", club_pow, pow, powf, 0.001, 10, -4, 4);
    /* the exact cases the C standard names */
    printf("edges: sin0 %g cos0 %g atan2(0,1) %g atan2(0,-1) %g atan2(-0,-1) %g atan2(1,0) %g pow(2,3) %.17g pow(2,-2) %g pow(-2,3) %g pow(0,2) %g log1 %g exp0 %g\n", club_sin(0), club_cos(0), club_atan2(0, 1), club_atan2(0, -1), club_atan2(-0.0, -1), club_atan2(1, 0), club_pow(2, 3), club_pow(2, -2), club_pow(-2, 3), club_pow(0, 2), club_log(1), club_exp(0));
    return 0;
}
