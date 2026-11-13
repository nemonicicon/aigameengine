/* rally.c -- bats and a ball, for one to four players: THE PLACE TO START AN ONLINE GAME (queue step 29). No bodies, no packs, no files: rectangles on the 360 x 240 canvas.
 * A bat on each wall that has a player, a wall where there's none, and a ball. Miss it and you lose a life; the last bat with lives wins. Player 1 is the left wall and
 * player 2 the right, on the stick's up and down; players 3 and 4 are the top and the bottom, on its left and right. Alone, the right bat is the game's.
 *
 * WHAT IT SHOWS, for a game that wants to be played across machines:
 *   - Nothing in tick knows whether the game is online: it never asks. E->pads is the session's pads on every machine, exactly as four pads on one machine would be,
 *     so the rules below are a couch game's rules and they are all there is. That's also why a session recorded with REVA_RECORD replays with no wire at all.
 *   - What differs between machines is only ever drawn. hud asks E->netHere(i) to mark the bats that are this machine's own. ⛔ Never ask it in tick.
 *   - Everything the rules depend on is in S, which the engine hashes every tick and holds against the other machines': if two machines ever part, they're told the tick.
 *   - The longest rally is read from a save in boot. On one machine it's that machine's; in a session it's the host's on every machine, which the engine sees to.
 *   - HOST and JOIN on its menu are E->netHost and E->netJoin: the engine starts again in a session, and waits for the players on a screen of its own.
 * Stick or W and S moves, A or SPACE starts, X or Q is the online menu, B or E goes back. Leave it alone and it plays itself. */
#include "../../engine.h"

static const EngineAPI *E;

#define MAX_PLAYERS 4
/* the court, inside the middle four fifths of the canvas: the part every TV shows, with a row of text above it and two below */
#define X0 40.f
#define X1 320.f
#define Y0 38.f
#define Y1 196.f
#define BAT 36.f        /* a bat's length */
#define THICK 5.f
#define BALL 3.f        /* half the ball's side */
#define LIVES 5

typedef struct {
    int screen;                                     /* 0 the title, playing itself; 1 the online menu; 2 a game; 3 someone's won */
    int go, online, back;                           /* actions */
    int attract, winner, hits, best, bestAtBoot;
    float wait, bx, by, vx, vy, speed;
    struct { int on, human, lives; float at; } p[MAX_PLAYERS];   /* at: the bat's middle, along its wall */
    EngineUi menu; float menuPads; char address[40];
} State;
static State S;

static int vertical(int i) { return i < 2; }        /* the left and right walls' bats go up and down */
static void serve(void) {
    S.bx = (X0 + X1) * 0.5f; S.by = (Y0 + Y1) * 0.5f; S.speed = 150.f; S.hits = 0;
    /* towards the left or the right, and never straight along a wall. The dice are the engine's, so every machine in a session serves the same way */
    const float side = E->frnd() < 0.5f ? -1.f : 1.f, slope = (E->frnd() - 0.5f) * 1.4f;
    const float len = sqrtf(1.f + slope * slope);
    S.vx = side * S.speed / len; S.vy = slope * S.speed / len;
}
/* a new game: a bat for every pad that's plugged in. The right wall's is the game's if nobody has it, so one player has someone to play; the top and bottom are walls */
static void newGame(int demo) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const int plugged = !demo && E->pads[i].connected;
        S.p[i].human = plugged; S.p[i].on = plugged || i < 2; S.p[i].lives = LIVES;
        S.p[i].at = vertical(i) ? (Y0 + Y1) * 0.5f : (X0 + X1) * 0.5f;
    }
    S.screen = demo ? 0 : 2; S.winner = -1; S.wait = 0.f;
    serve();
}

static void boot(void) {
    S.go = E->action("go", PAD_A);
    S.online = E->action("online", PAD_X);
    S.back = E->action("back", PAD_B);
    E->clear(0.02f, 0.03f, 0.05f);
    if (E->load("best")) S.best = (int)E->loadGetI("rally", 0);
    if (S.best < 0 || S.best > 100000) S.best = 0;   /* a save is a text file anyone can edit */
    S.bestAtBoot = S.best; S.menuPads = 2.f;
    newGame(1);
}

static void batTick(int i, float dt) {
    if (!S.p[i].on || S.p[i].lives <= 0) return;
    const float lo = (vertical(i) ? Y0 : X0) + BAT * 0.5f, hi = (vertical(i) ? Y1 : X1) - BAT * 0.5f;
    float want;
    if (S.p[i].human) want = vertical(i) ? -E->pads[i].y : E->pads[i].x;
    else {   /* the game's own bat: after the ball, and no faster than a hand */
        const float to = (vertical(i) ? S.by : S.bx) - S.p[i].at;
        want = to * 0.08f; if (want > 0.75f) want = 0.75f; if (want < -0.75f) want = -0.75f;
    }
    S.p[i].at += want * 170.f * dt;
    if (S.p[i].at < lo) S.p[i].at = lo;
    if (S.p[i].at > hi) S.p[i].at = hi;
}
/* the ball's at wall i: a bat sends it back with a push from where it was struck, a wall just sends it back, and a bat that isn't there loses a life */
static void wallTick(int i) {
    const int vert = vertical(i), low = (i == 0 || i == 2);
    float *pos = vert ? &S.bx : &S.by, *vel = vert ? &S.vx : &S.vy, *across = vert ? &S.vy : &S.vx;
    const float wall = vert ? (low ? X0 : X1) : (low ? Y0 : Y1), along = vert ? S.by : S.bx;
    const int coming = low ? *vel < 0.f : *vel > 0.f; if (!coming) return;
    const int live = S.p[i].on && S.p[i].lives > 0;
    const float face = live ? (low ? wall + THICK : wall - THICK) : wall;
    const float edge = low ? *pos - BALL : *pos + BALL;
    if (low ? edge > face : edge < face) return;
    if (live && fabsf(along - S.p[i].at) > BAT * 0.5f + BALL) {   /* past the bat */
        if (low ? *pos > wall - 10.f : *pos < wall + 10.f) return;   /* not out yet */
        S.p[i].lives--; if (S.hits > S.best) S.best = S.hits;
        serve(); return;
    }
    *vel = -*vel; *pos = low ? face + BALL : face - BALL;
    if (live) { *across += (along - S.p[i].at) / (BAT * 0.5f) * 70.f; S.hits++; if (S.speed < 330.f) S.speed += 8.f; }
    const float len = sqrtf(S.vx * S.vx + S.vy * S.vy);
    if (len > 1e-3f) { S.vx *= S.speed / len; S.vy *= S.speed / len; }
    /* never so flat that it creeps along a wall */
    if (fabsf(*vel) < S.speed * 0.25f) { *vel = (*vel < 0.f ? -1.f : 1.f) * S.speed * 0.25f; const float l2 = sqrtf(S.vx * S.vx + S.vy * S.vy); S.vx *= S.speed / l2; S.vy *= S.speed / l2; }
}

/* the online menu. It's in tick, like any menu, and what it does is ask the engine to start again in a session */
static void menuTick(void) {
    static const char *const none = "";
    E->uiBegin(&S.menu, -1, 6.f, 6.f, 23.f, 14.f);
    E->uiLabel("PLAY ACROSS MACHINES");
    E->uiSlider("PADS IN THE GAME", &S.menuPads, 2.f, 4.f, 1.f);
    if (E->uiButton("HOST A GAME")) E->netHost(0, (int)(S.menuPads + 0.5f), NULL);   /* these two end the run: the engine comes back up in a session, or says why not (E->netWhy) */
    E->uiGap(1.f);
    E->uiText("THE HOST'S ADDRESS", S.address, (int)sizeof S.address);
    if (E->uiButton("JOIN THAT GAME") && S.address[0]) E->netJoin(S.address, NULL);
    E->uiGap(1.f);
    if (E->uiButton("BACK") || E->uiBack()) S.screen = 0;
    E->uiEnd();
    (void)none;
}

static void tick(float dt) {
    S.attract = *E->attract;
    int go = 0, back = 0, menu = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) { if (E->actionPressed(i, S.go)) go = 1; if (E->actionPressed(i, S.back)) back = 1; }
    if (E->actionPressed(0, S.online)) menu = 1;
    if (S.screen == 1) { menuTick(); return; }
    if (S.screen == 0) {   /* the title: the game plays itself behind it */
        if (go) { newGame(0); return; }
        if (menu) { S.screen = 1; memset(&S.menu, 0, sizeof S.menu); return; }
    }
    if (S.screen == 3) {
        S.wait += dt;
        if (go || (S.attract && S.wait > 4.f)) { newGame(S.attract); return; }
        if (back) { E->netLeave(); newGame(1); return; }   /* in a session, B is also goodbye: the engine starts again alone. Out of one, netLeave does nothing */
        return;
    }
    /* a pad that's pulled, or a machine that's left: its bat is the game's on the left and right, and a wall on the top and bottom */
    if (S.screen == 2) for (int i = 0; i < MAX_PLAYERS; i++) if (S.p[i].human && !E->pads[i].connected) { S.p[i].human = 0; if (i >= 2) S.p[i].on = 0; }
    for (int i = 0; i < MAX_PLAYERS; i++) batTick(i, dt);
    S.bx += S.vx * dt; S.by += S.vy * dt;
    for (int i = 0; i < MAX_PLAYERS; i++) wallTick(i);
    if (S.screen == 2) {
        int alive = 0, last = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) if (S.p[i].on && S.p[i].lives > 0) { alive++; last = i; }
        if (alive <= 1) {
            S.screen = 3; S.winner = last; S.wait = 0.f;
            if (S.best > S.bestAtBoot) { S.bestAtBoot = S.best; E->saveBegin("best", 1); E->saveSetI("rally", S.best); E->saveEnd(); }
        }
    } else for (int i = 0; i < MAX_PLAYERS; i++) if (S.p[i].lives <= 0) S.p[i].lives = LIVES;   /* the title's game never ends */
}

static void hud(const void *state) {
    const State *s = state;
    static const float TINT[MAX_PLAYERS][3] = {{0.55f, 0.85f, 1.f}, {1.f, 0.65f, 0.4f}, {0.6f, 1.f, 0.6f}, {1.f, 0.6f, 0.9f}};
    char t[48];
    /* the court: a wall where there's no bat, and a line down the middle */
    for (int i = 0; i < MAX_PLAYERS; i++) {
        const int live = s->p[i].on && s->p[i].lives > 0, vert = vertical(i), low = (i == 0 || i == 2);
        const float wall = vert ? (low ? X0 : X1) : (low ? Y0 : Y1);
        if (!live) { if (vert) E->rect(low ? wall - 2.f : wall, Y0, 2.f, Y1 - Y0, 0.3f, 0.33f, 0.4f, 1.f); else E->rect(X0, low ? wall - 2.f : wall, X1 - X0, 2.f, 0.3f, 0.33f, 0.4f, 1.f); continue; }
        /* ⛔ which bats are this machine's is a thing to DRAW: a brighter bat here, and never a rule */
        const float k = E->netHere(i) ? 1.f : 0.6f;
        if (vert) E->rect(low ? wall : wall - THICK, s->p[i].at - BAT * 0.5f, THICK, BAT, TINT[i][0] * k, TINT[i][1] * k, TINT[i][2] * k, 1.f);
        else E->rect(s->p[i].at - BAT * 0.5f, low ? wall : wall - THICK, BAT, THICK, TINT[i][0] * k, TINT[i][1] * k, TINT[i][2] * k, 1.f);
    }
    for (float y = Y0; y < Y1; y += 12.f) E->rect((X0 + X1) * 0.5f - 0.5f, y, 1.f, 6.f, 0.25f, 0.27f, 0.33f, 1.f);
    if (s->screen != 1) E->rect(s->bx - BALL, s->by - BALL, BALL * 2.f, BALL * 2.f, 1.f, 1.f, 0.85f, 1.f);
    if (s->screen == 1) return;   /* the menu draws itself */
    if (s->screen == 2 || s->screen == 3) {
        for (int i = 0; i < MAX_PLAYERS; i++) if (s->p[i].on) {
            snprintf(t, sizeof t, s->p[i].human ? "P%d %d" : "-- %d", i + 1, s->p[i].lives > 0 ? s->p[i].lives : 0);
            E->tvText((float)(1 + i * 9), 0, 1, t, TINT[i][0], TINT[i][1], TINT[i][2]);
        }
        snprintf(t, sizeof t, E->word("RALLY %d  BEST %d"), s->hits, s->best); E->tvTextC(23, 1, t, 0.7f, 0.7f, 0.7f);
    }
    if (s->screen == 0) {
        E->tvTextC(7, 2, "RALLY", 1.f, 0.92f, 0.4f);
        if (fmodf(*E->gameT, 2.f) < 1.4f) E->tvTextC(13, 1, "A PLAY     X ONLINE", 1.f, 1.f, 1.f);
        if (s->best) { snprintf(t, sizeof t, E->word("BEST RALLY %d"), s->best); E->tvTextC(16, 1, t, 0.7f, 0.7f, 0.7f); }
        const char *why = E->netWhy(); if (why[0]) { snprintf(t, sizeof t, "%.34s", why); E->tvTextC(20, 1, t, 0.9f, 0.5f, 0.4f); }
    }
    if (s->screen == 3) {
        if (s->winner >= 0) { snprintf(t, sizeof t, s->p[s->winner].human ? E->word("PLAYER %d WINS") : E->word("THE GAME WINS"), s->winner + 1); E->tvTextC(9, 2, t, 1.f, 0.92f, 0.4f); }
        E->tvTextC(13, 1, "A AGAIN     B STOP", 1.f, 1.f, 1.f);
    }
    if (E->netState() == 2) { snprintf(t, sizeof t, E->word("ONLINE  %d PADS  %d TICKS"), E->netSeats(), E->netLag()); E->tvTextC(22, 1, t, 0.45f, 0.6f, 0.5f); }
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "RALLY", .state = &S, .stateSize = sizeof S, .boot = boot, .tick = tick, .hud = hud, .flat = 1 };
    E = engine;
    return &G;
}
