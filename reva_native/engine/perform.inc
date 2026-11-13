/* ---- (11-08) THE PERFORMER: a body playing an instrument, live (THE REVA BAND: design/reva_band_gdd.md, section 14) ---------------------------------------
 * A game gives a body's limbs TARGETS in time, as notes come in (the note bus), and the engine poses
 * the body to meet them, over whatever its clip gives (a drummer seated, a keyboard player standing):
 *   perfLimb(body, limb, x, y, z, t, lift): limb 0 the left hand, 1 the right, 2 the left foot, 3 the right. (x, y, z) is in the body's own frame, in metres: x to
 *     its LEFT, y up from its feet's rest floor, z to its front (ride.inc's frame). The limb's end arrives there at time t on E->noteTime's clock, travelling from its
 *     last target on a stroke lifted `lift` metres at the top (less when there isn't time: a drum stroke rises as far as the gap allows), and stays until the next.
 *     The arm or leg reaches it with its two bones (ride.inc's rideReach), the elbows out and back, the knees forward.
 *   perfFinger(body, hand, finger, t, dur, amount): one of a hand's fingers (0 the thumb .. 4 the little) pressed down by `amount` (0..1) at t, for dur seconds.
 *   perfOff(body): the body back to its clip alone.
 * ⛔ WHAT'S DRAWN, NEVER THE RULES: posed in the frame (main.inc, beside rideBodyPose), on the note bus's clock; nothing reaches [simhash].
 * ⛔ AN ANDROID MAY GO WHERE A WOMAN CAN'T, ONLY ROUND HER OWN JOINTS (the GDD's pillar 2): the reach is two bones from the shoulder or the hip, nothing stretched. */
#define PF_BODIES 8
#define PF_Q 96
typedef struct { float p[3]; double t; float lift; } PfTgt;
typedef struct { PfTgt q[PF_Q]; int n; PfTgt from; int has; } PfLimb;
typedef struct { double t; float dur, amt; } PfPress;
typedef struct { int body; PfLimb limb[4]; PfPress fin[2][5][8]; char stream[16]; int keepRoot; } PfBody;
static PfBody g_pf[PF_BODIES]; static int g_pfN = 0;
static PfBody *pfFind(int body, int make){
    for(int i = 0; i < g_pfN; i++) if(g_pf[i].body == body) return &g_pf[i];
    if(!make || g_pfN >= PF_BODIES || body < 0) return NULL;
    PfBody *b = &g_pf[g_pfN++]; memset(b, 0, sizeof *b); b->body = body; return b; }
static void apiPerfLimb(int body, int limb, float x, float y, float z, double t, float lift){
    if(limb < 0 || limb > 3 || !isfinite(x) || !isfinite(y) || !isfinite(z) || !isfinite(t) || !isfinite(lift)) return;
    PfBody *b = pfFind(body, 1); if(!b) return; PfLimb *L = &b->limb[limb];
    if(L->n >= PF_Q){ memmove(L->q, L->q + 1, sizeof(PfTgt) * (PF_Q - 1)); L->n--; }
    int k = L->n; while(k > 0 && L->q[k - 1].t > t){ L->q[k] = L->q[k - 1]; k--; }   /* kept in time order */
    L->q[k].p[0] = x; L->q[k].p[1] = y; L->q[k].p[2] = z; L->q[k].t = t; L->q[k].lift = lift; L->n++; }
static void apiPerfFinger(int body, int hand, int finger, double t, float dur, float amount){
    if(hand < 0 || hand > 1 || finger < 0 || finger > 4 || !isfinite(t) || !isfinite(dur) || !isfinite(amount)) return;
    PfBody *b = pfFind(body, 1); if(!b) return; PfPress *f = b->fin[hand][finger];
    int w = 0; for(int i = 1; i < 8; i++) if(f[i].t + f[i].dur < f[w].t + f[w].dur) w = i;   /* over the one that ended first */
    f[w].t = t; f[w].dur = dur; f[w].amt = amount < 0.f ? 0.f : (amount > 1.f ? 1.f : amount); }
static void apiPerfOff(int body){ for(int i = 0; i < g_pfN; i++) if(g_pf[i].body == body){ g_pf[i] = g_pf[--g_pfN]; return; } }
/* (11-08) THE POSE STREAM ON A BODY (engine/notebus.inc keeps the frames): this body plays the frames streamed under `name`, the takes' solver's poses, as a
 * clip's keys would set them; keepRoot 1 leaves the pack's root nodes (no parent) to the body's own place and turn. NULL or "" stops it */
static void apiPerfStream(int body, const char *name, int keepRoot){
    PfBody *b = pfFind(body, name && name[0]); if(!b) return; snprintf(b->stream, sizeof b->stream, "%s", name ? name : ""); b->keepRoot = keepRoot; }
static int psPose(const PfBody *b, Pack *P, float Q[][4], float T[][3], double now){
    const PsStream *s = psFind(b->stream, 0);
    int k = 0; while(s && k < s->n && s->f[k].t <= now) k++;              /* f[k-1] at or before now, f[k] after */
    /* (11-09) REVA_PSDBG=1, a check: every 10 s, how many of a player's pictures had the solver's pose (the rest showed the clip), and how far ahead its frames reach */
    { static int dbg = -1; if(dbg < 0) dbg = getenv("REVA_PSDBG") ? 1 : 0;
      if(dbg){ static int tot[PS_STREAMS], ok[PS_STREAMS]; static double last[PS_STREAMS]; const int si = s ? (int)(s - g_ps) : 0;
          const int has = s && s->n && k > 0 && k < s->n && s->f[k].t - s->f[k - 1].t <= 0.5; tot[si]++; ok[si] += has;
          if(now - last[si] >= 10.0){ if(last[si] > 0.0){ printf("[pose] %s: the solver's pose in %d of %d pictures, frames reach %.2f s ahead\n", b->stream, ok[si], tot[si], s && s->n ? s->f[s->n - 1].t - now : 0.0); fflush(stdout); }
              last[si] = now; tot[si] = ok[si] = 0; } } }
    if(!s || s->n == 0) return 0;
    if(k == 0 || k == s->n) return 0;                                     /* nothing yet, or the solver's behind: the clip shows */
    const PsFrame *a = &s->f[k - 1], *c = &s->f[k]; if(c->t - a->t > 0.5) return 0;   /* a gap: not across it */
    const float u = (float)((now - a->t) / (c->t - a->t > 1e-6 ? c->t - a->t : 1e-6));
    for(int j = 0; j < a->nq; j++){ const int i = a->qi[j]; if(i >= P->nNodes || (b->keepRoot && P->nodes[i].parent < 0)) continue;
        const float *q0 = a->q[j], *q1 = q0; for(int m = 0; m < c->nq; m++) if(c->qi[m] == i){ q1 = c->q[m]; break; }
        const float dt = q0[0] * q1[0] + q0[1] * q1[1] + q0[2] * q1[2] + q0[3] * q1[3], sg = dt < 0.f ? -1.f : 1.f; float r[4], l = 0.f;
        for(int c4 = 0; c4 < 4; c4++){ r[c4] = q0[c4] + (sg * q1[c4] - q0[c4]) * u; l += r[c4] * r[c4]; }
        l = sqrtf(l); if(l < 1e-6f) continue; for(int c4 = 0; c4 < 4; c4++) Q[i][c4] = r[c4] / l; }
    for(int j = 0; j < a->nt; j++){ const int i = a->ti[j]; if(i >= P->nNodes || (b->keepRoot && P->nodes[i].parent < 0)) continue;
        const float *t0 = a->tv[j], *t1 = t0; for(int m = 0; m < c->nt; m++) if(c->ti[m] == i){ t1 = c->tv[m]; break; }
        for(int c3 = 0; c3 < 3; c3++) T[i][c3] = t0[c3] + (t1[c3] - t0[c3]) * u; }
    return 1; }
/* where a limb's end is to be now: on its way from the last target it reached to the next, lifted at the middle, eased into the strike */
static int pfWhere(PfLimb *L, double now, float out[3]){
    while(L->n > 0 && L->q[0].t <= now){ L->from = L->q[0]; L->has = 1; memmove(L->q, L->q + 1, sizeof(PfTgt) * (size_t)(L->n - 1)); L->n--; }
    if(!L->has && L->n == 0) return 0;
    if(L->n == 0 || !L->has){ const PfTgt *s = L->has ? &L->from : &L->q[0]; memcpy(out, s->p, 12); return 1; }
    const PfTgt *a = &L->from, *b = &L->q[0]; const double gap = b->t - a->t;
    float u = gap > 1e-4 ? (float)((now - a->t) / gap) : 1.f; u = u < 0.f ? 0.f : (u > 1.f ? 1.f : u);
    const float e = u * u * (3.f - 2.f * u);
    const float lift = fminf(b->lift, (float)gap * 1.6f);                /* a quick stroke can't rise far: 1.6 m a second at most */
    const float up = lift * sinf(3.14159265f * fminf(1.f, u * 1.15f));   /* up early, down onto the strike */
    for(int k = 0; k < 3; k++) out[k] = a->p[k] + (b->p[k] - a->p[k]) * e;
    out[1] += up; return 1; }
static void perfBodyPose(int si, Pack *P, float Q[][4], float T[][3]){
    PfBody *b = pfFind(si, 0); if(!b || P->pbOK <= 0) return;
    const double now = apiNoteTime(); const float m = RIDE_U * ((P->scale > 1e-6f) ? (1.f / P->scale) : 1.f), floor0 = P->mny; float dq[4];
    if(b->stream[0]){ psPose(b, P, Q, T, now); return; }                  /* the takes' solver plays it: nothing of the quick reach below */
    for(int side = 0; side < 2; side++){                                  /* the legs first, then the arms over the body that results */
        PfLimb *L = &b->limb[2 + side]; float w[3]; if(!pfWhere(L, now, w)) continue;
        const int th = P->pb[side ? 14 : 12], kn = P->pb[side ? 15 : 13], ft = side ? P->footR : P->footL;
        const float tg[3] = {w[0] * m, floor0 + w[1] * m, w[2] * m}, pole[3] = {side ? -0.2f : 0.2f, 0.f, 1.f};
        rideReach(P, Q, T, th, kn, ft, tg, pole); }
    for(int side = 0; side < 2; side++){
        PfLimb *L = &b->limb[side]; float w[3]; if(!pfWhere(L, now, w)) continue;
        const int up = P->pb[side ? 10 : 7], fo = P->pb[side ? 11 : 8], ha = side ? P->gripNode : P->gripL; if(up < 0 || fo < 0 || ha < 0) continue;
        const float sg = side ? -1.f : 1.f, tg[3] = {w[0] * m, floor0 + w[1] * m, w[2] * m}, pole[3] = {sg * 0.7f, -0.5f, -0.5f};   /* elbows out, down, back */
        rideReach(P, Q, T, up, fo, ha, tg, pole);
        const int mid = P->pb[16 + side * 15 + 2 * 3];                    /* the hand forward and down, from where its middle finger starts */
        if(mid >= 0){ float H[3], M3[3], u[3]; rideWorld(P, Q, T, ha, H, NULL, NULL); rideWorld(P, Q, T, mid, M3, NULL, NULL); for(int k = 0; k < 3; k++) u[k] = M3[k] - H[k];
            const float ul = rideLen(u); if(ul > 1e-6f){ float v[3] = {0.f, -0.45f, 1.f}; const float vl = rideLen(v); for(int k = 0; k < 3; k++){ v[k] /= vl; u[k] /= ul; } qarc(dq, u, v); rideTurn(P, Q, T, ha, dq); } } }
    for(int side = 0; side < 2; side++) for(int f = 0; f < 5; f++){         /* fingers: a press curls the finger down, its tip onto the key */
        float a = 0.f; for(int i = 0; i < 8; i++){ const PfPress *p = &b->fin[side][f][i]; if(p->dur <= 0.f) continue;
            const double s = now - p->t; if(s < -0.03 || s > p->dur + 0.05) continue;
            const float in = (float)fmin(1.0, (s + 0.03) / 0.03), out = (float)fmin(1.0, fmax(0.0, (p->dur + 0.05 - s) / 0.05));
            a = fmaxf(a, p->amt * fminf(in, out)); }
        for(int j = 0; j < 3; j++){ const int bo = P->pb[16 + side * 15 + f * 3 + j]; if(bo < 0) continue;
            qaxis(dq, 1.f, 0.f, 0.f, (f ? 0.18f : 0.10f) + a * (f ? 0.55f : 0.35f)); rideTurn(P, Q, T, bo, dq); } } }
