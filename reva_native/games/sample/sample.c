/* sample.c -- a small whole game, and the place to start a new one: copy this folder. It includes engine.h and nothing else of the engine's, and it needs no content but its own body (make_body.py).
 * TAG: bodies wander a yard, and you run them down before the clock runs out. Each round has more of them. Stick moves, A sprints, B plays again. Nobody at the pad, and the game plays itself.
 * The shape to copy: tick changes things and writes them into the state. draw and hud get that state const and only put it on the screen. Anything left out of the table at the bottom gets a default. */
#include "../../engine.h"

static const EngineAPI *E;

#define YARD_X0 -21.f
#define YARD_X1 21.f
#define YARD_Z0 -54.f
#define YARD_Z1 -26.f
#define TAG_REACH 1.5f
#define MAX_RINGS 64

typedef struct {
    int round, score, left, over;
    float clock, roundLen;
    int attract;
    int nRings;
    struct { float x, z, pulse; } ring[MAX_RINGS];
} State;
static State S;

static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static float spread(float a, float b) { return a + E->frnd() * (b - a); }

/* hide everyone, then bring out as many as the round asks for, each somewhere in the yard */
static void dealRound(void) {
    int want = 3 + S.round * 2, n = 0;
    for (int i = 0; i < E->nBodies; i++) {
        E->hide[i] = 1;
        E->team[i] = 0;
        if (n >= want || E->ban[i] || !E->drawable[i]) continue;
        EngineBody *b = body(i);
        E->hide[i] = 0;
        E->team[i] = 1;
        b->x = b->tx = spread(YARD_X0 + 1, YARD_X1 - 1);
        b->z = b->tz = spread(YARD_Z0 + 1, YARD_Z1 - 1);
        b->state = 0; b->moving = 0; b->mate = -1; b->spd = 0;
        n++;
    }
    S.left = n;
    S.roundLen = S.clock = 12.f + 2.f * (float)n;
}

/* the best score so far, kept in a save of the game's own (save/tag_auto.txt): read when the game starts, written when a round's beaten it. It's not in the state, so the receipt can't see it */
static int g_best;
static void keepBest(void) {
    if (S.score <= g_best) return;
    g_best = S.score;
    E->saveBegin("auto", 1);
    E->saveSetI("best", g_best);
    E->saveSetI("round", S.round);
    E->saveEnd();
}

static void start(int onSet) {
    (void)onSet;
    *E->heroX = 0.f;
    *E->heroZ = 0.5f * (YARD_Z0 + YARD_Z1);
    S.round = 1;
    if (E->load("auto")) g_best = (int)E->loadGetI("best", 0);
    dealRound();
    E->banner("TAG THEM ALL", 3.f);
}

static void tick(float dt) {
    float hx = *E->heroX, hz = *E->heroZ;
    if (hx < YARD_X0) hx = YARD_X0;
    if (hx > YARD_X1) hx = YARD_X1;
    if (hz < YARD_Z0) hz = YARD_Z0;
    if (hz > YARD_Z1) hz = YARD_Z1;
    *E->heroX = hx;
    *E->heroZ = hz;
    E->tune->heroSpd = *E->btnA ? 16.f : 11.f;
    S.nRings = 0;
    for (int i = 0; i < E->nBodies; i++) {
        if (E->hide[i] || E->team[i] != 1) continue;
        EngineBody *b = body(i);
        if (!b->moving && E->frnd() < dt * 0.8f) {   /* a new place to wander to */
            b->tx = spread(YARD_X0 + 1, YARD_X1 - 1);
            b->tz = spread(YARD_Z0 + 1, YARD_Z1 - 1);
            b->state = 1; b->moving = 1;
        }
        if (hypotf(b->x - hx, b->z - hz) < TAG_REACH && !S.over) {   /* tagged */
            E->hide[i] = 1;
            E->team[i] = 0;
            S.left--;
            S.score += 100;
            E->burst(b->x, 1.5f, b->z, 0.4f, 1.1f, 1.4f, 10, 1.6f);
            E->banner("TAG", 0.6f);
            E->sound("pickup", 0.8f, NULL, 0);
            continue;
        }
        if (S.nRings < MAX_RINGS) {
            S.ring[S.nRings].x = b->x;
            S.ring[S.nRings].z = b->z;
            S.ring[S.nRings].pulse = 0.75f + 0.25f * sinf(*E->gameT * 4.f + (float)i);
            S.nRings++;
        }
    }
    if (!S.over) {
        S.clock -= dt;
        if (S.left <= 0) {
            S.round++;
            S.score += (int)S.clock * 10;
            keepBest();
            dealRound();
            E->banner("NEXT ROUND", 2.f);
        } else if (S.clock <= 0.f) {
            S.clock = 0.f;
            S.over = 1;
            E->run->stage = 3;   /* the engine's hero stands still on 3 */
            E->banner("OUT OF TIME", 4.f);
        }
    } else {
        static float wait = 0.f;
        wait += dt;
        if (*E->btnB || (*E->attract && wait > 4.f)) {
            wait = 0.f;
            S.over = 0;
            S.round = 1;
            S.score = 0;
            E->run->stage = 0;
            dealRound();
            E->banner("TAG THEM ALL", 3.f);
        }
    }
    S.attract = *E->attract;
}

/* the frame's marks in the world */
static void draw(const void *state) {
    const State *s = state;
    for (int i = 0; i < s->nRings; i++) {
        float p = s->ring[i].pulse;
        E->ring(s->ring[i].x, s->ring[i].z, 1.2f, 0.12f, 0.4f * p, 1.1f * p, 1.4f * p, 24);
    }
    float y = 0.25f;
    E->line(YARD_X0, y, YARD_Z0, YARD_X1, y, YARD_Z0, 1.1f, 0.9f, 0.3f);
    E->line(YARD_X0, y, YARD_Z1, YARD_X1, y, YARD_Z1, 1.1f, 0.9f, 0.3f);
    E->line(YARD_X0, y, YARD_Z0, YARD_X0, y, YARD_Z1, 1.1f, 0.9f, 0.3f);
    E->line(YARD_X1, y, YARD_Z0, YARD_X1, y, YARD_Z1, 1.1f, 0.9f, 0.3f);
}

/* the frame's text: a 35 x 24 grid inside the TV's safe box. If the player has picked bigger letters the engine fits all of this for you, and tvGrid is here for a choice you'd rather make yourself */
static void hud(const void *state) {
    const State *s = state;
    char t[48];
    snprintf(t, sizeof t, E->word("SCORE %d"), s->score);   /* E->word: this text in the player's language, if lang/<code>.txt has it. A fixed string needs nothing: the engine looks those up itself */
    E->tvText(0, 0, 1, t, 1.f, 1.f, 1.f);
    snprintf(t, sizeof t, E->word("ROUND %d"), s->round);
    E->tvTextR(0, 1, t, 1.f, 1.f, 1.f);
    float f = s->roundLen > 0.f ? s->clock / s->roundLen : 0.f;
    E->tvBar(0, 1.25f, 13, f, f > 0.3f ? 0.35f : 1.f, f > 0.3f ? 0.95f : 0.35f, 0.3f);
    snprintf(t, sizeof t, E->word("LEFT %d"), s->left);
    E->tvTextR(1.25f, 1, t, 0.55f, 0.95f, 1.f);
    if (g_best > 0) {
        snprintf(t, sizeof t, E->word("BEST %d"), g_best);
        E->tvTextR(2.5f, 1, t, 0.8f, 0.8f, 0.8f);
    }
    const char *m = E->bannerText();
    int cols;
    E->tvGrid(&cols, NULL);
    if (m) E->tvTextC(5, (int)strlen(m) * 2 <= cols ? 2 : 1, m, 1.f, 0.92f, 0.4f);
    if (s->attract) E->tvTextC(23, 1, "PRESS A TO PLAY", 1.f, 1.f, 1.f);
    else if (s->over) E->tvTextC(23, 1, "B TO PLAY AGAIN", 1.f, 1.f, 1.f);
}

/* nobody's at the pad: run at the nearest one */
static void host(float *heroHead, float dt) {
    int best = -1;
    float bestD = 1e9f, hx = *E->heroX, hz = *E->heroZ;
    for (int i = 0; i < E->nBodies; i++) {
        if (E->hide[i] || E->team[i] != 1) continue;
        float d = hypotf(body(i)->x - hx, body(i)->z - hz);
        if (d < bestD) { bestD = d; best = i; }
    }
    float speed = (best >= 0 && !S.over) ? 11.f : 0.f;
    if (best >= 0) *heroHead = atan2f(body(best)->x - hx, body(best)->z - hz);
    *E->heroSpd = speed;
    *E->heroX += sinf(*heroHead) * speed * dt;
    *E->heroZ += cosf(*heroHead) * speed * dt;
}

/* walk this body to its target. Returning 1 says the game walked it; 0 hands it to the crowd's own feet, which belong to a dance floor */
static int bodyStep(int i, float dt) {
    if (E->hide[i]) return 1;
    EngineBody *b = body(i);
    float dx = b->tx - b->x, dz = b->tz - b->z, d = hypotf(dx, dz);
    if (b->moving && d < 0.7f) { b->moving = 0; b->state = 0; }
    if (b->moving && d > 0.05f) {
        float turn = fmodf(atan2f(dx, dz) - b->h + 9.42477f, 6.28318f) - 3.14159f;
        b->h += turn * fminf(1.f, dt * 5.f);
    }
    float want = b->moving ? 1.6f : 0.f;
    b->spd += (want - b->spd) * fminf(1.f, dt * 5.f);
    b->x += sinf(b->h) * b->spd * dt;
    b->z += cosf(b->h) * b->spd * dt;
    b->rotY = b->h;
    return 1;
}

static float bodyUse(int i) { (void)i; return 0.55f; }   /* some of their team's colour over their texture */

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "TAG", .state = &S, .start = start, .tick = tick, .draw = draw, .hud = hud, .host = host, .bodyStep = bodyStep, .bodyUse = bodyUse, .stateSize = sizeof S };
    E = engine;
    /* where the lens sits, and how she moves */
    E->tune->heroSpd = 11.f; E->tune->heroSnap = 1.f; E->tune->heroTurn = 5000.f;
    E->tune->lensArm = 26.f; E->tune->lensPitch = 60.f; E->tune->lensFov = 32.f; E->tune->lensAimY = 1.7f;
    return &G;
}
