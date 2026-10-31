#ifndef P2D_D3DM_INTERNAL_H
#define P2D_D3DM_INTERNAL_H
#include "d3dm.h"
typedef struct P2DGles P2DGles;
typedef struct P2DResource P2DResource;
typedef struct P2DMip {
 BYTE *data;
 UINT width,height,pitch;
 BOOL locked;
 DWORD lock_flags;
 P2DResource *surface;
} P2DMip;
typedef struct P2DResourceDesc {
 D3DMRESOURCETYPE type;
 D3DMFORMAT format;
 D3DMPOOL pool;
 UINT size,width,height,levels;
 DWORD usage,fvf;
} P2DResourceDesc;
typedef struct P2DLight {
 struct P2DLight *next;
 DWORD index;
 D3DMLIGHT value;
 D3DMFORMAT format;
 BOOL enabled;
} P2DLight;
typedef struct P2DState {
 DWORD render[D3DM_MAXRENDERSTATES];
 DWORD texture[2][D3DMTSS_MAXTEXTURESTATES];
 D3DMMATRIX matrix[5];
 D3DMFORMAT matrix_format[5];
 D3DMVIEWPORT viewport;
 D3DMMATERIAL material;
 D3DMFORMAT material_format;
 P2DLight *lights;
 UINT generation;
} P2DState;
typedef struct P2DStage {
 DWORD color_op,alpha_op,color_arg[3],alpha_arg[3],result;
} P2DStage;
typedef struct P2DGLState {
 INT viewport[4];
 UINT src_blend,dest_blend,depth_func,alpha_func;
 DWORD depth_correction[16];
 P2DStage stage[2];
} P2DGLState;
typedef struct P2DAttribute {
 UINT size,type,offset;
 BOOL normalized;
} P2DAttribute;
typedef struct P2DDraw {
 UINT mode,count,vertices,position_type,base_offset;
 BOOL indexed,color_bgra;
 const BYTE *colors;
 const WORD *indices;
 P2DResource *vertex_buffer,*index_buffer;
 UINT index_offset;
 P2DAttribute attribute[6];
 BOOL temporary_indices;
} P2DDraw;
typedef struct P2DFactory {
 IDirect3DMobile iface;
 ULONG refs;
} P2DFactory;
typedef struct P2DDevice {
 IDirect3DMobileDevice iface;
 ULONG refs;
 P2DFactory *owner;
 P2DGles *gles;
 D3DMPRESENT_PARAMETERS pp;
 HWND window;
 BOOL scene;
 UINT resource_count,resource_pins,releasing;
 P2DResource *resources,*stream,*indices,*textures[2];
 UINT stride;
 P2DState state;
 P2DGLState gl_state;
 BOOL lost;
 WORD *index_scratch;
 UINT index_capacity;
} P2DDevice;
struct P2DResource {
 const void *lpVtbl;
 ULONG refs,bound_refs;
 P2DDevice *owner;
 P2DResource *next,*parent;
 P2DResourceDesc desc;
 UINT priority,lod,level,gl_name;
 BOOL gpu_dirty,copy_dirty,locked;
 DWORD lock_flags;
 BYTE *data;
 P2DMip mips[12];
};
HRESULT p2d_failure(const char *,HRESULT);
void p2d_diagnostic_close(void);
void *p2d_alloc(UINT bytes);
void p2d_free(void *memory);
void p2d_gles_dimensions(UINT *width,UINT *height);
HRESULT p2d_gles_open(HWND window,UINT width,UINT height,P2DGles **out);
void p2d_gles_close(P2DGles *gles);
HRESULT p2d_gles_clear(P2DGles *gles,const D3DMVIEWPORT *,DWORD flags,D3DMCOLOR color,float depth);
HRESULT p2d_gles_present(P2DGles *gles);
HRESULT p2d_gles_read_backbuffer(P2DGles *,BYTE *,UINT,UINT,UINT);
HRESULT p2d_gles_write_backbuffer(P2DGles *,const BYTE *,UINT,UINT,UINT);
BOOL p2d_backbuffer_locked(P2DDevice *);
void p2d_resource_reset_gpu(P2DDevice *);
HRESULT d_Reset(IDirect3DMobileDevice *,D3DMPRESENT_PARAMETERS *);
HRESULT d_GetBackBuffer(IDirect3DMobileDevice *,UINT,D3DMBACKBUFFER_TYPE,IDirect3DMobileSurface **);
HRESULT d_GetRenderTarget(IDirect3DMobileDevice *,IDirect3DMobileSurface **);
HRESULT d_CreateImageSurface(IDirect3DMobileDevice *,UINT,UINT,D3DMFORMAT,IDirect3DMobileSurface **);
HRESULT d_CopyRects(IDirect3DMobileDevice *,IDirect3DMobileSurface *,const RECT *,UINT,IDirect3DMobileSurface *,const POINT *);
HRESULT p2d_gles_draw(P2DGles *,P2DDevice *,const P2DDraw *);
BOOL p2d_gles_window_dimensions(HWND,UINT *,UINT *);
void p2d_state_init(P2DDevice *);
void p2d_state_discard(P2DDevice *);
void p2d_draw_discard(P2DDevice *);
HRESULT p2d_apply_state(P2DDevice *);
HRESULT p2d_draw(P2DDevice *,D3DMPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT,BOOL);
HRESULT d_SetRenderState(IDirect3DMobileDevice *,D3DMRENDERSTATETYPE,DWORD);
HRESULT d_GetRenderState(IDirect3DMobileDevice *,D3DMRENDERSTATETYPE,DWORD *);
HRESULT d_SetTransform(IDirect3DMobileDevice *,D3DMTRANSFORMSTATETYPE,const D3DMMATRIX *,D3DMFORMAT);
HRESULT d_GetTransform(IDirect3DMobileDevice *,D3DMTRANSFORMSTATETYPE,D3DMMATRIX *,D3DMFORMAT *);
HRESULT d_SetViewport(IDirect3DMobileDevice *,const D3DMVIEWPORT *);
HRESULT d_GetViewport(IDirect3DMobileDevice *,D3DMVIEWPORT *);
HRESULT d_SetTextureStageState(IDirect3DMobileDevice *,DWORD,D3DMTEXTURESTAGESTATETYPE,DWORD);
HRESULT d_GetTextureStageState(IDirect3DMobileDevice *,DWORD,D3DMTEXTURESTAGESTATETYPE,DWORD *);
HRESULT d_SetMaterial(IDirect3DMobileDevice *,const D3DMMATERIAL *,D3DMFORMAT);
HRESULT d_GetMaterial(IDirect3DMobileDevice *,D3DMMATERIAL *,D3DMFORMAT *);
HRESULT d_SetLight(IDirect3DMobileDevice *,DWORD,const D3DMLIGHT *,D3DMFORMAT);
HRESULT d_GetLight(IDirect3DMobileDevice *,DWORD,D3DMLIGHT *,D3DMFORMAT *);
HRESULT d_LightEnable(IDirect3DMobileDevice *,DWORD,BOOL);
HRESULT d_GetLightEnable(IDirect3DMobileDevice *,DWORD,BOOL *);
HRESULT d_DrawPrimitive(IDirect3DMobileDevice *,D3DMPRIMITIVETYPE,UINT,UINT);
HRESULT d_DrawIndexedPrimitive(IDirect3DMobileDevice *,D3DMPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
HRESULT p2d_gles_upload_texture(P2DGles *,P2DResource *,UINT *new_name);
void p2d_gles_delete_buffer(P2DGles *,UINT name);
void p2d_gles_delete_texture(P2DGles *,UINT name);
ULONG p2d_device_add_ref(P2DDevice *);
void p2d_device_maybe_destroy(P2DDevice *);
void p2d_resource_unbind_all(P2DDevice *);
HRESULT p2d_resource_create(P2DDevice *,const P2DResourceDesc *,P2DResource **);
HRESULT p2d_resource_upload(P2DResource *);
HRESULT p2d_texture_rgba(P2DResource *,UINT level,BYTE *,UINT bytes);
HRESULT p2d_fvf_stride(DWORD fvf,UINT *stride);
P2DResource *p2d_resource_owned(P2DDevice *,const void *,D3DMRESOURCETYPE);
HRESULT d_CreateVertexBuffer(IDirect3DMobileDevice *,UINT,DWORD,DWORD,D3DMPOOL,IDirect3DMobileVertexBuffer **);
HRESULT d_CreateIndexBuffer(IDirect3DMobileDevice *,UINT,DWORD,D3DMFORMAT,D3DMPOOL,IDirect3DMobileIndexBuffer **);
HRESULT d_CreateTexture(IDirect3DMobileDevice *,UINT,UINT,UINT,DWORD,D3DMFORMAT,D3DMPOOL,IDirect3DMobileTexture **);
HRESULT d_SetStreamSource(IDirect3DMobileDevice *,UINT,IDirect3DMobileVertexBuffer *,UINT);
HRESULT d_GetStreamSource(IDirect3DMobileDevice *,UINT,IDirect3DMobileVertexBuffer **,UINT *);
HRESULT d_SetIndices(IDirect3DMobileDevice *,IDirect3DMobileIndexBuffer *);
HRESULT d_GetIndices(IDirect3DMobileDevice *,IDirect3DMobileIndexBuffer **);
HRESULT d_SetTexture(IDirect3DMobileDevice *,DWORD,IDirect3DMobileBaseTexture *);
HRESULT d_GetTexture(IDirect3DMobileDevice *,DWORD,IDirect3DMobileBaseTexture **);
HRESULT d_UpdateTexture(IDirect3DMobileDevice *,IDirect3DMobileBaseTexture *,IDirect3DMobileBaseTexture *);
DWORD WINAPI P2DD3DMAdapterVersion(void);
static void p2d_zero(void *out,UINT bytes){BYTE *p=out;while(bytes--)*p++=0;}
#endif
