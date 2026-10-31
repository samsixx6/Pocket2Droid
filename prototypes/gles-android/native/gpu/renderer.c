/* Bounded GLES1 packet renderer. Android uses the native fixed-point arrays
 * and GLES1 float entry points; desktop GL is the local rendering surrogate.
 * Define P2D_GPU_PROBE_STATS only for replay measurements.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include "pvrtc.h"
int p2d_dxt35_decode(unsigned,unsigned,unsigned,const unsigned char*,size_t,unsigned char*,size_t);
#include "../../guest/compact.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifdef P2D_GPU_PROBE_STATS
#include <time.h>
#endif
#include <dlfcn.h>
#include "../../guest/query.h"
#ifndef P2D_GL_SURROGATE
#define P2D_GL_SURROGATE 0
#endif

#ifdef P2D_ANDROID_PROBE
typedef float GLreal;
#define glClearDepth glClearDepthf
#define glDepthRange glDepthRangef
#define glFrustum glFrustumf
#define glOrtho glOrthof
#else
typedef double GLreal;
#endif

typedef void *EP;
extern EP eglGetDisplay(EP);
extern int eglInitialize(EP,int*,int*),eglBindAPI(unsigned);
extern int eglChooseConfig(EP,const int*,EP*,int,int*);
extern EP eglCreateContext(EP,EP,EP,const int*),eglCreatePbufferSurface(EP,EP,const int*);
extern int eglMakeCurrent(EP,EP,EP,EP),eglDestroyContext(EP,EP),eglDestroySurface(EP,EP),eglTerminate(EP);
extern void *eglGetProcAddress(const char*);

#define PROC(ret,name,...) static ret (*name)(__VA_ARGS__)
PROC(const unsigned char*,glGetString,unsigned);
PROC(unsigned,glGetError,void);
PROC(void,glGetIntegerv,unsigned,int*);
PROC(void,glGetFloatv,unsigned,float*);
PROC(void,glLightModelfv,unsigned,const float*);
PROC(void,glLightfv,unsigned,unsigned,const float*);
PROC(void,glActiveTexture,unsigned); PROC(void,glClientActiveTexture,unsigned);
PROC(void,glAlphaFunc,unsigned,float); PROC(void,glBindTexture,unsigned,unsigned);
PROC(void,glBlendFunc,unsigned,unsigned); PROC(void,glClear,unsigned);
PROC(void,glClearColor,float,float,float,float); PROC(void,glClearDepth,GLreal);
PROC(void,glClearStencil,int); PROC(void,glColor4f,float,float,float,float);
PROC(void,glColorMask,unsigned char,unsigned char,unsigned char,unsigned char);
PROC(void,glColorPointer,int,unsigned,int,const void*);
PROC(void,glCullFace,unsigned); PROC(void,glDeleteTextures,int,const unsigned*);
PROC(void,glDepthFunc,unsigned); PROC(void,glDepthMask,unsigned char);
PROC(void,glDepthRange,GLreal,GLreal); PROC(void,glDisable,unsigned);
PROC(void,glEnable,unsigned); PROC(void,glDisableClientState,unsigned); PROC(void,glEnableClientState,unsigned);
PROC(void,glDrawArrays,unsigned,int,int); PROC(void,glDrawElements,unsigned,int,unsigned,const void*);
PROC(void,glFinish,void); PROC(void,glFlush,void); PROC(void,glFogf,unsigned,float);
PROC(void,glFogfv,unsigned,const float*); PROC(void,glFrontFace,unsigned);
PROC(void,glFrustum,GLreal,GLreal,GLreal,GLreal,GLreal,GLreal);
PROC(void,glHint,unsigned,unsigned); PROC(void,glLoadIdentity,void);
PROC(void,glLoadMatrixf,const float*); PROC(void,glMultMatrixf,const float*);
PROC(void,glMaterialfv,unsigned,unsigned,const float*);
PROC(void,glMatrixMode,unsigned); PROC(void,glMultiTexCoord4f,unsigned,float,float,float,float);
PROC(void,glNormal3f,float,float,float); PROC(void,glNormalPointer,unsigned,int,const void*);
PROC(void,glOrtho,GLreal,GLreal,GLreal,GLreal,GLreal,GLreal);
PROC(void,glPopMatrix,void); PROC(void,glPushMatrix,void);
PROC(void,glRotatef,float,float,float,float); PROC(void,glScalef,float,float,float);
PROC(void,glScissor,int,int,int,int); PROC(void,glShadeModel,unsigned);
PROC(void,glTexCoordPointer,int,unsigned,int,const void*);
PROC(void,glTexEnvf,unsigned,unsigned,float); PROC(void,glTexEnvfv,unsigned,unsigned,const float*);
PROC(void,glTexImage2D,unsigned,int,int,int,int,int,unsigned,unsigned,const void*);
PROC(void,glTexParameterf,unsigned,unsigned,float); PROC(void,glTranslatef,float,float,float);
PROC(void,glPolygonOffset,float,float);
PROC(void,glVertexPointer,int,unsigned,int,const void*); PROC(void,glViewport,int,int,int,int);
PROC(void,glReadPixels,int,int,int,int,unsigned,unsigned,void*);
PROC(void,glPixelStorei,unsigned,int);

#define MAX_PACKET (16u*1024*1024)
#define MAX_DIM 2048u
#define TEXTURE_UNITS 4u
#define MAX_TEXTURES 4096u
#define TEXTURE_SLOTS 8192u
#define TEXTURE_LEVELS 12u
#define MAX_TEXTURE_BYTES (128u*1024*1024)
#define MAX_SNAPSHOTS 4096u
typedef struct Snapshot {uint32_t base,len;unsigned char *bytes;struct Snapshot *next;} Snapshot;
typedef struct {uint32_t size,type,stride,pointer,enabled;} Array;
typedef struct {const unsigned char*data;size_t bytes;uint32_t size,type,stride;} PaletteSpan;
typedef struct {uint32_t name,generate_mipmap;size_t level_bytes[TEXTURE_LEVELS];} Texture;
typedef struct {
 EP display,config,context,surface;
 EP siege_target[9];uint32_t siege_w[9],siege_h[9],siege_id;
 uint32_t width,height,client_unit,server_unit,texture_enabled[4],error,units,available_units;
 Array vertex,color,normal,texcoord[4]; Snapshot *snapshots;size_t snapshot_bytes;uint32_t snapshot_count;
 Texture textures[TEXTURE_SLOTS],default_texture[TEXTURE_UNITS];
 uint32_t bound_texture[TEXTURE_UNITS],texture_count;size_t texture_bytes;
 unsigned char *response,*raw;size_t capacity;
 uint32_t query[19],has_query,palette_enabled,palette_mode,palette_index,lighting_enabled;
 Array weights,matrix_indices;float palette[32][16];
 uint32_t (*matrix_command)(float*,uint32_t,const uint32_t*);
 uint32_t (*blend_palette)(const float*,const PaletteSpan*,uint32_t,float*,float*,const unsigned char*);
 void *codec;int (*decode)(unsigned,unsigned,unsigned,const unsigned char*,size_t,unsigned char*,size_t);
#ifdef P2D_GPU_PROBE_STATS
 uint64_t batches,draws,submit_ns,readback_ns,convert_ns;
#endif
 char renderer[256];int first,compact;
} GPU;

#ifdef P2D_GPU_PROBE_STATS
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
#define COUNT(g,field) ((g)->field++)
#else
#define COUNT(g,field) ((void)0)
#endif
static uint32_t word(const void *p){uint32_t n;memcpy(&n,p,4);return n;}
static float val(uint32_t v,int fixed){float f;memcpy(&f,&v,4);return fixed?(float)(int32_t)v/65536.f:f;}
static void error(GPU *g,uint32_t e){if(!g->error)g->error=e;}
static void clear_snapshots(GPU *g){Snapshot*s=g->snapshots;while(s){Snapshot*n=s->next;free(s->bytes);free(s);s=n;}g->snapshots=NULL;g->snapshot_bytes=0;g->snapshot_count=0;}
/* Consume older driver errors before a resource mutation, then commit ownership
 * or budget changes only if that mutation itself succeeds. */
static int gl_success(GPU*g){unsigned e;int ok=1;while((e=glGetError())){error(g,e);ok=0;}return ok;}
static uint32_t texture_hash(uint32_t n){n^=n>>16;n*=0x7feb352du;n^=n>>15;n*=0x846ca68bu;n^=n>>16;return n&(TEXTURE_SLOTS-1);}
static Texture*texture_slot(GPU*g,uint32_t name){uint32_t i=texture_hash(name);while(g->textures[i].name&&g->textures[i].name!=name)i=(i+1)&(TEXTURE_SLOTS-1);return &g->textures[i];}
static void texture_remove(GPU*g,uint32_t name){
 if(!name)return;Texture*t=texture_slot(g,name);if(!t->name)return;
 for(unsigned level=0;level<TEXTURE_LEVELS;level++)g->texture_bytes-=t->level_bytes[level];g->texture_count--;
 for(unsigned unit=0;unit<g->units;unit++)if(g->bound_texture[unit]==name)g->bound_texture[unit]=0;
 /* Back-shift the open-addressed chain: deletion never accumulates tombstones. */
 uint32_t hole=(uint32_t)(t-g->textures),i=(hole+1)&(TEXTURE_SLOTS-1);
 while(g->textures[i].name){uint32_t home=texture_hash(g->textures[i].name);
  if(((i-home)&(TEXTURE_SLOTS-1))>=((i-hole)&(TEXTURE_SLOTS-1))){g->textures[hole]=g->textures[i];hole=i;}
  i=(i+1)&(TEXTURE_SLOTS-1);
 }memset(&g->textures[hole],0,sizeof(g->textures[hole]));
}
static void bind_texture_object(GPU*g,uint32_t target,uint32_t name){
 if(target!=0xde1){error(g,0x500);return;}Texture*t=name?texture_slot(g,name):NULL;
 if(t&&!t->name&&g->texture_count==MAX_TEXTURES){error(g,0x505);return;}
 gl_success(g);glBindTexture(target,name);if(!gl_success(g))return;
 if(t&&!t->name){memset(t,0,sizeof(*t));t->name=name;g->texture_count++;}g->bound_texture[g->server_unit]=name;
}
static void delete_texture_objects(GPU*g,uint32_t n,const unsigned char*names){
 gl_success(g);glDeleteTextures((int)n,(const unsigned*)names);if(!gl_success(g))return;
 for(uint32_t i=0;i<n;i++)texture_remove(g,word(names+i*4));
}
static int read_guest(GPU*g,uint32_t addr,size_t n,unsigned char*out){
 uint64_t cur=addr,end=cur+n;if(n>MAX_PACKET||end>0x100000000ull)return 0;
 while(cur<end){Snapshot*s=g->snapshots;while(s&&!(cur>=s->base&&cur<(uint64_t)s->base+s->len))s=s->next;if(!s)return 0;
  uint64_t until=end<(uint64_t)s->base+s->len?end:(uint64_t)s->base+s->len;
  for(Snapshot*newer=g->snapshots;newer!=s;newer=newer->next)if(newer->base>cur&&newer->base<until)until=newer->base;
  memcpy(out,s->bytes+(cur-s->base),until-cur);out+=until-cur;cur=until;
 }return 1;
}
static void*gl_proc(const char*name){
#ifdef P2D_ANDROID_PROBE
 static void*library;if(!library)library=dlopen("libGLESv1_CM.so",RTLD_NOW|RTLD_LOCAL);
 return library?dlsym(library,name):NULL;
#else
 return eglGetProcAddress(name);
#endif
}
static int load_gl(void){
#define STRING_INNER(n) #n
#define STRING(n) STRING_INNER(n)
#define LOAD(n) do{*(void**)(&n)=gl_proc(STRING(n));if(!n)return 0;}while(0)
 LOAD(glGetString);LOAD(glGetError);LOAD(glGetIntegerv);LOAD(glGetFloatv);LOAD(glLightModelfv);LOAD(glLightfv);LOAD(glActiveTexture);LOAD(glClientActiveTexture);LOAD(glAlphaFunc);LOAD(glBindTexture);
 LOAD(glBlendFunc);LOAD(glClear);LOAD(glClearColor);LOAD(glClearDepth);LOAD(glClearStencil);LOAD(glColor4f);LOAD(glColorMask);
 LOAD(glColorPointer);LOAD(glCullFace);LOAD(glDeleteTextures);LOAD(glDepthFunc);LOAD(glDepthMask);LOAD(glDepthRange);
 LOAD(glDisable);LOAD(glEnable);LOAD(glDisableClientState);LOAD(glEnableClientState);LOAD(glDrawArrays);LOAD(glDrawElements);
 LOAD(glFinish);LOAD(glFlush);LOAD(glFogf);LOAD(glFogfv);LOAD(glFrontFace);LOAD(glFrustum);LOAD(glHint);LOAD(glLoadIdentity);
 LOAD(glLoadMatrixf);LOAD(glMultMatrixf);LOAD(glMaterialfv);LOAD(glMatrixMode);LOAD(glMultiTexCoord4f);LOAD(glNormal3f);LOAD(glNormalPointer);
 LOAD(glOrtho);LOAD(glPopMatrix);LOAD(glPushMatrix);LOAD(glRotatef);LOAD(glScalef);LOAD(glScissor);LOAD(glShadeModel);
 LOAD(glTexCoordPointer);LOAD(glTexEnvf);LOAD(glTexEnvfv);LOAD(glTexImage2D);LOAD(glTexParameterf);LOAD(glTranslatef);
 LOAD(glPolygonOffset);LOAD(glVertexPointer);LOAD(glViewport);LOAD(glReadPixels);LOAD(glPixelStorei);return 1;
#undef LOAD
}
static int resize(GPU*g,uint32_t w,uint32_t h){
 if(!w||!h||w>MAX_DIM||h>MAX_DIM)return 0;
 unsigned slot=g->siege_id?g->siege_id-1:0;if(slot>=9)return 0;
 EP target=g->siege_target[slot];int fresh=!target||g->siege_w[slot]!=w||g->siege_h[slot]!=h;
 size_t size=24+(size_t)w*h*2;unsigned char*response=NULL,*raw=NULL;
 if(size!=g->capacity){response=malloc(size+76);raw=malloc((size_t)w*h*4);if(!response||!raw){free(response);free(raw);return 0;}}
 if(fresh){uint64_t total=(uint64_t)w*h;for(unsigned i=0;i<9;i++)if(i!=slot)total+=(uint64_t)g->siege_w[i]*g->siege_h[i];if(total>2ull*MAX_DIM*MAX_DIM){free(response);free(raw);return 0;}int attrs[]={0x3057,(int)w,0x3056,(int)h,0x3038};target=eglCreatePbufferSurface(g->display,g->config,attrs);if(!target){free(response);free(raw);return 0;}}
 if(!eglMakeCurrent(g->display,target,target,g->context)){if(fresh)eglDestroySurface(g->display,target);free(response);free(raw);return 0;}
 if(fresh){if(g->siege_target[slot])eglDestroySurface(g->display,g->siege_target[slot]);g->siege_target[slot]=target;g->siege_w[slot]=w;g->siege_h[slot]=h;}
 g->surface=target;g->width=w;g->height=h;
 if(response){free(g->response);free(g->raw);g->response=response;g->raw=raw;g->capacity=size;}
 if(g->first){glViewport(0,0,w,h);g->first=0;}return 1;
}
uint32_t p2d_gles_reply_capability(void){return P2D_COMPACT_CAPABILITY;}
void p2d_gles_destroy(void*opaque){GPU*g=opaque;if(!g)return;clear_snapshots(g);if(g->display){eglMakeCurrent(g->display,NULL,NULL,NULL);if(g->context)eglDestroyContext(g->display,g->context);for(unsigned i=0;i<9;i++)if(g->siege_target[i])eglDestroySurface(g->display,g->siege_target[i]);eglTerminate(g->display);}if(g->codec)dlclose(g->codec);free(g->raw);free(g->response);free(g);}
static int software_renderer(const unsigned char*s){
 if(!s)return 1;char lower[512];size_t i=0;for(;i<sizeof(lower)-1&&s[i];i++)lower[i]=(s[i]>='A'&&s[i]<='Z')?(char)(s[i]+'a'-'A'):(char)s[i];lower[i]=0;
 return strstr(lower,"llvmpipe")||strstr(lower,"softpipe")||strstr(lower,"swiftshader")||strstr(lower,"software")||strstr(lower,"swrast")||strstr(lower,"lavapipe")||strstr(lower,"gdi generic")||strstr(lower,"basic render");
}
void*p2d_gles_create(void){
 GPU*g=calloc(1,sizeof(*g));if(!g)return NULL;int major,minor,count;
 g->display=eglGetDisplay(NULL);if(!g->display||!eglInitialize(g->display,&major,&minor))goto fail;
#ifdef P2D_ANDROID_PROBE
 if(!eglBindAPI(0x30a0))goto fail;
 int attrs[]={0x3033,1,0x3040,1,0x3024,8,0x3023,8,0x3022,8,0x3025,16,0x3038};
 int ctxattrs[]={0x3098,1,0x3038};
#else
 if(!eglBindAPI(0x30a2))goto fail;
 int attrs[]={0x3033,1,0x3040,8,0x3024,8,0x3023,8,0x3022,8,0x3025,24,0x3026,8,0x3038};
 int ctxattrs[]={0x3038};
#endif
 if(!eglChooseConfig(g->display,attrs,&g->config,1,&count)||!count)goto fail;
 g->context=eglCreateContext(g->display,g->config,NULL,ctxattrs);if(!g->context||!load_gl()||!resize(g,1,1))goto fail;
 const unsigned char*r=glGetString(0x1f01);if(!r)goto fail;snprintf(g->renderer,sizeof(g->renderer),"%s",r);
 if(!P2D_GL_SURROGATE&&(software_renderer(r)||software_renderer(glGetString(0x1f00))))goto fail;
 int units=0;glGetIntegerv(0x84e2,&units);if(units<2||!gl_success(g))goto fail;g->available_units=(uint32_t)units;g->units=2;
 int max_texture=0;glGetIntegerv(0xd33,&max_texture);if(max_texture<(int)MAX_DIM||!gl_success(g))goto fail;
 g->vertex=(Array){4,0x1406,0,0,0};g->color=(Array){4,0x1406,0,0,0};g->normal=(Array){3,0x1406,0,0,0};
 for(int i=0;i<4;i++)g->texcoord[i]=(Array){4,0x1406,0,0,0};
 glDisable(0xbd0); /* RGB565 conversion is explicit; deterministic tests, no dithering. */
 glPixelStorei(0xcf5,1);glPixelStorei(0xd05,1);glClearColor(0,0,0,0);glClear(0x4500);g->first=1;for(unsigned m=0;m<32;m++)for(unsigned i=0;i<16;i++)g->palette[m][i]=(i%5)==0?1.f:0.f;return g;
 fail:p2d_gles_destroy(g);return NULL;
}
const char*p2d_gpu_renderer(void*opaque){return opaque?((GPU*)opaque)->renderer:NULL;}
#ifdef P2D_GPU_PROBE_STATS
void p2d_gpu_stats(void*opaque,uint64_t*out){GPU*g=opaque;out[0]=g->batches;out[1]=g->draws;out[2]=g->submit_ns;out[3]=g->readback_ns;out[4]=g->convert_ns;}
#endif
static int supported(uint32_t op){return(op>=35&&op<=51)||(op>=55&&op<=77)||(op>=80&&op<=88)||(op>=91&&op<=93)||(op>=95&&op<=108)||(op>=112&&op<=118)||(op>=121&&op<=124)||(op>=128&&op<=131)||(op>=133&&op<=135)||(op>=137&&op<=140)||op==144||op==147||op==148||op==150||op==151||op==0x10000||op==P2D_COMPACT_PROBE||op==0x10002;}
static int parse(const unsigned char*b,size_t n){
 if(n<24||n>MAX_PACKET||(word(b)!=P2D_REQUEST_FULL&&word(b)!=P2D_REQUEST_COMPACT)||word(b+20)!=n)return 0;
 uint32_t count=word(b+8),w=word(b+12),h=word(b+16);if(!w||!h||w>MAX_DIM||h>MAX_DIM||count>32768||count>(n-24)/72)return 0;
 size_t off=24;
 for(uint32_t i=0;i<count;i++){if(n-off<72)return 0;const unsigned char*r=b+off;uint32_t op=word(r),len=word(r+68);size_t size=72+((uint64_t)len+3u)/4*4;if(size>n-off||!supported(op))return 0;
  if(op==P2D_COMPACT_PROBE){
   if(n!=96||count!=1||word(b+4)!=1||word(b)!=P2D_REQUEST_FULL||len||(word(r+4)!=0&&word(r+4)!=P2D_COMPACT_HOST))return 0;
   for(unsigned k=1;k<16;k++)if(word(r+4+k*4))return 0;
  }
  else if(op==0x10002){if(i||len||word(r+4)<2||word(r+4)>9)return 0;for(unsigned k=1;k<16;k++)if(word(r+4+k*4))return 0;}
  else if(op==0x10000){if(word(r+8)!=len||(uint64_t)word(r+4)+len>0x100000000ull)return 0;}
  else if(op==56){if(word(r+4)>4096||len!=word(r+4)*4)return 0;}
  else if(op==133){if(word(r+16)>MAX_DIM||word(r+20)>MAX_DIM)return 0;}
  else if(op==51){if(word(r+16)>MAX_DIM||word(r+20)>MAX_DIM||word(r+28)!=len)return 0;}
  else if(op==147){if(len||i+1!=count||!p2d_query_count(word(r+4)))return 0;}
  else if(len)return 0;off+=size;
 }return off==n;
}
static unsigned component(uint32_t type){switch(type){case 0x1400:case 0x1401:return 1;case 0x1402:case 0x1403:return 2;case 0x1406:case 0x140c:return 4;default:return 0;}}
static void array_pointer(GPU*g,Array*p,uint32_t size,uint32_t type,uint32_t stride,uint32_t pointer,int color){
 if((color?size!=4:(size<2||size>4))||(int32_t)stride<0){error(g,0x501);return;}
 if(!(type==0x1406||type==0x140c||(color?type==0x1401:(type==0x1400||type==0x1402)))){error(g,0x500);return;}
 p->size=size;p->type=type;p->stride=stride;p->pointer=pointer;
}
static unsigned char*array_data(GPU*g,Array*a,uint32_t first,uint32_t count,unsigned*type,unsigned*stride){
 unsigned c=component(a->type);if(!a->size||a->size>4||!c){error(g,0x500);return NULL;}
 uint64_t step=a->stride?a->stride:a->size*c,addr=(uint64_t)a->pointer+first*step,n=count?(count-1)*step+a->size*c:0;
 if(addr>UINT32_MAX||n>MAX_PACKET){error(g,0x502);return NULL;}unsigned char*p=malloc(n?n:1);if(!p){error(g,0x505);return NULL;}
 if(!read_guest(g,(uint32_t)addr,n,p)){free(p);error(g,0x502);return NULL;}
 *type=a->type;*stride=a->stride;
#ifndef P2D_ANDROID_PROBE
 if(a->type==0x140c||(a->type==0x1400&&a!=&g->normal)){
  float*f=malloc((size_t)count*a->size*4);if(!f){free(p);error(g,0x505);return NULL;}
  for(uint32_t i=0;i<count;i++)for(uint32_t j=0;j<a->size;j++)f[i*a->size+j]=a->type==0x140c?val(word(p+i*step+j*4),1):(float)(int8_t)p[i*step+j];
  free(p);p=(unsigned char*)f;*type=0x1406;*stride=0;
 }
#endif
 return p;
}
static int load_codec(GPU*g);
static void draw(GPU*g,uint32_t op,const uint32_t*a){
 uint32_t count=op==63?a[2]:a[1],first=op==63?a[1]:0,max=first;unsigned char*indices=NULL,*data[7]={0};unsigned type,stride,vertex_type=0,vertex_stride=0,normal_type=0,normal_stride=0;int ok=1,palette_applied=0,saved_mode=0,restore_normal=0;float saved_modelview[16],saved_normal[3];
 if(count>65536||first>UINT32_MAX-count){error(g,0x501);goto done;}if(!count)goto done;
 if(op==64){unsigned c=component(a[2]);if((a[2]!=0x1401&&a[2]!=0x1403)||!c){error(g,0x500);goto done;}indices=malloc(count*c);if(!indices){error(g,0x505);goto done;}
  if(!read_guest(g,a[3],count*c,indices)){error(g,0x502);goto done;}
  first=65535;max=0;for(uint32_t i=0;i<count;i++){uint32_t n=c==1?indices[i]:((uint16_t*)indices)[i];if(n<first)first=n;if(n>max)max=n;}
  for(uint32_t i=0;i<count;i++){if(c==1)indices[i]-=first;else((uint16_t*)indices)[i]-=first;}
 }else max=first+count-1;
 Array*arrays[]={&g->vertex,&g->color,&g->normal,&g->texcoord[0],&g->texcoord[1],&g->texcoord[2],&g->texcoord[3]};
 unsigned enums[]={0x8074,0x8076,0x8075,0x8078,0x8078,0x8078,0x8078};
 for(unsigned i=0;i<3+g->units;i++){
  if(i>=3)glClientActiveTexture(0x84c0+i-3);
  int enabled=arrays[i]->enabled&&(i<3||g->texture_enabled[i-3]);
  if(!enabled){glDisableClientState(enums[i]);continue;}
  data[i]=array_data(g,arrays[i],first,max-first+1,&type,&stride);if(!data[i]){ok=0;break;}
  if(i==0){vertex_type=type;vertex_stride=stride;}if(i==2){normal_type=type;normal_stride=stride;}glEnableClientState(enums[i]);switch(i){case 0:glVertexPointer(arrays[i]->size,type,stride,data[i]);break;case 1:glColorPointer(arrays[i]->size,type,stride,data[i]);break;case 2:glNormalPointer(type,stride,data[i]);break;default:glTexCoordPointer(arrays[i]->size,type,stride,data[i]);}
 }
 glClientActiveTexture(0x84c0+g->client_unit);
 if(ok&&g->palette_enabled){
  if(!g->vertex.enabled||!g->weights.enabled||!g->matrix_indices.enabled||g->weights.size!=g->matrix_indices.size){ok=0;goto done;}
  if(!load_codec(g)||!g->blend_palette){error(g,0x502);ok=0;goto done;}
  uint32_t n=max-first+1;unsigned wt,ws,it,is;
  unsigned char*w=array_data(g,&g->weights,first,n,&wt,&ws),*ids=array_data(g,&g->matrix_indices,first,n,&it,&is);
  float*positions=calloc(n,16),*normals=g->lighting_enabled?calloc(n,12):NULL,*constant=NULL;unsigned char*used=indices?calloc(n,1):NULL;
  if(!w||!ids||!positions||(g->lighting_enabled&&!normals)||(indices&&!used)){free(w);free(ids);free(positions);free(normals);free(used);error(g,0x505);ok=0;goto done;}
  if(used)for(uint32_t j=0;j<count;j++)used[a[2]==0x1401?indices[j]:((uint16_t*)indices)[j]]=1;
  if(g->lighting_enabled&&!data[2]){glGetFloatv(0xb02,saved_normal);constant=malloc((size_t)n*12);if(!constant){free(w);free(ids);free(positions);free(normals);free(used);error(g,0x505);ok=0;goto done;}for(uint32_t j=0;j<n;j++)memcpy(constant+j*3,saved_normal,12);normal_type=0x1406;normal_stride=0;restore_normal=1;}
  unsigned sizes[]={g->vertex.size,g->lighting_enabled?3:0,g->weights.size,g->matrix_indices.size};unsigned types[]={vertex_type,normal_type,wt,it};unsigned strides[]={vertex_stride,normal_stride,ws,is};
  const unsigned char*pointers[]={data[0],data[2]?data[2]:(const unsigned char*)constant,w,ids};PaletteSpan spans[4];
  for(unsigned j=0;j<4;j++){unsigned c=component(types[j]);size_t step=strides[j]?strides[j]:sizes[j]*c;spans[j]=(PaletteSpan){pointers[j],(n-1)*step+sizes[j]*c,sizes[j],types[j],strides[j]};}
  uint32_t e=g->blend_palette(&g->palette[0][0],spans,n,positions,normals,used);free(w);free(ids);free(constant);free(used);
  if(e){free(positions);free(normals);error(g,e);ok=0;goto done;}
  glVertexPointer(4,0x1406,0,positions);free(data[0]);data[0]=(unsigned char*)positions;
  if(normals){glEnableClientState(0x8075);glNormalPointer(0x1406,0,normals);free(data[2]);data[2]=(unsigned char*)normals;}
  glGetIntegerv(0xba0,&saved_mode);glGetFloatv(0xba6,saved_modelview);glMatrixMode(0x1700);glLoadIdentity();palette_applied=1;
 }
 if(ok){if(op==63)glDrawArrays(a[0],0,count);else glDrawElements(a[0],count,a[2],indices);COUNT(g,draws);}
 done:
 if(palette_applied){glMatrixMode(0x1700);glLoadMatrixf(saved_modelview);glMatrixMode((unsigned)saved_mode);}
 /* Client pointer state outlives glDraw*. Detach every installed temporary
  * pointer before freeing it, including arrays installed before a failed draw. */
 for(unsigned i=0;i<7;i++)if(data[i]){
  switch(i){case 0:glDisableClientState(0x8074);glVertexPointer(4,0x1406,0,NULL);break;
   case 1:glDisableClientState(0x8076);glColorPointer(4,0x1406,0,NULL);break;
   case 2:glDisableClientState(0x8075);glNormalPointer(0x1406,0,NULL);break;
   default:glClientActiveTexture(0x84c0+i-3);glDisableClientState(0x8078);glTexCoordPointer(4,0x1406,0,NULL);break;
  }free(data[i]);
 }
 if(restore_normal)glNormal3f(saved_normal[0],saved_normal[1],saved_normal[2]);
 glClientActiveTexture(0x84c0+g->client_unit);free(indices);clear_snapshots(g);
}
static void texture_image(GPU*g,const uint32_t*a,const unsigned char*payload,uint32_t len){
 if(a[0]!=0xde1){error(g,0x500);return;}
 unsigned channels=a[6]==0x1908?4:a[6]==0x1907?3:a[6]==0x190a?2:(a[6]==0x1906||a[6]==0x1909)?1:0;
 unsigned bpp=a[7]==0x1401?channels:(a[7]==0x8363&&a[6]==0x1907)||((a[7]==0x8033||a[7]==0x8034)&&a[6]==0x1908)?2:0;
 if(!bpp){error(g,0x500);return;}
 if(a[1]>=TEXTURE_LEVELS||a[5]||a[2]!=a[6]||a[3]>(MAX_DIM>>a[1])||a[4]>(MAX_DIM>>a[1])){error(g,0x501);return;}
 size_t needed=(size_t)a[3]*a[4]*bpp;
 if((len&&len!=needed)||(!len&&a[8]&&needed)){error(g,0x502);return;}
 uint32_t name=g->bound_texture[g->server_unit];Texture*t=name?texture_slot(g,name):&g->default_texture[g->server_unit];
 /* Count RGBA8 expansion even for byte, RGB565 and null-data allocations.
  * Default name zero is a distinct default object on each texture unit. */
 size_t levels[TEXTURE_LEVELS];memcpy(levels,t->level_bytes,sizeof(levels));levels[a[1]]=(size_t)a[3]*a[4]*4;
 if(!a[1]&&t->generate_mipmap){uint32_t w=a[3],h=a[4],level=1;
  while((w>1||h>1)&&level<TEXTURE_LEVELS){w=w>1?w/2:1;h=h>1?h/2:1;levels[level++]=(size_t)w*h*4;}
 }
 size_t previous=0,expanded=0;for(unsigned level=0;level<TEXTURE_LEVELS;level++){previous+=t->level_bytes[level];expanded+=levels[level];}
 if(expanded>MAX_TEXTURE_BYTES-(g->texture_bytes-previous)){error(g,0x505);return;}
 gl_success(g);glTexImage2D(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],len?payload:NULL);if(!gl_success(g))return;
 g->texture_bytes=g->texture_bytes-previous+expanded;memcpy(t->level_bytes,levels,sizeof(levels));
}
static void texture_parameter(GPU*g,uint32_t target,uint32_t pname,float value){
 if(pname!=0x8191){glTexParameterf(target,pname,value);return;}
 /* GL_GENERATE_MIPMAP creates more than the submitted level on later base
  * uploads. Keep its successful driver state so those allocations are charged. */
 gl_success(g);glTexParameterf(target,pname,value);if(!gl_success(g))return;
 if(target==0xde1){uint32_t name=g->bound_texture[g->server_unit];Texture*t=name?texture_slot(g,name):&g->default_texture[g->server_unit];t->generate_mipmap=value!=0.f;}
}
static int load_codec(GPU*g){
 if(g->decode&&g->matrix_command&&g->blend_palette)return 1;
 if(!g->codec&&!g->decode){
  Dl_info info;char path[4096];if(!dladdr((void*)load_codec,&info)||!info.dli_fname)return 0;
  const char*slash=strrchr(info.dli_fname,'/');size_t dir=slash?(size_t)(slash-info.dli_fname+1):0;
  if(dir+sizeof("libp2d_gles_cpu.so")>sizeof(path))return 0;
  memcpy(path,info.dli_fname,dir);memcpy(path+dir,"libp2d_gles_cpu.so",sizeof("libp2d_gles_cpu.so"));g->codec=dlopen(path,RTLD_NOW|RTLD_LOCAL);
 }
 if(g->codec){
  *(void**)(&g->decode)=dlsym(g->codec,"p2d_gles_decode_compressed");
  *(void**)(&g->matrix_command)=dlsym(g->codec,"p2d_gles_palette_matrix");
  *(void**)(&g->blend_palette)=dlsym(g->codec,"p2d_gles_palette_blend");
 }
 return g->decode!=NULL;
}
static void compressed_image(GPU*g,const uint32_t*a,const unsigned char*payload,uint32_t len){
 if(a[0]!=0xde1){error(g,0x500);return;}if(a[1]>=TEXTURE_LEVELS||a[5]||!a[3]||!a[4]||a[3]>(MAX_DIM>>a[1])||a[4]>(MAX_DIM>>a[1])){error(g,0x501);return;}
 int pvrtc=a[2]>=0x8c00&&a[2]<=0x8c03;
 int dxt35=a[2]==0x83f2||a[2]==0x83f3;
 if(!pvrtc&&!dxt35&&(!load_codec(g)||!g->decode)){error(g,0x502);return;}
 size_t n=(size_t)a[3]*a[4]*4;unsigned char*rgba=malloc(n);if(!rgba){error(g,0x505);return;}
 int decoded=pvrtc?p2d_pvrtc_decode(a[2],a[3],a[4],payload,len,rgba,n):dxt35?p2d_dxt35_decode(a[2],a[3],a[4],payload,len,rgba,n):g->decode(a[2],a[3],a[4],payload,len,rgba,n);
 if(decoded){free(rgba);error(g,pvrtc&&decoded==2?0x505:0x501);return;}
 /* RGB base formats preserve primary alpha under GL_REPLACE. Uploading their
    decoded bytes as RGBA would incorrectly make fading sprites opaque. */
 unsigned format=0x1908;if(a[2]==0x8c92||a[2]==0x83f0||a[2]==0x8c00||a[2]==0x8c01){format=0x1907;for(size_t i=0;i<n/4;i++)memmove(rgba+i*3,rgba+i*4,3);n=n/4*3;}
 uint32_t image[16]={a[0],a[1],format,a[3],a[4],0,format,0x1401,1};texture_image(g,image,rgba,(uint32_t)n);free(rgba);
}
static void state(GPU*g,uint32_t op,const uint32_t*a,const unsigned char*payload,uint32_t len){
 if(op==99){if(a[0]==0x8840){g->palette_mode=1;return;}if(a[0]!=0x1700&&a[0]!=0x1701&&a[0]!=0x1702){error(g,0x500);return;}g->palette_mode=0;}
 if(g->palette_mode&&(op==91||op==92||op==93||op==100||op==101||op==137||op==138||op==121||op==122||op==117||op==118||op==74||op==75||op==107||op==108)){
  if(!load_codec(g)||!g->matrix_command){error(g,0x502);return;}uint32_t e=g->matrix_command(g->palette[g->palette_index],op,a);if(e)error(g,e);return;
 }
 if(g->palette_mode&&(op==114||op==115)){error(g,op==114?0x504:0x503);return;}
 float v[16];int fixed=op==37||op==42||op==44||op==48||op==60||op==71||op==72||op==75||op==83||op==84||op==87||op==88||op==93||op==97||op==98||op==101||op==103||op==105||op==108||op==113||op==118||op==122||op==131||op==135||op==138;
 for(int i=0;i<16;i++)v[i]=val(a[i],fixed);
 switch(op){
 case 0x10002:break;
 case 144:if(a[0]>=32)error(g,0x501);else g->palette_index=a[0];break;
 case 151:glGetFloatv(0xba6,g->palette[g->palette_index]);break;
 case 148:case 150:{if(a[0]<1||a[0]>4||(int32_t)a[2]<0){error(g,0x501);break;}if(op==148?a[1]!=0x1401:(a[1]!=0x1406&&a[1]!=0x140c)){error(g,0x500);break;}Array*p=op==148?&g->matrix_indices:&g->weights;uint32_t enabled=p->enabled;*p=(Array){a[0],a[1],a[2],a[3],enabled};break;}
 case 147:memset(g->query,0,sizeof(g->query));g->query[0]=0x31514750;g->query[1]=a[0];g->query[2]=p2d_query_count(a[0]);float result[16]={0};if(a[0]==0xd33)result[0]=MAX_DIM;else if(a[0]==0x84e2)result[0]=(float)g->units;else glGetFloatv(a[0],result);memcpy(g->query+3,result,sizeof(result));g->has_query=1;break;
 case 51:compressed_image(g,a,payload,len);break;
 case 81:case 82:case 83:case 84:glLightModelfv(a[0],v+1);break;
 case 85:case 86:case 87:case 88:glLightfv(a[0],a[1],v+2);break;
 case 0x10000:{if(!len)break;if(len>MAX_PACKET-g->snapshot_bytes||g->snapshot_count==MAX_SNAPSHOTS){error(g,0x505);break;}Snapshot*s=calloc(1,sizeof(*s));if(!s){error(g,0x505);break;}s->bytes=malloc(len);if(!s->bytes){free(s);error(g,0x505);break;}memcpy(s->bytes,payload,len);s->base=a[0];s->len=len;s->next=g->snapshots;g->snapshots=s;g->snapshot_bytes+=len;g->snapshot_count++;break;}
 case 35:if(a[0]<0x84c0||a[0]>=0x84c0+g->units){error(g,0x500);break;}g->server_unit=a[0]-0x84c0;glActiveTexture(a[0]);break;
 case 36:case 37:glAlphaFunc(a[0],v[1]);break;
 case 38:bind_texture_object(g,a[0],a[1]);break;case 39:glBlendFunc(a[0],a[1]);break;case 40:glClear(a[0]);break;
 case 41:case 42:glClearColor(v[0],v[1],v[2],v[3]);break;case 43:case 44:glClearDepth(v[0]);break;case 45:glClearStencil(a[0]);break;
 case 46:if(a[0]<0x84c0||a[0]>=0x84c0+g->units){error(g,0x500);break;}g->client_unit=a[0]-0x84c0;break;
 case 47:case 48:glColor4f(v[0],v[1],v[2],v[3]);break;case 49:glColorMask(!!a[0],!!a[1],!!a[2],!!a[3]);break;
 case 50:case 128:case 139:{Array*p=op==50?&g->color:op==128?&g->texcoord[g->client_unit]:&g->vertex;array_pointer(g,p,a[0],a[1],a[2],a[3],op==50);break;}
 case 55:glCullFace(a[0]);break;case 56:delete_texture_objects(g,a[0],payload);break;
 case 57:glDepthFunc(a[0]);break;case 58:glDepthMask(!!a[0]);break;case 59:case 60:glDepthRange(v[0],v[1]);break;
 case 61:case 65:if(a[0]==0x8840){g->palette_enabled=op==65;break;}if(a[0]==0xb50)g->lighting_enabled=op==65;if(a[0]==0xde1)g->texture_enabled[g->server_unit]=op==65;if(op==65)glEnable(a[0]);else glDisable(a[0]);break;
 case 62:case 66:{if(a[0]==0x8844){g->matrix_indices.enabled=op==66;break;}if(a[0]==0x86ad){g->weights.enabled=op==66;break;}Array*p=a[0]==0x8074?&g->vertex:a[0]==0x8076?&g->color:a[0]==0x8075?&g->normal:a[0]==0x8078?&g->texcoord[g->client_unit]:NULL;if(p)p->enabled=op==66;else error(g,0x500);break;}
 case 63:case 64:draw(g,op,a);break;case 67:glFinish();break;case 68:glFlush();break;
 case 69:case 70:case 71:case 72:if(a[0]==0xb65)glFogf(a[0],fixed?(float)a[1]:v[1]);else if(a[0]==0xb66)glFogfv(a[0],v+1);else glFogf(a[0],v[1]);break;
 case 73:glFrontFace(a[0]);break;case 74:case 75:glFrustum(v[0],v[1],v[2],v[3],v[4],v[5]);break;
 case 76:case 77:break;case 80:glHint(a[0],a[1]);break;case 91:glLoadIdentity();break;
 case 92:case 93:glLoadMatrixf(v);break;case 99:glMatrixMode(a[0]);break;case 100:case 101:glMultMatrixf(v);break;
 case 95:case 96:case 97:case 98:{
  int vector=op==96||op==98,shininess=a[1]==0x1601;
  if(a[0]!=0x408||(!shininess&&(!vector||!(a[1]==0x1200||a[1]==0x1201||a[1]==0x1202||a[1]==0x1600||a[1]==0x1602)))){error(g,0x500);break;}
  if(shininess&&!(v[2]>=0.f&&v[2]<=128.f)){error(g,0x501);break;}
  glMaterialfv(a[0],a[1],v+2);break;
 }
 case 102:case 103:glMultiTexCoord4f(a[0],v[1],v[2],v[3],v[4]);break;case 104:case 105:glNormal3f(v[0],v[1],v[2]);break;
 case 106:array_pointer(g,&g->normal,3,a[0],a[1],a[2],0);break;
 case 107:case 108:glOrtho(v[0],v[1],v[2],v[3],v[4],v[5]);break;case 112:case 113:glPolygonOffset(v[0],v[1]);break;case 114:glPopMatrix();break;case 115:glPushMatrix();break;
 case 116:break;case 117:case 118:glRotatef(v[0],v[1],v[2],v[3]);break;case 121:case 122:glScalef(v[0],v[1],v[2]);break;
 case 123:glScissor(a[0],a[1],a[2],a[3]);break;case 124:glShadeModel(a[0]);break;
 case 129:case 130:case 131:if(a[1]==0x2201)glTexEnvfv(a[0],a[1],v+2);else glTexEnvf(a[0],a[1],op==131?(float)a[2]:v[2]);break;
 case 133:texture_image(g,a,payload,len);break;
 case 134:case 135:texture_parameter(g,a[0],a[1],op==135?(float)a[2]:v[2]);break;
 case 137:case 138:glTranslatef(v[0],v[1],v[2]);break;case 140:glViewport(a[0],a[1],a[2],a[3]);break;
 default:error(g,0x502);
 }
}
int p2d_gles_execute(void*opaque,const unsigned char*request,size_t n,const unsigned char**response,size_t*response_size){
 if(!response||!response_size)return -1;*response=NULL;*response_size=0;if(!opaque||!request||n<24||n>MAX_PACKET)return -1;
 GPU*g=opaque;int compact=word(request)==P2D_REQUEST_COMPACT;if(compact&&!g->compact)return -1;
 uint32_t seq=word(request+4);int valid=parse(request,n);if(!eglMakeCurrent(g->display,g->surface,g->surface,g->context))return -2;
 if(valid){uint32_t target=word(request+8)&&word(request+24)==0x10002?word(request+28):0;if(target){if(g->available_units<4){fprintf(stderr,"Siege target protocol needs4 texture units; driver reports%u\n",g->available_units);return -2;}g->units=4;}g->siege_id=target;}
 if(valid&&!resize(g,word(request+12),word(request+16)))return -2;
#ifdef P2D_GPU_PROBE_STATS
 uint64_t start=now();
#endif
 g->error=0;g->has_query=0;
 if(!valid)error(g,0x502);
 else if(word(request+8)==1&&word(request+24)==P2D_COMPACT_PROBE){
  /* Old hosts do not stamp this argument: advertise only end-to-end support. */
  if(word(request+28)==P2D_COMPACT_HOST){g->compact=1;memset(g->query,0,sizeof(g->query));g->query[0]=0x31514750;g->query[1]=P2D_COMPACT_PROBE;g->query[2]=1;g->query[3]=P2D_COMPACT_CAPABILITY;g->has_query=1;}
 }
 else{size_t off=24;for(uint32_t i=0;i<word(request+8);i++){uint32_t a[16];memcpy(a,request+off+4,64);uint32_t len=word(request+off+68);state(g,word(request+off),a,request+off+72,len);off+=72+((len+3u)&~3u);}}
#ifdef P2D_GPU_PROBE_STATS
 g->submit_ns+=now()-start;
#endif
 gl_success(g);
 if(!compact){
#ifdef P2D_GPU_PROBE_STATS
 start=now();
#endif
#ifdef P2D_ANDROID_PROBE
 glReadPixels(0,0,g->width,g->height,0x1908,0x1401,g->raw);
#else
 glReadPixels(0,0,g->width,g->height,0x1907,0x8363,g->raw);
#endif
#ifdef P2D_GPU_PROBE_STATS
 g->readback_ns+=now()-start;
#endif
 if(glGetError())return -2;
#ifdef P2D_GPU_PROBE_STATS
 start=now();
#endif
 size_t row=(size_t)g->width*2;
 for(uint32_t y=0;y<g->height;y++){
#ifdef P2D_ANDROID_PROBE
  const unsigned char*src=g->raw+(size_t)(g->height-1-y)*g->width*4;uint16_t*dst=(uint16_t*)(g->response+24+y*row);
  for(uint32_t x=0;x<g->width;x++)dst[x]=(uint16_t)(((src[x*4]>>3)<<11)|((src[x*4+1]>>2)<<5)|(src[x*4+2]>>3));
#else
  memcpy(g->response+24+y*row,g->raw+(g->height-1-y)*row,row);
#endif
 }
#ifdef P2D_GPU_PROBE_STATS
 g->convert_ns+=now()-start;
#endif
 }
 size_t base=compact?24:g->capacity;
 uint32_t header[]={compact?P2D_REPLY_COMPACT:P2D_REPLY_FULL,seq,g->width,g->height,(uint32_t)(base-24),g->error};memcpy(g->response,header,24);if(g->has_query)memcpy(g->response+base,g->query,76);*response=g->response;*response_size=base+(g->has_query?76:0);COUNT(g,batches);return valid?0:1;
}
