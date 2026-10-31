precision mediump float;
uniform sampler2D uSampler0,uSampler1;
uniform vec4 uFactor,uColorOp,uAlphaOp,uColorArgs0,uColorArgs1,uAlphaArgs0,uAlphaArgs1;
uniform vec4 uFragment,uFog,uFogColor;
varying vec4 vColor,vSpec;
varying vec3 vTex0,vTex1;
varying float vFog;
vec4 argument(float bits,vec4 current,vec4 texel,vec4 temp){
 float which=mod(bits,16.0);
 vec4 v=which<0.5?vColor:which<1.5?current:which<2.5?texel:which<3.5?uFactor:which<4.5?vSpec:temp;
 if(mod(floor(bits/16.0),2.0)>0.5)v=vec4(1.0)-v;
 if(bits>=32.0)v=vec4(v.a);
 return v;
}
vec4 operation(float op,vec4 a,vec4 b,vec4 c,vec4 current,vec4 texel){
 if(op<2.5)return b;
 if(op<3.5)return c;
 if(op<4.5)return b*c;
 if(op<5.5)return 2.0*b*c;
 if(op<6.5)return 4.0*b*c;
 if(op<7.5)return b+c;
 if(op<8.5)return b+c-vec4(0.5);
 if(op<9.5)return 2.0*(b+c-vec4(0.5));
 if(op<10.5)return b-c;
 if(op<11.5)return b+c-b*c;
 if(op<12.5)return mix(c,b,vColor.a);
 if(op<13.5)return mix(c,b,texel.a);
 if(op<14.5)return mix(c,b,uFactor.a);
 if(op<15.5)return b+c*(1.0-texel.a);
 if(op<16.5)return mix(c,b,current.a);
 if(op<18.5)return b+vec4(b.a)*c;
 if(op<19.5)return b*c+vec4(b.a);
 if(op<20.5)return b+vec4(1.0-b.a)*c;
 if(op<21.5)return (vec4(1.0)-b)*c+vec4(b.a);
 if(op<24.5)return vec4(dot(2.0*b.rgb-1.0,2.0*c.rgb-1.0));
 if(op<25.5)return b*c+a;
 return a*b+(vec4(1.0)-a)*c;
}
bool compare(float a,float b,float f){
 if(f<1.5)return false;if(f<2.5)return a<b;if(f<3.5)return a==b;
 if(f<4.5)return a<=b;if(f<5.5)return a>b;if(f<6.5)return a!=b;
 if(f<7.5)return a>=b;return true;
}
void main(){
 vec4 current=vColor,temp=vec4(0.0);
 vec4 texel=uColorOp.z>0.5?texture2D(uSampler0,vTex0.xy/vTex0.z):vec4(1.0);
 if(uColorOp.x>1.5){
  vec4 c=operation(uColorOp.x,argument(uColorArgs0.x,current,texel,temp),argument(uColorArgs0.y,current,texel,temp),argument(uColorArgs0.z,current,texel,temp),current,texel);
  if(uAlphaOp.x>1.5)c.a=operation(uAlphaOp.x,argument(uAlphaArgs0.x,current,texel,temp),argument(uAlphaArgs0.y,current,texel,temp),argument(uAlphaArgs0.z,current,texel,temp),current,texel).a;
  else c.a=current.a;
  if(uColorArgs0.w>4.5)temp=clamp(c,0.0,1.0);else current=clamp(c,0.0,1.0);
  texel=uColorOp.w>0.5?texture2D(uSampler1,vTex1.xy/vTex1.z):vec4(1.0);
  if(uColorOp.y>1.5){
   c=operation(uColorOp.y,argument(uColorArgs1.x,current,texel,temp),argument(uColorArgs1.y,current,texel,temp),argument(uColorArgs1.z,current,texel,temp),current,texel);
   if(uAlphaOp.y>1.5)c.a=operation(uAlphaOp.y,argument(uAlphaArgs1.x,current,texel,temp),argument(uAlphaArgs1.y,current,texel,temp),argument(uAlphaArgs1.z,current,texel,temp),current,texel).a;
   else c.a=current.a;
   if(uColorArgs1.w>4.5)temp=clamp(c,0.0,1.0);else current=clamp(c,0.0,1.0);
  }
 }
 if(uFragment.z>0.5)current.rgb=clamp(current.rgb+vSpec.rgb,0.0,1.0);
 if(uFragment.x>0.5&&!compare(current.a,uFragment.y,uFragment.x))discard;
 if(uFog.w>0.5){
  float f=uFog.w<1.5?exp(-uFog.z*vFog):uFog.w<2.5?exp(-pow(uFog.z*vFog,2.0)):(uFog.y-vFog)/(uFog.y-uFog.x);
  current.rgb=mix(uFogColor.rgb,current.rgb,clamp(f,0.0,1.0));
 }
 gl_FragColor=current;
}
