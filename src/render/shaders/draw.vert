#version 450
// Positions arrive already in clip space (projected on the CPU from the
// draw list's camera-space vertices, w = camera z for perspective-correct
// texturing, as the original's rhw = 1/z).
layout(location = 0) in vec4 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_colour;
layout(location = 3) in float in_depth;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 colour;
// D3D TL vertices: sz = z / 65536, interpolated linearly across the screen
layout(location = 2) noperspective out float depth;
void main()
{
    gl_Position = in_pos;
    uv = in_uv;
    colour = in_colour;
    depth = in_depth;
}
