#version 450
// Self-authored test shader. No game or third-party shader data.
#pragma parameter TEST_GAIN "Test gain" 1.0 0.0 1.0 0.25
layout(push_constant) uniform Values { float TEST_GAIN; } parameters;
layout(std140, set=0, binding=0) uniform Transform { mat4 MVP; } transform;
#pragma stage vertex
layout(location=0) in vec4 VertexPosition;
layout(location=1) in vec2 TexturePosition;
layout(location=0) out vec2 coordinate;
void main() { gl_Position=transform.MVP*VertexPosition; coordinate=TexturePosition; }
#pragma stage fragment
layout(location=0) in vec2 coordinate;
layout(location=0) out vec4 color;
layout(set=0,binding=1) uniform sampler2D Source;
void main() { color=vec4(texture(Source,coordinate).bgr*parameters.TEST_GAIN,1.0); }
