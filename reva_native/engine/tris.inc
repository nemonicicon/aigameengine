/* ---- THE SET'S OWN TRIANGLES (11-12, the Godot proof of concept's step 2) -------------------------------------------------------------------------
 * The walked world (engine/spans.inc) keeps a set's surfaces as columns of spans, which is right for floors, ramps, stairs and the walls a body walks against, and it
 * can't hold what a skater rides: a column has a top and a bottom and no sides, so a quarter pipe's vertical face, a wall to ride and a lip aren't in it. A set line
 * `triangles` keeps its props' real triangles as well, in a tree of boxes (a BVH), for a game to ask:
 *   E->triRay(from, to, &hit)              the first triangle the segment meets: how far along it, where, the triangle's normal turned to face the segment, its kind
 *   E->triSweep(from, to, radius, &hit)    the first triangle a ball of that radius touches as it moves from one point to the other: when, the point it touches, the
 *                                          way out of the surface there (from that point to the ball's middle)
 *   E->triNear(at, radius, hits, max)      every triangle within the radius of a point, nearest first: its nearest point, the way out, how far, for resting contact
 * The props, which of their meshes are solid and their surface kinds are the walked world's own (spBuild, the set's `surface` lines). A ball that starts touching a
 * triangle and moves away from it or along it isn't stopped by it, so a ball can roll on a floor; one moving into what it already touches is stopped at once (t 0).
 * The rules may use all of it: the tree is built in a fixed order (ties by index) and asked with + - * / and sqrt only, so every machine gets the same answers.
 * Not yet: a streamed world's cells, and the objects a game spawns; the set's props only.
 * ⭐ (11-12, step 14) A GAME'S OWN TRIANGLES: Create a Map's pieces are placed while the game runs and ridden in its test. E->triAdd hands over triangles in
 * the world (nine floats each, the engine's units) with a surface kind (E->surfaceKind of a name the set's `surface` lines give); they stay until
 * E->triAddClear or the set goes, and E->triRebuild makes the tree again from the set's props and them (the set must have said `triangles`). In the
 * rules as the set's are: added in the tick, in the same order, every machine builds the same tree */
#define TR_LEAF 4
typedef struct { float lo[3], hi[3]; int a, n; } TrNode;   /* n > 0: a leaf, triangles a .. a+n-1; n == 0: its children are nodes a and a+1 */
static struct { float *v; unsigned char *kind; int *id; int n; TrNode *nd; int nn; int want; double buildMs; } g_tr;   /* id: a game's number for each of its own (E->triAdd), -1 the set's; none when there are none */

static void trClear(void) { free(g_tr.v); free(g_tr.kind); free(g_tr.id); free(g_tr.nd); memset(&g_tr, 0, sizeof g_tr); }
static float *g_trAddV; static unsigned char *g_trAddK; static int *g_trAddI; static int g_trAddN, g_trAddCap;   /* a game's own (E->triAdd): where, what kind, whose */
static void trAddClear(void) { g_trAddN = 0; }
static void trDrop(void) { const int w = g_tr.want; trClear(); g_tr.want = w; }   /* what's built goes, and that the set wants a tree stays (a set with nothing solid of its own, for a game's) */

static inline void trSub(float *o, const float *a, const float *b) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static inline float trDot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void trCross(float *o, const float *a, const float *b) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }

/* the build: every solid triangle of the set's props in the world, then the tree over them by halves along each box's longest side */
static float *g_trCen;
static void trSelect(int *ix, int lo, int hi, int k, int axis) {   /* ix[lo..hi) partly sorted so ix[k] is where it'd be sorted by centre (ties by index), smaller before */
    while (hi - lo > 1) {
        const int p = ix[lo + (hi - lo) / 2]; const float pv = g_trCen[p * 3 + axis];
        int i = lo, j = hi - 1;
        while (i <= j) {
            while (g_trCen[ix[i] * 3 + axis] < pv || (g_trCen[ix[i] * 3 + axis] == pv && ix[i] < p)) i++;
            while (g_trCen[ix[j] * 3 + axis] > pv || (g_trCen[ix[j] * 3 + axis] == pv && ix[j] > p)) j--;
            if (i <= j) { const int t = ix[i]; ix[i] = ix[j]; ix[j] = t; i++; j--; }
        }
        if (k <= j) hi = j + 1; else if (k >= i) lo = i; else return;
    }
}
static void trFill(int ni, int *ix, int a, int n) {
    TrNode *N = &g_tr.nd[ni];
    float clo[3] = {1e30f, 1e30f, 1e30f}, chi[3] = {-1e30f, -1e30f, -1e30f};
    N->lo[0] = N->lo[1] = N->lo[2] = 1e30f; N->hi[0] = N->hi[1] = N->hi[2] = -1e30f;
    for (int i = a; i < a + n; i++) {
        const float *v = g_tr.v + (size_t)ix[i] * 9;
        for (int k = 0; k < 9; k++) { const int c = k % 3; if (v[k] < N->lo[c]) N->lo[c] = v[k]; if (v[k] > N->hi[c]) N->hi[c] = v[k]; }
        const float *cc = g_trCen + (size_t)ix[i] * 3;
        for (int c = 0; c < 3; c++) { if (cc[c] < clo[c]) clo[c] = cc[c]; if (cc[c] > chi[c]) chi[c] = cc[c]; }
    }
    if (n <= TR_LEAF) { N->a = a; N->n = n; return; }
    int axis = 0; float ext = chi[0] - clo[0];
    for (int c = 1; c < 3; c++) if (chi[c] - clo[c] > ext) { ext = chi[c] - clo[c]; axis = c; }
    const int h = n / 2;
    trSelect(ix, a, a + n, a + h, axis);
    const int c = g_tr.nn; g_tr.nn += 2;
    N->a = c; N->n = 0;
    trFill(c, ix, a, h);
    trFill(c + 1, ix, a + h, n - h);
}
static unsigned trMesh(int pass, const float *Mw, const float *pos, unsigned verts, const unsigned *idx, unsigned idxCount, int kind, int *at) {
    const unsigned nt = idxCount ? idxCount / 3 : verts / 3;
    if (pass == 0) return nt;
    for (unsigned t = 0; t < nt; t++) {
        const unsigned i3[3] = {idx ? idx[t * 3] : t * 3, idx ? idx[t * 3 + 1] : t * 3 + 1, idx ? idx[t * 3 + 2] : t * 3 + 2};
        if (i3[0] >= verts || i3[1] >= verts || i3[2] >= verts) continue;
        float *o = g_tr.v + (size_t)(*at) * 9;
        for (int k = 0; k < 3; k++) { const float *p = pos + (size_t)i3[k] * 3;
            o[k * 3 + 0] = Mw[0] * p[0] + Mw[4] * p[1] + Mw[8] * p[2] + Mw[12];
            o[k * 3 + 1] = Mw[1] * p[0] + Mw[5] * p[1] + Mw[9] * p[2] + Mw[13];
            o[k * 3 + 2] = Mw[2] * p[0] + Mw[6] * p[1] + Mw[10] * p[2] + Mw[14]; }
        float e1[3], e2[3], cr[3]; trSub(e1, o + 3, o); trSub(e2, o + 6, o); trCross(cr, e1, e2);
        if (trDot(cr, cr) < 1e-14f) continue;   /* no area: nothing to touch */
        g_tr.kind[*at] = (unsigned char)kind;
        (*at)++;
    }
    return nt;
}
/* at the end of a set's load, after the walked world, when the set said `triangles` */
static void trBuild(const char *name) {
    if (!g_tr.want) return;
    const double t0 = wall_ms();
    unsigned cap = 0; int at = 0, nProp = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            g_tr.v = (float *)malloc((size_t)cap * 9 * sizeof(float)); g_tr.kind = (unsigned char *)malloc(cap ? cap : 1);
            if (g_trAddN) g_tr.id = (int *)malloc((size_t)(cap ? cap : 1) * sizeof(int));
            if (!g_tr.v || !g_tr.kind || (g_trAddN && !g_tr.id)) { printf("[triangles] %s: no memory for %u triangles\n", name, cap); fflush(stdout); trDrop(); return; }
        }
        for (int i = 0; i < g_setNI; i++) { SetInst *I = &g_setI[i]; Pack *Q = &g_setPk[I->pk]; if (Q->open) continue;
            if (pass == 0) nProp++;
            for (unsigned mi = 0; mi < Q->nMeshes; mi++) { PMesh *Mh = &Q->meshes[mi]; if (!(Mh->flags & 4) || Mh->boneCount > MAXB || Mh->verts < 3 || MOFF(Q, mi)) continue;
                M4 bind, Mw; memcpy(bind, Q->pk + Mh->offBind, 64); m4_mul(Mw, I->m, bind);
                const unsigned nt = trMesh(pass, Mw, (const float *)(Q->pk + Mh->offPos), Mh->verts, Mh->idxCount ? (const unsigned *)(Q->pk + Mh->offIdx) : NULL, Mh->idxCount,
                                           pass ? spKindOf(packStr(Q, Mh->name), 128u) : 0, &at);
                if (pass == 0) cap += nt; } }
        if (pass == 0) cap += (unsigned)g_trAddN;   /* and a game's own, after the set's */
        else { if (g_tr.id) for (int i = 0; i < at; i++) g_tr.id[i] = -1;   /* the set's own */
            for (int t = 0; t < g_trAddN; t++) { const float *s = g_trAddV + (size_t)t * 9; float e1[3], e2[3], cr[3]; trSub(e1, s + 3, s); trSub(e2, s + 6, s); trCross(cr, e1, e2);
                if (trDot(cr, cr) < 1e-14f) continue;
                memcpy(g_tr.v + (size_t)at * 9, s, 36); g_tr.kind[at] = g_trAddK[t]; g_tr.id[at] = g_trAddI[t]; at++; } }
    }
    g_tr.n = at;
    if (at <= 0) { printf("[triangles] %s: none solid\n", name); fflush(stdout); trDrop(); return; }
    int *ix = (int *)malloc((size_t)at * sizeof(int)); g_trCen = (float *)malloc((size_t)at * 3 * sizeof(float));
    g_tr.nd = (TrNode *)malloc((size_t)(2 * at + 1) * sizeof(TrNode));
    if (!ix || !g_trCen || !g_tr.nd) { free(ix); free(g_trCen); g_trCen = NULL; printf("[triangles] %s: no memory for the tree\n", name); fflush(stdout); trDrop(); return; }
    for (int i = 0; i < at; i++) { const float *v = g_tr.v + (size_t)i * 9; ix[i] = i;
        for (int c = 0; c < 3; c++) g_trCen[i * 3 + c] = (v[c] + v[3 + c] + v[6 + c]) * (1.f / 3.f); }
    g_tr.nn = 1;
    trFill(0, ix, 0, at);
    /* the triangles in the tree's order, so a leaf's are together */
    float *v2 = (float *)malloc((size_t)at * 9 * sizeof(float)); unsigned char *k2 = (unsigned char *)malloc((size_t)at); int *i2 = g_tr.id ? (int *)malloc((size_t)at * sizeof(int)) : NULL;
    if (v2 && k2 && (!g_tr.id || i2)) { for (int i = 0; i < at; i++) { memcpy(v2 + (size_t)i * 9, g_tr.v + (size_t)ix[i] * 9, 36); k2[i] = g_tr.kind[ix[i]]; if (i2) i2[i] = g_tr.id[ix[i]]; }
        free(g_tr.v); free(g_tr.kind); free(g_tr.id); g_tr.v = v2; g_tr.kind = k2; g_tr.id = i2; }
    else { free(v2); free(k2); free(i2); printf("[triangles] %s: no memory to order them\n", name); fflush(stdout); free(ix); free(g_trCen); g_trCen = NULL; trDrop(); return; }
    free(ix); free(g_trCen); g_trCen = NULL;
    { TrNode *nd2 = (TrNode *)realloc(g_tr.nd, (size_t)g_tr.nn * sizeof(TrNode)); if (nd2) g_tr.nd = nd2; }   /* room was made for the most a tree could need */
    g_tr.buildMs = wall_ms() - t0;
    printf("[triangles] %s: %d props, %d triangles, %d boxes, %.1f MB, %.0f ms\n", name, nProp, at, g_tr.nn,
           ((double)at * 37 + (double)g_tr.nn * sizeof(TrNode)) / 1048576.0, g_tr.buildMs); fflush(stdout);
}

/* ---- asking */
/* a segment o + t d, t in [0, tmax], against a box grown by g: the t it enters at, or -1 */
static float trBox(const TrNode *N, const float *o, const float *inv, float tmax, float g) {
    float t0 = 0.f, t1 = tmax;
    for (int c = 0; c < 3; c++) {
        float a = (N->lo[c] - g - o[c]) * inv[c], b = (N->hi[c] + g - o[c]) * inv[c];
        if (a > b) { const float s = a; a = b; b = s; }
        if (a > t0) t0 = a;
        if (b < t1) t1 = b;
        if (t0 > t1) return -1.f;
    }
    return t0;
}
static void trInv(const float *d, float *inv) { for (int c = 0; c < 3; c++) inv[c] = (d[c] > 1e-12f || d[c] < -1e-12f) ? 1.f / d[c] : (d[c] >= 0.f ? 1e30f : -1e30f); }
/* the point of triangle v nearest to p (Ericson, Real-Time Collision Detection 5.1.5) */
static void trClosest(const float *p, const float *v, float *q) {
    const float *a = v, *b = v + 3, *c = v + 6; float ab[3], ac[3], ap[3], bp[3], cp[3];
    trSub(ab, b, a); trSub(ac, c, a); trSub(ap, p, a);
    const float d1 = trDot(ab, ap), d2 = trDot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) { memcpy(q, a, 12); return; }
    trSub(bp, p, b); const float d3 = trDot(ab, bp), d4 = trDot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) { memcpy(q, b, 12); return; }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) { const float t = d1 / (d1 - d3); for (int k = 0; k < 3; k++) q[k] = a[k] + t * ab[k]; return; }
    trSub(cp, p, c); const float d5 = trDot(ab, cp), d6 = trDot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) { memcpy(q, c, 12); return; }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) { const float t = d2 / (d2 - d6); for (int k = 0; k < 3; k++) q[k] = a[k] + t * ac[k]; return; }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) { const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6)); for (int k = 0; k < 3; k++) q[k] = b[k] + t * (c[k] - b[k]); return; }
    const float den = 1.f / (va + vb + vc), sv = vb * den, sw = vc * den;
    for (int k = 0; k < 3; k++) q[k] = a[k] + ab[k] * sv + ac[k] * sw;
}
static void trFaceN(const float *v, float *n) { float e1[3], e2[3]; trSub(e1, v + 3, v); trSub(e2, v + 6, v); trCross(n, e1, e2);
    const float l = sqrtf(trDot(n, n)); if (l > 0.f) { n[0] /= l; n[1] /= l; n[2] /= l; } }

/* (11-12) kinds: which surface kinds count, a bit each (1u << kind), 0 all of them: Godot's collision mask (the skate game's probe asks for its ride
 * surfaces, or its obstacles, and looks past the rest) */
#define TR_KIND_OK(kinds, i) (!(kinds) || ((kinds) >> g_tr.kind[i] & 1u))
static int trRayK(const float *from, const float *to, unsigned kinds, EngineTriHit *hit) {
    if (!g_tr.n) return 0;
    float d[3], inv[3]; trSub(d, to, from); trInv(d, inv);
    float best = 1.f; int who = -1; int st[96], sp = 0; st[sp++] = 0;
    while (sp) {
        const TrNode *N = &g_tr.nd[st[--sp]];
        const float te = trBox(N, from, inv, best, 0.f); if (te < 0.f) continue;
        if (N->n) { for (int i = N->a; i < N->a + N->n; i++) { const float *v = g_tr.v + (size_t)i * 9; if (!TR_KIND_OK(kinds, i)) continue;
                float e1[3], e2[3], p[3], s[3], q[3]; trSub(e1, v + 3, v); trSub(e2, v + 6, v); trCross(p, d, e2);
                const float det = trDot(e1, p); if (det > -1e-12f && det < 1e-12f) continue;
                const float iv = 1.f / det; trSub(s, from, v); const float u = trDot(s, p) * iv; if (u < 0.f || u > 1.f) continue;
                trCross(q, s, e1); const float w = trDot(d, q) * iv; if (w < 0.f || u + w > 1.f) continue;
                const float t = trDot(e2, q) * iv; if (t < 0.f || t > best) continue;
                if (t < best || (t == best && i < who)) { best = t; who = i; } } }
        else if (sp < 94) { st[sp++] = N->a + 1; st[sp++] = N->a; }
    }
    if (who < 0) return 0;
    if (hit) { const float *v = g_tr.v + (size_t)who * 9; float n[3]; trFaceN(v, n); if (trDot(n, d) > 0.f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        hit->t = best; for (int k = 0; k < 3; k++) { hit->pos[k] = from[k] + d[k] * best; hit->nrm[k] = n[k]; }
        hit->dist = best * sqrtf(trDot(d, d)); hit->kind = g_tr.kind[who]; hit->tri = who; }
    return 1;
}
static int trRay(const float *from, const float *to, EngineTriHit *hit) { return trRayK(from, to, 0u, hit); }

/* a ball of radius r moving from a by D (t in [0, 1]) against one triangle: the first t it touches it at (< best), and the point it touches. -1 if not before best.
 * A ball that starts touching it and moves away or along isn't stopped; one moving into it is, at 0 */
static float trSweepTri(const float *a, const float *D, float r, const float *v, float best, float *cp) {
    float q[3], ap[3]; trClosest(a, v, q); trSub(ap, a, q);
    const float d2 = trDot(ap, ap);
    if (d2 < r * r) { if (trDot(ap, D) < 0.f) { memcpy(cp, q, 12); return 0.f; } return -1.f; }
    float got = -1.f;
    float n[3]; trFaceN(v, n);
    float av0[3]; trSub(av0, a, v); float s0 = trDot(av0, n), sd = trDot(D, n);
    if (s0 < 0.f) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; s0 = -s0; sd = -sd; }
    if (sd < 0.f && s0 >= r) {   /* the face */
        const float t = (s0 - r) / -sd;
        if (t <= best) { float P[3]; for (int k = 0; k < 3; k++) P[k] = a[k] + D[k] * t - n[k] * r;
            float qq[3], pq[3]; trClosest(P, v, qq); trSub(pq, P, qq);
            if (trDot(pq, pq) < 1e-8f * (1.f + r * r)) { best = t; got = t; memcpy(cp, qq, 12); } }
    }
    const float nn = trDot(D, D);
    if (nn < 1e-20f) return got;
    for (int e = 0; e < 3; e++) {   /* the edges, as capsules */
        const float *p = v + e * 3, *p2 = v + ((e + 1) % 3) * 3; float E[3], m[3]; trSub(E, p2, p); trSub(m, a, p);
        const float dd = trDot(E, E), md = trDot(m, E), nd = trDot(D, E), mn = trDot(m, D), mm = trDot(m, m);
        const float A = dd * nn - nd * nd; if (A < 1e-12f * dd * nn) continue;
        const float B = dd * mn - nd * md, C = dd * (mm - r * r) - md * md, disc = B * B - A * C;
        if (disc < 0.f) continue;
        const float t = (-B - sqrtf(disc)) / A; if (t < 0.f || t > best) continue;
        const float s = (md + t * nd) / dd; if (s < 0.f || s > 1.f) continue;
        best = t; got = t; for (int k = 0; k < 3; k++) cp[k] = p[k] + E[k] * s;
    }
    for (int k3 = 0; k3 < 3; k3++) {   /* the corners */
        const float *w = v + k3 * 3; float m[3]; trSub(m, a, w);
        const float B = trDot(m, D), C = trDot(m, m) - r * r, disc = B * B - nn * C; if (disc < 0.f) continue;
        const float t = (-B - sqrtf(disc)) / nn; if (t < 0.f || t > best) continue;
        best = t; got = t; memcpy(cp, w, 12);
    }
    return got;
}
static int trSweepK(const float *from, const float *to, float r, unsigned kinds, EngineTriHit *hit) {
    if (!g_tr.n || !(r > 0.f)) return trRayK(from, to, kinds, hit);
    float d[3], inv[3]; trSub(d, to, from); trInv(d, inv);
    float best = 1.f; int who = -1; float cp[3] = {0, 0, 0}; int st[96], sp = 0; st[sp++] = 0;
    while (sp) {
        const TrNode *N = &g_tr.nd[st[--sp]];
        const float te = trBox(N, from, inv, best, r); if (te < 0.f) continue;
        if (N->n) { for (int i = N->a; i < N->a + N->n; i++) { float c3[3]; if (!TR_KIND_OK(kinds, i)) continue;
                const float t = trSweepTri(from, d, r, g_tr.v + (size_t)i * 9, best, c3);
                if (t >= 0.f && (t < best || who < 0 || (t == best && i < who))) { best = t; who = i; memcpy(cp, c3, 12); } } }
        else if (sp < 94) { st[sp++] = N->a + 1; st[sp++] = N->a; }
    }
    if (who < 0) return 0;
    if (hit) { float c[3], o[3]; for (int k = 0; k < 3; k++) c[k] = from[k] + d[k] * best; trSub(o, c, cp);
        float l = sqrtf(trDot(o, o)); if (l < 1e-6f) { trFaceN(g_tr.v + (size_t)who * 9, o); if (trDot(o, d) > 0.f) { o[0] = -o[0]; o[1] = -o[1]; o[2] = -o[2]; } l = 1.f; }
        hit->t = best; for (int k = 0; k < 3; k++) { hit->pos[k] = cp[k]; hit->nrm[k] = o[k] / l; }
        hit->dist = best * sqrtf(trDot(d, d)); hit->kind = g_tr.kind[who]; hit->tri = who; }
    return 1;
}
static int trSweep(const float *from, const float *to, float r, EngineTriHit *hit) { return trSweepK(from, to, r, 0u, hit); }

static int trNearK(const float *at, float r, unsigned kinds, EngineTriHit *hits, int max) {
    if (!g_tr.n || !(r > 0.f) || max <= 0) return 0;
    int got = 0, st[96], sp = 0; st[sp++] = 0;
    while (sp) {
        const TrNode *N = &g_tr.nd[st[--sp]];
        if (at[0] + r < N->lo[0] || at[0] - r > N->hi[0] || at[1] + r < N->lo[1] || at[1] - r > N->hi[1] || at[2] + r < N->lo[2] || at[2] - r > N->hi[2]) continue;
        if (N->n) { for (int i = N->a; i < N->a + N->n; i++) { const float *v = g_tr.v + (size_t)i * 9; float q[3], o[3]; if (!TR_KIND_OK(kinds, i)) continue; trClosest(at, v, q); trSub(o, at, q);
                const float d2 = trDot(o, o); if (d2 >= r * r) continue;
                const float d = sqrtf(d2); EngineTriHit h; h.t = 0.f; h.dist = d; h.kind = g_tr.kind[i]; h.tri = i; memcpy(h.pos, q, 12);
                if (d > 1e-6f) { for (int k = 0; k < 3; k++) h.nrm[k] = o[k] / d; } else trFaceN(v, h.nrm);
                /* nearest first (ties by number): in among what's kept, and when it's full, the farthest let go, or this one if it's farther still */
                int k;
                if (got < max) k = got++;
                else { const EngineTriHit *L = &hits[max - 1]; if (L->dist < d || (L->dist == d && L->tri < i)) continue; k = max - 1; }
                while (k > 0 && (hits[k - 1].dist > d || (hits[k - 1].dist == d && hits[k - 1].tri > i))) { hits[k] = hits[k - 1]; k--; }
                hits[k] = h; } }
        else if (sp < 94) { st[sp++] = N->a + 1; st[sp++] = N->a; }
    }
    return got;
}
static int trNear(const float *at, float r, EngineTriHit *hits, int max) { return trNearK(at, r, 0u, hits, max); }
static int trCount(void) { return g_tr.n; }
static void apiTriAdd(const float *xyz, int n, int kind, int id) {
    if (!xyz || n <= 0) return;
    if (g_trAddN + n > g_trAddCap) { int c = g_trAddCap ? g_trAddCap : 4096; while (c < g_trAddN + n) c *= 2;
        float *v = (float *)realloc(g_trAddV, (size_t)c * 36); if (!v) return; g_trAddV = v;
        unsigned char *k = (unsigned char *)realloc(g_trAddK, (size_t)c); if (!k) return; g_trAddK = k;
        int *w = (int *)realloc(g_trAddI, (size_t)c * sizeof(int)); if (!w) return; g_trAddI = w; g_trAddCap = c; }
    for (int t = 0; t < n; t++) { const float *s = xyz + (size_t)t * 9; int ok = 1; for (int c = 0; c < 9; c++) if (!(s[c] - s[c] == 0.f) || fabsf(s[c]) > 1e7f) ok = 0;   /* a number that isn't one, or past any world: not kept */
        if (!ok) continue;
        memcpy(g_trAddV + (size_t)g_trAddN * 9, s, 36); g_trAddK[g_trAddN] = (unsigned char)(kind > 0 && kind < 256 ? kind : 0); g_trAddI[g_trAddN] = id; g_trAddN++; }
}
static int apiTriRebuild(void) { trDrop(); trBuild(g_setCur[0] ? g_setCur : "the game's"); return g_tr.n; }
static int apiTriId(int tri) { return g_tr.id && tri >= 0 && tri < g_tr.n ? g_tr.id[tri] : -1; }
