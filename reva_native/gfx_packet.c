/* gfx_packet.c -- THE PACKET (engine plan, Phase 2). The N64 lesson in usable form: the game writes the frame as data, and something else draws it.
 * gfxFrameBegin to gfxFrameEnd build one plain struct in ordinary memory, and nothing comes back, so the sim can't read the renderer. gfx_vk.c draws the finished packet. The no-display build
 * (-DREVA_HEADLESS) has no renderer at all and only hashes it, so everything the game asks to be drawn can be compared with no GPU, on any machine, in seconds, before any pixel check.
 * REVA_SIMHASH=<every N> or REVA_PACKETHASH=<every N> prints `[packet] tick T <hash>`; REVA_PACKETDUMP=<tick>:<file> writes that frame as text, one draw a line, so two runs can be diffed.
 * The hash leaves out GL names, which depend on load order rather than on the frame: a draw's stable id says whose it is.
 * Its memory is one fixed arena, reset every frame. Past the ceiling is loud and draws less, never more memory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

#define PK_MEM (16u << 20)
static GfxPacket g_pk;
static GfxStats g_st, g_stNext;
static int g_live;
#define PAL_SEEN 8192
static struct { const float *pal; unsigned gen, off, stamp; } g_palSeen[PAL_SEEN]; static unsigned g_palStamp;   /* this frame's shared poses, and where their bones went */

static void *pkAlloc(size_t n, size_t align, unsigned *off) {
    size_t o = (g_pk.memUsed + align - 1) & ~(align - 1);
    if (o + n > PK_MEM) { static int said; if (!said) { said = 1; printf("[packet] ⛔ past its %u MB in one frame: the rest of this frame isn't drawn\n", PK_MEM >> 20); fflush(stdout); } return NULL; }
    g_pk.memUsed = o + n; *off = (unsigned)o; return g_pk.mem + o;
}

/* a list is full: once, loud, and the frame draws less */
static int pkRoom(int n, int max, const char *what) { if (n < max) return 1; static int said; if (!said) { said = 1; printf("[packet] ⛔ more than %d %s in one frame: the rest aren't drawn\n", max, what); fflush(stdout); } return 0; }

void gfxFrameBegin(void) {
    if (!g_pk.mem) { g_pk.mem = malloc(PK_MEM); g_pk.draws = malloc(sizeof(GfxDraw) * GFX_MAX_DRAWS); }
    g_pk.nDraw = g_pk.nLine = g_pk.nHud = g_pk.nFx = g_pk.nBeam = g_pk.nDecal = g_pk.nStrands = 0; g_pk.star.on = 0; g_pk.memUsed = 0; memset(&g_stNext, 0, sizeof g_stNext); g_live = 1; g_palStamp++;
    gfxBackBegin();
}

void gfxMesh(const GfxMesh *m) {
    if (!g_live || !pkRoom(g_pk.nDraw, GFX_MAX_DRAWS, "draws")) return;
    int skin = m->pal != NULL && m->bones > 0; if (m->bones > 256) return;
    GfxDraw *d = &g_pk.draws[g_pk.nDraw];
    d->id = m->id; memcpy(d->vb, m->vb, sizeof d->vb); d->nIndex = m->nIndex; d->nVerts = m->nVerts; d->tex = m->tex;
    memcpy(d->tint, m->tint, 12); d->useTex = m->useTex; d->flags = m->flags; d->alpha = m->alpha > 0.f ? m->alpha : 1.f; d->bones = skin ? m->bones : 0; d->palOff = 0;
    if (skin) { memcpy(d->model, m->model, 64);
        unsigned off; unsigned *seenOff = NULL;
        if (m->palShared) { unsigned k = (unsigned)(((size_t)m->pal >> 6) * 2654435761u + m->palShared) & (PAL_SEEN - 1);
            for (int q = 0; q < 32; q++, k = (k + 1) & (PAL_SEEN - 1)) { if (g_palSeen[k].stamp != g_palStamp) { g_palSeen[k].stamp = g_palStamp; g_palSeen[k].pal = m->pal; g_palSeen[k].gen = m->palShared; g_palSeen[k].off = ~0u; seenOff = &g_palSeen[k].off; break; }
                if (g_palSeen[k].pal == m->pal && g_palSeen[k].gen == m->palShared) { seenOff = &g_palSeen[k].off; break; } } }
        if (seenOff && *seenOff != ~0u) d->palOff = *seenOff;
        else { float *o = pkAlloc((size_t)m->bones * 48, 256, &off); if (!o) return; d->palOff = off; if (seenOff) *seenOff = off; const float *B = m->bind;
        for (unsigned b = 0; b < m->bones; b++) { const float *P = m->pal + b * 16; float T[16];
            if (B) { for (int c = 0; c < 4; c++) for (int r = 0; r < 3; r++) T[c * 4 + r] = P[0 * 4 + r] * B[c * 4 + 0] + P[1 * 4 + r] * B[c * 4 + 1] + P[2 * 4 + r] * B[c * 4 + 2] + P[3 * 4 + r] * B[c * 4 + 3]; P = T; }
            *o++ = P[0]; *o++ = P[4]; *o++ = P[8]; *o++ = P[12]; *o++ = P[1]; *o++ = P[5]; *o++ = P[9]; *o++ = P[13]; *o++ = P[2]; *o++ = P[6]; *o++ = P[10]; *o++ = P[14]; } } }
    else if (m->bind) { const float *A = m->model, *B = m->bind; for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) d->model[c * 4 + r] = A[0 * 4 + r] * B[c * 4 + 0] + A[1 * 4 + r] * B[c * 4 + 1] + A[2 * 4 + r] * B[c * 4 + 2] + A[3 * 4 + r] * B[c * 4 + 3]; }
    else memcpy(d->model, m->model, 64);
    d->posOn = 0; d->mouth[6] = 0.f; if (m->mouth) memcpy(d->mouth, m->mouth, 32);
    memcpy(d->mat, m->mat, 16); d->ntex = m->ntex; d->kind = m->kind; memcpy(d->eye, m->eye, 16); d->rest = m->rest; d->restScale = m->restScale; d->hue = m->hue; d->shader = m->shader;
    if (m->pos) { unsigned off2; void *o2 = pkAlloc((size_t)m->nVerts * 12, 16, &off2); if (o2) { memcpy(o2, m->pos, (size_t)m->nVerts * 12); d->posOn = 1; d->posOff = off2; } }
    { long n = m->nIndex ? m->nIndex : m->nVerts; if (m->flags & GFX_SHADOW) { g_stNext.sunDraws++; g_stNext.sunIndices += n; } else { g_stNext.draws++; g_stNext.indices += n; } }
    g_pk.nDraw++;
}

void gfxLines3D(const float *xyzrgb, int n) {
    if (!g_live || n <= 0 || !pkRoom(g_pk.nLine, 64, "sets of lines")) return;
    unsigned off; void *p = pkAlloc((size_t)n * 24, 16, &off); if (!p) return;
    memcpy(p, xyzrgb, (size_t)n * 24); g_pk.lines[g_pk.nLine].off = off; g_pk.lines[g_pk.nLine].n = n; g_pk.lines[g_pk.nLine].over = 0; g_pk.nLine++;
}
void gfxLines3DOver(const float *xyzrgb, int n) { const int k = g_pk.nLine; gfxLines3D(xyzrgb, n); if (g_pk.nLine > k) g_pk.lines[k].over = 1; }
void gfxLines3DSolid(const float *xyzrgb, int n) { const int k = g_pk.nLine; gfxLines3D(xyzrgb, n); if (g_pk.nLine > k) g_pk.lines[k].over = 2; }

void gfxHud(const float *xy, int n, float cw, float ch, float r, float g, float b, float a) {
    if (!g_live || n <= 0 || !pkRoom(g_pk.nHud, GFX_MAX_HUD, "2D sets")) return;
    unsigned off; void *p = pkAlloc((size_t)n * 8, 16, &off); if (!p) return;
    memcpy(p, xy, (size_t)n * 8); GfxHudRec *h = &g_pk.hud[g_pk.nHud++]; h->off = off; h->n = n; h->scr[0] = cw; h->scr[1] = ch; h->col[0] = r; h->col[1] = g; h->col[2] = b; h->tex = 0; h->col[3] = a;
}

void gfxSprites(unsigned tex, const float *v, int n, float cw, float ch) {
    if (!g_live || n <= 0 || !pkRoom(g_pk.nHud, GFX_MAX_HUD, "2D sets")) return;
    unsigned off; void *p = pkAlloc((size_t)n * 32, 16, &off); if (!p) return;
    memcpy(p, v, (size_t)n * 32); GfxHudRec *h = &g_pk.hud[g_pk.nHud++]; memset(h, 0, sizeof *h); h->off = off; h->n = n; h->scr[0] = cw; h->scr[1] = ch; h->tex = tex;
}

#ifndef REVA_VK
void gfxMemSay(void) {}
void gfxToonSet(float a, float b, float c, float d, float e, const float *f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
#endif

/* ---- names: a buffer or a texture is a number, in both builds ------------------------------------------------------------------------- */
/* ⛔ 11-04: a name comes back when it's dropped, the oldest first and only once there's a good few, because the renderer's tables are as long as the
 * highest name. Without this, a world that streams made them longer for as long as it ran. Names are never 0: the engine reads 0 as "none" */
#define NAME_RING 65536u
typedef struct { unsigned q[NAME_RING]; unsigned r, w, next; } NameRing;
static NameRing g_bufNames = {.next = 1}, g_texNames = {.next = 1};
static unsigned nameTake(NameRing *R) { return R->w - R->r > 256u ? R->q[R->r++ % NAME_RING] : R->next++; }
static void nameBack(NameRing *R, unsigned n) { if (n && R->w - R->r < NAME_RING) R->q[R->w++ % NAME_RING] = n; }
unsigned gfxBufferNew(void) { return nameTake(&g_bufNames); }
unsigned gfxTextureNew(void) { return nameTake(&g_texNames); }
void gfxBuffersNew(int n, unsigned *names) { for (int i = 0; i < n; i++) names[i] = nameTake(&g_bufNames); }
void gfxTexturesNew(int n, unsigned *names) { for (int i = 0; i < n; i++) names[i] = nameTake(&g_texNames); }
void gfxBufferDrop(unsigned name) { if (!name) return; gfxBufferFree(name); nameBack(&g_bufNames, name); }
void gfxTextureDrop(unsigned name) { if (!name) return; gfxTextureFree(name); nameBack(&g_texNames, name); }
void gfxBuffersDrop(int n, const unsigned *names) { for (int i = 0; i < n; i++) gfxBufferDrop(names[i]); }
void gfxTexturesDrop(int n, const unsigned *names) { for (int i = 0; i < n; i++) gfxTextureDrop(names[i]); }
void gfxFxTrisView(const float *v, int n, int how, int under, int view) {
    if (!g_live || n <= 0 || !pkRoom(g_pk.nFx, 64, "effects")) return;
    unsigned off; void *p = pkAlloc((size_t)n * 36, 16, &off); if (!p) return;
    memcpy(p, v, (size_t)n * 36); GfxFx *f = &g_pk.fx[g_pk.nFx++]; f->off = off; f->n = n; f->how = how; f->under = under; f->vb = 0; f->view = view; f->tex = 0;
}
void gfxFxTex(const float *v, int n, unsigned tex) { const int k = g_pk.nFx; gfxFxTrisView(v, n, GFX_FX_PUFF, 0, -1); if (g_pk.nFx > k) g_pk.fx[k].tex = tex; }
void gfxFxTris(const float *v, int n, int how, int under) { gfxFxTrisView(v, n, how, under, -1); }

void gfxSkyPoints(unsigned buffer, const float *pts, int n, int under) {
    if (!g_live || n <= 0 || !pkRoom(g_pk.nFx, 64, "effects")) return;
    unsigned off = 0; if (!buffer) { void *p = pkAlloc((size_t)n * 20, 16, &off); if (!p) return; memcpy(p, pts, (size_t)n * 20); }
    GfxFx *f = &g_pk.fx[g_pk.nFx++]; f->off = off; f->n = n; f->how = GFX_FX_SKY; f->under = under; f->vb = buffer; f->view = -1; f->tex = 0;
}

void gfxBeam(const float *d) { if (g_live && pkRoom(g_pk.nBeam, 64, "beams")) memcpy(g_pk.beam[g_pk.nBeam++], d, 96); }
void gfxDecal(const GfxDecal *d) { if (g_live && pkRoom(g_pk.nDecal, GFX_MAX_DECALS, "decals")) g_pk.decal[g_pk.nDecal++] = *d; }

void gfxStar(const GfxStar *s) { if (g_live) g_pk.star = *s; }
void gfxStrands(const GfxStrands *s) { if (g_live && pkRoom(g_pk.nStrands, GFX_MAX_STRANDS, "hair systems")) g_pk.strands[g_pk.nStrands++] = *s; }   /* (11-06) hair on the GPU */

void gfxStats(GfxStats *s) { *s = g_st; }

static unsigned long long fnv(unsigned long long h, const void *p, size_t n) { const unsigned char *b = p; while (n--) { h ^= *b++; h *= 1099511628211ull; } return h; }

static unsigned long long pkHash(const GfxView *v) {
    unsigned long long h = 14695981039346656037ull;
    for (int i = 0; i < v->nViews; i++) { h = fnv(h, v->views[i].vp, 64); h = fnv(h, v->views[i].eye, 12); h = fnv(h, &v->views[i].x, 16); }
    h = fnv(h, v->vp, 64); h = fnv(h, v->sunVP, 64); h = fnv(h, v->eye, 12); h = fnv(h, v->sunDir, 12); h = fnv(h, &v->sunI, 4); h = fnv(h, v->sunCol, 12); h = fnv(h, v->sky, 12); h = fnv(h, v->fog, 12); h = fnv(h, &v->fogD, 4); h = fnv(h, v->clear, 12); h = fnv(h, &v->knee, 4); h = fnv(h, &v->shadowOn, 4); h = fnv(h, &v->cuts, 4); h = fnv(h, &v->reset, 4); if (v->exposure > 0.f) h = fnv(h, &v->exposure, 4); if (v->haze > 0.f) h = fnv(h, &v->haze, 4); if (v->nLights > 0) h = fnv(h, v->lights, (size_t)v->nLights * sizeof(GfxLight)); if (v->ground[0] > 0.f || v->ground[1] > 0.f || v->ground[2] > 0.f) h = fnv(h, v->ground, 12);
    for (int i = 0; i < g_pk.nDraw; i++) { const GfxDraw *d = &g_pk.draws[i];
        h = fnv(h, &d->id, 4); h = fnv(h, &d->nIndex, 4); h = fnv(h, &d->nVerts, 4); h = fnv(h, d->model, 64); h = fnv(h, d->tint, 12); h = fnv(h, &d->useTex, 4); h = fnv(h, &d->flags, 4); h = fnv(h, &d->bones, 4); if (d->alpha != 1.f) h = fnv(h, &d->alpha, 4);
        if (d->bones) h = fnv(h, g_pk.mem + d->palOff, (size_t)d->bones * 48);
        if (d->posOn) h = fnv(h, g_pk.mem + d->posOff, (size_t)d->nVerts * 12);
        if (d->mouth[6] > 0.f) h = fnv(h, d->mouth, 28); }
    for (int i = 0; i < g_pk.nBeam; i++) h = fnv(h, g_pk.beam[i], 96);
    for (int i = 0; i < g_pk.nDecal; i++) { h = fnv(h, g_pk.decal[i].m, 64); h = fnv(h, g_pk.decal[i].col, 16); h = fnv(h, &g_pk.decal[i].kind, 12); }   /* (step 24) a frame with none hashes as it did. A decal's picture is content, as a texture is */
    if (v->space) h = fnv(h, v->space, 288);
    if (v->filmGrade > 0.f || v->filmVig > 0.f) { h = fnv(h, &v->filmGrade, 4); h = fnv(h, &v->filmVig, 4); }
    if (v->gloss[3] > 0.f) h = fnv(h, v->gloss, 16);
    if (v->bars) h = fnv(h, &v->bars, 4);
    if (v->dof > 0.f || v->shutter > 0.f || v->sepia > 0.f || v->flash > 0.f || v->burn > 0.f || v->grain > 0.f || v->weave[0] != 0.f || v->weave[1] != 0.f) { h = fnv(h, &v->focus, 12); h = fnv(h, &v->sepia, 16); h = fnv(h, v->weave, 8); }   /* (step 25) the lens and film's look, when a frame has any */
    if (g_pk.star.on) { h = fnv(h, g_pk.star.c, sizeof g_pk.star.c * 3); h = fnv(h, g_pk.star.cut, 12); h = fnv(h, g_pk.star.spot, sizeof g_pk.star.spot); }
    for (int i = 0; i < g_pk.nLine; i++) h = fnv(h, g_pk.mem + g_pk.lines[i].off, (size_t)g_pk.lines[i].n * 24);
    for (int i = 0; i < g_pk.nHud; i++) { const GfxHudRec *r = &g_pk.hud[i]; h = fnv(h, g_pk.mem + r->off, (size_t)r->n * (r->tex ? 32 : 8)); h = fnv(h, r->scr, 8); h = fnv(h, r->col, 16); }
    for (int i = 0; i < g_pk.nFx; i++) { const GfxFx *f = &g_pk.fx[i]; h = fnv(h, &f->n, 4); h = fnv(h, &f->how, 4); h = fnv(h, &f->under, 4); if (f->view >= 0) h = fnv(h, &f->view, 4); if (!f->vb) h = fnv(h, g_pk.mem + f->off, (size_t)f->n * (f->how == GFX_FX_SKY ? 20 : 36)); }
    return h;
}

static void pkDump(const char *path, const GfxView *v, unsigned long long hash) {
    FILE *f = fopen(path, "w"); if (!f) return;
    fprintf(f, "tick %ld hash %016llx cuts %u reset %d\neye %.6f %.6f %.6f sun %.6f %.6f %.6f %.6f shadow %d clear %.4f %.4f %.4f\n", v->tick, hash, v->cuts, v->reset, v->eye[0], v->eye[1], v->eye[2], v->sunDir[0], v->sunDir[1], v->sunDir[2], v->sunI, v->shadowOn, v->clear[0], v->clear[1], v->clear[2]);
    fprintf(f, "%d draws\n", g_pk.nDraw);
    for (int i = 0; i < g_pk.nDraw; i++) { const GfxDraw *d = &g_pk.draws[i]; unsigned long long ph = d->bones ? fnv(14695981039346656037ull, g_pk.mem + d->palOff, (size_t)d->bones * 48) : 0;
        fprintf(f, "%4d id %08x kind %u owner %u mesh %u idx %u verts %u bones %u flags %d tex %u use %.3f tint %.3f %.3f %.3f at %.4f %.4f %.4f pal %016llx vb %u\n", i, d->id, d->id >> 28, (d->id >> 12) & 0xffff, d->id & 0xfff, d->nIndex, d->nVerts, d->bones, d->flags, d->tex, d->useTex, d->tint[0], d->tint[1], d->tint[2], d->model[12], d->model[13], d->model[14], ph, d->vb[0]); }
    fprintf(f, "%d line sets\n", g_pk.nLine);
    for (int i = 0; i < g_pk.nLine; i++) fprintf(f, "%4d %d vertices\n", i, g_pk.lines[i].n);
    fprintf(f, "%d effects\n", g_pk.nFx);
    for (int i = 0; i < g_pk.nFx; i++) fprintf(f, "%4d how %d under %d %d %s\n", i, g_pk.fx[i].how, g_pk.fx[i].under, g_pk.fx[i].n, g_pk.fx[i].vb ? "points from a buffer" : "inline");
    fprintf(f, "%d hud sets\n", g_pk.nHud);
    for (int i = 0; i < g_pk.nHud; i++) fprintf(f, "%4d %d vertices canvas %.0f %.0f colour %.3f %.3f %.3f %.3f first %.2f %.2f\n", i, g_pk.hud[i].n, g_pk.hud[i].scr[0], g_pk.hud[i].scr[1], g_pk.hud[i].col[0], g_pk.hud[i].col[1], g_pk.hud[i].col[2], g_pk.hud[i].col[3], ((float *)(g_pk.mem + g_pk.hud[i].off))[0], ((float *)(g_pk.mem + g_pk.hud[i].off))[1]);
    fclose(f);
}

/* ---- (step 25) THE PICTURES BETWEEN TWO TICKS ------------------------------------------------------------------------------------------------------------------------------
 * The rules tick 60 times a second on every machine, so on a 100 or 144 Hz display there were 60 new pictures a second, each held for one refresh or two: a judder on every
 * pan. With this on, the last two ticks' packets are kept whole, and every refresh draws the picture that lies between them where the clock says: a draw is paired with the
 * same draw of the tick before by its stable id, and its matrix, its bones, its own positions and its colour are mixed, and so is the lens. What has no pair is the later
 * tick's. It costs a tick of delay, and a copy of the packet a tick. */
typedef struct { GfxPacket pk; GfxView v; GfxLight *lights; float space[72]; size_t memCap; int ok; } PkKept;
static PkKept g_kept[2]; static int g_keptB, g_btwOn; static float g_btwT;
static GfxPacket g_mix; static GfxView g_mixV; static size_t g_mixCap; static unsigned char *g_mixSeen; static int g_mixPaired, g_mixDraws;
static void keptCopy(PkKept *k, const GfxPacket *p, const GfxView *v) {
    if (k->memCap < p->memUsed || !k->pk.mem) { k->memCap = p->memUsed + (1u << 20); k->pk.mem = realloc(k->pk.mem, k->memCap); }
    if (!k->pk.draws) k->pk.draws = malloc(sizeof(GfxDraw) * GFX_MAX_DRAWS);
    if (!k->lights) k->lights = malloc(sizeof(GfxLight) * GFX_MAX_LIGHTS);
    unsigned char *mem = k->pk.mem; GfxDraw *dr = k->pk.draws;
    k->pk = *p; k->pk.mem = mem; k->pk.draws = dr; memcpy(mem, p->mem, p->memUsed); memcpy(dr, p->draws, sizeof(GfxDraw) * (size_t)p->nDraw);
    k->v = *v; { int n = v->nLights < 0 ? 0 : v->nLights > GFX_MAX_LIGHTS ? GFX_MAX_LIGHTS : v->nLights; if (n) memcpy(k->lights, v->lights, sizeof(GfxLight) * (size_t)n); k->v.lights = k->lights; k->v.nLights = n; }
    if (v->space) { memcpy(k->space, v->space, sizeof k->space); k->v.space = k->space; }
    k->ok = 1;
}
#define MIX_SLOTS 32768
static int g_mixSlot[MIX_SLOTS];
static void mixBuild(float t) {
    const PkKept *A = &g_kept[1 - g_keptB], *B = &g_kept[g_keptB];
    if (g_mixCap < B->pk.memUsed || !g_mix.mem) { g_mixCap = B->pk.memUsed + (1u << 20); g_mix.mem = realloc(g_mix.mem, g_mixCap); }
    if (!g_mix.draws) g_mix.draws = malloc(sizeof(GfxDraw) * GFX_MAX_DRAWS);
    if (!g_mixSeen) g_mixSeen = malloc(65536);
    unsigned char *mem = g_mix.mem; GfxDraw *dr = g_mix.draws;
    g_mix = B->pk; g_mix.mem = mem; g_mix.draws = dr; memcpy(mem, B->pk.mem, B->pk.memUsed); memcpy(dr, B->pk.draws, sizeof(GfxDraw) * (size_t)B->pk.nDraw); g_mixV = B->v; g_mix.nStrands = 0;   /* (11-06) hair steps once a tick: a picture between two draws it as it was */
    g_mixPaired = 0; g_mixDraws = g_mix.nDraw;
    if (!A->ok || t >= 0.999f || A->v.cuts != B->v.cuts || B->v.reset || A->v.nViews != B->v.nViews || B->pk.nDraw > GFX_MAX_DRAWS) return;
    if (t < 0.f) t = 0.f;
    const float s = 1.f - t;
#define MIXF(dst, a, b, n) do { for (int q_ = 0; q_ < (n); q_++) (dst)[q_] = (a)[q_] * s + (b)[q_] * t; } while (0)
    MIXF(g_mixV.vp, A->v.vp, B->v.vp, 16); MIXF(g_mixV.eye, A->v.eye, B->v.eye, 3);
    for (int i = 0; i < B->v.nViews && i < 16; i++) if (A->v.views[i].w == B->v.views[i].w && A->v.views[i].h == B->v.views[i].h) { MIXF(g_mixV.views[i].vp, A->v.views[i].vp, B->v.views[i].vp, 16); MIXF(g_mixV.views[i].eye, A->v.views[i].eye, B->v.views[i].eye, 3); }
    /* the tick before's draws, by id: the same id twice (a body's copy in a mirror, its shadow) is taken in the order it was drawn */
    memset(g_mixSlot, 0xff, sizeof g_mixSlot);
    for (int i = A->pk.nDraw - 1; i >= 0; i--) { const GfxDraw *d = &A->pk.draws[i]; unsigned k = ((d->id * 2654435761u) ^ ((unsigned)d->flags * 40503u)) & (MIX_SLOTS - 1);
        for (int q = 0; q < MIX_SLOTS; q++, k = (k + 1) & (MIX_SLOTS - 1)) if (g_mixSlot[k] < 0) { g_mixSlot[k] = i; break; } }
    memset(g_mixSeen, 0, 65536);
    for (int i = 0; i < g_mix.nDraw; i++) { GfxDraw *d = &g_mix.draws[i]; const GfxDraw *a = NULL; unsigned k = ((d->id * 2654435761u) ^ ((unsigned)d->flags * 40503u)) & (MIX_SLOTS - 1);
        for (int q = 0; q < 64 && g_mixSlot[k] != -1; q++, k = (k + 1) & (MIX_SLOTS - 1)) { if (g_mixSlot[k] < 0) continue; const GfxDraw *c = &A->pk.draws[g_mixSlot[k]];
            if (c->id == d->id && c->flags == d->flags && c->nIndex == d->nIndex && c->nVerts == d->nVerts && c->bones == d->bones) { a = c; g_mixSlot[k] = -2; break; } }
        if (!a) continue;
        g_mixPaired++;
        MIXF(d->model, a->model, d->model, 16); MIXF(d->tint, a->tint, d->tint, 3); d->alpha = a->alpha * s + d->alpha * t; d->useTex = a->useTex * s + d->useTex * t;
        if (d->bones && d->palOff + (size_t)d->bones * 48 <= B->pk.memUsed && a->palOff + (size_t)a->bones * 48 <= A->pk.memUsed && !g_mixSeen[(d->palOff >> 8) & 65535u]) { g_mixSeen[(d->palOff >> 8) & 65535u] = 1;   /* a pose many bodies share is mixed once */
            float *pb = (float *)(mem + d->palOff); const float *pa = (const float *)(A->pk.mem + a->palOff); const int n = (int)d->bones * 12; for (int q = 0; q < n; q++) pb[q] = pa[q] * s + pb[q] * t; }
        if (d->posOn && a->posOn && d->posOff + (size_t)d->nVerts * 12 <= B->pk.memUsed && a->posOff + (size_t)a->nVerts * 12 <= A->pk.memUsed) {
            float *pb = (float *)(mem + d->posOff); const float *pa = (const float *)(A->pk.mem + a->posOff); const int n = (int)d->nVerts * 3; for (int q = 0; q < n; q++) pb[q] = pa[q] * s + pb[q] * t; } }
#undef MIXF
}
void gfxBetweenSay(int *paired, int *draws) { *paired = g_mixPaired; *draws = g_mixDraws; }
void gfxBetweenSet(int on, float t) { if (!on && g_btwOn) g_kept[0].ok = g_kept[1].ok = 0; g_btwOn = on; g_btwT = t; }
void gfxBetween(void) {
    if (!g_btwOn || !g_kept[g_keptB].ok) return;
    gfxBackBegin(); gfxBackBetween(); mixBuild(g_btwT); gfxBackEnd(&g_mix, &g_mixV);
}
void gfxFrameEnd(const GfxView *v) {
    if (!g_live) return;
    g_live = 0; g_st = g_stNext;
    static long every = -1, dumpTick = -1; static const char *dumpPath;
    if (every < 0) { const char *e = getenv("REVA_PACKETHASH"); if (!e) e = getenv("REVA_SIMHASH"); every = e ? atol(e) : 0; const char *d = getenv("REVA_PACKETDUMP"); if (d && strchr(d, ':')) { dumpTick = atol(d); dumpPath = strchr(d, ':') + 1; } }
    if ((every > 0 && v->tick % every == 0) || (dumpTick >= 0 && v->tick >= dumpTick)) { unsigned long long h = pkHash(v);
        if (every > 0 && v->tick % every == 0) printf("[packet] tick %ld %016llx draws %d sun %d hud %d lines %d mem %zu\n", v->tick, h, g_st.draws, g_st.sunDraws, g_pk.nHud, g_pk.nLine, g_pk.memUsed);
        if (dumpTick >= 0 && v->tick >= dumpTick) { pkDump(dumpPath, v, h); dumpTick = -1; } }
    if (g_btwOn) { g_keptB ^= 1; keptCopy(&g_kept[g_keptB], &g_pk, v); mixBuild(g_btwT); memcpy(g_mix.strands, g_pk.strands, sizeof(GfxStrands) * (size_t)g_pk.nStrands); g_mix.nStrands = g_pk.nStrands; gfxBackEnd(&g_mix, &g_mixV); }   /* (step 25) the picture between the tick before and this one */
    else gfxBackEnd(&g_pk, v);
}

#ifdef REVA_HEADLESS
/* no renderer: the packet is all there is. What the engine hands a renderer (geometry, textures, targets, Holly's head) goes nowhere, and a frame read back is none */
void gfxTextureNear(unsigned name) { (void)name; }
void gfxTextureHud(unsigned name) { (void)name; }
int gfxShaderNew(const uint32_t *spv, int words, int kind) { (void)spv; (void)words; (void)kind; return 0; }
void gfxSkyShader(int shader) { (void)shader; }   /* (11-12) a surface's own shader: none here, so its meshes are drawn plainly */
void gfxShaderTexture(int shader, int binding, int w, int h, int layers, const void *rgba, int flags) { (void)shader; (void)binding; (void)w; (void)h; (void)layers; (void)rgba; (void)flags; }
void gfxShaderDrop(int shader) { (void)shader; }
void gfxBackBegin(void) {}
void gfxBackEnd(const GfxPacket *p, const GfxView *v) { (void)p; (void)v; }
void gfxBufferData(unsigned name, long size, const void *data, int dynamic) { (void)name; (void)size; (void)data; (void)dynamic; }
void gfxBufferSub(unsigned name, long off, long size, const void *data) { (void)name; (void)off; (void)size; (void)data; }
void gfxTexture(unsigned name, int w, int h, const void *rgba) { (void)name; (void)w; (void)h; (void)rgba; }
void gfxTextureSub(unsigned name, int x, int y, int w, int h, const void *rgba) { (void)name; (void)x; (void)y; (void)w; (void)h; (void)rgba; }
void gfxTargetMake(unsigned tex, int w, int h, int nearest) { (void)tex; (void)w; (void)h; (void)nearest; }
void gfxHead(const GfxHead *h) { (void)h; }
int gfxRead(void *rgba, int w, int h) { (void)rgba; (void)w; (void)h; return 0; }
int gfxReadLag(void *rgba, int w, int h) { (void)rgba; (void)w; (void)h; return 0; }   /* (11-12) nothing drawn, nothing a frame late */
void gfxShotSize(int *w, int *h) { (void)w; (void)h; }
int gfxResize(int *W, int *H) { (void)W; (void)H; return 0; }
int gfxPointer(float u, float v, float *px, float *py) { (void)u; (void)v; (void)px; (void)py; return 0; }
float gfxShownAspect(void) { return 0.f; }
void gfxBackBetween(void) {}
void gfxBufferFree(unsigned name) { (void)name; }
int gfxCompute(void) { return 0; }
int gfxXrOn(void) { return 0; }   /* (11-11) no headset with no display */
int gfxXrFrame(GfxXrEye eye[2], GfxXrHand hand[2]) { (void)eye; (void)hand; return 0; }
void gfxXrEyeSize(int *w, int *h) { *w = *h = 0; }
void gfxXrBuzz(int hand, float amount, float seconds) { (void)hand; (void)amount; (void)seconds; }
int gfxXrQuit(void) { return 0; }
float gfxXrScale(void) { return 0.f; }
int gfxBufferRead(unsigned name, void *data, long size) { (void)name; (void)data; (void)size; return 0; }
void gfxBufferGpu(unsigned name, long size, const void *data) { (void)name; (void)size; (void)data; }
void gfxTextureFree(unsigned name) { (void)name; }
void gfxUploadAsync(int on) { (void)on; }
double gfxWaited(void) { return 0.0; }
double gfxIdleWaited(void) { return 0.0; }
void gfxDrain(void) { }
int gfxDutyOwn(void) { return 0; }
int gfxLines(void) { return -1; }
unsigned gfxVblankSeq(void) { return 0u; }
double gfxVblankAt(void) { return 0.0; }
double gfxFieldRate(void) { return 0.0; }
#endif
