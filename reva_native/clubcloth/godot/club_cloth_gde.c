/* club_cloth_gde.c -- clubcloth in Godot 4.7, as a GDExtension in plain C on the interface header Godot itself writes (godot --dump-gdextension-interface), so nothing to fetch and nothing else to build.
 *
 *   var sim = ClassDB.instantiate(&"ClubClothSim")      # never named in a script: where the library isn't built, the class isn't there, and the game falls back
 *   sim.set_skeleton(skeleton, bones, binds, parents)   # the palette: skeleton bone indices, inverse binds (12 floats each, by rows), parents within the palette
 *   sim.add_body(positions, weights, bones, indices)    # the skin, once per surface
 *   var g = sim.add_garment(positions, weights, bones, indices, limits, nears, [kg/m^2, bending length in cm, shear, drive, damp, flex_damp, hem, substep, slack, friction, lag])
 *   sim.set_capsules(g, [bone a, bone b, press, ...], [radius, ...])    # 0: measured off the skin
 *   sim.set_under(g, g_under, gap)                                       # g worn over g_under; -1: over nothing
 *   sim.set_parts(g, [part of each palette bone, ...], gap)              # a garment's parts keep out of each other
 *   sim.set_boxes([12 floats by rows, 3 for half its size, ...])         # what he's carrying or standing on, in skeleton space, each frame it moves
 *   sim.drawn_points(g), drawn_normals(g), drawn_indices(g), drawn_parents(g)   # the mesh to draw: finer than what's solved
 *   sim.set_output(g, mesh_rid, surface, pos_offset, pos_stride, nrm_offset, nrm_stride, total)
 *   sim.step(delta, gravity, accel, air, omega, alpha)  # reads the bones itself, solves, uploads the meshes. Skeleton space. omega and alpha, how it's turning, may be left off
 *
 * Everything goes through Variant calls: a script can't hold a typed reference to a class that may not exist, and only step() runs every frame.
 * Two garments or more solve at once, one to a thread: the caller waits for the slowest, not for the sum.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clubcloth.h"
#include "gdextension_interface.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CC_EXPORT __declspec(dllexport)
#else
#include <pthread.h>
#include <time.h>
#define CC_EXPORT __attribute__((visibility("default")))
#endif

#define MAX_GARMENTS 8
#define MAX_CAPS 16

static GDExtensionInterfaceGetProcAddress gpa;
static GDExtensionClassLibraryPtr lib;

static GDExtensionInterfaceStringNameNewWithLatin1Chars string_name_new;
static GDExtensionInterfaceClassdbRegisterExtensionClass6 reg_class;
static GDExtensionInterfaceClassdbUnregisterExtensionClass unreg_class;
static GDExtensionInterfaceClassdbRegisterExtensionClassMethod reg_method;
static GDExtensionInterfaceClassdbConstructObject3 construct_object;
static GDExtensionInterfaceObjectSetInstance set_instance;
static GDExtensionInterfaceGetVariantToTypeConstructor get_to;
static GDExtensionInterfaceGetVariantFromTypeConstructor get_from;
static GDExtensionInterfaceVariantGetPtrDestructor get_destructor;
static GDExtensionInterfaceVariantGetPtrConstructor get_constructor;
static GDExtensionInterfaceVariantGetPtrBuiltinMethod get_builtin;
static GDExtensionInterfacePackedFloat32ArrayOperatorIndexConst f32_get;
static GDExtensionInterfacePackedFloat32ArrayOperatorIndex f32_set;
static GDExtensionInterfacePackedInt32ArrayOperatorIndexConst i32_get;
static GDExtensionInterfacePackedInt32ArrayOperatorIndex i32_set;
static GDExtensionInterfacePackedVector3ArrayOperatorIndexConst v3_get;
static GDExtensionInterfacePackedVector3ArrayOperatorIndex v3_set;
static GDExtensionInterfacePackedByteArrayOperatorIndex u8_set;
static GDExtensionInterfaceClassdbGetMethodBind get_method_bind;
static GDExtensionInterfaceObjectMethodBindPtrcall bind_ptrcall;
static GDExtensionInterfaceGlobalGetSingleton get_singleton;

static uint8_t sn_class[8], sn_parent[8], sn_size[8], sn_resize[8], sn_rendering_server[8];
static GDExtensionMethodBindPtr mb_bone_pose, mb_update_region;
static GDExtensionObjectPtr rendering_server;

typedef struct {
    CCGarment *g;
    int active;
    uint8_t rid[8];
    int has_output, surface, pos_off, pos_stride, nrm_off, nrm_stride;
    uint8_t bytes[16];
    int ncap;
    int cap_a[MAX_CAPS], cap_b[MAX_CAPS], cap_press[MAX_CAPS];
    float cap_r[MAX_CAPS];
} SimGarment;

typedef struct Sim Sim;

typedef struct {
    Sim *sim;
    int garment;
#ifdef _WIN32
    HANDLE thread, go, done;
#else
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int go, done;
#endif
    volatile int quit;
} Worker;

struct Sim {
    GDExtensionObjectPtr skeleton;
    int nb;
    int *bones, *parent;
    float *binds, *pose, *pal;
    char *used;
    float *bpos, *bwgt;
    int *bbon, *bidx;
    int bnv, bni;
    SimGarment g[MAX_GARMENTS];
    int ng;
    /* this frame's step, for the workers */
    float dt, gravity[3], accel[3], air[3], omega[3], alpha[3];
    Worker workers[MAX_GARMENTS];
    int nworkers;
    double last_us;
};

/* ---------------------------------------------------------------- variants */

static int64_t arg_int(GDExtensionConstVariantPtr v) {
    int64_t x = 0;
    get_to(GDEXTENSION_VARIANT_TYPE_INT)(&x, (GDExtensionVariantPtr)v);
    return x;
}
static double arg_float(GDExtensionConstVariantPtr v) {
    double x = 0.0;
    get_to(GDEXTENSION_VARIANT_TYPE_FLOAT)(&x, (GDExtensionVariantPtr)v);
    return x;
}
static void arg_vec3(GDExtensionConstVariantPtr v, float *o) {
    get_to(GDEXTENSION_VARIANT_TYPE_VECTOR3)(o, (GDExtensionVariantPtr)v);
}

/* A packed array argument: its handle and where its data is. It holds a reference until unpacked. */
typedef struct {
    uint8_t handle[16];
    GDExtensionVariantType type;
    int64_t n;
} Packed;

static int64_t packed_size(GDExtensionVariantType t, void *handle) {
    int64_t n = 0;
    get_builtin(t, sn_size, 3173160232)(handle, NULL, &n, 0);
    return n;
}
static void pack_arg(Packed *p, GDExtensionConstVariantPtr v, GDExtensionVariantType t) {
    p->type = t;
    get_constructor(t, 0)(p->handle, NULL);
    get_to(t)(p->handle, (GDExtensionVariantPtr)v);
    p->n = packed_size(t, p->handle);
}
static void pack_done(Packed *p) { get_destructor(p->type)(p->handle); }

static float *copy_f32(Packed *p) {
    float *o = (float *)malloc(sizeof(float) * (size_t)(p->n + 1));
    if (p->n > 0) memcpy(o, f32_get(p->handle, 0), sizeof(float) * (size_t)p->n);
    return o;
}
static int *copy_i32(Packed *p) {
    int *o = (int *)malloc(sizeof(int) * (size_t)(p->n + 1));
    if (p->n > 0) memcpy(o, i32_get(p->handle, 0), sizeof(int) * (size_t)p->n);
    return o;
}
static float *copy_v3(Packed *p) {
    float *o = (float *)malloc(sizeof(float) * 3 * (size_t)(p->n + 1));
    if (p->n > 0) memcpy(o, v3_get(p->handle, 0), sizeof(float) * 3 * (size_t)p->n);
    return o;
}

static void ret_int(GDExtensionVariantPtr r, int64_t x) { get_from(GDEXTENSION_VARIANT_TYPE_INT)(r, &x); }
static void ret_float(GDExtensionVariantPtr r, double x) { get_from(GDEXTENSION_VARIANT_TYPE_FLOAT)(r, &x); }
static void ret_v3s(GDExtensionVariantPtr r, const float *data, int n) {
    uint8_t a[16];
    GDExtensionVariantType t = GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY;
    get_constructor(t, 0)(a, NULL);
    int64_t size = n, status = 0;
    const void *args[1] = {&size};
    get_builtin(t, sn_resize, 848867239)(a, (const GDExtensionConstTypePtr *)args, &status, 1);
    if (n > 0) memcpy(v3_set(a, 0), data, sizeof(float) * 3 * (size_t)n);
    get_from(t)(r, a);
    get_destructor(t)(a);
}
static void ret_f32s(GDExtensionVariantPtr r, const float *data, int n) {
    uint8_t a[16];
    GDExtensionVariantType t = GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY;
    get_constructor(t, 0)(a, NULL);
    int64_t size = n, status = 0;
    const void *args[1] = {&size};
    get_builtin(t, sn_resize, 848867239)(a, (const GDExtensionConstTypePtr *)args, &status, 1);
    if (n > 0) memcpy(f32_set(a, 0), data, sizeof(float) * (size_t)n);
    get_from(t)(r, a);
    get_destructor(t)(a);
}

static void ret_i32s(GDExtensionVariantPtr r, const int *data, int n) {
    uint8_t a[16];
    GDExtensionVariantType t = GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY;
    get_constructor(t, 0)(a, NULL);
    int64_t size = n, status = 0;
    const void *args[1] = {&size};
    get_builtin(t, sn_resize, 848867239)(a, (const GDExtensionConstTypePtr *)args, &status, 1);
    if (n > 0) memcpy(i32_set(a, 0), data, sizeof(int) * (size_t)n);
    get_from(t)(r, a);
    get_destructor(t)(a);
}

/* ---------------------------------------------------------------- workers: one thread for each garment after the first */

static void solve_one(Sim *s, SimGarment *sg);

#ifdef _WIN32
static DWORD WINAPI worker_main(LPVOID ud) {
    Worker *w = (Worker *)ud;
    for (;;) {
        WaitForSingleObject(w->go, INFINITE);
        if (w->quit) return 0;
        solve_one(w->sim, &w->sim->g[w->garment]);
        SetEvent(w->done);
    }
}
static void worker_start(Worker *w) {
    w->go = CreateEventA(NULL, FALSE, FALSE, NULL);
    w->done = CreateEventA(NULL, FALSE, FALSE, NULL);
    w->thread = CreateThread(NULL, 0, worker_main, w, 0, NULL);
}
static void worker_kick(Worker *w) { SetEvent(w->go); }
static void worker_wait(Worker *w) { WaitForSingleObject(w->done, INFINITE); }
static void worker_stop(Worker *w) {
    w->quit = 1;
    SetEvent(w->go);
    WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread); CloseHandle(w->go); CloseHandle(w->done);
}
static double now_us(void) {
    LARGE_INTEGER f, t;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return 1e6 * (double)t.QuadPart / (double)f.QuadPart;
}
#else
static void *worker_main(void *ud) {
    Worker *w = (Worker *)ud;
    for (;;) {
        pthread_mutex_lock(&w->lock);
        while (!w->go && !w->quit) pthread_cond_wait(&w->cond, &w->lock);
        w->go = 0;
        int q = w->quit;
        pthread_mutex_unlock(&w->lock);
        if (q) return NULL;
        solve_one(w->sim, &w->sim->g[w->garment]);
        pthread_mutex_lock(&w->lock);
        w->done = 1;
        pthread_cond_broadcast(&w->cond);
        pthread_mutex_unlock(&w->lock);
    }
}
static void worker_start(Worker *w) {
    pthread_mutex_init(&w->lock, NULL);
    pthread_cond_init(&w->cond, NULL);
    w->go = w->done = 0;
    pthread_create(&w->thread, NULL, worker_main, w);
}
static void worker_kick(Worker *w) {
    pthread_mutex_lock(&w->lock);
    w->go = 1;
    pthread_cond_broadcast(&w->cond);
    pthread_mutex_unlock(&w->lock);
}
static void worker_wait(Worker *w) {
    pthread_mutex_lock(&w->lock);
    while (!w->done) pthread_cond_wait(&w->cond, &w->lock);
    w->done = 0;
    pthread_mutex_unlock(&w->lock);
}
static void worker_stop(Worker *w) {
    pthread_mutex_lock(&w->lock);
    w->quit = 1;
    pthread_cond_broadcast(&w->cond);
    pthread_mutex_unlock(&w->lock);
    pthread_join(w->thread, NULL);
}
static double now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1e6 * (double)t.tv_sec + 1e-3 * (double)t.tv_nsec;
}
#endif

/* ---------------------------------------------------------------- the class */

static GDExtensionObjectPtr sim_create(void *ud, GDExtensionBool notify) {
    (void)ud; (void)notify;
    GDExtensionObjectPtr obj = construct_object(sn_parent);
    Sim *s = (Sim *)calloc(1, sizeof(Sim));
    set_instance(obj, sn_class, s);
    return obj;
}

static void sim_free(void *ud, GDExtensionClassInstancePtr inst) {
    (void)ud;
    Sim *s = (Sim *)inst;
    if (!s) return;
    for (int i = 0; i < s->nworkers; i++) worker_stop(&s->workers[i]);
    for (int i = 0; i < s->ng; i++) {
        cc_free(s->g[i].g);
        if (s->g[i].has_output) get_destructor(GDEXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY)(s->g[i].bytes);
    }
    free(s->bones); free(s->parent); free(s->binds); free(s->pose); free(s->pal); free(s->used);
    free(s->bpos); free(s->bwgt); free(s->bbon); free(s->bidx);
    free(s);
}

#define METHOD(name) static void name(void *mu, GDExtensionClassInstancePtr inst, const GDExtensionConstVariantPtr *a, GDExtensionInt argc, GDExtensionVariantPtr r, GDExtensionCallError *err)
#define NEED(n) do { (void)mu; if (argc < (n)) { err->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS; err->expected = (n); return; } } while (0)

METHOD(m_set_skeleton) {
    NEED(4);
    (void)r;
    Sim *s = (Sim *)inst;
    GDExtensionObjectPtr o = NULL;
    get_to(GDEXTENSION_VARIANT_TYPE_OBJECT)(&o, (GDExtensionVariantPtr)a[0]);
    s->skeleton = o;
    Packed b, m, p;
    pack_arg(&b, a[1], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    pack_arg(&m, a[2], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    pack_arg(&p, a[3], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    s->nb = (int)b.n;
    free(s->bones); free(s->binds); free(s->parent); free(s->pose); free(s->pal); free(s->used);
    s->bones = copy_i32(&b);
    s->binds = copy_f32(&m);
    s->parent = copy_i32(&p);
    s->pose = (float *)calloc((size_t)(s->nb + 1), sizeof(float) * 12);
    s->pal = (float *)calloc((size_t)(s->nb + 1), sizeof(float) * 12);
    s->used = (char *)calloc((size_t)(s->nb + 1), 1);
    pack_done(&b); pack_done(&m); pack_done(&p);
}

METHOD(m_add_body) {
    NEED(4);
    (void)r;
    Sim *s = (Sim *)inst;
    Packed pos, wgt, bon, idx;
    pack_arg(&pos, a[0], GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY);
    pack_arg(&wgt, a[1], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    pack_arg(&bon, a[2], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    pack_arg(&idx, a[3], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    int nv = (int)pos.n, ni = (int)idx.n, base = s->bnv;
    s->bpos = (float *)realloc(s->bpos, sizeof(float) * 3 * (size_t)(base + nv + 1));
    s->bwgt = (float *)realloc(s->bwgt, sizeof(float) * 4 * (size_t)(base + nv + 1));
    s->bbon = (int *)realloc(s->bbon, sizeof(int) * 4 * (size_t)(base + nv + 1));
    s->bidx = (int *)realloc(s->bidx, sizeof(int) * (size_t)(s->bni + ni + 1));
    if (nv > 0) {
        memcpy(s->bpos + base * 3, v3_get(pos.handle, 0), sizeof(float) * 3 * (size_t)nv);
        memcpy(s->bwgt + base * 4, f32_get(wgt.handle, 0), sizeof(float) * 4 * (size_t)nv);
        memcpy(s->bbon + base * 4, i32_get(bon.handle, 0), sizeof(int) * 4 * (size_t)nv);
    }
    if (ni > 0) {
        const int32_t *src = i32_get(idx.handle, 0);
        for (int i = 0; i < ni; i++) s->bidx[s->bni + i] = src[i] + base;
    }
    s->bnv += nv;
    s->bni += ni;
    pack_done(&pos); pack_done(&wgt); pack_done(&bon); pack_done(&idx);
}

METHOD(m_add_garment) {
    NEED(7);
    Sim *s = (Sim *)inst;
    if (s->ng >= MAX_GARMENTS) { ret_int(r, -1); return; }
    Packed pos, wgt, bon, idx, lim, nea, par;
    pack_arg(&pos, a[0], GDEXTENSION_VARIANT_TYPE_PACKED_VECTOR3_ARRAY);
    pack_arg(&wgt, a[1], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    pack_arg(&bon, a[2], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    pack_arg(&idx, a[3], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    pack_arg(&lim, a[4], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    pack_arg(&nea, a[5], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    pack_arg(&par, a[6], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    CCParams cp;
    cc_default_params(&cp);
    if (par.n >= 11) {
        const float *q = f32_get(par.handle, 0);
        cc_fabric(&cp, q[0], q[1] * 0.01f, q[2]);
        cp.drive = q[3]; cp.damp = q[4]; cp.flex_damp = q[5]; cp.hem = q[6]; cp.substep = q[7]; cp.slack = q[8]; cp.friction = q[9]; cp.lag = q[10];
    }
    CCGarmentDesc d;
    memset(&d, 0, sizeof(d));
    d.nv = (int)pos.n;
    d.pos = (const float *)v3_get(pos.handle, 0);
    d.wgt = f32_get(wgt.handle, 0);
    d.bon = (const int *)i32_get(bon.handle, 0);
    d.maxd = lim.n >= pos.n && pos.n > 0 ? f32_get(lim.handle, 0) : NULL;
    d.near_ = nea.n >= pos.n && pos.n > 0 ? f32_get(nea.handle, 0) : NULL;
    d.ni = (int)idx.n;
    d.idx = (const int *)i32_get(idx.handle, 0);
    d.nbones = s->nb;
    d.parent = s->parent;
    CCBody body = {s->bnv, s->bpos, s->bwgt, s->bbon, s->bni, s->bidx};
    SimGarment *sg = &s->g[s->ng];
    memset(sg, 0, sizeof(*sg));
    sg->g = cc_create(&d, &body, &cp);
    sg->active = 1;
    for (int v = 0; v < d.nv * CC_BONES; v++) if (d.bon[v] >= 0 && d.bon[v] < s->nb && d.wgt[v] > 0.f) s->used[d.bon[v]] = 1;
    for (int v = 0; v < s->bnv * CC_BONES; v++) if (s->bbon[v] >= 0 && s->bbon[v] < s->nb && s->bwgt[v] > 0.f) s->used[s->bbon[v]] = 1;
    pack_done(&pos); pack_done(&wgt); pack_done(&bon); pack_done(&idx); pack_done(&lim); pack_done(&nea); pack_done(&par);
    ret_int(r, s->ng);
    s->ng++;
}

/* A palette bone's rest position: the inverse of its bind. */
static void rest_origin(const float *m, float *o) {
    float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) { o[0] = o[1] = o[2] = 0.f; return; }
    float inv[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                    (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                    (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    float tx = m[3], ty = m[7], tz = m[11];
    o[0] = -(inv[0] * tx + inv[1] * ty + inv[2] * tz);
    o[1] = -(inv[3] * tx + inv[4] * ty + inv[5] * tz);
    o[2] = -(inv[6] * tx + inv[7] * ty + inv[8] * tz);
}

METHOD(m_set_capsules) {
    NEED(3);
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    SimGarment *sg = &s->g[gi];
    Packed tr, ra;
    pack_arg(&tr, a[1], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    pack_arg(&ra, a[2], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    int n = (int)(tr.n / 3);
    if (n > MAX_CAPS) n = MAX_CAPS;
    const int32_t *t = n > 0 ? i32_get(tr.handle, 0) : NULL;
    const float *q = ra.n > 0 ? f32_get(ra.handle, 0) : NULL;
    CCBody body = {s->bnv, s->bpos, s->bwgt, s->bbon, s->bni, s->bidx};
    float out[MAX_CAPS];
    for (int c = 0; c < n; c++) {
        sg->cap_a[c] = t[c * 3];
        sg->cap_b[c] = t[c * 3 + 1];
        sg->cap_press[c] = t[c * 3 + 2];
        s->used[t[c * 3]] = 1;
        s->used[t[c * 3 + 1]] = 1;
        float want = (q && c < ra.n) ? q[c] : 0.f;
        if (want <= 0.f) {
            float ra0[3], rb0[3];
            rest_origin(s->binds + t[c * 3] * 12, ra0);
            rest_origin(s->binds + t[c * 3 + 1] * 12, rb0);
            want = cc_measure_radius(&body, t[c * 3], ra0, rb0);
        }
        sg->cap_r[c] = want;
        out[c] = want;
        float ra1[3], rb1[3];
        rest_origin(s->binds + t[c * 3] * 12, ra1);
        rest_origin(s->binds + t[c * 3 + 1] * 12, rb1);
        cc_set_capsule_rest(sg->g, c, ra1, rb1, want);
    }
    sg->ncap = n;
    pack_done(&tr); pack_done(&ra);
    ret_f32s(r, out, n);
}

METHOD(m_set_output) {
    NEED(8);
    (void)r;
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    SimGarment *sg = &s->g[gi];
    get_to(GDEXTENSION_VARIANT_TYPE_RID)(sg->rid, (GDExtensionVariantPtr)a[1]);
    sg->surface = (int)arg_int(a[2]);
    sg->pos_off = (int)arg_int(a[3]);
    sg->pos_stride = (int)arg_int(a[4]);
    sg->nrm_off = (int)arg_int(a[5]);
    sg->nrm_stride = (int)arg_int(a[6]);
    int64_t total = arg_int(a[7]), status = 0;
    GDExtensionVariantType t = GDEXTENSION_VARIANT_TYPE_PACKED_BYTE_ARRAY;
    if (sg->has_output) get_destructor(t)(sg->bytes);
    get_constructor(t, 0)(sg->bytes, NULL);
    const void *args[1] = {&total};
    get_builtin(t, sn_resize, 848867239)(sg->bytes, (const GDExtensionConstTypePtr *)args, &status, 1);
    sg->has_output = 1;
}

METHOD(m_set_active) {
    NEED(2);
    (void)r;
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    int on = arg_int(a[1]) != 0;
    if (on && !s->g[gi].active) cc_reset(s->g[gi].g);
    s->g[gi].active = on;
}

METHOD(m_reset) {
    (void)mu; (void)a; (void)argc; (void)r; (void)err;
    Sim *s = (Sim *)inst;
    for (int i = 0; i < s->ng; i++) cc_reset(s->g[i].g);
}

/* Its space turned under it all at once, with the pose: 12 floats by rows, from the old space to the new. */
METHOD(m_carry) {
    NEED(1);
    (void)r;
    Sim *s = (Sim *)inst;
    Packed q;
    pack_arg(&q, a[0], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    if (q.n >= 12) {
        const float *f = f32_get(q.handle, 0);
        for (int i = 0; i < s->ng; i++) cc_carry(s->g[i].g, f);
    }
    pack_done(&q);
}

METHOD(m_set_parts) {
    NEED(3);
    (void)r;
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    Packed parts;
    pack_arg(&parts, a[1], GDEXTENSION_VARIANT_TYPE_PACKED_INT32_ARRAY);
    if ((int)parts.n >= s->nb && s->nb > 0) cc_set_parts(s->g[gi].g, (const int *)i32_get(parts.handle, 0), (float)arg_float(a[2]));
    pack_done(&parts);
}

/* Boxes for every garment: [12 floats by rows, 3 for half its size, ...], in skeleton space. */
METHOD(m_set_boxes) {
    NEED(1);
    (void)r;
    Sim *s = (Sim *)inst;
    Packed q;
    pack_arg(&q, a[0], GDEXTENSION_VARIANT_TYPE_PACKED_FLOAT32_ARRAY);
    CCBox boxes[CC_MAX_BOXES];
    int n = (int)(q.n / 15);
    if (n > CC_MAX_BOXES) n = CC_MAX_BOXES;
    const float *f = n > 0 ? f32_get(q.handle, 0) : NULL;
    for (int i = 0; i < n; i++) {
        memcpy(boxes[i].m, f + i * 15, sizeof(float) * 12);
        memcpy(boxes[i].half, f + i * 15 + 12, sizeof(float) * 3);
    }
    for (int i = 0; i < s->ng; i++) cc_set_boxes(s->g[i].g, boxes, n);
    pack_done(&q);
}

/* The mesh to draw: finer than what's solved. */
METHOD(m_drawn_points) {
    NEED(1);
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    int n = cc_drawn_vertex_count(s->g[gi].g);
    float *o = (float *)malloc(sizeof(float) * 3 * (size_t)(n + 1));
    cc_drawn_read(s->g[gi].g, o, NULL, NULL);
    ret_v3s(r, o, n);
    free(o);
}
METHOD(m_drawn_normals) {
    NEED(1);
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    int n = cc_drawn_vertex_count(s->g[gi].g);
    float *o = (float *)malloc(sizeof(float) * 3 * (size_t)(n + 1));
    cc_drawn_read(s->g[gi].g, NULL, o, NULL);
    ret_v3s(r, o, n);
    free(o);
}
METHOD(m_drawn_parents) {
    NEED(1);
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    int n = cc_drawn_vertex_count(s->g[gi].g);
    int *o = (int *)malloc(sizeof(int) * 2 * (size_t)(n + 1));
    cc_drawn_read(s->g[gi].g, NULL, NULL, o);
    ret_i32s(r, o, n * 2);
    free(o);
}
METHOD(m_drawn_indices) {
    NEED(1);
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]);
    if (gi < 0 || gi >= s->ng) return;
    ret_i32s(r, cc_drawn_indices(s->g[gi].g), cc_drawn_index_count(s->g[gi].g));
}

METHOD(m_set_under) {
    NEED(3);
    (void)r;
    Sim *s = (Sim *)inst;
    int gi = (int)arg_int(a[0]), ui = (int)arg_int(a[1]);
    if (gi < 0 || gi >= s->ng) return;
    cc_set_under(s->g[gi].g, ui >= 0 && ui < s->ng ? s->g[ui].g : NULL, (float)arg_float(a[2]));
}

static void solve_one(Sim *s, SimGarment *sg) {
    CCCapsule caps[MAX_CAPS];
    for (int c = 0; c < sg->ncap; c++) {
        const float *pa = s->pose + sg->cap_a[c] * 12, *pb = s->pose + sg->cap_b[c] * 12;
        caps[c].a[0] = pa[3]; caps[c].a[1] = pa[7]; caps[c].a[2] = pa[11];
        caps[c].b[0] = pb[3]; caps[c].b[1] = pb[7]; caps[c].b[2] = pb[11];
        caps[c].r = sg->cap_r[c];
        caps[c].bone = sg->cap_a[c];
        caps[c].press = sg->cap_press[c];
    }
    cc_solve(sg->g, s->dt, s->gravity, s->accel, s->air, caps, sg->ncap);
}

METHOD(m_step) {
    NEED(4);
    (void)r;
    Sim *s = (Sim *)inst;
    if (!s->skeleton || s->ng == 0) return;
    double t0 = now_us();
    s->dt = (float)arg_float(a[0]);
    if (s->dt <= 0.f) return;
    arg_vec3(a[1], s->gravity);
    arg_vec3(a[2], s->accel);
    arg_vec3(a[3], s->air);
    s->omega[0] = s->omega[1] = s->omega[2] = 0.f;
    s->alpha[0] = s->alpha[1] = s->alpha[2] = 0.f;
    if (argc >= 6) { arg_vec3(a[4], s->omega); arg_vec3(a[5], s->alpha); }
    /* The pose, as the skeleton has it at this instant, and the palette. */
    for (int i = 0; i < s->nb; i++) {
        if (!s->used[i]) continue;
        int64_t b = s->bones[i];
        const void *args[1] = {&b};
        float x[12];
        bind_ptrcall(mb_bone_pose, s->skeleton, (const GDExtensionConstTypePtr *)args, x);
        float *p = s->pose + i * 12;
        /* Transform3D: three rows, then the origin. */
        p[0] = x[0]; p[1] = x[1]; p[2] = x[2]; p[3] = x[9];
        p[4] = x[3]; p[5] = x[4]; p[6] = x[5]; p[7] = x[10];
        p[8] = x[6]; p[9] = x[7]; p[10] = x[8]; p[11] = x[11];
        const float *q = s->binds + i * 12;
        float *m = s->pal + i * 12;
        for (int row = 0; row < 3; row++) {
            const float *pr = p + row * 4;
            m[row * 4 + 0] = pr[0] * q[0] + pr[1] * q[4] + pr[2] * q[8];
            m[row * 4 + 1] = pr[0] * q[1] + pr[1] * q[5] + pr[2] * q[9];
            m[row * 4 + 2] = pr[0] * q[2] + pr[1] * q[6] + pr[2] * q[10];
            m[row * 4 + 3] = pr[0] * q[3] + pr[1] * q[7] + pr[2] * q[11] + pr[3];
        }
    }
    /* One garment here, the rest on threads of their own. */
    int list[MAX_GARMENTS], n = 0;
    for (int i = 0; i < s->ng; i++) if (s->g[i].active) list[n++] = i;
    /* Every garment's pose first, on this thread: a garment over another reads it. */
    for (int i = 0; i < n; i++) {
        cc_set_turning(s->g[list[i]].g, s->omega, s->alpha);
        cc_pose(s->g[list[i]].g, s->pal);
    }
    for (int i = 0; i < n; i++) cc_layer(s->g[list[i]].g);
    while (s->nworkers < n - 1 && s->nworkers < MAX_GARMENTS) {
        Worker *w = &s->workers[s->nworkers];
        w->sim = s;
        worker_start(w);
        s->nworkers++;
    }
    for (int i = 1; i < n; i++) { s->workers[i - 1].garment = list[i]; worker_kick(&s->workers[i - 1]); }
    if (n > 0) solve_one(s, &s->g[list[0]]);
    for (int i = 1; i < n; i++) worker_wait(&s->workers[i - 1]);
    /* And up to the GPU: this thread only, a vertex buffer's region for each garment. The engine's singletons don't exist yet when an extension loads. */
    if (!rendering_server) rendering_server = get_singleton(sn_rendering_server);
    for (int k = 0; k < n; k++) {
        SimGarment *sg = &s->g[list[k]];
        if (!sg->has_output) continue;
        uint8_t *bytes = u8_set(sg->bytes, 0);
        cc_write(sg->g, bytes + sg->pos_off, sg->pos_stride, sg->nrm_stride > 0 ? bytes + sg->nrm_off : NULL, sg->nrm_stride);
        int64_t surface = sg->surface, offset = 0;
        const void *args[4] = {sg->rid, &surface, &offset, sg->bytes};
        bind_ptrcall(mb_update_region, rendering_server, (const GDExtensionConstTypePtr *)args, NULL);
    }
    s->last_us = now_us() - t0;
}

#define GETTER(name, fn, ret, count) METHOD(name) { NEED(1); Sim *s = (Sim *)inst; int gi = (int)arg_int(a[0]); if (gi < 0 || gi >= s->ng) return; ret(r, fn(s->g[gi].g), count(s->g[gi].g)); }
GETTER(m_points, cc_points, ret_v3s, cc_point_count)
GETTER(m_shown, cc_shown, ret_v3s, cc_point_count)
GETTER(m_targets, cc_targets, ret_v3s, cc_point_count)
GETTER(m_skinned, cc_skinned, ret_v3s, cc_point_count)
GETTER(m_backstops, cc_backstops, ret_v3s, cc_point_count)
GETTER(m_backnormals, cc_backnormals, ret_v3s, cc_point_count)
GETTER(m_normals, cc_normals, ret_v3s, cc_point_count)
GETTER(m_limits, cc_limits, ret_f32s, cc_point_count)
GETTER(m_nears, cc_nears, ret_f32s, cc_point_count)

METHOD(m_last_microseconds) {
    (void)mu; (void)a; (void)argc; (void)err;
    ret_float(r, ((Sim *)inst)->last_us);
}

/* ---------------------------------------------------------------- registration */

static void reg(const char *name, GDExtensionClassMethodCall fn) {
    uint8_t sn[8];
    string_name_new(sn, name, 1);
    GDExtensionClassMethodInfo mi;
    memset(&mi, 0, sizeof(mi));
    mi.name = sn;
    mi.call_func = fn;
    mi.method_flags = GDEXTENSION_METHOD_FLAGS_DEFAULT | GDEXTENSION_METHOD_FLAG_VARARG;
    reg_method(lib, sn_class, &mi);
}

static void on_init(void *ud, GDExtensionInitializationLevel level) {
    (void)ud;
    if (level != GDEXTENSION_INITIALIZATION_SCENE) return;
    string_name_new(sn_class, "ClubClothSim", 1);
    string_name_new(sn_parent, "RefCounted", 1);
    string_name_new(sn_size, "size", 1);
    string_name_new(sn_resize, "resize", 1);
    uint8_t c[8], m[8];
    string_name_new(c, "Skeleton3D", 1);
    string_name_new(m, "get_bone_global_pose", 1);
    mb_bone_pose = get_method_bind(c, m, 1965739696);
    string_name_new(c, "RenderingServer", 1);
    string_name_new(m, "mesh_surface_update_vertex_region", 1);
    mb_update_region = get_method_bind(c, m, 2900195149);
    string_name_new(sn_rendering_server, "RenderingServer", 1);
    GDExtensionClassCreationInfo6 info;
    memset(&info, 0, sizeof(info));
    info.is_exposed = 1;
    info.create_instance_func = sim_create;
    info.free_instance_func = sim_free;
    reg_class(lib, sn_class, sn_parent, &info);
    reg("set_skeleton", m_set_skeleton);
    reg("add_body", m_add_body);
    reg("add_garment", m_add_garment);
    reg("set_capsules", m_set_capsules);
    reg("set_under", m_set_under);
    reg("set_parts", m_set_parts);
    reg("set_boxes", m_set_boxes);
    reg("drawn_points", m_drawn_points);
    reg("drawn_normals", m_drawn_normals);
    reg("drawn_parents", m_drawn_parents);
    reg("drawn_indices", m_drawn_indices);
    reg("normals", m_normals);
    reg("set_output", m_set_output);
    reg("set_active", m_set_active);
    reg("reset", m_reset);
    reg("carry", m_carry);
    reg("step", m_step);
    reg("points", m_points);
    reg("shown", m_shown);
    reg("targets", m_targets);
    reg("skinned", m_skinned);
    reg("backstops", m_backstops);
    reg("backnormals", m_backnormals);
    reg("limits", m_limits);
    reg("nears", m_nears);
    reg("last_microseconds", m_last_microseconds);
}

static void on_deinit(void *ud, GDExtensionInitializationLevel level) {
    (void)ud;
    if (level == GDEXTENSION_INITIALIZATION_SCENE) unreg_class(lib, sn_class);
}

#define LOAD(var, type, name) var = (type)gpa(name)

CC_EXPORT GDExtensionBool club_cloth_init(GDExtensionInterfaceGetProcAddress p_gpa, GDExtensionClassLibraryPtr p_lib, GDExtensionInitialization *init) {
    gpa = p_gpa;
    lib = p_lib;
    LOAD(string_name_new, GDExtensionInterfaceStringNameNewWithLatin1Chars, "string_name_new_with_latin1_chars");
    LOAD(reg_class, GDExtensionInterfaceClassdbRegisterExtensionClass6, "classdb_register_extension_class6");
    LOAD(unreg_class, GDExtensionInterfaceClassdbUnregisterExtensionClass, "classdb_unregister_extension_class");
    LOAD(reg_method, GDExtensionInterfaceClassdbRegisterExtensionClassMethod, "classdb_register_extension_class_method");
    LOAD(construct_object, GDExtensionInterfaceClassdbConstructObject3, "classdb_construct_object3");
    LOAD(set_instance, GDExtensionInterfaceObjectSetInstance, "object_set_instance");
    LOAD(get_to, GDExtensionInterfaceGetVariantToTypeConstructor, "get_variant_to_type_constructor");
    LOAD(get_from, GDExtensionInterfaceGetVariantFromTypeConstructor, "get_variant_from_type_constructor");
    LOAD(get_destructor, GDExtensionInterfaceVariantGetPtrDestructor, "variant_get_ptr_destructor");
    LOAD(get_constructor, GDExtensionInterfaceVariantGetPtrConstructor, "variant_get_ptr_constructor");
    LOAD(get_builtin, GDExtensionInterfaceVariantGetPtrBuiltinMethod, "variant_get_ptr_builtin_method");
    LOAD(f32_get, GDExtensionInterfacePackedFloat32ArrayOperatorIndexConst, "packed_float32_array_operator_index_const");
    LOAD(f32_set, GDExtensionInterfacePackedFloat32ArrayOperatorIndex, "packed_float32_array_operator_index");
    LOAD(i32_get, GDExtensionInterfacePackedInt32ArrayOperatorIndexConst, "packed_int32_array_operator_index_const");
    LOAD(i32_set, GDExtensionInterfacePackedInt32ArrayOperatorIndex, "packed_int32_array_operator_index");
    LOAD(v3_get, GDExtensionInterfacePackedVector3ArrayOperatorIndexConst, "packed_vector3_array_operator_index_const");
    LOAD(v3_set, GDExtensionInterfacePackedVector3ArrayOperatorIndex, "packed_vector3_array_operator_index");
    LOAD(u8_set, GDExtensionInterfacePackedByteArrayOperatorIndex, "packed_byte_array_operator_index");
    LOAD(get_method_bind, GDExtensionInterfaceClassdbGetMethodBind, "classdb_get_method_bind");
    LOAD(bind_ptrcall, GDExtensionInterfaceObjectMethodBindPtrcall, "object_method_bind_ptrcall");
    LOAD(get_singleton, GDExtensionInterfaceGlobalGetSingleton, "global_get_singleton");
    init->minimum_initialization_level = GDEXTENSION_INITIALIZATION_SCENE;
    init->userdata = NULL;
    init->initialize = on_init;
    init->deinitialize = on_deinit;
    return 1;
}
