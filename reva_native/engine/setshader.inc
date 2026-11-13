/* setshader.inc -- ⭐ (11-12) A SURFACE'S OWN SHADER, given by a set (the Godot proof of concept's step 12: their Hedge Maze draws its hedges and
 * ground with shaders of its own). A set line
 *     shader <prop> <mesh name> <file.gshd>
 * draws every mesh of that name in that prop with the shader the file describes: a game's surface code, translated and compiled at its cook
 * (skate_src/godot/godot_shader.py; what the code may say is mesh.frag's GODOT_HOOK, and the renderer's side is gfx_shd.inc). The file is text:
 *     gshd 1
 *     abi <n>                                    the GFX_SHADER_ABI it was built against (mesh.frag's GODOT_HOOK_ABI): another is refused
 *     spv <file>                                 the SPIR-V
 *     tex <binding> <flags> <layers> <png> ...   a texture of its set 3: GFX_ST_ flags, how many pictures deep, and a picture for each layer
 * Paths are from the .gshd's own folder. A mesh keeps its plain material wherever there's no shader: the own rasterizer, the no-display build,
 * the sun's pass, a mirror, a picture drawn off-screen. The shaders go with the set (shdSetClear at the next setLoad). Picture, not rules:
 * nothing of it is in [simhash] or the packet's hash, and a machine that can't run one draws the plain material. */
#define SSH_MAX 64
typedef struct { char prop[64], mesh[96], file[192]; } SshLine;
static SshLine g_sshLine[SSH_MAX]; static int g_sshN;
static char g_sshSky[192];   /* a `skyshader <file.gshd>` line: the sky drawn with a game's own sky shader (skydome.frag's GODOT_SKY_HOOK) */
static int g_sshLive[SSH_MAX], g_sshLiveN;   /* the renderer's shaders this set made */
static char g_sshFile[SSH_MAX][192]; static int g_sshFileId[SSH_MAX], g_sshFileN;   /* (11-12, step 14) each file read once a set, the set's props and a game's sharing it */
static void shdSetClear(void) {
    for (int k = 0; k < g_setNP; k++) { Pack *P = &g_setPk[k]; if (P->meshShd) { free(P->meshShd); P->meshShd = NULL; } }   /* a pack can outlive its set: it forgets */
    gfxSkyShader(0);
    for (int i = 0; i < g_sshLiveN; i++) gfxShaderDrop(g_sshLive[i]);
    g_sshLiveN = 0; g_sshN = 0; g_sshSky[0] = 0; g_sshFileN = 0; }
static void shdSetLine(const char *ln) {
    if (!strncmp(ln, "skyshader ", 10)) { if (sscanf(ln, "skyshader %191s", g_sshSky) != 1) printf("[set] a skyshader line is: skyshader <file.gshd>\n"); return; }
    if (g_sshN >= SSH_MAX) { printf("[set] more than %d shader lines: the rest are drawn plainly\n", SSH_MAX); return; }
    SshLine *L = &g_sshLine[g_sshN];
    if (sscanf(ln, "shader %63s %95s %191s", L->prop, L->mesh, L->file) != 3) { printf("[set] a shader line is: shader <prop> <its meshes' name> <file.gshd>\n"); return; }
    g_sshN++; }
static unsigned char *shdFileBytes(const char *path, long most, long *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL; clubOpened(path);
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > most) { fclose(f); return NULL; }
    unsigned char *b = malloc((size_t)len); if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); fclose(f); return NULL; }
    fclose(f); *n = len; return b; }
/* one .gshd made into a renderer's shader: 0 when this machine has none to give, or the file isn't one */
static int shdLoad(const char *file, int kind) {
    FILE *f = fopen(file, "rb"); if (!f) { printf("[set] shader %s NOT FOUND: drawn plainly\n", file); return 0; } clubOpened(file);
    char dir[192]; snprintf(dir, sizeof dir, "%s", file); { char *s = strrchr(dir, '/'), *b = strrchr(dir, '\\'); if (b > s) s = b; if (s) s[1] = 0; else dir[0] = 0; }
    char ln[2048]; int ver = 0, abi = -1, id = 0, bad = 0; char spv[160] = "";
    if (fgets(ln, sizeof ln, f)) sscanf(ln, "gshd %d", &ver);
    if (ver != 1) { printf("[set] %s isn't a shader file (gshd 1)\n", file); fclose(f); return 0; }
    while (fgets(ln, sizeof ln, f)) {
        if (ln[0] == '#' || ln[0] == '\n' || ln[0] == '\r') continue;
        if (!txtNumsOk(ln, 1e6)) { printf("[set] %s: a line with a number in it that isn't one, left out\n", file); continue; }
        if (!strncmp(ln, "abi ", 4)) { sscanf(ln, "abi %d", &abi); continue; }
        if (!strncmp(ln, "kind ", 5)) { char k9[16] = ""; sscanf(ln, "kind %15s", k9); if ((kind == 1) != !strcmp(k9, "sky")) { printf("[set] %s is a %s shader, named as a %s\n", file, k9, kind == 1 ? "sky" : "surface"); bad = 1; break; } continue; }
        if (!strncmp(ln, "spv ", 4)) {
            sscanf(ln, "spv %159s", spv);
            if (abi != GFX_SHADER_ABI) { printf("[set] %s was cooked for the surface shaders' ABI %d, and this engine's is %d: cook it again. Drawn plainly\n", file, abi, GFX_SHADER_ABI); bad = 1; break; }
            char p[384]; snprintf(p, sizeof p, "%s%s", dir, spv); long n = 0; unsigned char *b = shdFileBytes(p, 8L << 20, &n);
            if (!b || n % 4 || n < 20 || ((const uint32_t *)b)[0] != 0x07230203u) { printf("[set] %s: its SPIR-V %s isn't there or isn't SPIR-V: drawn plainly\n", file, p); free(b); bad = 1; break; }
            id = gfxShaderNew((const uint32_t *)b, (int)(n / 4), kind); free(b);
            if (!id) break;   /* a machine with no surface shaders (the own rasterizer, the no-display build) reads no further */
            continue; }
        if (!strncmp(ln, "tex ", 4) && id) {
            int bind = -1, flags = 0, layers = 0, at = 0; if (sscanf(ln, "tex %d %d %d %n", &bind, &flags, &layers, &at) < 3 || bind < 0 || bind > 7 || layers < 1 || layers > 256) { printf("[set] %s: a tex line it can't read\n", file); continue; }
            const char *q = ln + at; unsigned char *all = NULL; int W = 0, H = 0, got = 0;
            for (int L = 0; L < layers; L++) { char nm[160]; int k = 0; while (*q == ' ' || *q == '\t') q++; while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r' && k < 159) nm[k++] = *q++; nm[k] = 0; if (!k) break;
                char p[384]; snprintf(p, sizeof p, "%s%s", dir, nm); int w = 0, h = 0, c = 0; clubOpened(p); stbi_set_flip_vertically_on_load(0); unsigned char *px = stbi_load(p, &w, &h, &c, 4);
                if (!px || w < 1 || h < 1 || w > 8192 || h > 8192 || (L && (w != W || h != H))) { printf("[set] %s: its picture %s isn't there, or isn't the size of the layer before\n", file, p); if (px) stbi_image_free(px); break; }
                if (!L) { W = w; H = h; all = malloc((size_t)W * H * 4 * (size_t)layers); if (!all) { stbi_image_free(px); break; } }
                memcpy(all + (size_t)L * W * H * 4, px, (size_t)W * H * 4); stbi_image_free(px); got++; }
            if (got == layers) gfxShaderTexture(id, bind, W, H, layers, all, flags);
            else { static const unsigned char one[4] = {255, 255, 255, 255}; gfxShaderTexture(id, bind, 1, 1, 1, one, flags); }   /* the binding still names a texture of the shape its code reads */
            free(all); continue; } }
    fclose(f);
    if (bad && id) { gfxShaderDrop(id); id = 0; }
    return id; }
/* a file's shader, read once a set */
static int shdFileId(const char *file) {
    for (int k = 0; k < g_sshFileN; k++) if (!strcmp(g_sshFile[k], file)) return g_sshFileId[k];
    const int id = shdLoad(file, 0);
    if (g_sshFileN < SSH_MAX) { snprintf(g_sshFile[g_sshFileN], 192, "%s", file); g_sshFileId[g_sshFileN++] = id; }
    if (id && g_sshLiveN < SSH_MAX) g_sshLive[g_sshLiveN++] = id;
    return id; }
/* one line's meshes in one pack take its shader */
static void shdLineApply(const SshLine *L, int pk) {
    Pack *P = &g_setPk[pk]; const int id = shdFileId(L->file); int n = 0;
    if (id > 0) { if (!P->meshShd) P->meshShd = calloc(P->nMeshes ? P->nMeshes : 1, sizeof(int));
        if (P->meshShd) for (unsigned m = 0; m < P->nMeshes; m++) { const char *nm = packStr(P, P->meshes[m].name); if (nm && !strcmp(nm, L->mesh)) { P->meshShd[m] = id; n++; } } }
    printf("[set] shader %s: %d mesh%s of %s named %s%s\n", L->file, n, n == 1 ? "" : "es", L->prop, L->mesh, id > 0 ? "" : " (drawn plainly here)"); }
/* (11-12, step 14) a prop a game asks for after the set's in (E->prop: Create a Map's pieces) takes the lines that name it then */
static void shdPropIn(int pk) { for (int i = 0; i < g_sshN; i++) if (!strcmp(g_setNm[pk], g_sshLine[i].prop)) shdLineApply(&g_sshLine[i], pk); }
/* after the set's props are in: each line's meshes take their shader */
static void shdSetResolve(void) {
    for (int i = 0; i < g_sshN; i++) { const SshLine *L = &g_sshLine[i]; int pk = -1;
        for (int k = 0; k < g_setNP; k++) if (!strcmp(g_setNm[k], L->prop)) { pk = k; break; }
        if (pk < 0) { printf("[set] shader %s: no prop %s yet: a prop a game asks for by that name takes it then\n", L->file, L->prop); continue; }
        shdLineApply(L, pk); }
    if (g_sshSky[0]) { const int id = shdLoad(g_sshSky, 1); if (id > 0) { gfxSkyShader(id); if (g_sshLiveN < SSH_MAX) g_sshLive[g_sshLiveN++] = id; }
        printf("[set] skyshader %s%s\n", g_sshSky, id > 0 ? "" : ": the engine's own sky here"); }
    fflush(stdout); }
