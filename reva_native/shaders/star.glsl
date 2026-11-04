// What every part of a star's picture shares: noise, the granules, and a black body's colour.
float h1(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
vec3 h3(vec3 p) { return fract(sin(vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)))) * 43758.5453); }
float vn(vec3 p) { vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(h1(i), h1(i + vec3(1.0, 0.0, 0.0)), f.x), mix(h1(i + vec3(0.0, 1.0, 0.0)), h1(i + vec3(1.0, 1.0, 0.0)), f.x), f.y),
               mix(mix(h1(i + vec3(0.0, 0.0, 1.0)), h1(i + vec3(1.0, 0.0, 1.0)), f.x), mix(h1(i + vec3(0.0, 1.0, 1.0)), h1(i + vec3(1.0, 1.0, 1.0)), f.x), f.y), f.z); }
// the granules: cells of hot gas, a cellular noise whose feature points wander, so the cells boil
vec2 worley(vec3 p, float t) { vec3 i = floor(p), f = fract(p); float d1 = 8.0, d2 = 8.0;
    for (int z = -1; z <= 1; z++) for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) {
        vec3 g = vec3(float(x), float(y), float(z)); vec3 o = h3(i + g); o = 0.5 + 0.42 * sin(t * 0.55 + 6.2831 * o);
        float d = length(g + o - f); if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d; }
    return vec2(d1, d2); }
// a black body's colour (Helland's fit), for a temperature in K
vec3 bb(float T) { float t = clamp(T, 1000.0, 40000.0) / 100.0;
    float r = t <= 66.0 ? 1.0 : clamp(1.292936 * pow(t - 60.0, -0.1332048), 0.0, 1.0);
    float g = t <= 66.0 ? clamp(0.3900816 * log(t) - 0.6318414, 0.0, 1.0) : clamp(1.1298909 * pow(t - 60.0, -0.0755148), 0.0, 1.0);
    float b = t >= 66.0 ? 1.0 : (t <= 19.0 ? 0.0 : clamp(0.5432068 * log(t - 10.0) - 1.1962541, 0.0, 1.0));
    return vec3(r, g, b); }
// c: the centre and the radius. s: how it's stretched, and its clock. t: temperature, a warm white balance on it, its exposure, how fast it turns. k: the wedge cut out of it (where it starts, how wide), how many sunspots, where they start in the ring. r: across the lens (a loop: how far it's turned and its pulse's phase). u: up the lens
layout(push_constant) uniform PC { vec4 c; vec4 s; vec4 t; vec4 k; vec4 r; vec4 u; } P;
