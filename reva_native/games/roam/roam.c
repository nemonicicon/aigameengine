/* roam.c -- a world bigger than memory, and a place to start one: run across it, and the cells near you come and go. It's scenery so far: the game is getting across it. make_assets.py makes the cells by code, as many as you ask for, so nothing here depends on art.
 * Stick or WASD moves. Leave it alone and it runs a long line back and forth across the world. [stream] in the log says what the disk and the machine made of it. */
#include "../../engine.h"
#include <stdlib.h>

static const EngineAPI *E;

typedef struct { float x, z, dir, edge, speed, was, side; int stuck; } State;
static State S;

static const char *street(void) { return "roam"; }
static float tod(void) { return 16.f; }

static void start(int onSet) {
    (void)onSet;
    float x = -300.f, z = 0.f;
    E->mark("start", &x, NULL, &z);
    if (getenv("ROAM_START")) sscanf(getenv("ROAM_START"), "%f,%f", &x, &z);   /* a bench's question: the world from somewhere else */
    *E->heroX = x; *E->heroZ = z;
    S.dir = 1.f; S.edge = 300.f; S.speed = 11.f; S.side = 1.f;
    E->mark("edge", &S.edge, NULL, NULL);
    if (getenv("ROAM_SPEED")) S.speed = (float)atof(getenv("ROAM_SPEED"));   /* how fast the world comes at you: a bench's question */
    E->tune->heroSnap = 1.f; E->tune->heroTurn = 900.f; E->tune->heroSpd = 11.f;
    E->tune->lensArm = 22.f; E->tune->lensPitch = 38.f; E->tune->lensFov = 46.f; E->tune->lensAimY = 1.7f;
    if (getenv("ROAM_PITCH")) E->tune->lensPitch = (float)atof(getenv("ROAM_PITCH"));   /* a lens that can see the horizon */
}

static void tick(float dt) {
    S.x = *E->heroX; S.z = *E->heroZ;
    /* a bench's question: ROAM_PATH=dx,dz asks the way to a place that far from her once a second, and says how it came out. REVA_TALKDBG=1 has the engine say how it looked:
     * across cells whose part of the map is in, it's the map, and where one isn't in yet it's the slower search */
    if (getenv("ROAM_PATH")) {
        static float since;
        since += dt;
        if (since >= 1.f) {
            since = 0.f;
            float d[2] = {0.f, 0.f}, xyz[64 * 3];
            sscanf(getenv("ROAM_PATH"), "%f,%f", &d[0], &d[1]);
            int n = E->path(S.x, *E->heroFeet, S.z, S.x + d[0], 0.f, S.z + d[1], 0.f, xyz, 64);
            printf("[roam] the way from %.0f,%.0f to %.0f,%.0f: %d points, ending at %.1f,%.1f\n", S.x, S.z, S.x + d[0], S.z + d[1], n, xyz[(n - 1) * 3], xyz[(n - 1) * 3 + 2]);
        }
    }
}

/* nobody's at the pad: back and forth along a line that keeps moving over, so it keeps finding cells it hasn't been near for a while. The boxes are solid, so when one stops her she edges along it until she's past: one way, then the other, and if that's a corner, back the way she came */
static void host(float *heroHead, float dt) {
    if (*E->heroX > S.edge) { S.dir = -1.f; *E->heroZ += 60.f; }
    if (*E->heroX < -S.edge) { S.dir = 1.f; *E->heroZ += 60.f; }
    if (*E->heroZ > S.edge) *E->heroZ = -S.edge;
    *heroHead = S.dir > 0.f ? 1.5708f : -1.5708f;
    *E->heroSpd = S.speed;
    if (fabsf(*E->heroX - S.was) < S.speed * dt * 0.25f) {
        S.stuck++;
        if (S.stuck % 40 == 0) S.side = -S.side;
        if (S.stuck % 160 == 0) S.dir = -S.dir;
        *E->heroZ += S.side * S.speed * dt;
    } else S.stuck = 0;
    S.was = *E->heroX;
    *E->heroX += S.dir * S.speed * dt;
}

static void hud(const void *state) {
    const State *s = state;
    char t[40];
    snprintf(t, sizeof t, "%.0f, %.0f", s->x, s->z);
    E->tvText(0, 0, 1, t, 1.f, 1.f, 1.f);
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "ROAM", .state = &S, .stateSize = sizeof S, .street = street, .tod = tod, .start = start, .tick = tick, .hud = hud, .host = host };
    E = engine;
    return &G;
}
