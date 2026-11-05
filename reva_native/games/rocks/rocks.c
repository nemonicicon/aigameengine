/* rocks.c -- a small 2D game: the other place to start one, beside the sample. No bodies, no packs, no floor: sprites from one sheet on the 360 x 240 canvas, at 240p. Move, shoot what's falling, and lose a ship to whatever lands. Up to four players: another pad joins by pressing fire.
 * Stick or A/D moves, A or SPACE shoots, B or E plays again. Leave it alone and it plays itself. make_assets.py makes the sheet and the sounds. */
#include "../../engine.h"

static const EngineAPI *E;

#define MAX_PLAYERS 4
#define MAX_ROCKS 32
#define MAX_SHOTS 12
#define CANVAS_W 360.f
#define CANVAS_H 240.f
#define SHIP_Y 208.f
/* the sheet's cells are 16 x 16: ship, rock, shot, spark */
#define CELL(c) ((float)(c) * 16.f)

typedef struct {
    float spawn, wait, shake;
    int sheet, fire, again, score, ships, wave, over, attract, spawned, canJoin;
    struct { float x, cool; int on; } player[MAX_PLAYERS];
    struct { float x, y, vy; int on; } rock[MAX_ROCKS];
    struct { float x, y; int on; } shot[MAX_SHOTS];
    struct { float x, y, t; } spark[MAX_ROCKS];
} State;
static State S;

/* whoever's in stays in */
static void newGame(void) {
    State was = S;
    memset(&S, 0, sizeof S);
    S.sheet = was.sheet; S.fire = was.fire; S.again = was.again; S.attract = was.attract;
    S.ships = 3; S.wave = 1; S.spawn = 1.f;
    for (int i = 0; i < MAX_PLAYERS; i++) { S.player[i].on = was.player[i].on; S.player[i].x = CANVAS_W * (float)(i + 1) / (float)(MAX_PLAYERS + 1); }
}

static void boot(void) {
    S.sheet = E->image("sheet.png");
    S.fire = E->action("fire", PAD_A);
    S.again = E->action("again", PAD_B);
    E->clear(0.02f, 0.02f, 0.06f);
    E->soundLoad("shot"); E->soundLoad("kill"); E->soundLoad("hurt"); E->soundLoad("banner");   /* loading a file mid-game is a late frame */
    S.player[0].on = 1;
    newGame();
}

static void playerTick(int i, float dt) {
    const EnginePad *pad = &E->pads[i];
    if (!pad->connected) { S.player[i].on = 0; return; }
    if (!S.player[i].on) {
        if (!E->actionPressed(i, S.fire)) { S.canJoin = 1; return; }
        S.player[i].on = 1;
    }
    float want = pad->x; int fire = E->actionHeld(i, S.fire);
    if (S.attract) {   /* nobody's touched a pad: go for the lowest rock */
        float low = -1.f, tx = S.player[i].x;
        for (int r = 0; r < MAX_ROCKS; r++) if (S.rock[r].on && S.rock[r].y > low) { low = S.rock[r].y; tx = S.rock[r].x; }
        want = (tx - S.player[i].x) * 0.1f; if (want > 1.f) want = 1.f; if (want < -1.f) want = -1.f;
        fire = low > 0.f && fabsf(tx - S.player[i].x) < 12.f;
    }
    S.player[i].x += want * 180.f * dt;
    if (S.player[i].x < 24.f) S.player[i].x = 24.f;
    if (S.player[i].x > CANVAS_W - 24.f) S.player[i].x = CANVAS_W - 24.f;
    S.player[i].cool -= dt;
    if (fire && S.player[i].cool <= 0.f) for (int k = 0; k < MAX_SHOTS; k++) if (!S.shot[k].on) {
        S.shot[k].on = 1; S.shot[k].x = S.player[i].x; S.shot[k].y = SHIP_Y - 12.f; S.player[i].cool = 0.22f;
        E->sound("shot", 0.6f, NULL, 0);
        break;
    }
}

static void tick(float dt) {
    if (S.attract && !*E->attract) {   /* someone picked up a pad: it's their game now */
        S.attract = 0;
        for (int i = 1; i < MAX_PLAYERS; i++) S.player[i].on = 0;
        newGame();
    }
    S.attract = *E->attract;
    if (S.over) {
        S.wait += dt;
        int again = S.attract && S.wait > 4.f;
        for (int i = 0; i < MAX_PLAYERS; i++) if (E->actionPressed(i, S.again)) again = 1;
        if (again) newGame();
        return;
    }
    S.canJoin = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) if (i == 0 || !S.attract) playerTick(i, dt);
    for (int i = 0; i < MAX_SHOTS; i++) if (S.shot[i].on) { S.shot[i].y -= 320.f * dt; if (S.shot[i].y < -8.f) S.shot[i].on = 0; }
    /* rocks: a wave is 6 more each time, and they come faster */
    S.spawn -= dt;
    if (S.spawn <= 0.f && S.spawned < 6 + S.wave * 6) for (int i = 0; i < MAX_ROCKS; i++) if (!S.rock[i].on) {
        S.rock[i].on = 1; S.rock[i].x = 24.f + E->frnd() * (CANVAS_W - 48.f); S.rock[i].y = -16.f; S.rock[i].vy = 40.f + 12.f * (float)S.wave + E->frnd() * 30.f;
        S.spawned++; S.spawn = 0.9f - 0.06f * (float)(S.wave > 10 ? 10 : S.wave);
        break;
    }
    int alive = 0;
    for (int i = 0; i < MAX_ROCKS; i++) {
        if (!S.rock[i].on) { if (S.spark[i].t > 0.f) S.spark[i].t -= dt; continue; }
        alive++;
        S.rock[i].y += S.rock[i].vy * dt;
        for (int k = 0; k < MAX_SHOTS; k++) if (S.shot[k].on && fabsf(S.shot[k].x - S.rock[i].x) < 11.f && fabsf(S.shot[k].y - S.rock[i].y) < 11.f) {
            S.shot[k].on = 0; S.rock[i].on = 0; S.score += 10 * S.wave;
            S.spark[i].x = S.rock[i].x; S.spark[i].y = S.rock[i].y; S.spark[i].t = 0.25f;
            E->sound("kill", 0.6f, NULL, 0);
            break;
        }
        if (S.rock[i].on && S.rock[i].y > SHIP_Y + 8.f) {   /* it landed: that's a ship */
            S.rock[i].on = 0; S.ships--; S.shake = 0.3f;
            E->sound("hurt", 0.8f, NULL, 0);
            if (S.ships <= 0) { S.over = 1; S.wait = 0.f; E->banner("GAME OVER", 6.f); }
        }
    }
    if (S.shake > 0.f) S.shake -= dt;
    if (!alive && S.spawned >= 6 + S.wave * 6) { S.wave++; S.spawned = 0; S.spawn = 1.5f; E->banner("NEXT WAVE", 1.5f); }
}

/* a cell of the sheet, centred on x, y */
static void cell(int sheet, int c, float x, float y, float r, float g, float b, float a) { E->sprite(sheet, x - 8.f, y - 8.f, 16.f, 16.f, CELL(c), 0.f, CELL(c + 1), 16.f, r, g, b, a); }

static void hud(const void *state) {
    const State *s = state;
    static const float TINT[MAX_PLAYERS][3] = {{0.6f, 0.9f, 1.f}, {1.f, 0.7f, 0.4f}, {0.6f, 1.f, 0.6f}, {1.f, 0.6f, 0.9f}};
    float dy = s->shake > 0.f ? 2.f : 0.f;
    for (int i = 0; i < 40; i++) {   /* stars: same place every frame, drifting with the clock */
        float y = (float)((i * 37) % 240) + fmodf(*E->gameT * (float)(4 + i % 5), 240.f);
        E->rect((float)((i * 97) % 360), y >= 240.f ? y - 240.f : y, 1.f, 1.f, 0.4f, 0.4f, 0.5f, 1.f);
    }
    for (int i = 0; i < MAX_ROCKS; i++) if (s->rock[i].on) cell(s->sheet, 1, s->rock[i].x, s->rock[i].y + dy, 0.9f, 0.75f, 0.6f, 1.f);
    for (int i = 0; i < MAX_SHOTS; i++) if (s->shot[i].on) cell(s->sheet, 2, s->shot[i].x, s->shot[i].y, 1.f, 1.f, 0.6f, 1.f);
    for (int i = 0; i < MAX_ROCKS; i++) if (s->spark[i].t > 0.f) cell(s->sheet, 3, s->spark[i].x, s->spark[i].y, 1.f, 0.7f, 0.3f, s->spark[i].t * 4.f);
    if (!s->over) for (int i = 0; i < MAX_PLAYERS; i++) if (s->player[i].on) cell(s->sheet, 0, s->player[i].x, SHIP_Y + dy, TINT[i][0], TINT[i][1], TINT[i][2], 1.f);
    char t[40];
    snprintf(t, sizeof t, "SCORE %d", s->score); E->tvText(0, 0, 1, t, 1.f, 1.f, 1.f);
    snprintf(t, sizeof t, "SHIPS %d", s->ships); E->tvTextR(0, 1, t, 1.f, 1.f, 1.f);
    if (s->canJoin && !s->over) E->tvTextC(0, 1, "A JOINS", 0.6f, 0.6f, 0.6f);
    const char *m = E->bannerText();
    if (m) E->tvTextC(9, 2, m, 1.f, 0.92f, 0.4f);
    /* it blinks: nothing stays up on a tube for long if it doesn't have to */
    if (s->attract) { if (fmodf(*E->gameT, 2.f) < 1.4f) E->tvTextC(12, 1, "PRESS A TO PLAY", 1.f, 1.f, 1.f); }
    else if (s->over) E->tvTextC(12, 1, "B TO PLAY AGAIN", 1.f, 1.f, 1.f);
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "ROCKS", .state = &S, .stateSize = sizeof S, .boot = boot, .tick = tick, .hud = hud, .flat = 1, .video = "240p" };
    E = engine;
    return &G;
}
