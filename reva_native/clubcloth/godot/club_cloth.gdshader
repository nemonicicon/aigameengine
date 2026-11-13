// Whole cloth's look: a garment's own colour and print, matte, seen from both sides, with its shadows looked up a little way out from the cloth. With the sky in it at a glancing angle, cloth read as wet plastic.
// Smooth shaded and low-poly, cloth casting on itself breaks up in dots wherever the sun's at a glancing angle: the shadow lookup lands on its own surface, and its blur reaches further than any bias. So the lookup moves out of him along the normal (`sun_out`, metres: ClubCloth sets it by how bright the sun is). Moving it toward the sun instead was worse every way it was tried: through his body and out the back, or right up against whatever's casting the shadow, dots in every shade. What's left is where the light's nearly along the cloth, and the light fades out before that (`light_fade`, of the cosine). Going by each facet's own normal instead gave a saw-toothed edge to every shade. It moves all the light, not only the sun's: by a torch in his hand it has to stay small. Its shadow on everything else is the cloth's own.
// It writes depth even when it's see-through. The camera fades him out when it's squeezed in close, which draws him with no depth at all, and what's under his clothes came through them in patches.
shader_type spatial;
render_mode cull_disabled, depth_draw_always;

uniform vec4 albedo : source_color = vec4(1.0);
uniform sampler2D albedo_texture : source_color, hint_default_white, filter_linear_mipmap, repeat_enable;
uniform float roughness = 1.0;
uniform float specular = 0.08;
uniform float sun_out = 0.03;
uniform float light_fade = 0.35;

void fragment() {
	ALBEDO = albedo.rgb * texture(albedo_texture, UV).rgb;
	ROUGHNESS = roughness;
	SPECULAR = specular;
	LIGHT_VERTEX = VERTEX + NORMAL * sun_out;
}

void light() {
	float n_dot_l = clamp(dot(NORMAL, LIGHT), 0.0, 1.0);
	DIFFUSE_LIGHT += n_dot_l * smoothstep(0.0, light_fade, n_dot_l) * ATTENUATION * LIGHT_COLOR / PI;
}
