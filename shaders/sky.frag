#version 460
#extension GL_GOOGLE_include_directive:require
#include "shared.glsl"
layout(location=0) in vec2 uv;layout(location=0) out vec4 outColor;
void main(){vec3 d=normalize(cam.dir.xyz+uv.x*cam.right.xyz*cam.right.w-uv.y*cam.up.xyz*cam.up.w);outColor=vec4(skyColor(d),1);}
