/* engine.h -- everything a game sees of the engine (engine plan, Phase 3). A game includes this and nothing else of ours, so an AI working on a game needs this header and that game's file.
 * Quake II's shape: one table of what the engine offers (EngineAPI), one of what the game does (GameExport), and a version. The tables are append-only: a field never moves or changes
 * meaning, so an old game still links. The engine owns the loop and calls the game; the game never calls back into a draw. club.h comes with it: the club's math, one answer on every machine,
 * and the mixer. Bodies are the engine's fixed pool, reached by index. A game keeps its own state and reads nothing back from the renderer. Its tick writes that state; its draw and hud get it
 * const, which is the packet rule in C: what's on screen can't change what happens. */
#ifndef ENGINE_H
#define ENGINE_H
#include <stddef.h>
#include "club.h"

#define ENGINE_API_VERSION 1

/* a body in the pool. This is the start of the engine's own record, so the two agree field for field. */
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
typedef struct { float heroSpd, heroSnap, heroTurn; float lensArm, lensPitch, lensFov, lensAimY; float shotHit, shotLife; float stickYaw; int bodyCap; int poseRate, poseNear; } EngineTuning;   /* stickYaw: which way the stick's up points, for a lens that turns. bodyCap: how many bodies may be drawn; 0 is the engine's own 390. poseRate: a crowd's far bodies on one clip share this many poses a second of clip, which is what lets counts go up; 0 is every body its own. poseNear: the nearest this many always keep their own */

/* a player's pad, as of this tick. x and y are the stick, -1..1, right and up positive, and exactly 0 in its dead zone. held is which buttons are down, pressed which went down this tick. Pad 0 is also the keyboard, and it's always connected. A tape carries all four */
enum { PAD_A = 1, PAD_B = 2, PAD_X = 4, PAD_Y = 8, PAD_LB = 16, PAD_RB = 32, PAD_START = 64 };
typedef struct { float x, y, rx, ry, rt; unsigned held, pressed; int connected; } EnginePad;

/* what's true of a thing a game made, as of now. y is its lowest point and top its highest. OBJ_SOLID: bodies can't walk through it and rays stop at it. On a set that says `climb` it's its real triangles, which can be stood on. On any other set it's a circle, its footprint. OBJ_HIDDEN: still there, not drawn.
 * OBJ_LOOSE: the engine moves it: it falls, bounces, slides, rolls down what's sloping, stacks and knocks other loose things about, and asleep is 1 once it's come to rest. Its x and z are its middle. A box ends up on a face. OBJ_ROUND is a ball and rolls. OBJ_RESTING: it starts asleep where you put it, and stays there until what's under it changes or something hits it, so it can hang in the air. A loose thing that's also solid is only solid while it's asleep. A body walking into it then stops, and E->push wakes it. Awake, a body walking into it shoves it along */
enum { OBJ_SOLID = 1, OBJ_HIDDEN = 2, OBJ_LOOSE = 4, OBJ_ROUND = 8, OBJ_RESTING = 16 };
typedef struct { float x, y, z, yaw, scale; int prop; unsigned flags; float vx, vy, vz; int asleep; float top; } EngineObject;

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
    /* what the player's reading size really gives at size 1: 35 x 24, 23 x 12 or 17 x 12. For a game that would rather choose a shorter word or a smaller heading than be fitted. Ask in hud, never in tick: it differs from one player's screen to the next, so the rules can't depend on it */
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
    /* the music clock. Call it just after musicPlay: this many beats a minute, its first beat this many seconds in, this many beats to a bar. musicBeat is where it is, in beats, and GameExport.beat falls on the tick each comes round. In a live game it follows the sound, and a tape keeps what it read; in anything measured it's the tick. 0 for bpm turns it off */
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
} EngineAPI;

/* a game fills in what it needs and leaves the rest out: a missing entry does nothing, and bodies walk on their own feet. Only version is required. */
typedef struct {
    int version;
    const char *name;
    const void *state;                                          /* what draw and hud are handed */
    void (*boot)(void);                                         /* before anything is loaded: read the game's own files */
    const char *(*street)(void);                                /* the set it plays on; "none" for no set */
    float (*tod)(void);                                         /* its hour of the day */
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
