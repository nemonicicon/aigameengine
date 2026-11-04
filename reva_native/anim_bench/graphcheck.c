/* graphcheck.c -- a game whose only job is to try to break the animation graph on bodies, through the same table any game gets. graphcheck.py builds it with no display and runs it from games/tower. Every event it hears it prints with its tick, so the same run at odd tick patterns can be compared line for line. It writes a graph file of its own, changes it to check it's read again, and takes it away. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <time.h>
#include "../engine.h"

#define MY_GRAPH "_graphcheck.anim"
static const EngineAPI *E;
static int fails, checks, tickNo, a, b, c, d, e, g, hits[1024], steps[1024], extra;

static void check(const char *what, int ok) {
    checks++;
    if (!ok) { fails++; printf("[graphcheck] FAILED at tick %d: %s\n", tickNo, what); }
    else if (getenv("GRAPHCHECK_VERBOSE")) printf("[graphcheck] ok at tick %d: %s\n", tickNo, what);
}
static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }

static void writeGraph(const char *from, const char *to) {
    FILE *f = fopen(MY_GRAPH, "w");
    if (!f) return;
    fprintf(f, "state %s idle loop 1\nstate %s w_walk loop 1\nwhen %s %s go\nevent %s 0.5 half\n", from, to, from, to, to);
    fclose(f);
    struct utimbuf t; t.actime = t.modtime = time(NULL) + 10 + tickNo;   /* a stamp is seconds: this one's plainly different */
    utime(MY_GRAPH, &t);
}

/* each body gets its own stretch of ticks: which state it should be in by when */
static void script(void) {
    /* a: speed takes it up and down the ladder */
    if (tickNo == 2) check("it starts in its first state", E->bodyAnimIn(a, "idle") && !E->bodyAnimIn(a, "walk"));
    if (tickNo == 3) body(a)->spd = 3.f;
    if (tickNo == 6) check("speed: idle to walk", E->bodyAnimIn(a, "walk"));
    if (tickNo == 7) body(a)->spd = 6.f;
    if (tickNo == 10) check("speed: walk to run", E->bodyAnimIn(a, "run"));
    if (tickNo == 11) body(a)->spd = 3.f;
    if (tickNo == 14) check("speed: run to walk", E->bodyAnimIn(a, "walk"));
    if (tickNo == 15) body(a)->spd = 0.f;
    if (tickNo == 18) check("speed: walk to idle", E->bodyAnimIn(a, "idle"));
    /* b: a flag, a clip that ends, an event that lands once */
    if (tickNo == 20) { check("nothing's happened to b yet", E->bodyAnimIn(b, "idle") && hits[b] == 0); E->bodyAnimSet(b, "punch", 1.f); }
    if (tickNo == 22) check("a flag starts its state", E->bodyAnimIn(b, "punch"));
    if (tickNo == 23) check("and its neighbour's not in it", !E->bodyAnimIn(c, "punch") && !E->bodyAnimIn(a, "punch"));
    if (tickNo == 90) check("the clip ends and it goes back", E->bodyAnimIn(b, "idle"));
    if (tickNo == 91) check("the event landed once, for the body that punched", hits[b] == 1 && hits[a] == 0 && hits[c] == 0);
    if (tickNo == 92) { E->bodyAnimSet(b, "punch", 1.f); }
    if (tickNo == 160) check("a flag is used up: one punch a press", hits[b] == 2 && E->bodyAnimIn(b, "idle"));
    /* c: its own value */
    if (tickNo == 30) E->bodyAnimSet(c, "grounded", 0.f);
    if (tickNo == 33) check("a value it was given: grounded", E->bodyAnimIn(c, "fall"));
    if (tickNo == 34) E->bodyAnimSet(c, "grounded", 1.f);
    if (tickNo == 37) check("and back", E->bodyAnimIn(c, "idle"));
    /* d: steps while walking, none while hidden */
    if (tickNo == 40) body(d)->spd = 3.f;
    if (tickNo == 160) check("walking, it hears its steps", steps[d] >= 3);
    if (tickNo == 161) { E->hide[d] = 1; steps[d] = 0; }
    if (tickNo == 230) check("hidden, it hears nothing", steps[d] == 0);
    if (tickNo == 231) { E->bodyGone(d); int n = E->bodyNew(0, 0, 3); check("the pool gives the same body back", n == d); check("and bodyNew starts its graph again", E->bodyAnimIn(d, "idle")); }
    /* e: taken off, and things that aren't there */
    if (tickNo == 50) { E->bodyGraph(e, NULL); check("taken off: it's in no state", !E->bodyAnimIn(e, "idle")); E->bodyAnimSet(e, "punch", 1.f); }
    if (tickNo == 54) check("and a flag on nothing does nothing", !E->bodyAnimIn(e, "punch") && hits[e] == 0);
    if (tickNo == 55) { E->bodyGraph(e, "tw_no_such_file.anim"); check("a file that isn't there: no graph, and no crash", !E->bodyAnimIn(e, "idle")); E->bodyAnimSet(e, "punch", 1.f); }
    if (tickNo == 56) { E->bodyGraph(-1, "hero.anim"); E->bodyGraph(99999, "hero.anim"); E->bodyAnimSet(-1, "x", 1.f); E->bodyAnimSet(99999, "x", 1.f); check("nonsense bodies are nothing", !E->bodyAnimIn(-1, "idle") && !E->bodyAnimIn(99999, "idle")); }
    /* g: read again when it changes */
    if (tickNo == 60) { writeGraph("one", "two"); E->bodyGraph(g, MY_GRAPH); check("its own file: the first state", E->bodyAnimIn(g, "one")); }
    if (tickNo == 62) { E->bodyAnimSet(g, "go", 1.f); }
    if (tickNo == 64) check("its own file: a transition", E->bodyAnimIn(g, "two"));
    if (tickNo == 70) writeGraph("two", "three");
    if (tickNo == 200) check("read again: it keeps its state's name", E->bodyAnimIn(g, "two"));
    if (tickNo == 201) { E->bodyAnimSet(g, "go", 1.f); }
    if (tickNo == 204) check("and the new transition's what it follows", E->bodyAnimIn(g, "three"));
    if (tickNo == 280) { remove(MY_GRAPH); check("half-way events in its own file came", hits[g] == 0 && steps[g] == 0); }
    if (extra && tickNo == 240) E->bodyAnimSet(c, "punch", 1.f);
    if (tickNo == 300) { printf("[graphcheck] %d checks, %d failed\n", checks, fails); fflush(stdout); }
}

static void clipEvent(int who, const char *name) {
    printf("[graphcheck] event tick %d body %d %s\n", tickNo, who, name);
    if (who >= 0 && !strcmp(name, "hit")) hits[who]++;
    if (who >= 0 && !strcmp(name, "step")) steps[who]++;
}
static void start(int set) {
    (void)set;
    for (int i = 0; i < E->nBodies; i++) E->bodyGone(i);
    a = E->bodyNew(-10, -10, 1); b = E->bodyNew(-6, -10, 1); c = E->bodyNew(-2, -10, 1); d = E->bodyNew(2, -10, 1); e = E->bodyNew(6, -10, 1); g = E->bodyNew(10, -10, 1);
    check("six bodies", a >= 0 && b >= 0 && c >= 0 && d >= 0 && e >= 0 && g >= 0);
    E->bodyGraph(a, "hero.anim"); E->bodyGraph(b, "hero.anim"); E->bodyGraph(c, "hero.anim"); E->bodyGraph(d, "hero.anim"); E->bodyGraph(e, "hero.anim");
}
static void boot(void) { extra = getenv("GRAPHCHECK_EXTRA") != NULL; }
static void tick(float dt) { (void)dt; tickNo++; script(); }
/* a body holds still, at the speed the script gave it */
static int bodyStep(int i, float dt) { (void)dt; EngineBody *o = body(i); o->moving = o->spd > 0.2f; return 1; }
static const char *street(void) { return "tower"; }

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "GRAPHCHECK", .boot = boot, .street = street, .start = start, .tick = tick, .bodyStep = bodyStep, .clipEvent = clipEvent };
    E = engine;
    return &G;
}
