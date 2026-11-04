/* talk.c -- two people and a conversation, and the place to start a game whose people talk. Everything in it is made by code (make_assets.py). It includes engine.h and nothing else of the engine's.
 * What it shows: a conversation from a text file (talk/hello.talk) with its lens, its words and a question put to the player, and a face that does its own thing when nobody's talking: she follows you with her eyes and her head, and blinks.
 * Stick walks, A starts talking when you're close, and A cuts a line short. */
#include "../../engine.h"

static const EngineAPI *E;

typedef struct { int her, him, told; float waited; } State;
static State S;

static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static const char *street(void) { return "talk"; }
static float tod(void) { return 17.5f; }   /* late in the day: noon's sky is nearly white, which is hard on a tube and on text */

static void lensDefault(void) { E->lens(1.2f, 1.75f, 5.5f, 0.f, 1.25f, 0.f); }

static void start(int onSet) {
    (void)onSet;
    for (int i = 0; i < E->nBodies; i++) { E->hide[i] = 1; E->team[i] = 0; }
    S.her = E->bodyNew(0.65f, 0.f, 1);
    S.him = E->bodyNew(0.2f, -2.2f, 1);   /* on the same pack as her and with a face of his own: he's got a smile on, and she hasn't. He listens, and looks at whoever's speaking */
    *E->heroX = -0.65f; *E->heroZ = 0.f;
    lensDefault();
    E->tune->heroSpd = 3.f;
    if (S.her >= 0) {
        body(S.her)->rotY = -1.5708f;
        E->talkSeat(0, -1);
        E->talkSeat(1, S.her);
        E->bodyLookBody(S.her, -1);
    }
    if (S.him >= 0) {
        body(S.him)->rotY = 0.f;
        E->talkSeat(2, S.him);
        E->bodyShape(S.him, "smile", 1.f, 0.f);
        E->bodyLookBody(S.him, -1);
    }
}

static void tick(float dt) {
    if (S.her < 0) return;
    if (E->talking()) {
        /* they turn to each other, and nobody walks off */
        E->tune->heroSpd = -1.f;
        const float dx = body(S.her)->x - *E->heroX, dz = body(S.her)->z - *E->heroZ;
        E->heroFace(atan2f(dx, dz));
        body(S.her)->rotY = atan2f(-dx, -dz);
        return;
    }
    E->tune->heroSpd = 3.f;
    E->bodyLookBody(S.her, -1);
    S.waited += dt;
    const float dx = body(S.her)->x - *E->heroX, dz = body(S.her)->z - *E->heroZ;
    /* close enough, and you say something; left alone for a moment, so does the game */
    if (S.waited > 1.f && ((dx * dx + dz * dz < 6.25f && E->pads[0].pressed & PAD_A) || S.waited > 4.f)) { E->talkStart("hello"); S.waited = 0.f; }
}

static int bodyStep(int i, float dt) { (void)i; (void)dt; return 1; }
static void talkEvent(const char *name) { if (!strcmp(name, "told")) S.told++; }

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "TALK", .state = &S, .stateSize = sizeof S, .street = street, .tod = tod, .start = start, .tick = tick, .bodyStep = bodyStep, .talkEvent = talkEvent };
    E = engine;
    return &G;
}
