#include "deferred.glsl"
layout(location=0) in vec3 world;
layout(location=1) flat in vec3 normal;
layout(location=0) out vec4 color;
#ifdef HARDWARE_RT
layout(binding=15) uniform accelerationStructureEXT scene;
#endif

vec2 projectUV(vec3 point){
    vec3 delta=point-g.eyeTime.xyz;
    float z=dot(delta,g.dir.xyz);
    if(z<=0.)return vec2(-1);
    return vec2(dot(delta,g.right.xyz)/g.right.w,-dot(delta,g.up.xyz)/g.up.w)/z*.5+.5;
}
bool inside(vec2 uv){return all(greaterThanEqual(uv,vec2(0))) && all(lessThan(uv,vec2(1)));}
vec3 surfaceColor(vec2 uv,vec3 direction){
    if(texture(depthMap,uv).r>=1.)return sky(direction);
    return pow(texture(albedo,uv).rgb,vec3(2.2))*.85;
}
void main(){
    vec2 uv=gl_FragCoord.xy/vec2(textureSize(depthMap,0));
    float opaqueDepth=texture(depthMap,uv).r;
    // Occluded fragments fail the LESS test against the cleared water depth.
    gl_FragDepth=gl_FragCoord.z>=opaqueDepth?1.:gl_FragCoord.z;
    vec3 eye=g.eyeTime.xyz, incident=normalize(world-eye);
    float time=g.eyeTime.w;
    vec3 n=normalize(normal+vec3(.08*sin(world.x*.82+time*1.4),0,.09*cos(world.z*.65-time*1.2)));
    if(dot(n,incident)>0.)n=-n;
    vec3 reflected=reflect(incident,n),environment=sky(reflected),origin=world+n*.05;
    vec2 hitUV=vec2(-1);
#ifdef HARDWARE_RT
    rayQueryEXT query;
    rayQueryInitializeEXT(query,scene,gl_RayFlagsOpaqueEXT,255,origin,.025,reflected,170.);
    while(rayQueryProceedEXT(query)){}
    if(rayQueryGetIntersectionTypeEXT(query,true)!=gl_RayQueryCommittedIntersectionNoneEXT){
        vec3 hit=origin+reflected*rayQueryGetIntersectionTEXT(query,true);
        vec2 screen=projectUV(hit);
        environment=vec3(.11,.19,.17);
        if(inside(screen)&&distance(hit,position(screen,texture(depthMap,screen).r))<2.5)hitUV=screen;
    }
#else
    for(int i=1;i<=18;i++){
        vec3 hit=origin+reflected*(float(i*i)*.14);
        vec2 screen=projectUV(hit);
        if(!inside(screen))break;
        float z=texture(depthMap,screen).r;
        if(z>=1.)continue;
        float behind=dot(hit-position(screen,z),g.dir.xyz);
        if(behind>=0.&&behind<1.+float(i)*.14){hitUV=screen;break;}
    }
#endif
    if(inside(hitUV))environment=surfaceColor(hitUV,reflected);
    vec2 refraction=clamp(uv+n.xz*.009,0.,1.);
    float z=texture(depthMap,refraction).r;
    if(z<gl_FragCoord.z){refraction=uv;z=opaqueDepth;}
    float thickness=z>=1.?8.:min(12.,distance(position(refraction,z),world));
    vec3 trans=exp(-thickness*vec3(.26,.085,.055));
    vec3 water=surfaceColor(refraction,incident)*trans+vec3(.035,.30,.35)*(1.-trans);
    float fresnel=.025+.975*pow(1.-max(dot(-incident,n),0.),5.);
    vec3 radiance=mix(water,environment,fresnel)+vec3(1.4,1.1,.7)*pow(max(dot(reflected,sun),0.),100.);
    color=vec4(mix(radiance,sky(incident),1.-exp(-distance(world,eye)*.002)),1.);
}
