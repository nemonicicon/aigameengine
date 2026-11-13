/* THE SET'S DRAWINGS (11-11, design/facade_video.md): a set whose pictures are DRAWN, frame by frame, against the scene's clock.
 * The video this was built for is made the way a "plates" video is made: every frame a function of song time, a whiteboard that gets a creature
 * drawn on it for each line, the line's words written and printed as part of the picture. The engine could place a prop per frame (the
 * set's timeline, settrack.inc) and letter a caption; it could not draw: no pen stroke, no flat shape, no picture laid over the frame or
 * behind the bodies, nothing that's revealed as a pen moves. Now a set says
 *
 *   art <file> [seconds]           play <file>'s drawings; the scene's clock plus [seconds] is the file's clock (a take that starts mid-song).
 *                                  `trackt=` (the lab's hand on the timeline) holds the drawings at that moment too
 *
 * The drawings are written by code, as the light show is (skate_src/band/artfile.py), and the engine only plays them. Nothing here lays
 * out a word or eases a move: the file says where everything is in every frame. WHAT A FILE HOLDS, drawn once and used by any frame:
 *   pictures   image files (PNG and what stb_image reads), by path beside the file. A sheet of letters is a picture
 *   strokes    a pen's path: points on the canvas. Drawn with a width, round ends and a soft edge, and from any share of its length to
 *              any other, so it can be DRAWN in time
 *   groups     strokes in the order a hand would draw them, each with its own width and colour: a creature, a written line. Drawn up to
 *              a share of the whole group's length, pen at one speed
 *   meshes     flat triangles: a filled shape (its edge is hard: outline it with a stroke)
 *   planes     places in the world a drawing can lie on: where the canvas's top left corner is, and the world's step for one canvas unit
 *              across and one down. A whiteboard is a plane
 * and then, for every frame, its ITEMS in order, each 18 floats:
 *   kind (1 quad, 2 stroke, 3 mesh, 4 group), layer, id, | a b c d tx ty | p0 p1 p2 p3 | r g b a | p4
 *   The six numbers place it: canvas = (a*x + c*y + tx, b*x + d*y + ty) from its own x and y. A quad's own shape is the square 0..1.
 *   quad:   id is a picture (from 1) or 0 for a flat colour; p0..p3 are the picture's u0 v0 u1 v1; p4 a soft edge's width, 0 for none
 *   stroke: p0 its width on the canvas, p1 and p2 the shares of its length it's drawn from and to, p3 its soft edge (0: one unit)
 *   group:  each stroke's own width is in the group's own units, and is scaled as the six numbers scale the group (a drawing grows
 *           whole), times p0; p1 and p2 the shares of the whole group's length, p3 the soft edge; r g b a times each stroke's own colour
 * THE CANVAS is 1920 x 1080, top left 0,0, y down, whatever the picture's size: it's fitted to the picture's height and centred.
 * LAYERS: 0 OVER the finished picture, where the captions are (nothing of the world's light or lens touches it). 1 UNDER the world: on
 * the frame, behind everything solid, in front of the sky: a backdrop the bodies stand against. 2 + k ON PLANE k, in the world: bodies in
 * front of it cover it. ⛔ Under and on a plane, a picture gives its ALPHA only, coloured by the item's colour (the world's own
 * triangles carry a colour, and a texture's alpha: gfxFxTris, gfxFxTex), which is what a sheet of letters wants; and those two layers go
 * through the scene's light path (its exposure and its post), where layer 0 is exactly the colours the file says.
 * A set with no `art` line never comes here. Dressing: it draws, and no rule reads it. */
#define ART_CW 1920.f
#define ART_CH 1080.f
#define ART_TEX 64
#define ART_PLANES 32
#define ART_ITEM 18
typedef struct { int n; float *xy, *len; } ArtStroke;                  /* len: how far along the path each point is */
typedef struct { int n; float *xy; } ArtMesh;
typedef struct { int n; int *id; float *wc; float total; float *at; } ArtGroup;   /* wc: width r g b a per stroke; at: the group's length before each stroke */
static struct { int on, frames, nTex, nStroke, nMesh, nGroup, nPlane, last; float fps, off;
                unsigned tex[ART_TEX], white; ArtStroke *stroke; ArtMesh *mesh; ArtGroup *group; float plane[ART_PLANES][9];
                float *items; size_t *at; int *cnt; int fr; int said; } g_art;
static float *g_artV = NULL; static size_t g_artVN = 0, g_artVCap = 0;   /* this batch's canvas vertices, 8 floats each: x y u v r g b a */
static int g_artExact;   /* (11-13) `art <file> [seconds] exact`: the world's layers in the file's own colours, out of the scene's light (GFX_FX_EXACT) */
static float *g_artW = NULL; static size_t g_artWCap = 0;                /* and the same batch in the world, 9 floats each */

static void artDrop(void){
    if(!g_art.on && !g_art.items) return;
    for(int i = 0; i < g_art.nTex; i++) if(g_art.tex[i]) gfxTextureDrop(g_art.tex[i]);
    for(int i = 0; i < g_art.nStroke; i++){ free(g_art.stroke[i].xy); free(g_art.stroke[i].len); }
    for(int i = 0; i < g_art.nMesh; i++) free(g_art.mesh[i].xy);
    for(int i = 0; i < g_art.nGroup; i++){ free(g_art.group[i].id); free(g_art.group[i].wc); free(g_art.group[i].at); }
    free(g_art.stroke); free(g_art.mesh); free(g_art.group); free(g_art.items); free(g_art.at); free(g_art.cnt);
    const unsigned w9 = g_art.white; memset(&g_art, 0, sizeof g_art); g_art.white = w9; g_art.last = -1; }

static int artRdI(FILE *f, int *v){ return fread(v, 4, 1, f) == 1; }
static int artNum(const float *d, size_t n){ for(size_t i = 0; i < n; i++) if(!(d[i] > -1e8f && d[i] < 1e8f)) return 0; return 1; }   /* every float a number of a world's size */
static void artLoad(const char *path, float off){
    artDrop();
    FILE *f = fopen(path, "rb");
    if(!f){ printf("[art] CANNOT OPEN %s -- the set has no drawings\n", path); fflush(stdout); return; }
    char hd[160]; float fps = 30.f; int fr = 0, nt = 0, ns = 0, nm = 0, ng = 0, np = 0, ver = 0; const char *why = "its first line";
    if(!fgets(hd, sizeof hd, f) || sscanf(hd, "ART%d %f %d %d %d %d %d %d", &ver, &fps, &fr, &nt, &ns, &nm, &ng, &np) != 8 || ver != 1 || !(fps > 0.f && fps < 1000.f)
       || fr <= 0 || fr > 4000000 || nt < 0 || nt > ART_TEX || ns < 0 || ns > 1000000 || nm < 0 || nm > 100000 || ng < 0 || ng > 100000 || np < 0 || np > ART_PLANES) goto bad;
    g_art.fps = fps; g_art.off = off; g_art.frames = fr; g_art.last = -1;
    { char dir[300]; snprintf(dir, sizeof dir, "%s", path); char *sl = strrchr(dir, '/'); char *bs = strrchr(dir, '\\'); if(bs > sl) sl = bs; if(sl) sl[1] = 0; else dir[0] = 0;
      why = "its pictures";
      for(int i = 0; i < nt; i++){ char nm9[300], full[620]; if(!fgets(nm9, sizeof nm9, f)) goto bad;
          size_t L = strlen(nm9); while(L && (nm9[L - 1] == '\n' || nm9[L - 1] == '\r')) nm9[--L] = 0;
          const int abs9 = nm9[0] == '/' || nm9[0] == '\\' || (nm9[0] && nm9[1] == ':'); snprintf(full, sizeof full, "%s%s", abs9 ? "" : dir, nm9);
          int w = 0, h = 0, c = 0; stbi_set_flip_vertically_on_load(0); unsigned char *px = stbi_load(full, &w, &h, &c, 4);
          g_art.nTex = i + 1;
          if(!px){ printf("[art] %s: picture %d, %s, CANNOT BE READ: what uses it is drawn as a flat colour\n", path, i + 1, full); g_art.tex[i] = 0; continue; }
          g_art.tex[i] = gfxTextureNew(); gfxTexture(g_art.tex[i], w, h, px); gfxTextureHud(g_art.tex[i]); stbi_image_free(px); } }
    why = "its strokes";
    g_art.stroke = (ArtStroke *)calloc((size_t)(ns ? ns : 1), sizeof(ArtStroke)); if(!g_art.stroke) goto bad;
    for(int i = 0; i < ns; i++){ int n = 0; if(!artRdI(f, &n) || n < 1 || n > 1000000) goto bad;
        ArtStroke *s = &g_art.stroke[i]; g_art.nStroke = i + 1; s->n = n; s->xy = (float *)malloc((size_t)n * 8); s->len = (float *)malloc((size_t)n * 4);
        if(!s->xy || !s->len || fread(s->xy, 8, (size_t)n, f) != (size_t)n || !artNum(s->xy, (size_t)n * 2)) goto bad;
        s->len[0] = 0.f; for(int k = 1; k < n; k++){ const float dx = s->xy[k * 2] - s->xy[k * 2 - 2], dy = s->xy[k * 2 + 1] - s->xy[k * 2 - 1]; s->len[k] = s->len[k - 1] + sqrtf(dx * dx + dy * dy); } }
    why = "its meshes";
    g_art.mesh = (ArtMesh *)calloc((size_t)(nm ? nm : 1), sizeof(ArtMesh)); if(!g_art.mesh) goto bad;
    for(int i = 0; i < nm; i++){ int n = 0; if(!artRdI(f, &n) || n < 3 || n > 3000000 || n % 3) goto bad;
        ArtMesh *m = &g_art.mesh[i]; g_art.nMesh = i + 1; m->n = n; m->xy = (float *)malloc((size_t)n * 8);
        if(!m->xy || fread(m->xy, 8, (size_t)n, f) != (size_t)n || !artNum(m->xy, (size_t)n * 2)) goto bad; }
    why = "its groups";
    g_art.group = (ArtGroup *)calloc((size_t)(ng ? ng : 1), sizeof(ArtGroup)); if(!g_art.group) goto bad;
    for(int i = 0; i < ng; i++){ int n = 0; if(!artRdI(f, &n) || n < 1 || n > 100000) goto bad;
        ArtGroup *g = &g_art.group[i]; g_art.nGroup = i + 1; g->n = n; g->id = (int *)malloc((size_t)n * 4); g->wc = (float *)malloc((size_t)n * 20); g->at = (float *)malloc((size_t)n * 4);
        if(!g->id || !g->wc || !g->at) goto bad;
        float tot = 0.f;
        for(int k = 0; k < n; k++){ if(!artRdI(f, &g->id[k]) || fread(g->wc + k * 5, 4, 5, f) != 5 || g->id[k] < 0 || g->id[k] >= ns || !artNum(g->wc + k * 5, 5)) goto bad;
            g->at[k] = tot; tot += g_art.stroke[g->id[k]].len[g_art.stroke[g->id[k]].n - 1]; }
        g->total = tot; }
    why = "its planes";
    if(np && (fread(g_art.plane, 36, (size_t)np, f) != (size_t)np || !artNum(&g_art.plane[0][0], (size_t)np * 9))) goto bad;
    g_art.nPlane = np;
    why = "its frames";
    g_art.at = (size_t *)malloc((size_t)fr * sizeof(size_t)); g_art.cnt = (int *)malloc((size_t)fr * sizeof(int)); if(!g_art.at || !g_art.cnt) goto bad;
    { size_t cap = 4096, used = 0; g_art.items = (float *)malloc(cap * sizeof(float)); if(!g_art.items) goto bad;
      for(int i = 0; i < fr; i++){ int n = 0; if(!artRdI(f, &n) || n < 0 || n > 200000) goto bad;
          const size_t need = (size_t)n * ART_ITEM;
          if(used + need > cap){ while(used + need > cap) cap *= 2; float *q = (float *)realloc(g_art.items, cap * sizeof(float)); if(!q) goto bad; g_art.items = q; }
          if(need && (fread(g_art.items + used, 4, need, f) != need || !artNum(g_art.items + used, need))) goto bad;
          g_art.at[i] = used; g_art.cnt[i] = n; used += need; }
      printf("[art] %s: %d frames at %.0f, %d pictures, %d strokes, %d meshes, %d groups, %d planes, %.1f MB of frames, starting at %.2f s\n", path, fr, fps, nt, ns, nm, ng, np, (double)used * 4.0 / 1048576.0, off); }
    fclose(f); g_art.on = 1; fflush(stdout);
    if(!g_art.white){ unsigned char w16[4 * 4 * 4]; memset(w16, 255, sizeof w16); g_art.white = gfxTextureNew(); gfxTexture(g_art.white, 4, 4, w16); gfxTextureHud(g_art.white); }
    return;
bad:
    printf("[art] %s is not an ART1 file, or is short or holds what isn't a number: stopped at %s -- the set has no drawings\n", path, why); fflush(stdout);
    fclose(f); artDrop(); }

/* which frame of the file this moment is: the timeline's own rule (settrack.inc trackApply), so the two never part */
static void artApply(void){
    if(!g_art.on) return;
    float st = g_stkScrub >= 0.f ? g_stkScrub : (g_scnT > 0.f ? g_scnT : 0.f) + g_art.off;
    if(g_stkScrub < -1.5f) st = (float)fmod(now_ms() * 0.001 - g_stkPlay0, (double)g_art.frames / (double)g_art.fps);
    int fr = (int)(st * g_art.fps + 0.5f); if(fr < 0) fr = 0; if(fr >= g_art.frames) fr = g_art.frames - 1;
    g_art.fr = fr; }

static float *artRoom(size_t verts){
    if(g_artVN + verts * 8 > g_artVCap){ size_t cap = g_artVCap ? g_artVCap : 65536; while(g_artVN + verts * 8 > cap) cap *= 2;
        float *q = (float *)realloc(g_artV, cap * sizeof(float)); if(!q) return NULL; g_artV = q; g_artVCap = cap; }
    float *o = g_artV + g_artVN; g_artVN += verts * 8; return o; }
static inline float *artVert(float *o, float x, float y, float u, float v, const float *c, float a){ o[0] = x; o[1] = y; o[2] = u; o[3] = v; o[4] = c[0]; o[5] = c[1]; o[6] = c[2]; o[7] = a; return o + 8; }
static inline float *artQuad4(float *o, const float *p0, const float *p1, const float *p2, const float *p3, const float *c, float a0, float a1, float a2, float a3){
    o = artVert(o, p0[0], p0[1], 0.5f, 0.5f, c, a0); o = artVert(o, p1[0], p1[1], 0.5f, 0.5f, c, a1); o = artVert(o, p2[0], p2[1], 0.5f, 0.5f, c, a2);
    o = artVert(o, p0[0], p0[1], 0.5f, 0.5f, c, a0); o = artVert(o, p2[0], p2[1], 0.5f, 0.5f, c, a2); o = artVert(o, p3[0], p3[1], 0.5f, 0.5f, c, a3); return o; }

/* a pen's path on the canvas, from `s0` to `s1` along it: a ribbon `w` wide whose edge fades over `fe`, mitred where it turns, with round ends.
 * P: the path's own points; m: the six numbers that place it. A width under the soft edge is drawn that much fainter, as a hair is */
static void artRibbon(const ArtStroke *S, const float *m, float w, float s0, float s1, float fe, const float *col){
    const int n = S->n; if(n < 1) return;
    const float L = S->len[n - 1]; if(s0 < 0.f) s0 = 0.f; if(s1 > L) s1 = L; if(!(s1 >= s0)) return;
    if(fe <= 0.f) fe = 1.f;
    float a = col[3]; float hw = w * 0.5f - fe * 0.5f; if(hw < 0.02f){ a *= fmaxf(0.f, fminf(1.f, w / fe)); hw = 0.02f; }
    if(a <= 0.f) return;
    const float ow = hw + fe;
    /* the points of the part that's drawn, on the canvas */
    int i0 = 0; while(i0 < n - 1 && S->len[i0 + 1] <= s0) i0++;
    int i1 = i0; while(i1 < n - 1 && S->len[i1 + 1] < s1) i1++;
    const int cnt = (n == 1 || L <= 0.f) ? 1 : i1 - i0 + 2;
    static float *q = NULL; static int qCap = 0; if(cnt > qCap){ qCap = cnt + 256; q = (float *)realloc(q, (size_t)qCap * 16); if(!q){ qCap = 0; return; } }
    float *nr = q + (size_t)qCap * 2;                                   /* q: the points; nr: each point's way across, already as long as its mitre needs */
    #define ART_PT(k, x, y) { q[(k) * 2] = m[0] * (x) + m[2] * (y) + m[4]; q[(k) * 2 + 1] = m[1] * (x) + m[3] * (y) + m[5]; }
    if(cnt == 1){ ART_PT(0, S->xy[0], S->xy[1]) }
    else { int k = 0;
        for(int i = i0; i <= i1 + 1 && i < n; i++){ float x = S->xy[i * 2], y = S->xy[i * 2 + 1];
            if(i == i0 && S->len[i] < s0 && i + 1 < n){ const float d = S->len[i + 1] - S->len[i], t = d > 0.f ? (s0 - S->len[i]) / d : 0.f; x += (S->xy[i * 2 + 2] - x) * t; y += (S->xy[i * 2 + 3] - y) * t; }
            if(i == i1 + 1 && S->len[i] > s1){ const float d = S->len[i] - S->len[i - 1], t = d > 0.f ? (s1 - S->len[i - 1]) / d : 1.f; x = S->xy[i * 2 - 2] + (x - S->xy[i * 2 - 2]) * t; y = S->xy[i * 2 - 1] + (y - S->xy[i * 2 - 1]) * t; }
            ART_PT(k, x, y) k++; } }
    #undef ART_PT
    int np = cnt;
    { int k = 1; for(int i = 1; i < np; i++){ const float dx = q[i * 2] - q[k * 2 - 2], dy = q[i * 2 + 1] - q[k * 2 - 1]; if(dx * dx + dy * dy > 1e-6f || i == np - 1){ q[k * 2] = q[i * 2]; q[k * 2 + 1] = q[i * 2 + 1]; k++; } } np = k;   /* no two points on one spot */
      if(np == 2){ const float dx = q[2] - q[0], dy = q[3] - q[1]; if(dx * dx + dy * dy <= 1e-6f) np = 1; } }
    const int CAP = 8;                                                  /* wedges in a round end */
    float tx0 = 1.f, ty0 = 0.f, tx1 = 1.f, ty1 = 0.f;                     /* the way the pen is going at its first point and its last */
    if(np >= 2){
        for(int i = 0; i < np; i++){ float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
            if(i > 0){ ax = q[i * 2] - q[i * 2 - 2]; ay = q[i * 2 + 1] - q[i * 2 - 1]; const float l = sqrtf(ax * ax + ay * ay); ax /= l; ay /= l; }
            if(i < np - 1){ bx = q[i * 2 + 2] - q[i * 2]; by = q[i * 2 + 3] - q[i * 2 + 1]; const float l = sqrtf(bx * bx + by * by); bx /= l; by /= l; }
            if(i == 0){ ax = bx; ay = by; tx0 = bx; ty0 = by; } if(i == np - 1){ bx = ax; by = ay; tx1 = ax; ty1 = ay; }
            float mx = ax + bx, my = ay + by; const float ml = sqrtf(mx * mx + my * my);
            if(ml < 1e-4f){ mx = ax; my = ay; } else { mx /= ml; my /= ml; }
            float cs = mx * ax + my * ay; if(cs < 0.5f) cs = 0.5f;       /* a mitre no longer than twice the width: a sharp turn is cut short */
            nr[i * 2] = -my / cs; nr[i * 2 + 1] = mx / cs; }
        float *o = artRoom((size_t)(np - 1) * 18); if(!o) return;
        for(int i = 0; i < np - 1; i++){ const float *A = q + i * 2, *B = A + 2, *na = nr + i * 2, *nb = na + 2;
            const float aoL[2] = {A[0] + na[0] * ow, A[1] + na[1] * ow}, acL[2] = {A[0] + na[0] * hw, A[1] + na[1] * hw}, acR[2] = {A[0] - na[0] * hw, A[1] - na[1] * hw}, aoR[2] = {A[0] - na[0] * ow, A[1] - na[1] * ow};
            const float boL[2] = {B[0] + nb[0] * ow, B[1] + nb[1] * ow}, bcL[2] = {B[0] + nb[0] * hw, B[1] + nb[1] * hw}, bcR[2] = {B[0] - nb[0] * hw, B[1] - nb[1] * hw}, boR[2] = {B[0] - nb[0] * ow, B[1] - nb[1] * ow};
            o = artQuad4(o, aoL, boL, bcL, acL, col, 0.f, 0.f, a, a); o = artQuad4(o, acL, bcL, bcR, acR, col, a, a, a, a); o = artQuad4(o, acR, bcR, boR, aoR, col, a, a, 0.f, 0.f); } }
    /* its ends: half a disc each, or a whole dot for a path that's one point */
    for(int e = 0; e < 2; e++){ const float *C = e ? q + (np - 1) * 2 : q; float dx = e ? tx1 : -tx0, dy = e ? ty1 : -ty0; const int wedges = np == 1 ? CAP * 2 : CAP;
        if(np == 1 && e) break;
        float *o = artRoom((size_t)wedges * 9); if(!o) return;
        const float base = atan2f(dy, dx) - 1.5707963f, span = np == 1 ? 6.2831853f : 3.1415927f;
        for(int k = 0; k < wedges; k++){ const float a0 = base + span * (float)k / (float)wedges, a1 = base + span * (float)(k + 1) / (float)wedges;
            const float c0 = cosf(a0), s0v = sinf(a0), c1 = cosf(a1), s1v = sinf(a1);
            const float p0[2] = {C[0] + c0 * hw, C[1] + s0v * hw}, p1[2] = {C[0] + c1 * hw, C[1] + s1v * hw}, o0[2] = {C[0] + c0 * ow, C[1] + s0v * ow}, o1[2] = {C[0] + c1 * ow, C[1] + s1v * ow};
            o = artVert(o, C[0], C[1], 0.5f, 0.5f, col, a); o = artVert(o, p0[0], p0[1], 0.5f, 0.5f, col, a); o = artVert(o, p1[0], p1[1], 0.5f, 0.5f, col, a);
            o = artQuad4(o, p0, o0, o1, p1, col, a, 0.f, 0.f, a); } } }

/* 4 x 4, column-major, by cofactors: the lens's matrix back out, for a canvas that hangs behind the world */
static int artInv4(const float *m, float *o){
    float v[16];
    v[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    v[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    v[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    v[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    v[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    v[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    v[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    v[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    v[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    v[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    v[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    v[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    v[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    v[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    v[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    v[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * v[0] + m[1] * v[4] + m[2] * v[8] + m[3] * v[12];
    if(!(fabsf(det) > 1e-30f)) return 0;
    for(int i = 0; i < 16; i++) o[i] = v[i] / det;
    return 1; }

/* this batch goes to the renderer: over the picture as the captions go, or into the world as its own triangles */
static const float *g_artInv = NULL;
static void artFlush(int layer, unsigned tex){
    const size_t nv = g_artVN / 8; g_artVN = 0; if(!nv || g_noDraw) return;
    const float asp = g_aspect > 0.f ? g_aspect : 16.f / 9.f, cw = ART_CH * asp, xo = (cw - ART_CW) * 0.5f;
    if(layer == 0){ if(xo != 0.f) for(size_t i = 0; i < nv; i++) g_artV[i * 8] += xo;
        gfxSprites(tex ? tex : g_art.white, g_artV, (int)nv, cw, ART_CH); return; }
    if(nv * 9 > g_artWCap){ float *q = (float *)realloc(g_artW, nv * 9 * sizeof(float)); if(!q) return; g_artW = q; g_artWCap = nv * 9; }
    if(layer == 1){ if(!g_artInv) return;
        for(size_t i = 0; i < nv; i++){ const float *s = g_artV + i * 8; float *d = g_artW + i * 9;
            const float nx = (s[0] - ART_CW * 0.5f) / (ART_CH * 0.5f * asp), ny = 1.f - s[1] / (ART_CH * 0.5f), nz = 0.999f;   /* close in front of the far plane */
            const float *I = g_artInv; const float w = I[3] * nx + I[7] * ny + I[11] * nz + I[15], iw = w != 0.f ? 1.f / w : 0.f;
            d[0] = (I[0] * nx + I[4] * ny + I[8] * nz + I[12]) * iw; d[1] = (I[1] * nx + I[5] * ny + I[9] * nz + I[13]) * iw; d[2] = (I[2] * nx + I[6] * ny + I[10] * nz + I[14]) * iw;
            d[3] = s[4]; d[4] = s[5]; d[5] = s[6]; d[6] = s[7]; d[7] = s[2]; d[8] = s[3]; } }
    else { const float *P = g_art.plane[layer - 2];
        for(size_t i = 0; i < nv; i++){ const float *s = g_artV + i * 8; float *d = g_artW + i * 9;
            d[0] = P[0] + P[3] * s[0] + P[6] * s[1]; d[1] = P[1] + P[4] * s[0] + P[7] * s[1]; d[2] = P[2] + P[5] * s[0] + P[8] * s[1];
            d[3] = s[4]; d[4] = s[5]; d[5] = s[6]; d[6] = s[7]; d[7] = s[2]; d[8] = s[3]; } }
    if(tex) gfxFxTex(g_artW, (int)nv, tex);
    else gfxFxTrisView(g_artW, (int)nv, g_artExact ? GFX_FX_EXACT : GFX_FX_ALPHA, layer == 1, -1); }

/* this frame's items on one layer (or, with `world`, on every layer but 0), in the file's order */
static void artDraw(int world){
    if(!g_art.on || g_art.fr < 0 || g_art.fr >= g_art.frames) return;
    const float *it = g_art.items + g_art.at[g_art.fr]; const int n = g_art.cnt[g_art.fr];
    int curL = -1; unsigned curT = 0; g_artVN = 0;
    for(int i = 0; i < n; i++, it += ART_ITEM){
        const int kind = (int)(it[0] + 0.5f), layer = (int)(it[1] + 0.5f), id = (int)(it[2] + 0.5f);
        if((layer == 0) == (world != 0)) continue;
        if(layer < 0 || layer >= 2 + g_art.nPlane){ if(!g_art.said){ g_art.said = 1; printf("[art] an item on layer %d: the file has %d planes (layers 0, 1, and 2 to %d)\n", layer, g_art.nPlane, 1 + g_art.nPlane); } continue; }
        const float *m = it + 3, *p = it + 9, *c = it + 13;
        unsigned tex = 0; if(kind == 1 && id >= 1 && id <= g_art.nTex) tex = g_art.tex[id - 1];
        if(layer != curL || tex != curT){ if(curL >= 0) artFlush(curL, curT); curL = layer; curT = tex; }
        if(kind == 1){                                                   /* a quad: a flat colour or a picture, its own shape the square 0..1 */
            float P[4][2]; const float uv[4][2] = {{p[0], p[1]}, {p[2], p[1]}, {p[2], p[3]}, {p[0], p[3]}}; static const float cr[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for(int k = 0; k < 4; k++){ P[k][0] = m[0] * cr[k][0] + m[2] * cr[k][1] + m[4]; P[k][1] = m[1] * cr[k][0] + m[3] * cr[k][1] + m[5]; }
            float *o = artRoom(6); if(!o) break;
            static const int ix[6] = {0, 1, 2, 0, 2, 3};
            for(int k = 0; k < 6; k++){ const int j = ix[k]; o = artVert(o, P[j][0], P[j][1], tex ? uv[j][0] : 0.5f, tex ? uv[j][1] : 0.5f, c, c[3]); }
            const float fe = it[17];
            if(fe > 0.f && !tex){                                        /* a flat quad's soft edge: a strip out from each side, fading */
                float O[4][2]; for(int k = 0; k < 4; k++){ const int a9 = (k + 3) & 3, b9 = (k + 1) & 3;      /* each corner pushed out along both its sides' ways out */
                    float e0x = P[k][0] - P[a9][0], e0y = P[k][1] - P[a9][1], e1x = P[k][0] - P[b9][0], e1y = P[k][1] - P[b9][1];
                    const float l0 = sqrtf(e0x * e0x + e0y * e0y), l1 = sqrtf(e1x * e1x + e1y * e1y); if(l0 > 0.f){ e0x /= l0; e0y /= l0; } if(l1 > 0.f){ e1x /= l1; e1y /= l1; }
                    O[k][0] = P[k][0] + (e0x + e1x) * fe; O[k][1] = P[k][1] + (e0y + e1y) * fe; }
                o = artRoom(24); if(!o) break;
                for(int k = 0; k < 4; k++){ const int b9 = (k + 1) & 3; o = artQuad4(o, P[k], O[k], O[b9], P[b9], c, c[3], 0.f, 0.f, c[3]); } } }
        else if(kind == 2){ if(id < 0 || id >= g_art.nStroke) continue;
            const ArtStroke *S = &g_art.stroke[id]; const float L = S->len[S->n - 1]; artRibbon(S, m, p[0], p[1] * L, p[2] * L, p[3], c); }
        else if(kind == 3){ if(id < 0 || id >= g_art.nMesh) continue;
            const ArtMesh *M = &g_art.mesh[id]; float *o = artRoom((size_t)M->n); if(!o) break;
            for(int k = 0; k < M->n; k++){ const float x = M->xy[k * 2], y = M->xy[k * 2 + 1]; o = artVert(o, m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5], 0.5f, 0.5f, c, c[3]); } }
        else if(kind == 4){ if(id < 0 || id >= g_art.nGroup) continue;
            const ArtGroup *G = &g_art.group[id]; const float g0 = p[1] * G->total, g1 = p[2] * G->total, sc = sqrtf(fabsf(m[0] * m[3] - m[1] * m[2])) * p[0];
            for(int k = 0; k < G->n; k++){ const ArtStroke *S = &g_art.stroke[G->id[k]]; const float L = S->len[S->n - 1], a0 = G->at[k];
                if(a0 + L < g0 || (a0 > g1 && L > 0.f) || (a0 >= g1 && g1 < G->total)) continue;
                const float *wc = G->wc + k * 5; const float col[4] = {wc[1] * c[0], wc[2] * c[1], wc[3] * c[2], wc[4] * c[3]};
                artRibbon(S, m, wc[0] * sc, g0 - a0, g1 - a0, p[3], col); } } }
    if(curL >= 0) artFlush(curL, curT); }

/* the two moments of a frame it's drawn at: with the world (behind it and in it), and over the finished picture */
static void artWorld(const float *vp){ if(!g_art.on || g_noDraw) return; float inv[16]; g_artInv = artInv4(vp, inv) ? inv : NULL; artDraw(1); g_artInv = NULL; }
static void artOver(void){ if(!g_art.on || g_noDraw) return; artDraw(0); }
