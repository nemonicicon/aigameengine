/* tower.c -- a small 3D action game, and the place to start one: a lens you can turn, on a level you can climb, for up to four players on one screen. Light the three beacons at the top of the ramp, the stairs and the bridge before the things chasing you get there. Each round has more of them, and new crates in the way. You can jump up on a crate, or punch it into them, and a crate going fast enough knocks one down. There's a lift to the first deck.
 * Stick or WASD moves, right stick turns your lens, A or SPACE jumps, X or Q punches, B or E plays again. Another pad joins by pressing A, and the screen splits. Leave it alone and it plays itself. make_assets.py makes the level, the props, the body and the sounds by code; hero.anim says which clip the first player's body plays when.
 * What it shows: E->walk and E->ray on a set that says `climb`, triggers and marks from the set, an animation graph on every body with events that land a punch and a grab (hero.anim, chaser.anim), a stop in time and a shake when one lands, four pads, split screen, objects made and taken away as it runs (E->spawn, E->place, E->paint, E->despawn, E->bodyNew), physics (E->push, GameExport.objectHit, E->bodyLimp), and a menu when it's over (E->uiBegin). */
#include "../../engine.h"

static const EngineAPI *E;

#define MAX_BEACONS 3
#define MAX_CRATES 4
#define MAX_PLAYERS 4
#define PUNCH_REACH 3.0f
#define GRAB_START 2.2f
#define GRAB_REACH 2.6f
#define ROUTE_MAX 8
#define GRAVITY 17.2f

typedef struct {
    int jump, punch, again;
    float stop, clock, wait;
    int round, score, left, over, attract, lit[MAX_BEACONS], route, nViews;
    float bx[MAX_BEACONS], by[MAX_BEACONS], bz[MAX_BEACONS];
    /* what it makes as it runs: handles, which stay safe to use after what they name has gone */
    int crateProp, beaconProp, liftProp, crate[MAX_CRATES], beacon[MAX_BEACONS], lift;
    float liftX, liftZ, liftT;
    float down[64];   /* how long a body that's been knocked down lies there */
    /* what the menu when it's over changes */
    EngineUi menu;
    int chasing, noShake;
    float music;
    /* player 0 is the engine's hero. The others are the last bodies in the pool, walked here */
    struct { int on; float yaw, vy, cool; } p[MAX_PLAYERS];
} State;
static State S;

static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static int playerBody(int k) { return E->nBodies - k; }
static int isPlayer(int i) { for (int k = 1; k < MAX_PLAYERS; k++) if (S.p[k].on && playerBody(k) == i) return 1; return 0; }
static void playerAt(int k, float *x, float *y, float *z) {
    if (k == 0) { *x = *E->heroX; *y = *E->heroFeet; *z = *E->heroZ; return; }
    int i = playerBody(k);
    *x = body(i)->x; *y = E->feet[i]; *z = body(i)->z;
}

static void dealRound(void) {
    int want = 2 + S.round + (S.chasing - 1) * 2;
    float sx = 0.f, sz = 0.f;
    E->mark("spawn", &sx, NULL, &sz);
    /* everyone back in the pool but the players, whose last few bodies are kept for them by their team */
    for (int i = 0; i < E->nBodies; i++) if (!isPlayer(i)) { E->bodyGone(i); if (i >= playerBody(MAX_PLAYERS - 1)) E->team[i] = -1; }
    for (int n = 0; n < want; n++) {
        int i = E->bodyNew(sx + (float)(n % 4) * 3.f, sz + (float)(n / 4) * 3.f, 1);
        if (i < 0) break;
        body(i)->state = 1; body(i)->moving = 1;
        E->bodyGraph(i, "chaser.anim");
    }
    /* new crates where the set says, whatever happened to the last ones. Each round's stand in the way again */
    char name[16];
    for (int k = 0; k < MAX_CRATES; k++) {
        float x, y, z;
        E->despawn(S.crate[k]);
        snprintf(name, sizeof name, "crate%d", k);
        S.crate[k] = E->mark(name, &x, &y, &z) ? E->spawn(S.crateProp, x, y, z, 0.f, 1.f, OBJ_SOLID | OBJ_LOOSE | OBJ_RESTING) : 0;
    }
    S.left = 0;
    memset(S.down, 0, sizeof S.down);
    for (int k = 0; k < MAX_BEACONS; k++) {
        S.lit[k] = 0; S.left++;
        E->paint(S.beacon[k], 0.25f, 0.35f, 1.f);
    }
    S.clock = 60.f;
}

static void newGame(void) {
    float x = 0.f, z = 12.f;
    S.round = 1; S.score = 0; S.over = 0; S.wait = 0.f; S.route = 0;
    E->bodyRise(-1);
    for (int k = 1; k < MAX_PLAYERS; k++) E->bodyRise(playerBody(k));
    E->musicGain(S.music, 1.f);
    E->run->stage = 0;
    E->mark("start", &x, NULL, &z);
    *E->heroX = x; *E->heroZ = z;
    dealRound();
}

static void boot(void) {
    S.jump = E->action("jump", PAD_A);
    S.punch = E->action("punch", PAD_X);
    S.again = E->action("again", PAD_B);
    E->animGraph("hero.anim");
    S.crateProp = E->prop("tw_crate"); S.beaconProp = E->prop("tw_beacon"); S.liftProp = E->prop("tw_lift");
    E->soundLoad("hit"); E->soundLoad("ping"); E->soundLoad("step"); E->soundLoad("banner");
    S.nViews = 1;
    S.p[0].on = 1;
    S.chasing = 1; S.music = 1.f;
    E->uiSounds("step", "ping", "step");
}

/* each player's lens sits on an arm behind them and turns with their right stick. If the arm goes through anything, it's pulled in short of it */
static void lens(int k, int view, float dt) {
    float hx, hy, hz;
    playerAt(k, &hx, &hy, &hz);
    hy += 2.4f;
    if (!S.attract) S.p[k].yaw += E->pads[k].rx * dt * 2.4f;
    else S.p[k].yaw += dt * 0.12f;
    float arm = 15.f, up = 0.42f, yaw = S.p[k].yaw;
    float from[3] = {hx, hy, hz};
    float to[3] = {hx + sinf(yaw) * cosf(up) * arm, hy + sinf(up) * arm, hz + cosf(yaw) * cosf(up) * arm};
    float t = 1.f;
    if (E->ray(from, to, &t)) { t = t * 0.92f; for (int j = 0; j < 3; j++) to[j] = from[j] + (to[j] - from[j]) * t; }
    E->viewLens(view, to[0], to[1], to[2], hx, hy, hz);
}

/* everyone in gets a picture */
static void lenses(float dt) {
    int view = 0;
    for (int k = 0; k < MAX_PLAYERS; k++) if (S.p[k].on) lens(k, view++, dt);
    if (view != S.nViews) { S.nViews = view; E->views(view); }
    E->tune->stickYaw = S.p[0].yaw;
}

static void start(int onSet) {
    (void)onSet;
    char name[16];
    for (int k = 0; k < MAX_BEACONS; k++) {
        snprintf(name, sizeof name, "beacon%d", k);
        E->mark(name, &S.bx[k], &S.by[k], &S.bz[k]);
        S.beacon[k] = E->spawn(S.beaconProp, S.bx[k], S.by[k], S.bz[k], 0.f, 1.f, 0);
    }
    E->mark("lift", &S.liftX, NULL, &S.liftZ);
    S.lift = E->spawn(S.liftProp, S.liftX, -0.35f, S.liftZ, 0.f, 1.f, OBJ_SOLID);
    E->tune->heroSnap = 1.f; E->tune->heroTurn = 900.f;
    E->tune->lensFov = 46.f;
    newGame();
    lenses(0.f);   /* before the first frame, so the picture opens on it rather than cutting to it */
    E->banner("LIGHT THE BEACONS", 3.f);
    E->music("music.wav", 0.5f, 1);
}

/* one of the things chasing them goes down, away from what hit it, and is gone a moment later */
static void knockDown(int i, float fromX, float fromZ, float force) {
    EngineBody *b = body(i);
    float dx = b->x - fromX, dz = b->z - fromZ, d = hypotf(dx, dz);
    if (d < 0.01f) { dx = 1.f; d = 1.f; }
    E->team[i] = 0;
    E->particles(b->x, E->feet[i] + 1.5f, b->z, 40, 7.f, 0.6f, 0.22f, 1.f, 0.55f, 0.25f, 9.f);
    E->decal(b->x, E->feet[i], b->z, 1.3f, 0.03f, 0.02f, 0.03f, 0.75f, 15.f);
    if (i < 64 && E->bodyLimp(i, dx / d * force, 1.5f, dz / d * force)) S.down[i] = 3.f; else E->bodyGone(i);
}

/* a punch: whoever's in reach of this player goes, and any crate in reach flies. One someone took away has a dead handle, which object answers NULL for */
static void punchFrom(float x, float y, float z) {
    int hits = 0;
    for (int i = 0; i < E->nBodies; i++) if (!E->hide[i] && E->team[i] == 1) {
        EngineBody *b = body(i);
        if (hypotf(b->x - x, b->z - z) < PUNCH_REACH && fabsf(E->feet[i] - y) < 2.f) { knockDown(i, x, z, 5.f); hits++; }
    }
    for (int k = 0; k < MAX_CRATES; k++) {
        const EngineObject *c = E->object(S.crate[k]);
        if (c && hypotf(c->x - x, c->z - z) < PUNCH_REACH && c->top > y && c->y < y + 3.f) {
            float dx = c->x - x, dz = c->z - z, d = hypotf(dx, dz);
            if (d < 0.01f) { dx = 1.f; d = 1.f; }
            E->push(S.crate[k], dx / d * 11.f, 5.f, dz / d * 11.f);
            hits++;
        }
    }
    if (hits) {
        S.score += 10 * hits;
        E->sound("hit", 0.9f, NULL, 0);
        E->timeScale(0.2f); S.stop = 0.03f;
        if (!S.noShake) E->shake(0.25f, 0.25f);
    }
}

/* a pad that isn't in yet joins by pressing A. One that's pulled leaves its body behind for a moment, and then it's gone */
static void joins(void) {
    for (int k = 1; k < MAX_PLAYERS; k++) {
        int i = playerBody(k);
        if (i < 0) continue;
        if (S.p[k].on && !E->pads[k].connected) { S.p[k].on = 0; E->bodyGone(i); E->team[i] = -1; E->bodyGraph(i, NULL); continue; }
        if (!S.p[k].on && E->pads[k].connected && E->actionPressed(k, S.jump)) {
            EngineBody *b = body(i);
            S.p[k].on = 1; S.p[k].yaw = S.p[0].yaw; S.p[k].vy = 0.f;
            E->hide[i] = 0; E->team[i] = 2;
            b->x = b->tx = *E->heroX + 2.f * (float)k; b->z = b->tz = *E->heroZ + 2.f; b->state = 1; b->moving = 0; b->mate = -1; b->spd = 0.f;
            E->feet[i] = *E->heroFeet;
            E->bodyGraph(i, "hero.anim");
            E->banner("PLAYER IN", 1.5f);
        }
    }
}

static void tick(float dt) {
    S.attract = *E->attract;
    /* a stop in time when a punch lands. dt is slowed down with everything else, so a little of it lasts a while */
    if (S.stop > 0.f) { S.stop -= dt; if (S.stop <= 0.f) E->timeScale(1.f); }
    joins();
    lenses(dt);
    /* the lift keeps going, up to the first deck and back. What's standing on it goes with it */
    S.liftT += dt;
    for (int i = 0; i < 64 && i < E->nBodies; i++) if (S.down[i] > 0.f) { S.down[i] -= dt; if (S.down[i] <= 0.f) E->bodyGone(i); }
    E->place(S.lift, S.liftX, -0.35f + (2.95f) * 0.5f * (1.f - cosf(S.liftT * 0.8f)), S.liftZ, 0.f);
    if (S.over) {
        S.wait += dt;
        int again = S.attract && S.wait > 4.f;
        for (int k = 0; k < MAX_PLAYERS; k++) if (S.p[k].on && E->actionPressed(k, S.again)) again = 1;
        /* a game's settings are a menu in its state: said here, every tick, and it draws itself */
        if (!S.attract) {
            static const char *const HOW_MANY[] = {"FEW", "SOME", "MANY"};
            E->uiBegin(&S.menu, -1, 6.f, 9.f, 23.f, 10.f);
            if (E->uiButton("PLAY AGAIN")) again = 1;
            E->uiChoice("CHASING YOU", HOW_MANY, 3, &S.chasing);
            if (E->uiSlider("MUSIC", &S.music, 0.f, 1.f, 0.1f)) E->musicGain(S.music * 0.25f, 0.1f);
            int shake = !S.noShake;
            if (E->uiToggle("SHAKE", &shake)) S.noShake = !shake;
            E->uiEnd();
        }
        if (again) newGame();
        return;
    }
    E->tune->heroSpd = E->animIn("punch") ? 2.f : 11.f;
    if (!S.attract) {
        if (E->actionPressed(0, S.jump) && E->heroJump(9.5f)) E->animSet("jump", 1.f);
        if (E->actionPressed(0, S.punch)) E->animSet("punch", 1.f);
    }
    /* anyone can light a beacon, and anyone can be caught */
    char name[16];
    for (int k = 0; k < MAX_PLAYERS; k++) if (S.p[k].on) {
        float x, y, z;
        playerAt(k, &x, &y, &z);
        for (int b = 0; b < MAX_BEACONS; b++) {
            snprintf(name, sizeof name, "beacon%d", b);
            if (!S.lit[b] && E->inTrigger(name, x, y + 1.f, z)) {
                S.lit[b] = 1; S.left--; S.score += 50;
                E->paint(S.beacon[b], 1.f, 0.75f, 0.2f);
                E->sound("ping", 0.8f, NULL, 0);
                E->banner("BEACON LIT", 1.5f);
                E->particles(S.bx[b], S.by[b] + 1.f, S.bz[b], 60, 6.f, 1.2f, 0.18f, 1.f, 0.9f, 0.4f, 3.f);
            }
        }
        /* a chaser in reach goes for them, and the graph says when its hands arrive: clipEvent */
        for (int i = 0; i < E->nBodies; i++) if (!E->hide[i] && E->team[i] == 1 && !E->bodyAnimIn(i, "grab")) {
            EngineBody *o = body(i);
            if (hypotf(o->x - x, o->z - z) < GRAB_START && fabsf(E->feet[i] - y) < 2.f) E->bodyAnimSet(i, "near", 1.f);
        }
    }
    S.clock -= dt;
    if (S.left <= 0) { S.round++; S.score += (int)S.clock * 5; dealRound(); E->banner("NEXT ROUND", 2.f); }
    else if (S.clock <= 0.f) { S.clock = 0.f; S.over = 1; S.wait = 0.f; E->run->stage = 3; E->banner("OUT OF TIME", 4.f); }
}

/* a crate hit something. Going fast enough, it takes one of the things chasing them down with it */
static void objectHit(int obj, int other, int who, float speed) {
    (void)other;
    const EngineObject *c = E->object(obj);
    if (c && speed > 2.f) { float at[3] = {c->x, c->y, c->z}; E->sound("step", speed > 8.f ? 0.8f : speed * 0.1f, at, 0); }
    if (c && who >= 0 && E->team[who] == 1 && speed > 4.f) { knockDown(who, c->x, c->z, speed * 0.5f); S.score += 10; }
}

/* the graph says when a fist or a pair of hands arrives. who is a body, or -1 for the first player */
static void clipEvent(int who, const char *name) {
    float x = who < 0 ? *E->heroX : body(who)->x, y = who < 0 ? *E->heroFeet : E->feet[who], z = who < 0 ? *E->heroZ : body(who)->z, at[3] = {x, y, z};
    if (!strcmp(name, "step")) { E->sound("step", 0.3f, at, 0); return; }
    if (!strcmp(name, "hit")) { punchFrom(x, y, z); return; }
    if (!strcmp(name, "grab") && !S.over) for (int k = 0; k < MAX_PLAYERS; k++) if (S.p[k].on) {
        float px, py, pz;
        playerAt(k, &px, &py, &pz);
        if (hypotf(px - x, pz - z) < GRAB_REACH && fabsf(py - y) < 2.f) {
            S.over = 1; S.wait = 0.f; E->run->stage = 3;
            E->bodyLimp(k ? playerBody(k) : -1, (px - x) * 2.f, 2.f, (pz - z) * 2.f);   /* whoever's caught goes down */
            E->musicGain(0.25f, 0.5f);   /* it comes down when it's over, and back up with the next game */
            E->banner("CAUGHT", 4.f);
            E->sound("hit", 0.8f, NULL, 0);
            return;
        }
    }
}

/* another player's body: their stick, relative to their own lens, on the same surfaces */
static void playerStep(int k, int i, float dt) {
    EngineBody *b = body(i);
    const EnginePad *pad = &E->pads[k];
    float yaw = S.p[k].yaw, sx = S.over ? 0.f : pad->x, sy = S.over ? 0.f : pad->y;
    float vx = cosf(yaw) * sx - sinf(yaw) * sy, vz = -sinf(yaw) * sx - cosf(yaw) * sy, mag = hypotf(vx, vz), top = E->bodyAnimIn(i, "punch") ? 2.f : 11.f;
    if (mag > 0.01f) b->h = atan2f(vx, vz);
    b->spd = mag * top;
    float x = b->x + vx * top * dt, z = b->z + vz * top * dt;
    float y = E->walk(b->x, b->z, &x, &z, E->feet[i], 0.42f);
    b->x = x; b->z = z; b->tx = x; b->tz = z; b->rotY = b->h; b->moving = mag > 0.1f; b->state = b->moving;
    if (!S.over && E->actionPressed(k, S.punch)) E->bodyAnimSet(i, "punch", 1.f);
    if (!S.over && E->actionPressed(k, S.jump) && S.p[k].vy == 0.f && E->feet[i] <= y + 0.05f) { S.p[k].vy = 9.5f; E->bodyAnimSet(i, "jump", 1.f); }
    E->bodyAnimSet(i, "grounded", (S.p[k].vy != 0.f || E->feet[i] > y + 0.4f) ? 0.f : 1.f);
    if (S.p[k].vy != 0.f || E->feet[i] > y + 0.4f) {   /* in the air */
        S.p[k].vy -= GRAVITY * dt;
        E->feet[i] += S.p[k].vy * dt;
        if (S.p[k].vy < 0.f && E->feet[i] <= y) { E->feet[i] = y; S.p[k].vy = 0.f; }
    } else E->feet[i] += (y - E->feet[i]) * fminf(1.f, dt * 16.f);
}

/* the things chasing them use the same surfaces, and go for whoever's nearest */
static int bodyStep(int i, float dt) {
    if (E->hide[i] || E->bodyLimpState(i)) return 1;
    for (int k = 1; k < MAX_PLAYERS; k++) if (S.p[k].on && playerBody(k) == i) { playerStep(k, i, dt); return 1; }
    EngineBody *b = body(i);
    float best = 1e9f, tx = b->x, tz = b->z;
    for (int k = 0; k < MAX_PLAYERS; k++) if (S.p[k].on) {
        float x, y, z;
        playerAt(k, &x, &y, &z);
        float d = hypotf(x - b->x, z - b->z);
        if (d < best) { best = d; tx = x; tz = z; }
    }
    float dx = tx - b->x, dz = tz - b->z, d = hypotf(dx, dz);
    if (d > 0.05f) {
        float turn = fmodf(atan2f(dx, dz) - b->h + 9.42477f, 6.28318f) - 3.14159f;
        b->h += turn * fminf(1.f, dt * 5.f);
    }
    float want = (S.over || E->bodyAnimIn(i, "grab")) ? 0.f : 3.2f + 0.3f * (float)S.round;
    b->spd += (want - b->spd) * fminf(1.f, dt * 5.f);
    float x = b->x + sinf(b->h) * b->spd * dt, z = b->z + cosf(b->h) * b->spd * dt;
    int nb[64], nn = E->nearby(x, z, 1.6f, nb, 64);   /* they don't walk through each other */
    for (int n = 0; n < nn; n++) if (nb[n] != i && !E->hide[nb[n]]) {
        const EngineBody *o = body(nb[n]);
        float ax = x - o->x, az = z - o->z, a = hypotf(ax, az);
        if (a > 0.01f && a < 1.6f) { x += ax / a * (1.6f - a) * dt * 4.f; z += az / a * (1.6f - a) * dt * 4.f; }
    }
    float y = E->walk(b->x, b->z, &x, &z, E->feet[i], 0.42f);
    b->x = x; b->z = z; b->tx = x; b->tz = z; b->rotY = b->h; b->moving = b->spd > 0.2f;
    E->feet[i] += (y - E->feet[i]) * fminf(1.f, dt * 12.f);
    return 1;
}

/* nobody's at the pad: walk the route the set marks, which goes past all three beacons, and swing now and then */
static void host(float *heroHead, float dt) {
    char name[16]; float tx, tz;
    if (S.over) { *E->heroSpd = 0.f; return; }   /* she's down, and the lens stays with her */
    snprintf(name, sizeof name, "route%d", S.route);
    if (!E->mark(name, &tx, NULL, &tz)) { S.route = 0; return; }
    float dx = tx - *E->heroX, dz = tz - *E->heroZ;
    if (hypotf(dx, dz) < 1.5f) { S.route = (S.route + 1) % ROUTE_MAX; return; }
    *heroHead = atan2f(dx, dz);
    *E->heroSpd = 8.f;
    *E->heroX += sinf(*heroHead) * 8.f * dt;
    *E->heroZ += cosf(*heroHead) * 8.f * dt;
    if (fmodf(*E->gameT, 2.5f) < dt) E->animSet("punch", 1.f);
}

static float bodyUse(int i) { return E->team[i] == 2 ? 0.75f : 0.35f; }
static const char *street(void) { return "tower"; }
static float tod(void) { return 17.5f; }   /* late in the day: noon's sky is nearly white, which is hard on a tube and on text */

static void draw(const void *state) {
    const State *s = state;
    for (int k = 0; k < MAX_BEACONS; k++) {
        float r = s->lit[k] ? 1.2f : 0.4f, g = s->lit[k] ? 0.9f : 0.5f, b = s->lit[k] ? 0.3f : 1.3f;
        E->ring(s->bx[k], s->bz[k], 1.4f, s->by[k] + 0.15f, r, g, b, 24);
    }
}

static void hud(const void *state) {
    const State *s = state;
    char t[48];
    int view = 0;
    for (int k = 0; k < MAX_PLAYERS; k++) if (s->p[k].on) {
    E->tvView(view++);   /* everyone in gets their own, in their own picture */
    snprintf(t, sizeof t, "SCORE %d", s->score); E->tvText(0, 0, 1, t, 1.f, 1.f, 1.f);
    snprintf(t, sizeof t, "ROUND %d", s->round); E->tvTextR(0, 1, t, 1.f, 1.f, 1.f);
    E->tvBar(0, 1.25f, 13, s->clock / 60.f, s->clock > 15.f ? 0.35f : 1.f, s->clock > 15.f ? 0.95f : 0.35f, 0.3f);
    snprintf(t, sizeof t, "BEACONS %d", s->left); E->tvTextR(1.25f, 1, t, 0.55f, 0.95f, 1.f);
    const char *m = E->bannerText();
    if (m) E->tvTextC(5, (int)strlen(m) * 2 <= TV_COLS ? 2 : 1, m, 1.f, 0.92f, 0.4f);
    if (s->attract) { if (fmodf(*E->gameT, 2.f) < 1.4f) E->tvTextC(21, 1, "PRESS A TO PLAY", 1.f, 1.f, 1.f); }
    }
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "TOWER", .state = &S, .stateSize = sizeof S, .boot = boot, .street = street, .tod = tod, .start = start, .tick = tick, .draw = draw, .hud = hud, .host = host, .bodyStep = bodyStep, .bodyUse = bodyUse, .clipEvent = clipEvent, .objectHit = objectHit };
    E = engine;
    return &G;
}
