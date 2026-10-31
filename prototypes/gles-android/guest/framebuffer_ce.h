#ifndef P2D_FRAMEBUFFER_CE_H
#define P2D_FRAMEBUFFER_CE_H
#include "framebuffer.h"
typedef void *P2DFBHandle;
typedef unsigned short P2DFBWchar;
typedef struct {P2DFBI32 left,top,right,bottom;} P2DFBRect;
typedef struct {P2DFBI32 x,y;} P2DFBPoint;
/* Windows CE DEVMODEW public ABI (not desktop DEVMODEW). */
typedef struct {
    P2DFBWchar name[32];unsigned short spec,driver,size,extra;P2DFBU32 fields;
    short printer[13];P2DFBWchar form[32];unsigned short log_pixels;
    P2DFBU32 bpp,width,height,flags,frequency,orientation;
} P2DFBMode;
typedef struct {
    void *base,*allocation_base;P2DFBU32 allocation_protect,region_size,state,protect,type;
} P2DFBMemory;
typedef struct {
    P2DFBHandle (*get_dc)(P2DFBHandle);
    int (*release_dc)(P2DFBHandle,P2DFBHandle);
    P2DFBHandle (*foreground)(void);
    int (*visible)(P2DFBHandle);
    int (*client_rect)(P2DFBHandle,P2DFBRect*);
    int (*client_to_screen)(P2DFBHandle,P2DFBPoint*);
    int (*metrics)(int);
    int (*clip_box)(P2DFBHandle,P2DFBRect*);
    int (*escape)(P2DFBHandle,int,int,const void*,int,void*);
    int (*enum_mode)(const P2DFBWchar*,P2DFBU32,P2DFBMode*);
    P2DFBI32 (*test_mode)(const P2DFBWchar*,P2DFBMode*,P2DFBHandle,P2DFBU32,void*);
    P2DFBU32 (*query_memory)(const void*,P2DFBMemory*,P2DFBU32);
} P2DFBOps;
/* No window/framebuffer/DC pointer is retained between frames. Return zero
 * without pixel writes when any capability, visibility or mapping check fails. */
int p2d_fb_present(const P2DFBOps*,P2DFBHandle,const void*,P2DFBU32,
                  P2DFBU32 width,P2DFBU32 height);
#endif
