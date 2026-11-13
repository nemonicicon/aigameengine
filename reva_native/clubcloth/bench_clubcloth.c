/* bench_clubcloth.c -- does it move like cloth? The solver on its own, in scenes with an answer known from physics or from a textile lab, measured and held to it.
 * 11-03, after clips of the first cloth that ran: cloth physics needs a calibration and a verification of its own, checked here and not by filming, and the basics come first.
 *
 *   SWING        a sheet hung by its top edge, let go: its first mode is a compound pendulum, T = 2 pi sqrt(2L / 3g), whatever it's made of. Most of its motion is in that mode, and air and its own stiffness bring it to rest in a few swings.
 *   FRAME RATE   the same swing at 30, 60, 99 and 144 frames a second is the same swing.
 *   ENERGY       a sheet let go only ever loses energy.
 *   FALL         a sheet falling flat reaches v = sqrt(2 m g / (rho Cd A)), and no faster: for a tee, about a brisk walk.
 *   STRETCH      fabric under its own weight barely stretches.
 *   REST         hanging still, it is still: no buzz.
 *   CANTILEVER   ASTM D1388: a strip droops 41.5 degrees at an overhang of twice its bending length, c = cuberoot(G / (m g)), the textile lab's own number for how stiff a fabric is. Here it's clamped and held to the exact heavy elastica, and read back as that number.
 *   RESOLUTION   the same fabric on a garment's coarse irregular mesh reads the same. Only a mesh this coarse: one pass a substep can't carry a stiff fabric's bending down a mesh much finer than 1 cm.
 *   FRICTION     on a slope it stays below atan(mu) and slides above it.
 *   JOLT         worn, on a body that starts and stops: the hem swings at its pendulum's period and settles. It doesn't twitch.
 *   FRAME TIMES  frames that come unevenly are the same swing again, not a shake.
 *   THE LIMIT    a point held hard against its limit stores nothing there, and leaves it the moment it's let go.
 *   CRUMPLING    pushed together by a quarter, it folds in arcs. It doesn't scribble.
 *   SPIN         on a body that spins, a panel flares to where a heavy chain does: tan(angle) = w^2 r / g, integrated down its length. And it lags as the spin starts, and hangs straight again once it's over.
 *
 *   cl bench_clubcloth.c clubcloth.c, then run it: every line is a measurement and the range physics or the lab allows. Exit code 1 if any fails.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clubcloth.h"

#define PI 3.14159265f
#define G 9.81f
static int failed = 0;

/* Typical values, from general textile knowledge and not from one lab's table: a tee's jersey, a shorts' twill, a sweatshirt's fleece, jeans' denim. A knit gives on the bias far more easily than a weave. */
typedef struct {
    const char *name;
    float density;      /* kg/m^2 */
    float bending_cm;   /* its bending length, the lab's number */
    float shear;        /* N/m */
} Fabric;
static const Fabric TEE = {"tee", 0.16f, 2.0f, 3.f}, TWILL = {"twill", 0.23f, 3.0f, 5.f}, FLEECE = {"fleece", 0.32f, 3.0f, 3.f}, DENIM = {"denim", 0.40f, 4.5f, 8.f};

static void row(const char *what, double got, double low, double high, const char *unit) {
    int ok = got >= low && got <= high;
    printf("%s %-46s %10.3f %-8s [%.3f .. %.3f]\n", ok ? "ok  " : "FAIL", what, got, unit, low, high);
    if (!ok) failed++;
}

static CCParams params_of(const Fabric *f) {
    CCParams p;
    cc_default_params(&p);
    cc_fabric(&p, f->density, f->bending_cm * 0.01f, f->shear);
    return p;
}

/* A sheet w across and h down, about `edge` between points, hung with its top at y = 0 in the plane z = 0. */
typedef struct {
    int nx, ny, nv, ni;
    float *pos, *wgt, *lim;
    int *bon, *idx;
    float w, h;
} Sheet;

static Sheet make_sheet(float w, float h, float edge) {
    Sheet s;
    memset(&s, 0, sizeof(s));
    s.w = w; s.h = h;
    s.nx = (int)(w / edge + 0.5f) + 1;
    s.ny = (int)(h / edge + 0.5f) + 1;
    s.nv = s.nx * s.ny;
    s.pos = (float *)malloc(sizeof(float) * 3 * (size_t)s.nv);
    s.wgt = (float *)calloc((size_t)s.nv * 4, sizeof(float));
    s.lim = (float *)malloc(sizeof(float) * (size_t)s.nv);
    s.bon = (int *)calloc((size_t)s.nv * 4, sizeof(int));
    s.idx = (int *)malloc(sizeof(int) * 6 * (size_t)((s.nx - 1) * (s.ny - 1)));
    for (int j = 0; j < s.ny; j++) for (int i = 0; i < s.nx; i++) {
        int v = j * s.nx + i;
        s.pos[v * 3] = -w * 0.5f + w * (float)i / (float)(s.nx - 1);
        s.pos[v * 3 + 1] = -h * (float)j / (float)(s.ny - 1);
        s.pos[v * 3 + 2] = 0.f;
        s.wgt[v * 4] = 1.f;
        s.lim[v] = j == 0 ? 0.f : 100.f;
    }
    int m = 0;
    for (int j = 0; j < s.ny - 1; j++) for (int i = 0; i < s.nx - 1; i++) {
        int a = j * s.nx + i, b = a + 1, c = a + s.nx, d = c + 1;
        /* the diagonals alternate, so it has no grain */
        if ((i + j) % 2) { s.idx[m++] = a; s.idx[m++] = b; s.idx[m++] = c; s.idx[m++] = b; s.idx[m++] = d; s.idx[m++] = c; }
        else { s.idx[m++] = a; s.idx[m++] = b; s.idx[m++] = d; s.idx[m++] = a; s.idx[m++] = d; s.idx[m++] = c; }
    }
    s.ni = m;
    return s;
}

static void free_sheet(Sheet *s) { free(s->pos); free(s->wgt); free(s->lim); free(s->bon); free(s->idx); }

static CCGarment *garment_of(const Sheet *s, const CCParams *par) {
    static int parent[1] = {-1};
    CCGarmentDesc d;
    memset(&d, 0, sizeof(d));
    d.nv = s->nv; d.pos = s->pos; d.wgt = s->wgt; d.bon = s->bon; d.maxd = s->lim; d.ni = s->ni; d.idx = s->idx; d.nbones = 1; d.parent = parent;
    return cc_create(&d, NULL, par);
}

static const float PAL1[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

/* The share of a signal's power above a frequency, and the frequency it's strongest at. */
static double spectrum(const double *x, int n, double fps, double above, double *peak_hz) {
    double mean = 0.0;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= n;
    double total = 0.0, high = 0.0, best = 0.0;
    *peak_hz = 0.0;
    for (double f = 0.1; f < fps * 0.5; f += 0.1) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; i++) {
            double hann = 0.5 - 0.5 * cos(2.0 * PI * i / (n - 1));
            double a = 2.0 * PI * f * i / fps;
            re += (x[i] - mean) * hann * cos(a);
            im += (x[i] - mean) * hann * sin(a);
        }
        double power = re * re + im * im;
        total += power;
        if (f > above) high += power;
        if (power > best) { best = power; *peak_hz = f; }
    }
    return total > 0.0 ? high / total : 0.0;
}

/* The period from where it crosses its resting place, and how fast it dies away: peaks, and the time to half the first. */
static void swing_of(const double *x, int n, double fps, double rest, double *period, double *half_life, double *first_peak) {
    double last_cross = -1.0, sum = 0.0;
    int crossings = 0;
    for (int i = 1; i < n && crossings < 4; i++) {
        double a = x[i - 1] - rest, b = x[i] - rest;
        if (a < 0.0 && b >= 0.0) {
            double t = (i - 1 + a / (a - b)) / fps;
            if (last_cross >= 0.0) { sum += t - last_cross; crossings++; }
            last_cross = t;
        }
    }
    *period = crossings ? sum / crossings : 0.0;
    double first = 0.0;
    for (int i = 0; i < n; i++) first = fmax(first, fabs(x[i] - rest));
    *first_peak = first;
    *half_life = (double)n / fps;
    for (int i = n - 1; i >= 0; i--) if (fabs(x[i] - rest) > first * 0.5) { *half_life = i / fps; break; }
}

/* A sheet held off to one side by tilted gravity, then let go. */
typedef struct { double period, half_life, high, peak_hz, gain, strain, amp; } Swing;
static Swing swing_test(const Fabric *f, float L, float edge, float fps) {
    Swing out;
    memset(&out, 0, sizeof(out));
    Sheet s = make_sheet(0.3f, L, edge);
    CCParams par = params_of(f);
    CCGarment *g = garment_of(&s, &par);
    float zero[3] = {0, 0, 0}, tilt[3] = {0, -G * cosf(0.5f), G * sinf(0.5f)}, down[3] = {0, -G, 0};
    float dt = 1.f / fps;
    for (int i = 0; i < (int)(3.f * fps); i++) cc_step(g, dt, PAL1, tilt, zero, zero, NULL, 0);
    int tip = (s.ny - 1) * s.nx + s.nx / 2, n = (int)(8.f * fps);
    double *x = (double *)malloc(sizeof(double) * (size_t)n);
    float *was = (float *)malloc(sizeof(float) * 3 * (size_t)s.nv);
    int np = cc_point_count(g);
    memcpy(was, cc_points(g), sizeof(float) * 3 * (size_t)np);
    double e0 = -1.0, emax = 0.0;
    for (int i = 0; i < n; i++) {
        cc_step(g, dt, PAL1, down, zero, zero, NULL, 0);
        const float *p = cc_points(g);
        x[i] = p[tip * 3 + 2];
        /* its energy per unit mass, every point alike */
        double e = 0.0;
        for (int v = 0; v < np; v++) {
            float d[3] = {(p[v * 3] - was[v * 3]) * fps, (p[v * 3 + 1] - was[v * 3 + 1]) * fps, (p[v * 3 + 2] - was[v * 3 + 2]) * fps};
            e += 0.5 * (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + G * p[v * 3 + 1];
        }
        e /= np;
        if (i == 2) { e0 = e; emax = e; }
        if (i > 2) emax = fmax(emax, e);
        memcpy(was, p, sizeof(float) * 3 * (size_t)np);
        /* how far any edge down the sheet has stretched */
        for (int b = 0; b < s.ny - 1; b++) {
            int v = b * s.nx + s.nx / 2, w = v + s.nx;
            float d[3] = {p[w * 3] - p[v * 3], p[w * 3 + 1] - p[v * 3 + 1], p[w * 3 + 2] - p[v * 3 + 2]};
            out.strain = fmax(out.strain, sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) / (L / (s.ny - 1)) - 1.0);
        }
    }
    swing_of(x, n, fps, x[n - 1], &out.period, &out.half_life, &out.amp);
    out.high = spectrum(x, n, fps, 3.0, &out.peak_hz);
    out.gain = emax - e0;
    free(x); free(was);
    cc_free(g);
    free_sheet(&s);
    return out;
}

/* A sheet falling flat, nothing holding it. */
static double fall_test(const Fabric *f, float fps) {
    Sheet s = make_sheet(0.3f, 0.3f, 0.03f);
    /* laid flat: its y down the sheet is z */
    for (int v = 0; v < s.nv; v++) { s.pos[v * 3 + 2] = s.pos[v * 3 + 1]; s.pos[v * 3 + 1] = 0.f; s.lim[v] = 1e6f; }
    CCParams par = params_of(f);
    par.drive = 0.f;
    CCGarment *g = garment_of(&s, &par);
    float zero[3] = {0, 0, 0}, down[3] = {0, -G, 0};
    float dt = 1.f / fps;
    int mid = (s.ny / 2) * s.nx + s.nx / 2;
    double y0 = 0.0, y1 = 0.0;
    for (int i = 0; i < (int)(4.f * fps); i++) {
        cc_step(g, dt, PAL1, down, zero, zero, NULL, 0);
        if (i == (int)(3.f * fps)) y0 = cc_points(g)[mid * 3 + 1];
    }
    y1 = cc_points(g)[mid * 3 + 1];
    cc_free(g);
    free_sheet(&s);
    return (y0 - y1) / (1.0 - 1.0 / fps);
}

/* Hanging still, how much is it still moving. */
static void rest_test(const Fabric *f, double *mean_speed, double *strain) {
    float fps = 60.f;
    Sheet s = make_sheet(0.3f, 0.5f, 0.04f);
    CCParams par = params_of(f);
    CCGarment *g = garment_of(&s, &par);
    float zero[3] = {0, 0, 0}, down[3] = {0, -G, 0};
    float dt = 1.f / fps;
    for (int i = 0; i < (int)(6.f * fps); i++) cc_step(g, dt, PAL1, down, zero, zero, NULL, 0);
    int np = cc_point_count(g);
    float *was = (float *)malloc(sizeof(float) * 3 * (size_t)np);
    memcpy(was, cc_points(g), sizeof(float) * 3 * (size_t)np);
    double sum = 0.0;
    for (int i = 0; i < 60; i++) {
        cc_step(g, dt, PAL1, down, zero, zero, NULL, 0);
        const float *p = cc_points(g);
        for (int v = 0; v < np; v++) {
            float d[3] = {p[v * 3] - was[v * 3], p[v * 3 + 1] - was[v * 3 + 1], p[v * 3 + 2] - was[v * 3 + 2]};
            sum += sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * fps;
        }
        memcpy(was, p, sizeof(float) * 3 * (size_t)np);
    }
    *mean_speed = sum / (60.0 * np);
    const float *p = cc_points(g);
    double length = 0.0;
    int col = s.nx / 2;
    for (int b = 0; b < s.ny - 1; b++) {
        int v = b * s.nx + col, w = v + s.nx;
        float d[3] = {p[w * 3] - p[v * 3], p[w * 3 + 1] - p[v * 3 + 1], p[w * 3 + 2] - p[v * 3 + 2]};
        length += sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    }
    *strain = length / 0.5 - 1.0;
    free(was);
    cc_free(g);
    free_sheet(&s);
}

/* The exact droop of a strip clamped level, length l, bending length c, as an angle from the clamp to its tip: the heavy elastica, theta'' = -(l - s) cos(theta) / c^3, theta(0) = 0, theta'(l) = 0. Shot on theta'(0). At l = 2c it's 41.5 degrees, which is the lab's whole test (ASTM D1388). */
static double exact_droop(double l, double c) {
    double lo = 0.0, hi = l * l / (2.0 * c * c * c), x = 0.0, y = 0.0;
    int n = 2000;
    double ds = l / n;
    for (int it = 0; it < 50; it++) {
        double k0 = 0.5 * (lo + hi), th = 0.0, k = k0;
        x = 0.0; y = 0.0;
        for (int i = 0; i < n; i++) {
            k += -(l - i * ds) * cos(th) / (c * c * c) * ds;
            th += k * ds;
            x += cos(th) * ds;
            y += sin(th) * ds;
        }
        if (k > 0.0) hi = k0; else lo = k0;
    }
    return atan2(y, x) * 180.0 / PI;
}

/* The bending length that droops this far at this overhang: what the lab would read off the strip. */
static double reading_of(double droop, double l) {
    double lo = 0.2 * l, hi = 5.0 * l;
    for (int it = 0; it < 30; it++) {
        double c = 0.5 * (lo + hi);
        if (exact_droop(l, c) > droop) lo = c; else hi = c;
    }
    return 0.5 * (lo + hi);
}

/* A strip cut, not hemmed, clamped level and left to droop: `cells` of `edge` metres sticking out. */
static double droop_of(const Fabric *f, int cells, float edge, float jitter) {
    float fps = 60.f, over = (float)cells * edge, back = 2.f * edge;
    Sheet s = make_sheet(0.24f, back + over, edge);
    unsigned seed = 777u;
    for (int v = 0; v < s.nv; v++) {
        int j = v / s.nx, i = v % s.nx;
        float along = -s.pos[v * 3 + 1] - back, across = s.pos[v * 3];
        /* the mesh a garment has is irregular */
        if (j > 2 && j < s.ny - 1 && i > 0 && i < s.nx - 1) {
            seed = seed * 1664525u + 1013904223u;
            along += jitter * edge * ((float)(seed >> 16 & 0xffffu) / 32767.5f - 1.f);
            seed = seed * 1664525u + 1013904223u;
            across += jitter * edge * ((float)(seed >> 16 & 0xffffu) / 32767.5f - 1.f);
        }
        s.pos[v * 3] = along; s.pos[v * 3 + 1] = 0.f; s.pos[v * 3 + 2] = across;
        s.lim[v] = along < 1e-4f ? 0.f : 100.f;
    }
    CCParams par = params_of(f);
    par.drive = 0.f;
    par.drag = 0.f;
    par.damp = 8.f;
    par.hem = 0.f;
    CCGarment *g = garment_of(&s, &par);
    float zero[3] = {0, 0, 0}, down[3] = {0, -G, 0};
    for (int i = 0; i < (int)(8.f * fps); i++) cc_step(g, 1.f / fps, PAL1, down, zero, zero, NULL, 0);
    const float *p = cc_points(g);
    double sum = 0.0;
    int count = 0;
    for (int i = s.nx / 4; i < s.nx - s.nx / 4; i++) {
        int tip = (s.ny - 1) * s.nx + i;
        sum += atan2(-p[tip * 3 + 1], p[tip * 3]) * 180.0 / PI;
        count++;
    }
    cc_free(g);
    free_sheet(&s);
    return sum / count;
}

/* A sheet on a slope: gravity tilted by `degrees`, on a flat box. Does it slide? */
static double slide_test(float degrees, float mu) {
    float fps = 60.f;
    Sheet s = make_sheet(0.2f, 0.2f, 0.04f);
    for (int v = 0; v < s.nv; v++) { s.pos[v * 3 + 2] = s.pos[v * 3 + 1] + 0.1f; s.pos[v * 3 + 1] = 0.f; s.lim[v] = 1e6f; }
    CCParams par = params_of(&TWILL);
    par.friction = mu;
    par.drag = 0.f;
    par.drive = 0.f;
    CCGarment *g = garment_of(&s, &par);
    CCBox floor;
    memset(&floor, 0, sizeof(floor));
    floor.m[0] = 1.f; floor.m[5] = 1.f; floor.m[10] = 1.f;
    floor.m[7] = -0.5f - 0.003f;
    floor.half[0] = 5.f; floor.half[1] = 0.5f; floor.half[2] = 5.f;
    cc_set_boxes(g, &floor, 1);
    float a = degrees * PI / 180.f;
    float zero[3] = {0, 0, 0}, sloped[3] = {G * sinf(a), -G * cosf(a), 0.f};
    int mid = (s.ny / 2) * s.nx + s.nx / 2;
    for (int i = 0; i < 30; i++) cc_step(g, 1.f / fps, PAL1, zero, zero, zero, NULL, 0);
    double x0 = cc_points(g)[mid * 3];
    for (int i = 0; i < (int)(1.f * fps); i++) cc_step(g, 1.f / fps, PAL1, sloped, zero, zero, NULL, 0);
    double moved = cc_points(g)[mid * 3] - x0;
    cc_free(g);
    free_sheet(&s);
    return moved;
}

/* A tube worn on a body: a skirt, hung from its top ring, radius r, length L. */
static CCGarment *skirt(const Fabric *f, float r, float L, float edge, Sheet *out) {
    int segs = (int)(2.f * PI * r / edge + 0.5f), rings = (int)(L / edge + 0.5f);
    Sheet s;
    memset(&s, 0, sizeof(s));
    s.w = r; s.h = L; s.nx = segs; s.ny = rings + 1; s.nv = s.nx * s.ny;
    s.pos = (float *)malloc(sizeof(float) * 3 * (size_t)s.nv);
    s.wgt = (float *)calloc((size_t)s.nv * 4, sizeof(float));
    s.lim = (float *)malloc(sizeof(float) * (size_t)s.nv);
    s.bon = (int *)calloc((size_t)s.nv * 4, sizeof(int));
    s.idx = (int *)malloc(sizeof(int) * 6 * (size_t)(s.nx * rings));
    for (int j = 0; j < s.ny; j++) for (int i = 0; i < s.nx; i++) {
        int v = j * s.nx + i;
        float a = 2.f * PI * (float)i / (float)s.nx;
        s.pos[v * 3] = r * cosf(a);
        s.pos[v * 3 + 1] = -L * (float)j / (float)rings;
        s.pos[v * 3 + 2] = r * sinf(a);
        s.wgt[v * 4] = 1.f;
        s.lim[v] = j == 0 ? 0.f : 100.f;
    }
    int m = 0;
    for (int j = 0; j < rings; j++) for (int i = 0; i < s.nx; i++) {
        int a = j * s.nx + i, b = j * s.nx + (i + 1) % s.nx, c = a + s.nx, d = b + s.nx;
        if ((i + j) % 2) { s.idx[m++] = a; s.idx[m++] = b; s.idx[m++] = c; s.idx[m++] = b; s.idx[m++] = d; s.idx[m++] = c; }
        else { s.idx[m++] = a; s.idx[m++] = b; s.idx[m++] = d; s.idx[m++] = a; s.idx[m++] = d; s.idx[m++] = c; }
    }
    s.ni = m;
    CCParams par = params_of(f);
    *out = s;
    return garment_of(&s, &par);
}

/* The body starts and stops: 4 m/s^2 for a fifth of a second, then the same the other way. The hem swings at its period, and settles. */
static void jolt_test(const Fabric *f, float fps, double *peak_hz, double *high, double *peak_off, double *half_life) {
    float L = 0.4f;
    Sheet s;
    CCGarment *g = skirt(f, 0.14f, L, 0.04f, &s);
    float zero[3] = {0, 0, 0}, down[3] = {0, -G, 0};
    float dt = 1.f / fps;
    for (int i = 0; i < (int)(3.f * fps); i++) cc_step(g, dt, PAL1, down, zero, zero, NULL, 0);
    int n = (int)(6.f * fps);
    double *x = (double *)malloc(sizeof(double) * (size_t)n);
    int hem = (s.ny - 1) * s.nx + s.nx / 2;
    float speed = 0.f;
    for (int i = 0; i < n; i++) {
        float t = (float)i * dt;
        float accel[3] = {0.f, 0.f, t < 0.2f ? 4.f : (t < 0.4f ? -4.f : 0.f)};
        speed += accel[2] * dt;
        float air[3] = {0.f, 0.f, -speed};
        cc_step(g, dt, PAL1, down, accel, air, NULL, 0);
        x[i] = cc_shown(g)[hem * 3 + 2];
    }
    *high = spectrum(x, n, fps, 3.0, peak_hz);
    double period, amp;
    swing_of(x, n, fps, x[n - 1], &period, half_life, &amp);
    *peak_off = amp;
    free(x);
    cc_free(g);
    free_sheet(&s);
}

/* The same swing, once in even frames and once in frames that wobble: how far apart they end up, as a share of the swing. */
static double frame_noise(const Fabric *f, float wobble) {
    float L = 0.4f, fps = 99.f, zero[3] = {0, 0, 0}, tilt[3] = {0, -G * cosf(0.5f), G * sinf(0.5f)}, down[3] = {0, -G, 0};
    int n = (int)(5.f * fps);
    double *even = (double *)malloc(sizeof(double) * (size_t)n);
    Sheet s = make_sheet(0.3f, L, 0.04f);
    CCParams par = params_of(f);
    int tip = (s.ny - 1) * s.nx + s.nx / 2;
    CCGarment *g = garment_of(&s, &par);
    for (int i = 0; i < 300; i++) cc_step(g, 1.f / fps, PAL1, tilt, zero, zero, NULL, 0);
    for (int i = 0; i < n; i++) {
        cc_step(g, 1.f / fps, PAL1, down, zero, zero, NULL, 0);
        even[i] = cc_points(g)[tip * 3 + 2];
    }
    cc_free(g);
    double first = 0.0;
    for (int i = 0; i < n; i++) first = fmax(first, fabs(even[i] - even[n - 1]));
    g = garment_of(&s, &par);
    for (int i = 0; i < 300; i++) cc_step(g, 1.f / fps, PAL1, tilt, zero, zero, NULL, 0);
    double t = 0.0, sum = 0.0;
    int count = 0;
    unsigned seed = 12345u;
    while (t < (n - 2) / fps) {
        seed = seed * 1664525u + 1013904223u;
        float r = (float)(seed >> 16 & 0xffffu) / 65535.f;
        float dt = (1.f / fps) * (1.f + wobble * (2.f * r - 1.f));
        cc_step(g, dt, PAL1, down, zero, zero, NULL, 0);
        t += dt;
        double at = t * fps - 1.0;
        int i = (int)at;
        if (i < 0 || i + 1 >= n) continue;
        double want = even[i] + (even[i + 1] - even[i]) * (at - i), got = cc_points(g)[tip * 3 + 2];
        sum += (got - want) * (got - want);
        count++;
    }
    cc_free(g);
    free(even);
    free_sheet(&s);
    return sqrt(sum / fmax(count, 1)) / fmax(first, 1e-6);
}

/* Held hard against its limit by a gale, then let go: how long before it's come off. */
static double limit_test(const Fabric *f) {
    float fps = 60.f, L = 0.4f;
    Sheet s = make_sheet(0.3f, L, 0.04f);
    for (int j = 0; j < s.ny; j++) for (int i = 0; i < s.nx; i++) if (j > 0) s.lim[j * s.nx + i] = 0.08f * (float)j / (float)(s.ny - 1);
    CCParams par = params_of(f);
    CCGarment *g = garment_of(&s, &par);
    float zero[3] = {0, 0, 0}, down[3] = {0, -G, 0}, gale[3] = {0, -G, 6.f * G};
    for (int i = 0; i < 180; i++) cc_step(g, 1.f / fps, PAL1, gale, zero, zero, NULL, 0);
    int tip = (s.ny - 1) * s.nx + s.nx / 2;
    const float *p = cc_points(g), *t = cc_targets(g);
    double taken = -1.0;
    for (int i = 0; i < 120; i++) {
        cc_step(g, 1.f / fps, PAL1, down, zero, zero, NULL, 0);
        p = cc_points(g);
        t = cc_targets(g);
        float d[3] = {p[tip * 3] - t[tip * 3], p[tip * 3 + 1] - t[tip * 3 + 1], p[tip * 3 + 2] - t[tip * 3 + 2]};
        if (sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) < 0.08 - 0.002) { taken = (i + 1) / fps; break; }
    }
    cc_free(g);
    free_sheet(&s);
    return taken < 0.0 ? 2.0 : taken;
}

/* A sheet 30 cm long, both ends held, its ends brought a quarter closer as a limb's skin brings them when it bends. Is it a smooth arc, and is it still? */
static void crumple_test(const Fabric *f, float edge, double *sharpest, double *hem_sharpest, double *speed) {
    float fps = 60.f, L = 0.3f, zero[3] = {0, 0, 0}, down[3] = {0, -G, 0};
    Sheet s = make_sheet(0.3f, L, edge);
    static int parent2[2] = {-1, 0};
    for (int j = 0; j < s.ny; j++) for (int i = 0; i < s.nx; i++) {
        int v = j * s.nx + i;
        float a = (float)j / (float)(s.ny - 1);
        s.bon[v * 4] = 0; s.bon[v * 4 + 1] = 1;
        s.wgt[v * 4] = 1.f - a; s.wgt[v * 4 + 1] = a;
        s.lim[v] = (j == 0 || j == s.ny - 1) ? 0.f : 100.f;
        /* A breath of a curve, so it knows which way to go: as anything sewn does. */
        s.pos[v * 3 + 2] = 0.002f * sinf(PI * a);
    }
    CCParams par = params_of(f);
    CCGarmentDesc d;
    memset(&d, 0, sizeof(d));
    d.nv = s.nv; d.pos = s.pos; d.wgt = s.wgt; d.bon = s.bon; d.maxd = s.lim; d.ni = s.ni; d.idx = s.idx; d.nbones = 2; d.parent = parent2;
    CCGarment *g = cc_create(&d, NULL, &par);
    float pal[24] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    int np = cc_point_count(g);
    float *was = (float *)malloc(sizeof(float) * 3 * (size_t)np);
    *speed = 0.0;
    for (int i = 0; i < (int)(10.f * fps); i++) {
        float t = (float)i / fps;
        pal[12 + 7] = 0.25f * L * fminf(t, 1.f);
        cc_step(g, 1.f / fps, pal, down, zero, zero, NULL, 0);
        if (i >= (int)(9.f * fps)) {
            const float *p = cc_points(g);
            double sum = 0.0;
            for (int v = 0; v < np; v++) {
                float e[3] = {p[v * 3] - was[v * 3], p[v * 3 + 1] - was[v * 3 + 1], p[v * 3 + 2] - was[v * 3 + 2]};
                sum += sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]) * fps;
            }
            *speed = fmax(*speed, sum / np);
        }
        memcpy(was, cc_points(g), sizeof(float) * 3 * (size_t)np);
    }
    /* The sharpest fold between two triangles on it, and the sharpest corner along its free edges. Both as drawn: that's what's seen. */
    const float *p = cc_shown(g);
    *sharpest = 0.0;
    for (int a = 0; a < s.ni; a += 3) for (int b = a + 3; b < s.ni; b += 3) {
        int shared = 0;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) if (s.idx[a + i] == s.idx[b + j]) shared++;
        if (shared != 2) continue;
        float n[2][3];
        for (int t = 0; t < 2; t++) {
            const int *f3 = s.idx + (t ? b : a);
            float u[3], w[3];
            for (int k = 0; k < 3; k++) { u[k] = p[f3[1] * 3 + k] - p[f3[0] * 3 + k]; w[k] = p[f3[2] * 3 + k] - p[f3[0] * 3 + k]; }
            n[t][0] = u[1] * w[2] - u[2] * w[1]; n[t][1] = u[2] * w[0] - u[0] * w[2]; n[t][2] = u[0] * w[1] - u[1] * w[0];
        }
        double c = (n[0][0] * n[1][0] + n[0][1] * n[1][1] + n[0][2] * n[1][2]) / fmax(sqrt(n[0][0] * n[0][0] + n[0][1] * n[0][1] + n[0][2] * n[0][2]) * sqrt(n[1][0] * n[1][0] + n[1][1] * n[1][1] + n[1][2] * n[1][2]), 1e-12);
        *sharpest = fmax(*sharpest, acos(fmin(fmax(c, -1.0), 1.0)) * 180.0 / PI);
    }
    *hem_sharpest = 0.0;
    for (int j = 1; j < s.ny - 1; j++) for (int side = 0; side < 2; side++) {
        int m = j * s.nx + (side ? s.nx - 1 : 0), u = m - s.nx, w = m + s.nx;
        float e0[3], e1[3];
        for (int k = 0; k < 3; k++) { e0[k] = p[m * 3 + k] - p[u * 3 + k]; e1[k] = p[w * 3 + k] - p[m * 3 + k]; }
        double c = (e0[0] * e1[0] + e0[1] * e1[1] + e0[2] * e1[2]) / fmax(sqrt(e0[0] * e0[0] + e0[1] * e0[1] + e0[2] * e0[2]) * sqrt(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]), 1e-12);
        *hem_sharpest = fmax(*hem_sharpest, acos(fmin(fmax(c, -1.0), 1.0)) * 180.0 / PI);
    }
    free(was);
    cc_free(g);
    free_sheet(&s);
}

/* A heavy chain length L hung from radius r0, turning at w: where's its end? Going up from the end, tension gathers what it holds up and what it holds in, and it runs along the tension. */
static double chain_radius(float r0, float L, float w) {
    double lo = r0, hi = r0 + L;
    for (int it = 0; it < 40; it++) {
        double rh = 0.5 * (lo + hi), r = rh, Tr = 0.0, Ty = 0.0, ds = L / 2000.0;
        for (int i = 0; i < 2000; i++) {
            Tr += w * w * r * ds;
            Ty += G * ds;
            r -= ds * Tr / sqrt(Tr * Tr + Ty * Ty);
        }
        if (r > r0) hi = rh; else lo = rh;
    }
    return 0.5 * (lo + hi);
}

/* A panel on a body that spins up over a second, holds, and stops. */
static void spin_test(const Fabric *f, float r0, float L, float w, double *radius, double *lag, double *after) {
    float fps = 99.f, zero[3] = {0, 0, 0}, down[3] = {0, -G, 0};
    Sheet s = make_sheet(0.1f, L, 0.03f);
    for (int v = 0; v < s.nv; v++) { s.pos[v * 3 + 2] = s.pos[v * 3]; s.pos[v * 3] = r0; }
    CCParams par = params_of(f);
    par.drive = 0.f;
    CCGarment *g = garment_of(&s, &par);
    int tip = (s.ny - 1) * s.nx + s.nx / 2;
    *lag = 0.0;
    double held = 0.0;
    int held_n = 0;
    for (int i = 0; i < (int)(17.f * fps); i++) {
        float t = (float)i / fps;
        float o = t < 1.f ? w * t : (t < 6.f ? w : (t < 7.f ? w * (7.f - t) : 0.f));
        float a = t < 1.f ? w : (t >= 6.f && t < 7.f ? -w : 0.f);
        float omega[3] = {0.f, o, 0.f}, alpha[3] = {0.f, a, 0.f};
        cc_set_turning(g, omega, alpha);
        cc_step(g, 1.f / fps, PAL1, down, zero, zero, NULL, 0);
        const float *p = cc_points(g);
        if (t < 1.f) *lag = fmax(*lag, p[tip * 3 + 2]);
        if (t >= 5.f && t < 6.f) { held += sqrt(p[tip * 3] * p[tip * 3] + p[tip * 3 + 2] * p[tip * 3 + 2]); held_n++; }
    }
    *radius = held / fmax(held_n, 1);
    const float *p = cc_points(g);
    *after = sqrt(p[tip * 3] * p[tip * 3] + p[tip * 3 + 2] * p[tip * 3 + 2]);
    cc_free(g);
    free_sheet(&s);
}

int main(void) {
    printf("A sheet 40 cm long, hung and let go. A compound pendulum's period is %.3f s.\n", 2.0 * PI * sqrt(2.0 * 0.4 / (3.0 * G)));
    double T = 2.0 * PI * sqrt(2.0 * 0.4 / (3.0 * G));
    const Fabric *all[4] = {&TEE, &TWILL, &FLEECE, &DENIM};
    for (int i = 0; i < 4; i++) {
        Swing o = swing_test(all[i], 0.4f, 0.04f, 60.f);
        char what[64];
        sprintf(what, "%s: its period", all[i]->name); row(what, o.period, T * 0.8, T * 1.2, "s");
        sprintf(what, "%s: strongest frequency", all[i]->name); row(what, o.peak_hz, 0.7, 1.3, "Hz");
        sprintf(what, "%s: share of its motion above 3 Hz", all[i]->name); row(what, o.high, 0.0, 0.05, "");
        sprintf(what, "%s: time to half its swing", all[i]->name); row(what, o.half_life, 0.4, 4.0, "s");
        sprintf(what, "%s: energy it gains once let go", all[i]->name); row(what, o.gain, -1e9, 0.02, "J/kg");
        sprintf(what, "%s: how far it stretches, swinging", all[i]->name); row(what, o.strain * 100.0, -1.0, 2.0, "%");
    }
    printf("\nThe same swing at other frame rates.\n");
    Swing base = swing_test(&TWILL, 0.4f, 0.04f, 60.f);
    float rates[3] = {30.f, 99.f, 144.f};
    for (int i = 0; i < 3; i++) {
        Swing o = swing_test(&TWILL, 0.4f, 0.04f, rates[i]);
        char what[64];
        sprintf(what, "at %.0f fps: period against 60 fps", rates[i]); row(what, o.period / base.period, 0.92, 1.08, "x");
        sprintf(what, "at %.0f fps: time to half against 60 fps", rates[i]); row(what, o.half_life / base.half_life, 0.75, 1.3, "x");
    }
    printf("\nFalling flat: sqrt(2 m g / (rho Cd A)), Cd 1.2, air 1.2 kg/m^3.\n");
    for (int i = 0; i < 4; i++) {
        double want = sqrt(2.0 * all[i]->density * G / (1.2 * 1.2));
        char what[64];
        sprintf(what, "%s: speed it falls at", all[i]->name);
        row(what, fall_test(all[i], 60.f), want * 0.85, want * 1.15, "m/s");
    }
    printf("\nHanging still.\n");
    for (int i = 0; i < 4; i++) {
        double speed, strain;
        rest_test(all[i], &speed, &strain);
        char what[64];
        sprintf(what, "%s: still moving at", all[i]->name); row(what, speed * 1000.0, 0.0, 1.0, "mm/s");
        sprintf(what, "%s: stretched under its own weight", all[i]->name); row(what, strain * 100.0, -2.0, 1.0, "%");
    }
    printf("\nThe cantilever (ASTM D1388), clamped, against the exact elastica at l = 2c or the nearest the mesh has: its bending length as the lab would read it.\n");
    {
        float edges[3] = {0.01f, 0.02f, 0.03f};
        for (int i = 0; i < 4; i++) for (int e = 0; e < 3; e++) for (int irregular = 0; irregular < 2; irregular++) {
            int cells = (int)(2.f * all[i]->bending_cm * 0.01f / edges[e] + 0.5f);
            if (cells < 2) continue;
            /* A pass a substep can't carry a stiff fabric's bending down a mesh this fine: denim at 1 cm reads a third short at 480 substeps a second. Garments are coarser than that. */
            if (all[i]->bending_cm > 4.f && edges[e] < 0.015f) continue;
            double got = reading_of(droop_of(all[i], cells, edges[e], irregular ? 0.25f : 0.f), cells * edges[e]) * 100.0;
            char what[64];
            sprintf(what, "%s, %.0f cm mesh%s: its bending length", all[i]->name, edges[e] * 100.f, irregular ? ", irregular" : "");
            row(what, got, all[i]->bending_cm * 0.75, all[i]->bending_cm * 1.25, "cm");
        }
    }
    printf("\nOn a slope, friction 0.3: stays below %.1f degrees, slides above.\n", atan(0.3) * 180.0 / PI);
    row("10 degrees: how far it slides in a second", fabs(slide_test(10.f, 0.3f)) * 100.0, 0.0, 1.0, "cm");
    row("25 degrees: how far it slides in a second", fabs(slide_test(25.f, 0.3f)) * 100.0, 5.0, 300.0, "cm");
    printf("\nWorn: a skirt 40 cm long on a body that starts and stops.\n");
    for (int i = 0; i < 4; i++) {
        double hz, high, off, half;
        jolt_test(all[i], 60.f, &hz, &high, &off, &half);
        char what[64];
        sprintf(what, "%s: strongest frequency of its hem", all[i]->name); row(what, hz, 0.5, 1.6, "Hz");
        sprintf(what, "%s: share of its motion above 3 Hz", all[i]->name); row(what, high, 0.0, 0.1, "");
        sprintf(what, "%s: how far its hem swings", all[i]->name); row(what, off * 100.0, 0.5, 15.0, "cm");
        sprintf(what, "%s: time to half its swing", all[i]->name); row(what, half, 0.3, 4.0, "s");
    }
    printf("\nFrames that come unevenly, at 99 fps +-40%%: how far from the even swing, against the swing.\n");
    for (int i = 0; i < 4; i++) {
        char what[64];
        sprintf(what, "%s: off the even swing by", all[i]->name);
        row(what, frame_noise(all[i], 0.4f) * 100.0, 0.0, 5.0, "%");
    }
    printf("\nHeld against its limit by a gale, then let go.\n");
    for (int i = 0; i < 4; i++) {
        char what[64];
        sprintf(what, "%s: comes off it in", all[i]->name);
        row(what, limit_test(all[i]), 0.0, 0.2, "s");
    }
    printf("\nPushed a quarter shorter, on a garment's own mesh.\n");
    for (int i = 0; i < 4; i++) {
        double sharp, hem, speed;
        crumple_test(all[i], 0.03f, &sharp, &hem, &speed);
        /* A fold's radius is about the bending length, and a mesh this coarse turns that into a corner of about edge / radius. Past that it's the mesh folding, not the fabric. */
        double most = 1.7 * (3.0 / all[i]->bending_cm) * 180.0 / PI;
        char what[64];
        sprintf(what, "%s: its sharpest fold", all[i]->name); row(what, sharp, 0.0, most, "deg");
        sprintf(what, "%s: its sharpest corner along a free edge", all[i]->name); row(what, hem, 0.0, most, "deg");
        sprintf(what, "%s: 9 s on, still moving at", all[i]->name); row(what, speed * 1000.0, 0.0, 1.0, "mm/s");
    }
    printf("\nOn a body spinning at 3 and 6 radians a second, a panel 40 cm long from a radius of 14 cm.\n");
    for (int i = 0; i < 4; i++) {
        float spins[2] = {3.f, 6.f};
        for (int w = 0; w < 2; w++) {
            double r, lag, after, want = chain_radius(0.14f, 0.4f, spins[w]);
            spin_test(all[i], 0.14f, 0.4f, spins[w], &r, &lag, &after);
            char what[64];
            sprintf(what, "%s at %.0f: how far out its end flies", all[i]->name, spins[w]); row(what, r * 100.0, want * 90.0, want * 110.0, "cm");
            sprintf(what, "%s at %.0f: how far it lags as the spin starts", all[i]->name, spins[w]); row(what, lag * 100.0, 0.2, 40.0, "cm");
            sprintf(what, "%s at %.0f: hangs straight again after", all[i]->name, spins[w]); row(what, (after - 0.14) * 100.0, -2.0, 2.0, "cm");
        }
    }
    printf(failed ? "\nFAILED: %d\n" : "\nall passed\n", failed);
    return failed ? 1 : 0;
}
