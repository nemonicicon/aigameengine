/* clubcloth.c -- worn cloth. See clubcloth.h for what it is and why. Order inside a substep is the design: air and gravity, then the cloth's own shape, then whatever pushes it, the backstop and the limit last so they win. */
#include "clubcloth.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CC_MAX_CAPS 16
#define CC_NEAR_MIN 0.005f
/* How far a point may be from a capsule, at the start of a frame, and still be tested against it during that frame. A knee moves about 5 cm in a frame at 99 Hz. */
#define CC_CAP_MARGIN 0.12f
/* The most the skin may push what a point's held to (metres). A limp body folds past where any plane through its skin means anything. */
#define CC_TARGET_OUT 0.08f
/* How far from what's under it a point may be and still test it (metres). */
#define CC_UNDER_REACH 0.09f
#define CC_UNDER_FACING 0.2f
/* Deeper than this behind the nearest point's plane it isn't a contact (metres). A contact's never let get this deep, so it's the plane that's wrong: a concave crotch's side, the far side of a limb. Cloth that moves freely finds those, and each was a jump of 9 cm in a frame. */
#define CC_UNDER_DEEP 0.03f
/* When the nearest point changes, the plane it's kept outside fades across in this long (seconds), and what it's held to follows in this long. Going straight there is a jump of up to an edge's length, at frame rate: 15 to 30 Hz. */
#define CC_PLANE_FADE 0.04f
#define CC_TARGET_FADE 0.06f
/* The fastest anything pushes a point out (m/s), on top of however fast what's pushing it is coming: a point found inside is walked out, not thrown. */
#define CC_PUSH_SPEED 1.5f
/* A garment's parts meet where they don't face the same way: an inner thigh and an inner thigh, or the front of one leg against the side of the other. Two front panels side by side have the same way out, and one being behind the other's plane means nothing. */
#define CC_PART_FACING 0.3f
/* Substeps a garment settles for, unseen, when it's put on: a tenth of a second. */
#define CC_SETTLE 48
/* What's covered tucks this far under what covers it (metres), never more than this far out to begin with, eased at this much a frame. */
#define CC_TUCK_UNDER 0.006f
#define CC_TUCK_MOST 0.07f
#define CC_TUCK_EASE 0.35f
/* How far outside a box cloth keeps (metres). */
#define CC_BOX_GAP 0.006f
/* How far past the wrong side of a board a point may be and still be brought back (metres). */
#define CC_BOX_REACH 0.12f
#define CC_PART_SOFT 0.12f
/* How far out of its neighbours a point may stand before it's drawn nearer them, as a share of the length of its edges. */
#define CC_SPIKE_FROM 0.3f
#define CC_SPIKE_TO 0.7f
#define CC_SPIKE_EASE 0.25f
/* A point's mass is never less than this share of the average: a sliver left by decimation isn't a speck of dust. */
#define CC_MASS_LEAST 0.25f
/* What the bench's cantilever reads: without it the lab would read every fabric 10 to 14% short, on any mesh from 1 to 3 cm, regular or not. */
#define CC_BEND_CAL 1.4f
/* Past this a triangle's a sliver, and its cotangent says nothing about the cloth. */
#define CC_COT_MOST 8.f
/* Air on a flat plate is +0.8 on the side it comes from and -0.4 on the side it leaves. A loose sheet has both, on one surface. Worn, only one side of each is in the air. As a share of the sheet's 1.2. */
#define CC_AIR_WINDWARD 0.667f
#define CC_AIR_LEEWARD 0.333f
/* Under compression an edge gives way: a real fabric buckles at a scale far finer than these points, and on a coarse mesh that shows as the edge getting shorter. Held hard, it buckles at the mesh's own scale: a scribble. */
#define CC_EDGE_PRESS 0.1f

typedef struct { float x, y, z; } V3;
/* Bergou's quadratic bending: a vector of curvature C = sum k[i] x[i], and an energy 1/2 C^2 / comp. No square root, linear at small angles where a distance across a hinge has no stiffness at all, and the same fabric whatever the mesh. Bergou et al. 2006, A Quadratic Bending Model for Inextensible Surfaces. */
typedef struct { int i[4]; float k[4]; float wk; float comp; float loss; } Hinge;

struct CCGarment {
    CCParams par;
    int nv, np, nt, ne;
    int nbones;
    int *vert_point;            /* nv: each drawn vertex's point */
    float (*rest)[3];           /* np */
    float (*wgt)[CC_BONES];
    int (*bon)[CC_BONES];
    float *maxd;
    int *tri;                   /* nt * 3, in points */
    float *invm;                /* np: 1 / its mass, kg. 0: it's the skin's */
    int *e0, *e1;
    float *elen, *ecomp;        /* ne: its rest length, and its compliance, m/N: 0 along a thread, the fabric's shear on the bias */
    Hinge *hinge;               /* what resists folding: each edge between two triangles, and each point of a free edge */
    float (*hc_rest)[3], (*hc0)[3], (*hc1)[3]; /* each hinge's curvature as it was cut, and that turned with the limb, as a frame starts and as it ends */
    int nhinge;
    int *anchor;                /* np: the nearest point that doesn't move, along the cloth */
    float *tlen;                /* np: how far that is */
    /* the backstop */
    float (*bs_pos)[3], (*bs_nrm)[3], (*bs_wgt)[CC_BONES];
    int (*bs_bon)[CC_BONES];
    int *bs_bone;               /* np: the bone the skin under it is mostly on */
    float *near_;               /* np: how close to the skin it may come */
    /* this frame */
    float (*cur)[3], (*prv)[3], (*tg0)[3], (*tg1)[3], (*bs0)[3], (*bs1)[3], (*bn0)[3], (*bn1)[3], (*nrm)[3], (*vnrm)[3];
    float (*raw)[3], (*raw0)[3];/* np: the skinned pose, as the bones put it, before anything's pushed it out of anything; and as this frame began */
    float (*off)[3], (*shown)[3];   /* what's drawn: its offset from the skinned pose, and where that puts it */
    float *allow;               /* per capsule, per point: how far from the capsule's axis it's held, CC_MAX_CAPS * np */
    int *cand;                  /* per capsule, a list of points near it: CC_MAX_CAPS * np */
    int ncand[CC_MAX_CAPS];
    CCCapsule cap0[CC_MAX_CAPS];
    int ncap0;
    int fresh;
    int carried;                /* the next cc_pose takes everything through carry */
    float carry[12];
    int settle;                 /* substeps it's still to settle for, unseen, having just been put on: the skinned pose isn't at its rest lengths, and it'd snap to them at 8 m/s in the first frame */
    float h_was;                /* the last frame's substep: speed is a distance per substep, so when it changes every point's has to */
    float omega[3], alpha[3];   /* how the skeleton's turning */
    float (*push)[3], (*wind)[3];   /* np: per substep, this frame. What the world does to a point, and the air's speed past it */
    float air_out, air_in;      /* the share of drag on the side the air comes from and the side it leaves */
    float out;                  /* 1 or -1: which way round this garment's triangles wind, against which way is out of the body */
    int *adj_start, *adj;       /* each point's neighbours along the cloth */
    int *parent;                /* the palette's parents */
    /* what's over this one reads these: as they were when this frame's pose was taken, so a thread solving it can read them while this one's solved */
    float (*off_was)[3], (*nrm_was)[3];
    /* the garment this one's worn over */
    CCGarment *under;
    int *u_near;                /* np: its nearest point, -1 when this point can never meet it */
    float *u_gap;               /* np: how far outside it this point keeps */
    float (*u_n)[3], (*u_dn)[3];/* np: the plane it keeps outside: its normal, and what's left of the last nearest point's */
    float *u_dh;                /* np: and what's left of the last one's height */
    char *u_on;                 /* np: was near it last frame */
    float (*t_out)[3], (*t_want)[3];/* np: how far what it's held to is out of what's under it, and how far it would like to be */
    float dt_was;
    int *u_list;                /* the points near it this frame */
    int u_count;
    /* the garment worn over this one: what it covers tucks under it, as drawn */
    const CCGarment *over;
    int *o_near;                /* np: its nearest point, -1 when it doesn't cover this point at rest */
    float (*o_tuck)[3];         /* np: how far this point's drawn in from where it's solved */
    /* a garment's parts, the two legs of a pair: each keeps outside the others */
    int *sh;                    /* np: which part, 0 when there's only one */
    int *s_near;                /* np: the nearest point on another part, -1 for none */
    float *s_gap;
    int *s_list;
    int s_n;
    /* what he's carrying or standing on */
    CCBox box[CC_MAX_BOXES];
    int nbox;
    int *box_cand;              /* CC_MAX_BOXES * np */
    int box_n[CC_MAX_BOXES];
    /* a free edge: each point's two neighbours along it */
    int *bnd_a, *bnd_b;
    /* what's drawn: one level of Loop subdivision of the points, so a fold or a hem reads as cloth and not as a few big triangles. The solver never sees it. */
    int *vtri;                  /* nt * 3, in the caller's drawn vertices */
    int *e_opp0, *e_opp1;       /* ne: the points opposite each edge, -1 on a free edge */
    int dp;                     /* refined points: np + ne */
    float (*dpos)[3], (*dnrm)[3], (*spike)[3], (*spike_was)[3], (*spike_off)[3];
    int ndt;
    int *dtri;                  /* ndt * 3, in refined points */
    int dv;                     /* refined drawn vertices: nv + a vertex on each drawn edge */
    int *dv_point, *dv_a, *dv_b;/* dv: its refined point, and the two drawn vertices it's between, -1 for one of the caller's */
    int *didx;                  /* ndt * 3, in refined drawn vertices */
};

static void refine(CCGarment *g);
static void build_drawn(CCGarment *g);

static float dot3(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float len3(const float *a) { return sqrtf(dot3(a, a)); }
static void sub3(const float *a, const float *b, float *o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static void cross3(const float *a, const float *b, float *o) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
static float clampf_(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
/* The cotangent of the angle between two vectors. */
static float cot_of(const float *a, const float *b) {
    float c[3];
    cross3(a, b, c);
    return clampf_(dot3(a, b) / fmaxf(len3(c), 1e-12f), -CC_COT_MOST, CC_COT_MOST);
}

void cc_fabric(CCParams *p, float density, float bending_length, float shear) {
    const float g = 9.81f, rho = 1.2f, cd = 1.2f;
    p->density = density;
    p->bend = density * g * bending_length * bending_length * bending_length;
    p->drag = 0.5f * rho * cd / density;
    p->shear = shear;
}

void cc_default_params(CCParams *p) {
    cc_fabric(p, 0.2f, 0.025f, 4.f);
    p->drive = 0.5f;
    p->damp = 0.3f;
    p->flex_damp = 2e-6f;
    p->hem = 0.08f;
    p->substep = 1.0f / 480.0f;
    p->slack = 0.012f;
    p->friction = 0.25f;
    p->lag = 0.03f;
}

/* How much of a hinge's rate of folding goes in a substep: a Kelvin-Voigt damper whose time is the fabric's losses over its rigidity, solved implicitly so it can't overshoot. */
static void hinge_loss(const CCGarment *g, Hinge *H) {
    float tau = g->par.flex_damp * g->par.density / fmaxf(g->par.bend, 1e-12f);
    float s = H->wk / fmaxf(H->comp, 1e-9f) * tau * g->par.substep;
    H->loss = s / (1.f + s);
}

void cc_set_turning(CCGarment *g, const float omega[3], const float alpha[3]) {
    memcpy(g->omega, omega, sizeof(float) * 3);
    memcpy(g->alpha, alpha, sizeof(float) * 3);
}

/* The nearest point on a triangle, as barycentric weights. */
static void closest_tri(const float *p, const float *a, const float *b, const float *c, float *ou, float *ov, float *ow) {
    float ab[3], ac[3], ap[3], bp[3], cp[3];
    sub3(b, a, ab); sub3(c, a, ac); sub3(p, a, ap);
    float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) { *ou = 1.f; *ov = 0.f; *ow = 0.f; return; }
    sub3(p, b, bp);
    float d3 = dot3(ab, bp), d4 = dot3(ac, bp);
    if (d3 >= 0.f && d4 <= d3) { *ou = 0.f; *ov = 1.f; *ow = 0.f; return; }
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) { float v = d1 / (d1 - d3); *ou = 1.f - v; *ov = v; *ow = 0.f; return; }
    sub3(p, c, cp);
    float d5 = dot3(ab, cp), d6 = dot3(ac, cp);
    if (d6 >= 0.f && d5 <= d6) { *ou = 0.f; *ov = 0.f; *ow = 1.f; return; }
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) { float w = d2 / (d2 - d6); *ou = 1.f - w; *ov = 0.f; *ow = w; return; }
    float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) { float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); *ou = 0.f; *ov = 1.f - w; *ow = w; return; }
    float den = 1.f / (va + vb + vc);
    *ov = vb * den; *ow = vc * den; *ou = 1.f - *ov - *ow;
}

/* A point's weights: the strongest CC_BONES of a blend. */
typedef struct { int b; float w; } BW;
static void bw_add(BW *l, int *n, int b, float w) {
    if (w <= 0.f) return;
    for (int i = 0; i < *n; i++) if (l[i].b == b) { l[i].w += w; return; }
    if (*n < 16) { l[*n].b = b; l[*n].w = w; (*n)++; }
}
static int bw_cmp(const void *a, const void *b) { float d = ((const BW *)b)->w - ((const BW *)a)->w; return d > 0.f ? 1 : (d < 0.f ? -1 : 0); }

static int key_cmp(const void *a, const void *b) {
    unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}
typedef struct { unsigned long long key; int opp; } EdgeRec;
static int edge_cmp(const void *a, const void *b) { return key_cmp(&((const EdgeRec *)a)->key, &((const EdgeRec *)b)->key); }

static void *cc_alloc(size_t n) { void *p = calloc(n ? n : 1, 1); return p; }

static void find_anchors(CCGarment *g, const int *adj_start, const int *adj) {
    /* Along the cloth, from every point that doesn't move: Dijkstra with a plain scan, a few hundred points once at load. */
    int np = g->np;
    char *done = (char *)cc_alloc((size_t)np);
    for (int i = 0; i < np; i++) {
        if (g->maxd[i] <= 0.f) { g->anchor[i] = i; g->tlen[i] = 0.f; }
        else { g->anchor[i] = -1; g->tlen[i] = 1e9f; }
    }
    for (;;) {
        int best = -1;
        float bd = 1e8f;
        for (int i = 0; i < np; i++) if (!done[i] && g->tlen[i] < bd) { bd = g->tlen[i]; best = i; }
        if (best < 0) break;
        done[best] = 1;
        for (int k = adj_start[best]; k < adj_start[best + 1]; k++) {
            int j = adj[k];
            float d[3];
            sub3(g->rest[j], g->rest[best], d);
            float nd = bd + len3(d);
            if (nd < g->tlen[j]) { g->tlen[j] = nd; g->anchor[j] = g->anchor[best]; }
        }
    }
    for (int i = 0; i < np; i++) if (g->anchor[i] < 0) { g->tlen[i] = 0.f; }
    free(done);
}

CCGarment *cc_create(const CCGarmentDesc *d, const CCBody *body, const CCParams *par) {
    CCGarment *g = (CCGarment *)cc_alloc(sizeof(CCGarment));
    if (par) g->par = *par; else cc_default_params(&g->par);
    g->nv = d->nv;
    g->nbones = d->nbones;
    g->fresh = 1;
    g->vert_point = (int *)cc_alloc(sizeof(int) * (size_t)d->nv);
    /* Weld the drawn vertices back into points, by where they are. */
    int hsize = d->nv * 4 + 16;
    int *hash = (int *)malloc(sizeof(int) * (size_t)hsize);
    for (int i = 0; i < hsize; i++) hash[i] = -1;
    int *first = (int *)cc_alloc(sizeof(int) * (size_t)d->nv);
    int np = 0;
    for (int v = 0; v < d->nv; v++) {
        const float *p = d->pos + v * 3;
        long kx = lroundf(p[0] * 2000.f), ky = lroundf(p[1] * 2000.f), kz = lroundf(p[2] * 2000.f);
        unsigned h = ((unsigned)kx * 73856093u ^ (unsigned)ky * 19349663u ^ (unsigned)kz * 83492791u) % (unsigned)hsize;
        for (;;) {
            int e = hash[h];
            if (e < 0) { hash[h] = np; first[np] = v; g->vert_point[v] = np; np++; break; }
            const float *q = d->pos + first[e] * 3;
            if (lroundf(q[0] * 2000.f) == kx && lroundf(q[1] * 2000.f) == ky && lroundf(q[2] * 2000.f) == kz) { g->vert_point[v] = e; break; }
            h = (h + 1) % (unsigned)hsize;
        }
    }
    free(hash);
    g->np = np;
    g->rest = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->wgt = cc_alloc(sizeof(float[CC_BONES]) * (size_t)np);
    g->bon = cc_alloc(sizeof(int[CC_BONES]) * (size_t)np);
    g->maxd = cc_alloc(sizeof(float) * (size_t)np);
    g->near_ = cc_alloc(sizeof(float) * (size_t)np);
    for (int i = 0; i < np; i++) {
        int v = first[i];
        memcpy(g->rest[i], d->pos + v * 3, sizeof(float) * 3);
        memcpy(g->wgt[i], d->wgt + v * CC_BONES, sizeof(float) * CC_BONES);
        memcpy(g->bon[i], d->bon + v * CC_BONES, sizeof(int) * CC_BONES);
        g->maxd[i] = d->maxd ? d->maxd[v] : 0.1f;
        g->near_[i] = d->near_ ? d->near_[v] : -1.f;
    }
    free(first);
    /* Triangles, in points; the degenerate ones a weld leaves go. */
    g->tri = (int *)cc_alloc(sizeof(int) * (size_t)d->ni);
    g->vtri = (int *)cc_alloc(sizeof(int) * (size_t)d->ni);
    int nt = 0;
    for (int t = 0; t + 2 < d->ni; t += 3) {
        int a = g->vert_point[d->idx[t]], b = g->vert_point[d->idx[t + 1]], c = g->vert_point[d->idx[t + 2]];
        if (a == b || b == c || a == c) continue;
        g->tri[nt * 3] = a; g->tri[nt * 3 + 1] = b; g->tri[nt * 3 + 2] = c;
        g->vtri[nt * 3] = d->idx[t]; g->vtri[nt * 3 + 1] = d->idx[t + 1]; g->vtri[nt * 3 + 2] = d->idx[t + 2];
        nt++;
    }
    g->nt = nt;
    /* Edges: every one once. A pair of triangles across an edge is a bend, between the two points opposite it. */
    EdgeRec *rec = (EdgeRec *)malloc(sizeof(EdgeRec) * (size_t)(nt * 3 + 1));
    int nr = 0;
    for (int t = 0; t < nt; t++) {
        const int *f = g->tri + t * 3;
        for (int k = 0; k < 3; k++) {
            unsigned a = (unsigned)f[k], b = (unsigned)f[(k + 1) % 3];
            unsigned lo = a < b ? a : b, hi = a < b ? b : a;
            rec[nr].key = ((unsigned long long)lo << 32) | hi;
            rec[nr].opp = f[(k + 2) % 3];
            nr++;
        }
    }
    qsort(rec, (size_t)nr, sizeof(EdgeRec), edge_cmp);
    g->e0 = (int *)cc_alloc(sizeof(int) * (size_t)nr);
    g->e1 = (int *)cc_alloc(sizeof(int) * (size_t)nr);
    g->elen = (float *)cc_alloc(sizeof(float) * (size_t)nr);
    /* Its mass: a third of every triangle it's on, times what the fabric weighs. All points alike would have an irregular mesh's small triangles swinging like its big ones. */
    {
        float *area = (float *)cc_alloc(sizeof(float) * (size_t)np);
        float total = 0.f;
        for (int t = 0; t < nt; t++) {
            const int *f = g->tri + t * 3;
            float ab[3], ac[3], n[3];
            sub3(g->rest[f[1]], g->rest[f[0]], ab);
            sub3(g->rest[f[2]], g->rest[f[0]], ac);
            cross3(ab, ac, n);
            float a = 0.5f * len3(n);
            total += a;
            for (int q = 0; q < 3; q++) area[f[q]] += a / 3.f;
        }
        g->invm = (float *)cc_alloc(sizeof(float) * (size_t)np);
        float least = CC_MASS_LEAST * total / (float)np;
        for (int p = 0; p < np; p++) g->invm[p] = g->maxd[p] > 0.f ? 1.f / (g->par.density * fmaxf(area[p], fmaxf(least, 1e-7f))) : 0.f;
        free(area);
    }
    g->hinge = (Hinge *)cc_alloc(sizeof(Hinge) * (size_t)(nr + np));
    g->e_opp0 = (int *)cc_alloc(sizeof(int) * (size_t)nr);
    g->e_opp1 = (int *)cc_alloc(sizeof(int) * (size_t)nr);
    g->bnd_a = (int *)malloc(sizeof(int) * (size_t)np);
    g->bnd_b = (int *)malloc(sizeof(int) * (size_t)np);
    for (int p = 0; p < np; p++) g->bnd_a[p] = g->bnd_b[p] = -1;
    int ne = 0, nh = 0;
    for (int i = 0; i < nr;) {
        int j = i + 1;
        while (j < nr && rec[j].key == rec[i].key) j++;
        int a = (int)(rec[i].key >> 32), b = (int)(rec[i].key & 0xffffffffu);
        float dd[3];
        sub3(g->rest[a], g->rest[b], dd);
        g->e_opp0[ne] = rec[i].opp;
        g->e_opp1[ne] = j - i >= 2 ? rec[i + 1].opp : -1;
        g->e0[ne] = a; g->e1[ne] = b; g->elen[ne] = len3(dd); ne++;
        if (j - i == 1) {
            if (g->bnd_a[a] < 0) g->bnd_a[a] = b; else g->bnd_b[a] = b;
            if (g->bnd_a[b] < 0) g->bnd_a[b] = a; else g->bnd_b[b] = a;
        }
        if (j - i >= 2 && rec[i].opp != rec[i + 1].opp) {
            /* Bergou's weights: the cotangents of the four corners on this edge. */
            int c = rec[i].opp, e = rec[i + 1].opp;
            const float *x0 = g->rest[a], *x1 = g->rest[b], *x2 = g->rest[c], *x3 = g->rest[e];
            float e0[3], e1[3], e2[3], e3[3], e4[3], m0[3], n0[3], n1[3];
            sub3(x1, x0, e0); sub3(x2, x0, e1); sub3(x3, x0, e2); sub3(x2, x1, e3); sub3(x3, x1, e4);
            m0[0] = -e0[0]; m0[1] = -e0[1]; m0[2] = -e0[2];
            float c01 = cot_of(e0, e1), c02 = cot_of(e0, e2), c03 = cot_of(m0, e3), c04 = cot_of(m0, e4);
            cross3(e0, e1, n0); cross3(e0, e2, n1);
            float area2 = 0.5f * (len3(n0) + len3(n1));
            Hinge *H = &g->hinge[nh];
            H->i[0] = a; H->i[1] = b; H->i[2] = c; H->i[3] = e;
            H->k[0] = c03 + c04; H->k[1] = c01 + c02; H->k[2] = -c01 - c03; H->k[3] = -c02 - c04;
            H->wk = 0.f;
            for (int q = 0; q < 4; q++) H->wk += g->invm[H->i[q]] * H->k[q] * H->k[q];
            /* Bergou has 3 / (A0 + A1) for the energy's weight. On a fold along one axis that lands all of the curvature on the edges across it and none on the others, three times the fabric's own energy: the weight that gives G k^2 / 2 is 1 / (A0 + A1). A hinge on a line that doesn't move has only half a cell to bend in, so twice that: then a cantilever's right to 1 + 1/n^2 on n cells, where without it it's (1 + 1/n)^2. */
            H->comp = area2 / fmaxf(CC_BEND_CAL * g->par.bend, 1e-12f);
            if (g->invm[a] <= 0.f && g->invm[b] <= 0.f) H->comp *= 0.5f;
            if (H->wk > 0.f) { hinge_loss(g, H); nh++; }
        }
        i = j;
    }
    g->ne = ne;
    free(rec);
    /* A free edge is folded twice over in anything sewn: a rod along it, so it curves and doesn't zigzag. */
    for (int p = 0; p < np && g->par.hem > 0.f; p++) {
        int a = g->bnd_a[p], b = g->bnd_b[p];
        if (a < 0 || b < 0) continue;
        float da[3], db[3];
        sub3(g->rest[a], g->rest[p], da);
        sub3(g->rest[b], g->rest[p], db);
        float l = 0.5f * (len3(da) + len3(db));
        Hinge *H = &g->hinge[nh];
        H->i[0] = a; H->i[1] = b; H->i[2] = p; H->i[3] = p;
        H->k[0] = 1.f; H->k[1] = 1.f; H->k[2] = -1.f; H->k[3] = -1.f;
        H->wk = g->invm[a] + g->invm[b] + 4.f * g->invm[p];
        H->comp = l * l * l / fmaxf(g->par.hem * g->par.bend, 1e-12f);
        if (H->wk > 0.f) { hinge_loss(g, H); nh++; }
    }
    g->nhinge = nh;
    g->hc0 = cc_alloc(sizeof(float[3]) * (size_t)(nh + 1));
    g->hc_rest = cc_alloc(sizeof(float[3]) * (size_t)(nh + 1));
    for (int e = 0; e < nh; e++) {
        const Hinge *H = &g->hinge[e];
        for (int k = 0; k < 3; k++) g->hc_rest[e][k] = H->k[0] * g->rest[H->i[0]][k] + H->k[1] * g->rest[H->i[1]][k] + H->k[2] * g->rest[H->i[2]][k] + H->k[3] * g->rest[H->i[3]][k];
    }
    g->hc1 = cc_alloc(sizeof(float[3]) * (size_t)(nh + 1));
    /* Along the cloth, the nearest point that doesn't move: a tether, so a hem can be lifted and never dragged below its own length. */
    int *cnt = (int *)cc_alloc(sizeof(int) * (size_t)(np + 1));
    for (int e = 0; e < ne; e++) { cnt[g->e0[e]]++; cnt[g->e1[e]]++; }
    int *start = (int *)cc_alloc(sizeof(int) * (size_t)(np + 2));
    for (int i = 0; i < np; i++) start[i + 1] = start[i] + cnt[i];
    int *adj = (int *)cc_alloc(sizeof(int) * (size_t)(start[np] + 1));
    int *fill = (int *)cc_alloc(sizeof(int) * (size_t)(np + 1));
    memcpy(fill, start, sizeof(int) * (size_t)np);
    for (int e = 0; e < ne; e++) { adj[fill[g->e0[e]]++] = g->e1[e]; adj[fill[g->e1[e]]++] = g->e0[e]; }
    g->anchor = (int *)cc_alloc(sizeof(int) * (size_t)np);
    g->tlen = (float *)cc_alloc(sizeof(float) * (size_t)np);
    find_anchors(g, start, adj);
    /* Which way its threads run: down from what holds it, and across that. A sleeve hangs along itself from a shoulder and a shirt down itself from a collar, whatever the pose it was sewn in. Each edge's compliance goes by how far it is on the bias: an orthotropic sheet's 1/E = sin^2 cos^2 / G, its E along a thread being far past anything here. */
    {
        g->ecomp = (float *)cc_alloc(sizeof(float) * (size_t)ne);
        float *share = (float *)cc_alloc(sizeof(float) * (size_t)ne);
        for (int t = 0; t < nt; t++) {
            const int *f = g->tri + t * 3;
            float ab[3], ac[3], n[3];
            sub3(g->rest[f[1]], g->rest[f[0]], ab);
            sub3(g->rest[f[2]], g->rest[f[0]], ac);
            cross3(ab, ac, n);
            float a = 0.5f * len3(n) / 3.f;
            for (int q = 0; q < 3; q++) {
                int x = f[q], y = f[(q + 1) % 3];
                int lo = x < y ? x : y, hi = x < y ? y : x, l = 0, r = ne - 1;
                while (l <= r) {
                    int m = (l + r) / 2;
                    if (g->e0[m] == lo && g->e1[m] == hi) { share[m] += a; break; }
                    if (g->e0[m] < lo || (g->e0[m] == lo && g->e1[m] < hi)) l = m + 1; else r = m - 1;
                }
            }
        }
        for (int e = 0; e < ne; e++) {
            int a = g->e0[e], b = g->e1[e], from = g->anchor[a] >= 0 ? g->anchor[a] : g->anchor[b];
            if (from < 0 || g->par.shear <= 0.f || share[e] <= 0.f) continue;
            float mid[3], way[3], along[3];
            for (int k = 0; k < 3; k++) mid[k] = 0.5f * (g->rest[a][k] + g->rest[b][k]);
            sub3(mid, g->rest[from], way);
            sub3(g->rest[b], g->rest[a], along);
            float w2 = dot3(way, way), l2 = dot3(along, along);
            if (w2 < 1e-8f || l2 < 1e-8f) continue;
            float d = dot3(way, along), c2 = d * d / (w2 * l2);
            g->ecomp[e] = c2 * (1.f - c2) / g->par.shear * l2 / share[e];
        }
        free(share);
    }
    free(cnt); free(fill);
    g->adj_start = start;
    g->adj = adj;
    g->parent = (int *)cc_alloc(sizeof(int) * (size_t)(d->nbones + 1));
    for (int b = 0; b < d->nbones; b++) g->parent[b] = d->parent ? d->parent[b] : -1;
    /* The backstop: for each point, the skin under it, only on its own bones and their parents. */
    g->bs_pos = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->bs_nrm = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->bs_wgt = cc_alloc(sizeof(float[CC_BONES]) * (size_t)np);
    g->bs_bon = cc_alloc(sizeof(int[CC_BONES]) * (size_t)np);
    g->bs_bone = (int *)cc_alloc(sizeof(int) * (size_t)np);
    int ntb = body ? body->ni / 3 : 0;
    float (*tc)[4] = cc_alloc(sizeof(float[4]) * (size_t)(ntb + 1));
    float (*tn)[3] = cc_alloc(sizeof(float[3]) * (size_t)(ntb + 1));
    int *tdom = (int *)cc_alloc(sizeof(int) * (size_t)(ntb + 1));
    for (int t = 0; t < ntb; t++) {
        const int *f = body->idx + t * 3;
        const float *a = body->pos + f[0] * 3, *b = body->pos + f[1] * 3, *c = body->pos + f[2] * 3;
        float cc[3], ab[3], ac[3];
        for (int k = 0; k < 3; k++) cc[k] = (a[k] + b[k] + c[k]) / 3.f;
        float r = 0.f, dd[3];
        sub3(a, cc, dd); r = fmaxf(r, len3(dd));
        sub3(b, cc, dd); r = fmaxf(r, len3(dd));
        sub3(c, cc, dd); r = fmaxf(r, len3(dd));
        tc[t][0] = cc[0]; tc[t][1] = cc[1]; tc[t][2] = cc[2]; tc[t][3] = r;
        sub3(b, a, ab); sub3(c, a, ac); cross3(ab, ac, tn[t]);
        float l = len3(tn[t]);
        if (l > 1e-12f) for (int k = 0; k < 3; k++) tn[t][k] /= l;
        BW bw[16]; int nbw = 0;
        for (int q = 0; q < 3; q++) for (int w = 0; w < CC_BONES; w++) bw_add(bw, &nbw, body->bon[f[q] * CC_BONES + w], body->wgt[f[q] * CC_BONES + w]);
        if (nbw) qsort(bw, (size_t)nbw, sizeof(BW), bw_cmp);
        tdom[t] = nbw ? bw[0].b : -1;
    }
    char *allowed = (char *)cc_alloc((size_t)(d->nbones + 1));
    for (int p = 0; p < np; p++) {
        memset(allowed, 0, (size_t)d->nbones);
        for (int w = 0; w < CC_BONES; w++) {
            if (g->wgt[p][w] < 0.05f) continue;
            int b = g->bon[p][w];
            if (b < 0 || b >= d->nbones) continue;
            allowed[b] = 1;
            if (d->parent && d->parent[b] >= 0) allowed[d->parent[b]] = 1;
        }
        const float *x = g->rest[p];
        for (int pass = 0; pass < 2; pass++) {
            float bd = 1e9f; int bt = -1; float bu = 0.f, bv = 0.f, bw_ = 0.f;
            for (int t = 0; t < ntb; t++) {
                if (pass == 0 && (tdom[t] < 0 || !allowed[tdom[t]])) continue;
                float dd[3];
                sub3(x, tc[t], dd);
                float dc = len3(dd) - tc[t][3];
                if (dc >= bd) continue;
                const int *f = body->idx + t * 3;
                float u, v, w;
                closest_tri(x, body->pos + f[0] * 3, body->pos + f[1] * 3, body->pos + f[2] * 3, &u, &v, &w);
                float q[3];
                for (int k = 0; k < 3; k++) q[k] = body->pos[f[0] * 3 + k] * u + body->pos[f[1] * 3 + k] * v + body->pos[f[2] * 3 + k] * w;
                sub3(x, q, dd);
                float dist = len3(dd);
                if (dist < bd) { bd = dist; bt = t; bu = u; bv = v; bw_ = w; }
            }
            if (bt < 0) continue;
            const int *f = body->idx + bt * 3;
            for (int k = 0; k < 3; k++) g->bs_pos[p][k] = body->pos[f[0] * 3 + k] * bu + body->pos[f[1] * 3 + k] * bv + body->pos[f[2] * 3 + k] * bw_;
            memcpy(g->bs_nrm[p], tn[bt], sizeof(float) * 3);
            BW bl[16]; int nbl = 0;
            const float bar[3] = {bu, bv, bw_};
            for (int q = 0; q < 3; q++) for (int w = 0; w < CC_BONES; w++) bw_add(bl, &nbl, body->bon[f[q] * CC_BONES + w], body->wgt[f[q] * CC_BONES + w] * bar[q]);
            qsort(bl, (size_t)nbl, sizeof(BW), bw_cmp);
            float tot = 0.f;
            for (int w = 0; w < CC_BONES && w < nbl; w++) tot += bl[w].w;
            for (int w = 0; w < CC_BONES; w++) {
                if (w < nbl) { g->bs_bon[p][w] = bl[w].b; g->bs_wgt[p][w] = bl[w].w / (tot > 0.f ? tot : 1.f); }
                else { g->bs_bon[p][w] = 0; g->bs_wgt[p][w] = 0.f; }
            }
            g->bs_bone[p] = nbl ? bl[0].b : -1;
            break;
        }
        float dd[3];
        sub3(x, g->bs_pos[p], dd);
        float d0 = dot3(dd, g->bs_nrm[p]);
        /* The skin's normal may face in, on a surface wound the wrong way: out is the side this point is on. */
        if (d0 < 0.f) {
            for (int k = 0; k < 3; k++) g->bs_nrm[p][k] = -g->bs_nrm[p][k];
            d0 = -d0;
        }
        float want = g->near_[p] >= 0.f ? g->near_[p] : d0 - g->par.slack;
        /* At rest the backstop moves nothing: a tight spot's own distance is its allowance. */
        if (want > d0) want = d0;
        if (want < CC_NEAR_MIN && d0 >= CC_NEAR_MIN) want = CC_NEAR_MIN;
        g->near_[p] = want;
    }
    free(allowed); free(tc); free(tn); free(tdom);
    /* Which way out is: an engine winds its triangles as it likes (Godot's front faces go the other way from glTF's), and a normal pointing into the body lights the cloth from behind and sends the shadow's offset the wrong way, as a stipple all over it. */
    float agree = 0.f;
    for (int t = 0; t < nt; t++) {
        const int *f = g->tri + t * 3;
        float ab[3], ac[3], n[3];
        sub3(g->rest[f[1]], g->rest[f[0]], ab);
        sub3(g->rest[f[2]], g->rest[f[0]], ac);
        cross3(ab, ac, n);
        agree += dot3(n, g->bs_nrm[f[0]]);
    }
    g->out = agree < 0.f ? -1.f : 1.f;
    /* A loose sheet has air on both sides, so all of it either way. Worn, the inside is out of it. */
    g->air_out = ntb > 0 ? CC_AIR_WINDWARD : 1.f;
    g->air_in = ntb > 0 ? CC_AIR_LEEWARD : 1.f;
    g->push = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->wind = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->cur = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->prv = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->tg0 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->tg1 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->bs0 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->bs1 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->bn0 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->bn1 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->nrm = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->cand = (int *)cc_alloc(sizeof(int) * (size_t)(CC_MAX_CAPS * np));
    g->off = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->raw = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->raw0 = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->shown = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->allow = (float *)cc_alloc(sizeof(float) * (size_t)(CC_MAX_CAPS * np));
    for (int i = 0; i < CC_MAX_CAPS * np; i++) g->allow[i] = 1e9f;
    g->off_was = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->nrm_was = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->u_near = (int *)cc_alloc(sizeof(int) * (size_t)np);
    g->u_gap = (float *)cc_alloc(sizeof(float) * (size_t)np);
    g->u_list = (int *)cc_alloc(sizeof(int) * (size_t)np);
    g->u_n = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->u_dn = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->u_dh = (float *)cc_alloc(sizeof(float) * (size_t)np);
    g->u_on = (char *)cc_alloc((size_t)np);
    g->t_out = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->t_want = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->dt_was = 1.f / 60.f;
    g->o_near = (int *)malloc(sizeof(int) * (size_t)np);
    g->o_tuck = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->sh = (int *)cc_alloc(sizeof(int) * (size_t)np);
    g->s_near = (int *)malloc(sizeof(int) * (size_t)np);
    g->s_gap = (float *)cc_alloc(sizeof(float) * (size_t)np);
    g->s_list = (int *)cc_alloc(sizeof(int) * (size_t)np);
    g->box_cand = (int *)cc_alloc(sizeof(int) * (size_t)(CC_MAX_BOXES * np));
    for (int p = 0; p < np; p++) { g->o_near[p] = -1; g->s_near[p] = -1; }
    g->spike = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->spike_was = cc_alloc(sizeof(float[3]) * (size_t)np);
    g->spike_off = cc_alloc(sizeof(float[3]) * (size_t)np);
    build_drawn(g);
    memcpy(g->shown, g->rest, sizeof(float[3]) * (size_t)np);
    refine(g);
    return g;
}

void cc_free(CCGarment *g) {
    if (!g) return;
    free(g->vert_point); free(g->rest); free(g->wgt); free(g->bon); free(g->maxd); free(g->tri);
    free(g->e0); free(g->e1); free(g->elen); free(g->ecomp); free(g->invm); free(g->hinge); free(g->hc_rest); free(g->hc0); free(g->hc1); free(g->push); free(g->wind);
    free(g->anchor); free(g->tlen);
    free(g->bs_pos); free(g->bs_nrm); free(g->bs_wgt); free(g->bs_bon); free(g->bs_bone); free(g->near_);
    free(g->cur); free(g->prv); free(g->tg0); free(g->tg1); free(g->bs0); free(g->bs1); free(g->bn0); free(g->bn1); free(g->nrm);
    free(g->cand); free(g->off); free(g->shown); free(g->allow);
    free(g->adj_start); free(g->adj); free(g->parent);
    free(g->off_was); free(g->nrm_was); free(g->u_near); free(g->u_gap); free(g->u_list); free(g->u_n); free(g->u_dn); free(g->u_dh); free(g->u_on); free(g->t_out); free(g->t_want);
    free(g->o_near); free(g->o_tuck); free(g->sh); free(g->s_near); free(g->s_gap); free(g->s_list); free(g->box_cand);
    free(g->bnd_a); free(g->bnd_b);
    free(g->vtri); free(g->e_opp0); free(g->e_opp1); free(g->dpos); free(g->dnrm); free(g->spike); free(g->spike_was); free(g->spike_off); free(g->raw); free(g->raw0); free(g->dtri); free(g->dv_point); free(g->dv_a); free(g->dv_b); free(g->didx);
    free(g);
}

void cc_reset(CCGarment *g) { g->fresh = 1; }

void cc_carry(CCGarment *g, const float m[12]) {
    memcpy(g->carry, m, sizeof(float) * 12);
    g->carried = 1;
}

static void carry_dir(const float *m, float *v) {
    float x = v[0], y = v[1], z = v[2];
    v[0] = m[0] * x + m[1] * y + m[2] * z;
    v[1] = m[4] * x + m[5] * y + m[6] * z;
    v[2] = m[8] * x + m[9] * y + m[10] * z;
}

static void carry_point(const float *m, float *v) {
    carry_dir(m, v);
    v[0] += m[3]; v[1] += m[7]; v[2] += m[11];
}

void cc_set_capsule_rest(CCGarment *g, int c, const float a[3], const float b[3], float r) {
    if (c < 0 || c >= CC_MAX_CAPS) return;
    float ab[3];
    sub3(b, a, ab);
    float ab2 = dot3(ab, ab);
    for (int p = 0; p < g->np; p++) {
        float ap[3];
        sub3(g->rest[p], a, ap);
        float t = ab2 > 1e-12f ? clampf_(dot3(ap, ab) / ab2, 0.f, 1.f) : 0.f;
        float q[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
        float d = len3(q);
        g->allow[c * g->np + p] = d < r ? d : r;
    }
}

static void skin_point(const float *pal, const float *p, const int *bon, const float *wgt, float *o) {
    float x = 0.f, y = 0.f, z = 0.f;
    for (int w = 0; w < CC_BONES; w++) {
        float a = wgt[w];
        if (a <= 0.f) continue;
        const float *m = pal + bon[w] * 12;
        x += a * (m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3]);
        y += a * (m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7]);
        z += a * (m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11]);
    }
    o[0] = x; o[1] = y; o[2] = z;
}

static void skin_dir(const float *pal, const float *p, const int *bon, const float *wgt, float *o) {
    float x = 0.f, y = 0.f, z = 0.f;
    for (int w = 0; w < CC_BONES; w++) {
        float a = wgt[w];
        if (a <= 0.f) continue;
        const float *m = pal + bon[w] * 12;
        x += a * (m[0] * p[0] + m[1] * p[1] + m[2] * p[2]);
        y += a * (m[4] * p[0] + m[5] * p[1] + m[6] * p[2]);
        z += a * (m[8] * p[0] + m[9] * p[1] + m[10] * p[2]);
    }
    float l = sqrtf(x * x + y * y + z * z);
    if (l > 1e-9f) { x /= l; y /= l; z /= l; }
    o[0] = x; o[1] = y; o[2] = z;
}

/* What's drawn: what's solved, at fabric's own pace. Everything here is against the skinned pose as the bones put it, so the body's own motion goes straight through, and what moves slowly is the cloth's offset from it. The first cloth that ran moved about too much, glitching, and didn't react the way fabric does. It averaged 0.2 m/s against his body and changed that by 17 m/s^2: a buzz at 13 Hz. Contacts correct a point in a substep and a target's pushed out of the skin in a frame, and all of that lands here first. */
static void steady(CCGarment *g, float dt) {
    float a = 1.f - expf(-dt / fmaxf(g->par.lag, 1e-3f));
    for (int p = 0; p < g->np; p++) {
        const float *want = g->maxd[p] <= 0.f ? g->tg1[p] : g->cur[p];
        for (int k = 0; k < 3; k++) {
            g->off[p][k] += (want[k] - g->raw[p][k] - g->off[p][k]) * a;
            g->shown[p][k] = g->raw[p][k] + g->off[p][k];
        }
    }
}

/* What a garment over this one covers stays under it, as drawn. The top keeps its points outside this one, but its triangles are big, and a ridge of what's under comes out between three of them. It's out of sight there, so it can go anywhere: in. Eased, so nothing pops when a hem lifts off it. */
static void tuck_under(CCGarment *g) {
    const CCGarment *o = g->over;
    if (!o) return;
    for (int p = 0; p < g->np; p++) {
        int k = g->o_near[p];
        if (k < 0) continue;
        float *x = g->shown[p];
        float b[3], d[3];
        b[0] = o->raw[k][0] + o->off_was[k][0]; b[1] = o->raw[k][1] + o->off_was[k][1]; b[2] = o->raw[k][2] + o->off_was[k][2];
        sub3(x, b, d);
        float bd = dot3(d, d);
        for (int step = 0; step < 32; step++) {
            int bj = k;
            for (int q = o->adj_start[k]; q < o->adj_start[k + 1]; q++) {
                int c = o->adj[q];
                float e[3] = {x[0] - o->raw[c][0] - o->off_was[c][0], x[1] - o->raw[c][1] - o->off_was[c][1], x[2] - o->raw[c][2] - o->off_was[c][2]};
                float dd = dot3(e, e);
                if (dd < bd) { bd = dd; bj = c; }
            }
            if (bj == k) break;
            k = bj;
        }
        g->o_near[p] = k;
        b[0] = o->raw[k][0] + o->off_was[k][0]; b[1] = o->raw[k][1] + o->off_was[k][1]; b[2] = o->raw[k][2] + o->off_was[k][2];
        sub3(x, b, d);
        const float *n = o->nrm_was[k];
        float out = dot3(d, n), want = 0.f;
        /* Past a hem it's not covered. More than this far out it isn't a ridge, it's a thigh in front of a belly. Facing another way, it's something else again. */
        if (o->bnd_a[k] < 0 && out > -CC_TUCK_UNDER && out < CC_TUCK_MOST && bd < CC_UNDER_REACH * CC_UNDER_REACH && dot3(n, g->bn1[p]) > 0.2f) want = out + CC_TUCK_UNDER;
        /* Eased as a vector: the nearest point's normal changes when the nearest point does, and along that normal it'd jump by all of it. */
        float *t = g->o_tuck[p];
        for (int q = 0; q < 3; q++) { t[q] += (n[q] * want - t[q]) * CC_TUCK_EASE; x[q] -= t[q]; }
    }
}

/* Cloth this coarse can only crumple where it folds hard, and the skinned pose it's held near crushes itself at every joint: a point sticking out of its neighbours by a good part of an edge's length is noise, and what's left of its fold reads better drawn nearer them. Only what's drawn. Points that are held get it too: an armpit's are, and that's where it's worst. */
static void smooth_spikes(CCGarment *g) {
    float (*before)[3] = g->spike_was;
    memcpy(before, g->shown, sizeof(float[3]) * (size_t)g->np);
    for (int pass = 0; pass < 2; pass++) {
        for (int p = 0; p < g->np; p++) {
            float *o = g->spike[p];
            const float *x = g->shown[p];
            o[0] = x[0]; o[1] = x[1]; o[2] = x[2];
            int n = g->adj_start[p + 1] - g->adj_start[p];
            if (n < 3 || g->bnd_a[p] >= 0) continue;
            float mid[3] = {0.f, 0.f, 0.f}, reach = 0.f;
            for (int i = g->adj_start[p]; i < g->adj_start[p + 1]; i++) {
                const float *q = g->shown[g->adj[i]];
                float d[3];
                sub3(q, x, d);
                reach += len3(d);
                mid[0] += q[0]; mid[1] += q[1]; mid[2] += q[2];
            }
            float way[3] = {mid[0] / (float)n - x[0], mid[1] / (float)n - x[1], mid[2] / (float)n - x[2]};
            float out = len3(way) * (float)n / fmaxf(reach, 1e-6f);
            float t = clampf_((out - CC_SPIKE_FROM) / (CC_SPIKE_TO - CC_SPIKE_FROM), 0.f, 1.f);
            t = t * t * (3.f - 2.f * t) * 0.8f;
            o[0] += way[0] * t; o[1] += way[1] * t; o[2] += way[2] * t;
        }
        memcpy(g->shown, g->spike, sizeof(float[3]) * (size_t)g->np);
    }
    /* All of that eased: a fold that goes over to its other side has its neighbours' middle on the other side too, and what's drawn would jump the whole way across. */
    for (int p = 0; p < g->np; p++) for (int k = 0; k < 3; k++) {
        g->spike_off[p][k] += (g->shown[p][k] - before[p][k] - g->spike_off[p][k]) * CC_SPIKE_EASE;
        g->shown[p][k] = before[p][k] + g->spike_off[p][k];
    }
}

/* One level of Loop subdivision of what's drawn. */
static void refine(CCGarment *g) {
    int np = g->np;
    const float (*s)[3] = (const float (*)[3])g->shown;
    float (*o)[3] = g->dpos;
    for (int p = 0; p < np; p++) {
        int a = g->bnd_a[p], b = g->bnd_b[p];
        if (a >= 0 && b >= 0) {
            for (int k = 0; k < 3; k++) o[p][k] = 0.75f * s[p][k] + 0.125f * (s[a][k] + s[b][k]);
            continue;
        }
        int n = g->adj_start[p + 1] - g->adj_start[p];
        if (n < 3 || a >= 0) { o[p][0] = s[p][0]; o[p][1] = s[p][1]; o[p][2] = s[p][2]; continue; }
        float beta = n == 3 ? 0.1875f : 0.375f / (float)n, sum[3] = {0.f, 0.f, 0.f};
        for (int i = g->adj_start[p]; i < g->adj_start[p + 1]; i++) {
            const float *q = s[g->adj[i]];
            sum[0] += q[0]; sum[1] += q[1]; sum[2] += q[2];
        }
        float keep = 1.f - (float)n * beta;
        for (int k = 0; k < 3; k++) o[p][k] = keep * s[p][k] + beta * sum[k];
    }
    for (int e = 0; e < g->ne; e++) {
        const float *a = s[g->e0[e]], *b = s[g->e1[e]];
        float *m = o[np + e];
        int c = g->e_opp0[e], d = g->e_opp1[e];
        if (c >= 0 && d >= 0) for (int k = 0; k < 3; k++) m[k] = 0.375f * (a[k] + b[k]) + 0.125f * (s[c][k] + s[d][k]);
        else for (int k = 0; k < 3; k++) m[k] = 0.5f * (a[k] + b[k]);
    }
    memset(g->dnrm, 0, sizeof(float[3]) * (size_t)g->dp);
    for (int t = 0; t < g->ndt; t++) {
        const int *f = g->dtri + t * 3;
        float ab[3], ac[3], n[3];
        sub3(o[f[1]], o[f[0]], ab);
        sub3(o[f[2]], o[f[0]], ac);
        cross3(ab, ac, n);
        for (int q = 0; q < 3; q++) { float *r = g->dnrm[f[q]]; r[0] += n[0]; r[1] += n[1]; r[2] += n[2]; }
    }
    for (int p = 0; p < g->dp; p++) {
        float *r = g->dnrm[p];
        float l = len3(r);
        if (l > 1e-12f) { float k = g->out / l; r[0] *= k; r[1] *= k; r[2] *= k; }
        else { r[0] = 0.f; r[1] = 1.f; r[2] = 0.f; }
    }
}

typedef struct { unsigned long long key; int at; } DrawnEdge;
static int drawn_edge_cmp(const void *a, const void *b) { return key_cmp(&((const DrawnEdge *)a)->key, &((const DrawnEdge *)b)->key); }

/* The edge between two points: e0 and e1 come sorted. */
static int edge_of(const CCGarment *g, int a, int b) {
    int lo = a < b ? a : b, hi = a < b ? b : a, l = 0, r = g->ne - 1;
    while (l <= r) {
        int m = (l + r) / 2;
        if (g->e0[m] == lo && g->e1[m] == hi) return m;
        if (g->e0[m] < lo || (g->e0[m] == lo && g->e1[m] < hi)) l = m + 1; else r = m - 1;
    }
    return 0;
}

static void build_drawn(CCGarment *g) {
    int nt = g->nt, np = g->np;
    g->dp = np + g->ne;
    g->dpos = cc_alloc(sizeof(float[3]) * (size_t)g->dp);
    g->dnrm = cc_alloc(sizeof(float[3]) * (size_t)g->dp);
    DrawnEdge *r = (DrawnEdge *)malloc(sizeof(DrawnEdge) * (size_t)(nt * 3 + 1));
    for (int t = 0; t < nt; t++) for (int s = 0; s < 3; s++) {
        unsigned a = (unsigned)g->vtri[t * 3 + s], b = (unsigned)g->vtri[t * 3 + (s + 1) % 3];
        r[t * 3 + s].key = ((unsigned long long)(a < b ? a : b) << 32) | (a < b ? b : a);
        r[t * 3 + s].at = t * 3 + s;
    }
    qsort(r, (size_t)(nt * 3), sizeof(DrawnEdge), drawn_edge_cmp);
    int unique = 0;
    for (int i = 0; i < nt * 3; i++) if (i == 0 || r[i].key != r[i - 1].key) unique++;
    g->dv = g->nv + unique;
    g->dv_point = (int *)malloc(sizeof(int) * (size_t)g->dv);
    g->dv_a = (int *)malloc(sizeof(int) * (size_t)g->dv);
    g->dv_b = (int *)malloc(sizeof(int) * (size_t)g->dv);
    for (int v = 0; v < g->nv; v++) { g->dv_point[v] = g->vert_point[v]; g->dv_a[v] = -1; g->dv_b[v] = -1; }
    int *mid = (int *)malloc(sizeof(int) * (size_t)(nt * 3 + 1));
    int k = g->nv - 1;
    for (int i = 0; i < nt * 3; i++) {
        if (i == 0 || r[i].key != r[i - 1].key) {
            k++;
            int a = (int)(r[i].key >> 32), b = (int)(r[i].key & 0xffffffffu);
            g->dv_a[k] = a;
            g->dv_b[k] = b;
            g->dv_point[k] = np + edge_of(g, g->vert_point[a], g->vert_point[b]);
        }
        mid[r[i].at] = k;
    }
    free(r);
    g->ndt = nt * 4;
    g->didx = (int *)malloc(sizeof(int) * 3 * (size_t)g->ndt);
    g->dtri = (int *)malloc(sizeof(int) * 3 * (size_t)g->ndt);
    for (int t = 0; t < nt; t++) {
        int A = g->vtri[t * 3], B = g->vtri[t * 3 + 1], C = g->vtri[t * 3 + 2], ab = mid[t * 3], bc = mid[t * 3 + 1], ca = mid[t * 3 + 2];
        int f[12] = {A, ab, ca, ab, B, bc, ca, bc, C, ab, bc, ca};
        for (int q = 0; q < 12; q++) {
            g->didx[t * 12 + q] = f[q];
            g->dtri[t * 12 + q] = g->dv_point[f[q]];
        }
    }
    free(mid);
}

static void point_normals(CCGarment *g) {
    int np = g->np;
    memset(g->nrm, 0, sizeof(float[3]) * (size_t)np);
    for (int t = 0; t < g->nt; t++) {
        const int *f = g->tri + t * 3;
        float ab[3], ac[3], n[3];
        sub3(g->shown[f[1]], g->shown[f[0]], ab);
        sub3(g->shown[f[2]], g->shown[f[0]], ac);
        cross3(ab, ac, n);
        for (int q = 0; q < 3; q++) { float *o = g->nrm[f[q]]; o[0] += n[0]; o[1] += n[1]; o[2] += n[2]; }
    }
    for (int p = 0; p < np; p++) {
        float *o = g->nrm[p];
        float l = len3(o);
        if (l > 1e-12f) { float k = g->out / l; o[0] *= k; o[1] *= k; o[2] *= k; }
        else { o[0] = g->bn1[p][0]; o[1] = g->bn1[p][1]; o[2] = g->bn1[p][2]; }
    }
}

/* A contact, as the coat learned it: the point is put on the surface, whatever speed that would give it off the surface is taken back out, and what it slides by is cut by friction. */
static void contact(CCGarment *g, int p, const float *n, float push, const float *surf_move) {
    float *x = g->cur[p], *o = g->prv[p];
    float most = fmaxf(dot3(surf_move, n), 0.f) + CC_PUSH_SPEED * g->h_was;
    if (push > most) push = most;
    x[0] += n[0] * push; x[1] += n[1] * push; x[2] += n[2] * push;
    float r[3] = {x[0] - o[0] - surf_move[0], x[1] - o[1] - surf_move[1], x[2] - o[2] - surf_move[2]};
    float vn = dot3(r, n);
    if (vn > 0.f) { o[0] += n[0] * vn; o[1] += n[1] * vn; o[2] += n[2] * vn; r[0] -= n[0] * vn; r[1] -= n[1] * vn; r[2] -= n[2] * vn; }
    float t[3];
    float rn = dot3(r, n);
    t[0] = r[0] - n[0] * rn; t[1] = r[1] - n[1] * rn; t[2] = r[2] - n[2] * rn;
    float lt = len3(t);
    if (lt > 1e-9f) {
        float mu = g->par.friction;
        float f = lt < mu * push ? 1.f : fminf(0.6f * mu * push / lt, 1.f);
        x[0] -= t[0] * f; x[1] -= t[1] * f; x[2] -= t[2] * f;
    }
}

/* The skin's own contact has the last word however far: the limit on how fast a thing's pushed out is for planes that may be wrong, and a point's own skin isn't one. */
static void contact_skin(CCGarment *g, int p, const float *n, float push, const float *surf_move) {
    float *x = g->cur[p], *o = g->prv[p];
    x[0] += n[0] * push; x[1] += n[1] * push; x[2] += n[2] * push;
    float r[3] = {x[0] - o[0] - surf_move[0], x[1] - o[1] - surf_move[1], x[2] - o[2] - surf_move[2]};
    float vn = dot3(r, n);
    if (vn > 0.f) { o[0] += n[0] * vn; o[1] += n[1] * vn; o[2] += n[2] * vn; r[0] -= n[0] * vn; r[1] -= n[1] * vn; r[2] -= n[2] * vn; }
    float t[3];
    float rn = dot3(r, n);
    t[0] = r[0] - n[0] * rn; t[1] = r[1] - n[1] * rn; t[2] = r[2] - n[2] * rn;
    float lt = len3(t);
    if (lt > 1e-9f) {
        float mu = g->par.friction;
        float f = lt < mu * push ? 1.f : fminf(0.6f * mu * push / lt, 1.f);
        x[0] -= t[0] * f; x[1] -= t[1] * f; x[2] -= t[2] * f;
    }
}

void cc_pose(CCGarment *g, const float *pal) {
    int np = g->np;
    memcpy(g->tg0, g->tg1, sizeof(float[3]) * (size_t)np);
    memcpy(g->bs0, g->bs1, sizeof(float[3]) * (size_t)np);
    memcpy(g->raw0, g->raw, sizeof(float[3]) * (size_t)np);
    memcpy(g->bn0, g->bn1, sizeof(float[3]) * (size_t)np);
    for (int p = 0; p < np; p++) {
        skin_point(pal, g->rest[p], g->bon[p], g->wgt[p], g->tg1[p]);
        memcpy(g->raw[p], g->tg1[p], sizeof(float) * 3);
        skin_point(pal, g->bs_pos[p], g->bs_bon[p], g->bs_wgt[p], g->bs1[p]);
        skin_dir(pal, g->bs_nrm[p], g->bs_bon[p], g->bs_wgt[p], g->bn1[p]);
        /* What it's held to is out of the skin as well. A garment's weights aren't the skin's under it, on purpose at a waist or by the cut of a hem, and low-poly limbs bend sharply: with its target inside, the limit held a point inside however little it was allowed to move. */
        float d[3];
        sub3(g->tg1[p], g->bs1[p], d);
        float in = g->near_[p] - dot3(d, g->bn1[p]);
        if (in > 0.f) {
            if (in > CC_TARGET_OUT) in = CC_TARGET_OUT;
            for (int k = 0; k < 3; k++) g->tg1[p][k] += g->bn1[p][k] * in;
        }
    }
    /* How curved it was cut, turned with the limb: that's what it folds from, so it hangs in the shape it was sewn. Not how curved the skinned pose is: skinning crushes itself at every joint, and cloth holding that shape was crumpled paper at every elbow, armpit and knee. */
    memcpy(g->hc0, g->hc1, sizeof(float[3]) * (size_t)g->nhinge);
    for (int e = 0; e < g->nhinge; e++) {
        const Hinge *H = &g->hinge[e];
        int p = H->i[0];
        float x = 0.f, y = 0.f, z = 0.f;
        const float *c = g->hc_rest[e];
        for (int w = 0; w < CC_BONES; w++) {
            float a = g->wgt[p][w];
            if (a <= 0.f) continue;
            const float *m = pal + g->bon[p][w] * 12;
            x += a * (m[0] * c[0] + m[1] * c[1] + m[2] * c[2]);
            y += a * (m[4] * c[0] + m[5] * c[1] + m[6] * c[2]);
            z += a * (m[8] * c[0] + m[9] * c[1] + m[10] * c[2]);
        }
        g->hc1[e][0] = x; g->hc1[e][1] = y; g->hc1[e][2] = z;
    }
    /* Its space turned under it, and the pose with it. Everything that says where a point is or which way goes across, and as this frame starts the pose is where it is now: nothing moved. Without this the whole garment is a half turn from its body for a frame, as far as its limits let it be, with all that speed. */
    if (g->carried) {
        g->carried = 0;
        for (int p = 0; p < np; p++) {
            carry_point(g->carry, g->cur[p]);
            carry_point(g->carry, g->prv[p]);
            carry_dir(g->carry, g->off[p]);
            carry_dir(g->carry, g->nrm[p]);
            carry_dir(g->carry, g->t_out[p]);
            carry_dir(g->carry, g->spike_off[p]);
            for (int k = 0; k < 3; k++) g->tg0[p][k] = g->tg1[p][k] + g->t_out[p][k];
            if (g->under) {
                carry_dir(g->carry, g->u_n[p]);
                carry_dir(g->carry, g->u_dn[p]);
            }
            if (g->over) carry_dir(g->carry, g->o_tuck[p]);
        }
        memcpy(g->hc0, g->hc1, sizeof(float[3]) * (size_t)g->nhinge);
        memcpy(g->bs0, g->bs1, sizeof(float[3]) * (size_t)np);
        memcpy(g->raw0, g->raw, sizeof(float[3]) * (size_t)np);
        memcpy(g->bn0, g->bn1, sizeof(float[3]) * (size_t)np);
        g->ncap0 = 0;
    }
    if (g->fresh) {
        memcpy(g->hc0, g->hc1, sizeof(float[3]) * (size_t)g->nhinge);
        memcpy(g->cur, g->tg1, sizeof(float[3]) * (size_t)np);
        memcpy(g->prv, g->tg1, sizeof(float[3]) * (size_t)np);
        memcpy(g->tg0, g->tg1, sizeof(float[3]) * (size_t)np);
        memcpy(g->bs0, g->bs1, sizeof(float[3]) * (size_t)np);
        memcpy(g->raw0, g->raw, sizeof(float[3]) * (size_t)np);
        memcpy(g->bn0, g->bn1, sizeof(float[3]) * (size_t)np);
        memcpy(g->shown, g->tg1, sizeof(float[3]) * (size_t)np);
        for (int p = 0; p < np; p++) sub3(g->tg1[p], g->raw[p], g->off[p]);
        g->fresh = 0;
        g->settle = CC_SETTLE;
        g->ncap0 = 0;
        point_normals(g);
    }
    memcpy(g->off_was, g->off, sizeof(float[3]) * (size_t)np);
    memcpy(g->nrm_was, g->nrm, sizeof(float[3]) * (size_t)np);
}

/* Where a point of what's under this one is drawn, this frame: its pose now, and how far off that it was drawn last frame. */
static void under_pos(const CCGarment *u, int j, float *o) {
    o[0] = u->raw[j][0] + u->off_was[j][0];
    o[1] = u->raw[j][1] + u->off_was[j][1];
    o[2] = u->raw[j][2] + u->off_was[j][2];
}

static float under_dist2(const CCGarment *u, int j, const float *x) {
    float b[3], d[3];
    under_pos(u, j, b);
    sub3(x, b, d);
    return dot3(d, d);
}

static void rest_normals(const CCGarment *g, float (*nr)[3]) {
    for (int t = 0; t < g->nt; t++) {
        const int *f = g->tri + t * 3;
        float ab[3], ac[3], n[3];
        sub3(g->rest[f[1]], g->rest[f[0]], ab);
        sub3(g->rest[f[2]], g->rest[f[0]], ac);
        cross3(ab, ac, n);
        for (int q = 0; q < 3; q++) { nr[f[q]][0] += n[0]; nr[f[q]][1] += n[1]; nr[f[q]][2] += n[2]; }
    }
    for (int j = 0; j < g->np; j++) {
        float l = len3(nr[j]);
        if (l > 1e-12f) { float k = g->out / l; nr[j][0] *= k; nr[j][1] *= k; nr[j][2] *= k; }
    }
}

void cc_set_under(CCGarment *g, CCGarment *under, float gap) {
    if (g->under && g->under != under) {
        CCGarment *was = g->under;
        was->over = NULL;
        memset(was->o_tuck, 0, sizeof(float[3]) * (size_t)was->np);
    }
    g->under = under;
    g->u_count = 0;
    memset(g->u_on, 0, (size_t)g->np);
    if (!under) return;
    /* What it covers at rest, over its own points, and never past a hem. */
    {
        under->over = g;
        float (*top)[3] = cc_alloc(sizeof(float[3]) * (size_t)g->np);
        rest_normals(g, top);
        for (int j = 0; j < under->np; j++) {
            float bd = 1e18f;
            int bk = 0;
            for (int k = 0; k < g->np; k++) {
                float d[3];
                sub3(under->rest[j], g->rest[k], d);
                float dd = dot3(d, d);
                if (dd < bd) { bd = dd; bk = k; }
            }
            float d[3];
            sub3(under->rest[j], g->rest[bk], d);
            under->o_near[j] = (bd < CC_UNDER_REACH * CC_UNDER_REACH && g->bnd_a[bk] < 0 && dot3(d, top[bk]) < 0.f) ? bk : -1;
        }
        free(top);
    }
    /* Only on the bones it's on and their children: a spine bends over a waistband, and a sleeve hanging by a hip has nothing to do with the trousers. */
    int nb = g->nbones;
    char *on = (char *)cc_alloc((size_t)(nb + 1));
    for (int p = 0; p < under->np; p++) for (int w = 0; w < CC_BONES; w++) {
        int b = under->bon[p][w];
        if (under->wgt[p][w] >= 0.05f && b >= 0 && b < nb) on[b] = 1;
    }
    char *shared = (char *)cc_alloc((size_t)(nb + 1));
    for (int b = 0; b < nb; b++) shared[b] = on[b] || (g->parent[b] >= 0 && on[g->parent[b]]);
    free(on);
    /* The garment under at rest, and its normals there: which way out is at each of its points. */
    float (*nr)[3] = cc_alloc(sizeof(float[3]) * (size_t)under->np);
    rest_normals(under, nr);
    for (int p = 0; p < g->np; p++) {
        float best = 0.f;
        int dom = -1;
        for (int w = 0; w < CC_BONES; w++) if (g->wgt[p][w] > best) { best = g->wgt[p][w]; dom = g->bon[p][w]; }
        if (dom < 0 || dom >= nb || !shared[dom]) { g->u_near[p] = -1; continue; }
        /* Nearest at rest. It's where the walk starts each frame, and a point that starts inside its gap is allowed that much: at rest this moves nothing. */
        float bd = 1e18f;
        int bj = 0;
        for (int j = 0; j < under->np; j++) {
            float d[3];
            sub3(g->rest[p], under->rest[j], d);
            float dd = dot3(d, d);
            if (dd < bd) { bd = dd; bj = j; }
        }
        g->u_near[p] = bj;
        float want = gap;
        if (bd < CC_UNDER_REACH * CC_UNDER_REACH) {
            float d[3];
            sub3(g->rest[p], under->rest[bj], d);
            float d0 = dot3(d, nr[bj]);
            if (d0 < want) want = d0;
        }
        g->u_gap[p] = want;
    }
    free(nr);
    free(shared);
}

void cc_set_parts(CCGarment *g, const int *bone_part, float gap) {
    int np = g->np;
    int any = 0;
    for (int p = 0; p < np; p++) {
        float best = 0.f;
        int dom = -1;
        for (int w = 0; w < CC_BONES; w++) if (g->wgt[p][w] > best) { best = g->wgt[p][w]; dom = g->bon[p][w]; }
        g->sh[p] = dom >= 0 && dom < g->nbones ? bone_part[dom] : 0;
        if (g->sh[p] != g->sh[0]) any = 1;
    }
    for (int p = 0; p < np; p++) g->s_near[p] = -1;
    if (!any) return;
    float (*nr)[3] = cc_alloc(sizeof(float[3]) * (size_t)np);
    rest_normals(g, nr);
    for (int p = 0; p < np; p++) {
        float bd = 1e18f;
        int bj = -1;
        for (int j = 0; j < np; j++) {
            if (g->sh[j] == g->sh[p]) continue;
            float d[3];
            sub3(g->rest[p], g->rest[j], d);
            float dd = dot3(d, d);
            if (dd < bd) { bd = dd; bj = j; }
        }
        g->s_near[p] = bj;
        if (bj < 0) continue;
        /* A point that starts closer is allowed that much: where two legs join, they're touching. */
        float want = gap;
        if (bd < CC_UNDER_REACH * CC_UNDER_REACH) {
            float d[3];
            sub3(g->rest[p], g->rest[bj], d);
            float d0 = dot3(d, nr[bj]);
            if (d0 < want) want = d0;
        }
        g->s_gap[p] = want;
    }
    free(nr);
}

void cc_set_boxes(CCGarment *g, const CCBox *boxes, int n) {
    if (n > CC_MAX_BOXES) n = CC_MAX_BOXES;
    for (int i = 0; i < n; i++) g->box[i] = boxes[i];
    g->nbox = n;
}

/* A board is a flat thing with two sides, and it never comes between cloth and its skin: over the board, a point keeps to the side its own skin is on. How far it's out of place: its depth, or 0. The nearest way out of a box changes from one face to another across its middle, and a point carried across that jumped 6 cm in a frame; its own side of the box's middle sent a sleeve on the grip side out through the wood. */
static float box_out(const CCBox *b, const float *x, const float *skin, float margin, float *n) {
    float e[3] = {x[0] - b->m[3], x[1] - b->m[7], x[2] - b->m[11]};
    int thin = 0;
    for (int a = 1; a < 3; a++) if (b->half[a] < b->half[thin]) thin = a;
    for (int a = 0; a < 3; a++) {
        float axis[3] = {b->m[a], b->m[4 + a], b->m[8 + a]};
        float q = dot3(e, axis);
        if (a != thin && fabsf(q) >= b->half[a] + margin) return 0.f;
    }
    float axis[3] = {b->m[thin], b->m[4 + thin], b->m[8 + thin]};
    float k[3] = {skin[0] - b->m[3], skin[1] - b->m[7], skin[2] - b->m[11]};
    float q = dot3(e, axis), s = dot3(k, axis) < 0.f ? -1.f : 1.f;
    float in = b->half[thin] + margin - q * s;
    if (in <= 0.f || in > b->half[thin] + margin + CC_BOX_REACH) return 0.f;
    n[0] = axis[0] * s; n[1] = axis[1] * s; n[2] = axis[2] * s;
    return in;
}

void cc_layer(CCGarment *g) {
    float fade = expf(-g->dt_was / CC_PLANE_FADE), ease = 1.f - expf(-g->dt_was / CC_TARGET_FADE);
    /* What's near a box, and what it's held to is outside it: how far, for each point, before it's eased. */
    float (*want_all)[3] = g->t_want;
    memset(want_all, 0, sizeof(float[3]) * (size_t)g->np);
    for (int i = 0; i < g->nbox; i++) {
        const CCBox *b = &g->box[i];
        float reach = sqrtf(b->half[0] * b->half[0] + b->half[1] * b->half[1] + b->half[2] * b->half[2]) + CC_CAP_MARGIN;
        int n = 0;
        int *list = g->box_cand + i * g->np;
        for (int p = 0; p < g->np; p++) {
            float d[3] = {g->cur[p][0] - b->m[3], g->cur[p][1] - b->m[7], g->cur[p][2] - b->m[11]};
            if (dot3(d, d) >= reach * reach) continue;
            list[n++] = p;
            float out[3], in = box_out(b, g->tg1[p], g->bs1[p], CC_BOX_GAP, out);
            if (in > 0.f) for (int k = 0; k < 3; k++) want_all[p][k] += out[k] * in;
        }
        g->box_n[i] = n;
    }
    const CCGarment *u = g->under;
    g->u_count = 0;
    for (int p = 0; p < g->np; p++) {
        float *want = want_all[p];
        int j = u ? g->u_near[p] : -1;
        if (j < 0) continue;
        int j0 = j;
        /* Where this point will be about now: it goes with its skin. */
        float x[3];
        for (int k = 0; k < 3; k++) x[k] = g->cur[p][k] + g->tg1[p][k] - g->tg0[p][k];
        /* Down the garment under from where it was last frame: bone by bone they can't go far in a frame, so a few neighbours each do it. */
        float bd = under_dist2(u, j, x);
        for (int step = 0; step < 32; step++) {
            int bj = j;
            for (int k = u->adj_start[j]; k < u->adj_start[j + 1]; k++) {
                int o = u->adj[k];
                float dd = under_dist2(u, o, x);
                if (dd < bd) { bd = dd; bj = o; }
            }
            if (bj == j) break;
            j = bj;
        }
        g->u_near[p] = j;
        /* Facing away from this point's own way out, that's the far side of a fold or of another limb: nothing to stay outside of. */
        if (bd <= CC_UNDER_REACH * CC_UNDER_REACH && dot3(u->nrm_was[j], g->bn1[p]) >= CC_UNDER_FACING) {
            float b[3], d[3];
            under_pos(u, j, b);
            /* Near it last frame on another point: that plane fades into this one, so it doesn't jump. */
            if (g->u_on[p] && j != j0) {
                float bo[3], no[3], e[3];
                under_pos(u, j0, bo);
                for (int k = 0; k < 3; k++) { no[k] = g->u_n[p][k]; e[k] = x[k] - bo[k]; }
                float dold = dot3(e, no) - g->u_dh[p];
                sub3(x, b, e);
                g->u_dh[p] = dot3(e, no) - dold;
                for (int k = 0; k < 3; k++) g->u_dn[p][k] = no[k] - u->nrm_was[j][k];
            } else if (!g->u_on[p]) {
                g->u_dh[p] = 0.f;
                g->u_dn[p][0] = g->u_dn[p][1] = g->u_dn[p][2] = 0.f;
            }
            g->u_on[p] = 1;
            g->u_dh[p] *= fade;
            float n[3];
            for (int k = 0; k < 3; k++) { g->u_dn[p][k] *= fade; n[k] = u->nrm_was[j][k] + g->u_dn[p][k]; }
            float l = len3(n);
            if (l > 1e-6f) { n[0] /= l; n[1] /= l; n[2] /= l; } else { memcpy(n, u->nrm_was[j], sizeof(float) * 3); }
            memcpy(g->u_n[p], n, sizeof(float) * 3);
            g->u_list[g->u_count++] = p;
            /* What it's held to is outside the garment under as well, or the limit holds it inside. */
            sub3(g->tg1[p], b, d);
            float in = g->u_gap[p] - (dot3(d, n) - g->u_dh[p]);
            if (in > 0.f) {
                if (in > CC_TARGET_OUT) in = CC_TARGET_OUT;
                for (int k = 0; k < 3; k++) want[k] += n[k] * in;
            }
        } else {
            g->u_on[p] = 0;
        }
    }
    /* Eased, whatever's pushing it: a target that jumps carries a point at its limit along with it. And still outside the skin: that has the last word. */
    for (int p = 0; p < g->np; p++) {
        float any = 0.f;
        for (int k = 0; k < 3; k++) {
            g->t_out[p][k] += (want_all[p][k] - g->t_out[p][k]) * ease;
            g->tg1[p][k] += g->t_out[p][k];
            any += fabsf(g->t_out[p][k]);
        }
        if (any > 0.f) {
            float d[3];
            sub3(g->tg1[p], g->bs1[p], d);
            float in = g->near_[p] - dot3(d, g->bn1[p]);
            if (in > 0.f) for (int k = 0; k < 3; k++) g->tg1[p][k] += g->bn1[p][k] * in;
        }
    }
}

void cc_solve(CCGarment *g, float dt, const float gravity[3], const float accel[3], const float air[3], const CCCapsule *caps, int ncaps) {
    if (dt <= 0.f) return;
    if (dt > 0.034f) dt = 0.034f;
    int np = g->np;
    if (ncaps > CC_MAX_CAPS) ncaps = CC_MAX_CAPS;
    /* The capsules' ends through the frame go from where they were to where they are. A capsule that just came in has no last frame. */
    CCCapsule from[CC_MAX_CAPS];
    for (int c = 0; c < ncaps; c++) from[c] = (c < g->ncap0) ? g->cap0[c] : caps[c];
    /* Which points are anywhere near which capsule: a frame's worth of tests saved in every substep. */
    for (int c = 0; c < ncaps; c++) {
        int n = 0;
        int *list = g->cand + c * np;
        float ab[3];
        sub3(caps[c].b, caps[c].a, ab);
        float ab2 = dot3(ab, ab);
        float reach = caps[c].r + CC_CAP_MARGIN;
        for (int p = 0; p < np; p++) {
            if (g->maxd[p] <= 0.f || g->bs_bone[p] == caps[c].bone) continue;
            float ap[3];
            sub3(g->cur[p], caps[c].a, ap);
            float t = ab2 > 1e-12f ? clampf_(dot3(ap, ab) / ab2, 0.f, 1.f) : 0.f;
            float q[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
            if (dot3(q, q) < reach * reach) list[n++] = p;
        }
        g->ncand[c] = n;
    }
    point_normals(g);
    /* Each point to the nearest of another part of this garment: it can't go far in a frame. */
    g->s_n = 0;
    for (int p = 0; p < np; p++) {
        int j = g->s_near[p];
        if (j < 0 || g->maxd[p] <= 0.f) continue;
        float d[3];
        sub3(g->cur[p], g->cur[j], d);
        float bd = dot3(d, d);
        for (int step = 0; step < 32; step++) {
            int bj = j;
            for (int q = g->adj_start[j]; q < g->adj_start[j + 1]; q++) {
                int o = g->adj[q];
                if (g->sh[o] == g->sh[p]) continue;
                sub3(g->cur[p], g->cur[o], d);
                float dd = dot3(d, d);
                if (dd < bd) { bd = dd; bj = o; }
            }
            if (bj == j) break;
            j = bj;
        }
        g->s_near[p] = j;
        if (bd < CC_UNDER_REACH * CC_UNDER_REACH && dot3(g->bn1[p], g->bn1[j]) < CC_PART_FACING) g->s_list[g->s_n++] = p;
    }
    int N = (int)ceilf(dt / g->par.substep);
    if (N < 2) N = 2;
    if (N > 32) N = 32;
    float h = dt / (float)N;
    float damp = expf(-g->par.damp * h);
    float cq = g->par.drag;
    float ih2 = 1.f / (h * h);
    float dr = fminf(g->par.drive * h, 1.f);
    const float *maxd = g->maxd;
    const float *invm = g->invm;
    float (*cur)[3] = g->cur, (*prv)[3] = g->prv;
    /* Speed is kept as a distance a substep, and a frame's substep isn't always the last one's: a frame time that wobbles would otherwise shake every point by it. */
    if (g->h_was > 0.f && g->h_was != h) {
        float rescale = h / g->h_was;
        for (int p = 0; p < np; p++) for (int k = 0; k < 3; k++) prv[p][k] = cur[p][k] - (cur[p][k] - prv[p][k]) * rescale;
    }
    g->h_was = h;
    /* What the world does to each point, this frame. It's simulated in the skeleton's own space, so that space's own motion arrives as forces: minus its acceleration, and if it's turning, the Euler and centrifugal forces. Coriolis goes by a point's speed, so it's in the loop. */
    int turning = dot3(g->omega, g->omega) > 1e-4f || dot3(g->alpha, g->alpha) > 1e-4f;
    {
        float base[3] = {gravity[0] - accel[0], gravity[1] - accel[1], gravity[2] - accel[2]};
        for (int p = 0; p < np; p++) {
            float a[3] = {base[0], base[1], base[2]}, w[3] = {air[0], air[1], air[2]};
            if (turning) {
                float wr[3], wwr[3], ar[3];
                cross3(g->omega, cur[p], wr);
                cross3(g->omega, wr, wwr);
                cross3(g->alpha, cur[p], ar);
                for (int k = 0; k < 3; k++) { a[k] -= ar[k] + wwr[k]; w[k] -= wr[k]; }
            }
            for (int k = 0; k < 3; k++) { g->push[p][k] = a[k] * h * h; g->wind[p][k] = w[k] * h; }
        }
    }
    float cor[3] = {-2.f * h * g->omega[0], -2.f * h * g->omega[1], -2.f * h * g->omega[2]};
    for (int s = -g->settle; s < N; s++) {
        float a1 = (float)(s + 1) / (float)N, a0 = (float)s / (float)N;
        /* Having just been put on, it settles first: with no speed, and it's all the same instant. */
        if (s < 0) {
            a1 = 1.f; a0 = 1.f;
            memcpy(prv, cur, sizeof(float[3]) * (size_t)np);
        }
        /* air and gravity */
        for (int p = 0; p < np; p++) {
            if (maxd[p] <= 0.f) continue;
            float v[3] = {(cur[p][0] - prv[p][0]) * damp, (cur[p][1] - prv[p][1]) * damp, (cur[p][2] - prv[p][2]) * damp};
            const float *n = g->nrm[p], *w = g->wind[p];
            float vn = (v[0] - w[0]) * n[0] + (v[1] - w[1]) * n[1] + (v[2] - w[2]) * n[2];
            /* A point going outward against the air has it coming at its outside; going inward, the air's leaving the outside of it. */
            float keep = 1.f / (1.f + cq * (vn > 0.f ? g->air_out : g->air_in) * fabsf(vn));
            float cut = vn * (1.f - keep);
            prv[p][0] = cur[p][0]; prv[p][1] = cur[p][1]; prv[p][2] = cur[p][2];
            cur[p][0] += v[0] - n[0] * cut + g->push[p][0];
            cur[p][1] += v[1] - n[1] * cut + g->push[p][1];
            cur[p][2] += v[2] - n[2] * cut + g->push[p][2];
            if (turning) {
                float c[3];
                cross3(cor, v, c);
                cur[p][0] += c[0]; cur[p][1] += c[1]; cur[p][2] += c[2];
            }
        }
        /* points that don't move are the skin's, wherever it is at this instant */
        for (int p = 0; p < np; p++) {
            if (maxd[p] > 0.f) continue;
            for (int k = 0; k < 3; k++) {
                float a = g->tg0[p][k], b = g->tg1[p][k];
                prv[p][k] = a + (b - a) * a0;
                cur[p][k] = a + (b - a) * a1;
            }
        }
        /* the cloth doesn't stretch */
        for (int e = 0; e < g->ne; e++) {
            int i = g->e0[e], j = g->e1[e];
            float wi = invm[i], wj = invm[j], ws = wi + wj;
            if (ws <= 0.f) continue;
            float d[3] = {cur[j][0] - cur[i][0], cur[j][1] - cur[i][1], cur[j][2] - cur[i][2]};
            float L = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (L < 1e-9f) continue;
            float c = (L - g->elen[e]) / (L * (ws + g->ecomp[e] * ih2));
            if (L < g->elen[e]) c *= CC_EDGE_PRESS;
            cur[i][0] += wi * c * d[0]; cur[i][1] += wi * c * d[1]; cur[i][2] += wi * c * d[2];
            cur[j][0] -= wj * c * d[0]; cur[j][1] -= wj * c * d[1]; cur[j][2] -= wj * c * d[2];
        }
        /* it resists folding, against how it was sewn: XPBD with its own losses (Macklin et al. 2016) */
        for (int e = 0; e < g->nhinge; e++) {
            const Hinge *H = &g->hinge[e];
            int i0 = H->i[0], i1 = H->i[1], i2 = H->i[2], i3 = H->i[3];
            float k0 = H->k[0], k1 = H->k[1], k2 = H->k[2], k3 = H->k[3];
            float den = H->wk + H->comp * ih2;
            float w0 = invm[i0] * k0, w1 = invm[i1] * k1, w2 = invm[i2] * k2, w3 = invm[i3] * k3;
            for (int k = 0; k < 3; k++) {
                float C = k0 * cur[i0][k] + k1 * cur[i1][k] + k2 * cur[i2][k] + k3 * cur[i3][k] - (g->hc0[e][k] + (g->hc1[e][k] - g->hc0[e][k]) * a1);
                float l = -C / den;
                cur[i0][k] += w0 * l; cur[i1][k] += w1 * l; cur[i2][k] += w2 * l; cur[i3][k] += w3 * l;
            }
        }
        /* a hem is never further from what holds it than its cloth reaches */
        for (int p = 0; p < np; p++) {
            int a = g->anchor[p];
            if (a < 0 || maxd[p] <= 0.f) continue;
            float d[3] = {cur[p][0] - cur[a][0], cur[p][1] - cur[a][1], cur[p][2] - cur[a][2]};
            float L2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2], Lm = g->tlen[p] * 1.02f;
            if (L2 > Lm * Lm) {
                float f = Lm / sqrtf(L2);
                cur[p][0] = cur[a][0] + d[0] * f; cur[p][1] = cur[a][1] + d[1] * f; cur[p][2] = cur[a][2] + d[2] * f;
            }
        }
        /* It creeps back to its shape. Both ends of its speed move together: moving only where it is would be speed, every substep, and that was a spring ringing at 6 Hz on a sheet that swings at 1 (design/cloth.md, "the follow must not become momentum": made again here, and only a bench with a known period caught it). */
        for (int p = 0; p < np; p++) {
            if (maxd[p] <= 0.f) continue;
            for (int k = 0; k < 3; k++) {
                float t = g->tg0[p][k] + (g->tg1[p][k] - g->tg0[p][k]) * a1;
                float by = (t - cur[p][k]) * dr;
                cur[p][k] += by;
                prv[p][k] += by;
            }
        }
        /* limbs */
        for (int c = 0; c < ncaps; c++) {
            const CCCapsule *C1 = &caps[c], *C0 = &from[c];
            float A[3], B[3], A0[3], B0[3];
            float a00 = (float)s / (float)N;
            for (int k = 0; k < 3; k++) {
                A[k] = C0->a[k] + (C1->a[k] - C0->a[k]) * a1;
                B[k] = C0->b[k] + (C1->b[k] - C0->b[k]) * a1;
                A0[k] = C0->a[k] + (C1->a[k] - C0->a[k]) * a00;
                B0[k] = C0->b[k] + (C1->b[k] - C0->b[k]) * a00;
            }
            float ab[3];
            sub3(B, A, ab);
            float ab2 = dot3(ab, ab);
            const float *allow = g->allow + c * np;
            const int *list = g->cand + c * np;
            for (int q = 0; q < g->ncand[c]; q++) {
                int p = list[q];
                float *x = cur[p];
                float r = fminf(C1->r, allow[p]), r2 = r * r;
                float ap[3] = {x[0] - A[0], x[1] - A[1], x[2] - A[2]};
                float t = ab2 > 1e-12f ? clampf_(dot3(ap, ab) / ab2, 0.f, 1.f) : 0.f;
                float off[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
                float d2 = dot3(off, off);
                if (d2 >= r2) continue;
                float dist = sqrtf(d2);
                float n[3], push;
                if (C1->press) {
                    /* An arm is outside a top: it presses the cloth in toward the body. Straight along the skin's normal, to the far side of the capsule from the point: not through the body, the backstop has the last word. */
                    const float *bn = g->bn1[p];
                    n[0] = -bn[0]; n[1] = -bn[1]; n[2] = -bn[2];
                    float along = off[0] * n[0] + off[1] * n[1] + off[2] * n[2];
                    float side2 = d2 - along * along;
                    if (side2 < 0.f) side2 = 0.f;
                    push = sqrtf(fmaxf(r2 - side2, 0.f)) - along;
                } else {
                    if (dist < 1e-6f) { n[0] = g->bn1[p][0]; n[1] = g->bn1[p][1]; n[2] = g->bn1[p][2]; }
                    else { n[0] = off[0] / dist; n[1] = off[1] / dist; n[2] = off[2] / dist; }
                    push = r - dist;
                }
                if (push <= 0.f) continue;
                float mv[3];
                for (int k = 0; k < 3; k++) mv[k] = (A[k] + ab[k] * t) - (A0[k] + (B0[k] - A0[k]) * t);
                contact(g, p, n, push, mv);
            }
        }
        /* the garment it's worn over */
        if (g->under) {
            const CCGarment *u = g->under;
            for (int q = 0; q < g->u_count; q++) {
                int p = g->u_list[q], j = g->u_near[p];
                if (maxd[p] <= 0.f) continue;
                const float *n = g->u_n[p];
                float b[3], mv[3];
                for (int k = 0; k < 3; k++) {
                    mv[k] = (u->raw[j][k] - u->raw0[j][k]) / (float)N;
                    b[k] = u->raw0[j][k] + (u->raw[j][k] - u->raw0[j][k]) * a1 + u->off_was[j][k];
                }
                float d = (cur[p][0] - b[0]) * n[0] + (cur[p][1] - b[1]) * n[1] + (cur[p][2] - b[2]) * n[2] - g->u_dh[p];
                if (d < g->u_gap[p] && d > g->u_gap[p] - CC_UNDER_DEEP) contact(g, p, n, g->u_gap[p] - d, mv);
            }
        }
        /* another part of the same garment: softly, a little of it each substep, along the skin's own way out. Both sides push and neither has anywhere to go when two baggy legs stand together: a hard contact on the cloth's own normals had them buzzing at 10 cm/s. */
        {
            const float none[3] = {0.f, 0.f, 0.f};
            for (int q = 0; q < g->s_n; q++) {
                int p = g->s_list[q], j = g->s_near[p];
                const float *n = g->bn1[j];
                float d = (cur[p][0] - cur[j][0]) * n[0] + (cur[p][1] - cur[j][1]) * n[1] + (cur[p][2] - cur[j][2]) * n[2];
                if (d >= g->s_gap[p]) continue;
                float push = fminf((g->s_gap[p] - d) * CC_PART_SOFT, CC_PUSH_SPEED * h);
                for (int k = 0; k < 3; k++) { cur[p][k] += n[k] * push; prv[p][k] += n[k] * push; }
            }
            /* what he's carrying or standing on */
            for (int i = 0; i < g->nbox; i++) {
                const int *list = g->box_cand + i * np;
                for (int q = 0; q < g->box_n[i]; q++) {
                    int p = list[q];
                    if (maxd[p] <= 0.f) continue;
                    float n[3], in = box_out(&g->box[i], cur[p], g->bs1[p], CC_BOX_GAP, n);
                    if (in > 0.f) contact(g, p, n, in, none);
                }
            }
        }
        /* the skin under it */
        for (int p = 0; p < np; p++) {
            if (maxd[p] <= 0.f) continue;
            float *x = cur[p];
            /* It turns through the frame with the limb. Swapped at the frame's start, a hem far along a turning limb met a new plane all at once, and was knocked by it 99 times a second. */
            float n[3], b[3], mv[3];
            for (int k = 0; k < 3; k++) {
                n[k] = g->bn0[p][k] + (g->bn1[p][k] - g->bn0[p][k]) * a1;
                mv[k] = (g->bs1[p][k] - g->bs0[p][k]) / (float)N;
                b[k] = g->bs0[p][k] + (g->bs1[p][k] - g->bs0[p][k]) * a1;
            }
            float d = (x[0] - b[0]) * n[0] + (x[1] - b[1]) * n[1] + (x[2] - b[2]) * n[2];
            float want = g->near_[p];
            if (d < want) {
                float l = len3(n);
                if (l > 1e-6f) { n[0] /= l; n[1] /= l; n[2] /= l; d = want - (want - d) / l; }
                contact_skin(g, p, n, want - d, mv);
            }
        }
        /* and it's never further from the skin's pose than the tailor said */
        for (int p = 0; p < np; p++) {
            float m = maxd[p];
            if (m <= 0.f) continue;
            float *x = cur[p];
            float t[3];
            for (int k = 0; k < 3; k++) t[k] = g->tg0[p][k] + (g->tg1[p][k] - g->tg0[p][k]) * a1;
            float d[3] = {x[0] - t[0], x[1] - t[1], x[2] - t[2]};
            float L2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
            if (L2 > m * m) {
                float L = sqrtf(L2), f = m / L;
                x[0] = t[0] + d[0] * f; x[1] = t[1] + d[1] * f; x[2] = t[2] + d[2] * f;
                /* It stops dead against the limit, as the limit moves. Neither a throw back toward its target nor speed kept against the limit: a point held there by a steady wind would store that speed for as long as it blew. */
                float out[3] = {d[0] / L, d[1] / L, d[2] / L}, r[3];
                for (int k = 0; k < 3; k++) r[k] = x[k] - prv[p][k] - (g->tg1[p][k] - g->tg0[p][k]) / (float)N;
                float vn = dot3(r, out);
                for (int k = 0; k < 3; k++) prv[p][k] += out[k] * vn;
            }
        }
        /* And bending's losses. Only ever on speed, once every substep has settled: at rest there's nothing for them to act on, so they can't change what it settles to. */
        for (int e = 0; e < g->nhinge; e++) {
            const Hinge *H = &g->hinge[e];
            int i0 = H->i[0], i1 = H->i[1], i2 = H->i[2], i3 = H->i[3];
            float k0 = H->k[0], k1 = H->k[1], k2 = H->k[2], k3 = H->k[3];
            float f = H->loss / H->wk;
            float w0 = invm[i0] * k0 * f, w1 = invm[i1] * k1 * f, w2 = invm[i2] * k2 * f, w3 = invm[i3] * k3 * f;
            for (int k = 0; k < 3; k++) {
                float rate = k0 * (cur[i0][k] - prv[i0][k]) + k1 * (cur[i1][k] - prv[i1][k]) + k2 * (cur[i2][k] - prv[i2][k]) + k3 * (cur[i3][k] - prv[i3][k]) - (g->hc1[e][k] - g->hc0[e][k]) / (float)N;
                prv[i0][k] += w0 * rate; prv[i1][k] += w1 * rate; prv[i2][k] += w2 * rate; prv[i3][k] += w3 * rate;
            }
        }
    }
    for (int c = 0; c < ncaps; c++) g->cap0[c] = caps[c];
    g->ncap0 = ncaps;
    if (g->settle > 0) {
        g->settle = 0;
        for (int p = 0; p < np; p++) sub3(cur[p], g->raw[p], g->off[p]);
    }
    g->dt_was = dt;
    steady(g, dt);
    tuck_under(g);
    smooth_spikes(g);
    point_normals(g);
    refine(g);
}

void cc_step(CCGarment *g, float dt, const float *pal, const float gravity[3], const float accel[3], const float air[3], const CCCapsule *caps, int ncaps) {
    if (dt <= 0.f) return;
    cc_pose(g, pal);
    cc_layer(g);
    cc_solve(g, dt, gravity, accel, air, caps, ncaps);
}

float cc_measure_radius(const CCBody *body, int bone, const float a[3], const float b[3]) {
    /* The 70th percentile of the skin's distance from the segment, the skin dominated by this bone. */
    if (!body || body->nv <= 0) return 0.05f;
    float *d = (float *)malloc(sizeof(float) * (size_t)body->nv);
    int n = 0;
    float ab[3];
    sub3(b, a, ab);
    float ab2 = dot3(ab, ab);
    for (int v = 0; v < body->nv; v++) {
        int dom = -1; float best = 0.f;
        for (int w = 0; w < CC_BONES; w++) {
            float x = body->wgt[v * CC_BONES + w];
            if (x > best) { best = x; dom = body->bon[v * CC_BONES + w]; }
        }
        if (dom != bone) continue;
        float ap[3];
        sub3(body->pos + v * 3, a, ap);
        float t = ab2 > 1e-12f ? clampf_(dot3(ap, ab) / ab2, 0.f, 1.f) : 0.f;
        float q[3] = {ap[0] - ab[0] * t, ap[1] - ab[1] * t, ap[2] - ab[2] * t};
        d[n++] = len3(q);
    }
    float r = 0.05f;
    if (n > 0) {
        for (int i = 1; i < n; i++) { float x = d[i]; int j = i - 1; while (j >= 0 && d[j] > x) { d[j + 1] = d[j]; j--; } d[j + 1] = x; }
        r = d[(int)(0.7f * (float)(n - 1))];
    }
    free(d);
    return r;
}

static void oct_encode(const float *n, unsigned short *o) {
    float x = n[0], y = n[1], z = n[2];
    float s = fabsf(x) + fabsf(y) + fabsf(z);
    if (s < 1e-9f) { x = 0.f; y = 1.f; z = 0.f; s = 1.f; }
    x /= s; y /= s; z /= s;
    float u, v;
    if (z >= 0.f) { u = x; v = y; }
    else {
        u = (1.f - fabsf(y)) * (x >= 0.f ? 1.f : -1.f);
        v = (1.f - fabsf(x)) * (y >= 0.f ? 1.f : -1.f);
    }
    o[0] = (unsigned short)clampf_((u * 0.5f + 0.5f) * 65535.f, 0.f, 65535.f);
    o[1] = (unsigned short)clampf_((v * 0.5f + 0.5f) * 65535.f, 0.f, 65535.f);
}

void cc_write(const CCGarment *g, unsigned char *pos_out, int pos_stride, unsigned char *nrm_out, int nrm_stride) {
    for (int v = 0; v < g->dv; v++) {
        int p = g->dv_point[v];
        memcpy(pos_out + v * pos_stride, g->dpos[p], sizeof(float) * 3);
        if (nrm_out) {
            unsigned short o[2];
            oct_encode(g->dnrm[p], o);
            memcpy(nrm_out + v * nrm_stride, o, sizeof(unsigned short) * 2);
        }
    }
}

int cc_drawn_vertex_count(const CCGarment *g) { return g->dv; }
int cc_drawn_index_count(const CCGarment *g) { return g->ndt * 3; }
const int *cc_drawn_indices(const CCGarment *g) { return g->didx; }
void cc_drawn_read(const CCGarment *g, float *pos, float *nrm, int *parents) {
    for (int v = 0; v < g->dv; v++) {
        int p = g->dv_point[v];
        if (pos) memcpy(pos + v * 3, g->dpos[p], sizeof(float) * 3);
        if (nrm) memcpy(nrm + v * 3, g->dnrm[p], sizeof(float) * 3);
        if (parents) { parents[v * 2] = g->dv_a[v]; parents[v * 2 + 1] = g->dv_b[v]; }
    }
}

int cc_vertex_count(const CCGarment *g) { return g->nv; }
int cc_point_count(const CCGarment *g) { return g->np; }
int cc_edge_count(const CCGarment *g) { return g->ne; }
const float *cc_points(const CCGarment *g) { return (const float *)g->cur; }
const float *cc_shown(const CCGarment *g) { return (const float *)g->shown; }
const float *cc_targets(const CCGarment *g) { return (const float *)g->tg1; }
const float *cc_skinned(const CCGarment *g) { return (const float *)g->raw; }
const float *cc_backstops(const CCGarment *g) { return (const float *)g->bs1; }
const float *cc_backnormals(const CCGarment *g) { return (const float *)g->bn1; }
const float *cc_normals(const CCGarment *g) { return (const float *)g->nrm; }
const float *cc_limits(const CCGarment *g) { return g->maxd; }
const float *cc_nears(const CCGarment *g) { return g->near_; }
