#include "internal.h"
#include "east_shader_map.h"
static U32 textlen(const char*p,U32 max){U32 n=0;if(!p)return max;while(n<max&&p[n])n++;return n;}
static GuestBuffer*buffer(U32 name,int create){GuestBuffer*empty=0;if(!name)return 0;for(U32 i=0;i<1024;i++){GuestBuffer*b=S.buffers+i;if(b->name==name)return b;if(!b->name&&!empty)empty=b;}if(create&&empty){empty->name=name;return empty;}return 0;}
static U32 component(U32 t){return t==0x1400||t==0x1401?1:t==0x1402||t==0x1403?2:t==0x1406||t==0x140c?4:0;}
static int snapshots(U32 count){if(!count)return 1;for(U32 i=0;i<16;i++){GuestAttribute*t=S.attr+i;if(!t->enabled||t->buffer)continue;U32 element=t->size*component(t->type),stride=t->stride?t->stride:element;if(!t->pointer||!element||stride>CAP||count-1>(CAP-96-element)/stride){fail(0x501);return 0;}U32 n=(count-1)*stride+element,arg[16]={0};arg[0]=i;if(!record(P2D2_SNAPSHOT,arg,(void*)t->pointer,n))return 0;}return 1;}
static U32 qscalar(U32 op,U32*a,const void*p,U32 n){U32 value=0;query(op,a,p,n,&value,4);return value;}
U32 guest_gl(U32 op,U32*a){
 if(op==P2D2_glGetError){U32 local=S.error;U32 value=qscalar(op,a,0,0);S.error=0;return local?local:value;}
 if(!S.current){fail(0x502);return 0;}
 switch(op){
 /* No vendor binary formats are advertised; callers may fall back to GLSL.
    Never dereference or forward unsupported native shader binaries. */
 case P2D2_glShaderBinary:if(S.compat_profile&P2D2_PROFILE_EAST)return east_shader_binary(a);fail((I32)a[0]<0||(I32)a[4]<0?0x501:0x500);return 0;
 case P2D2_glReleaseShaderCompiler:return 0; /* Permitted hint/no-op. */
 case P2D2_p2dD3DMReadBackBuffer565:{
  if(a[2]!=S.width||a[3]!=S.height||a[2]>2048||a[3]>2048||a[1]<a[2]*2&&a[0]){fail(0x501);return 0;}
  if(!record(op,a,0,0)||!flush(0))return 0;
  if(!S.pixels)return 0;
  if(a[0]){for(U32 y=0;y<S.height;y++)copy((unsigned char*)a[0]+y*a[1],S.pixels+y*S.width*2,S.width*2);}
  else present();
  return !S.failed;
 }

 case P2D2_glGetString:return (U32)(a[0]==0x1f00?"Pocket2Droid":a[0]==0x1f01?"Pocket2Droid GLES2 bridge":a[0]==0x1f02?"OpenGL ES 2.0 Pocket2Droid compatibility bridge":a[0]==0x8b8c?"OpenGL ES GLSL ES 1.00":a[0]==0x1f03?"GL_AMD_compressed_ATC_texture":"");
 case P2D2_glCreateShader:case P2D2_glCreateProgram:case P2D2_glCheckFramebufferStatus:case P2D2_glIsShader:case P2D2_glIsProgram:return qscalar(op,a,0,0);
 case P2D2_glGetShaderiv:case P2D2_glGetProgramiv:if(!a[2]){fail(0x501);return 0;}query(op,a,0,0,(void*)a[2],4);return 0;
 case P2D2_glGetAttribLocation:case P2D2_glGetUniformLocation:{const char*p=(void*)a[1];U32 n=textlen(p,1024*1024);if(n==1024*1024){fail(0x501);return 0xffffffff;}return qscalar(op,a,p,n+1);}
 case P2D2_glBindAttribLocation:{const char*p=(void*)a[2];U32 n=textlen(p,1024*1024);if(n==1024*1024){fail(0x501);return 0;}record(op,a,p,n+1);return 0;}
 case P2D2_glShaderSource:{if((I32)a[1]<0||a[1]>256||(!a[2]&&a[1])){fail(0x501);return 0;}U32 total=0;const U32*strings=(void*)a[2];const I32*lengths=(void*)a[3];for(U32 i=0;i<a[1];i++){const char*p=(void*)strings[i];U32 n=lengths&&lengths[i]>=0?(U32)lengths[i]:textlen(p,1024*1024);if(!p||n>1024*1024-total){fail(0x501);return 0;}if(n&&!reserve_aux(total+n))return 0;if(n)copy(S.aux+total,p,n);total+=n;}record(op,a,S.aux,total);return 0;}
 case P2D2_glGetShaderInfoLog:case P2D2_glGetProgramInfoLog:case P2D2_glGetShaderSource:{if((I32)a[1]<0||a[1]>1024*1024-4||(!a[3]&&a[1])){fail(0x501);return 0;}if(!reserve_aux(a[1]+4))return 0;if(query(op,a,0,0,S.aux,a[1]+4)){U32 n=*(U32*)S.aux;if(a[2])*(U32*)a[2]=n;if(a[1])copy((void*)a[3],S.aux+4,a[1]);}return 0;}
 case P2D2_glGetIntegerv:{if(!a[1]){fail(0x501);return 0;}if(a[0]==0x8df9){*(U32*)a[1]=(S.compat_profile&P2D2_PROFILE_EAST)?1:0;S.num_shader_formats=(S.compat_profile&P2D2_PROFILE_EAST)?1:0;return 0;}if(a[0]==0x8df8){if(S.compat_profile&P2D2_PROFILE_EAST)*(U32*)a[1]=0;return 0;}U32 c=a[0]==0xba2||a[0]==0xc22||a[0]==0xc10||a[0]==0xc23||a[0]==0x8005?4:a[0]==0xb70||a[0]==0x846d||a[0]==0x846e||a[0]==0xd3a?2:a[0]==0x86a3?S.num_formats:1;if(c>256){fail(0x505);return 0;}if(query(op,a,0,0,(void*)a[1],c*4)){if(a[0]==0x86a2)S.num_formats=*(U32*)a[1];}return 0;}
 case P2D2_glGenBuffers:case P2D2_glGenFramebuffers:case P2D2_glGenRenderbuffers:case P2D2_glGenTextures:{if((I32)a[0]<0||a[0]>65536||(!a[1]&&a[0])){fail(0x501);return 0;}U32*out=(void*)a[1];U32 count=a[0];while(count){U32 args[16]={0};U32 n=count>16?16:count;args[0]=n;if(!query(op,args,0,0,out,n*4))return 0;out+=n;count-=n;}return 0;}
 case P2D2_glDeleteBuffers:case P2D2_glDeleteFramebuffers:case P2D2_glDeleteRenderbuffers:case P2D2_glDeleteTextures:{if((I32)a[0]<0||a[0]>(CAP-96)/4||(!a[1]&&a[0])){fail(0x501);return 0;}if(op==P2D2_glDeleteBuffers){U32*names=(void*)a[1];for(U32 i=0;i<a[0];i++){GuestBuffer*b=buffer(names[i],0);if(b){S.buffer_bytes-=b->size;if(b->data)S.release(b->data,0,0x8000);zero(b,sizeof(*b));}if(S.array_buffer==names[i])S.array_buffer=0;if(S.element_buffer==names[i])S.element_buffer=0;for(U32 j=0;j<16;j++)if(S.attr[j].buffer==names[i])S.attr[j].buffer=0;}}record(op,a,(void*)a[1],a[0]*4);return 0;}
 case P2D2_glBindBuffer:if(a[0]!=0x8892&&a[0]!=0x8893){fail(0x500);return 0;}if(a[1]&&!buffer(a[1],1)){fail(0x505);return 0;}if(a[0]==0x8892)S.array_buffer=a[1];else S.element_buffer=a[1];break;
 case P2D2_glBufferData:case P2D2_glBufferSubData:{if(a[0]!=0x8892&&a[0]!=0x8893){fail(0x500);return 0;}GuestBuffer*b=buffer(a[0]==0x8892?S.array_buffer:S.element_buffer,0);if(!b){fail(0x502);return 0;}
  if(op==P2D2_glBufferData){if(a[3]!=0x88e0&&a[3]!=0x88e4&&a[3]!=0x88e8){fail(0x500);return 0;}if(a[1]>CAP-96||S.buffer_bytes-b->size+a[1]>64*1024*1024){fail(0x505);return 0;}U32 accepted=0;if(!query(op,a,a[2]?(void*)a[2]:0,a[2]?a[1]:0,&accepted,4)||!accepted)return 0;S.buffer_bytes=S.buffer_bytes-b->size+a[1];b->size=a[1];}
  else{if(a[1]>b->size||a[2]>b->size-a[1]||(!a[3]&&a[2])){fail(0x501);return 0;}U32 accepted=0;query(op,a,(void*)a[3],a[2],&accepted,4);}return 0;}
 case P2D2_glVertexAttribPointer:{if(a[0]>=16||a[1]<1||a[1]>4||!component(a[2])||(I32)a[4]<0){fail(0x501);return 0;}GuestAttribute*t=S.attr+a[0];t->size=a[1];t->type=a[2];t->normalized=a[3];t->stride=a[4];t->pointer=a[5];t->buffer=S.array_buffer;a[6]=S.array_buffer;break;}
 case P2D2_glEnableVertexAttribArray:case P2D2_glDisableVertexAttribArray:if(a[0]>=16){fail(0x501);return 0;}S.attr[a[0]].enabled=op==P2D2_glEnableVertexAttribArray;break;
 case P2D2_glDrawArrays:if((I32)a[1]<0||(I32)a[2]<0||a[1]>1048576||a[2]>1048576-a[1]){fail(0x501);return 0;}if(!snapshots(a[1]+a[2]))return 0;break;
 case P2D2_glDrawElements:{U32 c=a[2]==0x1401?1:a[2]==0x1403?2:0;if(!c||(I32)a[1]<0||a[1]>1048576){fail(0x501);return 0;}U32 n=c*a[1],max=0;const unsigned char*p=(void*)a[3];if(S.element_buffer){GuestBuffer*b=buffer(S.element_buffer,0);if(!b||a[3]>b->size||n>b->size-a[3]){fail(0x502);return 0;}U32 clients=0;for(U32 i=0;i<16;i++)if(S.attr[i].enabled&&!S.attr[i].buffer)clients=1;if(clients&&a[1]){U32 args[16]={S.element_buffer,a[3],a[1],a[2]},values[2]={0};if(!query(P2D2_INDEX_MAX,args,0,0,values,8)||!values[0])return 0;max=values[1];}}else{if(!p&&n){fail(0x501);return 0;}for(U32 i=0;i<a[1];i++){U32 v=c==1?p[i]:(U32)p[i*2]|(U32)p[i*2+1]<<8;if(v>max)max=v;}}if(!snapshots(a[1]?max+1:0))return 0;a[4]=S.element_buffer;record(op,a,S.element_buffer?0:p,S.element_buffer?0:n);return 0;}
 case P2D2_glUniformMatrix3fv:case P2D2_glUniformMatrix4fv:{U32 c=op==P2D2_glUniformMatrix3fv?9:16;if((I32)a[1]<0||a[1]>(CAP-96)/(c*4)||(!a[3]&&a[1])){fail(0x501);return 0;}record(op,a,(void*)a[3],a[1]*c*4);return 0;}
 case P2D2_glUniform1fv:case P2D2_glUniform2fv:case P2D2_glUniform3fv:case P2D2_glUniform4fv:{U32 c=op==P2D2_glUniform1fv?1:op==P2D2_glUniform2fv?2:op==P2D2_glUniform3fv?3:4;if((I32)a[1]<0||a[1]>(CAP-96)/(c*4)||(!a[2]&&a[1])){fail(0x501);return 0;}record(op,a,(void*)a[2],a[1]*c*4);return 0;}
 case P2D2_glCompressedTexImage2D:if((I32)a[6]<0||a[6]>CAP-96||(!a[7]&&a[6])){fail(0x501);return 0;}record(op,a,(void*)a[7],a[6]);return 0;
 case P2D2_glTexImage2D:case P2D2_glTexSubImage2D:{U32 w=op==P2D2_glTexSubImage2D?a[4]:a[3],h=op==P2D2_glTexSubImage2D?a[5]:a[4],format=a[6],type=a[7],pixel=type==0x1401?(format==0x1908?4:format==0x1907?3:format==0x190a?2:format==0x1906||format==0x1909?1:0):(type==0x8363||type==0x8033||type==0x8034)?2:0;if(!pixel||w>2048||h>2048){fail(0x501);return 0;}U32 row=w*pixel,stride=(row+S.unpack-1)&~(S.unpack-1),n=h?(h-1)*stride+row:0;if(n>CAP-96){fail(0x505);return 0;}record(op,a,(void*)a[8],a[8]?n:0);return 0;}
 case P2D2_glPixelStorei:if(a[0]==0xcf5){if(a[1]!=1&&a[1]!=2&&a[1]!=4&&a[1]!=8){fail(0x501);return 0;}S.unpack=a[1];}break;
 default:break;
 }
 record(op,a,0,0);return 0;
}
