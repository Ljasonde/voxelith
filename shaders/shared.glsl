layout(push_constant) uniform Camera {mat4 vp;vec4 eyeTime,dir,right,up;} cam;
const vec3 sun=vec3(-.584,.648,-.488);
vec3 skyColor(vec3 d){
 float y=clamp(d.y,0,1),s=max(dot(sun,d),0);
 vec3 horizon=mix(vec3(.75,.84,.89),vec3(1,.80,.64),pow(s,4));
 vec3 sky=mix(horizon,vec3(.22,.48,.71),pow(y,.48));
 sky+=vec3(.75,.52,.30)*pow(s,42)+vec3(1,.84,.64)*smoothstep(.9985,.9993,s);
 if(d.y<0)sky=mix(horizon,vec3(.52,.66,.78),smoothstep(0,.6,-d.y));
 return sky;
}
