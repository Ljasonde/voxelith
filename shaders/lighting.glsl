#include "deferred.glsl"
layout(location=0) in vec2 ndc;layout(location=0) out vec4 color;
#ifdef HARDWARE_RT
layout(binding=15) uniform accelerationStructureEXT scene;
float trace(vec3 origin,vec3 dir,float farT,bool closest){
 rayQueryEXT q;rayQueryInitializeEXT(q,scene,closest?gl_RayFlagsOpaqueEXT:(gl_RayFlagsOpaqueEXT|gl_RayFlagsTerminateOnFirstHitEXT),255,origin,.025,dir,farT);
 while(rayQueryProceedEXT(q)){}return rayQueryGetIntersectionTypeEXT(q,true)==gl_RayQueryCommittedIntersectionNoneEXT?-1.:rayQueryGetIntersectionTEXT(q,true);
}
#else
layout(binding=4) uniform sampler2D nearShadow;layout(binding=5) uniform sampler2D midShadow;layout(binding=6) uniform sampler2D farShadow;
layout(binding=7) uniform sampler2D aoMap;
float compareShadow(sampler2D map,vec3 p,float bias){float sum=0.;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)sum+=float(texture(map,p.xy+vec2(x,y)/2048.).r+bias>=p.z);return sum/9.;}
float shadowAt(vec3 world,vec3 n){for(int i=0;i<3;i++){vec4 q=g.light[i]*vec4(world+n*.08,1);vec3 p=q.xyz/q.w;p.xy=p.xy*.5+.5;
 if(all(greaterThan(p,vec3(.002)))&&all(lessThan(p,vec3(.998)))){float b=.00022*(1.+i);if(i==0)return compareShadow(nearShadow,p,b);if(i==1)return compareShadow(midShadow,p,b);return compareShadow(farShadow,p,b);}}return 1.;}
#endif
void main(){vec2 uv=ndc*.5+.5;float z=texture(depthMap,uv).r;vec3 rd=normalize(g.dir.xyz+ndc.x*g.right.xyz*g.right.w-ndc.y*g.up.xyz*g.up.w);
 if(z>=1.){color=vec4(sky(rd),1);return;}vec3 p=position(uv,z);vec4 material=texture(albedo,uv);int id=int(round(material.a*255));vec4 surface=texture(normals,uv);
 vec3 n=normalize(round(surface.rgb*2.-1.));vec3 base=pow(material.rgb,vec3(2.2));float shade=1.,ao=1.,ndl=max(dot(n,sun),0.);
#ifdef HARDWARE_RT
 vec3 tangent=normalize(cross(abs(n.y)>.9?vec3(1,0,0):vec3(0,1,0),n)),bitangent=cross(n,tangent);
 float rng=fract(dot(floor(p*8.),vec3(.1031,.11369,.13787)));vec3 start=p+n*.045;
 if(ndl>0.&&g.settings.x>.5){shade=0.;for(int i=0;i<2;i++){float a=(rng+i*.5)*6.283;vec3 l=normalize(sun+.024*(tangent*cos(a)+bitangent*sin(a)));shade+=.5*float(trace(start,l,260.,false)<0.);}}
 for(int i=0;i<2;i++){float a=6.283*(rng+float(i)*.5);vec3 d=normalize(n*.84+tangent*cos(a)+bitangent*sin(a));float hit=trace(start,d,3.5,false);if(hit>0.)ao-=.20*(1.-hit/3.5);}
#else
 if(ndl>0.&&g.settings.x>.5)shade=shadowAt(p,n);
 float acc=0.,weight=0.;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){vec2 q=uv+vec2(x,y)/vec2(textureSize(aoMap,0));float w=exp(-length(position(q,texture(depthMap,q).r)-p)*2.);acc+=texture(aoMap,q).r*w;weight+=w;}ao=acc/max(.001,weight);
#endif
 vec3 light=vec3(.32,.41,.52)*(.65+.35*max(0.,n.y))*ao*surface.a+vec3(1.76,1.32,.89)*ndl*shade;
 vec3 lit=base*light;
 if(id==8)lit+=base*vec3(.12,.065,.06);
 if(id==14||id==15||id==10||id>=18)lit+=base*(id>=18?5.:2.9);
 if(id==11||id==14){vec3 wn=n;if(id==11)wn=normalize(n+vec3(.095*sin(p.x*.8+g.eyeTime.w*1.1),0,.075*cos(p.z*.69+g.eyeTime.w)));
   vec3 refl=reflect(rd,wn),ref=sky(refl);
#ifdef HARDWARE_RT
   float t=trace(p+n*.06,refl,170.,true);if(t>0.){vec3 h=p+n*.06+refl*t;
    vec3 delta=h-g.eyeTime.xyz;float forward=dot(delta,g.dir.xyz);vec2 q=vec2(dot(delta,g.right.xyz)/g.right.w,-dot(delta,g.up.xyz)/g.up.w)/max(.001,forward)*.5+.5;
    ref=vec3(.13,.22,.20);if(forward>0.&&all(greaterThan(q,vec2(0)))&&all(lessThan(q,vec2(1)))&&distance(position(q,texture(depthMap,q).r),h)<2.5)ref=pow(texture(albedo,q).rgb,vec3(2.2));}
#endif
   float fres=.12+.76*pow(1.-abs(dot(-rd,wn)),3.);lit=mix(lit,ref,id==11?fres:.16);}
 float distanceToEye=distance(p,g.eyeTime.xyz);float density=.0017+.0028*exp(-max(0.,p.y-34.)*.09);
 float fog=1.-exp(-distanceToEye*density);lit=mix(lit,sky(rd)*(.87+.13*shade),fog);color=vec4(lit,1);
}
