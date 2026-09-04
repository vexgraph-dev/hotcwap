#version 450

// Animated cosine-palette background with a bobbing neon triangle:
//   - base color: IQ cosine palette 0.5 + 0.5*cos(t + uv.xyx * vec3(0, 2, 4))
//   - inside the triangle: the same palette at double speed, mixed 85% over base
//   - outside: red-orange glow = exp(-14 * distance-to-nearest-edge) * 0.8
//
// v_uv carries raw clip-space coords in [-1, 3], which is the same space the
// triangle vertices live in — that is why the half-plane tests below use those
// exact constants.

layout(location = 0) in  vec2 v_uv;
layout(location = 1) in  float v_time;

layout(location = 0) out vec4 fragColor;

float segDist(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a;
    vec2 ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    float t = v_time;
    vec2 uv = v_uv;

    // Window-surface domain: flat dark blue. This is the layer that "never
    // has gaps" — it is repainted for every canvas pixel every frame.
    vec3 col = vec3(0.02, 0.04, 0.18);

    float w = 0.35 * sin(t * 1.4);
    vec2 a = vec2(-0.9 + w, -0.7);
    vec2 b = vec2( 0.5 + w, -0.1);
    vec2 c = vec2(-0.3 + w,  0.8);

    float d1 =  0.6 * (uv.x - a.x) - 1.4 * (uv.y - a.y);
    float d2 =  0.9 * (uv.x - b.x) + 0.8 * (uv.y - b.y);
    float d3 = -1.5 * (uv.x - c.x) + 0.6 * (uv.y - c.y);

    if (d1 >= 0.0 && d2 >= 0.0 && d3 >= 0.0) {
        // Panel domain: the animated cosine palette lives only here.
        vec3 inner = vec3(0.5) + 0.5 * cos(vec3(t) + uv.xyx + vec3(0.0, 2.0, 4.0));
        col = mix(col, inner, vec3(0.92));
    } else {
        float dist = segDist(uv, a, b);
        dist = min(dist, segDist(uv, b, c));
        dist = min(dist, segDist(uv, c, a));
        col += exp(-14.0 * dist) * 0.8 * vec3(1.0, 0.4, 0.4);
    }

    fragColor = vec4(col, 1.0);
}
