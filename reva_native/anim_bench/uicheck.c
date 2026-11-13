/* uicheck.c -- a game whose only job is to try to break the menus: E->uiBegin and its rows, through the same table any game gets, with a pad on a tape. uicheck.py builds it with no display, writes the tape and runs it. Its state is in [simhash], the way a game's is. It prints [uicheck] lines. UICHECK_SHOW=<screen> keeps one screen up, for a look on Vulkan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine.h"

static const EngineAPI *E;
static int fails, checks, tickNo, show;

typedef struct {
    EngineUi main, second, any;
    int plays, quits, backs, locked, music, mode, done, secondHits, anyHits, longPicked, changed;
    float volume;
    char name[8];
} State;
static State S;

static void check(const char *what, int ok) {
    checks++;
    if (!ok) { fails++; printf("[uicheck] FAILED at tick %d: %s\n", tickNo, what); }
    else if (getenv("UICHECK_VERBOSE")) printf("[uicheck] ok at tick %d: %s\n", tickNo, what);
}
static int closeTo(float a, float b) { return a > b - 0.001f && a < b + 0.001f; }

static const char *const MODES[] = {"EASY", "HARD", "MAD"};
static const char *const TABS[] = {"GAME", "LONG"};

/* the menu under test, said every tick the way a game says one. Seven rows can be used in its first tab, or six: the fifth is switched off */
static void mainMenu(void) {
    E->uiBegin(&S.main, 0, 2.f, 3.f, 31.f, 18.f);
    int tab = E->uiTabs(TABS, 2);
    if (tab == 0) {
        E->uiLabel("A MENU");
        if (E->uiButton("PLAY")) S.plays++;
        if (E->uiToggle("MUSIC", &S.music)) S.changed++;
        if (E->uiSlider("VOLUME", &S.volume, 0.f, 1.f, 0.1f)) S.changed++;
        if (E->uiChoice("MODE", MODES, 3, &S.mode)) S.changed++;
        E->uiEnable(0);
        if (E->uiButton("LOCKED")) S.locked++;
        E->uiEnable(1);
        if (E->uiText("NAME", S.name, (int)sizeof S.name)) S.done++;
        E->uiGap(1.f);
        if (E->uiButton("QUIT")) S.quits++;
    } else {
        char t[16];
        for (int i = 0; i < 30; i++) { snprintf(t, sizeof t, "ROW %d", i); if (E->uiButton(t)) S.longPicked = i; }
    }
    if (E->uiBack()) S.backs++;
    E->uiEnd();
}
static void others(void) {
    E->uiBegin(&S.second, 1, 2.f, 0.f, 10.f, 4.f);
    if (E->uiButton("P2 ONE")) S.secondHits++;
    if (E->uiButton("P2 TWO")) S.secondHits += 10;
    E->uiEnd();
    E->uiBegin(&S.any, -1, 20.f, 0.f, 10.f, 4.f);
    if (E->uiButton("ANY ONE")) S.anyHits++;
    if (E->uiButton("ANY TWO")) S.anyHits += 10;
    E->uiEnd();
}

/* each check reads what the tape's press on the tick before did: uicheck.py has the other half of this list */
static void script(void) {
    const EngineUi *m = &S.main;
    if (tickNo == 8) check("it starts on its first row, and counts the rows", m->sel == 0 && m->n == 7);
    if (tickNo == 11) check("A on a button", S.plays == 1 && S.changed == 0);
    if (tickNo == 13) check("once for one press", S.plays == 1);
    if (tickNo == 21) check("up from the top goes round to the bottom", m->sel == 6);
    if (tickNo == 31) check("down from the bottom goes round to the top", m->sel == 0);
    if (tickNo == 41) check("down, one row", m->sel == 1);
    if (tickNo == 43) check("and only one, for a tick's stick", m->sel == 1);
    if (tickNo == 51) check("A flips a toggle", S.music == 1 && S.changed == 1);
    if (tickNo == 61) check("so does the stick", S.music == 0 && S.changed == 2);
    if (tickNo == 71) check("down to the slider", m->sel == 2);
    if (tickNo == 81) check("the stick moves a slider a step", closeTo(S.volume, 0.6f));
    if (tickNo == 112) check("held, one step at once and a wait", closeTo(S.volume, 0.7f));
    if (tickNo == 131) check("then a step every 6 ticks, up to the top", closeTo(S.volume, 1.f));
    if (tickNo == 225) check("and down to the bottom", closeTo(S.volume, 0.f));
    if (tickNo == 231) check("down to the words", m->sel == 3);
    if (tickNo == 241) check("the stick goes through the words", S.mode == 1);
    if (tickNo == 261) check("round the end", S.mode == 0);
    if (tickNo == 271) check("and back round", S.mode == 2);
    if (tickNo == 281) check("A goes on through them", S.mode == 0);
    if (tickNo == 291) check("down goes past what's switched off", m->sel == 5);
    if (tickNo == 301) check("A on a word opens the keyboard", m->editing != 0);
    if (tickNo == 311) check("A types", !strcmp(S.name, "A"));
    if (tickNo == 331) check("the stick picks a key", !strcmp(S.name, "AB"));
    if (tickNo == 351) check("in both directions", !strcmp(S.name, "ABL"));
    if (tickNo == 361) check("B rubs out, and stays on the keyboard", !strcmp(S.name, "AB") && m->editing != 0);
    if (tickNo == 401) check("SPACE", !strcmp(S.name, "AB "));
    if (tickNo == 431) { check("DONE ends it, and it says so", S.done == 1 && m->editing == 0); check("B on the keyboard was never the menu's back", S.backs == 0); }
    if (tickNo == 451) { check("it stops at the room it was given", !strcmp(S.name, "AB AAAA")); check("nothing else heard any of that", S.plays == 1 && S.quits == 0 && S.locked == 0); }
    if (tickNo == 461) check("START ends it too", S.done == 2 && m->editing == 0);
    if (tickNo == 481) check("the next row's the last", m->sel == 6);
    if (tickNo == 491) check("the last one's a button", S.quits == 1);
    if (tickNo == 501) check("B goes back", S.backs == 1);
    if (tickNo == 511) check("RB changes tab, from its first row", m->tab == 1 && m->sel == 0 && m->top == 0.f);
    if (tickNo == 513) check("a tab has its own rows", m->n == 30);
    if (tickNo == 625) { printf("[uicheck] held down for 100 ticks: row %d, scrolled to %.1f\n", m->sel, m->top); check("a long one scrolls, so the row in use shows", m->sel == 14 && m->top > 0.f && (float)m->sel * 2.f >= m->top && (float)m->sel * 2.f + 2.f <= m->top + 16.f); }
    if (tickNo == 631) check("A on a row a long way down", S.longPicked == 14);
    if (tickNo == 641) check("LB goes back a tab", m->tab == 0 && m->sel == 0 && m->top == 0.f);
    if (tickNo == 651) check("nothing's been pressed that couldn't be", S.locked == 0);
    /* a second player's is their own, and anyone's is anyone's */
    if (tickNo == 661) { check("a second player's menu goes with their pad", S.second.sel == 1); check("and the first player's doesn't", S.main.sel == 0); check("anyone's goes with anyone's", S.any.sel == 1); }
    if (tickNo == 671) { check("player one's stick isn't player two's", S.second.sel == 1); check("and it moves anyone's", S.any.sel == 0); check("player one's moved", S.main.sel == 1); }
    if (tickNo == 681) { check("player two's A", S.secondHits == 10); check("anyone's A", S.anyHits == 1); check("isn't player one's", S.plays == 1 && S.music == 0); }
    /* silly things do nothing */
    if (tickNo == 691) { E->uiEnd(); E->uiButton("OUTSIDE"); E->uiLabel("OUTSIDE"); E->uiBack(); E->uiEnd(); E->uiBegin(NULL, 0, 0, 0, 10, 10); E->uiEnd(); check("rows outside a menu are nothing", 1); }
    if (tickNo == 760) { printf("[uicheck] %d checks, %d failed\n", checks, fails); fflush(stdout); }
}

/* for a look: one screen at a time */
static void showMenu(void) {
    static EngineUi a; static int on = 1, mode = 1; static float vol = 0.7f; static char name[10] = "ROSE";
    if (show == 1) {
        E->uiBegin(&a, -1, 2.f, 3.f, 31.f, 18.f);
        E->uiTabs(TABS, 2);
        E->uiLabel("A MENU");
        E->uiButton("PLAY");
        E->uiToggle("MUSIC", &on);
        E->uiSlider("VOLUME", &vol, 0.f, 1.f, 0.1f);
        E->uiChoice("MODE", MODES, 3, &mode);
        E->uiEnable(0); E->uiButton("LOCKED"); E->uiEnable(1);
        E->uiText("NAME", name, (int)sizeof name);
        E->uiButton("QUIT");
        E->uiEnd();
        a.sel = 3;
    } else if (show == 2) {
        char t[16];
        a.sel = 14;
        E->uiBegin(&a, -1, 4.f, 4.f, 27.f, 16.f);
        for (int i = 0; i < 30; i++) { snprintf(t, sizeof t, "ROW %d", i); E->uiButton(t); }
        E->uiEnd();
    } else {
        a.editing = 1; a.kx = 4; a.ky = 1;
        E->uiBegin(&a, -1, 4.f, 4.f, 27.f, 16.f);
        E->uiText("NAME", name, (int)sizeof name);
        E->uiEnd();
    }
}
static void boot(void) { const char *s = getenv("UICHECK_SHOW"); show = s ? atoi(s) : 0; S.volume = 0.5f; }
static const char *street(void) { return "none"; }
static void start(int set) { (void)set; for (int i = 0; i < E->nBodies; i++) E->bodyGone(i); }
static void tick(float dt) { (void)dt; tickNo++; if (show) { showMenu(); return; } mainMenu(); if (tickNo >= 655) others(); script(); }
static int bodyStep(int i, float dt) { (void)i; (void)dt; return 1; }

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "UICHECK", .state = &S, .stateSize = sizeof S, .boot = boot, .street = street, .start = start, .tick = tick, .bodyStep = bodyStep };
    E = engine;
    return &G;
}
