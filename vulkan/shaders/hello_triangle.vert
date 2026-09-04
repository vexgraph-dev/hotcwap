#version 450

// Fullscreen triangle: 3 oversized vertices, no vertex buffers.
// gl_VertexIndex picks one corner; the interpolation covers the viewport.

layout(location = 0) out vec2 v_uv;   // raw clip-space coords, spans [-1, 3]
layout(location = 1) out float v_time;

layout(push_constant) uniform Push {
    float u_time;                     // engine pushes float seconds (offset 0, 4 bytes, VERTEX stage)
} push;

const vec2 POSITIONS[3] = vec2[3](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

void main() {
    v_time = push.u_time;
    vec2 p = POSITIONS[gl_VertexIndex];
    gl_Position = vec4(p, 0.0, 1.0);
    v_uv = p;
}
