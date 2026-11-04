/* test_clubcloth.c -- the solver on its own, no engine: a tube of cloth on a jointed limb, swung about hard. Holds it to what it promises, and to known-bad controls. build_windows.bat and build_unix.sh build and run it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "clubcloth.h"

#define RINGS 24
#define SEGS 20
static int failed = 0;

static double now_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER fq, t;
    QueryPerformanceFrequency(&fq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)fq.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}
#define CHECK(c, ...) do { printf("%s: ", (c) ? "ok" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!(c)) failed++; } while (0)

/* A body: a tube from y=1 down to y=0, radius r, on two bones jointed at y=0.5. Its faces are wound outward. */
static void make_tube(int rings, int segs, float r, float top, float bot, float *pos, float *wgt, int *bon, int *idx, int *ni) {
    int n = 0;
    for (int i = 0; i <= rings; i++) {
        float t = (float)i / (float)rings, y = top + (bot - top) * t;
        for (int j = 0; j < segs; j++) {
            float a = 6.2831853f * (float)j / (float)segs;
            pos[n * 3] = r * cosf(a);
            pos[n * 3 + 1] = y;
            pos[n * 3 + 2] = r * sinf(a);
            float w = (0.5f - y) / 0.2f + 0.5f;
            w = w < 0.f ? 0.f : (w > 1.f ? 1.f : w);
            wgt[n * 4] = 1.f - w; wgt[n * 4 + 1] = w; wgt[n * 4 + 2] = 0.f; wgt[n * 4 + 3] = 0.f;
            bon[n * 4] = 0; bon[n * 4 + 1] = 1; bon[n * 4 + 2] = 0; bon[n * 4 + 3] = 0;
            n++;
        }
    }
    int m = 0;
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            int a = i * segs + j, b = i * segs + (j + 1) % segs, c = a + segs, d = b + segs;
            idx[m++] = a; idx[m++] = b; idx[m++] = c;
            idx[m++] = b; idx[m++] = d; idx[m++] = c;
        }
    *ni = m;
}

static void set_rot_x(float *m, float ang, float py, float *pos_out_unused) {
    /* a rotation about x through the point (0, py, 0), as 3x4 by rows */
    float c = cosf(ang), s = sinf(ang);
    m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 0;
    m[4] = 0; m[5] = c; m[6] = -s; m[7] = py - c * py;
    m[8] = 0; m[9] = s; m[10] = c; m[11] = -s * py;
    (void)pos_out_unused;
}

static float dist3(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

int main(void) {
    int nb = (RINGS + 1) * SEGS;
    float *bp = malloc(sizeof(float) * 3 * nb), *bw = malloc(sizeof(float) * 4 * nb);
    int *bb = malloc(sizeof(int) * 4 * nb), *bi = malloc(sizeof(int) * 6 * RINGS * SEGS);
    int bni;
    make_tube(RINGS, SEGS, 0.06f, 1.0f, 0.0f, bp, bw, bb, bi, &bni);
    CCBody body = {nb, bp, bw, bb, bni, bi};
    /* The cloth: a looser tube, held still at the top, free at the bottom. */
    int cr = 30, cs = 24, nc = (cr + 1) * cs;
    float *cp = malloc(sizeof(float) * 3 * nc), *cw = malloc(sizeof(float) * 4 * nc), *lim = malloc(sizeof(float) * nc);
    int *cb = malloc(sizeof(int) * 4 * nc), *ci = malloc(sizeof(int) * 6 * cr * cs);
    int cni;
    make_tube(cr, cs, 0.085f, 0.95f, 0.15f, cp, cw, cb, ci, &cni);
    for (int i = 0; i < nc; i++) {
        float y = cp[i * 3 + 1];
        float t = (0.95f - y) / 0.8f;
        lim[i] = t < 0.1f ? 0.f : 0.12f * t;
    }
    int parent[2] = {-1, 0};
    CCGarmentDesc d;
    memset(&d, 0, sizeof(d));
    d.nv = nc; d.pos = cp; d.wgt = cw; d.bon = cb; d.maxd = lim; d.ni = cni; d.idx = ci; d.nbones = 2; d.parent = parent;
    CCParams par;
    cc_default_params(&par);
    CCGarment *g = cc_create(&d, &body, &par);
    int np = cc_point_count(g);
    CHECK(np == nc, "welded %d vertices to %d points, %d edges", nc, np, cc_edge_count(g));
    float pal[24];
    float grav[3] = {0, -9.81f, 0}, zero[3] = {0, 0, 0}, air[3] = {0, 0, 0};
    set_rot_x(pal, 0.f, 0.5f, NULL);
    set_rot_x(pal + 12, 0.f, 0.5f, NULL);
    /* Still: it hangs about where it was cut. */
    for (int f = 0; f < 240; f++) cc_step(g, 1.f / 60.f, pal, grav, zero, air, NULL, 0);
    float sag = 0.f;
    for (int p = 0; p < np; p++) sag = fmaxf(sag, dist3(cc_points(g) + p * 3, cc_targets(g) + p * 3));
    CHECK(sag < 0.06f, "standing still, 4 s on, the most any point has sagged from its cut: %.3f m", sag);
    /* Swung about hard: the knee goes through 90 degrees, 3 times a second. */
    float worst_lim = 0.f, worst_in = 0.f, moved = 0.f;
    int nans = 0;
    double t0 = now_seconds();
    int frames = 1200;
    for (int f = 0; f < frames; f++) {
        float t = (float)f / 99.f, ang = 0.8f * sinf(t * 18.f);
        set_rot_x(pal + 12, ang, 0.5f, NULL);
        air[2] = 6.f * sinf(t * 2.f);
        cc_step(g, 1.f / 99.f, pal, grav, zero, air, NULL, 0);
        const float *x = cc_points(g), *tg = cc_targets(g), *bs = cc_backstops(g), *bn = cc_backnormals(g), *lm = cc_limits(g), *nr = cc_nears(g);
        for (int p = 0; p < np; p++) {
            for (int k = 0; k < 3; k++) if (!(fabsf(x[p * 3 + k]) < 100.f)) nans++;
            float dl = dist3(x + p * 3, tg + p * 3);
            worst_lim = fmaxf(worst_lim, dl - lm[p]);
            moved = fmaxf(moved, dl);
            float e[3] = {x[p * 3] - bs[p * 3], x[p * 3 + 1] - bs[p * 3 + 1], x[p * 3 + 2] - bs[p * 3 + 2]};
            float in = nr[p] - (e[0] * bn[p * 3] + e[1] * bn[p * 3 + 1] + e[2] * bn[p * 3 + 2]);
            if (lm[p] > 0.f) worst_in = fmaxf(worst_in, in);
        }
    }
    double t1 = now_seconds();
    CHECK(nans == 0, "no point ever blows up (%d bad values)", nans);
    CHECK(worst_lim < 0.001f, "no point ever goes past its limit (by %.4f m)", worst_lim);
    CHECK(worst_in < 0.002f, "no point ever goes inside its skin's plane (by %.4f m)", worst_in);
    CHECK(moved > 0.03f, "and it really moves (%.3f m off the skinned pose at most)", moved);
    /* A capsule pushing into it, and one on the point's own bone, which it ignores. */
    cc_reset(g);
    CCCapsule cap;
    memset(&cap, 0, sizeof(cap));
    cap.a[0] = 0.f; cap.a[1] = 0.3f; cap.a[2] = 0.14f;
    cap.b[0] = 0.f; cap.b[1] = 0.1f; cap.b[2] = 0.14f;
    cap.r = 0.05f;
    cap.bone = 9;
    set_rot_x(pal + 12, 0.f, 0.5f, NULL);
    air[2] = 0.f;
    float inside = 0.f;
    for (int f = 0; f < 120; f++) cc_step(g, 1.f / 99.f, pal, grav, zero, air, &cap, 1);
    for (int p = 0; p < np; p++) {
        const float *x = cc_points(g) + p * 3;
        if (cc_limits(g)[p] <= 0.f || x[1] > 0.3f || x[1] < 0.1f) continue;
        float q[3] = {x[0], 0.f, x[2] - 0.14f};
        inside = fmaxf(inside, 0.05f - sqrtf(q[0] * q[0] + q[2] * q[2]));
    }
    CHECK(inside < 0.012f, "a limb pushing into it holds the cloth out of itself (%.4f m inside at most)", inside);
    /* A garment over another: what's under it pushed out by a limb, and it stays outside. Once without, so this is known to be able to fail. */
    float *ip = malloc(sizeof(float) * 3 * nc), *iw = malloc(sizeof(float) * 4 * nc);
    int *ib = malloc(sizeof(int) * 4 * nc), *ii = malloc(sizeof(int) * 6 * cr * cs);
    int ini;
    make_tube(cr, cs, 0.07f, 0.95f, 0.15f, ip, iw, ib, ii, &ini);
    CCGarmentDesc du = d;
    du.pos = ip; du.wgt = iw; du.bon = ib; du.idx = ii; du.ni = ini;
    CCGarment *under = cc_create(&du, &body, &par);
    CCCapsule bulge;
    memset(&bulge, 0, sizeof(bulge));
    bulge.a[1] = 0.45f; bulge.a[2] = 0.06f;
    bulge.b[1] = 0.25f; bulge.b[2] = 0.06f;
    bulge.r = 0.06f;
    bulge.bone = 9;
    for (int with = 0; with < 2; with++) {
        cc_reset(under);
        cc_reset(g);
        cc_set_under(g, with ? under : NULL, 0.006f);
        for (int f = 0; f < 120; f++) {
            cc_pose(under, pal);
            cc_pose(g, pal);
            cc_layer(under);
            cc_layer(g);
            cc_solve(under, 1.f / 99.f, grav, zero, air, &bulge, 1);
            cc_solve(g, 1.f / 99.f, grav, zero, air, NULL, 0);
        }
        /* Both cut the same way, so a point of one is over the same point of the other. */
        float through = -1.f;
        for (int p = 0; p < np; p++) {
            const float *o = cc_shown(g) + p * 3, *u = cc_shown(under) + p * 3;
            through = fmaxf(through, sqrtf(u[0] * u[0] + u[2] * u[2]) - sqrtf(o[0] * o[0] + o[2] * o[2]));
        }
        if (with) CHECK(through < 0.004f, "worn over it, it stays outside what's under it (%.4f m through at most)", through);
        else CHECK(through > 0.01f, "and without that, what's under it comes through (%.4f m)", through);
    }
    cc_set_under(g, NULL, 0.f);
    cc_free(under);
    /* A board in the way, flat side on: the cloth stays outside it. It's where the tube hangs, off to one side. Once without. */
    cc_reset(g);
    CCBox box;
    memset(&box, 0, sizeof(box));
    box.m[0] = 1.f; box.m[5] = 1.f; box.m[10] = 1.f;
    box.m[3] = 0.f; box.m[7] = 0.3f; box.m[11] = 0.1f;
    box.half[0] = 0.2f; box.half[1] = 0.1f; box.half[2] = 0.03f;
    for (int with = 0; with < 2; with++) {
        cc_reset(g);
        cc_set_boxes(g, &box, with);
        for (int f = 0; f < 60; f++) cc_step(g, 1.f / 99.f, pal, grav, zero, air, NULL, 0);
        float in_box = 0.f;
        for (int p = 0; p < np; p++) {
            const float *x = cc_points(g) + p * 3;
            float dx = 0.2f - fabsf(x[0]), dy = 0.1f - fabsf(x[1] - 0.3f), dz = 0.03f - fabsf(x[2] - 0.1f);
            in_box = fmaxf(in_box, fminf(dx, fminf(dy, dz)));
        }
        if (with) CHECK(in_box < 0.003f, "a box in the way stays clear (%.4f m in at most)", in_box);
        else CHECK(in_box > 0.005f, "and without it the cloth's where the box would be (%.4f m in)", in_box);
    }
    cc_set_boxes(g, NULL, 0);
    /* Two tubes in one garment, the first moved across into the second: its parts keep out of each other. Once without. */
    {
        int tn = nc * 2, ti = cni * 2;
        float *tp = malloc(sizeof(float) * 3 * tn), *tw = malloc(sizeof(float) * 4 * tn), *tl = malloc(sizeof(float) * tn);
        int *tb = malloc(sizeof(int) * 4 * tn), *tx = malloc(sizeof(int) * ti);
        int one;
        make_tube(cr, cs, 0.05f, 0.95f, 0.15f, tp, tw, tb, tx, &one);
        for (int i = 0; i < nc; i++) {
            memcpy(tp + (nc + i) * 3, tp + i * 3, sizeof(float) * 3);
            memcpy(tw + (nc + i) * 4, tw + i * 4, sizeof(float) * 4);
            for (int w = 0; w < 4; w++) tb[(nc + i) * 4 + w] = tb[i * 4 + w] + 2;
            tp[i * 3] -= 0.07f;
            tp[(nc + i) * 3] += 0.07f;
            tl[i] = tl[nc + i] = lim[i];
        }
        for (int i = 0; i < cni; i++) tx[cni + i] = tx[i] + nc;
        /* Two thin limbs inside them. */
        int bn2 = nb * 2;
        float *bp2 = malloc(sizeof(float) * 3 * bn2), *bw2 = malloc(sizeof(float) * 4 * bn2);
        int *bb2 = malloc(sizeof(int) * 4 * bn2), *bi2 = malloc(sizeof(int) * 2 * bni);
        int bone1;
        make_tube(RINGS, SEGS, 0.03f, 1.0f, 0.0f, bp2, bw2, bb2, bi2, &bone1);
        for (int i = 0; i < nb; i++) {
            memcpy(bp2 + (nb + i) * 3, bp2 + i * 3, sizeof(float) * 3);
            memcpy(bw2 + (nb + i) * 4, bw2 + i * 4, sizeof(float) * 4);
            for (int w = 0; w < 4; w++) bb2[(nb + i) * 4 + w] = bb2[i * 4 + w] + 2;
            bp2[i * 3] -= 0.07f;
            bp2[(nb + i) * 3] += 0.07f;
        }
        for (int i = 0; i < bone1; i++) bi2[bone1 + i] = bi2[i] + nb;
        CCBody limbs = {bn2, bp2, bw2, bb2, bone1 * 2, bi2};
        int par4[4] = {-1, 0, -1, 2};
        CCGarmentDesc dt = d;
        dt.nv = tn; dt.pos = tp; dt.wgt = tw; dt.bon = tb; dt.maxd = tl; dt.ni = ti; dt.idx = tx; dt.nbones = 4; dt.parent = par4;
        float pal4[48];
        for (int b = 0; b < 4; b++) set_rot_x(pal4 + b * 12, 0.f, 0.5f, NULL);
        CCGarment *two = cc_create(&dt, &limbs, &par);
        int bone_part[4] = {1, 1, 2, 2};
        for (int with = 0; with < 2; with++) {
            cc_reset(two);
            if (with) cc_set_parts(two, bone_part, 0.006f);
            for (int b = 0; b < 2; b++) pal4[b * 12 + 3] = 0.f;
            for (int f = 0; f < 120; f++) {
                /* The first tube's bones move across into the second. */
                float across = 0.09f * (f < 60 ? (float)f / 60.f : 1.f);
                for (int b = 0; b < 2; b++) pal4[b * 12 + 3] = across;
                cc_step(two, 1.f / 99.f, pal4, zero, zero, zero, NULL, 0);
            }
            /* A point of the first tube past one of the second at about the same place: the two can curve round each other, so only side by side counts. */
            float through = -1.f;
            for (int p = 0; p < nc; p++) {
                const float *x = cc_points(two) + p * 3;
                if (x[1] < 0.3f || x[1] > 0.45f) continue;
                for (int q = nc; q < cc_point_count(two); q++) {
                    const float *o = cc_points(two) + q * 3;
                    if (fabsf(x[1] - o[1]) < 0.03f && fabsf(x[2] - o[2]) < 0.01f) through = fmaxf(through, x[0] - o[0]);
                }
            }
            if (with) CHECK(through < 0.03f, "a garment's parts lean off each other (%.4f m through at most)", through);
            else CHECK(through > 0.02f, "and without that, they pass through (%.4f m)", through);
        }
        cc_free(two);
    }
    /* The body's own space turned half way round under the cloth all at once, and the pose with it: in the world nothing moved, so what's drawn mustn't. Once without telling it. */
    {
        const float half_turn[12] = {-1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, -1.f, 0.f};
        float *was = malloc(sizeof(float) * 3 * np);
        set_rot_x(pal + 12, 0.f, 0.5f, NULL);
        for (int with = 0; with < 2; with++) {
            cc_reset(g);
            air[2] = 5.f;
            for (int f = 0; f < 120; f++) cc_step(g, 1.f / 99.f, pal, grav, zero, air, NULL, 0);
            memcpy(was, cc_shown(g), sizeof(float) * 3 * np);
            float turned[24];
            for (int b = 0; b < 2; b++) for (int row = 0; row < 3; row++) for (int c = 0; c < 4; c++) {
                float sum = 0.f;
                for (int k = 0; k < 3; k++) sum += half_turn[row * 4 + k] * pal[b * 12 + k * 4 + c];
                turned[b * 12 + row * 4 + c] = sum;
            }
            if (with) cc_carry(g, half_turn);
            float air_turned[3] = {0.f, 0.f, -air[2]};
            cc_step(g, 1.f / 99.f, turned, grav, zero, air_turned, NULL, 0);
            float worst = 0.f;
            for (int p = 0; p < np; p++) {
                const float *x = cc_shown(g) + p * 3;
                float back[3] = {-x[0], x[1], -x[2]};
                worst = fmaxf(worst, dist3(back, was + p * 3));
            }
            if (with) CHECK(worst < 0.01f, "its space turned half way round under it, nothing moves in the world (%.4f m at most)", worst);
            else CHECK(worst > 0.03f, "and without being told, it does (%.4f m)", worst);
        }
        air[2] = 0.f;
        free(was);
    }
    /* What's drawn is finer than what's solved, and stays on it. */
    CHECK(cc_drawn_vertex_count(g) > nc * 2, "what's drawn has %d vertices for %d points", cc_drawn_vertex_count(g), np);
    /* The numbers. */
    double ms = 1000.0 * (t1 - t0) / frames;
    printf("%d points, %.3f ms a frame at 99 Hz on this thread, with the checks' own reading in it\n", np, ms);
    cc_free(g);
    printf(failed ? "FAILED: %d\n" : "all passed\n", failed);
    return failed ? 1 : 0;
}
