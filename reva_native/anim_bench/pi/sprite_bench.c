/* sprite_bench.c -- how many sprites fit in a field (engine plan, Phase 6: bench first, then a slice). A flat game that moves REVA_SPRN 16 x 16 sprites from one sheet, blended, and does nothing else.
 * Build it as the game: GAME=anim_bench/pi/sprite_bench.c EXTRA=-DREVA_NODOORS ./build_pi.sh vk. Run it from games/rocks, for the sheet: REVA_VIDEO=240p REVA_PASSTIME=2 ../../crowd_vk 0 packs. They're dim on purpose: on a TV, thousands of bright sprites would be a white screen. */
#include "../../engine.h"
#include <stdlib.h>

static const EngineAPI *E;
static struct { int sheet, n; } S;

static void boot(void) {
    const char *e = getenv("REVA_SPRN");
    S.sheet = E->image("sheet.png");
    S.n = e ? atoi(e) : 1000;
    E->clear(0.02f, 0.02f, 0.06f);
    printf("[sprites] %d\n", S.n);
}

static void hud(const void *state) {
    (void)state;
    float t = *E->gameT;
    for (int i = 0; i < S.n; i++) {
        float x = fmodf((float)(i * 97) + t * (float)(12 + i % 23), 376.f) - 16.f;
        float y = fmodf((float)(i * 61) + t * (float)(8 + i % 17), 256.f) - 16.f;
        E->sprite(S.sheet, x, y, 16.f, 16.f, 16.f, 0.f, 32.f, 16.f, 0.35f, 0.3f, 0.25f, 1.f);
    }
}

const GameExport *gameEntry(const EngineAPI *engine) {
    static const GameExport G = { .version = ENGINE_API_VERSION, .name = "SPRITE BENCH", .boot = boot, .hud = hud, .flat = 1 };
    E = engine;
    return &G;
}
