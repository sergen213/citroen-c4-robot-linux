#version 450
#extension GL_GOOGLE_include_directive : require

#include "../../material_bloom.glsl"

layout(constant_id = 2) const uint composeMode = 0u;
layout(location = 0) out vec2 uv;

void main()
{
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    if (composeMode >= 1u && composeMode <= 4u) {
        gl_Position = c4BloomClipPosition(uv);
        gl_Position.y = -gl_Position.y;
    } else {
        gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    }
}
