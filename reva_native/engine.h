/* engine.h -- everything a game sees of the engine (engine plan, Phase 3). A game includes this and nothing else of ours, so an AI working on a game needs this header and that game's file.
 * Quake II's shape: one table of what the engine offers (EngineAPI), one of what the game does (GameExport), and a version. The tables are append-only: a field never moves or changes
 * meaning, so an old game still links. The engine owns the loop and calls the game; the game never calls back into a draw. club_math.h comes with it, and nothing else of the engine's: the
 * engine's math, one answer on every machine, which a deterministic game must use. Sound, music and a save are in the table (step 20). Bodies are the engine's fixed pool, reached by index.
 * A game keeps its own state and reads nothing back from the renderer. Its tick writes that state; its draw and hud get it const, which is the packet rule in C: what's on screen can't
 * change what happens. */
#ifndef ENGINE_H
#define ENGINE_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "club_math.h"

#define ENGINE_API_VERSION 1

/* a body in the pool. This is the start of the engine's own record, so the two agree field for field. */
/* (11-08) a note off the note bus (E->notesIn): when it sounds on E->noteTime's clock, how long, its MIDI channel, note and velocity */
typedef struct { double t; float dur; int ch, note, vel; } EngineNote;
typedef struct { int pack, clip; unsigned phase; float x, z, rotY; int state, mate, moving; float tT, stateT, tx, tz, h, spd; } EngineBody;

/* the run's small state: score, health, level. Shared because a door that rotates games (the arcade's) banks across them. The engine reads one thing here: stage 3 means the hero stands still. */
typedef struct { int on; long score; int kills, wave, lvl, xp, deaths; float hp, hpMax, ifr; float t0, tA, tB; float ax, az, bx, bz; float r; int stage, flag, count; } EngineRun;

/* what a game sets and the engine's own systems read: how she moves, where the lens sits, what a bullet is. heroSpd: 0 is the default, and below 0 the stick doesn't move her at all, for a menu or a conversation */
/* a person to build while the game runs, E->bodyBuild. Leave anything at 0 and it's what a grown-up would have. Lengths are metres. New fields only ever go on the end
 * stature: how tall. heads: how many heads tall they stand, about 5.5 at five years old, 7 at twelve and 7.5 grown up, and the whole body's proportions follow from it. build: how broad, 1 is ordinary. shoulders, waist, hips and limbs are 1 for ordinary, on top of that. legs: the share of them that's leg, about 0.47. bust and belly: 0 to 1. nose and jaw: 1 is ordinary.
 * Colours are red, green and blue: all three 0 is the default. hairStyle: 0 none, 1 short, 2 long, 3 a ponytail, 4 to the jaw. hairLength: 0 to 2, longer. sleeves: 0 bare arms, 1 short, 2 to the wrist. bottomKind: 0 shorts, 1 long, 2 a skirt */
typedef struct {
    float stature, heads, build, shoulders, waist, hips, limbs, legs, bust, belly, nose, jaw, hairLength;
    unsigned char skin[3], hair[3], eyes[3], top[3], bottom[3], shoes[3];
    int hairStyle, sleeves, bottomKind;
} EngineBodySpec;
typedef struct { float heroSpd, heroSnap, heroTurn; float lensArm, lensPitch, lensFov, lensAimY; float shotHit, shotLife; float stickYaw; int bodyCap; int poseRate, poseNear; float fairAspect; int noWalkMap; int firstPerson; float heroAccel; int hudWide; float farPx; int bodyLite; int padOwn; int pauseOwn; int pointer; } EngineTuning;   /* pointer (11-12): 1, the mouse's pointer is shown and free (a game's menus); 2, it's grabbed for looking round and hidden; 0, the default: the engine's own rule (grabbed once a key or the mouse is used, let go in its pause) */   /* pauseOwn (11-12): 1, and Start (and Escape) are the game's: the engine's own Start menu doesn't open, and a game that binds "escape" with E->keyPad gets it as a button. 0, the default: the engine's menu, as before */   /* padOwn (11-12): 1, and the D-pad is only its buttons (PAD_UP and the rest): the engine doesn't also read it as the left stick when the stick's left alone, as it does for a game that doesn't say. 0, the default: as before */   /* bodyLite (11-07): 1, and a crowd body small in the picture is drawn from a lighter copy of itself, made from its own mesh the first time it's needed: per mesh, the same vertices with each moved onto a neighbour in its cell of a grid over the body (a 40th of its height, then a 16th) that has the same strongest bone, and the triangles that collapse gone. A copy is drawn while its cell is under two of the picture's lines, and the sun sees the same copy. Picture, not rules. 0, the default: every body whole, as before */   /* farPx (11-07): a body shorter than this many of the picture's lines isn't drawn, posed or shadowed, and one that was cut comes back a quarter bigger than that, so a body at the edge doesn't flicker. Lines of the picture, so it's the same "too small to see" on a tube and on a PC's screen. Picture, not rules: nothing in a tick reads a pose. 0, the default: every body the lens sees, as before */   /* hudWide (11-06): 1, and on a screen wider than 4:3 the TV's grid has as many more columns as fit at the same size of letter, so a HUD's corners are the screen's. A game still places its text on 35 x 24: a column goes where it would in proportion, text set right or centred finds the real edge or middle, a bar is as much longer, and E->tvPanel's panels go with them. On a 4:3 screen, a tube or a composite raster nothing moves. 0, the default: the grid in the middle of the screen, as before */   /* heroAccel (11-06): units a second each second the hero's speed goes toward what the stick asks, steadily, so a step into or out of a gait authored for that rate (skate_src/author/moves/z_ft.py's accel) keeps its planted foot where it is. 0, the default: it eases there, a quarter of the way in a sixteenth of a second, as before */   /* firstPerson (11-06): the lens a game places is the hero's eyes, so the near plane comes in to 0.12 units (the lens's fov is still lensFov). 1, and her body isn't drawn; 2, and it is, for a lens placed a little ahead of her face, so she sees her own arms and legs and not the inside of her head. 0, the default: a lens behind her, as before */   /* noWalkMap (11-05): 1, and the engine makes no walking map of a set or of a streaming world's cells, and E->path finds every way by searching the columns as it goes. A cell's map is a place every half unit and every step between them, and on a city's cells it cost more than a whole tick while it was made; a game whose ways are short and seldom asked for, or that routes on a graph of its own, is better off without it. 0, the default: the map, as before */   /* fairAspect (step 25): the shape this game is played in when players are against each other, width over height (1.3333 is 4:3). A screen wider than that shows the same world through the same lens, with black either side, so nobody sees round a corner because of their monitor. 0: the screen's own shape */   /* stickYaw: which way the stick's up points, for a lens that turns. bodyCap: how many bodies may be drawn; 0 is the engine's own 390. poseRate: a crowd's far bodies on one clip share this many poses a second of clip, which is what lets counts go up; 0 is every body its own. poseNear: the nearest this many always keep their own */

/* a kind of particle: what an emitter makes (queue step 24). rate: how many a second. life: seconds, from the least to the most. speed: along the emitter's way, least to most.
 * spread: the half-angle of the cone they're thrown in, degrees, 180 for every way. grav: how it falls, 9.8 like a stone, 0 hangs, under 0 rises. drag: how fast the air stops it.
 * size0 and size1: at birth and at death, in the world's units. r0 g0 b0 a0 and r1 g1 b1 a1: its colour and how solid, at birth and at death; past 1 is bright, for what's added.
 * stretch: 0 a round sprite, over 0 a streak along its own going, this many seconds of its speed long. spin: a round one turns, radians a second. wind: how much the set's wind
 * carries it, 0 to 1. curl: how much the air swirls it. jitter: it's born anywhere in a ball this wide. add: 0 laid over the picture (smoke, dust), 1 added to it (fire, sparks).
 * soft: it fades out where it meets a surface, on a machine that can. New fields only ever go on the end */
typedef struct {
    float rate, lifeMin, lifeMax, speedMin, speedMax, spread, grav, drag, size0, size1;
    float r0, g0, b0, a0, r1, g1, b1, a1;
    float stretch, spin, wind, curl, jitter;
    int add, soft;
} EngineParticles;
/* (queue step 25) the lens's own look, for a cutscene: zero what you don't set, and E->lensLook(NULL, 0) puts it all back. roll: degrees about the lens's own line. focus: how far
 * away it's focused, in world units, -1 for whatever it's aimed at, 0 for everything sharp; dof: how soft the rest goes, 1 a portrait lens wide open. shutter: how much of a
 * frame it's open, 0 none to 1, so a fast pan smears (off unless a game asks: blur on a flat panel isn't for everyone). sepia, grain: 0 to 1. flash: white over the picture,
 * for as long as it's set. burn: a film burn in from the edge, 0 to 1. What's drawn, never the rules; and nothing on a machine with no post pass */
typedef struct { float roll, focus, dof, shutter, sepia, grain, flash, burn; } EngineLensLook;
/* (queue step 24) a decal thrown at the world: whatever lies in its box takes the mark, a floor, a step, a wall, a prop. Where the machine reads the scene's depth mid-frame
 * it folds over what's there; where it doesn't (a Pi) it's a flat mark, and the game needn't know which. Zero what you don't set */
typedef struct {
    int kind;                 /* 0 a soft round mark, 1 a splat, 2 a scorch, 3 a wet patch, 4 a painted stripe, 5 a crack, 6 a picture. 0 to 3 and 5 stain what's under them, and are lit and shadowed as it is; 4 and 6 are paint, lit by the sun and the sky */
    float x, y, z;            /* its middle, on the surface */
    float dx, dy, dz;         /* the way it's thrown: into the surface. All 0 is straight down */
    float size, along;        /* how far across, in world units; and along, for a stripe or a picture that isn't square (0: the same) */
    float deep;               /* how far through the surface it reaches, each way (0: half its size). A splat on a stair wants enough to reach the step below */
    float turn;               /* about the throw, in degrees */
    float r, g, b, a;         /* its colour, and how strong */
    float seconds;            /* how long it stays, fading over its last quarter. 0 or less: until E->decalsClear or the level changes */
    const char *picture;      /* for a picture: a model's name, as E->prop takes it, whose first texture it shows. Name one that's loaded: this doesn't read a file politely */
    int image;                /* (11-12) for a picture: a game's image (E->image, E->imageMake) in place of a model's texture; what's painted into it later shows on the wall. 0: the picture above */
} EngineDecal;

/* a player's pad, as of this tick. x and y are the stick, -1..1, right and up positive, and exactly 0 in its dead zone. held is which buttons are down, pressed which went down this tick. Pad 0 is also the keyboard, and it's always connected. A tape carries all four.
 * PAD_BACK (11-06) is the pad's Back or View button, and V on the keyboard */
enum { PAD_A = 1, PAD_B = 2, PAD_X = 4, PAD_Y = 8, PAD_LB = 16, PAD_RB = 32, PAD_START = 64, PAD_BACK = 128,
       PAD_L3 = 256, PAD_R3 = 512, PAD_UP = 1024, PAD_DOWN = 2048, PAD_LEFT = 4096, PAD_RIGHT = 8192, PAD_LT = 16384, PAD_RT = 32768 };
/* (11-12) the stick clicks, the D-pad, and each trigger as a button (held past half way, so a game gets its press): a pad's whole face, for a game that
 * reads it. The D-pad is also the left stick when that's left alone, unless the game says it reads the D-pad itself (EngineTuning.padOwn) */
typedef struct { int on; float pos[3], dir[3], aim[3], aimDir[3]; float trigger, squeeze, stick[2]; unsigned buttons; } EngineXrHand;   /* (11-11) a headset's controller in the world: where the hand holds it and the way the hand points, where it aims from and the way it aims; buttons 1 the lower face button, 2 the upper, 4 the stick pressed, 8 the menu */
typedef struct { float x, y, rx, ry, rt; unsigned held, pressed; int connected; float mx, my; float lt; float px, py; unsigned pb, pbPressed; int hit; int keys; } EnginePad;   /* (11-12) keys: 1 when this player's last input came from the keys and mouse (or pad 0 has no controller plugged in), 0 from the pad: which a game's prompts name (OLLIE [SPACE] or OLLIE [RB]), and which control scheme its rules read (the skate game's flips: LMB and WASD, or the right stick). Input: on the tape (v7) and the wire */   /* (11-12) THE POINTER, for a game's menus: px, py where the mouse points on the 2D canvas (E->rect's units), under -999 when it's off the picture, grabbed or there's no mouse; pb its buttons held (1 left, 2 right, 4 middle) and pbPressed those that went down this tick; hit what the game's own hud said the pointer was over (E->pointerOver), 0 none. Pad 0's is this machine's mouse; in a session each player's is their machine's. On the tape (v6) and the wire, so a game may use all of it in its rules */   /* lt (11-12): the left trigger, 0 to 1, as rt is. mx, my (11-10, the bridge game's helm): how far the mouse moved this tick, in its own counts, to the right and towards the player positive. Every pad's: in a session each machine's mouse is its player's (11-11); on one machine it's pad 0's, as the keyboard is, and its buttons are already A, B and X (left, right, middle). The engine has read the mouse since 08-25 and carried it on the tape and across the wire; this is the first a game sees of it. It's with the pads, so a game may use it in its rules */

/* what's true of a thing a game made, as of now. y is its lowest point and top its highest. OBJ_SOLID: bodies can't walk through it and rays stop at it. On a set that says `climb` it's its real triangles, which can be stood on. On any other set it's a circle, its footprint. OBJ_HIDDEN: still there, not drawn.
 * OBJ_LOOSE: the engine moves it: it falls, bounces, slides, rolls down what's sloping, stacks and knocks other loose things about, and asleep is 1 once it's come to rest. Its x and z are its middle. A box ends up on a face. OBJ_ROUND is a ball and rolls. OBJ_RESTING: it starts asleep where you put it, and stays there until what's under it changes or something hits it, so it can hang in the air. A loose thing that's also solid is only solid while it's asleep. A body walking into it then stops, and E->push wakes it. Awake, a body walking into it shoves it along */
enum { OBJ_SOLID = 1, OBJ_HIDDEN = 2, OBJ_LOOSE = 4, OBJ_ROUND = 8, OBJ_RESTING = 16, OBJ_ORIGIN = 32, OBJ_NOCAST = 64 };   /* (11-12) OBJ_NOCAST: no shadow (an editor's cursor and ghost). OBJ_ORIGIN: y is where its own origin goes, not its
                                                                                                       * lowest point: a model made in another editor, placed where that editor puts it.
                                                                                                       * Up to 8192 at once, of 512 models */
typedef struct { float x, y, z, yaw, scale; int prop; unsigned flags; float vx, vy, vz; int asleep; float top; float pitch, roll; } EngineObject;   /* pitch, roll: radians, as placeTurned left it (0 for anything else) */

/* (11-05) A RIDDEN VEHICLE: something with two wheels a body stands on and rides, a scooter, a bike, a board (engine/ride.inc says all of it). Lengths in metres, weights in kg,
 * forces in newtons, speeds in metres a second, angles in degrees unless it says radians. Zero what doesn't apply. New fields only ever go on the end.
 * EngineRidePose is how the rider stands on it, in radians laid over the rider's own clip on its canonical bones: crouch (metres the hips sink), lean (the back, forward),
 * legSplit (the left thigh forward and the right one back), stance (both thighs out to the side), knee (each knee folds), armFwd, armSpread and elbow (the hands to the bars),
 * swing (the right arm off the bars and out to the side, for a blow: 0 on the bars, 1 all the way out; set it with rideSpec as the blow goes) */
typedef struct { float crouch, lean, legSplit, stance, knee, armFwd, armSpread, elbow, swing; } EngineRidePose;
typedef struct {
    const char *prop;                       /* its model in props/, drawn under the rider, its forward +z and its lowest point on the ground; NULL for none. Copied as it begins */
    float wheelbase, wheelRadius, deckHeight, width;   /* wheel to wheel; a wheel; the deck the rider stands on, over the ground; how wide it is to a wall */
    float mass, riderMass;                  /* the vehicle, and the rider with what's carried */
    float motorPower, motorForce, motorTop, motorFade, motorEff;   /* the most power at the wheel (W); the most pull at the wheel (N); the speed past which the motor gives nothing; the speed below that over which it fades out; wheel power over battery power, 0..1 */
    float cda, airDensity;                  /* the drag area (m^2), the air (kg/m^3) */
    float roll, rollOf[16];                 /* the tyres' rolling resistance, a coefficient, and by surface kind (E->surface's kind, 1..15), where 0 is `roll` */
    float grip, gripOf[16], slide, cgHeight;   /* the tyres' grip on a dry road (a friction coefficient), a share of it by surface kind (0 is all of it), a sliding wheel's share of it, and the centre of mass over the ground */
    float brakeFront, brakeRear;            /* the most each brake can pull, N: past the tyre's grip the wheel locks */
    float steerMax, steerHalf;              /* how far the bars turn the wheel at a standstill, degrees; the speed at which they turn it half as far */
    float frontSave, rearSlideFall;         /* seconds a locked front can slide before it drops the rider (let go and it's saved); degrees a locked rear can come round in a turn before it does */
    float wobbleOnset, wobbleGrow, wobbleHz, wobbleHold, wobbleThrow;   /* the speed the front starts to swing; how fast the swing grows, a share a second for each m/s past the onset; how fast it swings; how fast it dies, a share a second, with the bars held still; the swing in degrees that throws the rider */
    float kickForce, kickTime, kickTop;     /* a kick from the rider's foot: its push, how long, and the speed past which a foot can't push */
    float crashSpeed;                       /* into something solid faster than this throws the rider */
    EngineRidePose pose;
    float pivotRate;                        /* radians a second the rider can swing it round by hand at a standstill, a foot down; it fades out by 1 m/s. 0: it only turns rolling */
    float stepUp, stepTime;                 /* (11-05) an edge up, a curb: at a crawl (under kickTop) with the throttle or a kick asked for, the rider steps off and lifts it up an edge as high as stepUp (m, at most
                                             * 0.2: what a body walks up), taking stepTime seconds to walk it a wheelbase on, and gets back on at a standstill. 0: no lifting, and a curb is only a slope between the wheels */
    float gripFwd, gripUp, gripHalf;        /* (11-06) the bars' grips, from the middle of the deck's top: how far ahead of it, how high over it, how far out each side (m) */
    float footFront, footBack, footOut, footTurn;   /* (11-06) where the rider's ankles stand on the deck, from its middle: the left (front) one ahead, the right (back) one behind (m), how far
                                             * out from the middle line each is (m), and how far the back foot turns out (degrees) */
    int reach;                              /* (11-06) 1: the rider's own limbs put its hands on those grips and its feet on the deck (two bones each, on any rig), under the pose's crouch and lean,
                                             * and a kick puts the back foot on the ground; the pose's other angles are then unused. 0: the pose's angles, which suit a rig whose bones rest square to the body */
} EngineRide;
/* what's true of a ride now. x y z yaw are the world's units and radians, y where its wheels meet the ground; pitch and lean radians. speed m/s along its way, negative rolling back;
 * grade the rise over its wheelbase (0.08 is 8%). wheelPower and batteryPower W. drag, rolling and gravity the forces along its way, N. frontSlide: seconds the front's been locked;
 * rearSlide: degrees the back's come round; wobble: degrees of swing. down: it's thrown its rider, and why: 1 the front wheel, 2 the rear came round, 3 the wobble, 4 it hit something,
 * 5 it landed too hard, or the game's own number from rideKnock. vx vy vz: how the rider leaves it, units a second, for E->bodyLimp. kindFront, kindRear: E->surface's kinds under its
 * wheels. kicks counts every kick, so a game can charge for each. metres: how far it's gone */
typedef struct {
    float x, y, z, yaw, pitch, lean, speed, grade, wheelPower, batteryPower, drag, rolling, gravity, frontSlide, rearSlide, wobble, vx, vy, vz, metres;
    int frontLocked, rearLocked, kicking, kicks, down, why, kindFront, kindRear;
    int stepping, steps;                    /* (11-05) it's being lifted up an edge now; how many times it has been, so a game can charge for each */
} EngineRideState;

/* (11-05) LIQUID IN A CONTAINER (engine/liquid.inc): an upright round cup, carried on a ride or set down. radius and height (its rim, over its floor) in metres; fill, how deep the
 * liquid stands in it, metres; damping, the slosh's damping ratio (water about 0.02, a milkshake far more); thick, how much slower than water it pours over the rim (1 is water);
 * lid: 1 and nothing gets out. New fields only ever go on the end.
 * Its state: tiltFwd and tiltSide, the surface's tilt in radians (forward is the carrier's way: the liquid up the back wall is a tilt below 0); slosh, how far the surface at the
 * wall stands over where it would be still, m, and peak the most it ever has; level, how deep it is now, m; litres in it now and spilt out of it; spilling, 1 this tick;
 * accFwd, accSide, accUp: what the carrier did to it this tick, m/s^2 (side is to the right of the way) */
typedef struct { float radius, height, fill, damping, thick; int lid; } EngineLiquid;
typedef struct { float tiltFwd, tiltSide, slosh, peak, level, litres, spilt, accFwd, accSide, accUp; int spilling; } EngineLiquidState;

/* (11-12) WHAT A RAY OR A BALL MET among a set's own triangles (a set that says `triangles`: E->triRay, triSweep, triNear). t: how far along the move, 0 to 1. pos: the
 * point met; for a ball, and for triNear, the point of the triangle it touches. nrm: the way out of the surface there, a unit vector: a ray's is the triangle's own, turned
 * to face where the ray came from; a ball's points from what it touches to its middle. dist: how far, in units, along the move, or from the point for triNear. kind: its
 * mesh's surface kind (a set's `surface` lines, as E->surface says). tri: which triangle, the same number on every machine */
typedef struct { float t, pos[3], nrm[3], dist; int kind, tri; } EngineTriHit;

/* where a menu is: which row, how far it's scrolled, which tab. Keep one in your state, zeroed, for each menu, and uiBegin to uiEnd does the rest. tab is the only part you might read */
typedef struct { int sel, n, tab, holdX, holdY, editing, kx, ky; float top; unsigned long long off; } EngineUi;

#define TV_COLS 35      /* the TV's text: a grid of 35 x 24 cells inside the safe box, on any display */
#define TV_ROWS 24

typedef struct {
    int version;
    /* the bodies: nBodies records, bodyStride bytes apart, and what the engine keeps beside each */
    int nBodies; void *bodies; size_t bodyStride;
    int *hide, *team, *ban, *drawable; float *alt, *spdMul;
    /* the hero */
    float *heroX, *heroZ, *heroHead, *heroSpd, *heroAlt;
    void (*heroPistol)(void);                                   /* a pistol in her hand */
    /* the run, the rules the engine reads, the clocks, the pad */
    EngineRun *run; EngineTuning *tune;
    float *gameT; unsigned long *frameNo; int *attract, *btnA, *btnB;
    /* dice, and the world */
    float (*frnd)(void); int (*rnd)(void);
    float (*collide)(float *x, float *z, float radius);         /* pushes a point out of the world; how far it moved */
    const char *(*bodyLabel)(int i);
    /* bullets: they fly and hit by the tuning above, and GameExport.bulletKill says what a hit does */
    void (*bulletSpawn)(float x, float z, float dx, float dz);
    int (*bulletPeek)(float *x, float *z);                      /* how many are flying, and where the first is */
    /* what the player sees: lines and rings in the world, sparks, a banner with its ping */
    void (*line)(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g, float b);
    void (*ring)(float cx, float cz, float rad, float y, float r, float g, float b, int segs);
    void (*burst)(float x, float y, float z, float r, float g, float b, int n, float rad);
    void (*banner)(const char *s, float seconds);
    const char *(*bannerText)(void);                            /* NULL when there's none up */
    /* the TV's text: column, row, size in cells, text, colour. Always on the 35 x 24 grid, whatever the screen. If the player has picked bigger letters, the engine moves what you placed so that it fits, so don't allow for it */
    void (*tvText)(float col, float row, int size, const char *s, float r, float g, float b);
    void (*tvTextR)(float row, int size, const char *s, float r, float g, float b);
    void (*tvTextC)(float row, int size, const char *s, float r, float g, float b);
    void (*tvBar)(float col, float row, float cols, float frac, float r, float g, float b);
    /* the players: four pads */
    const EnginePad *pads;
    /* 2D, in hud: a canvas of 360 x 240 on every display. image() loads a file from the game's folder and gives a handle, or 0. u and v are in the image's pixels */
    int (*image)(const char *path);
    void (*sprite)(int image, float x, float y, float w, float h, float u0, float v0, float u1, float v1, float r, float g, float b, float a);
    void (*rect)(float x, float y, float w, float h, float r, float g, float b, float a);
    void (*clear)(float r, float g, float b);                   /* a flat game's background */
    /* the signal: "480i" or "240p". Call it behind a cut or a fade; the set blanks for a moment. 240p only happens once the player has kept it on this set */
    void (*video)(const char *mode);
    /* actions: a name for what a button does, so a player can move it (save/controls.txt). Ask at boot, with the buttons it's on by default, and keep the handle in your state. Then ask about it by player, 0..3 */
    int (*action)(const char *name, unsigned buttons);
    int (*actionHeld)(int player, int action);
    int (*actionPressed)(int player, int action);
    /* the lens: put it somewhere and it stays until lensFree hands it back to the follow lens the tuning sets. A shake is a kick that dies away */
    void (*lens)(float ex, float ey, float ez, float ax, float ay, float az);
    void (*lensFree)(void);
    void (*shake)(float amount, float seconds);
    /* the world's clock, 0.02 to 2: slow motion, a hit that stops time for a moment. A game's tick gets the scaled dt and the bodies go with it. Whole ticks still go by at the same rate, so a replay still matches */
    void (*timeScale)(float scale);
    /* the world. On a set that says `climb`, its surfaces are walked on, and these read its real triangles: ramps, stairs and upper floors need nothing but the geometry. On any other set, walk is the old colliders and the ground is 0.
     * walk: a body with feet at y goes from one place towards x, z. It slides along what stops it, and you get back the height to stand at there. ground: the place to stand under a point, within a step of y.
     * ray: does a straight line hit anything, and how far along it, 0..1. inTrigger: is this point in the box the set calls `trigger <name> x y z width height depth`. mark: where the set's `mark <name> x y z yaw` is, 1 if it has one. */
    float (*walk)(float fromX, float fromZ, float *x, float *z, float y, float radius);
    float (*ground)(float x, float y, float z);
    int (*ray)(const float *from, const float *to, float *t);
    int (*inTrigger)(const char *name, float x, float y, float z);
    int (*mark)(const char *name, float *x, float *y, float *z);
    /* the hero on a set she walks on: how high her feet are, which the engine keeps; and a jump, at this speed in units a second. 1 if she was standing on something to jump from */
    const float *heroFeet;
    int (*heroJump)(float speed);
    /* how high each body's feet are drawn, for a game walking them on a climbable set */
    float *feet;
    /* the hero's animation graph: a text file saying which clip plays and when, read again when it changes (engine/animgraph.inc). animSet sets a value or a flag it asks about. animIn says whether she's in a state. GameExport.clipEvent hears its events. bodyGraph, further down, is the same for any other body */
    int (*animGraph)(const char *path);
    void (*animSet)(const char *name, float value);
    int (*animIn)(const char *state);
    /* split screen: n pictures of the same world, 1 to 4, each with its own lens. Two are stacked, three or four are in quarters. tvView and uiView put text in one player's picture */
    void (*views)(int n);
    void (*viewLens)(int i, float ex, float ey, float ez, float ax, float ay, float az);
    /* effects, from a fixed budget: the oldest makes way. particles: n thrown out from a point at up to this speed, fading over life seconds, falling by grav. decal: a soft mark lying flat on a surface, for this many seconds. Neither touches the game's dice */
    void (*particles)(float x, float y, float z, int n, float speed, float life, float size, float r, float g, float b, float grav);
    void (*decal)(float x, float y, float z, float radius, float r, float g, float b, float a, float seconds);
    /* a menu. In tick, uiMove moves *sel with this player's stick and gives back the item when A is pressed, or -1. In hud, uiList draws the items from a row of the grid, every other row */
    int (*uiMove)(int player, int *sel, int n);
    void (*uiList)(float row, const char *const *items, int n, int sel);
    /* a crowd's question: the bodies that could be within a radius of a point, in index order, up to max. Do your own exact test on what comes back. It's the same answer as walking every body, for a tenth of the cost at 100 bodies and far less past that */
    int (*nearby)(float x, float z, float radius, int *out, int max);
    /* lights for this frame, in draw: a point light, and a spot aimed along dx, dy, dz with this half-angle in degrees. falloff is how fast it dims with distance, 1/(1+falloff*d*d), and 0.055 is a lamp. Bright colours go past 1. A machine's settings say how many it draws */
    void (*light)(float x, float y, float z, float r, float g, float b, float falloff);
    void (*spot)(float x, float y, float z, float dx, float dy, float dz, float r, float g, float b, float falloff, float degrees);
    void (*lightShadow)(void);                                  /* the light just added casts shadows, if this machine's settings have room */
    /* what the player's reading size really gives at size 1: 35 x 24, 23 x 12 or 17 x 12, or at desk (11-06, a PC's screen) about 60 x 41 in 92% of the screen; more columns with E->tune->hudWide. At desk a
     * game's places are taken in proportion, rows as well as columns, and its letters are smaller, so a block that should stay together (a phone, a card in a corner) sizes
     * itself in these real cells: its columns over cols/35 and its rows over rows/24. For a game that would rather choose a shorter word or a smaller heading than be fitted. Ask in hud, never in tick: it differs from one player's screen to the next, so the rules can't depend on it */
    void (*tvGrid)(int *cols, int *rows);
    /* things a game makes and takes away as it runs: a crate, a door, a pickup, a lift. Everything below is for tick, so a replay does the same, and it's all in [simhash].
     * prop names a model in props/, and its number is good for the whole run. Ask in boot: asked in play, the file is read there and then, and the frame waits.
     * spawn puts one in the world with its lowest point at y, and gives a handle, or 0 if there's no such prop or 1024 are already out. yaw is in radians, the way a body's heading is. A set changing takes them all away.
     * A handle stays good until despawn and is never given out again, so a stale one is safe: calls on it do nothing, and object gives NULL. place moves one: along is next to nothing for a solid one, and a new yaw builds its collision again. paint gives it a flat colour, and a negative red takes that off */
    int (*prop)(const char *stem);
    int (*spawn)(int prop, float x, float y, float z, float yaw, float scale, unsigned flags);
    void (*despawn)(int obj);
    void (*place)(int obj, float x, float y, float z, float yaw);
    void (*paint)(int obj, float r, float g, float b);
    const EngineObject *(*object)(int obj);
    /* a body from the pool: bodyNew wakes one that's hidden and on no team, standing still at x, z, and gives its index, or -1 if none's free. bodyGone puts it back. It's the lowest free one, so a replay wakes the same one. Team 0 means no team, so give it another. The pool's size is set at boot, and which pack each one is */
    int (*bodyNew)(float x, float z, int team);
    void (*bodyGone)(int i);
    /* any body's animation graph, its own run of a file like animGraph's, keeping its own place. bodyGraph gives it one, and NULL takes it off, back to bodyLoco and the crowd's ladder. Its clips can be its own, or borrowed by name from a bank if its rig can take them. The engine sets `speed` from its spd. `grounded` is yours, and starts at 1. bodyAnimSet and bodyAnimIn are animSet and animIn for one body, and clipEvent hears its events with its index. It runs in the tick, seen or not, and stops while the body's hidden. bodyNew starts it again from its first state */
    void (*bodyGraph)(int i, const char *path);
    void (*bodyAnimSet)(int i, const char *name, float value);
    int (*bodyAnimIn)(int i, const char *state);
    /* physics. push adds to a loose thing's speed and wakes it, and pushAt does it at a place on it, so a push high up on something tall sends it over. feel says how much it bounces, 0 to 0.95, and how much it grips, 0 to 1 (it starts at 0.32 and 0.55): a grip of 0.55 holds a box on a slope of 29 degrees. GameExport.objectHit hears what it hits. objectOnRay gives the first object a straight line meets, solid or not, by the box around it, and how far along, 0..1, or 0 if there's none. E->ray is what says whether a wall's in the way.
     * bodyLimp sends a body limp the next time it's posed, carrying this speed: the same ragdoll film uses, on what's really there. -1 is the hero. 16 others at once, and it gives back 0 if they're all in use. It's what's drawn, not the rules, so its place stays what you say, and it's the game's to stop walking it. What's off screen doesn't fall until it's seen, so never let a rule wait on bodyLimpState: 0 not limp, 1 falling, 2 at rest. bodyRise puts it back on its clips, and bodyGone does too */
    void (*push)(int obj, float vx, float vy, float vz);
    void (*pushAt)(int obj, float x, float y, float z, float vx, float vy, float vz);
    void (*feel)(int obj, float bounce, float grip);
    int (*objectOnRay)(const float *from, const float *to, float *t);
    int (*bodyLimp)(int i, float vx, float vy, float vz);
    void (*bodyRise)(int i);
    int (*bodyLimpState)(int i);
    /* a fuller menu than uiList, made for a pad on a tube. Say what's on it, in order, in tick, every tick, between uiBegin and uiEnd. Each row answers there and then what the player just did, the values are yours, and it draws itself with the HUD, so hud has nothing to do for it.
     * uiBegin: this menu, for this player (-1 is anyone), in this part of the grid. The stick goes between rows, round the ends and past what's switched off, and scrolls a long menu so the row in use shows. uiButton: 1 when A's pressed on it. uiToggle: A or the stick flips it, and 1 if it did. uiSlider: the stick moves it by step, between lo and hi. uiChoice: the stick or A goes through the words. uiText: a word typed with the pad on a keyboard on the grid, and 1 when it's finished. uiTabs: LB and RB, first in a menu, and it gives back which tab to say the rows of. uiBack: 1 when B's pressed. uiEnable(0): the rows after it are grey and can't be used, until uiEnable(1). uiLabel and uiGap: words, and a space. uiColors: its three colours, in use, not in use, and switched off. uiSounds: what it plays, by name, when it moves, picks and goes back; ask in boot. Two menus at once is fine, for two players. A question's just a smaller menu: say it instead of the other one until it's answered */
    /* a model you've finished with: its memory goes, and its number still works, because the next spawn reads it again. 0 if anything still has it out, or the set was using it first */
    int (*propDone)(int prop);
    /* bodies that stream, for a game with more kinds of people than fit in memory. bodyPack asks for a body's pack by name, from bodies/ and then packs/, and gives its number, good for the whole run, or -1 if there's no such file. It's read off the main thread: bodyPackReady says when it's in. bodyWear puts it on a body, which starts again on its clips, and gives 0 if it isn't ready. bodyPackDone: you've finished with it, and it's freed once no body has it on. Asking again reads it again */
    int (*bodyPack)(const char *stem);
    int (*bodyPackReady)(int pack);
    void (*bodyPackDone)(int pack);
    int (*bodyWear)(int i, int pack);
    void (*uiBegin)(EngineUi *u, int player, float col, float row, float cols, float rows);
    void (*uiView)(int view);                                   /* straight after uiBegin: this menu goes in that picture of a split screen, and its col and row are that picture's, the way tvView's are */
    void (*tvView)(int view);                                   /* in hud: the text after this goes in that picture of a split screen, and -1 is back to the whole screen. It's placed on TV_COLS x TV_ROWS of that picture, and the engine fits it, the way it does for the player's reading size. With one picture it changes nothing, so a game can always say it */
    void (*uiEnd)(void);
    void (*uiLabel)(const char *text);
    void (*uiGap)(float rows);
    void (*uiEnable)(int on);
    int (*uiButton)(const char *text);
    int (*uiToggle)(const char *text, int *on);
    int (*uiSlider)(const char *text, float *value, float lo, float hi, float step);
    int (*uiChoice)(const char *text, const char *const *items, int n, int *choice);
    int (*uiText)(const char *text, char *buf, int cap);
    int (*uiTabs)(const char *const *names, int n);
    int (*uiBack)(void);
    void (*uiColors)(float focusR, float focusG, float focusB, float restR, float restG, float restB, float offR, float offG, float offB);
    void (*uiSounds)(const char *move, const char *pick, const char *back);
    /* faces, on any body, and -1 is the hero. What's drawn and not the rules: nothing here reaches [simhash], and no rule may wait on how a face looks or when a line ends. What a face can do depends on its pack: a jaw bone or a shape called jaw, eye bones or eyeLook shapes, a shape called blink, and any shape by name. The first of these on a body wakes its face up, and from then on it blinks and breathes. 64 at once.
     * bodySay: a line. The sound plays on the voice bus and its own loudness moves the mouth and the head. words are put on screen for as long as it lasts and, with no sound (NULL or "-"), the mouth moves on the words alone for as long as they take to read. Gives its length in seconds, or 0 if the sound's still being read, and it starts when it's in. bodySaying: seconds left. bodyShut stops it. captions(0) keeps the words off the screen.
     * bodySayData: a line from your own data, a song's lip sync say: env its loudness and vis how far the shape of the sound opens the jaw, each 0..1 and either can be NULL, n values at rate a second, and no sound of its own. Copied. bodyMouth: how far open, 0..1, for this one tick.
     * bodyShape: a shape by name, 0..1, over this many seconds, until you say otherwise. Gives how many meshes have it, so 0 means it hasn't got one.
     * bodyLook: head and eyes turn to this point and stay on it. bodyLookBody: to another body's eyes, following them. bodyLookOff: back to what its clips do. */
    float (*bodySay)(int i, const char *sound, const char *words);
    float (*bodySaying)(int i);
    void (*bodyShut)(int i);
    void (*captions)(int on);
    void (*bodySayData)(int i, const float *env, const float *vis, int n, float rate);
    void (*bodyMouth)(int i, float open);
    int (*bodyShape)(int i, const char *name, float amount, float seconds);
    void (*bodyLook)(int i, float x, float y, float z);
    void (*bodyLookBody)(int i, int other);
    void (*bodyLookOff)(int i);
    /* another level, while the game runs: sets/<name>.set, behind a fade of this many seconds out and back, or none. What the old one had goes, what you spawned with it, and any pack nothing's using. GameExport.levelIn is where to put things in the new one, with the screen dark. 0 if there's no such set */
    int (*level)(const char *name, float fadeSeconds);
    const char *(*levelName)(void);
    /* finding a way: points to walk to in a straight line, into xyz, 3 floats each, the first where it starts. How many. On a set that says `climb` it goes by the real surfaces, up ramps and stairs and round what's in the way, at any height, and radius is how wide the walker is (0 is a person). If there's no way it gives the way to as near as it could get, so look at where the last point is. It's a search, so it costs: a few a second, not one per body per tick */
    int (*path)(float x0, float y0, float z0, float x1, float y1, float z1, float radius, float *xyz, int max);
    /* a model on a bone of a body, -1 being the hero, as long as you like. bone is its name in the pack, or head, neck, chest, spine, hips, hand_l, hand_r, foot_l, foot_r. It goes with the bone. The offset and scale are for a body of ordinary height, so the same crown fits a child. 256 at once. Gives a handle for detach, or 0. It's what's drawn, not the rules */
    int (*attach)(int body, int prop, const char *bone, float x, float y, float z, float yaw, float pitch, float roll, float scale);
    void (*detach)(int handle);
    void (*detachAll)(int body);
    /* the music clock. Call it just after music (below): this many beats a minute, its first beat this many seconds in, this many beats to a bar. musicBeat is where it is, in beats, and GameExport.beat falls on the tick each comes round. In a live game it follows the sound, and a tape keeps what it read; in anything measured it's the tick. 0 for bpm turns it off */
    void (*musicClock)(float bpm, float firstBeat, int beatsPerBar);
    float (*musicTime)(void);
    float (*musicBeat)(void);
    /* a person built from measurements while the game runs: pass the spec and its size. It gives a number like bodyPack's: ask bodyPackReady, put it on with bodyWear or heroWear, and let it go with bodyPackDone, and after that the number's given out again. The building's off the main thread. It walks, runs and stands by itself (idle, w_walk, w_run) and takes clips from a bank, and its face does everything above. The same spec builds the same body on any machine */
    int (*bodyBuild)(const EngineBodySpec *spec, size_t size);
    void (*heroWear)(int pack);
    void (*heroFace)(float yaw);                                /* the hero turns this way, a little each tick you ask */
    /* a conversation: a text file, talk/<name>.talk, read each time it's started. talkSeat says which body sits in which seat, 0..7. talkStart plays it: who says what, who looks at whom, the lens, the words, a question put to the player. talking is 1 until it's over, talkStop ends it early, and the lens and the tuning's lens go back to what they were. engine/face.inc says what can be in the file */
    void (*talkSeat)(int seat, int body);
    int (*talkStart)(const char *name);
    int (*talking)(void);
    void (*talkStop)(void);
    /* sound, music and a save of the game's own (step 20). None of it is in the sim: the receipt can't hear a sound or read a file.
     * sound: one from sounds/, a wav or an ogg by name, at this gain, from a place in the world (3 floats) or NULL for everywhere, looping or once. Gives a handle for soundStop, or -1. soundBus: a bus's gain over this many seconds; 0 is effects, 1 music, 2 voices, 3 the menu.
     * music: a file by path, streamed by a thread of its own so the disk can't make a frame late, taking over from what played; 1 if it opened. musicStop fades it out over this many seconds, and musicGain moves its gain, for a menu's slider or a line that wants it down.
     * a save is a text file of your own, save/<game>_<slot>.txt: a version line at the top, then key=value lines, so a player can read it and edit it. saveBegin names the slot and the version, saveSet and the others put values, and saveEnd writes it in one go, 1 if it did.
     * load reads a slot and gives its version, 0 for none or unreadable, and loadGet and the others then read its values, with your default where there's none or it isn't a number. Everything's checked on the way in: a slot name is letters, digits, - and _, 31 at most; a key 47 characters, a value 95, 256 keys. REVA_NOSAVE=1 makes them all blind, so a measured run can't depend on a file */
    int (*sound)(const char *name, float gain, const float *pos, int loop);
    void (*soundLoad)(const char *name);                        /* into the bank now, in boot, so its first play doesn't read the disk in the middle of a frame */
    void (*soundStop)(int id);
    void (*soundBus)(int bus, float gain, float seconds);
    int (*music)(const char *path, float gain, int loop);
    void (*musicStop)(float fadeSeconds);
    void (*musicGain)(float gain, float seconds);
    void (*saveBegin)(const char *slot, int version);
    void (*saveSet)(const char *key, const char *value);
    void (*saveSetI)(const char *key, long value);
    void (*saveSetF)(const char *key, float value);
    int (*saveEnd)(void);
    int (*load)(const char *slot);
    const char *(*loadGet)(const char *key, const char *def);
    long (*loadGetI)(const char *key, long def);
    float (*loadGetF)(const char *key, float def);
    /* split screen: a conversation's lens goes in this player's picture, and -1, the start, is the whole screen's */
    void (*talkView)(int view);
    /* a file of your own that you've just opened: say so, and the list of what the game reads (REVA_OPENLOG, the cook check) has it */
    void (*opened)(const char *path);
    /* eyes that glow, on any body, -1 the hero: a colour, bright ones past 1, and how much of it, 0 to put them out. It's the body's eyes that glow, wherever its pack says they are, and not a light: add one with E->light if it should light the room. What's drawn, not the rules */
    void (*bodyEyes)(int i, float r, float g, float b, float amount);
    /* particles of any kind (queue step 24). particleKind gives one of the engine's own by name (smoke, fire, sparks, steam, dust, embers, fountain, snow, exhaust, pyro, fuse), to use as it is or to copy and change; NULL if there's no such. emitter starts one at a place, pointing a way, and gives a handle, or 0 when there are none left. emitterAt moves it and turns it. emitterRate says how many a second: 0 stops it making more, and what's in the air lives out its life. emitterGone lets the handle go. emitterBurst throws n at once. size is sizeof your EngineParticles. They're picture: their own dice, stepped by the tick, and never in the rules. Unlike particles above, which stays as it was, these come from the machine's own budget */
    const EngineParticles *(*particleKind)(const char *name);
    int (*emitter)(const EngineParticles *kind, size_t size, float x, float y, float z, float dx, float dy, float dz);
    void (*emitterAt)(int handle, float x, float y, float z, float dx, float dy, float dz);
    void (*emitterRate)(int handle, float perSecond);
    void (*emitterGone)(int handle);
    void (*emitterBurst)(const EngineParticles *kind, size_t size, float x, float y, float z, float dx, float dy, float dz, int n);
    /* decals thrown at the world (queue step 24): decalAt(&d, sizeof d). From a fixed pool, the oldest making way; decalsClear takes every one a game has put down. E->decal is the older flat mark, and stays. What's drawn, not the rules */
    void (*decalAt)(const EngineDecal *d, size_t size);
    void (*decalsClear)(void);
    /* the lens's look for a cutscene (queue step 25): roll, depth of field, motion blur, sepia, grain, a flash, a burn. lensLook(NULL, 0) is all of it off */
    void (*lensLook)(const EngineLensLook *l, size_t size);
    /* another language's words (queue step 26). Text is UTF-8 everywhere: the TV font has accented Latin, Greek and Cyrillic, and a language can bring bigger glyphs of its own.
     * language("es") reads lang/es.txt beside the game, a file of `"your text" = "their text"` lines, and remembers the choice; language("") is the game's own words again;
     * it returns 1 if that's the language now. Every string given to the TV's text or a menu is looked up whole, so a fixed label needs nothing. A string you build is looked
     * up before you build it: snprintf(b, sizeof b, E->word("SCORE %d"), n). languageNow is the code in use, "" for none. What's drawn, never the rules */
    int (*language)(const char *code);
    const char *(*word)(const char *text);
    const char *(*languageNow)(void);
    /* online play (queue step 29): the same game on several machines, a pad or more each. Every machine works out the same world from the same dice and the same pads, so
     * all that crosses the wire is pads, and a Pi and a PC can share a game. E->pads is the session's pads on every machine, exactly as on one machine: pad 0 is player 1
     * wherever player 1 is sitting. A button is played a few ticks after it's pressed (netLag), the same on every machine, the presser's own included, and a machine that
     * hasn't everyone's pads for a tick waits for them: a slow wire holds the picture and never changes the game.
     * netState: 0 this run isn't online; 1 it's gathering its players or loading; 2 playing; 3 it's over, and netWhy says what happened: the game goes on, on this machine alone.
     * netSeats: how many pads the session has. netHere(player): is that pad one of this machine's (always 1 when not online).
     * ⛔ ALL OF THESE ARE FOR WHAT'S DRAWN: whose lens, whose half of a split, whose name is lit, a line that says the game is online. Ask them in draw and hud and
     * never in tick. The rules must be the same on every machine: every tick's hash of them is compared as the game runs, and a game that decides anything by netHere
     * is stopped at the tick it parted. And a game whose rules read even netState can't be replayed from its tape, which is how an online fault gets found.
     * The same goes for anything else that differs between machines: the screen's shape, the reading size, the language, the clock on the wall.
     * What tick may do is call netHost, netJoin and netLeave, from a menu: they end the run.
     * A session is a whole run, from the first tick. netHost(port, pads, word) and netJoin(address, word) end this run and start the engine again in one, waiting for
     * the players on a screen of the engine's own (port 0 is 7777; pads is how many the session has, 2 to 4; word, or NULL, keeps a stranger out; address is a name or
     * four numbers, with ":port" if it isn't 7777). netLeave starts it again alone. After a session that couldn't start, netWhy says why, back in the game.
     * A session's saves are the host's: E->load reads what the host had, on every machine, and a save made in it is kept by the host. The engine's own pause menu is off */
    int (*netState)(void);
    int (*netSeats)(void);
    int (*netHere)(int player);
    int (*netLag)(void);
    const char *(*netWhy)(void);
    void (*netHost)(int port, int pads, const char *word);
    void (*netJoin)(const char *address, const char *word);
    void (*netLeave)(void);
    /* the hero's feet at this height, from where she falls onto whatever's under her (a set she walks on): a start on a hill, on a bridge's deck or under it. Only her x and z can be set by
     * writing them, and a walked world always starts her feet at 0, which on ground higher than a step is inside the hill. Call it in start, or whenever she's put somewhere new */
    void (*heroDrop)(float y);
    /* (11-05) what the ground is at a point, on a set she walks on: the height to stand at, the same one ground gives, and the surface that's there. normal (3 floats, or NULL) is which
     * way its top faces, always up, so the slope along a way (dx, dz) is -(normal[0]*dx + normal[2]*dz) / normal[1] for a unit (dx, dz). kind is which of the set's `surface <kind> <words>`
     * lines it is, from 1 in the order the set says them, by the name of the mesh that made it (its name up to a '#' is one of the words); 0 where nothing's said: the floor at 0, or a set
     * with no surface lines. surfaceName gives a kind's name ("" for 0 or one there isn't), surfaceKind a name's kind (0 for none) */
    float (*surface)(float x, float y, float z, float *normal, int *kind);
    const char *(*surfaceName)(int kind);
    int (*surfaceKind)(const char *name);
    /* (11-05) a ridden vehicle (EngineRide above, engine/ride.inc). rideBegin: one, under a rider (-1 the hero, a body's index, -2 nobody), where the rider is; a handle, or 0 if its
     * numbers can't be ridden (it says why). Up to 8. rideSpec: new numbers for it (a heavier rider, a flat). rideControl, in tick, every tick: the throttle 0..1, the front and rear
     * brakes 0..1, the bars -1 (left) to 1, and a kick. rideState: what's true of it now. ridePlace: put it somewhere, at a standstill, upright, its rider back on. rideRider: who's on
     * it now. rideKnock: throw its rider, as a fight does, with a push (units a second). rideEnd: it's gone. While a body rides, the engine places it, turns it and poses it: the hero
     * needs E->tune->heroSpd below 0 so the stick doesn't walk her, and a game's bodyStep leaves a riding body alone. Once it's thrown its rider, the rider is the game's again.
     * It's all rules: in tick, in [simhash], the same on every machine */
    int (*rideBegin)(const EngineRide *ride, size_t size, int rider);
    void (*rideSpec)(int ride, const EngineRide *spec, size_t size);
    void (*rideControl)(int ride, float throttle, float front, float rear, float steer, int kick);
    const EngineRideState *(*rideState)(int ride);
    void (*ridePlace)(int ride, float x, float y, float z, float yaw);
    void (*rideRider)(int ride, int rider);
    void (*rideKnock)(int ride, int why, float vx, float vz);
    void (*rideEnd)(int ride);
    /* an object turned any way, not only about its up: yaw, then pitch (its nose down for more than 0), then roll, radians. For one that isn't loose; a solid one's columns are built again each time */
    void (*placeTurned)(int obj, float x, float y, float z, float yaw, float pitch, float roll);
    /* a number of the game's that the engine's bridge can read (REVA_BRIDGE, `get <name>`), for a check with no window: the speed, the charge, an order's state. The pointer must stay good, so point into your state. 64 names, 31 letters */
    void (*bridgeValue)(const char *name, const float *value);
    /* (11-05) liquid in a container (EngineLiquid above, engine/liquid.inc). liquidBegin: one, carried by a ride (a rideBegin handle) or by nothing (0); a handle, or 0 if its numbers
     * can't hold liquid (it says why). Up to 16. liquidSpec: new numbers, and its fill is what's in it again (a cup filled at the fountain). liquidCarry: carried by another ride, or
     * set down (0). liquidPush: an acceleration on it for this tick, m/s^2 along the carrier's way, to its right and up, on top of what its ride does. liquidState: what's true of
     * it now. liquidEnd: it's gone. All rules: in tick, in [simhash], the same on every machine */
    int (*liquidBegin)(const EngineLiquid *spec, size_t size, int ride);
    void (*liquidSpec)(int liquid, const EngineLiquid *spec, size_t size);
    void (*liquidCarry)(int liquid, int ride);
    void (*liquidPush)(int liquid, float fwd, float side, float up);
    const EngineLiquidState *(*liquidState)(int liquid);
    void (*liquidEnd)(int liquid);
    /* (11-05) a playing sound, by E->sound's handle: soundPitch plays it faster or slower, 1 as it was recorded, 2 an octave up (0.25 to 4), from now on; soundGain sets its gain.
     * A motor's whine with the speed, a ping that climbs. What's heard, never the rules */
    void (*soundPitch)(int id, float pitch);
    void (*soundGain)(int id, float gain);
    /* (11-06) in hud: a panel behind text, in the TV's grid's columns and rows, so it sits exactly where the text it's behind does on any screen and with E->tune->hudWide. a is how much it hides */
    void (*tvPanel)(float col, float row, float cols, float rows, float r, float g, float b, float a);
    /* (11-06) in hud: a triangle, and a line `width` rows wide (the same width whichever way it runs), in the grid's columns and rows as tvPanel places them, for a game's own
     * drawing: a phone's map, an arrow turned any way, a ring. On a wide grid (E->tune->hudWide) a column is spread out, and at the desk reading size a row is too: a shape
     * that has to keep its proportions divides its columns by the grid's columns over 35 and its rows by the grid's rows over 24 (E->tvGrid). A line's width is in rows */
    void (*tvTri)(float c0, float r0, float c1, float r1, float c2, float r2, float r, float g, float b, float a);
    void (*tvLine)(float c0, float r0, float c1, float r1, float width, float r, float g, float b, float a);
    /* (11-06) a bank of clips a game brings: bodies/<stem>.rpack, or packs/, read there and then. Any body's graph (bodyGraph) can then borrow a clip of it by name, if the body's
     * rig can take it, after the engine's own banks. The number of clips in it, or 0 if there's no such file or no room (eight banks in all, four of them a game's). Ask in boot or start */
    int (*bankLoad)(const char *stem);
    /* (11-06) where a body is through its graph's clip, 0 to 1, from now on: bodies that start together needn't move together. Say it after bodyWear and bodyGraph, which start it at 0 */
    void (*bodyAnimAt)(int i, float u);
    /* (11-08, THE REVA BAND) THE NOTE BUS: timed notes from a band brain beside the engine (REVA_NOTES=<port>, engine/notebus.inc). notesIn hands over the notes that came
     * in since the last call, each at noteTime's clock (seconds), early, as the sender scheduled them. In draw: what's drawn and heard, never the rules */
    int (*notesIn)(EngineNote *out, int max);
    double (*noteTime)(void);
    /* (11-08, THE REVA BAND) THE PERFORMER: a body playing an instrument, live (engine/perform.inc). perfLimb: limb 0 the left hand, 1 the right, 2 the left foot,
     * 3 the right, arrives at (x, y, z) -- the body's own frame, metres: x its left, y up from its feet, z its front -- at time t on noteTime's clock, from its last
     * target on a stroke lifted `lift` metres (less when there's no time), and stays. perfFinger: a hand's finger (0 thumb .. 4 little) pressed by amount 0..1 at t
     * for dur. perfOff: back to its clip alone. Over whatever its clip gives; in the frame, never the rules */
    void (*perfLimb)(int body, int limb, float x, float y, float z, double t, float lift);
    void (*perfFinger)(int body, int hand, int finger, double t, float dur, float amount);
    void (*perfOff)(int body);
    /* (11-08) a pack's outfit, by name (one outfit per PACK: every body wearing it changes): 1 if it has one of that name. "bare" is every body's */
    int (*packOutfit)(int pack, const char *outfit);
    /* (11-08) THE POSE STREAM: this body plays the frames a solver beside the engine streams under `name` (the note bus's port, `p` lines: engine/notebus.inc),
     * each bone set as a clip's keys would set it, blended between the two frames round now; keepRoot 1 leaves its root to the body's own place and turn. NULL stops it */
    void (*perfStream)(int body, const char *name, int keepRoot);
    /* (11-09) a sound that starts `seconds` from now, counted in samples by the mixer: for sounds known ahead (a band's notes), which E->sound could only
     * start at the next frame, a different few hundredths late each time. Its id is E->sound's (soundStop, soundGain, soundPitch) */
    int (*soundIn)(const char *name, float gain, float seconds);
    /* (11-10, the bridge game) THE SKY TURNED, on a set that says `space`: its stars, its milky way and its named stars as they're seen from something that has turned by this yaw (about up) and this pitch
     * (nose down for more than 0), radians, the way an object is turned. For a ship that turns under its crew: the room stays where it is in the world and the sky goes round it. 0, 0 is the sky as the set
     * has it, and a set changing puts it back there. In draw: what's drawn, never the rules. Not turned yet: the set's sun, so the light still comes from where the set says; and the engine's own
     * rasterizer draws space black, with no stars to turn */
    void (*skyTurn)(float yaw, float pitch);
    /* (11-11, the bridge game) A HEADSET (engine/xr.inc, gfx_xr.inc: OpenXR; REVA_XR=1 asks for one at start-up, REVA_XR=fake is a stand-in for a desk). xrOn: 1 while one is
     * this run's way of seeing. Then the game's lens is where the player's head is anchored: put it at their eyes, facing the way their body faces, and the headset adds
     * the head's own turn and lean. Nothing the HUD draws is shown in a headset: write what must be read in the world (E->line). The controllers are pad 0 as well
     * (a trigger or a grip A, the right hand's buttons B and Y, the left's X and RB, the left stick walks and the right turns). xrHand: a hand (0 left, 1 right) in the
     * world, 1 if it's being tracked. xrHead: the head's middle and the way it faces, in the world. xrBuzz: a controller's buzz, 0..1, for so many seconds.
     * ⛔ A tape records pad 0, never a controller's place, so what a game does with xrHand can't be replayed */
    int (*xrOn)(void);
    int (*xrHand)(int hand, EngineXrHand *out, size_t size);
    int (*xrHead)(float pos[3], float fwd[3]);
    void (*xrBuzz)(int hand, float amount, float seconds);
    /* (11-11) a headset's options, for a game's own menu. xrScale: how much of the runtime's own eye size is drawn now, and with next above 0, what's drawn from the next start
     * (0.3 to 1.5), or above 100 an eye's width in pixels (the height keeps the runtime's shape), kept in save/xr.txt. With nothing kept, 2160 wide (an HP Reverb G2's own). xrRecentre: where the head is now becomes the anchor. xrTunnel: this frame's comfort tunnel, 0 none to 1 the most, each eye
     * closed in to a circle (ask in draw, while the player turns or moves). xrHanded: 1 swaps the controllers' sides for a left-handed player, sticks and buttons. The
     * menu button is pad 0's Back */
    float (*xrScale)(float next);
    void (*xrRecentre)(void);
    void (*xrTunnel)(float amount);
    void (*xrHanded)(int left);
    void (*xrEyeSize)(int *w, int *h);   /* (11-11) the pixels each eye is drawn at, 0 x 0 with no headset */
    /* (11-11) the light just added (E->light, E->spot) reaches this far, in units, and no further: its light fades smoothly to nothing there, and nothing past it pays
     * for it. A light with no reach goes on until it's under 1.5% of white, which for a strong lamp is the whole of a room: every pixel shades it. A set's `spot` line
     * takes the same as a 13th number */
    void (*lightReach)(float reach);
    /* (11-11) in draw, for what this machine shows and another doesn't: this body, or this object, isn't in this frame's picture here (its shadow still is). A
     * first-person player's own body, or the view out of a room nobody on this machine is in. Nothing of the rules changes, and it's forgotten at the next draw */
    void (*bodyUnseen)(int i);
    void (*objectUnseen)(int obj);
    /* (11-12) THE SET'S OWN TRIANGLES, for what rides real surfaces: a quarter pipe's vertical face, a wall, a lip, which the walked world's columns can't hold. Only a
     * set that says `triangles` keeps them (engine/tris.inc), and with none each of these says 0. The rules may use them: the answers are the same on every machine.
     * triRay: the first triangle the segment from -> to meets, either face; 1 if one did. triSweep: the first a ball of that radius meets moving from -> to; a ball
     * already touching one and moving away from it or along it isn't stopped by it, so a ball can roll. triNear: the triangles within radius of a point, nearest first,
     * at most max of them; how many. triCount: how many triangles the set has */
    int (*triRay)(const float *from, const float *to, EngineTriHit *hit);
    int (*triSweep)(const float *from, const float *to, float radius, EngineTriHit *hit);
    int (*triNear)(const float *at, float radius, EngineTriHit *hits, int max);
    int (*triCount)(void);
    /* (11-12) in draw: a line in its own colour, hiding what's behind it, where E->line's are added to what's there (and so can't be seen on a light surface).
     * Depth-tested like E->line's, one pixel wide, 8,192 a frame */
    void (*lineSolid)(float x1, float y1, float z1, float x2, float y2, float z2, float r, float g, float b);
    /* (11-12) a game's own keyboard, in boot: while any of these keys is held, pad 0 holds these buttons, and PAD_LT or PAD_RT pulls that trigger all the way. Keys by
     * name, apart by spaces or commas: a letter or digit, space, tab, enter, backspace, lshift rshift shift, lctrl rctrl ctrl, lalt ralt alt, up down left
     * right (the arrows), mouse1 mouse2 mouse3. Once a game has said any, the engine's own keys for buttons (space, E, Q, R, Z, C, shift, the mouse's
     * buttons, V and Tab) are off; WASD is still the left stick, and Esc still the engine's menu. What the keys did is on the tape */
    void (*keyPad)(const char *keys, unsigned buttons);
    /* (11-12) in draw: this body drawn with its origin at x, y, z (units), turned as E->placeTurned turns an object (yaw, then pitch about its own x, then roll about its
     * own z), this frame only. For a body that isn't standing on the ground: a skater on a wall, a rider in a loop. Its pose, its shadow and what's on its bones go
     * with it; the rules never see it. Its place in the crowd (EngineBody x, z) is still the game's to keep near, for what's drawn round it */
    void (*bodyPlace)(int i, float x, float y, float z, float yaw, float pitch, float roll);
    /* (11-12) a game's own bytes in its save, beside its text slots: save/<game>_<slot>.bin, the slot named as saveBegin's (letters, digits, - and _, 31 at most). saveBytes writes
     * them whole (a temp file, then renamed) and gives 1 if they were kept; loadBytes reads them into buf and gives how many, 0 for none, -1 if there are more than cap. What a
     * game reads back it checks before believing. A session's are the host's, as a save's are: handed to every machine at the start (40,000 bytes a file at most), and a save in
     * the session goes into every machine's copy and is written by the host's alone. REVA_NOSAVE makes them blind */
    int (*saveBytes)(const char *slot, const void *bytes, int n);
    int (*loadBytes)(const char *slot, void *buf, int cap);
    /* (11-12) sound for a game played as it goes (the Godot proof of concept's step 8). soundPlace moves a playing sound, by E->sound's handle: its world
     * position from now on, its pan and distance worked out from the lens each buffer, so a rolling board's sound goes with the board. soundLength is a
     * sound's length in seconds, loading it if it isn't in; -1 for none. The music has two decks: musicCross brings a track in over this many seconds
     * while the playing one goes out (with nothing playing, it fades in), 1 if it opened; musicLeft is how long the track playing has left in
     * seconds, -1 for none, a looping one, or one not open yet, so a soundtrack crosses to its next song when that's under its crossfade */
    void (*soundPlace)(int id, float x, float y, float z);
    float (*soundLength)(const char *name);
    int (*musicCross)(const char *path, float gain, int loop, float seconds);
    float (*musicLeft)(void);
    /* (11-12) a game's own UI (the Godot proof of concept's step 9). font: a TrueType file from the game's folder, baked at this size of the 2D canvas (a
     * line's height) in the display's own pixels, so its letters are as sharp as the screen; a handle, or 0. The TV font is still the engine's own:
     * a game's font comes with its licence. fontText draws a line on the canvas from its top-left, in hud; fontWidth is how wide it is.
     * imageSmooth: an image drawn smoothly, not nearest (a picture scaled, not pixel art), on a texture of its own; say it before its first draw.
     * canvasEdges: the screen's edges in canvas units (a 3:2 canvas in a wider screen has room either side). quit: the run ends */
    int (*font)(const char *path, float size);
    void (*fontText)(int font, float x, float y, const char *text, float r, float g, float b, float a);
    float (*fontWidth)(int font, const char *text);
    void (*imageSmooth)(int image);
    void (*canvasEdges)(float *x0, float *y0, float *x1, float *y1);
    void (*quit)(void);
    void (*imageSize)(int image, int *w, int *h);              /* (11-12) an image's size in its own pixels (a sprite's u and v are in them); 0 for none */
    void (*bodyMirror)(int i, int on);   /* (11-12) a game's body shown mirrored, left for right: each bone its twin's by name (_L _R, Left Right) reflected across the pack's own x. For a rig whose rest is symmetric that's its clip done by the other side; what's on its bones goes with it. Picture, not rules */
    void (*bodyTint)(int i, const char *word, float r, float g, float b);   /* (11-12) a game's body: its untextured meshes whose names hold the word drawn in this colour (0 to 1, as a name's #rrggbb is) in place of their own; NULL or "" puts them back. Picture, not rules */
    /* (11-12) A PICTURE A GAME PAINTS WHILE IT RUNS (the Godot proof of concept's step 13: their graffiti). imageMake gives a blank image, w x h pixels and clear,
     * drawn smoothly on a texture of its own (a 2D sprite, or a decal's picture: EngineDecal.image), or 0. imagePaint puts a rectangle of the game's RGBA pixels into it
     * (w x h, rows top first); what shows it shows the paint from the next frame, near and far. Picture, not rules: the pixels the game keeps are its own */
    int (*imageMake)(int w, int h);
    void (*imagePaint)(int image, int x, int y, int w, int h, const unsigned char *rgba);
    void (*imageGone)(int image);   /* (11-12) an imageMake picture let go: its texture back, its handle the next imageMake's. A decal showing it must go first (E->decalsClear) */
    void (*fontEdge)(int font, float width, float r, float g, float b);   /* (11-12) an edge round a font's letters, this many canvas units wide, in this colour, drawn under them: white words on a white park */
    /* (11-12) A GAME'S OWN TRIANGLES in the tree triRay, triSweep and triNear ask (the Godot proof of concept's step 14: Create a Map's pieces, ridden in its
     * test). triAdd hands over n triangles in the world (nine floats each, the engine's units) with a surface kind (surfaceKind of a name) and a number
     * of the game's own (which piece they're of); they stay until triAddClear or the set goes. triRebuild makes the tree again from the set's props and
     * them, and says how many triangles it has: the set must have said `triangles`. triId(hit.tri) is a hit triangle's number, -1 for the set's own.
     * Rules: in the tick, in the same order */
    void (*triAdd)(const float *xyz, int n, int kind, int id);
    void (*triAddClear)(void);
    int (*triRebuild)(void);
    int (*triId)(int tri);
    /* (11-12) THE POINTER in draw and hud: pointerAt is this machine's mouse on the 2D canvas now (0 when it's off the picture or grabbed); pointerOver
     * says what the game drew there that it's over (a number of the game's own, not 0), and that number comes back as input, pads[].hit, on the
     * next tick: what a click lands on is decided by what was drawn, and every machine and every replay agrees on it */
    int (*pointerAt)(float *x, float *y);
    void (*pointerOver)(int id);
    /* (11-12) SHAPES IN 2D, in hud, on the canvas as rect is (the Godot proof of concept's comparison: their HUD's slanted plates, their cards' blobs, their
     * wheel's rings, their marker strokes). poly fills the shape inside n points (x, y pairs; any outline that doesn't cross itself, either way round, concave too). stroke
     * draws a stroke through n points, this many canvas units wide, its corners mitred; closed joins the last point back to the first */
    void (*poly)(const float *xy, int n, float r, float g, float b, float a);
    void (*stroke)(const float *xy, int n, float width, float r, float g, float b, float a, int closed);
    /* (11-12) a font sized by its em, as Godot, a browser and a word processor size type (font's size is the whole line): a Godot UI's font_size, times its
     * pixel's size on the canvas, is this size. Otherwise as font */
    int (*fontEm)(const char *path, float size);
    /* (11-12) triRay, triSweep and triNear asking only for some surface kinds, a bit each (1u << E->surfaceKind(name)), 0 all of them, and looking past
     * the rest: Godot's collision mask (the skate game's probe asks for its ride surfaces, or its obstacles). Rules, as the others */
    int (*triRayKinds)(const float *from, const float *to, unsigned kinds, EngineTriHit *hit);
    int (*triSweepKinds)(const float *from, const float *to, float radius, unsigned kinds, EngineTriHit *hit);
    int (*triNearKinds)(const float *at, float radius, unsigned kinds, EngineTriHit *hits, int max);
} EngineAPI;

/* a game fills in what it needs and leaves the rest out: a missing entry does nothing, and bodies walk on their own feet. Only version is required. */
typedef struct {
    int version;
    const char *name;
    const void *state;                                          /* what draw and hud are handed */
    void (*boot)(void);                                         /* before anything is loaded: read the game's own files */
    const char *(*street)(void);                                /* the set it plays on; "none" for no set */
    float (*tod)(void);                                         /* its hour of the day. (11-12) Under 0: none, and its set's own `sun` and `sky` lines light it (a Godot scene's moon and ambient) */
    void (*start)(int onSet);                                   /* the bodies are loaded: deal the first round */
    void (*tick)(float dt);                                     /* one tick */
    void (*draw)(const void *state);                            /* the frame's marks in the world: line, ring, burst */
    void (*hud)(const void *state);                             /* the frame's text */
    void (*host)(float *heroHead, float dt);                    /* nobody's touched the pad: the game plays itself */
    int (*bodyStep)(int i, float dt);                           /* walk this body. 1 if the game walked it; 0 hands it to the crowd's own feet */
    float (*bodyUse)(int i);                                    /* how much of its texture over its team's colour, 0..1 */
    const char *(*bodyLoco)(int i, float spd);                  /* the clip in the zombie bank it walks on; NULL for its own */
    const char *(*bodyIdle)(int i);                             /* and stands in */
    float (*bodyGait)(int i);                                   /* clip rate per unit of its speed */
    void (*bulletKill)(int i);                                  /* a bullet hit this body */
    const char *video;                                          /* the mode it wants, "480i" or "240p"; NULL for whatever the player's file says */
    size_t stateSize;                                           /* how big state is. The receipt hashes it, so keep what the rules depend on in there and no pointers. Under dev reload, a new build of the game gets the old one's state if the size still matches */
    int flat;                                                   /* no 3D at all: no floor, no sky, and it needs no bodies or packs */
    void (*clipEvent)(int body, const char *name);              /* an event in an animation graph fell on this tick. body -1 is the hero */
    void (*levelIn)(const char *name);                          /* the new level's in, and the screen's dark: put things in it */
    void (*beat)(int beat);                                     /* the music clock's come round to this beat */
    void (*talkEvent)(const char *name);                        /* a conversation's `event` line, or the label of the answer to its question */
    void (*objectHit)(int obj, int other, int body, float speed);   /* a loose thing hit something this hard: another object (other), a body (body, and -1 is the hero), or, with other 0 and body -2, the world. Once a touch */
} GameExport;

const GameExport *gameEntry(const EngineAPI *engine);           /* the one thing a game's file must define */

#endif
