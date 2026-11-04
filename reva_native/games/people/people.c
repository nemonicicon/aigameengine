/* people.c -- a character creator, and the place to start a game whose players make their own person. It includes engine.h and nothing else of the engine's, and it needs no content but a body to boot with.
 * What it shows: E->bodyBuild making a person from measurements while the game runs, E->heroWear and E->bodyWear putting it on, a menu that changes it live, and a street of people dealt from the dice, none of them made ahead of time.
 * Stick up and down picks a row, left and right changes it, and B says something. Everything on the menu is a number in the spec below. */
#include "../../engine.h"

static const EngineAPI *E;

#define CROWD 6
typedef struct { float stature, heads, build, shoulders, hips, limbs; int skin, hairC, hairS, top, bottom, sleeves, bottomK; int bust; } Choice;
typedef struct {
    Choice c;
    EngineUi ui;
    int worn, want, dirty, crowd[CROWD], crowdBody[CROWD], dressed[CROWD];
    float turn, sayIn;
} State;
static State S;

static const unsigned char SKIN[6][3] = {{238, 200, 170}, {214, 170, 140}, {190, 140, 105}, {150, 105, 72}, {105, 70, 48}, {70, 46, 34}};
static const unsigned char HAIR[6][3] = {{25, 20, 20}, {70, 45, 28}, {150, 105, 55}, {210, 180, 110}, {150, 150, 155}, {150, 40, 80}};
static const unsigned char CLOTH[6][3] = {{70, 110, 170}, {170, 60, 55}, {60, 130, 90}, {205, 190, 150}, {50, 55, 70}, {230, 230, 225}};
static const char *const SKIN_N[6] = {"PALE", "FAIR", "TAN", "OLIVE", "BROWN", "DARK"};
static const char *const HAIR_N[6] = {"BLACK", "BROWN", "CHESTNUT", "BLOND", "GREY", "PINK"};
static const char *const STYLE_N[5] = {"NONE", "SHORT", "LONG", "PONYTAIL", "BOB"};
static const char *const CLOTH_N[6] = {"BLUE", "RED", "GREEN", "SAND", "NAVY", "WHITE"};
static const char *const SLEEVE_N[3] = {"BARE ARMS", "SHORT", "LONG"};
static const char *const BOTTOM_N[3] = {"SHORTS", "TROUSERS", "SKIRT"};

static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static const char *street(void) { return "people"; }
static float tod(void) { return 16.5f; }

static void specOf(const Choice *c, EngineBodySpec *o) {
    memset(o, 0, sizeof *o);
    o->stature = c->stature; o->heads = c->heads; o->build = c->build; o->shoulders = c->shoulders; o->hips = c->hips; o->limbs = c->limbs; o->bust = c->bust ? 0.8f : 0.f;
    memcpy(o->skin, SKIN[c->skin], 3); memcpy(o->hair, HAIR[c->hairC], 3); memcpy(o->top, CLOTH[c->top], 3); memcpy(o->bottom, CLOTH[c->bottom], 3);
    o->hairStyle = c->hairS; o->sleeves = c->sleeves; o->bottomKind = c->bottomK;
}
static float rnd(float a, float b) { return a + E->frnd() * (b - a); }
static int rndi(int n) { int v = (int)(E->frnd() * (float)n); return v >= n ? n - 1 : v; }

static void start(int onSet) {
    (void)onSet;
    for (int i = 0; i < E->nBodies; i++) { E->hide[i] = 1; E->team[i] = 0; }
    *E->heroX = 0.f; *E->heroZ = 0.f;
    E->lens(-1.7f, 2.35f, 8.8f, -1.7f, 1.55f, 0.f);   /* a little to one side, so she stands clear of the menu */
    E->tune->heroSpd = -1.f;   /* the stick's for the menu */
    E->tune->lensFov = 30.f;
    S.c = (Choice){1.70f, 7.5f, 1.f, 1.f, 1.f, 1.f, 1, 1, 1, 0, 4, 1, 1, 0};
    S.worn = -1; S.want = -1; S.dirty = 1;
    /* a street behind, of people nobody made ahead of time */
    for (int k = 0; k < CROWD; k++) {
        Choice c = {rnd(1.1f, 1.95f), 0, rnd(0.8f, 1.35f), rnd(0.9f, 1.1f), rnd(0.9f, 1.15f), rnd(0.85f, 1.2f), rndi(6), rndi(6), rndi(5), rndi(6), rndi(6), rndi(3), rndi(3), rndi(2)};
        c.heads = c.stature < 1.3f ? rnd(5.5f, 6.2f) : rnd(7.f, 7.8f);
        EngineBodySpec sp; specOf(&c, &sp);
        S.crowd[k] = E->bodyBuild(&sp, sizeof sp);
        S.crowdBody[k] = E->bodyNew(-7.5f + 3.f * (float)k, -6.f - 2.f * (float)(k % 2), 1);
        if (S.crowdBody[k] >= 0) { body(S.crowdBody[k])->rotY = rnd(-0.5f, 0.5f); E->hide[S.crowdBody[k]] = 1; }   /* out of sight until they've been built */
    }
}

static void menu(void) {
    Choice *c = &S.c; const Choice was = *c;
    E->uiBegin(&S.ui, -1, 1.f, 1.f, 16.f, 22.f);
    E->uiLabel("MAKE A PERSON");
    E->uiSlider("HEIGHT", &c->stature, 1.0f, 2.1f, 0.05f);
    E->uiSlider("HEADS TALL", &c->heads, 5.5f, 8.0f, 0.1f);
    E->uiSlider("BUILD", &c->build, 0.7f, 1.6f, 0.05f);
    E->uiSlider("SHOULDERS", &c->shoulders, 0.8f, 1.3f, 0.05f);
    E->uiSlider("HIPS", &c->hips, 0.8f, 1.3f, 0.05f);
    E->uiSlider("LIMBS", &c->limbs, 0.7f, 1.5f, 0.05f);
    E->uiChoice("SKIN", SKIN_N, 6, &c->skin);
    E->uiChoice("HAIR", STYLE_N, 5, &c->hairS);
    E->uiChoice("COLOUR", HAIR_N, 6, &c->hairC);
    E->uiChoice("TOP", CLOTH_N, 6, &c->top);
    E->uiChoice("SLEEVES", SLEEVE_N, 3, &c->sleeves);
    E->uiChoice("BOTTOM", BOTTOM_N, 3, &c->bottomK);
    E->uiChoice("THEM", CLOTH_N, 6, &c->bottom);
    E->uiEnd();
    if (memcmp(&was, c, sizeof was)) S.dirty = 1;
}

static void tick(float dt) {
    menu();
    /* she turns slowly on the spot, so you see all of it, and looks at the lens when she's facing it */
    S.turn += dt * 0.5f;
    E->heroFace(0.45f * sinf(S.turn));
    /* a change builds the new one, and when it's in she puts it on and the old one's let go. It's off the main thread, so a slider held down doesn't reach a frame */
    if (S.dirty && S.want < 0) { EngineBodySpec sp; specOf(&S.c, &sp); S.want = E->bodyBuild(&sp, sizeof sp); S.dirty = 0; }
    if (S.want >= 0 && E->bodyPackReady(S.want)) {
        E->heroWear(S.want);
        if (S.worn >= 0) E->bodyPackDone(S.worn);
        S.worn = S.want; S.want = -1;
    }
    /* the street gets its people when they've been built */
    for (int k = 0; k < CROWD; k++) {
        const int b = S.crowdBody[k];
        if (b < 0 || S.crowd[k] < 0 || S.dressed[k] || !E->bodyPackReady(S.crowd[k])) continue;
        if (E->bodyWear(b, S.crowd[k])) { S.dressed[k] = 1; E->team[b] = 1; E->hide[b] = 0; E->bodyLookBody(b, -1); }
    }
    if (E->pads[0].pressed & PAD_B) E->bodySay(-1, "-", "Hello. I was only made just now.");
}

static int bodyStep(int i, float dt) { (void)i; (void)dt; return 1; }

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "PEOPLE", .state = &S, .stateSize = sizeof S, .street = street, .tod = tod, .start = start, .tick = tick, .bodyStep = bodyStep };
    E = engine;
    return &G;
}
