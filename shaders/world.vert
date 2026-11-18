#version 460
#extension GL_GOOGLE_include_directive:require
#include "shared.glsl"
layout(location=0) in vec3 pos;layout(location=1) in uint face;
layout(location=0) out vec3 world;layout(location=1) flat out uint surface;layout(location=2) out float ao;
void main(){gl_Position=cam.vp*vec4(pos,1);world=pos;surface=face&4095u;ao=1.-.13*float((face>>12)&3u);}
