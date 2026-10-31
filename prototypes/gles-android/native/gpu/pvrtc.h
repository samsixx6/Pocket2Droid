#ifndef P2D_PVRTC_H
#define P2D_PVRTC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 0 success, 1 invalid input, 2 allocation failure. Invalid inputs do not write. */
int p2d_pvrtc_decode(uint32_t format,uint32_t width,uint32_t height,const void *data,size_t bytes,unsigned char *rgba,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
