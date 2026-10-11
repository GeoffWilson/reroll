#version 450
// The original texture stage: MODULATE (texture colour x vertex colour,
// alpha from the vertex because the RGB565 pages have none), colour-key
// texels (alpha 0 here) dropped when colour keying is on.
layout(set = 0, binding = 0) uniform sampler2D pages[65]; // VKR_SLOTS: 64 game slots and the image slot
layout(push_constant) uniform Draw {
    int slot;
    int textured;
    int colour_key;
} draw;
layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 colour;
layout(location = 2) noperspective in float depth;
layout(location = 0) out vec4 result;
void main()
{
    gl_FragDepth = clamp(depth, 0.0, 1.0);
    if (draw.textured == 0) {
        result = colour;
        return;
    }
    vec4 texel = texture(pages[draw.slot], uv);
    if (draw.colour_key != 0 && texel.a < 0.5)
        discard;
    result = vec4(texel.rgb * colour.rgb, colour.a);
}
