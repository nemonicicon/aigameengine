/* objcheck.c -- a game whose only job is to try to break E->spawn and the rest of what engine/objects.inc offers, through the same table any game gets. objcheck.py builds it with no display and runs it from games/tower, on the set that says `climb` and on no set at all. It prints [objcheck] lines and how many failed. It also leaves a few things moving, so the same run's [simhash] lines can be compared. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../engine.h"

static const EngineAPI *E;
static int fails, checks, crate, beacon, lift, mover, tickNo, extra, onSet;
static int spawnedInBoot;

static void check(const char *what, int ok) {
    checks++;
    if (!ok) { fails++; printf("[objcheck] FAILED: %s\n", what); }
    else if (getenv("OBJCHECK_VERBOSE")) printf("[objcheck] ok: %s\n", what);
}

static int cmpInt(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return x < y ? -1 : x > y; }
static int hitT(float x0, float y0, float z0, float x1, float y1, float z1, float *t) {
    float a[3] = {x0, y0, z0}, b[3] = {x1, y1, z1};
    return E->ray(a, b, t);
}
/* one step of a walk along x: where it ended up */
static float walkX(float x0, float x1, float z, float y) {
    float x = x1, zz = z;
    E->walk(x0, z, &x, &zz, y, 0.42f);
    return x;
}

static void testHandles(void) {
    check("the same name is the same prop", E->prop("tw_crate") == crate);
    check("a prop that isn't there spawns nothing, and doesn't crash", E->spawn(E->prop("tw_no_such_prop"), 0, 0, 0, 0, 1, 0) == 0);
    check("a number nobody was given spawns nothing", E->spawn(9999, 0, 0, 0, 0, 1, 0) == 0 && E->spawn(-1, 0, 0, 0, 0, 1, 0) == 0);
    check("spawn in boot was refused", spawnedInBoot == 0);
    int h = E->spawn(crate, 1, 2, 3, 0.5f, 2.f, 0);
    check("spawn gives a handle", h != 0);
    const EngineObject *o = E->object(h);
    check("object says what it is", o && o->x == 1 && o->y == 2 && o->z == 3 && o->yaw == 0.5f && o->scale == 2.f && o->prop == crate && o->flags == 0);
    E->place(h, 4, 5, 6, 0.25f);
    o = E->object(h);
    check("place moves it", o && o->x == 4 && o->y == 5 && o->z == 6 && o->yaw == 0.25f);
    E->paint(h, 1, 0, 0); E->paint(h, -1, 0, 0);
    E->despawn(h);
    check("despawn takes it out", E->object(h) == NULL);
    E->place(h, 7, 7, 7, 0); E->paint(h, 1, 1, 1); E->despawn(h);
    int h2 = E->spawn(crate, 8, 8, 8, 0, 1, 0);
    check("the slot's used again under a new handle", h2 != 0 && h2 != h && (h2 & 1023) == (h & 1023));
    check("the old handle stays dead", E->object(h) == NULL);
    E->place(h, 9, 9, 9, 0); E->despawn(h);
    o = E->object(h2);
    check("a stale handle can't touch what took its slot", o && o->x == 8);
    E->despawn(h2);
    check("nonsense handles are nothing", E->object(0) == NULL && E->object(-1) == NULL && E->object(123456789) == NULL);
    E->despawn(0); E->despawn(-1); E->place(0, 0, 0, 0, 0); E->paint(-5, 0, 0, 0);
}

static void testPool(void) {
    static int seen[1024 * 6], live[1024];
    int n = 0;
    for (int cycle = 0; cycle < 5; cycle++) {
        for (int i = 0; i < 1024; i++) { live[i] = E->spawn(beacon, (float)i, 0, 0, 0, 1, 0); seen[n++] = live[i]; }
        int all = 1;
        for (int i = 0; i < 1024; i++) if (!live[i]) all = 0;
        check("all 1024 come out", all);
        if (cycle == 0) {
            check("the next one gets 0", E->spawn(beacon, 0, 0, 0, 0, 1, 0) == 0);
            E->despawn(live[17]);
            int again = E->spawn(beacon, 0, 0, 0, 0, 1, 0);
            check("taking one out makes room for one", again != 0);
            live[17] = again; seen[n++] = again;
        }
        for (int i = 0; i < 1024; i++) E->despawn(live[i]);
    }
    qsort(seen, (size_t)n, sizeof(int), cmpInt);
    int dup = 0;
    for (int i = 1; i < n; i++) if (seen[i] == seen[i - 1]) dup = 1;
    check("no handle was ever given out twice", !dup);
    int turned = 1, h0 = 0;
    for (int i = 0; i < 3000; i++) { int h = E->spawn(beacon, 0, 0, 0, 0, 1, 0); if (!h || h == h0) turned = 0; h0 = h; E->despawn(h); }
    check("3000 times through one slot, every handle new", turned);
}

/* on the set that says `climb`, in a clear part of the ground */
static void testSolidClimb(void) {
    float t = 0;
    check("nothing there yet: a walk goes through", walkX(23, 24, 25, 0) == 24);
    check("nothing there yet: a ray goes through", !hitT(20, 1, 25, 30, 1, 25, &t));
    int plain = E->spawn(crate, 25, 0, 25, 0, 1, 0);
    check("not solid: a walk still goes through", walkX(23, 24, 25, 0) == 24);
    check("not solid: a ray still goes through", !hitT(20, 1, 25, 30, 1, 25, &t));
    E->despawn(plain);
    int h = E->spawn(crate, 25, 0, 25, 0, 1, OBJ_SOLID);
    check("solid: a walk into it stops at its face, a body's width off", walkX(23, 24, 25, 0) < 23.85f);
    check("solid: a walk past it doesn't", walkX(23, 24, 22.5f, 0) == 24);
    check("solid: a ray stops at its face", hitT(20, 1, 25, 30, 1, 25, &t) && t > 0.39f && t < 0.45f);
    check("solid: too high to step on from the ground", E->ground(25, 0, 25) == 0);
    check("solid: stand on it from above", E->ground(25, 1.5f, 25) > 1.55f && E->ground(25, 1.5f, 25) < 1.65f);
    E->place(h, 29, 0, 25, 0);
    check("moved: the old place is clear", walkX(23, 24, 25, 0) == 24);
    check("moved: the new place stops a walk", walkX(27, 28, 25, 0) < 27.85f);
    check("moved: a ray stops at the new face", hitT(20, 1, 25, 40, 1, 25, &t) && t > 0.40f && t < 0.43f);
    E->place(h, 29, 5, 25, 0);
    check("lifted: a walk goes under it", walkX(27, 28, 25, 0) == 28);
    check("lifted: a ray under it goes through", !hitT(20, 1, 25, 40, 1, 25, &t));
    check("lifted: a ray at its height stops", hitT(20, 5.5f, 25, 40, 5.5f, 25, &t));
    check("lifted: stand on its new top", E->ground(29, 6.5f, 25) > 6.55f && E->ground(29, 6.5f, 25) < 6.65f);
    E->despawn(h);
    check("gone: a ray at that height goes through", !hitT(20, 5.5f, 25, 40, 5.5f, 25, &t));
    int l = E->spawn(lift, 25, 0, 25, 0, 1, OBJ_SOLID);
    check("square on: its face is 1.5 from the middle", hitT(20, 0.2f, 25, 30, 0.2f, 25, &t) && t > 0.32f && t < 0.38f);
    E->place(l, 25, 0, 25, 0.7853982f);
    check("turned 45 degrees: its corner is 2.12 from the middle", hitT(20, 0.2f, 25, 30, 0.2f, 25, &t) && t > 0.26f && t < 0.32f);
    E->despawn(l);
    /* more solid things than the grids hold: they keep working as far as they can, and it all comes back */
    static int many[420];
    for (int i = 0; i < 420; i++) many[i] = E->spawn(beacon, 35.f + (float)(i % 20), 0, 30.f + (float)(i / 20), 0, 1, OBJ_SOLID);
    int all = 1;
    for (int i = 0; i < 420; i++) if (!many[i]) all = 0;
    check("420 solid ones all come out", all);
    for (int i = 0; i < 420; i++) E->despawn(many[i]);
    check("and all go: a ray through where they were is clear", !hitT(30, 1, 31, 60, 1, 31, &t));
    check("and the first grids are free again", (h = E->spawn(crate, 25, 0, 25, 0, 1, OBJ_SOLID)) && hitT(20, 1, 25, 30, 1, 25, &t));
    E->despawn(h);
    /* what's in a body's circle: it can walk out of that, and along it, and never further in */
    int inA = E->spawn(crate, 25, 0, 25, 0, 1, OBJ_SOLID);
    check("a crate came down on it: it can walk out", walkX(25, 26.5f, 25, 0) == 26.5f);
    int inB = E->spawn(crate, 27.3f, 0, 25, 0, 1, OBJ_SOLID);
    check("inside one thing is no way into another", walkX(25, 26.3f, 25, 0) < 26.1f);
    E->despawn(inA); E->despawn(inB);
    check("beside the deck's wall, on the ground: not through it in one long move", walkX(8.44f, 7.f, -7.2f, 0) > 8.f);
    { float x = 8.44f; for (int i = 0; i < 40; i++) x = walkX(x, x - 0.18f, -7.2f, 0); check("and not a step at a time either", x > 8.f); }
    check("a fast body doesn't jump the wall from further off", walkX(9.5f, 7.6f, -7.2f, 0) > 8.f);
    check("but a game can put someone anywhere", walkX(30.f, 2.f, -20.f, 0) == 2.f);
    { float x = 8.44f, z = -7.2f; E->walk(8.44f, -7.2f, &x, &z, 0.f, 0.42f); check("standing still there is fine", x == 8.44f); }
    { float x = 8.44f, z = -6.7f; E->walk(8.44f, -7.2f, &x, &z, 0.f, 0.42f); check("and it can walk along the wall", z == -6.7f); }
    check("and away from it", walkX(8.44f, 9.f, -7.2f, 0) == 9.f);
}

/* with no set, the old colliders: a circle, and the ray still sees its triangles */
static void testSolidFloor(void) {
    float t = 0, x = 3.f, z = 3.f;
    check("nothing there yet: collide doesn't move a point", E->collide(&x, &z, 0.4f) == 0.f && x == 3.f);
    int plain = E->spawn(crate, 3, 0, 3, 0, 1, 0);
    check("not solid: collide still doesn't", E->collide(&x, &z, 0.4f) == 0.f);
    E->despawn(plain);
    int h = E->spawn(crate, 3, 0, 3, 0, 1, OBJ_SOLID);
    x = 3.2f; z = 3.f;
    float d = E->collide(&x, &z, 0.4f);
    check("solid: collide pushes a point out", d > 0.5f && x > 3.8f);
    x = 3.2f; z = 3.f;
    { float nx = x, nz = z; E->walk(1.f, 3.f, &nx, &nz, 0.f, 0.4f); check("solid: walk pushes it out too", nx > 3.8f); }
    check("solid: a ray stops at its face", hitT(0, 1, 3, 10, 1, 3, &t) && t > 0.18f && t < 0.26f);
    E->place(h, 9, 0, 3, 0);
    x = 3.2f; z = 3.f;
    check("moved: the old place is clear", E->collide(&x, &z, 0.4f) == 0.f);
    x = 9.2f; z = 3.f;
    check("moved: the new place pushes", E->collide(&x, &z, 0.4f) > 0.5f);
    check("moved: the ray follows", hitT(0, 1, 3, 20, 1, 3, &t) && t > 0.39f && t < 0.44f);
    E->despawn(h);
    x = 9.2f; z = 3.f;
    check("gone: nothing pushes", E->collide(&x, &z, 0.4f) == 0.f);
    check("gone: nothing stops a ray", !hitT(0, 1, 3, 20, 1, 3, &t));
}

static void testBodies(void) {
    for (int i = 0; i < E->nBodies; i++) E->bodyGone(i);
    int usable = 0, first = -1;
    for (int i = 0; i < E->nBodies; i++) if (!E->ban[i] && E->drawable[i]) { usable++; if (first < 0) first = i; }
    check("this pool has bodies to test with", usable >= 4);
    int a = E->bodyNew(3, 4, 1);
    check("bodyNew wakes the lowest free one", a == first);
    const EngineBody *b = (const EngineBody *)((const char *)E->bodies + (size_t)a * E->bodyStride);
    check("it's where it was asked, on the team, standing still", b->x == 3 && b->z == 4 && b->tx == 3 && b->tz == 4 && E->team[a] == 1 && !E->hide[a] && b->spd == 0 && !b->moving && E->feet[a] == 0);
    int c = E->bodyNew(5, 6, 2);
    check("the next one is another", c != a && c >= 0 && E->team[c] == 2);
    E->bodyGone(a);
    check("bodyGone hides it and takes its team", E->hide[a] && E->team[a] == 0);
    check("and it's the next one out", E->bodyNew(7, 8, 1) == a);
    E->team[first] = 1;
    int n = 2;
    while (E->bodyNew(0, 0, 1) >= 0) n++;
    check("the pool runs out at what it holds", n == usable);
    check("and then it says -1", E->bodyNew(0, 0, 1) == -1);
    E->bodyGone(-1); E->bodyGone(99999);
    for (int i = 0; i < E->nBodies; i++) E->bodyGone(i);
    for (int i = 0; i < 2; i++) E->bodyNew(-20.f + 4.f * (float)i, -20.f, 1);
}

static void boot(void) {
    extra = getenv("OBJCHECK_EXTRA") != NULL;
    crate = E->prop("tw_crate"); beacon = E->prop("tw_beacon"); lift = E->prop("tw_lift");
    spawnedInBoot = E->spawn(crate, 0, 0, 0, 0, 1, 0);
}
static const char *street(void) { const char *s = getenv("OBJCHECK_STREET"); onSet = s && strcmp(s, "none"); return onSet ? s : "none"; }
static void start(int set) {
    (void)set;
    testHandles();
    testPool();
    if (onSet) testSolidClimb(); else testSolidFloor();
    testBodies();
    mover = E->spawn(crate, 20, 0, 20, 0, 1, OBJ_SOLID);
}
/* the hero on a lift, a tick at a time, which is what a game actually does with one */
/* a model you've finished with: it goes, and its number still works */
static void testPropDone(void) {
    static int prop, h;
    if (tickNo == 291) { prop = lift; check("letting go of a model something has out does nothing", (h = E->spawn(prop, 40, 0, 40, 0, 1, 0)) && !E->propDone(prop)); }
    if (tickNo == 292) { E->despawn(h); h = 0; }
    if (tickNo == 293) check("and when nothing has it out, it goes", E->propDone(prop));
    if (tickNo == 294) { check("and its number still works", (h = E->spawn(prop, 40, 0, 40, 0, 1, 0)) && E->object(h)); E->despawn(h); }
    if (tickNo == 295) check("a number nobody was given is refused", !E->propDone(9999) && !E->propDone(-1));
}
static void testRide(void) {
    static int ride;
    if (tickNo == 5) { ride = E->spawn(lift, -30, -0.35f, 30, 0, 1, OBJ_SOLID); *E->heroX = -30; *E->heroZ = 30; }
    if (tickNo > 5 && tickNo <= 125) E->place(ride, -30, -0.35f + (float)(tickNo - 5) * 0.02f, 30, 0);
    if (tickNo == 130) check("she rides a lift up", *E->heroFeet > 2.35f && *E->heroFeet < 2.55f);
    if (tickNo > 130 && tickNo <= 250) E->place(ride, -30, -0.35f + (float)(250 - tickNo) * 0.02f, 30, 0);
    if (tickNo == 255) check("and down", *E->heroFeet < 0.2f);
    if (tickNo == 256) { E->despawn(ride); }
    if (tickNo == 290) check("and when it's gone she's on the ground", *E->heroFeet < 0.05f);
}
static void tick(float dt) {
    (void)dt;
    tickNo++;
    if (onSet) testRide();
    testPropDone();
    if (tickNo == 300) { printf("[objcheck] %d checks, %d failed, on %s\n", checks, fails, onSet ? "a set that says climb" : "no set"); fflush(stdout); }
    E->place(mover, 20.f + 2.f * sinf((float)tickNo * 0.02f), 0, 20, 0);
    if (tickNo % 30 == 0) { static int h; E->despawn(h); h = E->spawn(beacon, (float)(tickNo % 90), 0, -3, 0, 1, 0); }
    if (extra && tickNo == 200) E->spawn(beacon, 11, 0, 11, 0, 1, 0);
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "OBJCHECK", .boot = boot, .street = street, .start = start, .tick = tick };
    E = engine;
    return &G;
}
