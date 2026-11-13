/* physcheck.c -- a game whose only job is to try to break the physics a game can reach: E->spawn with OBJ_LOOSE, E->push, E->pushAt, E->feel, E->objectOnRay, GameExport.objectHit and E->bodyLimp, through the same table any game gets. physcheck.py builds it with no display and runs it from games/tower, on the set that says `climb` and on no set. It works through a list of little experiments, each somewhere out of the others' way, and prints [physcheck] lines. PHYSCHECK_TRACE=1 says where its things are every ten ticks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine.h"

static const EngineAPI *E;
static int fails, checks, tickNo, crate, beacon, onSet, extra, noLimp;
static int drop, kicked, wallHit, stackA, stackB, a, b, hitsWorld, hitsObj, hitsBody, thrown, limp2, rolling, sleeper, slider, base, top1, top2, hitter, target, tall, tall2, gone, hung, thin;
static float mark0, firstBounce, secondBounce, wallMin = 99.f;
static int flung;
static int bounces;

static void check(const char *what, int ok) {
    checks++;
    if (!ok) { fails++; printf("[physcheck] FAILED at tick %d: %s\n", tickNo, what); }
    else if (getenv("PHYSCHECK_VERBOSE")) printf("[physcheck] ok at tick %d: %s\n", tickNo, what);
}
static const EngineObject *o(int h) { return E->object(h); }
static EngineBody *body(int i) { return (EngineBody *)((char *)E->bodies + (size_t)i * E->bodyStride); }
static int closeTo(float x, float want, float by) { return x > want - by && x < want + by; }
static int at(int h, float y) { return o(h) && closeTo(o(h)->y, y, 0.06f); }

static void objectHit(int obj, int other, int who, float speed) {
    if (speed <= 0.f) check("a hit has a speed", 0);
    if (who >= 0 || who == -1) hitsBody++;
    else if (other) hitsObj++;
    else hitsWorld++;
    if (obj == drop && who == -2 && !other) { if (bounces == 0) firstBounce = speed; else if (bounces == 1) secondBounce = speed; bounces++; }
    printf("[physcheck] hit tick %d obj %d other %d body %d speed %.4f\n", tickNo, obj & 1023, other ? other & 1023 : -1, who, speed);
    if (obj == gone) { E->despawn(gone); printf("[physcheck] took it away in its own hit, tick %d\n", tickNo); }
}

static void script(void) {
    /* a dropped crate: it falls at gravity, bounces less each time, ends up flat, and sleeps */
    if (tickNo == 2) { drop = E->spawn(crate, -30, 6, 30, 0.4f, 1, OBJ_LOOSE); check("a loose thing starts awake, at no speed", o(drop) && !o(drop)->asleep && o(drop)->vy == 0.f); check("where it was put", o(drop)->y == 6.f && o(drop)->top > 7.5f && o(drop)->top < 7.7f); }
    if (tickNo == 3) { check("it's falling", o(drop)->y < 6.f && o(drop)->vy < 0.f); hitsWorld = 0; }
    if (tickNo == 12) check("at gravity, 9.8 metres a second a second at 1.75 units a metre", o(drop)->vy < -2.5f && o(drop)->vy > -3.2f);
    if (tickNo == 90) { check("the ground stopped it", o(drop)->y > -0.01f && o(drop)->y < 3.f); check("it told the game it hit the world", hitsWorld >= 1); check("it bounced less the second time", bounces >= 2 && secondBounce < firstBounce * 0.5f); }
    if (tickNo == 180) { check("it's come to rest", o(drop)->asleep && o(drop)->vx == 0.f && o(drop)->vy == 0.f); check("on the ground, flat", closeTo(o(drop)->y, 0.f, 0.02f) && closeTo(o(drop)->top, 1.6f, 0.02f)); }
    /* a push wakes it, it goes, and friction stops it, in about the time you'd work out: 6 units a second at a grip of 0.55 is 0.64 s and 1.9 units along the ground. It's in the air for the first part */
    if (tickNo == 181) { E->push(drop, 6, 2, 0); check("a push wakes it", !o(drop)->asleep && o(drop)->vx > 5.f); mark0 = o(drop)->x; }
    if (tickNo == 186) check("and it goes", o(drop)->x > mark0 + 0.3f);
    if (tickNo == 330) { printf("[physcheck] the crate slid %.2f units\n", o(drop)->x - mark0); check("it's at rest again, further on, and not far", o(drop)->asleep && o(drop)->x > mark0 + 1.f && o(drop)->x < mark0 + 5.f); E->despawn(drop); }
    /* a handle that's gone is safe */
    if (tickNo == 331) { E->push(drop, 1, 1, 1); E->pushAt(drop, 0, 0, 0, 1, 1, 1); E->feel(drop, 0.5f, 0.5f); check("a push on a dead handle does nothing", o(drop) == NULL); }
    /* something that isn't loose can't be pushed, and doesn't fall */
    if (tickNo == 20) kicked = E->spawn(crate, -26, 3, 30, 0, 1, 0);
    if (tickNo == 21) { E->push(kicked, 5, 5, 5); E->pushAt(kicked, 0, 0, 0, 5, 5, 5); E->feel(kicked, 0.1f, 0.1f); }
    if (tickNo == 40) { check("not loose: it doesn't fall, and a push does nothing", o(kicked)->y == 3.f && o(kicked)->x == -26.f && o(kicked)->vx == 0.f); E->despawn(kicked); }
    /* resting and solid, one on another: it stays where it was put until the thing under it goes */
    if (tickNo == 10) { stackA = E->spawn(crate, 30, 0, 30, 0, 1, OBJ_LOOSE | OBJ_SOLID | OBJ_RESTING); stackB = E->spawn(crate, 30, 1.6f, 30, 0, 1, OBJ_LOOSE | OBJ_SOLID | OBJ_RESTING); hung = E->spawn(crate, 40, 5, 30, 0, 1, OBJ_LOOSE | OBJ_RESTING); }
    if (tickNo == 30) { float t = 0, p0[3] = {27, 0.8f, 30}, p1[3] = {33, 0.8f, 30}; check("resting: it starts asleep and stays", o(stackB)->asleep && o(stackA)->asleep && o(stackB)->y == 1.6f); check("resting and solid: a ray stops at it", E->ray(p0, p1, &t)); check("resting in the air, it hangs", o(hung)->asleep && o(hung)->y == 5.f); }
    if (tickNo == 31) { E->push(stackA, 12, 2, 0); E->push(hung, 0, 0.1f, 0); }
    if (tickNo == 33) check("pushed: awake, and not solid", !o(stackA)->asleep);
    if (tickNo == 36) check("what was standing on it wakes", !o(stackB)->asleep);
    if (tickNo == 100) check("a push on what's hanging lets it go", closeTo(o(hung)->y, 0.f, 0.3f));
    if (tickNo == 220) { check("and comes down onto the ground, where it was", at(stackB, 0.f) && o(stackB)->asleep && closeTo(o(stackB)->x, 30.f, 0.8f)); float t = 0, p0[3] = {27, 0.8f, 30}, p1[3] = {o(stackB)->x, 0.8f, o(stackB)->z}; check("solid again, asleep, where it landed", E->ray(p0, p1, &t)); check("friction stopped the other", o(stackA)->asleep && o(stackA)->x < 40.f); E->despawn(stackA); E->despawn(stackB); E->despawn(hung); }
    /* two thrown at each other: they meet, and it's in the event, by name */
    if (tickNo == 40) { a = E->spawn(crate, -30, 1, -30, 0, 1, OBJ_LOOSE); b = E->spawn(crate, -24, 1, -30, 0, 1, OBJ_LOOSE); E->push(a, 8, 2, 0); E->push(b, -8, 2, 0); hitsObj = 0; }
    if (tickNo == 90) { check("two loose things meet", hitsObj >= 2); check("and go back the way they came", o(a)->x < -27.f && o(b)->x > -27.f); check("and they haven't gone through each other", o(b)->x - o(a)->x >= 1.59f); E->despawn(a); E->despawn(b); }
    /* one thrown at a body: it bounces, the game hears, and the body's not moved */
    if (tickNo == 95) { thrown = E->spawn(crate, -40, 1.5f, 10, 0, 1, OBJ_LOOSE); limp2 = E->bodyNew(-34, 10, 1); hitsBody = 0; E->push(thrown, 14, 3, 0); }
    if (tickNo == 130) { check("a thrown thing hits a body, and the game hears, once", hitsBody == 1); check("it bounced off", o(thrown)->x < -34.f); check("and the body's where it was", body(limp2)->x == -34.f); E->despawn(thrown); }
    /* a body goes limp, and back */
    if (tickNo == 140 && !noLimp) { check("not limp yet", E->bodyLimpState(limp2) == 0); check("it can go limp", E->bodyLimp(limp2, 3, 1, 0) == 1); check("then it's falling", E->bodyLimpState(limp2) == 1); check("asking twice is fine", E->bodyLimp(limp2, 3, 1, 0) == 1); }
    if (tickNo == 141) { E->bodyLimp(-5, 0, 0, 0); E->bodyLimp(99999, 0, 0, 0); E->bodyRise(-5); E->bodyRise(99999); check("nonsense bodies are nothing", E->bodyLimpState(-5) == 0 && E->bodyLimpState(99999) == 0); }
    if (tickNo == 259 && !noLimp) { printf("[physcheck] a limp body's state at tick 259: %d\n", E->bodyLimpState(limp2)); check("it's come to rest", E->bodyLimpState(limp2) == 2); check("and it's still where the game says", body(limp2)->x == -34.f); }
    if (tickNo == 260) { E->bodyRise(limp2); check("bodyRise puts it back", E->bodyLimpState(limp2) == 0); E->bodyGone(limp2); }
    if (tickNo == 262) { static int many[20]; int n = 0, got = 0; while (n < 20) { int i = E->bodyNew(-34.f + (float)n, 14.f, 1); if (i < 0) break; many[n++] = i; }
        for (int i = 0; i < n; i++) if (E->bodyLimp(many[i], 0, 0, 0)) got++;
        printf("[physcheck] %d of %d bodies went limp\n", got, n);
        check("16 at once, and the rest told no", n > 16 ? got == 16 : got == n);
        for (int i = 0; i < n; i++) E->bodyGone(many[i]);
        check("bodyGone frees them", n == 0 || E->bodyLimpState(many[0]) == 0);
        int again = E->bodyNew(-34.f, 10.f, 1); check("and they can go limp again", again >= 0 && E->bodyLimp(again, 0, 0, 0) == 1); E->bodyGone(again); }
    /* a body thrown a long way while it's limp is still the game's: nothing else decides it's lost */
    if (tickNo == 270) { flung = E->bodyNew(-36.f, 8.f, 1); if (!noLimp) E->bodyLimp(flung, 0, 3, -30); }
    if (tickNo == 340) { check("a body thrown a long way isn't banned from the pool", !E->ban[flung]); E->bodyGone(flung); }
    /* the hero, which is -1 */
    if (tickNo == 270 && !noLimp) { check("the hero isn't limp", E->bodyLimpState(-1) == 0); check("the hero can go limp", E->bodyLimp(-1, 2, 1, 0) == 1); check("the hero's falling", E->bodyLimpState(-1) == 1); }
    if (tickNo == 400 && !noLimp) { printf("[physcheck] the hero's state at tick 400: %d\n", E->bodyLimpState(-1)); check("the hero's come to rest", E->bodyLimpState(-1) == 2); E->bodyRise(-1); check("the hero's back", E->bodyLimpState(-1) == 0); }
    /* a stack: one that's solid, one that isn't on it, and one on that. Knock the bottom out and they come down */
    if (tickNo == 345) { base = E->spawn(crate, 30, 0, -30, 0, 1, OBJ_LOOSE | OBJ_SOLID | OBJ_RESTING); top1 = E->spawn(crate, 30, 4, -30, 0, 1, OBJ_LOOSE); }
    if (tickNo == 400) top2 = E->spawn(crate, 30, 6, -30, 0, 1, OBJ_LOOSE | OBJ_SOLID);
    if (tickNo == 520) { printf("[physcheck] a stack: %.3f %.3f %.3f\n", o(base)->y, o(top1)->y, o(top2)->y); check("a crate landed on a crate", at(top1, 1.6f) && o(top1)->asleep); check("and another on that", at(top2, 3.2f) && o(top2)->asleep); check("the bottom one hasn't moved", o(base)->x == 30.f && o(base)->y == 0.f && o(base)->asleep); E->push(base, 12, 0, 0); }
    if (tickNo == 530) check("knock out the bottom one and the next wakes", !o(top1)->asleep);
    if (tickNo == 535) check("and the one on it", !o(top2)->asleep);
    if (tickNo == 640) { printf("[physcheck] the stack came down: %.3f %.3f\n", o(top1)->y, o(top2)->y); check("they came down", at(top1, 0.f) && at(top2, 1.6f)); E->despawn(base); E->despawn(top1); E->despawn(top2); }
    /* a crate knocks a crate along: what it had goes into the other one */
    if (tickNo == 345) { hitter = E->spawn(crate, -40, 0, -40, 0, 1, OBJ_LOOSE); target = E->spawn(crate, -34, 0, -40, 0, 1, OBJ_LOOSE | OBJ_SOLID | OBJ_RESTING); hitsObj = 0; E->push(hitter, 13, 0, 0); }
    if (tickNo == 420) { check("a crate hit another", hitsObj >= 2); check("and knocked it along", o(target)->x > -33.f); check("and neither's in the other", o(target)->x - o(hitter)->x >= 1.59f); }
    if (tickNo == 520) { check("they've both stopped", o(target)->asleep && o(hitter)->asleep); E->despawn(hitter); E->despawn(target); }
    /* something tall goes over from a push high up, and stands up to a push at its foot */
    if (tickNo == 345) { tall = E->spawn(beacon, -50, 0, -40, 0, 1, OBJ_LOOSE); tall2 = E->spawn(beacon, -50, 0, -30, 0, 1, OBJ_LOOSE); }
    if (tickNo == 400) { check("a tall thing stands", o(tall)->asleep && closeTo(o(tall)->top, 3.f, 0.02f)); E->pushAt(tall, o(tall)->x, 2.9f, o(tall)->z, 2.5f, 0, 0); E->pushAt(tall2, o(tall2)->x, 0.1f, o(tall2)->z, 1.f, 0, 0); }
    if (tickNo == 540) { printf("[physcheck] pushed high it's %.2f tall, and pushed low %.2f\n", o(tall)->top - o(tall)->y, o(tall2)->top - o(tall2)->y); check("pushed high up, it's gone over, onto its side", o(tall)->asleep && closeTo(o(tall)->top - o(tall)->y, 0.5f, 0.03f)); check("nudged at its foot, it's still up", o(tall2)->asleep && closeTo(o(tall2)->top - o(tall2)->y, 3.f, 0.03f)); E->despawn(tall); E->despawn(tall2); }
    /* a straight line asks what it meets */
    if (tickNo == 530) { float t = 0.f, p0[3] = {-55, 1, 0}, p1[3] = {-45, 1, 0}, p2[3] = {-55, 1, 9}, p3[3] = {-45, 1, 9}; thin = E->spawn(crate, -50, 0, 0, 0, 1, 0); check("objectOnRay: what's there", E->objectOnRay(p0, p1, &t) == thin && closeTo(t, 0.42f, 0.02f)); check("objectOnRay: nothing", E->objectOnRay(p2, p3, &t) == 0); E->despawn(thin); check("objectOnRay: not what's gone", E->objectOnRay(p0, p1, &t) == 0); }
    /* taken away inside its own hit */
    if (tickNo == 300) gone = E->spawn(crate, 50, 6, 50, 0, 1, OBJ_LOOSE | OBJ_SOLID);
    if (tickNo == 370) check("a thing can be taken away in its own hit", o(gone) == NULL);
    if (onSet) {
        /* on the walked world: a crate lands on the deck. On the ramp a crate stays and a ball rolls down, and with no grip a crate slides */
        if (tickNo == 150) { sleeper = E->spawn(crate, 3, 9, -7, 0, 1, OBJ_LOOSE); rolling = E->spawn(crate, 12, 4, -9.2f, 0, 1, OBJ_LOOSE | OBJ_ROUND); slider = E->spawn(crate, 15, 4, -11, 0, 1, OBJ_LOOSE); E->feel(rolling, 0.f, 0.1f); E->feel(slider, 0.f, 0.55f); }
        if (tickNo == 300) { printf("[physcheck] on the deck %.3f, on the ramp %.3f at %.3f, the ball at %.2f\n", o(sleeper)->y, o(slider)->y, o(slider)->x, o(rolling)->x); check("it landed on the deck, not the ground", o(sleeper)->asleep && at(sleeper, 3.f)); check("a ball on the ramp rolled down it", o(rolling)->x > 21.f); check("a crate on the ramp stays, on a slope its grip can hold", o(slider)->asleep && closeTo(o(slider)->x, 15.f, 1.f) && o(slider)->y > 1.f); E->feel(slider, 0.f, 0.05f); E->push(slider, 0, 0.5f, 0); E->despawn(sleeper); E->despawn(rolling); }
        if (tickNo == 420) { printf("[physcheck] with no grip the crate's at %.2f\n", o(slider)->x); check("with no grip it slid down", o(slider)->x > 18.f); E->despawn(slider); }
        /* a wall stops it: the one at z 2 to 2.5, from x 14 to 30 */
        if (tickNo == 310) { wallHit = E->spawn(crate, 20, 2, 7, 0, 1, OBJ_LOOSE); hitsWorld = 0; E->push(wallHit, 0, 3, -16); }
        if (tickNo > 310 && tickNo < 345 && o(wallHit) && o(wallHit)->z < wallMin) wallMin = o(wallHit)->z;
        if (tickNo == 345) { printf("[physcheck] the wall stopped it at z %.3f\n", wallMin); check("a wall stops it, thrown fast, at the wall", wallMin > 3.1f && wallMin < 3.6f); check("it heard the wall", hitsWorld >= 1); E->despawn(wallHit); }
        /* it won't go into the deck from a ledge either: slid off the edge, it ends up clear of it */
        if (tickNo == 430) { sleeper = E->spawn(crate, 6.5f, 3, -6, 0, 1, OBJ_LOOSE); E->push(sleeper, 8, 0, 0); }
        if (tickNo == 560) { printf("[physcheck] off the edge of the deck: %.3f %.3f\n", o(sleeper)->x, o(sleeper)->y); check("slid off the deck, it's on the ground, clear of it", at(sleeper, 0.f) && o(sleeper)->x > 8.7f); E->despawn(sleeper); }
    } else {
        if (tickNo == 150) { sleeper = E->spawn(crate, 5, 0, 5, 0, 1, OBJ_SOLID); wallHit = E->spawn(crate, 1, 1, 5, 0, 1, OBJ_LOOSE); hitsWorld = 0; E->push(wallHit, 12, 0, 0); }
        if (tickNo == 200) { check("no set: something solid and not loose stops it", o(wallHit)->x < 3.45f); E->despawn(sleeper); E->despawn(wallHit); }
    }
    if (extra && tickNo == 240) E->spawn(crate, 10, 5, 10, 0, 1, OBJ_LOOSE);
    if (tickNo == 660) { printf("[physcheck] %d checks, %d failed, on %s\n", checks, fails, onSet ? "a set that says climb" : "no set"); fflush(stdout); }
}
static void boot(void) { extra = getenv("PHYSCHECK_EXTRA") != NULL; noLimp = getenv("PHYSCHECK_NOLIMP") != NULL; crate = E->prop("tw_crate"); beacon = E->prop("tw_beacon"); }
static const char *street(void) { const char *s = getenv("PHYSCHECK_STREET"); onSet = s && strcmp(s, "none"); return onSet ? s : "none"; }
/* the lens is on the bodies that go limp, because what's off screen isn't posed */
static void start(int set) { (void)set; for (int i = 0; i < E->nBodies; i++) E->bodyGone(i); *E->heroX = -40.f; *E->heroZ = 12.f;
    float L[6] = {-37.f, 7.f, 26.f, -36.f, 1.f, 11.f}; const char *s = getenv("PHYSCHECK_LENS"); if (s) sscanf(s, "%f,%f,%f,%f,%f,%f", &L[0], &L[1], &L[2], &L[3], &L[4], &L[5]);
    E->lens(L[0], L[1], L[2], L[3], L[4], L[5]); }
static void trace(const char *name, int h) { const EngineObject *O = o(h); if (O) printf("[trace] %d %s %.3f %.3f %.3f top %.3f v %.3f %.3f %.3f %s\n", tickNo, name, O->x, O->y, O->z, O->top, O->vx, O->vy, O->vz, O->asleep ? "asleep" : ""); }
static void tick(float dt) { (void)dt; tickNo++; *E->heroX = -40.f; *E->heroZ = 12.f; script();
    if (getenv("PHYSCHECK_TRACE") && tickNo % 10 == 0) { trace("drop", drop); trace("A", stackA); trace("B", stackB); trace("sleeper", sleeper); trace("rolling", rolling); trace("slider", slider); trace("wall", wallHit); trace("base", base); trace("top1", top1); trace("top2", top2); trace("hitter", hitter); trace("target", target); trace("tall", tall); trace("tall2", tall2); } }
static int bodyStep(int i, float dt) { (void)i; (void)dt; return 1; }

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "PHYSCHECK", .boot = boot, .street = street, .start = start, .tick = tick, .bodyStep = bodyStep, .objectHit = objectHit };
    E = engine;
    return &G;
}
