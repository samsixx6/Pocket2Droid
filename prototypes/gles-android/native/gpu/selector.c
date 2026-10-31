#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../libp2d_gles.h"
#include "../../guest/compact.h"

typedef void *(*Create)(void);
typedef void (*Destroy)(void *);
typedef int (*Execute)(void *,const uint8_t *,size_t,const uint8_t **,size_t *);
typedef struct {
    void *library,*backend;
    Destroy destroy;
    Execute execute;
    const uint8_t *previous;
    size_t previous_size;
    int poisoned,api,force_cpu,probed;
} Selector;

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* The common transport probe carries no graphics command and must not bind
   the stream to GLES1 before its first real operation. */
static int common_probe(const uint8_t *r,size_t n) {
    if(n!=96||u32(r)!=P2D_REQUEST_FULL||u32(r+4)!=1||u32(r+8)!=1||
       u32(r+20)!=96||u32(r+24)!=P2D_COMPACT_PROBE||u32(r+92)||
       (u32(r+28)!=0&&u32(r+28)!=P2D_COMPACT_HOST))return 0;
    for(unsigned i=1;i<16;i++)if(u32(r+28+i*4))return 0;
    return 1;
}
static int compact_backend(Selector *s) {
    uint32_t (*capability)(void)=(uint32_t (*)(void))dlsym(s->library,"p2d_gles_reply_capability");
    return capability&&capability()==P2D_COMPACT_CAPABILITY;
}
static int overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!an||!bn)return 0;
    if(x>UINTPTR_MAX-an||y>UINTPTR_MAX-bn)return 1;
    return x<y+bn&&y<x+an;
}
static int load(Selector *s,const char *name) {
    Dl_info own;
    char path[PATH_MAX],resolved[PATH_MAX];
    if(!dladdr((void *)p2d_gles_create,&own)||!own.dli_fname||!realpath(own.dli_fname,resolved))return 0;
    char *slash=strrchr(resolved,'/');
    if(!slash)return 0;
    *slash=0;
    int n=snprintf(path,sizeof(path),"%s/%s",resolved,name);
    if(n<0||(size_t)n>=sizeof(path))return 0;
    void *lib=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if(!lib)return 0;
    Create create=(Create)dlsym(lib,"p2d_gles_create");
    Destroy destroy=(Destroy)dlsym(lib,"p2d_gles_destroy");
    Execute execute=(Execute)dlsym(lib,"p2d_gles_execute");
    if(!create||!destroy||!execute||create==p2d_gles_create){dlclose(lib);return 0;}
    void *backend=create();
    if(!backend){dlclose(lib);return 0;}
    s->library=lib;s->backend=backend;s->destroy=destroy;s->execute=execute;
    return 1;
}
static void unload(Selector *s) {
    if(s->backend&&s->destroy)s->destroy(s->backend);
    if(s->library)dlclose(s->library);
    s->library=s->backend=NULL;s->destroy=NULL;s->execute=NULL;
    s->previous=NULL;s->previous_size=0;
}
uint32_t p2d_gles_reply_capability(void) { return P2D_COMPACT_CAPABILITY; }
void *p2d_gles_create(void) {
    if(getenv("POCKETHLE_DUMP_TEXTURES"))return NULL;
    Selector *s=calloc(1,sizeof(*s));
    if(!s)return NULL;
    const char *mode=getenv("P2D_GLES_BACKEND");
    int force_cpu=mode&&!strcmp(mode,"cpu");
    s->force_cpu=force_cpu;
    if(!force_cpu&&load(s,"libp2d_gles_gpu.so")){
        const char *(*renderer)(void *)=(const char *(*)(void *))dlsym(s->library,"p2d_gpu_renderer");
        const char *name=renderer?renderer(s->backend):NULL;
        fprintf(stderr,"Pocket2Droid GLES: GPU backend%s%s\n",name?" / ":"",name?name:"");
        return s;
    }
    if(load(s,"libp2d_gles_cpu.so")){
        fprintf(stderr,"Pocket2Droid GLES: CPU software backend (%s)\n",force_cpu?"selected":"GPU initialization unavailable");
        return s;
    }
    fprintf(stderr,"Pocket2Droid GLES: renderer initialization failed\n");
    free(s);return NULL;
}
void p2d_gles_destroy(void *context) {
    Selector *s=context;
    if(!s)return;
    unload(s);
    free(s);
}
int p2d_gles_execute(void *context,const uint8_t *request,size_t request_size,
                     const uint8_t **response,size_t *response_size) {
    if(!response||!response_size||(uintptr_t)response%_Alignof(void *)||
       (uintptr_t)response_size%_Alignof(size_t)||
       overlaps(response,sizeof(*response),response_size,sizeof(*response_size)))return -1;
    Selector *s=context;
    int valid_context=s&&!((uintptr_t)s%_Alignof(Selector));
    /* Caller must supply live accessible storage. Reject known aliasing before
       writing output slots, so an invalid call cannot mutate input or state. */
    if(overlaps(response,sizeof(*response),request,request_size)||
       overlaps(response_size,sizeof(*response_size),request,request_size)||
       overlaps(response,sizeof(*response),s,valid_context?sizeof(*s):0)||
       overlaps(response_size,sizeof(*response_size),s,valid_context?sizeof(*s):0))return -1;
    if(valid_context&&(overlaps(response,sizeof(*response),s->previous,s->previous_size)||
       overlaps(response_size,sizeof(*response_size),s->previous,s->previous_size)))return -1;
    *response=NULL;*response_size=0;
    if(!valid_context||!request||request_size<24||request_size>16777216||
       (uintptr_t)request>UINTPTR_MAX-request_size||
       overlaps(request,request_size,s,sizeof(*s))||
       overlaps(request,request_size,s->previous,s->previous_size))return -1;
    if(s->poisoned)return -2;
    int compact=u32(request)==P2D_REQUEST_COMPACT;
    int probe=!s->api&&common_probe(request,request_size);
    if(compact&&!s->probed)return -1;
    /* Both protocols keep the legacy envelope. GLES2 commands occupy their
       own range, and a stream may choose its API only before real commands.
       An empty initial GLES1 batch therefore does not preclude GLES2. */
    if(!probe&&request_size>=96&&(u32(request)==P2D_REQUEST_FULL||u32(request)==P2D_REQUEST_COMPACT)&&u32(request+8)&&
       u32(request+20)==request_size){
        int api=(u32(request+24)&0xffff0000u)==0x20000u?2:1;
        if(s->api&&s->api!=api){s->poisoned=1;return -2;}
        if(!s->api&&api==2){
            /* Destroy GLES1 before creating GLES2: the GLES1 backend owns its
               EGL display lifetime. Never retain a context it may terminate. */
            unload(s);
            if(s->force_cpu||!load(s,"libp2d_gles2.so")){
                s->poisoned=1;
                fprintf(stderr,"Pocket2Droid GLES2: GPU preferred and a working GLES2 driver are required\n");
                return -2;
            }
            if(s->probed&&!compact_backend(s)){s->poisoned=1;return -2;}
            fprintf(stderr,"Pocket2Droid GLES: GLES2 GPU backend\n");
        }
        s->api=api;
    }
    uint32_t seq=u32(request+4);
    const uint8_t *out=NULL;size_t size=0;
    int status=s->execute(s->backend,request,request_size,&out,&size);
    s->previous=NULL;s->previous_size=0;
    int query=out&&size>=100&&size==100+(size_t)u32(out+16)&&
       u32(out+24+u32(out+16))==0x31514750&&
       u32(out+32+u32(out+16))>=1&&u32(out+32+u32(out+16))<=16;
    if(status<0||status>1||!out||size<24||size>8388708||
       u32(out)!=(compact?P2D_REPLY_COMPACT:P2D_REPLY_FULL)||u32(out+4)!=seq||
       !u32(out+8)||u32(out+8)>2048||!u32(out+12)||u32(out+12)>2048||
       u32(out+16)!=(compact?0:(uint64_t)u32(out+8)*u32(out+12)*2)||(!query&&size!=24+(size_t)u32(out+16))){
        s->poisoned=1;
        fprintf(stderr,"Pocket2Droid GLES: backend failed; close the ROM before changing renderer\n");
        return -2;
    }
    if(probe&&query&&u32(out+28+u32(out+16))==P2D_COMPACT_PROBE&&
       u32(out+32+u32(out+16))==1&&u32(out+36+u32(out+16))==P2D_COMPACT_CAPABILITY)
        s->probed=1;
    s->previous=out;s->previous_size=size;
    *response=out;*response_size=size;return status;
}
