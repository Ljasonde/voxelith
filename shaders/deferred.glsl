layout(binding=0) uniform Globals {mat4 invVP,light[3];vec4 eyeTime,dir,right,up,settings;} g;
layout(binding=1) uniform sampler2D albedo;
layout(binding=2) uniform sampler2D normals;
layout(binding=3) uniform sampler2D depthMap;
vec3 position(vec2 uv,float z){vec4 p=g.invVP*vec4(uv*2-1,z,1);return p.xyz/p.w;}
vec3 solar(){float a=g.settings.y,c=cos(a),s=sin(a);return vec3(-.584*c-.648*s,-.584*s+.648*c,-.488);}
#define sun solar()
float noise2(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);vec4 h=fract(sin(vec4(dot(i,vec2(127.1,311.7)))+vec4(0,127.1,311.7,438.8))*43758.5453);return mix(mix(h.x,h.y,f.x),mix(h.z,h.w,f.x),f.y);}
vec3 sky(vec3 d){
 float y=clamp(d.y,0,1),s=max(dot(d,sun),0.);
 vec3 horizon=mix(vec3(.55,.72,.81),vec3(1.13,.73,.43),pow(s,3));
 vec3 c=mix(horizon,vec3(.095,.34,.57),pow(y,.52));c+=vec3(.6,.32,.12)*pow(s,32.)+vec3(8,5,2)*smoothstep(.9982,.9992,s);
 if(d.y>.015){vec2 uv=d.xz/(d.y+.035)*2.+g.eyeTime.xz*.002;float cloud=noise2(uv)*.57+noise2(uv*2.)*.28+noise2(uv*4.3)*.15;
   c=mix(c,vec3(1.04,.99,.90),smoothstep(.49,.74,cloud)*smoothstep(.015,.20,d.y)*.8);}
 if(d.y<0)c=mix(horizon,vec3(.40,.57,.68),smoothstep(0,.45,-d.y));if(g.settings.y!=0.){float night=1.-smoothstep(-.17,.16,sun.y);vec3 nsky=mix(vec3(.025,.037,.072),vec3(.003,.009,.027),y);
 float moon=smoothstep(.9986,.9993,dot(d,-sun));vec2 stars=floor(vec2(atan(d.z,d.x),asin(clamp(d.y,-1.,1.)))*580.);float h=fract(sin(dot(stars,vec2(12.9898,78.233)))*43758.5453);nsky+=vec3(.60,.72,.91)*(moon*2.5+step(.9985,h)*smoothstep(.02,.4,d.y));c=mix(c,nsky,night);}
 return c;
}
