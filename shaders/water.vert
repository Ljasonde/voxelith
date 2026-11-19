#version 460
layout(binding=0) uniform Globals { mat4 invVP,light[3]; vec4 eyeTime,dir,right,up,settings; } g;
layout(location=0) in vec3 pos;
layout(location=1) in uint face;
layout(location=0) out vec3 world;
layout(location=1) flat out vec3 normal;
void main(){
    world=pos;
    int code=int((face>>8)&7u);
    normal=vec3(0);
    normal[code/2]=code%2==1?1:-1;
    gl_Position=inverse(g.invVP)*vec4(pos,1);
}
