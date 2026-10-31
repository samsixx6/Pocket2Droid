attribute vec4 aPosition;
attribute vec3 aNormal;
attribute vec4 aDiffuse,aSpecular;
attribute vec3 aTex0,aTex1;
uniform mat4 uWorld,uView,uProjection,uTex0,uTex1;
uniform vec4 uViewport,uVertexFlags,uRender,uTextureInfo,uTexSizes;
uniform vec4 uMaterialD,uMaterialA,uMaterialS,uLighting,uLightFlags,uAmbient;
uniform vec4 uLightPos[8],uLightD[8],uLightA[8],uLightS[8],uLightAtt[8];
varying vec4 vColor,vSpec;
varying vec3 vTex0,vTex1;
varying float vFog;
vec4 source(float which,vec4 material,vec4 diffuse,vec4 specular){
 return which<0.5?material:which<1.5?(uVertexFlags.z>0.5?diffuse:material):(uVertexFlags.w>0.5?specular:material);
}
vec3 texcoord(vec3 inputCoord,mat4 transform,float flags,float size,float clipW){
 vec4 t=vec4(inputCoord,1.0);
 if(flags>0.5){
  t=size<1.5?vec4(inputCoord.x,1.0,0.0,0.0):size<2.5?vec4(inputCoord.xy,1.0,0.0):vec4(inputCoord,1.0);
  t=transform*t;
 }
 vec3 result=vec3(t.x,flags>0.5&&flags<1.5?0.0:t.y,flags>256.0?t.z:1.0);
 if(uRender.w<0.5)result*=clipW;
 return result;
}
/* Bias pixel-center ties by 1/256 pixel to retain D3DM top/left coverage
   after the GL Y inversion, without shifting the image by half a pixel. */
void main(){
 vec4 diffuse=uVertexFlags.z>0.5?aDiffuse.bgra:vec4(1.0);
 vec4 specular=uVertexFlags.w>0.5?aSpecular.bgra:vec4(0.0);
 vec4 world=uWorld*vec4(aPosition.xyz,1.0);
 vec4 eye=uView*world;
 vec4 clip=uProjection*eye;
 if(uVertexFlags.x>0.5){
  float w=1.0/aPosition.w;
  vec2 xy=(aPosition.xy-uViewport.xy+vec2(0.49609375))/uViewport.zw;
  clip=vec4((2.0*xy.x-1.0)*w,(1.0-2.0*xy.y)*w,(2.0*aPosition.z-1.0)*w,w);
 }else{
  clip.z=2.0*clip.z-clip.w;
  clip.xy+=vec2(0.9921875/uViewport.z,-0.9921875/uViewport.w)*clip.w;
 }
 vColor=diffuse;vSpec=specular;vFog=abs(eye.z);
 if(uRender.x>0.5&&uVertexFlags.x<0.5){
  mat3 mv=mat3(uView*uWorld);
  vec3 a=cross(mv[1],mv[2]),b=cross(mv[2],mv[0]),c=cross(mv[0],mv[1]);
  vec3 n=mat3(a,b,c)*(uVertexFlags.y>0.5?aNormal:vec3(0.0,0.0,1.0))/dot(mv[0],a);
  if(uRender.y>0.5)n=normalize(n);
  vec4 md=source(uLighting.z,uMaterialD,diffuse,specular);
  vec4 ma=source(uLighting.w,uMaterialA,diffuse,specular);
  vec4 ms=source(uLightFlags.x,uMaterialS,diffuse,specular);
  vec3 color=uAmbient.rgb*ma.rgb,shine=vec3(0.0);
  vec3 viewer=uLighting.x>0.5?normalize(-eye.xyz):vec3(0.0,0.0,-1.0);
  for(int i=0;i<8;i++){
   if(float(i)>=uLightFlags.y)break;
   vec3 delta=(uView*vec4(uLightPos[i].xyz,uLightPos[i].w)).xyz;
   float attenuation=1.0;
   if(uLightPos[i].w>0.5){
    delta-=eye.xyz;float distance=length(delta);
    attenuation=distance>uLightAtt[i].w?0.0:1.0/max(dot(uLightAtt[i].xyz,vec3(1.0,distance,distance*distance)),0.000001);
   }else delta=-delta;
   vec3 direction=normalize(delta);
   float ndotl=max(dot(n,direction),0.0);
   color+=attenuation*(uLightA[i].rgb*ma.rgb+uLightD[i].rgb*md.rgb*ndotl);
   if(ndotl>0.0&&uRender.z>0.5)shine+=attenuation*uLightS[i].rgb*ms.rgb*pow(max(dot(n,normalize(direction+viewer)),0.0),uLighting.y);
  }
  vColor=vec4(clamp(color,0.0,1.0),md.a);vSpec=vec4(clamp(shine,0.0,1.0),specular.a);
 }
 vTex0=texcoord(uTextureInfo.x<0.5?aTex0:aTex1,uTex0,uTextureInfo.y,uTextureInfo.x<0.5?uTexSizes.x:uTexSizes.y,clip.w);
 vTex1=texcoord(uTextureInfo.z<0.5?aTex0:aTex1,uTex1,uTextureInfo.w,uTextureInfo.z<0.5?uTexSizes.x:uTexSizes.y,clip.w);
 gl_Position=clip;gl_PointSize=1.0;
}
