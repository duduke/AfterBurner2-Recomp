#version 450
// Green proves the host bound the supplied microseconds, without conversion.
layout(push_constant) uniform Timing { uint FrameTimeDelta; } clock_value;
layout(std140,set=0,binding=0) uniform Transform { mat4 MVP; } transform;
#pragma stage vertex
layout(location=0) in vec4 VertexPosition;
layout(location=1) in vec2 TexturePosition;
layout(location=0) out vec2 coordinate;
void main() { gl_Position=transform.MVP*VertexPosition; coordinate=TexturePosition; }
#pragma stage fragment
layout(location=0) in vec2 coordinate;
layout(location=0) out vec4 color;
void main() { color=clock_value.FrameTimeDelta==12345u && coordinate.x>=0.0 ? vec4(0,1,0,1) : vec4(1,0,0,1); }
