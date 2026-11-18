#version 460
#extension GL_GOOGLE_include_directive:require
#include "deferred.glsl"
layout(location=0) in vec2 ndc;layout(location=0) out vec4 color;
void main(){vec2 uv=ndc*.5+.5;float z=texture(depthMap,uv).r;if(z==1.){color=vec4(1);return;}
 vec3 p=position(uv,z),n=normalize(texture(normals,uv).rgb*2.-1.);float occ=0.;
 float radius=clamp(1.8/max(2.,distance(g.eyeTime.xyz,p)),.002,.09);vec2 aspect=vec2(float(textureSize(depthMap,0).y)/textureSize(depthMap,0).x,1);
 for(int i=0;i<12;i++){float a=float(i)*2.39996;vec2 q=uv+radius*aspect*sqrt((i+.5)/12.)*vec2(cos(a),sin(a));
 vec3 delta=position(q,texture(depthMap,q).r)-p;float len=length(delta);occ+=max(0.,dot(n,delta)/(len+.001)-.10)*(1.-smoothstep(.15,3.,len));}
 color=vec4(vec3(1.-occ/12.*1.8),1);}
