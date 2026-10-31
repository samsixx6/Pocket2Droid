/* CE binding to the existing GLES2 shared-memory GPU bridge. */
#include "gpu_api.h"
extern HANDLE LoadLibraryW(const WCHAR *);
extern void *GetProcAddressW(HANDLE,const WCHAR *);
static HANDLE core,gles_module;
static void *(*local_alloc)(UINT,UINT);
static HANDLE (*local_free)(HANDLE);
static INT (*metrics)(INT);
static BOOL (*client_rect)(HWND,RECT*);
typedef void *EGL;
#define PROC(type,name,args) static type (*name)args
PROC(EGL,eglGetDisplay,(void*));
PROC(INT,eglInitialize,(EGL,INT*,INT*));
PROC(INT,eglChooseConfig,(EGL,const INT*,EGL*,INT,INT*));
PROC(EGL,eglCreateWindowSurface,(EGL,EGL,HWND,const INT*));
PROC(EGL,eglCreateContext,(EGL,EGL,EGL,const INT*));
PROC(INT,eglMakeCurrent,(EGL,EGL,EGL,EGL));
PROC(INT,eglDestroyContext,(EGL,EGL));
PROC(INT,eglDestroySurface,(EGL,EGL));
PROC(INT,eglTerminate,(EGL));
PROC(INT,eglSwapBuffers,(EGL,EGL));
static int init_core(void){
 if(local_alloc&&local_free&&metrics&&client_rect)return 1;
 core=LoadLibraryW((const WCHAR*)L"coredll.dll");if(!core)return 0;
 local_alloc=GetProcAddressW(core,(const WCHAR*)L"LocalAlloc");
 local_free=GetProcAddressW(core,(const WCHAR*)L"LocalFree");
 metrics=GetProcAddressW(core,(const WCHAR*)L"GetSystemMetrics");
 client_rect=GetProcAddressW(core,(const WCHAR*)L"GetClientRect");
 return local_alloc&&local_free&&metrics&&client_rect;
}
void *p2d_alloc(UINT bytes){return init_core()?local_alloc(0x40,bytes):0;}
void p2d_free(void *p){if(p&&local_free)local_free(p);}
void p2d_gles_dimensions(UINT *w,UINT *h){*w=init_core()?(UINT)metrics(0):0;*h=metrics?(UINT)metrics(1):0;}
BOOL p2d_gles_window_dimensions(HWND window,UINT *w,UINT *h){
 RECT rect;if(!init_core()||!client_rect(window,&rect)||rect.right<=rect.left||rect.bottom<=rect.top)return 0;
 *w=(UINT)(rect.right-rect.left);*h=(UINT)(rect.bottom-rect.top);return 1;
}
void __aeabi_memcpy4(void *out,const void *in,UINT n){BYTE *d=out;const BYTE *s=in;while(n--)*d++=*s++;}
void __aeabi_memcpy(void *out,const void *in,UINT n){__aeabi_memcpy4(out,in,n);}
void __aeabi_memclr4(void *out,UINT n){p2d_zero(out,n);}
static int load_gles(void){
 if(gles_module)return 1;
 HANDLE module=LoadLibraryW((const WCHAR*)L"\\Storage Card\\Game\\p2dGLES2.dll");if(!module)return 0;
#define LOAD(name) name=GetProcAddressW(module,(const WCHAR*)L## #name);if(!name)return 0
 LOAD(eglGetDisplay);LOAD(eglInitialize);LOAD(eglChooseConfig);LOAD(eglCreateWindowSurface);
 LOAD(eglCreateContext);LOAD(eglMakeCurrent);LOAD(eglDestroyContext);LOAD(eglDestroySurface);
 LOAD(eglTerminate);LOAD(eglSwapBuffers);
#undef LOAD
 gles_module=module;return 1;
}
void p2d_gles_close(P2DGles *g){
 if(!g)return;
 if(g->display){
  if(g->context){p2d_pipeline_discard(g);eglMakeCurrent(g->display,0,0,0);eglDestroyContext(g->display,g->context);}
  if(g->surface)eglDestroySurface(g->display,g->surface);eglTerminate(g->display);
 }
 p2d_free(g);
}
HRESULT p2d_gles_open(HWND window,UINT width,UINT height,P2DGles **out){
 *out=0;if(!load_gles())return D3DMERR_NOTAVAILABLE;
 P2DGles *g=p2d_alloc(sizeof(*g));if(!g)return E_OUTOFMEMORY;p2d_zero(g,sizeof(*g));g->width=width;g->height=height;
 EGL config=0;INT major=0,minor=0,count=0;const INT attrs[]={0x3038},context[]={0x3098,2,0x3038};
 g->display=eglGetDisplay(0);if(!g->display||!eglInitialize(g->display,&major,&minor))goto fail;
 if(!eglChooseConfig(g->display,attrs,&config,1,&count)||count!=1)goto fail;
 g->surface=eglCreateWindowSurface(g->display,config,window,attrs);if(!g->surface)goto fail;
 g->context=eglCreateContext(g->display,config,0,context);if(!g->context)goto fail;
 if(!eglMakeCurrent(g->display,g->surface,g->surface,g->context))goto fail;
#define W_(x) L##x
#define G(ret,name,...) g->gl.name=GetProcAddressW(gles_module,(const WCHAR*)W_("gl" #name));if(!g->gl.name&&(void*)&g->gl.name!=(void*)&g->gl.ReadPixels)goto fail;
#include "../../gles2/native/gl_procs.inc"
#undef G
#undef W_
 g->read565=GetProcAddressW(gles_module,(const WCHAR*)L"p2dD3DMReadBackBuffer565");if(!g->read565)goto fail;
 if(FAILED(p2d_pipeline_init(g)))goto fail;
 *out=g;return S_OK;
fail:p2d_gles_close(g);return D3DMERR_NOTAVAILABLE;
}
HRESULT p2d_gles_clear(P2DGles *g,const D3DMVIEWPORT *vp,DWORD flags,D3DMCOLOR c,float depth){
 UINT mask=0;g->gl.BindFramebuffer(0x8d40,g->framebuffer);g->gl.Enable(0xc11);
 g->gl.Scissor(vp->X,g->height-vp->Y-vp->Height,vp->Width,vp->Height);
 if(flags&D3DMCLEAR_TARGET){
  g->gl.ColorMask(1,1,1,1);
  g->gl.ClearColor(p2d_word_float(p2d_u8_float_bits((c>>16)&255)),p2d_word_float(p2d_u8_float_bits((c>>8)&255)),p2d_word_float(p2d_u8_float_bits(c&255)),p2d_word_float(p2d_u8_float_bits(c>>24)));mask|=0x4000;
 }
 if(flags&D3DMCLEAR_ZBUFFER){g->gl.DepthMask(1);g->gl.ClearDepthf(depth);mask|=0x100;}
 g->gl.Clear(mask);g->applied_generation=0;return S_OK;
}
HRESULT p2d_gles_present(P2DGles *g){
 g->gl.BindFramebuffer(0x8d40,g->framebuffer);
 if(!g->read565(0,0,g->width,g->height))return D3DMERR_DRIVERINTERNALERROR;
 return g->gl.GetError()?D3DMERR_DRIVERINTERNALERROR:S_OK;
}
void p2d_gles_delete_texture(P2DGles *g,UINT name){if(name)g->gl.DeleteTextures(1,&name);}
void p2d_gles_delete_buffer(P2DGles *g,UINT name){if(name)g->gl.DeleteBuffers(1,&name);}

HRESULT p2d_gles_upload_texture(P2DGles *g,P2DResource *r,UINT *out){
 *out=0;UINT base=r->desc.pool==D3DMPOOL_MANAGED?r->lod:0;for(UINT i=base;i<r->desc.levels;i++)if(r->mips[i].width*r->mips[i].height>0x3fff00u/4)return D3DMERR_NOTAVAILABLE;
 UINT candidate=0;g->gl.GenTextures(1,&candidate);if(!candidate)return E_OUTOFMEMORY;
 g->gl.BindTexture(0xde1,candidate);g->gl.PixelStorei(0xcf5,4);g->gl.TexParameteri(0xde1,0x2801,0x2600);g->gl.TexParameteri(0xde1,0x2800,0x2600);
 HRESULT result=S_OK;
 for(UINT i=base;i<r->desc.levels;i++){
  P2DMip *m=&r->mips[i];UINT bytes=m->width*m->height*4;BYTE *rgba=p2d_alloc(bytes);
  if(!rgba){result=E_OUTOFMEMORY;break;}
  result=p2d_texture_rgba(r,i,rgba,bytes);
  if(SUCCEEDED(result))g->gl.TexImage2D(0xde1,i-base,0x1908,m->width,m->height,0,0x1908,0x1401,rgba);
  p2d_free(rgba);if(FAILED(result))break;
 }
 UINT error=g->gl.GetError();if(error&&SUCCEEDED(result))result=error==0x505?E_OUTOFMEMORY:D3DMERR_DRIVERINTERNALERROR;
 if(FAILED(result)){g->gl.DeleteTextures(1,&candidate);return result;}
 *out=candidate;return S_OK;
}

UINT __aeabi_uidiv(UINT n,UINT d){UINT q=0,r=0;if(!d)return 0;for(INT i=31;i>=0;i--){UINT hi=r>>31;r=(r<<1)|((n>>i)&1);if(hi||r>=d){r-=d;q|=1u<<i;}}return q;}

extern HANDLE CreateFileW(const WCHAR*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
extern BOOL WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);
extern BOOL CloseHandle(HANDLE);
static HANDLE diagnostic_log;
HRESULT p2d_failure(const char *name,HRESULT result){
 if(!diagnostic_log)diagnostic_log=CreateFileW((const WCHAR*)L"\\Storage Card\\d3dm-api-errors.txt",0x40000000,3,0,2,0x80,0);
 if(diagnostic_log&&diagnostic_log!=(HANDLE)-1){
  char line[128];UINT n=0,done=0;while(name[n]&&n<110){line[n]=name[n];n++;}line[n++]='=';
  for(UINT i=0;i<8;i++)line[n++]="0123456789abcdef"[((DWORD)result>>(28-4*i))&15];line[n++]='\n';
  WriteFile(diagnostic_log,line,n,&done,0);
 }
 return result;
}
void p2d_diagnostic_close(void){if(diagnostic_log&&diagnostic_log!=(HANDLE)-1)CloseHandle(diagnostic_log);diagnostic_log=0;}
