#include "state.h"
static LegacyTexture*legacy_texture(Native*g,int create){
 int name=0;g2GetIntegerv(0x8069,&name);LegacyTexture*empty=0;
 for(unsigned i=0;i<1024;i++){LegacyTexture*t=g->legacy_textures+i;if(t->used&&t->name==(unsigned)name)return t;if(!t->used&&!empty)empty=t;}
 if(create&&empty){empty->name=(unsigned)name;empty->used=1;return empty;}return 0;
}
int p2d2_legacy_parameter(Native*g,uint32_t target,uint32_t pname,float value){
 if(!g->ferrari_legacy_state||pname!=0x8191)return 0;
 if(target!=0xde1){p2d2_error(g,0x500);return 1;}
 LegacyTexture*t=legacy_texture(g,1);if(!t){p2d2_error(g,0x505);return 1;}t->generate=value!=0.f;return 1;
}
void p2d2_legacy_forget(Native*g,uint32_t name){
 if(name&&g->ferrari_legacy_state)for(unsigned i=0;i<1024;i++)if(g->legacy_textures[i].used&&g->legacy_textures[i].name==name)memset(g->legacy_textures+i,0,sizeof(LegacyTexture));
}
static void legacy_mipmap(Native*g,const P2D2Record*r){
 if(!g->ferrari_legacy_state||r->args[0]!=0xde1||r->args[1])return;
 unsigned error=g2GetError();if(error){p2d2_error(g,error);return;}
 LegacyTexture*t=legacy_texture(g,0);if(t&&t->generate)g2GenerateMipmap(0xde1);
}
static int codec(Native*g){
 if(g->decode)return 1;
 Dl_info own;if(!dladdr((void*)codec,&own)||!own.dli_fname)return 0;char path[4096];const char*slash=strrchr(own.dli_fname,'/');size_t n=slash?(size_t)(slash-own.dli_fname+1):0;
 if(n+sizeof("libp2d_gles_cpu.so")>sizeof(path))return 0;memcpy(path,own.dli_fname,n);memcpy(path+n,"libp2d_gles_cpu.so",sizeof("libp2d_gles_cpu.so"));g->codec=dlopen(path,RTLD_NOW|RTLD_LOCAL);if(!g->codec)return 0;*(void**)(&g->decode)=dlsym(g->codec,"p2d_gles_decode_compressed");return g->decode!=0;
}
void p2d2_texture(Native*g,const P2D2Record*r){
 const uint32_t*a=r->args;uint32_t w=r->op==P2D2_glTexSubImage2D?a[4]:a[3],h=r->op==P2D2_glTexSubImage2D?a[5]:a[4];
 if(w>2048||h>2048||(int32_t)a[1]<0||a[1]>11){p2d2_error(g,0x501);return;}
 if(r->op==P2D2_glCompressedTexImage2D){
  if(a[0]!=0xde1||a[5]||!w||!h||r->length!=a[6]){p2d2_error(g,0x501);return;}
  if(a[2]!=0x8c92&&a[2]!=0x8c93&&a[2]!=0x87ee&&a[2]!=0x83f0&&a[2]!=0x83f1&&a[2]!=0x83f2&&a[2]!=0x83f3){p2d2_error(g,0x500);return;}
  if(!codec(g)){p2d2_error(g,0x502);return;}size_t size=(size_t)w*h*4;uint8_t*data=malloc(size);if(!data){p2d2_error(g,0x505);return;}
  if(g->decode(a[2],w,h,r->payload,r->length,data,size)){free(data);p2d2_error(g,0x501);return;}
  unsigned format=0x1908;if(a[2]==0x8c92||a[2]==0x83f0){for(size_t i=0;i<size/4;i++)memmove(data+i*3,data+i*4,3);format=0x1907;}
  int unpack=4;g2GetIntegerv(0xcf5,&unpack);g2PixelStorei(0xcf5,1);g2TexImage2D(a[0],a[1],format,w,h,0,format,0x1401,data);g2PixelStorei(0xcf5,unpack);free(data);legacy_mipmap(g,r);return;
 }
 unsigned format=a[6],type=a[7],pixel=type==0x1401?(format==0x1908?4:format==0x1907?3:format==0x190a?2:format==0x1906||format==0x1909?1:0):(type==0x8363||type==0x8033||type==0x8034)?2:0;
 if(!pixel){p2d2_error(g,0x500);return;}size_t row=(size_t)w*pixel,stride=(row+g->unpack-1)&~(size_t)(g->unpack-1),need=h?(h-1)*stride+row:0;
 if((a[8]&&r->length!=need)||(!a[8]&&r->length)){p2d2_error(g,0x501);return;}
 const void*data=a[8]?r->payload:0;
 if(r->op==P2D2_glTexImage2D)g2TexImage2D(a[0],a[1],a[2],w,h,a[5],format,type,data);
 else g2TexSubImage2D(a[0],a[1],a[2],a[3],w,h,format,type,data);
 legacy_mipmap(g,r);
}
