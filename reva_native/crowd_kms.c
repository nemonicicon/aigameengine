/* crowd_kms.c — THE CLUB's engine, one translation unit: a table of contents over the engine/ includes. Born 2026-07-22 as the
 * native dancer ON THE GLASS (DRM master on the Pi's card, the baked crowd page-flipped forever, under construction by
 * design on stream); since step 18 of the engine plan (11-04) the picture is Vulkan everywhere (gfx_vk.c), on SDL's window
 * or straight into the display card's buffers on the Pi, and the no-display build hashes the frame instead (gfx_packet.c).
 *   ./crowd <slots> <packdir> [pack|select]      (loads every .rpack in packdir, deals round-robin)
 * Build: driver.py build (Windows), build_linux.sh, build_pi.sh. Stop: SIGINT/SIGTERM.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
/* [linux-sdl port 2026-09-30] The Windows path was never Windows: it is "SDL2 supplies the
 * window, the renderer's surface and the pad", plus four Windows-only calls (_execv/_putenv/_chdir and
 * the PHOSPHOR handoff). REVA_SDL names the SDL platform; _WIN32 implies it, and -DREVA_SDL builds
 * the same path on Linux (a desktop, or SDL's own kmsdrm). Without it: the direct road on the Pi, DRM and Vulkan. */
#if defined(_WIN32) && !defined(REVA_SDL)
#define REVA_SDL 1
#endif
/* ⭐ REVA_HEADLESS (11-02, engine plan Phase 1): the SDL road with nothing on the end of it. No GPU, no window, no vsync, the same sim, and the frame
 * it asks for goes into the packet (gfx_packet.c) and is hashed there. Build: build_linux.sh headless, or driver.py check */
#ifdef REVA_HEADLESS
#define REVA_SDL 1
#endif
/* ⭐ REVA_SOFT (queue step 27, 11-05): the engine's own rasterizer on the processor (gfx_soft.c) where the Vulkan renderer would be. It answers the same calls, so
 * everything below that asks REVA_VK is asking whether a renderer is linked, and gets yes. On the SDL road only: driver.py buildsoft, build_linux.sh soft */
#if defined(REVA_SOFT) && !defined(REVA_VK)
#define REVA_VK 1
#endif
#if !defined(REVA_VK) && !defined(REVA_HEADLESS)
#error "build with -DREVA_VK (the Vulkan renderer), -DREVA_SOFT (the engine's own rasterizer) or -DREVA_HEADLESS (no display): there's no other picture"
#endif
#include <ctype.h>
/* [pi 2026-09-30] SDL on EVERY platform: the audio mixer and the scene WAVs (09-11) are SDL audio,
 * and were written while only the Windows build was being compiled -- the KMS build had been
 * dead since then. On the Pi's KMS path SDL is initialised for AUDIO only; the glass stays DRM. */
#include <SDL2/SDL.h>
#ifdef REVA_SDL
/* [win32 port 2026-08-21] The Pi presents by taking DRM master on card1 and flipping the renderer's
 * buffers itself. Windows has no KMS, so SDL2 supplies the window, the renderer's surface and the
 * gamepad; only the platform code differs. */
#ifdef _WIN32
#include <process.h>   /* _execv: the start menu's MAIN MENU relaunch */
#include <direct.h>    /* _chdir: the PHOSPHOR grade-A handoff */
#endif
#else
#include <xf86drm.h>
#include <xf86drmMode.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG   /* cast expansion: the Fab GLBs ship JPEG basecolors */
#include <stb/stb_image.h>
#include <stb/stb_easy_font.h>
#ifdef __ANDROID__
static void touchPoll(double tsec); static void touchDraw(void); static void touchScreen(int W,int H);   /* (11-06) engine/android.inc: the phone's touch controls */
#define ANDROID_TOUCHDRAW touchDraw();
#else
#define ANDROID_TOUCHDRAW
#endif
#include "holly_fbo.h"
/* ---- THE DICE (2026-09-30) ---------------------------------------------------------------
 * rand() is the C library's: glibc and msvcrt roll different sequences from the same seed, so
 * a bench-seeded show on Windows and the same show on a Pi were never the same show. One
 * 64-bit LCG (Knuth's MMIX constants), 31 bits out like rand(), identical on every platform.
 * REVA_RAND=libc puts the library's back, for re-checking a capture blessed before this. */
static unsigned long long g_dice=12345ull; static int g_diceLibc=-1;
static void clubSrand(unsigned s){ if(g_diceLibc<0){ const char*e=getenv("REVA_RAND"); g_diceLibc=(e&&!strcmp(e,"libc"))?1:0; }
    (srand)(s); g_dice=(unsigned long long)s*0x9E3779B97F4A7C15ull+0x2545F4914F6CDD1Dull; }
static int clubRand(void){ if(g_diceLibc<0) clubSrand(1); if(g_diceLibc) return (rand)();
    g_dice=g_dice*6364136223846793005ull+1442695040888963407ull; return (int)(g_dice>>33); }
#define rand() clubRand()
#define srand(s) clubSrand(s)   /* the head that goes on the venue screens */
/* Mouth state. Fixed until the UDP receiver lands -- the same three floats the
 * native renderer already takes, so the wire format needs no design. */
static float g_holly_open=0.45f, g_holly_wide=0.0f, g_holly_seal=0.0f;
/* ---- PLACED SCREENS (ROBOT NEKO ep 8) -------------------------------------------------
 * Holly's head, hung anywhere in the world instead of only on the venue's jumbotrons.
 * Each screen is x,y,z,yaw,pitch,halfW,halfH. `screen` in a .set places one; the scene
 * key `screen=` moves it per line, so a telly face-down in the dirt (pitch ~90) can be
 * RIGHTED on a cut rather than animated -- which is what a stop-motion animator does.
 * A screen with nothing behind it is a floating head, and that is allowed. */
#define SCR_MAX 4
static float g_scr[SCR_MAX][7];
static int   g_scrN=0;
static float g_camEye[3]={0,2,6};   /* where the lens is this frame (ep 8) */
static float g_scrAmp=2.8f;   /* ctl screenamp= : a screen emits, and his head is dim */

/* ⭐ MEASURED: this machine reports MAX_VERTEX_UNIFORM_VECTORS=4095, i.e. 1023 mat4.
 * 64 was a guess that happened to work and cost a Daz-class rig most of its FACE (57 of
 * 92 facial joints pruned to fit). 256 mat4 is 1024 vec4 -- a quarter of what is
 * available here -- and covers every model in the club: her body wants 124, the cat girl
 * 108, Anima 95. ⛔ The shaders' pal[] is stringified FROM this, so the two cannot
 * drift apart, and startup prints a warning if the hardware cannot carry it. */
/* ---- THE CAST (ROBOT NEKO ep 7) -------------------------------------------------
 * A scene seats SCN_MAXCAST slots. Slots 0..SCN_BODIES-1 carry a BODY; the last slot is
 * an off-screen VOICE with no body and no mouth (`cast 3=director`). These live up here,
 * not next to the scene code, because chainSim and the overlay/motion-energy arrays are
 * declared long before it and were carrying bare `[4]`s that nothing tied to the cap.
 * ⛔ EVERY array sized per cast slot uses SCN_MAXCAST. Growing this without growing them
 * is the ep 5 landmine (a cap sized "one per body" the moment there are two). */
#define SCN_MAXCAST 5
#define SCN_BODIES  (SCN_MAXCAST-1)   /* slots with a body: 0..3 */
#define SCN_VOICE   (SCN_MAXCAST-1)   /* the off-screen voice slot */
#define MAXB 256
#define MAXB_STR2(x) #x
#define MAXB_STR(x) MAXB_STR2(x)
/* ⭐ THE PRACTICAL COUNT, IN ONE PLACE. It used to be the literal 3 in four shader
 * locations and six C arrays, all of which had to agree by hand -- and a disagreement
 * does not fail, it lights every door wrongly. */
#define NLIGHT 6
/* ⭐ THE PICTURE'S SHAPE IS NOT THE FRAMEBUFFER'S (11-02). A composite raster's pixels are not square, and 720x240's are about
 * twice as wide as tall: W/H made the lens 3.0 wide at 240p, so it saw far more of the world than a tube shows, and 1.5 at 480i.
 * On composite the picture is 15:11 in all four modes: the 720 raster by BT.601, whose central 704 samples are 4:3. That is the
 * standard, never a measurement of one TV. REVA_ASPECT=0 is the control (the old W/H); REVA_ASPECT=<n> pins it. */
static float g_dispAspect=0.f;
static float g_fairNow=0.f;   /* (step 25) this frame the picture's held to a game's fair shape: its width over its height, 0 for the screen's own */
#define VIEW_ASPECT(W,H) (g_fairNow>0.f?g_fairNow:g_dispAspect>0.f?g_dispAspect:(float)(W)/(float)(H))
/* ⭐ ep 12: A HELD-PROP SPEC IS ONE STRING EVERYWHERE, AND IT OUTGREW 64. `v1_x_cigarette@0.16@0@middle_01_r:0.3@F2EFE8@
 * light:0.5:0.5@smoke` is 64 characters -- the 63 that fit ended in `@smok`, heldParse rejected the field, and
 * the cigarette never smoked, never registered a drag and never flared. (ep 4 hit the same wall at 32.) Every
 * buffer a spec passes through is this size, and a value that still does not fit is refused out loud. */
#define PROP_SPEC 160
#define NCANON 46   /* 16 body bones + 30 finger joints (09-11); slots 16.. are 16+side*15+finger*3+joint */
/* ⭐ THE CLIPPING AUDIT'S HOOKS (ROBOT NEKO ep 10) -- defined beside the scene clock, search
 * "THE CLIPPING AUDIT". Anything drawn on or with a cast body (a board's wheels, a deck's
 * corners, a held prop) feeds its world points here; they are tested at the end of the frame
 * against the floor, the set and every OTHER cast body. Free when the audit is off. */
void clipAuditPoint(int slot,const char*what,float x,float y,float z);
void clipAuditBox(int slot,const char*what,const float*model,const float*lo,const float*hi);
static void clipAuditSetLoaded(void);
static float frnd(void);
static volatile int g_run=1;
static void onsig(int s){ (void)s; g_run=0; }

/* ⭐ THE NO-DRAW PASS (11-02, engine plan Phase 1). A pass through the loop that only ticks puts nothing in the packet, and changes nothing else, so everything
 * the loop keeps stays true. See clockFrame. */
static int g_noDraw=0;
#include "gfx.h"      /* the renderer's face (engine plan, Phase 2): the draw sites write the packet (gfx_packet.c), Vulkan draws it, the no-display build hashes it */

#include "club.h"   /* the 4x4s (club_math.c) and the mixer (club_audio.c): the split, stage 1 and 2 (2026-09-30) */
#include "engine.h" /* what a game sees of all this (engine plan, Phase 3) */
#include "club_net.h" /* online play's wire (queue step 29): club_net.c, and engine/net.inc is the engine's side */
static const GameExport *g_game=NULL; static EngineTuning g_tune;
/* REVA_OPENLOG=<file> (11-02): every file the engine reads, once each, in order. A game's manifest is checked against this (anim_bench/cookcheck.py), because what a game
 * really needs is whatever it opens. Off, an open costs a branch. */
void clubOpened(const char*path){ static FILE*lg=NULL; static int on=-1,n=0; static char(*seen)[256]=NULL;
    if(on<0){ const char*e=getenv("REVA_OPENLOG"); on=0; if(e&&e[0]){ lg=fopen(e,"w"); seen=malloc(8192*256); on=(lg&&seen)?1:0; } }
    if(!on||n>=8192) return;
    for(int i=0;i<n;i++) if(!strcmp(seen[i],path)) return;
    snprintf(seen[n++],256,"%s",path); fprintf(lg,"%s\n",path); fflush(lg); }
static FILE* clubOpen(const char*p,const char*m){ FILE*f=fopen(p,m); if(f&&m[0]=='r') clubOpened(p); return f; }
#define fopen(p,m) clubOpen(p,m)
/* THE ENGINE, BY PART (11-02, engine plan Phase 1). Still one translation unit, read in this order: nothing is hidden behind a header yet, so a part
 * sees everything above it. Each part holds whole functions, cut only at brace depth 0. Real boundaries come later, part by part, on the recipe in
 * design/platform_tiers.md. Find a function with a search across engine/, not by line number. */
#include "engine/pack.inc"    /* the pack format, the wardrobe, morphs, stretch bones, canon bones, hair and cloth chains */
#include "engine/body.inc"    /* the one body: the steps the hero, the crowd, the cast and the actors all take */
#include "engine/clock.inc"   /* the clock and tick, stop motion, the tone knee, the day, the quality ladder, the sim hash */
#include "engine/gfxset.inc"  /* what this machine draws with: one table, never moved by a timer */
#include "engine/packcheck.inc" /* a pack is checked whole before any of it is believed */
#include "engine/packchunks.inc"/* what's authored on a body, in chunks a new bake can't lose */
#include "engine/load.inc"    /* loadPack and what it elects at boot */
#include "engine/anim.inc"    /* clip banks, the sampler, layered animation, the hip drop, prop and normal-map draws */
#include "engine/skatepark.inc"/* the skatepark, its small animation system and state tree, the camera director's notes */
#include "engine/doors.inc"   /* game modes, the save, variants, the zombie cab's rules, the start menu, data-driven doors */
#include "engine/post.inc"    /* the post pass and render scale */
#include "engine/xr.inc"      /* (11-11) a headset in the world: the anchor, the eyes, the controllers as pad 0 */
#include "engine/input.inc"   /* pad, keyboard, mouse, and the tape */
#ifdef __ANDROID__
#include "engine/android.inc" /* (11-06) the phone: the data out of the APK, the touch controls */
#endif
#include "engine/tvgrown.inc" /* the TV font's two sheets, and the font grown to a typeface's size: the only typeface that ships */
#include "engine/menus.inc"   /* character select, game mode select, the lighthouse door's include */
#include "engine/skater.inc"  /* the skater: physics, tricks, points; the beat clock */
#include "engine/weapons.inc" /* weapons in the hand */
#include "engine/set.inc"     /* sets, marks, collision, actors, the nav grid and minds, placed lenses, stage marks */
#include "engine/spans.inc"   /* the walked world: ramps, stairs, rays, triggers */
#include "engine/tris.inc"    /* (11-12) a set's own triangles: a ray, a moving ball, what touches a point */
#include "engine/people.inc"  /* a person built while the game runs, from measurements */
#include "engine/stream.inc"  /* the streaming world: cells that come and go with the hero */
#include "engine/sky.inc"     /* water, the sky, space, beams, the star, the skybox */
#include "engine/particles.inc"/* particles of any kind: emitters a game, a set or a mover has */
#include "engine/decals.inc"  /* decals thrown at the world: whatever's in their box takes the mark */
#include "engine/bake.inc"    /* baked light: probes traced once against a set's triangles */
#include "engine/setshader.inc"/* (11-12) a surface's own shader, from a set's `shader` line */
#include "engine/setcloth.inc"/* set cloth, half-pipes, routes */
#include "engine/skatepose.inc"/* the procedural riding pose on any body */
#include "engine/williams.inc"/* the Williams layer, mesh colours, drawing and ticking set actors */
#include "engine/shadow.inc"  /* the sun's shadow map, placed screens, drawing a set */
#include "engine/rigid.inc"   /* loose objects */
#include "engine/objects.inc" /* what a game makes and takes away as it runs */
#include "engine/ride.inc"    /* a ridden vehicle: two wheels on the walked world, a motor, two brakes, its rider */
#include "engine/liquid.inc"  /* liquid in a container: a drink that sloshes on a ride and spills over its rim */
#include "engine/lab.inc"     /* gait sync, the cast's setup, glossy floors, the toon pass, skin palettes, morph blending, the mark gizmo */
#include "engine/scene.inc"   /* conversation scenes, held props, boards, walks to marks, lenses on bodies */
#include "engine/settrack.inc"/* the set's timeline: lights, beams and props on the scene's clock */
#include "engine/setart.inc"  /* the set's drawings: pen strokes, flat shapes and pictures, frame by frame on the same clock (11-11) */
#include "engine/smoke.inc"   /* smoke, the mouth, scene staging */
#include "engine/head.inc"    /* the head layer, jaw, look-at, eyes */
#include "engine/strands.inc" /* (11-06) hair, lock by lock: every lock its own simulation */
#include "engine/cloth.inc"   /* cloth on bodies, measuring the body, the hanging cloth */
#include "engine/marks.inc"   /* the mark editor, eye morphs, the performance layer */
#include "engine/audit.inc"   /* the clipping audit */
#include "engine/contact.inc" /* the contact pass and jiggle */
#include "engine/ragdoll.inc" /* the ragdoll */
#include "engine/world.inc"   /* the solid world, walks round furniture */
#include "engine/cast.inc"    /* the clipping audit on one cast body and each frame, loading a scene */
#include "engine/captions.inc"/* captions, drawing the lab's partner, clip strides */
#include "engine/footlock.inc"/* the foot lock, gait rates, tricks, the crowd's weapons */
#include "engine/animgraph.inc"/* which clip the hero plays, from a text file */
#include "engine/panic.inc"   /* the panic layer, crowd behaviour, the sun pass, the camera director */
#include "engine/lang.inc"    /* another language's words: a file of them, and every string looked up */
#include "engine/tvtext.inc"  /* the TV's text: a font on whole pixels, a grid inside the safe box */
#include "engine/sprite.inc"  /* 2D: sprites on a canvas every display agrees on */
#include "engine/gamefont.inc"/* (11-12) a game's own typeface, baked for the canvas's pixels */
#include "engine/venue.inc"   /* the dance floor as data, for the packet */
#include "engine/ui.inc"      /* a list on the TV's grid, and the CONTROLS screen */
#include "engine/near.inc"    /* who's near: a grid over the floor plan, for a crowd */
#include "engine/gamefx.inc"  /* particles and decals */
#include "engine/poses.inc"   /* far bodies sharing poses, for a crowd */
#include "engine/face.inc"    /* faces and conversations: blinking, saying a line, looking at someone, for any body */
#include "engine/gameworld.inc"/* changing level, finding a way, props on any bone, a music clock */
#include "engine/notebus.inc" /* (11-08) the note bus: timed notes from a band brain beside the engine */
#include "engine/perform.inc" /* (11-08) the performer: a body playing an instrument from timed targets, live */
#include "engine/api.inc"     /* the table a game gets */
#include "engine/main.inc"    /* the frame ledger, the pass clock, and main() */
