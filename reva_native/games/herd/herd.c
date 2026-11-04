/* herd.c -- a small crowd game, and the place to start an RTS: hundreds of bodies on one field. Yours are blue. Hold A and everyone near the cursor comes with it; lead them into the red ones and they turn blue. Turn them all.
 * Stick or WASD moves the cursor, A or SPACE gathers, right stick turns the lens, B or E plays again. Leave it alone and it plays itself. make_assets.py makes the field and the body by code. How many bodies is the number on the command line: `crowd_herd_vk 400 packs packs/v1_herd.rpack`.
 * What it shows: E->nearby, so bodies keep apart without every body asking about every other; E->tune->bodyCap, which says how many may be drawn; the lens on an arm. A body here is a light one, about 300 triangles and 26 bones, and that's why there can be this many: a body's weight is what a crowd is budgeted by. */
#include "../../engine.h"

static const EngineAPI *E;

#define FIELD 110.f
#define GATHER 22.f
#define TURN_REACH 2.2f

typedef struct {
    int gather, again;
    float cx, cz, yaw, wait;
    int blue, red, over, attract, holding;
} State;
static State S;

static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static const char *street(void) { return "herd"; }
static float tod(void) { return 16.f; }

static void newGame(void) {
    /* the first few are yours, in the middle. Everyone else is scattered, each with a place of their own to stand */
    for (int i = 0; i < E->nBodies; i++) {
        EngineBody *b = body(i);
        int mine = i < E->nBodies / 8;
        float a = E->frnd() * 6.283f, r = mine ? 4.f + E->frnd() * 8.f : 25.f + E->frnd() * (FIELD - 30.f);
        b->x = b->tx = sinf(a) * r; b->z = b->tz = cosf(a) * r;
        b->h = E->frnd() * 6.283f; b->spd = 0.f; b->state = 0; b->moving = 0; b->mate = -1;
        E->hide[i] = 0; E->team[i] = mine ? 1 : 2;
    }
    S.cx = S.cz = 0.f; S.over = 0; S.wait = 0.f;
}

static void boot(void) {
    S.gather = E->action("gather", PAD_A);
    S.again = E->action("again", PAD_B);
    sfxPreload("banner"); sfxPreload("turn");
}

static void lens(void) {
    float up = 1.05f, arm = 46.f;
    E->lens(S.cx + sinf(S.yaw) * cosf(up) * arm, sinf(up) * arm, S.cz + cosf(S.yaw) * cosf(up) * arm, S.cx, 0.5f, S.cz);
}

static void start(int onSet) {
    (void)onSet;
    E->tune->lensFov = 46.f;
    E->tune->bodyCap = 2000;   /* every one of them may be drawn */
    /* for more bodies than this: E->tune->poseRate and poseNear let the far ones share poses. REVA_POSERATE=15 REVA_POSENEAR=24 tries it here */
    *E->heroX = 0.f; *E->heroZ = -2000.f;   /* there's no hero in this game: she's parked well off the field */
    newGame();
    lens();
    E->banner("TURN THEM ALL", 3.f);
}

static void tick(float dt) {
    const EnginePad *pad = &E->pads[0];
    S.attract = *E->attract;
    if (S.over) {
        S.wait += dt;
        if (E->actionPressed(0, S.again) || (S.attract && S.wait > 4.f)) newGame();
    }
    float mx = pad->x, my = pad->y;
    S.holding = E->actionHeld(0, S.gather);
    if (S.attract) {   /* nobody's at the pad: go for the nearest red one with everyone in tow */
        float best = 1e9f, tx = S.cx, tz = S.cz;
        for (int i = 0; i < E->nBodies; i++) if (E->team[i] == 2) { float d = hypotf(body(i)->x - S.cx, body(i)->z - S.cz); if (d < best) { best = d; tx = body(i)->x; tz = body(i)->z; } }
        float dx = tx - S.cx, dz = tz - S.cz, d = hypotf(dx, dz);
        float cy = cosf(S.yaw), sy = sinf(S.yaw);
        if (d > 0.1f) { mx = (dx * cy - dz * sy) / d * 0.5f; my = (-dx * sy - dz * cy) / d * 0.5f; }
        S.holding = 1;
        S.yaw += dt * 0.05f;
    } else S.yaw += pad->rx * dt * 1.8f;
    S.cx += (cosf(S.yaw) * mx - sinf(S.yaw) * my) * 26.f * dt;
    S.cz += (-sinf(S.yaw) * mx - cosf(S.yaw) * my) * 26.f * dt;
    if (S.cx > FIELD) S.cx = FIELD;
    if (S.cx < -FIELD) S.cx = -FIELD;
    if (S.cz > FIELD) S.cz = FIELD;
    if (S.cz < -FIELD) S.cz = -FIELD;
    lens();
    S.blue = S.red = 0;
    for (int i = 0; i < E->nBodies; i++) { if (E->team[i] == 1) S.blue++; else if (E->team[i] == 2) S.red++; }
    if (!S.over && S.red == 0) { S.over = 1; S.wait = 0.f; E->banner("ALL TURNED", 4.f); }
}

/* a body's whole mind. Blue, near the cursor while it's held: come on. Everyone else strolls about a place of their own. A red one a blue one reaches turns blue */
static int bodyStep(int i, float dt) {
    EngineBody *b = body(i);
    int mine = E->team[i] == 1;
    float tx = b->tx, tz = b->tz, want = 0.f;
    if (mine && S.holding && hypotf(b->x - S.cx, b->z - S.cz) < GATHER) { tx = b->tx = S.cx; tz = b->tz = S.cz; want = 7.f; }
    float dx = tx - b->x, dz = tz - b->z, d = hypotf(dx, dz);
    if (d > 1.2f && want == 0.f) want = 2.2f;
    if (d > 0.3f) {
        float turn = fmodf(atan2f(dx, dz) - b->h + 9.42477f, 6.28318f) - 3.14159f;
        b->h += turn * fminf(1.f, dt * 6.f);
    } else want = 0.f;
    b->spd += (want - b->spd) * fminf(1.f, dt * 6.f);
    float x = b->x + sinf(b->h) * b->spd * dt, z = b->z + cosf(b->h) * b->spd * dt;
    int nb[48], nn = E->nearby(x, z, TURN_REACH, nb, 48);
    for (int n = 0; n < nn; n++) {
        int j = nb[n]; if (j == i) continue;
        const EngineBody *o = body(j);
        float ax = x - o->x, az = z - o->z, a = hypotf(ax, az);
        if (a > 0.01f && a < 1.4f) { x += ax / a * (1.4f - a) * dt * 4.f; z += az / a * (1.4f - a) * dt * 4.f; }
        if (mine && E->team[j] == 2 && a < TURN_REACH) {
            E->team[j] = 1;
            E->particles(o->x, 1.5f, o->z, 10, 4.f, 0.5f, 0.2f, 0.4f, 0.7f, 1.2f, 6.f);
            sfxPlay("turn", 0.3f, NULL, 0);
        }
    }
    b->x = x; b->z = z; b->rotY = b->h; b->moving = b->spd > 0.2f; b->state = b->moving;
    return 1;
}

static float bodyUse(int i) { (void)i; return 0.5f; }

static void draw(const void *state) {
    const State *s = state;
    float k = s->holding ? 1.f : 0.45f;
    E->ring(s->cx, s->cz, GATHER, 0.12f, 0.35f * k, 0.8f * k, 1.2f * k, 48);
    E->ring(s->cx, s->cz, 1.2f, 0.12f, 1.1f * k, 1.1f * k, 0.6f * k, 16);
}

static void hud(const void *state) {
    const State *s = state;
    char t[32];
    snprintf(t, sizeof t, "YOURS %d", s->blue); E->tvText(0, 0, 1, t, 0.55f, 0.75f, 1.f);
    snprintf(t, sizeof t, "LEFT %d", s->red); E->tvTextR(0, 1, t, 1.f, 0.6f, 0.5f);
    const char *m = E->bannerText();
    if (m) E->tvTextC(5, (int)strlen(m) * 2 <= TV_COLS ? 2 : 1, m, 1.f, 0.92f, 0.4f);
    if (s->attract) { if (fmodf(*E->gameT, 2.f) < 1.4f) E->tvTextC(22, 1, "PRESS A TO PLAY", 1.f, 1.f, 1.f); }
    else if (s->over) E->tvTextC(22, 1, "B TO PLAY AGAIN", 1.f, 1.f, 1.f);
}

/* nobody's at the pad, and there's no hero to walk */
static void host(float *heroHead, float dt) { (void)heroHead; (void)dt; *E->heroSpd = 0.f; }

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "HERD", .state = &S, .stateSize = sizeof S, .boot = boot, .street = street, .tod = tod, .start = start, .tick = tick, .draw = draw, .hud = hud, .host = host, .bodyStep = bodyStep, .bodyUse = bodyUse };
    E = engine;
    return &G;
}
