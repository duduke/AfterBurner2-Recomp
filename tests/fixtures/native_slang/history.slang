#version 450
// Self-authored temporal test; the previous original input supplies half RGB.
layout(std140,set=0,binding=0) uniform Transform { mat4 MVP; } transform;
#pragma stage vertex
layout(location=0) in vec4 VertexPosition;
layout(location=1) in vec2 TexturePosition;
layout(location=0) out vec2 coordinate;
void main() { gl_Position=transform.MVP*VertexPosition; coordinate=TexturePosition; }
#pragma stage fragment
layout(location=0) in vec2 coordinate;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D Source;
layout(set=0,binding=2) uniform sampler2D OriginalHistory1;
void main() {
    color=vec4(0.5*texture(Source,coordinate).rgb+0.5*texture(OriginalHistory1,coordinate).rgb,1.0);
}
