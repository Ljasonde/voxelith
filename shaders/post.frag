#version 460
layout(location=0) in vec2 ndc;layout(location=0) out vec4 outColor;
layout(binding=8) uniform sampler2D hdr;
layout(binding=9) uniform sampler2D bloom1;layout(binding=10) uniform sampler2D bloom2;layout(binding=11) uniform sampler2D bloom3;
layout(binding=12) uniform sampler2D bloom4;layout(binding=13) uniform sampler2D bloom5;layout(binding=14) uniform sampler2D bloom6;
float luma(vec3 v){return dot(v,vec3(.299,.587,.114));}
vec3 bright(sampler2D b,vec2 p){return max(vec3(0),texture(b,p).rgb-.92);}
void main(){vec2 uv=ndc*.5+.5,px=1./vec2(textureSize(hdr,0));vec3 c=texture(hdr,uv).rgb;
 float a=luma(texture(hdr,uv+px*vec2(-1,-1)).rgb),b=luma(texture(hdr,uv+px*vec2(1,-1)).rgb),d=luma(texture(hdr,uv+px*vec2(-1,1)).rgb),e=luma(texture(hdr,uv+px).rgb);
 vec2 dir=vec2(-(a+b-d-e),a+d-b-e);dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+max((a+b+d+e)*.03125,.0078)),vec2(-6),vec2(6))*px;
 if(max(max(a,b),max(d,e))-min(min(a,b),min(d,e))>.085)c=mix(c,.5*(texture(hdr,uv+dir*.25).rgb+texture(hdr,uv-dir*.25).rgb),.65);
 c+=.14*(bright(bloom1,uv)+bright(bloom2,uv)+bright(bloom3,uv)+bright(bloom4,uv)+bright(bloom5,uv)+bright(bloom6,uv));
 c*=.9;c=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);outColor=vec4(pow(clamp(c,0.,1.),vec3(1./2.2)),1);}
