/* Programmable D3DM fixed-function emulation. Vertex work stays on the GPU. */
#include "gpu_api.h"
#include "shaders.inc"
enum {UW,UV,UP,UT0,UT1,UVP,UFLAGS,URENDER,UTINFO,UTSIZES,UMD,UMA,UMS,ULIGHTING,ULFLAGS,UAMBIENT,ULPOS,ULD,ULA,ULS,ULATT,UFACTOR,UCOP,UAOP,UCA0,UCA1,UAA0,UAA1,UFRAG,UFOG,UFOGC,US0,US1,UNIFORMS};
static const char *names[UNIFORMS]={"uWorld","uView","uProjection","uTex0","uTex1","uViewport","uVertexFlags","uRender","uTextureInfo","uTexSizes","uMaterialD","uMaterialA","uMaterialS","uLighting","uLightFlags","uAmbient","uLightPos[0]","uLightD[0]","uLightA[0]","uLightS[0]","uLightAtt[0]","uFactor","uColorOp","uAlphaOp","uColorArgs0","uColorArgs1","uAlphaArgs0","uAlphaArgs1","uFragment","uFog","uFogColor","uSampler0","uSampler1"};
static float number(INT n){return p2d_word_float(p2d_i32_float_bits(n));}
static float value(DWORD n,D3DMFORMAT f){return p2d_word_float(f==D3DMFMT_D3DMVALUE_FIXED?p2d_fixed_float_bits((INT)n):n);}
static void v4(P2DGles *g,UINT loc,float a,float b,float c,float d){g->gl.Uniform4f(g->uniform[loc],a,b,c,d);}
static void color(P2DGles *g,UINT loc,DWORD c){v4(g,loc,p2d_word_float(p2d_u8_float_bits((c>>16)&255)),p2d_word_float(p2d_u8_float_bits((c>>8)&255)),p2d_word_float(p2d_u8_float_bits(c&255)),p2d_word_float(p2d_u8_float_bits(c>>24)));}
static void values(P2DGles *g,UINT loc,const void *data,D3DMFORMAT format,UINT count){
 float words[16];const DWORD *p=data;for(UINT i=0;i<count;i++)words[i]=value(p[i],format);
 if(count==16)g->gl.UniformMatrix4fv(g->uniform[loc],1,0,words);else g->gl.Uniform4fv(g->uniform[loc],1,words);
}
static UINT shader(P2DGles *g,UINT type,const char *source){
 UINT obj=g->gl.CreateShader(type);if(!obj)return 0;g->gl.ShaderSource(obj,1,&source,0);g->gl.CompileShader(obj);
 INT ok=0;g->gl.GetShaderiv(obj,0x8b81,&ok);if(!ok){g->gl.GetShaderInfoLog(obj,sizeof(g->error),0,g->error);g->gl.DeleteShader(obj);return 0;}return obj;
}
HRESULT p2d_pipeline_init(P2DGles *g){
 UINT vs=shader(g,0x8b31,p2d_vertex),fs=shader(g,0x8b30,p2d_fragment);
 if(!vs||!fs){if(vs)g->gl.DeleteShader(vs);if(fs)g->gl.DeleteShader(fs);return D3DMERR_NOTAVAILABLE;}
 g->program=g->gl.CreateProgram();g->gl.AttachShader(g->program,vs);g->gl.AttachShader(g->program,fs);
 const char *attrs[]={"aPosition","aNormal","aDiffuse","aSpecular","aTex0","aTex1"};
 for(UINT i=0;i<6;i++)g->gl.BindAttribLocation(g->program,i,attrs[i]);
 g->gl.LinkProgram(g->program);g->gl.DeleteShader(vs);g->gl.DeleteShader(fs);
 INT ok=0;g->gl.GetProgramiv(g->program,0x8b82,&ok);if(!ok){g->gl.GetProgramInfoLog(g->program,sizeof(g->error),0,g->error);return D3DMERR_NOTAVAILABLE;}
 for(UINT i=0;i<UNIFORMS;i++)g->uniform[i]=g->gl.GetUniformLocation(g->program,names[i]);
 g->gl.GenFramebuffers(1,&g->framebuffer);g->gl.BindFramebuffer(0x8d40,g->framebuffer);
 g->gl.GenRenderbuffers(1,&g->colorbuffer);g->gl.BindRenderbuffer(0x8d41,g->colorbuffer);
 g->gl.RenderbufferStorage(0x8d41,0x8d62,g->width,g->height);
 g->gl.FramebufferRenderbuffer(0x8d40,0x8ce0,0x8d41,g->colorbuffer);
 g->gl.GenRenderbuffers(1,&g->depthbuffer);g->gl.BindRenderbuffer(0x8d41,g->depthbuffer);
 g->gl.RenderbufferStorage(0x8d41,0x81a5,g->width,g->height);
 g->gl.FramebufferRenderbuffer(0x8d40,0x8d00,0x8d41,g->depthbuffer);
 if(g->gl.CheckFramebufferStatus(0x8d40)!=0x8cd5||g->gl.GetError())return D3DMERR_NOTAVAILABLE;
 return S_OK;
}
void p2d_pipeline_discard(P2DGles *g){
 if(g->program)g->gl.DeleteProgram(g->program);
 if(g->blit_program)g->gl.DeleteProgram(g->blit_program);
 if(g->blit_buffer)g->gl.DeleteBuffers(1,&g->blit_buffer);
 if(g->blit_texture)g->gl.DeleteTextures(1,&g->blit_texture);
 if(g->framebuffer)g->gl.DeleteFramebuffers(1,&g->framebuffer);
 if(g->colorbuffer)g->gl.DeleteRenderbuffers(1,&g->colorbuffer);
 if(g->depthbuffer)g->gl.DeleteRenderbuffers(1,&g->depthbuffer);
 if(g->index_scratch)g->gl.DeleteBuffers(1,&g->index_scratch);
}
HRESULT p2d_gles_upload_buffer(P2DGles *g,P2DResource *r,UINT target){
 if(r->locked)return D3DMERR_INVALIDCALL;
 if(r->gl_name&&!r->gpu_dirty)return S_OK;
 if(r->desc.size>0x3fff00u)return D3DMERR_NOTAVAILABLE;
 UINT name=0;g->gl.GenBuffers(1,&name);if(!name)return E_OUTOFMEMORY;
 g->gl.BindBuffer(target,name);g->gl.BufferData(target,r->desc.size,r->data,0x88e4);
 UINT e=g->gl.GetError();if(e){g->gl.DeleteBuffers(1,&name);return e==0x505?E_OUTOFMEMORY:D3DMERR_DRIVERINTERNALERROR;}
 if(r->gl_name)g->gl.DeleteBuffers(1,&r->gl_name);r->gl_name=name;r->gpu_dirty=0;return S_OK;
}
static void toggle(P2DGles *g,UINT capability,BOOL on){if(on)g->gl.Enable(capability);else g->gl.Disable(capability);}
static UINT texsize(DWORD fvf,UINT index){if(index>=((fvf>>8)&7))return 0;UINT n=(fvf>>(16+index*2))&3;return n==3?1:n==1?3:2;}
static void uniforms(P2DGles *g,P2DDevice *d,DWORD fvf){
 P2DState *s=&d->state;DWORD *r=s->render;
 for(UINT i=0;i<5;i++)values(g,UW+i,&s->matrix[i],s->matrix_format[i],16);
 D3DMVIEWPORT *vp=&s->viewport;
 v4(g,UVP,number(vp->X),number(vp->Y),number(vp->Width),number(vp->Height));
 v4(g,UFLAGS,number((fvf&7)>=2),number(!!(fvf&0x38)),number(!!(fvf&0x40)),number(!!(fvf&0x80)));
 v4(g,URENDER,number(!!r[37]),number(!!r[42]),number(!!r[14]),number(!!r[31]));
 v4(g,UTINFO,number(s->texture[0][7]),number(s->texture[0][17]),number(s->texture[1][7]),number(s->texture[1][17]));
 v4(g,UTSIZES,number(texsize(fvf,0)),number(texsize(fvf,1)),0,0);
 values(g,UMD,&s->material.Diffuse,s->material_format,4);
 values(g,UMA,&s->material.Ambient,s->material_format,4);
 values(g,UMS,&s->material.Specular,s->material_format,4);
 v4(g,ULIGHTING,number(!!r[41]),s->material.Power,number(r[40]?r[43]:0),number(r[40]?r[45]:0));
 color(g,UAMBIENT,r[38]);
 float pos[32],dif[32],amb[32],spec[32],att[32];UINT count=0;
 for(P2DLight *l=s->lights;l&&count<8;l=l->next)if(l->enabled){
  D3DMLIGHT *a=&l->value;UINT j=count*4;const DWORD *p=(void*)(a->Type==1?&a->Position:&a->Direction);
  for(UINT k=0;k<3;k++)pos[j+k]=value(p[k],l->format);pos[j+3]=number(a->Type==1);
  const DWORD *cd=(void*)&a->Diffuse,*ca=(void*)&a->Ambient,*cs=(void*)&a->Specular;
  for(UINT k=0;k<4;k++){dif[j+k]=value(cd[k],l->format);amb[j+k]=value(ca[k],l->format);spec[j+k]=value(cs[k],l->format);}
  att[j]=a->Attenuation0;att[j+1]=a->Attenuation1;att[j+2]=a->Attenuation2;att[j+3]=a->Range;count++;
 }
 if(count){g->gl.Uniform4fv(g->uniform[ULPOS],count,pos);g->gl.Uniform4fv(g->uniform[ULD],count,dif);g->gl.Uniform4fv(g->uniform[ULA],count,amb);g->gl.Uniform4fv(g->uniform[ULS],count,spec);g->gl.Uniform4fv(g->uniform[ULATT],count,att);}
 v4(g,ULFLAGS,number(r[40]?r[44]:0),number(count),0,0);
 color(g,UFACTOR,r[30]);
 v4(g,UCOP,number(s->texture[0][1]),number(s->texture[1][1]),number(d->textures[0]!=0),number(d->textures[1]!=0));
 v4(g,UAOP,number(s->texture[0][4]),number(s->texture[1][4]),0,0);
 for(UINT i=0;i<2;i++){
  DWORD *t=s->texture[i];
  v4(g,UCA0+i,number(t[19]),number(t[2]),number(t[3]),number(t[21]));
  v4(g,UAA0+i,number(t[20]),number(t[5]),number(t[6]),0);
 }
 v4(g,UFRAG,r[3]?number(r[10]):0,p2d_word_float(p2d_u8_float_bits(r[9])),number(!!r[14]),0);
 UINT fog=r[13]?(r[16]?r[16]:r[39]):0;
 v4(g,UFOG,p2d_word_float(r[17]),p2d_word_float(r[18]),p2d_word_float(r[19]),number(fog));color(g,UFOGC,r[15]);
 g->gl.Uniform1i(g->uniform[US0],0);g->gl.Uniform1i(g->uniform[US1],1);
}
static HRESULT textures(P2DGles *g,P2DDevice *d){
 if(d->textures[0]&&d->textures[0]==d->textures[1]){
  for(UINT i=8;i<=18;i++)if(d->state.texture[0][i]!=d->state.texture[1][i])return D3DMERR_NOTAVAILABLE;
 }
 for(UINT i=0;i<2;i++){
  P2DResource *r=d->textures[i];g->gl.ActiveTexture(0x84c0+i);
  if(!r){g->gl.BindTexture(0xde1,0);continue;}
  HRESULT h=p2d_resource_upload(r);if(FAILED(h))return h;
  g->gl.BindTexture(0xde1,r->gl_name);DWORD *t=d->state.texture[i];
  UINT min=t[12]==2?0x2601:0x2600;
  if(t[13]){UINT w=r->mips[r->desc.levels-1].width,h=r->mips[r->desc.levels-1].height;if(w!=1||h!=1)return D3DMERR_NOTAVAILABLE;min=t[13]==1?(t[12]==2?0x2701:0x2700):(t[12]==2?0x2703:0x2702);}
  g->gl.TexParameteri(0xde1,0x2801,min);g->gl.TexParameteri(0xde1,0x2800,t[11]==2?0x2601:0x2600);
  const UINT address[]={0,0x2901,0x8370,0x812f};
  g->gl.TexParameteri(0xde1,0x2802,address[t[8]]);g->gl.TexParameteri(0xde1,0x2803,address[t[9]]);
 }
 return S_OK;
}
HRESULT p2d_gles_draw(P2DGles *g,P2DDevice *d,const P2DDraw *call){
 DWORD *r=d->state.render;DWORD fvf=call->vertex_buffer->desc.fvf;
 if(r[22]||!r[36]||r[21])return D3DMERR_NOTAVAILABLE;
 HRESULT result=textures(g,d);if(FAILED(result))return result;
 result=p2d_gles_upload_buffer(g,call->vertex_buffer,0x8892);if(FAILED(result))return result;
 g->gl.BindFramebuffer(0x8d40,g->framebuffer);g->gl.UseProgram(g->program);
 if(g->applied_generation!=d->state.generation||g->applied_fvf!=fvf||g->applied_blit){
  P2DGLState *s=&d->gl_state;g->gl.Viewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
  toggle(g,0xb71,!!r[48]);g->gl.DepthFunc(s->depth_func);g->gl.DepthMask(!!r[2]);
  g->gl.DepthRangef((fvf&7)>=2?0:d->state.viewport.MinZ,(fvf&7)>=2?1:d->state.viewport.MaxZ);
  toggle(g,0xbe2,!!r[12]);g->gl.BlendFunc(s->src_blend,s->dest_blend);g->gl.BlendEquation(r[47]==1?0x8006:r[47]==2?0x800a:0x800b);
  toggle(g,0xb44,r[7]!=1);g->gl.FrontFace(0x901);g->gl.CullFace(r[7]==2?0x405:0x404);
  toggle(g,0xbd0,!!r[11]);g->gl.ColorMask(!!(r[46]&1),!!(r[46]&2),!!(r[46]&4),!!(r[46]&8));
  g->gl.Disable(0xc11);g->gl.Disable(0xb90);uniforms(g,d,fvf);
  g->applied_generation=d->state.generation;g->applied_fvf=fvf;g->applied_blit=0;
 }
 g->gl.BindBuffer(0x8892,call->vertex_buffer->gl_name);
 for(UINT i=0;i<6;i++){
  const P2DAttribute *a=&call->attribute[i];
  if(a->size){g->gl.EnableVertexAttribArray(i);g->gl.VertexAttribPointer(i,a->size,a->type,a->normalized,d->stride,(void*)(uintptr_t)(call->base_offset+a->offset));}
  else g->gl.DisableVertexAttribArray(i);
 }
 if(call->indexed){
  if(call->temporary_indices){
   if(!g->index_scratch)g->gl.GenBuffers(1,&g->index_scratch);g->gl.BindBuffer(0x8893,g->index_scratch);
   g->gl.BufferData(0x8893,call->count*2,call->indices,0x88e0);
  }else{
   result=p2d_gles_upload_buffer(g,call->index_buffer,0x8893);if(FAILED(result))return result;
   g->gl.BindBuffer(0x8893,call->index_buffer->gl_name);
  }
  g->gl.DrawElements(call->mode,call->count,0x1403,(void*)(uintptr_t)(call->temporary_indices?0:call->index_offset));
 }else g->gl.DrawArrays(call->mode,0,call->count);
 return S_OK;
}

HRESULT p2d_gles_read_backbuffer(P2DGles *g,BYTE *data,UINT pitch,UINT w,UINT h){
 if(!data||w!=g->width||h!=g->height||pitch<w*2)return D3DMERR_INVALIDCALL;
 g->gl.BindFramebuffer(0x8d40,g->framebuffer);
 return g->read565(data,pitch,w,h)?S_OK:D3DMERR_DRIVERINTERNALERROR;
}
static HRESULT blit_init(P2DGles *g){
 if(g->blit_program)return S_OK;
 const char *v="attribute vec2 p;varying vec2 t;void main(){gl_Position=vec4(p,0.,1.);t=vec2((p.x+1.)*.5,(1.-p.y)*.5);}";
 const char *f="precision mediump float;uniform sampler2D s;varying vec2 t;void main(){gl_FragColor=texture2D(s,t);}";
 UINT vs=shader(g,0x8b31,v),fs=shader(g,0x8b30,f);
 if(!vs||!fs){if(vs)g->gl.DeleteShader(vs);if(fs)g->gl.DeleteShader(fs);return D3DMERR_NOTAVAILABLE;}
 UINT program=g->gl.CreateProgram();g->gl.AttachShader(program,vs);g->gl.AttachShader(program,fs);g->gl.BindAttribLocation(program,0,"p");g->gl.LinkProgram(program);
 g->gl.DeleteShader(vs);g->gl.DeleteShader(fs);INT ok=0;g->gl.GetProgramiv(program,0x8b82,&ok);
 if(!ok){g->gl.DeleteProgram(program);return D3DMERR_NOTAVAILABLE;}
 g->blit_program=program;g->gl.GenBuffers(1,&g->blit_buffer);g->gl.GenTextures(1,&g->blit_texture);
 return g->blit_buffer&&g->blit_texture?S_OK:E_OUTOFMEMORY;
}
HRESULT p2d_gles_write_backbuffer(P2DGles *g,const BYTE *data,UINT pitch,UINT w,UINT h){
 if(!data||w!=g->width||h!=g->height||pitch<w*2||w*h>0x3fff00u/4)return D3DMERR_INVALIDCALL;
 HRESULT result=blit_init(g);if(FAILED(result))return result;
 BYTE *rgba=p2d_alloc(w*h*4);if(!rgba)return E_OUTOFMEMORY;
 for(UINT y=0;y<h;y++)for(UINT x=0;x<w;x++){
  UINT v=data[y*pitch+x*2]|((UINT)data[y*pitch+x*2+1]<<8);BYTE *p=rgba+(y*w+x)*4;
  UINT r=(v>>11)&31,b=v&31,c=(v>>5)&63;p[0]=(r<<3)|(r>>2);p[1]=(c<<2)|(c>>4);p[2]=(b<<3)|(b>>2);p[3]=255;
 }
 g->gl.BindFramebuffer(0x8d40,g->framebuffer);g->gl.UseProgram(g->blit_program);
 g->gl.ActiveTexture(0x84c0);g->gl.BindTexture(0xde1,g->blit_texture);g->gl.PixelStorei(0xcf5,4);
 g->gl.TexImage2D(0xde1,0,0x1908,w,h,0,0x1908,0x1401,rgba);p2d_free(rgba);
 g->gl.TexParameteri(0xde1,0x2801,0x2600);g->gl.TexParameteri(0xde1,0x2800,0x2600);
 g->gl.TexParameteri(0xde1,0x2802,0x812f);g->gl.TexParameteri(0xde1,0x2803,0x812f);
 g->gl.Uniform1i(g->gl.GetUniformLocation(g->blit_program,"s"),0);
 static const float quad[]={-1,-1,1,-1,-1,1,1,1};
 g->gl.BindBuffer(0x8892,g->blit_buffer);g->gl.BufferData(0x8892,sizeof(quad),quad,0x88e4);
 g->gl.EnableVertexAttribArray(0);g->gl.VertexAttribPointer(0,2,0x1406,0,0,0);
 for(UINT i=1;i<6;i++)g->gl.DisableVertexAttribArray(i);
 g->gl.Disable(0xb71);g->gl.Disable(0xbe2);g->gl.Disable(0xb44);g->gl.Disable(0xc11);g->gl.Disable(0xb90);
 g->gl.ColorMask(1,1,1,1);g->gl.Viewport(0,0,w,h);g->gl.DrawArrays(5,0,4);
 g->applied_blit=1;g->applied_generation=0;
 return g->gl.GetError()?D3DMERR_DRIVERINTERNALERROR:S_OK;
}
