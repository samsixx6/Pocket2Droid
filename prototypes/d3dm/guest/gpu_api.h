#ifndef P2D_D3DM_GPU_API_H
#define P2D_D3DM_GPU_API_H
#include "internal.h"
#include "numbers.h"
typedef struct P2DGL {
#define G(ret,name,...) ret (*name)(__VA_ARGS__);
#include "../../gles2/native/gl_procs.inc"
#undef G
} P2DGL;
struct P2DGles {
 void *display,*surface,*context;
 P2DGL gl;
 UINT (*read565)(BYTE *,UINT,UINT,UINT);
 UINT width,height,framebuffer,colorbuffer,depthbuffer;
 UINT program,blit_program,index_scratch,blit_buffer,blit_texture;
 INT uniform[64];
 UINT applied_generation,applied_fvf;
 BOOL applied_blit;
 char error[512];
};
HRESULT p2d_pipeline_init(P2DGles *);
void p2d_pipeline_discard(P2DGles *);
HRESULT p2d_gles_upload_buffer(P2DGles *,P2DResource *,UINT target);
#endif
