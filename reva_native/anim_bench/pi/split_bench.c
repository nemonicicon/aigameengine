/* split_bench.c -- what split screen costs on the real renderer (engine plan, Phase 6: bench first). The tower's level, argv[1] bodies walking about it, and REVA_VIEWS pictures of them, each lens going round slowly.
 * Build it as the game: GAME=anim_bench/pi/split_bench.c EXTRA=-DREVA_NODOORS ./build_pi.sh vk. Run it from games/tower: REVA_VIEWS=4 REVA_VIDEO=240p ../../crowd_vk 100 packs packs/v1_tower.rpack */
#include "../../engine.h"
#include <stdlib.h>

static const EngineAPI *E;
static int g_views = 1;

static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static const char *street(void) { return "tower"; }
static float tod(void) { return 17.5f; }

static void boot(void) {
    const char *e = getenv("REVA_VIEWS");
    g_views = e ? atoi(e) : 1;
    E->views(g_views);
    printf("[split] %d views\n", g_views);
}

static void start(int onSet) {
    (void)onSet;
    E->tune->lensFov = 46.f;
    E->tune->bodyCap = 2000;
    for (int i = 0; i < E->nBodies; i++) {
        EngineBody *b = body(i);
        b->x = b->tx = -30.f + (float)(i % 20) * 3.f;
        b->z = b->tz = 16.f + (float)(i / 20) * 3.f;
        b->h = (float)i; b->spd = 2.f; b->moving = 1; b->state = 1; b->mate = -1;
        E->team[i] = 1; E->hide[i] = 0;
    }
    *E->heroX = 0.f; *E->heroZ = 22.f;
}

static void tick(float dt) {
    (void)dt;
    float t = *E->gameT;
    /* far enough back that every body is in the picture: past this many the frustum would quietly drop them, and the number would mean nothing */
    float back = E->nBodies > 100 ? 1.3f * sqrtf((float)E->nBodies / 100.f) : 1.f, cz = 16.f + (float)(E->nBodies / 40) * 3.f;
    for (int v = 0; v < g_views; v++) {
        float a = t * 0.2f + (float)v * 1.57f;
        E->viewLens(v, sinf(a) * 34.f * back, 15.f * back, cz + cosf(a) * 20.f * back, 0.f, 1.f, cz);
    }
}

static int bodyStep(int i, float dt) {
    EngineBody *b = body(i);
    b->h += sinf(*E->gameT + (float)i) * dt;
    float x = b->x + sinf(b->h) * b->spd * dt, z = b->z + cosf(b->h) * b->spd * dt;
    if (x < -40.f || x > 40.f) b->h += 3.14f;
    if (z < 14.f || z > 42.f) b->h += 3.14f;
    b->x = x; b->z = z; b->tx = x; b->tz = z; b->rotY = b->h;
    return 1;
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "SPLIT BENCH", .boot = boot, .street = street, .tod = tod, .start = start, .tick = tick, .bodyStep = bodyStep };
    E = engine;
    return &G;
}
