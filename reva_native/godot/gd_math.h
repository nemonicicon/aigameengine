/* gd_math.h -- Godot 4's math types and their methods, in C, as Godot computes them (11-12: the Godot proof of concept, the first file of the
 * Godot layer: what a Godot game's scripts lean on, built once in the engine so a port of the next one finds it here).
 *
 * Header only, everything static inline. Floats, as Godot's default build is (real_t is float). Include it AFTER engine.h: engine.h brings
 * club_math.h, which takes sinf, cosf, atan2f, acosf, expf and powf over to the engine's own math, so a game's rules that use these give the same bits
 * on every machine. The names follow Godot's: gv3_slide is Vector3.slide, gbasis_orthonormalized is Basis.orthonormalized, and so on. Godot's axes:
 * x right, y up, -z forward; a Basis is three columns (x, y, z), Basis * v is x*v.x + y*v.y + z*v.z.
 *
 * What's here is what the skate game's scripts call (skate_controller.gd and its movers, tricks, input and rail graph): add to it as the next
 * port needs, from Godot's own source (core/math/), and keep its answers Godot's. */
#ifndef GD_MATH_H
#define GD_MATH_H
#include <math.h>

#define GD_PI 3.14159265358979323846f
#define GD_TAU 6.28318530717958647692f
#define GD_CMP_EPSILON 0.00001f

typedef struct { float x, y, z; } GV3;
typedef struct { GV3 x, y, z; } GBasis;              /* columns */
typedef struct { float x, y, z, w; } GQuat;
typedef struct { GBasis basis; GV3 origin; } GXform;  /* Transform3D */
typedef struct { GV3 position, size; } GAABB;

/* ---- scalars (@GlobalScope) */
static inline float gd_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float gd_minf(float a, float b) { return a < b ? a : b; }
static inline float gd_maxf(float a, float b) { return a > b ? a : b; }
static inline float gd_absf(float a) { return a < 0.f ? -a : a; }
static inline float gd_signf(float a) { return a > 0.f ? 1.f : a < 0.f ? -1.f : 0.f; }
static inline float gd_lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float gd_inverse_lerp(float a, float b, float v) { return (v - a) / (b - a); }
static inline float gd_deg_to_rad(float d) { return d * (GD_PI / 180.f); }
static inline float gd_rad_to_deg(float r) { return r * (180.f / GD_PI); }
static inline int gd_is_equal_approx(float a, float b) { if (a == b) return 1; float tol = GD_CMP_EPSILON * gd_absf(a); if (tol < GD_CMP_EPSILON) tol = GD_CMP_EPSILON; return gd_absf(a - b) < tol; }
static inline int gd_is_zero_approx(float a) { return gd_absf(a) < GD_CMP_EPSILON; }
static inline float gd_move_toward(float from, float to, float delta) { return gd_absf(to - from) <= delta ? to : from + gd_signf(to - from) * delta; }
static inline float gd_smoothstep(float from, float to, float s) {
    if (gd_is_equal_approx(from, to)) return from <= to ? (s <= from ? 0.f : 1.f) : (s <= to ? 1.f : 0.f);   /* Godot 4.3+: a step */
    const float t = gd_clampf((s - from) / (to - from), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
static inline float gd_fposmod(float x, float y) { float v = fmodf(x, y); if ((v < 0.f && y > 0.f) || (v > 0.f && y < 0.f)) v += y; return v; }
static inline float gd_wrapf(float value, float lo, float hi) {
    const float range = hi - lo;
    if (gd_is_zero_approx(range)) return lo;
    float r = value - (range * floorf((value - lo) / range));
    if (gd_is_equal_approx(r, hi)) return lo;
    return r;
}
static inline float gd_angle_difference(float from, float to) { const float d = fmodf(to - from, GD_TAU); return fmodf(2.f * d, GD_TAU) - d; }
static inline float gd_lerp_angle(float from, float to, float w) { return from + gd_angle_difference(from, to) * w; }
static inline float gd_snappedf(float v, float step) { return step != 0.f ? floorf(v / step + 0.5f) * step : v; }

/* ---- Vector3 */
static inline GV3 gv3(float x, float y, float z) { GV3 v = {x, y, z}; return v; }
#define GV3_ZERO gv3(0.f, 0.f, 0.f)
#define GV3_UP gv3(0.f, 1.f, 0.f)
#define GV3_DOWN gv3(0.f, -1.f, 0.f)
#define GV3_RIGHT gv3(1.f, 0.f, 0.f)
#define GV3_LEFT gv3(-1.f, 0.f, 0.f)
#define GV3_FORWARD gv3(0.f, 0.f, -1.f)
#define GV3_BACK gv3(0.f, 0.f, 1.f)
static inline GV3 gv3_add(GV3 a, GV3 b) { return gv3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline GV3 gv3_sub(GV3 a, GV3 b) { return gv3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline GV3 gv3_mul(GV3 a, float s) { return gv3(a.x * s, a.y * s, a.z * s); }
static inline GV3 gv3_mulv(GV3 a, GV3 b) { return gv3(a.x * b.x, a.y * b.y, a.z * b.z); }
static inline GV3 gv3_neg(GV3 a) { return gv3(-a.x, -a.y, -a.z); }
static inline GV3 gv3_mad(GV3 a, GV3 b, float s) { return gv3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }   /* a + b * s */
static inline float gv3_dot(GV3 a, GV3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline GV3 gv3_cross(GV3 a, GV3 b) { return gv3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float gv3_length_squared(GV3 a) { return gv3_dot(a, a); }
static inline float gv3_length(GV3 a) { return sqrtf(gv3_dot(a, a)); }
static inline GV3 gv3_normalized(GV3 a) { const float l = gv3_length_squared(a); if (l == 0.f) return GV3_ZERO; return gv3_mul(a, 1.f / sqrtf(l)); }   /* Godot: zero stays zero */
static inline int gv3_is_normalized(GV3 a) { return gd_is_equal_approx(gv3_length_squared(a), 1.f); }
static inline float gv3_distance_to(GV3 a, GV3 b) { return gv3_length(gv3_sub(b, a)); }
static inline float gv3_distance_squared_to(GV3 a, GV3 b) { return gv3_length_squared(gv3_sub(b, a)); }
static inline GV3 gv3_direction_to(GV3 a, GV3 b) { return gv3_normalized(gv3_sub(b, a)); }
static inline GV3 gv3_lerp(GV3 a, GV3 b, float t) { return gv3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline GV3 gv3_slide(GV3 v, GV3 n) { return gv3_sub(v, gv3_mul(n, gv3_dot(v, n))); }
static inline GV3 gv3_reflect(GV3 v, GV3 n) { return gv3_sub(gv3_mul(n, 2.f * gv3_dot(v, n)), v); }
static inline GV3 gv3_bounce(GV3 v, GV3 n) { return gv3_neg(gv3_reflect(v, n)); }
static inline GV3 gv3_project(GV3 v, GV3 onto) { const float l = gv3_length_squared(onto); return l == 0.f ? GV3_ZERO : gv3_mul(onto, gv3_dot(v, onto) / l); }
static inline GV3 gv3_limit_length(GV3 v, float len) { const float l = gv3_length(v); return (l > 0.f && len < l) ? gv3_mul(v, len / l) : v; }
static inline float gv3_angle_to(GV3 a, GV3 b) { return atan2f(gv3_length(gv3_cross(a, b)), gv3_dot(a, b)); }
static inline float gv3_signed_angle_to(GV3 a, GV3 b, GV3 axis) { const GV3 c = gv3_cross(a, b); const float u = atan2f(gv3_length(c), gv3_dot(a, b)); return gv3_dot(c, axis) < 0.f ? -u : u; }
static inline GV3 gv3_move_toward(GV3 from, GV3 to, float delta) { const GV3 vd = gv3_sub(to, from); const float l = gv3_length(vd); return (l <= delta || l < GD_CMP_EPSILON) ? to : gv3_add(from, gv3_mul(vd, delta / l)); }
static inline int gv3_is_equal_approx(GV3 a, GV3 b) { return gd_is_equal_approx(a.x, b.x) && gd_is_equal_approx(a.y, b.y) && gd_is_equal_approx(a.z, b.z); }
static inline int gv3_is_zero_approx(GV3 a) { return gd_is_zero_approx(a.x) && gd_is_zero_approx(a.y) && gd_is_zero_approx(a.z); }
static inline GV3 gv3_abs(GV3 a) { return gv3(gd_absf(a.x), gd_absf(a.y), gd_absf(a.z)); }
static inline GV3 gv3_get_any_perpendicular(GV3 a) {   /* Vector3.get_any_perpendicular: crossed with RIGHT, or UP when x isn't the smallest */
    return gv3_normalized(gv3_cross(a, (gd_absf(a.x) <= gd_absf(a.y) && gd_absf(a.x) <= gd_absf(a.z)) ? GV3_RIGHT : GV3_UP));
}

/* ---- Quaternion */
static inline GQuat gquat(float x, float y, float z, float w) { GQuat q = {x, y, z, w}; return q; }
#define GQUAT_IDENTITY gquat(0.f, 0.f, 0.f, 1.f)
static inline GQuat gquat_axis_angle(GV3 axis, float angle) {   /* Quaternion(axis, angle): axis must be normalized */
    const float d = gv3_length(axis); if (d == 0.f) return GQUAT_IDENTITY;
    const float s = sinf(angle * 0.5f) / d;
    return gquat(axis.x * s, axis.y * s, axis.z * s, cosf(angle * 0.5f));
}
static inline GQuat gquat_arc(GV3 from, GV3 to) {   /* Quaternion(arc_from, arc_to): the shortest turn from one to the other, as Godot 4's */
    const GV3 n0 = gv3_normalized(from), n1 = gv3_normalized(to);
    const float d = gv3_dot(n0, n1);
    if (gd_absf(d) > 1.f - GD_CMP_EPSILON) {
        if (d >= 0.f) return GQUAT_IDENTITY;   /* the same way */
        const GV3 ax = gv3_get_any_perpendicular(n0); return gquat(ax.x, ax.y, ax.z, 0.f);   /* opposite: half a turn about any axis square to it */
    }
    const GV3 c = gv3_cross(n0, n1); const float s = sqrtf((1.f + d) * 2.f), rs = 1.f / s;
    return gquat(c.x * rs, c.y * rs, c.z * rs, s * 0.5f);
}
static inline GQuat gquat_mul(GQuat a, GQuat b) {
    return gquat(a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
                 a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}
static inline float gquat_dot(GQuat a, GQuat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
static inline GQuat gquat_normalized(GQuat q) { const float l = sqrtf(gquat_dot(q, q)); return l > 0.f ? gquat(q.x / l, q.y / l, q.z / l, q.w / l) : GQUAT_IDENTITY; }
static inline GQuat gquat_inverse(GQuat q) { return gquat(-q.x, -q.y, -q.z, q.w); }
static inline GV3 gquat_xform(GQuat q, GV3 v) {   /* q * v */
    const GV3 u = gv3(q.x, q.y, q.z), uv = gv3_cross(u, v);
    return gv3_add(v, gv3_add(gv3_mul(uv, 2.f * q.w), gv3_mul(gv3_cross(u, uv), 2.f)));
}
static inline GQuat gquat_slerp(GQuat a, GQuat b, float w) {
    float c = gquat_dot(a, b); GQuat t = b;
    if (c < 0.f) { c = -c; t = gquat(-b.x, -b.y, -b.z, -b.w); }
    float s0, s1;
    if (1.f - c > GD_CMP_EPSILON) { const float om = acosf(c), so = sinf(om); s0 = sinf((1.f - w) * om) / so; s1 = sinf(w * om) / so; }
    else { s0 = 1.f - w; s1 = w; }
    return gquat(s0 * a.x + s1 * t.x, s0 * a.y + s1 * t.y, s0 * a.z + s1 * t.z, s0 * a.w + s1 * t.w);
}
static inline float gquat_get_angle(GQuat q) { return 2.f * acosf(gd_clampf(q.w, -1.f, 1.f)); }
static inline GV3 gquat_get_axis(GQuat q) { if (gd_absf(q.w) > 1.f - GD_CMP_EPSILON) return gv3(q.x, q.y, q.z); const float r = 1.f / sqrtf(1.f - q.w * q.w); return gv3(q.x * r, q.y * r, q.z * r); }

/* ---- Basis (three columns) */
static inline GBasis gbasis(GV3 x, GV3 y, GV3 z) { GBasis b = {x, y, z}; return b; }
#define GBASIS_IDENTITY gbasis(GV3_RIGHT, GV3_UP, GV3_BACK)
static inline GV3 gbasis_xform(GBasis b, GV3 v) { return gv3(b.x.x * v.x + b.y.x * v.y + b.z.x * v.z, b.x.y * v.x + b.y.y * v.y + b.z.y * v.z, b.x.z * v.x + b.y.z * v.y + b.z.z * v.z); }
static inline GV3 gbasis_xform_inv(GBasis b, GV3 v) { return gv3(gv3_dot(b.x, v), gv3_dot(b.y, v), gv3_dot(b.z, v)); }   /* the transpose's: for an orthonormal basis, its inverse */
static inline GBasis gbasis_mul(GBasis a, GBasis b) { return gbasis(gbasis_xform(a, b.x), gbasis_xform(a, b.y), gbasis_xform(a, b.z)); }
static inline GBasis gbasis_transposed(GBasis b) { return gbasis(gv3(b.x.x, b.y.x, b.z.x), gv3(b.x.y, b.y.y, b.z.y), gv3(b.x.z, b.y.z, b.z.z)); }
static inline float gbasis_determinant(GBasis b) { return gv3_dot(b.x, gv3_cross(b.y, b.z)); }
static inline GBasis gbasis_inverse(GBasis b) {
    const float d = gbasis_determinant(b); if (d == 0.f) return GBASIS_IDENTITY;
    const GV3 r0 = gv3_cross(b.y, b.z), r1 = gv3_cross(b.z, b.x), r2 = gv3_cross(b.x, b.y);   /* rows of the inverse, times d */
    const float s = 1.f / d;
    return gbasis(gv3(r0.x * s, r1.x * s, r2.x * s), gv3(r0.y * s, r1.y * s, r2.y * s), gv3(r0.z * s, r1.z * s, r2.z * s));
}
static inline GBasis gbasis_from_quat(GQuat q) {
    const float d = gquat_dot(q, q), s = d > 0.f ? 2.f / d : 0.f;
    const float xs = q.x * s, ys = q.y * s, zs = q.z * s, wx = q.w * xs, wy = q.w * ys, wz = q.w * zs, xx = q.x * xs, xy = q.x * ys, xz = q.x * zs, yy = q.y * ys, yz = q.y * zs, zz = q.z * zs;
    return gbasis(gv3(1.f - (yy + zz), xy + wz, xz - wy), gv3(xy - wz, 1.f - (xx + zz), yz + wx), gv3(xz + wy, yz - wx, 1.f - (xx + yy)));
}
static inline GBasis gbasis_axis_angle(GV3 axis, float angle) { return gbasis_from_quat(gquat_axis_angle(gv3_normalized(axis), angle)); }   /* Basis(axis, angle) */
static inline GBasis gbasis_orthonormalized(GBasis b) {   /* Gram-Schmidt, x first, as Godot */
    GV3 x = gv3_normalized(b.x);
    GV3 y = gv3_normalized(gv3_sub(b.y, gv3_mul(x, gv3_dot(x, b.y))));
    GV3 z = gv3_normalized(gv3_sub(gv3_sub(b.z, gv3_mul(x, gv3_dot(x, b.z))), gv3_mul(y, gv3_dot(y, b.z))));
    return gbasis(x, y, z);
}
static inline GQuat gbasis_get_quaternion(GBasis b) {   /* of an orthonormal basis (Basis.get_quaternion) */
    const float m00 = b.x.x, m11 = b.y.y, m22 = b.z.z, tr = m00 + m11 + m22;
    if (tr > 0.f) { const float s = sqrtf(tr + 1.f) * 2.f; return gquat((b.y.z - b.z.y) / s, (b.z.x - b.x.z) / s, (b.x.y - b.y.x) / s, 0.25f * s); }
    if (m00 > m11 && m00 > m22) { const float s = sqrtf(1.f + m00 - m11 - m22) * 2.f; return gquat(0.25f * s, (b.y.x + b.x.y) / s, (b.z.x + b.x.z) / s, (b.y.z - b.z.y) / s); }
    if (m11 > m22) { const float s = sqrtf(1.f + m11 - m00 - m22) * 2.f; return gquat((b.y.x + b.x.y) / s, 0.25f * s, (b.z.y + b.y.z) / s, (b.z.x - b.x.z) / s); }
    const float s = sqrtf(1.f + m22 - m00 - m11) * 2.f; return gquat((b.z.x + b.x.z) / s, (b.z.y + b.y.z) / s, 0.25f * s, (b.x.y - b.y.x) / s);
}
static inline GQuat gbasis_get_rotation_quaternion(GBasis b) {   /* a mirrored basis is turned inside out first, as Godot's */
    GBasis m = gbasis_orthonormalized(b);
    if (gbasis_determinant(m) < 0.f) m = gbasis(gv3_neg(m.x), gv3_neg(m.y), gv3_neg(m.z));
    return gbasis_get_quaternion(m);
}
static inline GBasis gbasis_looking_at(GV3 target, GV3 up) {   /* Basis.looking_at(target, up): -z toward target */
    const GV3 vz = gv3_neg(gv3_normalized(target)); GV3 vx = gv3_cross(up, vz);
    if (gv3_is_zero_approx(vx)) vx = gv3_get_any_perpendicular(up);   /* the target and up in line */
    vx = gv3_normalized(vx);
    return gbasis(vx, gv3_cross(vz, vx), vz);
}
static inline GV3 gv3_rotated(GV3 v, GV3 axis, float angle) { return gbasis_xform(gbasis_axis_angle(axis, angle), v); }   /* Vector3.rotated */
/* Vector3.slerp, as Godot 4's: about the axis between them, the length lerped; zero-length or in-line vectors lerp */
static inline GV3 gv3_slerp(GV3 a, GV3 b, float w) {
    const float sl = gv3_length_squared(a), el = gv3_length_squared(b);
    if (sl == 0.f || el == 0.f) return gv3_lerp(a, b, w);
    GV3 axis = gv3_cross(a, b); const float al = gv3_length_squared(axis);
    if (al == 0.f) return gv3_lerp(a, b, w);
    axis = gv3_mul(axis, 1.f / sqrtf(al));
    const float s = sqrtf(sl), r = gd_lerpf(s, sqrtf(el), w);
    return gv3_mul(gv3_rotated(a, axis, gv3_angle_to(a, b) * w), r / s);
}
static inline GBasis gbasis_rotated(GBasis b, GV3 axis, float angle) { return gbasis_mul(gbasis_axis_angle(axis, angle), b); }   /* Basis.rotated: rotated in the parent's frame */
static inline GBasis gbasis_slerp(GBasis a, GBasis b, float w) { return gbasis_from_quat(gquat_slerp(gbasis_get_rotation_quaternion(a), gbasis_get_rotation_quaternion(b), w)); }

/* ---- Transform3D */
static inline GXform gxform(GBasis b, GV3 o) { GXform t = {b, o}; return t; }
#define GXFORM_IDENTITY gxform(GBASIS_IDENTITY, GV3_ZERO)
static inline GV3 gxform_xform(GXform t, GV3 v) { return gv3_add(gbasis_xform(t.basis, v), t.origin); }
static inline GXform gxform_mul(GXform a, GXform b) { return gxform(gbasis_mul(a.basis, b.basis), gxform_xform(a, b.origin)); }
static inline GXform gxform_affine_inverse(GXform t) { const GBasis bi = gbasis_inverse(t.basis); return gxform(bi, gv3_neg(gbasis_xform(bi, t.origin))); }
static inline GXform gxform_translated(GXform t, GV3 off) { return gxform(t.basis, gv3_add(t.origin, off)); }   /* Godot 4: in the parent's frame */
static inline GXform gxform_looking_at(GXform t, GV3 target, GV3 up) { return gxform(gbasis_looking_at(gv3_sub(target, t.origin), up), t.origin); }

/* ---- AABB */
static inline GAABB gaabb(GV3 p, GV3 s) { GAABB a = {p, s}; return a; }
static inline GV3 gaabb_end(GAABB a) { return gv3_add(a.position, a.size); }
static inline GV3 gaabb_center(GAABB a) { return gv3_add(a.position, gv3_mul(a.size, 0.5f)); }
static inline GAABB gaabb_expand(GAABB a, GV3 p) {
    GV3 lo = a.position, hi = gaabb_end(a);
    lo = gv3(p.x < lo.x ? p.x : lo.x, p.y < lo.y ? p.y : lo.y, p.z < lo.z ? p.z : lo.z);
    hi = gv3(p.x > hi.x ? p.x : hi.x, p.y > hi.y ? p.y : hi.y, p.z > hi.z ? p.z : hi.z);
    return gaabb(lo, gv3_sub(hi, lo));
}
static inline GAABB gaabb_grow(GAABB a, float by) { return gaabb(gv3_sub(a.position, gv3(by, by, by)), gv3_add(a.size, gv3(2.f * by, 2.f * by, 2.f * by))); }
static inline int gaabb_has_point(GAABB a, GV3 p) { const GV3 e = gaabb_end(a); return p.x >= a.position.x && p.y >= a.position.y && p.z >= a.position.z && p.x <= e.x && p.y <= e.y && p.z <= e.z; }
static inline int gaabb_intersects(GAABB a, GAABB b) { const GV3 ae = gaabb_end(a), be = gaabb_end(b);
    return !(a.position.x >= be.x || ae.x <= b.position.x || a.position.y >= be.y || ae.y <= b.position.y || a.position.z >= be.z || ae.z <= b.position.z); }

/* ---- Geometry3D */
static inline void gd_closest_points_between_segments(GV3 p1, GV3 p2, GV3 q1, GV3 q2, GV3 *on_p, GV3 *on_q) {   /* Geometry3D.get_closest_points_between_segments */
    const GV3 d1 = gv3_sub(p2, p1), d2 = gv3_sub(q2, q1), r = gv3_sub(p1, q1);
    const float a = gv3_dot(d1, d1), e = gv3_dot(d2, d2), f = gv3_dot(d2, r);
    float s = 0.f, t = 0.f;
    if (a <= GD_CMP_EPSILON && e <= GD_CMP_EPSILON) { *on_p = p1; *on_q = q1; return; }
    if (a <= GD_CMP_EPSILON) { s = 0.f; t = gd_clampf(f / e, 0.f, 1.f); }
    else {
        const float c = gv3_dot(d1, r);
        if (e <= GD_CMP_EPSILON) { t = 0.f; s = gd_clampf(-c / a, 0.f, 1.f); }
        else {
            const float b = gv3_dot(d1, d2), denom = a * e - b * b;
            s = denom != 0.f ? gd_clampf((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
            t = (b * s + f) / e;
            if (t < 0.f) { t = 0.f; s = gd_clampf(-c / a, 0.f, 1.f); }
            else if (t > 1.f) { t = 1.f; s = gd_clampf((b - c) / a, 0.f, 1.f); }
        }
    }
    *on_p = gv3_mad(p1, d1, s); *on_q = gv3_mad(q1, d2, t);
}
static inline int gd_ray_intersects_triangle(GV3 from, GV3 dir, GV3 a, GV3 b, GV3 c, GV3 *at) {   /* Geometry3D.ray_intersects_triangle (Moller-Trumbore, two-sided) */
    const GV3 e1 = gv3_sub(b, a), e2 = gv3_sub(c, a), h = gv3_cross(dir, e2);
    const float det = gv3_dot(e1, h);
    if (gd_is_zero_approx(det)) return 0;
    const float f = 1.f / det; const GV3 s = gv3_sub(from, a);
    const float u = f * gv3_dot(s, h); if (u < 0.f || u > 1.f) return 0;
    const GV3 q = gv3_cross(s, e1); const float v = f * gv3_dot(dir, q); if (v < 0.f || u + v > 1.f) return 0;
    const float t = f * gv3_dot(e2, q); if (t <= GD_CMP_EPSILON) return 0;
    if (at) *at = gv3_mad(from, dir, t);
    return 1;
}
#endif
