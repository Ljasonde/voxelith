#version 460
layout(location=0) in vec3 pos;layout(push_constant) uniform Light{mat4 vp;} l;
void main(){gl_Position=l.vp*vec4(pos,1);}
