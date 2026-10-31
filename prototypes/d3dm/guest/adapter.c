#define INITGUID
#include "internal.h"
static const GUID iid_unknown={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static P2DDevice *active_device;
static const IDirect3DMobileVtbl factory_vtable;
static const IDirect3DMobileDeviceVtbl device_vtable;

static int same_iid(REFIID a,REFIID b){
 if(!a||!b)return 0;
 const BYTE *x=(const BYTE*)a,*y=(const BYTE*)b;
 for(UINT i=0;i<16;i++)if(x[i]!=y[i])return 0;
 return 1;
}
static ULONG f_AddRef(IDirect3DMobile *self){return ++((P2DFactory*)self)->refs;}
static ULONG f_Release(IDirect3DMobile *self){
 P2DFactory *f=(void*)self;ULONG refs=--f->refs;if(!refs)p2d_free(f);return refs;
}
static HRESULT f_QueryInterface(IDirect3DMobile *self,REFIID iid,void **out){
 if(!out)return E_POINTER;*out=0;
 if(!same_iid(iid,&iid_unknown)&&!same_iid(iid,&IID_IDirect3DMobile))return E_NOINTERFACE;
 *out=self;f_AddRef(self);return S_OK;
}
static HRESULT f_GetAdapterIdentifier(IDirect3DMobile *self,UINT adapter,ULONG flags,D3DMADAPTER_IDENTIFIER *out){
 (void)self;if(!out||adapter||flags)return D3DMERR_INVALIDCALL;p2d_zero(out,sizeof(*out));
 const char *driver="p2dD3DM.dll",*description="Pocket2Droid virtual Direct3D Mobile adapter";
 for(UINT i=0;driver[i];i++)out->Driver[i]=driver[i];for(UINT i=0;description[i];i++)out->Description[i]=description[i];
 out->DriverVersion.LowPart=0x00010000;out->DeviceIdentifier=(GUID){0x50324433,0x4d47,0x100,{0x8e,0xc1,0x5a,0x28,0x70,0xd2,0x41,0x63}};return S_OK;
}
static UINT f_GetAdapterCount(IDirect3DMobile *self){(void)self;return 1;}
static UINT f_GetAdapterModeCount(IDirect3DMobile *self,UINT adapter){(void)self;return adapter?0:1;}
static HRESULT f_GetAdapterDisplayMode(IDirect3DMobile *self,UINT adapter,D3DMDISPLAYMODE *out){
 (void)self;if(!out||adapter)return D3DMERR_INVALIDCALL;
 p2d_gles_dimensions(&out->Width,&out->Height);out->RefreshRate=0;out->Format=D3DMFMT_R5G6B5;return S_OK;
}
static HRESULT f_EnumAdapterModes(IDirect3DMobile *self,UINT adapter,UINT mode,D3DMDISPLAYMODE *out){
 if(mode)return D3DMERR_INVALIDCALL;return f_GetAdapterDisplayMode(self,adapter,out);
}
static HRESULT f_CheckDeviceType(IDirect3DMobile *self,UINT adapter,D3DMDEVTYPE type,D3DMFORMAT display,D3DMFORMAT back,BOOL windowed){
 (void)self;(void)windowed;
 if(adapter||type!=D3DMDEVTYPE_DEFAULT)return D3DMERR_INVALIDCALL;
 return display==D3DMFMT_R5G6B5&&back==D3DMFMT_R5G6B5?S_OK:D3DMERR_NOTAVAILABLE;
}
static HRESULT f_GetDeviceCaps(IDirect3DMobile *self,UINT adapter,D3DMDEVTYPE type,D3DMCAPS *out){
 (void)self;if(!out||adapter||type!=D3DMDEVTYPE_DEFAULT)return D3DMERR_INVALIDCALL;
 p2d_zero(out,sizeof(*out));out->DeviceType=type;out->MaxBackBuffer=1;
 out->DevCaps=D3DMDEVCAPS_TEXTURE|D3DMDEVCAPS_SPECULAR|D3DMDEVCAPS_NATIVEFLOAT;
 out->SurfaceCaps=D3DMSURFCAPS_VIDBACKBUFFER|D3DMSURFCAPS_VIDDEPTHBUFFER|D3DMSURFCAPS_SYSVERTEXBUFFER|D3DMSURFCAPS_VIDVERTEXBUFFER|D3DMSURFCAPS_SYSINDEXBUFFER|D3DMSURFCAPS_VIDINDEXBUFFER|D3DMSURFCAPS_SYSTEXTURE|D3DMSURFCAPS_VIDTEXTURE|D3DMSURFCAPS_SYSIMAGESURFACE|D3DMSURFCAPS_MANAGEDPOOL|D3DMSURFCAPS_LOCKRENDERTARGET|D3DMSURFCAPS_LOCKTEXTURE;
 out->PrimitiveMiscCaps=0x7f;out->ZCmpCaps=out->AlphaCmpCaps=0xff;out->SrcBlendCaps=0x7ff;out->DestBlendCaps=0x3ff;out->BlendOpCaps=7;
 out->ShadeCaps=D3DMPSHADECAPS_COLORGOURAUDRGB|D3DMPSHADECAPS_SPECULARGOURAUDRGB|D3DMPSHADECAPS_ALPHAGOURAUDBLEND;
 out->RasterCaps=D3DMPRASTERCAPS_COLORPERSPECTIVE;
 out->TextureCaps=D3DMPTEXTURECAPS_PERSPECTIVE|D3DMPTEXTURECAPS_POW2|D3DMPTEXTURECAPS_ALPHA|D3DMPTEXTURECAPS_MIPMAP;
 out->TextureFilterCaps=0x03030300;out->TextureAddressCaps=0x17;
 out->MaxTextureWidth=out->MaxTextureHeight=out->MaxTextureAspectRatio=512;out->MaxTextureRepeat=32768;out->MaxAnisotropy=1;out->MaxVertexW=0x7f7fffff;
 out->TextureOpCaps=0x39efffe;out->MaxTextureBlendStages=out->MaxSimultaneousTextures=2;
 out->VertexProcessingCaps=D3DMVTXPCAPS_MATERIALSOURCE|D3DMVTXPCAPS_DIRECTIONALLIGHTS|D3DMVTXPCAPS_POSITIONALLIGHTS|D3DMVTXPCAPS_LOCALVIEWER;
 out->MaxActiveLights=8;out->MaxPrimitiveCount=out->MaxVertexIndex=65535;return S_OK;
}
static BOOL color_format(D3DMFORMAT f){return (f>=1&&f<=7)||f==10||f==13;}
static HRESULT f_CheckDeviceFormat(IDirect3DMobile *self,UINT adapter,D3DMDEVTYPE type,D3DMFORMAT display,ULONG usage,D3DMRESOURCETYPE resource,D3DMFORMAT format){
 (void)self;if(adapter||type!=D3DMDEVTYPE_DEFAULT)return D3DMERR_INVALIDCALL;
 if(display!=D3DMFMT_R5G6B5)return D3DMERR_NOTAVAILABLE;
 if(resource==D3DMRTYPE_TEXTURE)return !(usage&~D3DMUSAGE_LOCKABLE)&&color_format(format)?S_OK:D3DMERR_NOTAVAILABLE;
 if(resource==D3DMRTYPE_VERTEXBUFFER)return !(usage&~(D3DMUSAGE_DYNAMIC|D3DMUSAGE_WRITEONLY))&&format==D3DMFMT_VERTEXDATA?S_OK:D3DMERR_NOTAVAILABLE;
 if(resource==D3DMRTYPE_INDEXBUFFER)return !(usage&~(D3DMUSAGE_DYNAMIC|D3DMUSAGE_WRITEONLY))&&format==D3DMFMT_INDEX16?S_OK:D3DMERR_NOTAVAILABLE;
 return D3DMERR_NOTAVAILABLE;
}
static HRESULT f_CheckDepthStencilMatch(IDirect3DMobile *self,UINT adapter,D3DMDEVTYPE type,D3DMFORMAT display,D3DMFORMAT target,D3DMFORMAT depth){
 (void)self;if(adapter||type!=D3DMDEVTYPE_DEFAULT)return D3DMERR_INVALIDCALL;
 return display==D3DMFMT_R5G6B5&&target==D3DMFMT_R5G6B5&&depth==D3DMFMT_D16?S_OK:D3DMERR_NOTAVAILABLE;
}
static HRESULT d_ValidateDevice(IDirect3DMobileDevice *self,DWORD *passes){
 if(!passes)return D3DMERR_INVALIDCALL;*passes=0;P2DDevice *d=(void*)self;
 if(d->lost)return D3DMERR_DEVICELOST;
 if(d->state.render[22]||!d->state.render[36]||d->state.render[21])return D3DMERR_NOTAVAILABLE;
 if(d->textures[0]&&d->textures[0]==d->textures[1])for(UINT i=8;i<=18;i++)if(d->state.texture[0][i]!=d->state.texture[1][i])return D3DMERR_NOTAVAILABLE;
 *passes=1;return S_OK;
}
static HRESULT d_GetCreationParameters(IDirect3DMobileDevice *self,D3DMDEVICE_CREATION_PARAMETERS *p){
 if(!p)return D3DMERR_INVALIDCALL;p2d_zero(p,sizeof(*p));p->DeviceType=D3DMDEVTYPE_DEFAULT;p->hFocusWindow=((P2DDevice*)self)->window;return S_OK;
}
ULONG p2d_device_add_ref(P2DDevice *d){d->refs++;return d->refs+d->resource_pins;}
static ULONG d_AddRef(IDirect3DMobileDevice *self){return p2d_device_add_ref((void*)self);}
void p2d_device_maybe_destroy(P2DDevice *d){
 if(d->refs||d->resource_pins||d->releasing)return;
 d->releasing++;p2d_resource_unbind_all(d);d->releasing--;
 if(d->resource_count)return;
 p2d_state_discard(d);p2d_draw_discard(d);p2d_gles_close(d->gles);active_device=0;f_Release(&d->owner->iface);p2d_free(d);
}
static ULONG d_Release(IDirect3DMobileDevice *self){
 P2DDevice *d=(void*)self;d->refs--;ULONG refs=d->refs+d->resource_pins;
 if(!refs)p2d_device_maybe_destroy(d);return refs;
}
static HRESULT d_QueryInterface(IDirect3DMobileDevice *self,REFIID iid,void **out){
 if(!out)return E_POINTER;*out=0;
 if(!same_iid(iid,&iid_unknown)&&!same_iid(iid,&IID_IDirect3DMobileDevice))return E_NOINTERFACE;
 *out=self;d_AddRef(self);return S_OK;
}
static HRESULT d_GetDirect3D(IDirect3DMobileDevice *self,IDirect3DMobile **out){
 if(!out)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)self;
 *out=&d->owner->iface;f_AddRef(*out);return S_OK;
}
static HRESULT d_GetDeviceCaps(IDirect3DMobileDevice *self,D3DMCAPS *out){
 P2DDevice *d=(void*)self;return f_GetDeviceCaps(&d->owner->iface,0,D3DMDEVTYPE_DEFAULT,out);
}
static HRESULT d_GetDisplayMode(IDirect3DMobileDevice *self,D3DMDISPLAYMODE *out){
 if(!out)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)self;
 out->Width=d->pp.BackBufferWidth;out->Height=d->pp.BackBufferHeight;out->RefreshRate=0;out->Format=d->pp.BackBufferFormat;return S_OK;
}
static HRESULT d_TestCooperativeLevel(IDirect3DMobileDevice *self){return ((P2DDevice*)self)->lost?D3DMERR_DEVICELOST:S_OK;}
static HRESULT d_BeginScene(IDirect3DMobileDevice *self){
 P2DDevice *d=(void*)self;if(d->lost)return D3DMERR_DEVICELOST;if(d->scene)return D3DMERR_INVALIDCALL;d->scene=1;return S_OK;
}
static HRESULT d_EndScene(IDirect3DMobileDevice *self){
 P2DDevice *d=(void*)self;if(!d->scene)return D3DMERR_INVALIDCALL;d->scene=0;return S_OK;
}
static HRESULT d_Clear(IDirect3DMobileDevice *self,DWORD count,const RECT *rects,DWORD flags,D3DMCOLOR color,float depth,DWORD stencil){
 P2DDevice *d=(void*)self;if(d->lost)return D3DMERR_DEVICELOST;if(p2d_backbuffer_locked(d))return D3DMERR_INVALIDCALL;union{float f;DWORD u;}z={depth};(void)stencil;
 if(!flags||(flags&~7u)||((flags&D3DMCLEAR_ZBUFFER)&&(z.u&0x7fffffffu)>0x3f800000u)||
   ((flags&D3DMCLEAR_ZBUFFER)&&(z.u>>31)&&(z.u&0x7fffffffu)))return D3DMERR_INVALIDCALL;
 if(count||rects||(flags&D3DMCLEAR_STENCIL))return D3DMERR_NOTAVAILABLE;
 if((flags&D3DMCLEAR_ZBUFFER)&&!d->pp.EnableAutoDepthStencil)return D3DMERR_INVALIDCALL;
 return p2d_gles_clear(d->gles,&d->state.viewport,flags,color,depth);
}
static HRESULT d_Present(IDirect3DMobileDevice *self,const RECT *source,const RECT *dest,HWND window,const RGNDATA *dirty){
 P2DDevice *d=(void*)self;if(d->lost)return D3DMERR_DEVICELOST;if(d->scene)return D3DMERR_INVALIDCALL;
 if(p2d_backbuffer_locked(d))return D3DMERR_INVALIDCALL;
 if(source||dest||window||dirty)return D3DMERR_NOTAVAILABLE;
 return p2d_gles_present(d->gles);
}
static HRESULT parameters(HWND window,D3DMPRESENT_PARAMETERS *pp,D3DMPRESENT_PARAMETERS *out){
 if(!pp)return D3DMERR_INVALIDCALL;
 if(pp->BackBufferFormat!=D3DMFMT_R5G6B5||pp->BackBufferCount>1||
    pp->MultiSampleType!=D3DMMULTISAMPLE_NONE||pp->SwapEffect!=D3DMSWAPEFFECT_DISCARD||
    (pp->EnableAutoDepthStencil&&pp->AutoDepthStencilFormat!=D3DMFMT_D16)||
    (pp->Flags&~1u)||pp->FullScreen_PresentationInterval)return D3DMERR_NOTAVAILABLE;
 *out=*pp;D3DMPRESENT_PARAMETERS value=*out;UINT w,h;if(!p2d_gles_window_dimensions(window,&w,&h))return D3DMERR_INVALIDCALL;
 if(!value.BackBufferWidth)value.BackBufferWidth=w;
 if(!value.BackBufferHeight)value.BackBufferHeight=h;
 if(!value.BackBufferCount)value.BackBufferCount=1;
 if(value.BackBufferWidth!=w||value.BackBufferHeight!=h)return D3DMERR_NOTAVAILABLE;
 if(!value.BackBufferWidth||!value.BackBufferHeight||value.BackBufferWidth>2048||value.BackBufferHeight>2048)return D3DMERR_INVALIDCALL;
 *out=value;return S_OK;
}
static HRESULT f_CreateDevice(IDirect3DMobile *self,UINT adapter,D3DMDEVTYPE type,HWND window,ULONG flags,D3DMPRESENT_PARAMETERS *pp,IDirect3DMobileDevice **out){
 if(!out)return D3DMERR_INVALIDCALL;*out=0;
 if(!pp||!window||adapter||type!=D3DMDEVTYPE_DEFAULT)return D3DMERR_INVALIDCALL;
 if(flags||active_device)return D3DMERR_NOTAVAILABLE;
 D3DMPRESENT_PARAMETERS value;HRESULT valid=parameters(window,pp,&value);if(FAILED(valid))return valid;
 P2DDevice *d=p2d_alloc(sizeof(*d));if(!d)return E_OUTOFMEMORY;p2d_zero(d,sizeof(*d));
 HRESULT result=p2d_gles_open(window,value.BackBufferWidth,value.BackBufferHeight,&d->gles);
 if(FAILED(result)){p2d_free(d);return result;}
 d->iface.lpVtbl=&device_vtable;d->refs=1;d->owner=(void*)self;d->pp=value;d->window=window;p2d_state_init(d);
 f_AddRef(self);active_device=d;*out=&d->iface;*pp=value;return S_OK;
}
HRESULT d_Reset(IDirect3DMobileDevice *self,D3DMPRESENT_PARAMETERS *pp){
 P2DDevice *d=(void*)self;if(d->scene)return D3DMERR_INVALIDCALL;
 D3DMPRESENT_PARAMETERS value;HRESULT h=parameters(d->window,pp,&value);if(FAILED(h))return h;
 for(P2DResource *r=d->resources;r;r=r->next){
  if((r->desc.pool==D3DMPOOL_VIDEOMEM&&r->refs)||r->locked)return D3DMERR_INVALIDCALL;
  for(UINT i=0;i<r->desc.levels;i++)if(r->mips[i].locked)return D3DMERR_INVALIDCALL;
 }
 p2d_resource_unbind_all(d);p2d_resource_reset_gpu(d);
 p2d_gles_close(d->gles);d->gles=0;d->lost=1;
 h=p2d_gles_open(d->window,value.BackBufferWidth,value.BackBufferHeight,&d->gles);
 if(FAILED(h))return h;
 d->pp=value;*pp=value;d->lost=0;p2d_state_init(d);p2d_draw_discard(d);return S_OK;
}
DWORD WINAPI P2DD3DMAdapterVersion(void){return 0x00010000u;}
IDirect3DMobile *WINAPI Direct3DMobileCreate(UINT sdk){
 if(sdk!=120)return 0;P2DFactory *f=p2d_alloc(sizeof(*f));if(!f)return 0;
 f->iface.lpVtbl=&factory_vtable;f->refs=1;return &f->iface;
}
#if defined(__arm__)
int _start(void *module,unsigned reason,void *reserved){(void)module;(void)reserved;if(reason==0)p2d_diagnostic_close();return 1;}
#endif
#include "vtables.inc"
