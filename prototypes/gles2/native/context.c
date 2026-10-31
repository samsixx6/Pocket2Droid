#include "state.h"
extern EP eglGetDisplay(EP),eglCreateContext(EP,EP,EP,const int*),eglCreatePbufferSurface(EP,EP,const int*);
extern int eglInitialize(EP,int*,int*),eglBindAPI(unsigned),eglChooseConfig(EP,const int*,EP*,int,int*),eglMakeCurrent(EP,EP,EP,EP),eglDestroyContext(EP,EP),eglDestroySurface(EP,EP),eglTerminate(EP);
extern void *eglGetProcAddress(const char*);
#define G(ret,n,...) ret(*g2##n)(__VA_ARGS__);
#include "gl_procs.inc"
#undef G
void p2d2_error(Native*g,uint32_t e){if(!g->glerror)g->glerror=e;}
int p2d2_current(Native*g){return eglBindAPI(0x30a0)&&eglMakeCurrent(g->display,g->surface,g->surface,g->context);}
int p2d2_resize(Native*g,uint32_t w,uint32_t h){
 if(g->width==w&&g->height==h)return p2d2_current(g);
 if(!w||!h||w>2048||h>2048)return 0;
 size_t pixels=(size_t)w*h;uint8_t *response=calloc(1,24+pixels*2+76),*raw=malloc(pixels*4);
 if(!response||!raw){free(response);free(raw);return 0;}
 int attrs[]={0x3057,(int)w,0x3056,(int)h,0x3038};EP next=eglCreatePbufferSurface(g->display,g->config,attrs);
 if(!next||!eglMakeCurrent(g->display,next,next,g->context)){if(next)eglDestroySurface(g->display,next);free(response);free(raw);return 0;}
 if(g->surface)eglDestroySurface(g->display,g->surface);free(g->response);free(g->raw);g->surface=next;g->response=response;g->raw=raw;g->response_size=24+pixels*2;g->width=w;g->height=h;
 g2Viewport(0,0,w,h);return 1;
}
int p2d2_context_init(Native*g){
 g->ferrari_legacy_state=!!(g->compat_profile&P2D2_PROFILE_LEGACY_STATE);
 g->display=eglGetDisplay(0);if(!g->display||!eglInitialize(g->display,0,0)||!eglBindAPI(0x30a0))return 0;
 int attrs[]={0x3033,1,0x3040,4,0x3024,8,0x3023,8,0x3022,8,0x3021,8,0x3025,24,0x3026,8,0x3038},n=0;
 if(!eglChooseConfig(g->display,attrs,&g->config,1,&n)||!n)return 0;
 int ctx[]={0x3098,2,0x3038};g->context=eglCreateContext(g->display,g->config,0,ctx);if(!g->context)return 0;
#define G(ret,name,...) *(void**)(&g2##name)=eglGetProcAddress("gl"#name);if(!g2##name)return 0;
#include "gl_procs.inc"
#undef G
 g->unpack=4;for(unsigned j=0;j<16;j++){g->attr[j].size=4;g->attr[j].type=0x1406;}
 if(!p2d2_resize(g,16,16))return 0;
 g2ClearColor(0,0,0,0);g2ClearDepthf(1);g2Clear(0x4500);while(g2GetError()){}return 1;
}
void p2d2_context_close(Native*g){
 if(g->display){eglMakeCurrent(g->display,0,0,0);if(g->surface)eglDestroySurface(g->display,g->surface);if(g->context)eglDestroyContext(g->display,g->context);/* EGL display is process-shared: do not terminate another renderer's display. */}
 for(unsigned i=0;i<1024;i++)free(g->buffers[i].data);for(unsigned i=0;i<16;i++)free(g->attr[i].snapshot);
 if(g->codec)dlclose(g->codec);free(g->response);free(g->raw);free(g->result);
}
void p2d2_readback(Native*g){
 int bound=0,pack=4;g2GetIntegerv(0x8ca6,&bound);g2GetIntegerv(0xd05,&pack);if(g->present!=2)g2BindFramebuffer(0x8d40,0);g2PixelStorei(0xd05,1);g2ReadPixels(0,0,g->width,g->height,0x1908,0x1401,g->raw);g2PixelStorei(0xd05,pack);g2BindFramebuffer(0x8d40,bound);
 uint16_t *out=(void*)(g->response+24);
 for(uint32_t y=0;y<g->height;y++)for(uint32_t x=0;x<g->width;x++){uint8_t*p=g->raw+((size_t)(g->height-1-y)*g->width+x)*4;*out++=(uint16_t)((p[0]>>3)<<11|(p[1]>>2)<<5|(p[2]>>3));}
}
