/* Holly's head, rendered off-screen so the dancefloor can hang it on a wall.
 *
 * The venue wants jumbotrons -- Dreadzone-style screens around the floor with a face on them.
 * That is one head drawn once into a texture, then sampled by as many quads as you like: ten
 * screens cost the same as one.
 *
 * The small render target is not a compromise. Holly's jaw is a vertex-shader stretch on a mesh
 * with sealed lips and no mouth interior; it only reads as a mouth because everything is crushed
 * to ~160x120 and blown back up. On an arena screen seen from across a room that is exactly the
 * right look.
 *
 * Mouth state is the (open, wide, seal) triple the scene's voice track drives.
 */
#ifndef HOLLY_FBO_H
#define HOLLY_FBO_H

/* Build the head, its texture, its interior and the off-screen target.
 * Call once, after gfxInit. Returns 0 on failure (already reported). */
int holly_fbo_init(int w, int h, const char *model_path);

/* Draw the head into the off-screen target, this frame. `t` drives the idle sway so he is not a corpse
 * between sentences. */
void holly_fbo_render(float open, float wide, float seal, double t);

/* The colour texture the screens sample. 0 until holly_fbo_init succeeds. */
unsigned int holly_fbo_texture(void);

/* Off-screen size, for anyone wanting to match aspect on a quad. */
int holly_fbo_width(void);
int holly_fbo_height(void);

#endif

/* ROBOT NEKO ep 9 probe: renders so far. */
unsigned holly_fbo_renders(void);
