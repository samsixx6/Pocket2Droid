#include "state.h"
Buffer*p2d2_buffer(Native*g,uint32_t name,int create){
 if(!name)return 0;Buffer*empty=0;for(unsigned i=0;i<1024;i++){Buffer*b=g->buffers+i;if(b->name==name)return b;if(!b->name&&!empty)empty=b;}if(create&&empty){empty->name=name;return empty;}return 0;
}
static void scalar(Native*g,uint32_t op,uint32_t v){p2d2_result(g,op,&v,4);}
static unsigned component(unsigned type){return type==0x1400||type==0x1401?1:type==0x1402||type==0x1403?2:type==0x1406||type==0x140c?4:0;}
static int arrays(Native*g,uint32_t count){
 if(!count)return 1;
 for(unsigned i=0;i<16;i++){
  Attribute*a=g->attr+i;if(!a->enabled)continue;unsigned element=a->size*component(a->type),stride=a->stride?a->stride:element;
  uint64_t need=(uint64_t)(count-1)*stride+element;if(!element||need>P2D2_MAX_BUFFER){p2d2_error(g,0x501);return 0;}
  const void*pointer;
  if(a->buffer){Buffer*b=p2d2_buffer(g,a->buffer,0);if(!b||a->pointer>b->size||need>b->size-a->pointer){p2d2_error(g,0x502);return 0;}g2BindBuffer(0x8892,a->buffer);pointer=(void*)(uintptr_t)a->pointer;}
  else{if(need>a->snapshot_size){p2d2_error(g,0x502);return 0;}g2BindBuffer(0x8892,0);pointer=a->snapshot;}
  g2VertexAttribPointer(i,a->size,a->type,a->normalized,a->stride,pointer);
 }
 g2BindBuffer(0x8892,g->array_buffer);return 1;
}
static int names(const P2D2Record*r){return (int32_t)r->args[0]>=0&&r->args[0]<=P2D2_MAX_PACKET/4&&r->length==r->args[0]*4;}
static unsigned query_count(uint32_t p){return p==0xba2||p==0xc22||p==0xc10||p==0xc23||p==0x8005?4:p==0xb70||p==0x846d||p==0x846e||p==0xd3a?2:1;}
void p2d2_dispatch(Native*g,const P2D2Record*r){
 const uint32_t*a=r->args;const uint8_t*d=r->payload;uint32_t n=r->length;float f[4];for(unsigned i=0;i<4;i++)f[i]=p2d2_float(a[i]);
 if(r->op!=P2D2_APP_PROFILE&&r->op>=0x20000u&&r->op<0x20100u)g->compat_locked=1;
 switch(r->op){
 case P2D2_APP_PROFILE:{unsigned invalid=n||(a[0]!=0&&a[0]!=1&&a[0]!=3);for(unsigned i=1;i<16;i++)invalid|=a[i];
  if(invalid){p2d2_error(g,0x501);break;}
  if(g->compat_locked&&a[0]!=g->compat_profile){p2d2_error(g,0x502);break;}
  g->compat_profile=a[0];g->compat_locked=1;g->ferrari_legacy_state=!!(a[0]&P2D2_PROFILE_LEGACY_STATE);break;}

 case P2D2_glShaderBinary:p2d2_error(g,(int32_t)a[0]<0||(int32_t)a[4]<0?0x501:0x500);break;
 case P2D2_glReleaseShaderCompiler:break;
 case P2D2_glIsShader:scalar(g,r->op,g2IsShader(a[0]));break;
 case P2D2_glIsProgram:scalar(g,r->op,g2IsProgram(a[0]));break;
 case P2D2_INDEX_MAX:{uint32_t values[2]={0,0};unsigned c=a[3]==0x1401?1:a[3]==0x1403?2:0;Buffer*b=p2d2_buffer(g,a[0],0);if(n||!c||a[2]>1048576||!a[0]||a[0]!=g->element_buffer||!b||a[1]>b->size||(size_t)a[2]*c>b->size-a[1])p2d2_error(g,0x502);else{const uint8_t*p=b->data+a[1];values[0]=1;for(unsigned i=0;i<a[2];i++){unsigned v=c==1?p[i]:(unsigned)p[i*2]|(unsigned)p[i*2+1]<<8;if(v>values[1])values[1]=v;}}p2d2_result(g,r->op,values,8);break;}
 case P2D2_QUERY_CHUNK:if(a[0]>g->result_size){p2d2_error(g,0x501);break;}g->has_reply=1;g->reply_tag=r->op;g->reply_offset=a[0];break;
 case P2D2_SNAPSHOT:{if(a[0]>=16||n>P2D2_MAX_BUFFER){p2d2_error(g,0x501);break;}Attribute*t=g->attr+a[0];uint8_t*p=realloc(t->snapshot,n?n:1);if(!p){p2d2_error(g,0x505);break;}t->snapshot=p;t->snapshot_size=n;if(n)memcpy(p,d,n);break;}
 case P2D2_glCreateShader:scalar(g,r->op,g2CreateShader(a[0]));break;
 case P2D2_glCreateProgram:scalar(g,r->op,g2CreateProgram());break;
 case P2D2_glShaderSource:{if(n>P2D2_MAX_RESULT){p2d2_error(g,0x505);break;}const char*s=n?(const char*)d:"";int len=n;g2ShaderSource(a[0],1,&s,&len);break;}
 case P2D2_glCompileShader:g2CompileShader(a[0]);break;
 case P2D2_glAttachShader:g2AttachShader(a[0],a[1]);break;
 case P2D2_glDeleteShader:g2DeleteShader(a[0]);break;
 case P2D2_glDeleteProgram:g2DeleteProgram(a[0]);break;
 case P2D2_glLinkProgram:g2LinkProgram(a[0]);break;
 case P2D2_glUseProgram:g2UseProgram(a[0]);break;
 case P2D2_glBindAttribLocation:case P2D2_glGetAttribLocation:case P2D2_glGetUniformLocation:{if(!n||d[n-1]){p2d2_error(g,0x501);break;}if(r->op==P2D2_glBindAttribLocation)g2BindAttribLocation(a[0],a[1],(const char*)d);else scalar(g,r->op,(uint32_t)(r->op==P2D2_glGetAttribLocation?g2GetAttribLocation(a[0],(const char*)d):g2GetUniformLocation(a[0],(const char*)d)));break;}
 case P2D2_glGetShaderiv:case P2D2_glGetProgramiv:{int value=0;if(r->op==P2D2_glGetShaderiv)g2GetShaderiv(a[0],a[1],&value);else g2GetProgramiv(a[0],a[1],&value);scalar(g,r->op,(uint32_t)value);break;}
 case P2D2_glGetShaderInfoLog:case P2D2_glGetProgramInfoLog:case P2D2_glGetShaderSource:{
  if(a[1]>P2D2_MAX_RESULT-4){p2d2_error(g,0x505);break;}size_t size=(size_t)a[1]+4;uint8_t*p=calloc(1,size?size:4);if(!p){p2d2_error(g,0x505);break;}int written=0;
  if(r->op==P2D2_glGetShaderInfoLog)g2GetShaderInfoLog(a[0],a[1],&written,(char*)p+4);else if(r->op==P2D2_glGetShaderSource)g2GetShaderSource(a[0],a[1],&written,(char*)p+4);else g2GetProgramInfoLog(a[0],a[1],&written,(char*)p+4);
  memcpy(p,&written,4);p2d2_result(g,r->op,p,size);free(p);break;}
 case P2D2_glUniform1i:g2Uniform1i((int)a[0],a[1]);break;
 case P2D2_glUniform1f:g2Uniform1f((int)a[0],p2d2_float(a[1]));break;
 case P2D2_glUniform2f:g2Uniform2f((int)a[0],p2d2_float(a[1]),p2d2_float(a[2]));break;
 case P2D2_glUniform3f:g2Uniform3f((int)a[0],p2d2_float(a[1]),p2d2_float(a[2]),p2d2_float(a[3]));break;
 case P2D2_glUniform4f:g2Uniform4f((int)a[0],p2d2_float(a[1]),p2d2_float(a[2]),p2d2_float(a[3]),p2d2_float(a[4]));break;
 case P2D2_glUniformMatrix3fv:case P2D2_glUniformMatrix4fv:{unsigned c=r->op==P2D2_glUniformMatrix3fv?9:16;if((int32_t)a[1]<0||a[1]>P2D2_MAX_PACKET/(c*4)||n!=a[1]*c*4){p2d2_error(g,0x501);break;}if(c==9)g2UniformMatrix3fv((int)a[0],a[1],a[2],(const float*)d);else g2UniformMatrix4fv((int)a[0],a[1],a[2],(const float*)d);break;}
 case P2D2_glUniform1fv:case P2D2_glUniform2fv:case P2D2_glUniform3fv:case P2D2_glUniform4fv:{unsigned c=r->op==P2D2_glUniform1fv?1:r->op==P2D2_glUniform2fv?2:r->op==P2D2_glUniform3fv?3:4;if((int32_t)a[1]<0||a[1]>P2D2_MAX_PACKET/(c*4)||n!=a[1]*c*4){p2d2_error(g,0x501);break;}if(c==1)g2Uniform1fv((int)a[0],a[1],(const float*)d);else if(c==2)g2Uniform2fv((int)a[0],a[1],(const float*)d);else if(c==3)g2Uniform3fv((int)a[0],a[1],(const float*)d);else g2Uniform4fv((int)a[0],a[1],(const float*)d);break;}
 case P2D2_glGetIntegerv:{if(a[0]==0x8df9){scalar(g,r->op,0);break;}if(a[0]==0x8df8){p2d2_result(g,r->op,0,0);break;}int value[256]={0};unsigned c=query_count(a[0]);if(a[0]==0x86a3){int count=0;g2GetIntegerv(0x86a2,&count);if(count<0||count>256){p2d2_error(g,0x505);break;}c=(unsigned)count;}if(a[0]==0x87ff){p2d2_error(g,0x500);break;}g2GetIntegerv(a[0],value);if(a[0]==0x8869&&value[0]>16)value[0]=16;if((a[0]==0xd33||a[0]==0x851c||a[0]==0x84e8)&&value[0]>2048)value[0]=2048;if(a[0]==0xd3a)for(unsigned i=0;i<2;i++)if(value[i]>2048)value[i]=2048;p2d2_result(g,r->op,value,c*4);break;}
 case P2D2_glGetError:{unsigned value=g->glerror;g->glerror=0;scalar(g,r->op,value);break;}
 case P2D2_glGenBuffers:case P2D2_glGenFramebuffers:case P2D2_glGenRenderbuffers:case P2D2_glGenTextures:{if(a[0]>16){p2d2_error(g,0x501);break;}unsigned values[16]={0};if(r->op==P2D2_glGenBuffers)g2GenBuffers(a[0],values);else if(r->op==P2D2_glGenFramebuffers)g2GenFramebuffers(a[0],values);else if(r->op==P2D2_glGenRenderbuffers)g2GenRenderbuffers(a[0],values);else g2GenTextures(a[0],values);p2d2_result(g,r->op,values,a[0]*4);break;}
 case P2D2_glDeleteBuffers:case P2D2_glDeleteFramebuffers:case P2D2_glDeleteRenderbuffers:case P2D2_glDeleteTextures:{if(!names(r)){p2d2_error(g,0x501);break;}if(r->op==P2D2_glDeleteBuffers){for(unsigned i=0;i<a[0];i++){unsigned name=p2d2_u32(d+i*4);Buffer*b=p2d2_buffer(g,name,0);if(b){g->buffer_bytes-=b->size;free(b->data);memset(b,0,sizeof(*b));}if(g->array_buffer==name)g->array_buffer=0;if(g->element_buffer==name)g->element_buffer=0;for(unsigned j=0;j<16;j++)if(g->attr[j].buffer==name)g->attr[j].buffer=0;}g2DeleteBuffers(a[0],(const unsigned*)d);}else if(r->op==P2D2_glDeleteFramebuffers)g2DeleteFramebuffers(a[0],(const unsigned*)d);else if(r->op==P2D2_glDeleteRenderbuffers)g2DeleteRenderbuffers(a[0],(const unsigned*)d);else{for(unsigned i=0;i<a[0];i++)p2d2_legacy_forget(g,p2d2_u32(d+i*4));g2DeleteTextures(a[0],(const unsigned*)d);}break;}
 case P2D2_glBindBuffer:if(a[0]!=0x8892&&a[0]!=0x8893){p2d2_error(g,0x500);break;}if(a[1]&&!p2d2_buffer(g,a[1],1)){p2d2_error(g,0x505);break;}g2BindBuffer(a[0],a[1]);if(a[0]==0x8892)g->array_buffer=a[1];else g->element_buffer=a[1];break;
 case P2D2_glBufferData:case P2D2_glBufferSubData:{
  scalar(g,r->op,0);
  if(a[0]!=0x8892&&a[0]!=0x8893){p2d2_error(g,0x500);break;}Buffer*b=p2d2_buffer(g,a[0]==0x8892?g->array_buffer:g->element_buffer,0);if(!b){p2d2_error(g,0x502);break;}
  if(r->op==P2D2_glBufferData){if(a[3]!=0x88e0&&a[3]!=0x88e4&&a[3]!=0x88e8){p2d2_error(g,0x500);break;}if(a[1]>P2D2_MAX_BUFFER||g->buffer_bytes-b->size+a[1]>128u*1024*1024||((a[2]&&n!=a[1])||(!a[2]&&n))){p2d2_error(g,0x501);break;}uint8_t*p=calloc(1,a[1]?a[1]:1);if(!p){p2d2_error(g,0x505);break;}if(n)memcpy(p,d,n);g2BufferData(a[0],a[1],p,a[3]);unsigned e=g2GetError();if(e){free(p);p2d2_error(g,e);break;}g->buffer_bytes=g->buffer_bytes-b->size+a[1];free(b->data);b->data=p;b->size=a[1];}
  else{if(a[1]>b->size||a[2]>b->size-a[1]||n!=a[2]){p2d2_error(g,0x501);break;}g2BufferSubData(a[0],a[1],a[2],d);unsigned e=g2GetError();if(e){p2d2_error(g,e);break;}if(n)memcpy(b->data+a[1],d,n);}scalar(g,r->op,1);break;}
 case P2D2_glVertexAttribPointer:{if(a[0]>=16||a[1]<1||a[1]>4||!component(a[2])||(int32_t)a[4]<0){p2d2_error(g,0x501);break;}Attribute*t=g->attr+a[0];t->size=a[1];t->type=a[2];t->normalized=a[3]!=0;t->stride=a[4];t->pointer=a[5];t->buffer=a[6];break;}
 case P2D2_glEnableVertexAttribArray:case P2D2_glDisableVertexAttribArray:if(a[0]>=16){p2d2_error(g,0x501);break;}g->attr[a[0]].enabled=r->op==P2D2_glEnableVertexAttribArray;if(g->attr[a[0]].enabled)g2EnableVertexAttribArray(a[0]);else g2DisableVertexAttribArray(a[0]);break;
 case P2D2_glDrawArrays:if((int32_t)a[1]<0||(int32_t)a[2]<0||a[1]>1048576||a[2]>1048576-a[1]){p2d2_error(g,0x501);break;}if(arrays(g,a[1]+a[2]))g2DrawArrays(a[0],a[1],a[2]);break;
 case P2D2_glDrawElements:{unsigned c=a[2]==0x1401?1:a[2]==0x1403?2:0;if(!c||(int32_t)a[1]<0||a[1]>1048576){p2d2_error(g,0x501);break;}const uint8_t*indices=d;const void*pointer=d;size_t size=(size_t)c*a[1];if(g->element_buffer){Buffer*b=p2d2_buffer(g,g->element_buffer,0);if(!b||a[3]>b->size||size>b->size-a[3]){p2d2_error(g,0x502);break;}indices=b->data+a[3];pointer=(void*)(uintptr_t)a[3];}else if(n!=size){p2d2_error(g,0x501);break;}unsigned max=0;for(unsigned i=0;i<a[1];i++){unsigned v=c==1?indices[i]:(unsigned)indices[i*2]|(unsigned)indices[i*2+1]<<8;if(v>max)max=v;}if(arrays(g,a[1]?max+1:0))g2DrawElements(a[0],a[1],a[2],pointer);break;}
 case P2D2_glTexImage2D:case P2D2_glTexSubImage2D:case P2D2_glCompressedTexImage2D:p2d2_texture(g,r);break;
 case P2D2_glBindFramebuffer:g2BindFramebuffer(a[0],a[1]);break;
 case P2D2_glBindRenderbuffer:g2BindRenderbuffer(a[0],a[1]);break;
 case P2D2_glFramebufferRenderbuffer:g2FramebufferRenderbuffer(a[0],a[1],a[2],a[3]);break;
 case P2D2_glFramebufferTexture2D:g2FramebufferTexture2D(a[0],a[1],a[2],a[3],a[4]);break;
 case P2D2_glRenderbufferStorage:if(a[2]>2048||a[3]>2048){p2d2_error(g,0x501);break;}g2RenderbufferStorage(a[0],a[1],a[2],a[3]);break;
 case P2D2_glCheckFramebufferStatus:scalar(g,r->op,g2CheckFramebufferStatus(a[0]));break;
 case P2D2_glBindTexture:g2BindTexture(a[0],a[1]);break;
 case P2D2_glActiveTexture:g2ActiveTexture(a[0]);break;
 case P2D2_glTexParameteri:if(!p2d2_legacy_parameter(g,a[0],a[1],(float)(int32_t)a[2]))g2TexParameteri(a[0],a[1],a[2]);break;
 case P2D2_glTexParameterf:if(!p2d2_legacy_parameter(g,a[0],a[1],f[2]))g2TexParameterf(a[0],a[1],f[2]);break;
 case P2D2_glHint:if(g->ferrari_legacy_state&&a[0]==0xc50&&(a[1]==0x1100||a[1]==0x1101||a[1]==0x1102))break;g2Hint(a[0],a[1]);break;
 case P2D2_glGenerateMipmap:g2GenerateMipmap(a[0]);break;
 case P2D2_glPixelStorei:if(a[0]==0xcf5){if(a[1]!=1&&a[1]!=2&&a[1]!=4&&a[1]!=8){p2d2_error(g,0x501);break;}g->unpack=a[1];}g2PixelStorei(a[0],a[1]);break;
 case P2D2_glClear:g2Clear(a[0]);break;
 case P2D2_glClearColor:g2ClearColor(f[0],f[1],f[2],f[3]);break;
 case P2D2_glClearDepthf:g2ClearDepthf(f[0]);break;
 case P2D2_glDepthRangef:g2DepthRangef(f[0],f[1]);break;
 case P2D2_glDepthFunc:g2DepthFunc(a[0]);break;
 case P2D2_glDepthMask:g2DepthMask(a[0]);break;
 case P2D2_glBlendFunc:g2BlendFunc(a[0],a[1]);break;
 case P2D2_glBlendEquation:g2BlendEquation(a[0]);break;
 /* This ES2-only profile has no AlphaFunc entrypoint: the legacy test stays
    at its GL_ALWAYS default. Fog is only disabled, never emulated. */
 case P2D2_glEnable:if(g->ferrari_legacy_state&&(a[0]==0xde1||a[0]==0xbc0))break;g2Enable(a[0]);break;
 case P2D2_glDisable:if(g->ferrari_legacy_state&&(a[0]==0xde1||a[0]==0xbc0||a[0]==0xb50||a[0]==0xb60))break;g2Disable(a[0]);break;
 case P2D2_glCullFace:g2CullFace(a[0]);break;
 case P2D2_glFrontFace:g2FrontFace(a[0]);break;
 case P2D2_glColorMask:g2ColorMask(a[0],a[1],a[2],a[3]);break;
 case P2D2_glClearStencil:g2ClearStencil(a[0]);break;
 case P2D2_glStencilFunc:g2StencilFunc(a[0],a[1],a[2]);break;
 case P2D2_glStencilOp:g2StencilOp(a[0],a[1],a[2]);break;
 case P2D2_glStencilMask:g2StencilMask(a[0]);break;
 case P2D2_glViewport:g2Viewport(a[0],a[1],a[2],a[3]);break;
 case P2D2_glScissor:g2Scissor(a[0],a[1],a[2],a[3]);break;
 case P2D2_glFlush:g2Flush();break;
 case P2D2_glFinish:g2Finish();break;
 case P2D2_eglInitialize:{uint32_t values[3]={1,1,4};p2d2_result(g,r->op,values,12);break;}
 case P2D2_eglDestroyContext:case P2D2_eglTerminate:{uint32_t w=g->width,h=g->height,profile=g->compat_profile,locked=g->compat_locked;p2d2_context_close(g);memset(g,0,sizeof(*g));g->compat_profile=profile;g->compat_locked=locked;if(!p2d2_context_init(g)||!p2d2_resize(g,w,h))p2d2_error(g,0x505);break;}
 case 0x10001u:{uint32_t value=a[0]==0x48504331u?0x43525031u:0;p2d2_result(g,r->op,&value,4);break;}
 case P2D2_p2dD3DMReadBackBuffer565:g->present=2;break;
 case P2D2_eglSwapBuffers:g->present=1;break;
 default:p2d2_error(g,0x502);break;
 }
}
