/* clubcloth -- WORN CLOTH, on any skinned character, in plain C with no engine under it. The club's solver for a whole garment: the hanging cloth's recipe (design/engine_capabilities.md 37) made
 * for a body that moves fast and a garment that isn't a skirt. Built 11-02 for the alien's clothes, with the collision and clipping work they needed.
 *
 * A garment is its mesh, skinned to the body as usual: that skinned pose is the TARGET. The cloth is simulated in the skeleton's own space, so the body's speed through the world can't fling it and a
 * teleport means nothing; what the world does to it arrives as an acceleration and as air, both passed in. Two things keep it on him whatever happens, and neither is a tuning:
 *
 *   MAX DISTANCE    each point has a limit on how far it may be from its target, painted by the tailor: 0 at a collar, a shoulder or a waistband (those points ARE the skin's pose, kept outside the skin), 10 cm at a hem.
 *                   ⛔ Chains hung down a whole garment went 35 cm off the leg, and his shins floated free of his shorts: the body looked disconnected.
 *   THE BACKSTOP    each point is tied to the bit of skin under it at rest, and may not pass inside that skin's tangent plane, moved by the skin's own weights. Where the skin is at its own limb, this is
 *                   the collision: it costs a dot product a point, and a garment can be pressed flat against the body and never through it.
 *
 * Everything else that passes under a garment is a capsule, on the limb's own bones: a thigh swinging up under a hem, a calf folding into a knee. An ARM IS OUTSIDE A TOP, so it presses the cloth in toward the body
 * instead of out the shortest way, which threw panels over the arm (the coat's lesson, engine_capabilities 37). No point tests the capsule of the bone it's already on.
 *
 * A GARMENT OVER ANOTHER stays outside it (cc_set_under): the skin says nothing about what's between it and the cloth, and baggy legs came 6 cm through a top's belly in a bail.
 *
 * No contact ever adds speed: a push is a move ("A 30 mm correction in a 2 ms substep is 15 m/s, and Verlet keeps it: the coat flew over her shoulders").
 *
 * IT MOVES LIKE FABRIC, AND bench_clubcloth.c HOLDS IT TO THAT: what a fabric is made of is two numbers from a textile lab (cc_fabric), and everything comes from them. Each point weighs its share of the area. Air's the real drag. Bending is Bergou's quadratic form, stiff by the lab's bending length, folding from how it was cut. Threads run two ways and scissor, so an edge on the bias gives and a tube can swing. It creeps back to its shape without gaining speed. Without all that it passed every check on clipping and swung at 6 Hz.
 *
 * Nothing here knows Godot: godot/club_cloth_gde.c binds it there. All positions are metres in skeleton space, all matrices 3x4 by rows: [r00 r01 r02 tx, r10 r11 r12 ty, r20 r21 r22 tz].
 */
#ifndef CLUBCLOTH_H
#define CLUBCLOTH_H

#ifdef __cplusplus
extern "C" {
#endif

#define CC_BONES 4

typedef struct CCGarment CCGarment;

/* The skin a garment is worn over: triangles, wound outward, with their weights. bon: indices into the caller's palette. */
typedef struct {
    int nv;
    const float *pos;   /* nv * 3 */
    const float *wgt;   /* nv * 4 */
    const int *bon;     /* nv * 4 */
    int ni;
    const int *idx;
} CCBody;

typedef struct {
    int nv;             /* drawn vertices: a seam in the UVs may split a point, and they're welded back here by position */
    const float *pos;   /* nv * 3 */
    const float *wgt;   /* nv * 4 */
    const int *bon;     /* nv * 4 */
    const float *maxd;  /* nv: how far from the skinned pose this point may go (m). 0: it is the skinned pose */
    const float *near_; /* nv: how close to the skin it may come (m), so a layer over another keeps out of it. NULL: its own distance at rest less `slack`, down to a few mm */
    int ni;
    const int *idx;
    int nbones;         /* the palette's size */
    const int *parent;  /* nbones: each bone's parent in the palette, -1 for none. A point's backstop is only sought on its own bones' skin and their parents', or a tee's side would tie itself to the arm beside it */
} CCGarmentDesc;

/* What a garment is made of comes from the lab: cc_fabric takes what a square metre weighs and its bending length (ASTM D1388) and sets the rest. bench_clubcloth.c holds the solver to those. */
typedef struct {
    float density;      /* kg/m^2. A tee 0.16, twill 0.23, fleece 0.32, denim 0.40 */
    float bend;         /* flexural rigidity, N m: mass per area * g * bending length cubed */
    float drag;         /* air, 1/m: 1/2 rho Cd / density */
    float drive;        /* per second: how fast it creeps back to the skinned pose. It adds no speed, so it can't ring: what stops it staying crumpled after the body has moved on */
    float damp;         /* per second: what's left of still air. Air's own drag does the real work */
    float flex_damp;    /* m^4/s: bending's own losses, per unit mass. Fabric's fibres rub, and this goes as the fourth power of how short a ripple is: one the size of a few points dies at once and a swing the size of the garment doesn't feel it */
    float shear;        /* N/m: how hard it is to pull on the bias. Fabric's threads run two ways and the weave lets them scissor, so a tube of it can swing. Edges are only ever straight lines between points, so an edge on the bias takes this and an edge along a thread stays stiff */
    float hem;          /* metres: a free edge's rod, its EI over the sheet's rigidity. A sewn hem's 0.08: twice the cloth, stitched, 2 cm wide. 0: a cut edge */
    float substep;      /* seconds: a frame is cut into as many of these as fit */
    float slack;        /* metres a point may come in from where it was cut, when the tailor didn't say */
    float friction;     /* static, against skin and limbs; kinetic is 0.6 of it */
    float lag;          /* seconds: how slowly what's drawn follows what's solved. Fabric doesn't react fast: heavier cloth takes longer */
} CCParams;

/* A capsule on a limb. The caller gives its ends in skeleton space each frame; `bone` is the palette index of the bone it's on, and `press` makes it push cloth toward the body instead of out the shortest way. */
typedef struct {
    float a[3], b[3];
    float r;
    int bone;
    int press;
} CCCapsule;

void cc_default_params(CCParams *p);
/* A fabric: kg/m^2, its bending length in metres, and how hard it is to pull on the bias, N/m. */
void cc_fabric(CCParams *p, float density, float bending_length, float shear);
CCGarment *cc_create(const CCGarmentDesc *d, const CCBody *body, const CCParams *p);
void cc_free(CCGarment *g);

/* One frame. pal: the palette, pose * inverse bind, nbones * 12 floats. gravity, accel (the body's own acceleration through the world) and air (the air's speed past the body, so minus its velocity): skeleton space, m/s^2 and m/s. */
void cc_step(CCGarment *g, float dt, const float *pal, const float gravity[3], const float accel[3], const float air[3], const CCCapsule *caps, int ncaps);

/* How the skeleton itself is turning through the world, in its own space: radians a second and a second squared, about its origin. Read by the next step. A spin flares a hem and a stop swings it: all of it arrives as forces, since the cloth is simulated in the skeleton's own space. */
void cc_set_turning(CCGarment *g, const float omega[3], const float alpha[3]);

/* The same in three parts, for several garments on several threads: every garment's cc_pose, then every garment's cc_layer, garments under first, on one thread. Then cc_solve, each on any thread: nothing cc_solve writes is read by another garment's. */
void cc_pose(CCGarment *g, const float *pal);
void cc_layer(CCGarment *g);
void cc_solve(CCGarment *g, float dt, const float gravity[3], const float accel[3], const float air[3], const CCCapsule *caps, int ncaps);

/* A garment worn over another stays outside it by `gap` metres, where they share a bone. Each point walks to the nearest point of the one under it each frame and keeps outside that point's plane, as it's drawn; a point that starts closer is allowed that much. One way: what's under never moves for what's over. NULL: over nothing. The one under must be stepped too. */
void cc_set_under(CCGarment *g, CCGarment *under, float gap);

/* A garment's parts, the two legs of a pair, a sleeve and a body: each keeps outside the others by `gap` metres. Which part a point's on goes by its strongest bone: bone_part is nbones long, and a point on part 0 is a part like any other. A point that starts closer is allowed that much. A fold within one part can still pass through itself. */
void cc_set_parts(CCGarment *g, const int *bone_part, float gap);

/* What he's carrying or standing on, set before cc_layer each frame: m is its place in skeleton space, 3x4 by rows, orthonormal, centred on the box; half is half its size. Cloth keeps outside it. */
#define CC_MAX_BOXES 2
typedef struct { float m[12]; float half[3]; } CCBox;
void cc_set_boxes(CCGarment *g, const CCBox *boxes, int n);

/* A capsule's place at rest, the same order as cc_step's list. A point that starts inside it is allowed that much: at rest the collision moves nothing (engine_capabilities 37), and a point between two thighs isn't fought over by both. */
void cc_set_capsule_rest(CCGarment *g, int c, const float a[3], const float b[3], float r);

/* Start again from the skinned pose on the next step. */
void cc_reset(CCGarment *g);

/* The body's own space turned under the cloth all at once, and the pose turned with it, so that in the world nothing moved: a rider put back after turning round on a deck, in the pose that's the same thing. m takes a point of the old space to the new one, 3x4 by rows, rigid. The next cc_pose carries the cloth across. */
void cc_carry(CCGarment *g, const float m[12]);

/* The radius of a limb between two rest points, measured off the skin dominated by this palette bone: to the segment, never to the bone's origin (cloth.md: "A BONE IS A LINE, AND ITS ORIGIN IS NOT ITS AXIS"). */
float cc_measure_radius(const CCBody *body, int bone, const float a[3], const float b[3]);

/* What's drawn is one level finer than what's solved: the same points smoothed (Loop subdivision), with a new vertex on every edge, so a fold or a hem reads as cloth for about the cost of one more pass over the points. The mesh to draw comes from these: its vertices and their normals at rest, its triangles, and for each vertex the two of the caller's it's between (-1, -1 for one of the caller's own), for its UV. */
int cc_drawn_vertex_count(const CCGarment *g);
int cc_drawn_index_count(const CCGarment *g);
const int *cc_drawn_indices(const CCGarment *g);
void cc_drawn_read(const CCGarment *g, float *pos, float *nrm, int *parents);

/* Those vertices, for an upload: positions as 3 floats at pos_stride apart, normals octahedral in two uint16s at nrm_stride apart, as Godot's vertex buffer has them. */
void cc_write(const CCGarment *g, unsigned char *pos_out, int pos_stride, unsigned char *nrm_out, int nrm_stride);

int cc_vertex_count(const CCGarment *g);
int cc_point_count(const CCGarment *g);
const float *cc_points(const CCGarment *g);     /* points * 3, this frame, as solved */
const float *cc_shown(const CCGarment *g);      /* points * 3, this frame, as drawn: following what's solved at fabric's pace */
const float *cc_targets(const CCGarment *g);    /* points * 3, what it's held to this frame: the skinned pose, out of whatever it's worn over */
const float *cc_skinned(const CCGarment *g);    /* points * 3, the skinned pose this frame, as the bones put it */
const float *cc_backstops(const CCGarment *g);  /* points * 3, the skin point under each */
const float *cc_backnormals(const CCGarment *g);/* points * 3 */
const float *cc_normals(const CCGarment *g);    /* points * 3, out of the drawn cloth */
const float *cc_limits(const CCGarment *g);     /* points: its max distance */
const float *cc_nears(const CCGarment *g);      /* points: how close to the skin it may come */
int cc_edge_count(const CCGarment *g);

#ifdef __cplusplus
}
#endif
#endif
