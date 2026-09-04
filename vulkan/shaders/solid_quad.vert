#version 450

// Panel quad: 6 vertices from gl_VertexIndex over a unit square, mapped by a
// push-constant rect already converted to NDC on the CPU (slot 0, offset 0,
// vertex stage). No vertex buffers, one CmdDraw(6) per resolved panel.

layout(push_constant) uniform Push {
    layout(offset = 0) vec4 u_rectNdc; // x, y = top-left NDC; z, w = size in NDC units
} push;

const vec2 CORNERS[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
);

void main() {
    vec2 p = push.u_rectNdc.xy + CORNERS[gl_VertexIndex] * push.u_rectNdc.zw;
    gl_Position = vec4(p, 0.0, 1.0);
}
