#ifndef P2D2_STATE_H
#define P2D2_STATE_H
#define _GNU_SOURCE
#include "../protocol.h"
#include "../api_generated.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dlfcn.h>
typedef void *EP;
#define G(ret,n,...) extern ret(*g2##n)(__VA_ARGS__);
#include "gl_procs.inc"
#undef G
typedef struct {uint32_t name,size;uint8_t *data;} Buffer;
typedef struct {uint32_t size,type,normalized,stride,pointer,buffer,enabled;uint8_t *snapshot;size_t snapshot_size;} Attribute;
typedef struct {uint32_t name,used,generate;} LegacyTexture;
typedef struct {
 EP display,config,context,surface;
 uint32_t width,height,array_buffer,element_buffer,unpack,glerror,has_reply,reply_tag,reply_offset,present;
 Attribute attr[16];Buffer buffers[1024];size_t buffer_bytes;
 uint32_t compat_profile,compat_locked,ferrari_legacy_state;LegacyTexture legacy_textures[1024];
 uint8_t *response,*raw,*result;size_t response_size,result_size;
 void *codec;int(*decode)(uint32_t,uint32_t,uint32_t,const uint8_t*,size_t,uint8_t*,size_t);
} Native;
void p2d2_error(Native*,uint32_t);
int p2d2_current(Native*);
int p2d2_resize(Native*,uint32_t,uint32_t);
int p2d2_context_init(Native*);
void p2d2_context_close(Native*);
void p2d2_readback(Native*);
void p2d2_dispatch(Native*,const P2D2Record*);
void p2d2_result(Native*,uint32_t,const void*,size_t);
void p2d2_texture(Native*,const P2D2Record*);
int p2d2_legacy_parameter(Native*,uint32_t,uint32_t,float);
void p2d2_legacy_forget(Native*,uint32_t);
Buffer *p2d2_buffer(Native*,uint32_t,int);
static inline float p2d2_float(uint32_t x){float v;memcpy(&v,&x,4);return v;}
#endif
