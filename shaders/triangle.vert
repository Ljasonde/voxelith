#version 460
layout(location=0) out vec3 col;
void main(){
 vec2 p[3]=vec2[](vec2(-.56,.46),vec2(.56,.46),vec2(0,-.62));
 vec3 c[3]=vec3[](vec3(.04,.77,.68),vec3(.98,.60,.27),vec3(.43,.62,1));
 gl_Position=vec4(p[gl_VertexIndex],0,1); col=c[gl_VertexIndex];
}
