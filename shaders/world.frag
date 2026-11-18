#version 460
#extension GL_GOOGLE_include_directive:require
#include "shared.glsl"
layout(location=0) in vec3 world;layout(location=1) flat in uint surface;layout(location=2) in float ao;
layout(location=0) out vec4 color;
const vec3 palette[22]=vec3[](vec3(0),vec3(.40,.66,.28),vec3(.48,.32,.21),vec3(.42,.46,.49),vec3(.40,.24,.15),vec3(.26,.53,.28),vec3(.82,.68,.44),vec3(.88,.94,.98),vec3(.88,.53,.66),vec3(.22,.23,.28),vec3(1,.36,.08),vec3(.10,.58,.71),vec3(.46,.53,.54),vec3(.77,.63,.37),vec3(.13,.91,.91),vec3(1,.69,.21),vec3(.31,.52,.40),vec3(.64,.46,.30),vec3(.15,.95,1),vec3(.43,1,.39),vec3(1,.54,.07),vec3(.57,.31,.98));
float hash3(ivec3 p){uvec3 q=uvec3(p)*uvec3(1597334677u,3812015801u,2798796415u);uint n=q.x^q.y^q.z;n=(n^(n>>16))*2246822519u;return float(n^(n>>13))/4294967295.;}
void main(){
 int id=int(surface&255u),f=int(surface>>8),axis=f/2;vec3 n=vec3(0);n[axis]=(f%2==1)?1:-1;vec3 p=world-n*.002;
 vec2 uv=axis==0?p.zy:axis==1?p.xz:p.xy;vec2 tile=fract(uv);ivec2 texel=ivec2(floor(tile*16));
 float grain=hash3(ivec3(texel,id)),coarse=hash3(ivec3(floor(p)));
 vec3 base=palette[id];
 if(id==1 && axis!=1 && fract(p.y)<.73+.12*grain)base=palette[2];
 float tex=.90+.17*grain+.09*coarse;
 if(id==12||id==13||id==16){float row=floor(tile.y*2);vec2 bricks=fract(vec2(uv.x+row*.5,uv.y*2));if(min(bricks.x,bricks.y*.65)<.046)tex*=.66;}
 if(id==4||id==17)tex*=.88+.13*sin(float(texel.x)*2+floor(float(texel.y)/5));
 if(id==8||id==5)tex*=.83+.28*grain;
 if(id==14||id==15||id>=18){tex=.85+.28*grain;if(min(min(tile.x,1-tile.x),min(tile.y,1-tile.y))<.08)tex+=.3;}
 float diffuse=max(dot(n,sun),0);vec3 lit=base*tex*(vec3(.62,.67,.74)+vec3(.51,.44,.32)*diffuse)*ao;
 if(id==10||id==14||id==15||id>=18)lit=mix(lit,base*1.25,.75);
 vec3 eye=cam.eyeTime.xyz-world;float dist=length(eye);
 if(id==11){float wave=sin(world.x*.9+world.z*.3+cam.eyeTime.w*2)*.07;
  vec3 wn=normalize(n+vec3(wave,0,.08*cos(world.z*.8+cam.eyeTime.w)));float fr=pow(1.-abs(dot(normalize(eye),wn)),3);
  lit=mix(base*tex,skyColor(reflect(-normalize(eye),wn)),.25+.65*fr);}
 float fog=1-exp(-dist*.0023-max(0.,52.-world.y)*.007);lit=mix(lit,skyColor(-normalize(eye)),fog);
 color=vec4(lit,1);
}
