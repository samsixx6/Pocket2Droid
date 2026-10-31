#include "internal.h"
#define MAX_RESOURCE_BYTES (16u*1024u*1024u)
static const IDirect3DMobileVertexBufferVtbl vb_vtable;
static const IDirect3DMobileIndexBufferVtbl ib_vtable;
static const IDirect3DMobileTextureVtbl tex_vtable;
static const IDirect3DMobileSurfaceVtbl surface_vtable;
static const GUID iid_unknown={0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static int iid_equal(REFIID a,REFIID b){
 if(!a||!b)return 0;const BYTE *x=(const BYTE*)a,*y=(const BYTE*)b;
 for(UINT i=0;i<16;i++)if(x[i]!=y[i])return 0;return 1;
}
static UINT color_bytes(D3DMFORMAT f){
 switch(f){
 case D3DMFMT_R8G8B8:return 3;
 case D3DMFMT_A8R8G8B8:case D3DMFMT_X8R8G8B8:return 4;
 case D3DMFMT_R5G6B5:case D3DMFMT_X1R5G5B5:case D3DMFMT_A1R5G5B5:
 case D3DMFMT_A4R4G4B4:case D3DMFMT_X4R4G4B4:return 2;
 case D3DMFMT_A8:return 1;default:return 0;
 }
}
HRESULT p2d_fvf_stride(DWORD fvf,UINT *out){
 UINT position=fvf&7,normal=fvf&0x38,textures=(fvf>>8)&7;
 if(!out||position>3||normal>0x10||normal==0x18||(fvf&0xf800))return D3DMERR_INVALIDCALL;
 if(textures>2)return D3DMERR_NOTAVAILABLE;
 if(position>=2&&normal)return D3DMERR_INVALIDCALL;
 UINT n=position>=2?16:12;if(normal)n+=12;
 if(fvf&D3DMFVF_DIFFUSE)n+=4;if(fvf&D3DMFVF_SPECULAR)n+=4;
 for(UINT i=0;i<textures;i++){
  UINT count=(fvf>>(16+2*i))&3,format=(fvf>>(24+2*i))&3;
  if(count==2||format>1)return D3DMERR_INVALIDCALL;
  n+=(count==3?1:count==1?3:2)*4;
 }
 *out=n;return S_OK;
}
static ULONG res_addref(P2DResource *r){
 if(!r->refs)r->owner->resource_pins++;r->refs++;return r->refs+r->bound_refs;
}
static void res_destroy(P2DResource *r);
static ULONG res_release(P2DResource *r){
 P2DDevice *d=r->owner;r->refs--;if(!r->refs)d->resource_pins--;
 ULONG refs=r->refs+r->bound_refs;
 if(!refs)res_destroy(r);else p2d_device_maybe_destroy(d);
 return refs;
}
static void internal_hold(P2DResource *r){if(r)r->bound_refs++;}
static void internal_drop(P2DResource *r){if(r){r->bound_refs--;if(!r->bound_refs&&!r->refs)res_destroy(r);}}
static void res_destroy(P2DResource *r){
 P2DDevice *d=r->owner;d->releasing++;
 P2DResource **link=&d->resources;while(*link&&*link!=r)link=&(*link)->next;
 if(*link==r)*link=r->next;d->resource_count--;
 if(r->gl_name){if(r->desc.type==D3DMRTYPE_TEXTURE)p2d_gles_delete_texture(d->gles,r->gl_name);else p2d_gles_delete_buffer(d->gles,r->gl_name);}
 if(r->parent){r->parent->mips[r->level].surface=0;internal_drop(r->parent);}
 else{p2d_free(r->data);for(UINT i=0;i<r->desc.levels;i++)p2d_free(r->mips[i].data);}
 p2d_free(r);d->releasing--;p2d_device_maybe_destroy(d);
}
static HRESULT res_query(P2DResource *r,REFIID iid,void **out){
 if(!out)return E_POINTER;*out=0;
 REFIID own=r->desc.type==D3DMRTYPE_VERTEXBUFFER?&IID_IDirect3DMobileVertexBuffer:
  r->desc.type==D3DMRTYPE_INDEXBUFFER?&IID_IDirect3DMobileIndexBuffer:
  r->desc.type==D3DMRTYPE_TEXTURE?&IID_IDirect3DMobileTexture:&IID_IDirect3DMobileSurface;
 BOOL supported=iid_equal(iid,&iid_unknown)||iid_equal(iid,own);
 if(r->desc.type==D3DMRTYPE_TEXTURE||r->desc.type==D3DMRTYPE_VERTEXBUFFER||r->desc.type==D3DMRTYPE_INDEXBUFFER)supported|=iid_equal(iid,&IID_IDirect3DMobileResource);
 if(r->desc.type==D3DMRTYPE_TEXTURE)supported|=iid_equal(iid,&IID_IDirect3DMobileBaseTexture);
 if(!supported)return E_NOINTERFACE;*out=r;res_addref(r);return S_OK;
}
static HRESULT res_device(P2DResource *r,IDirect3DMobileDevice **out){
 if(!out)return D3DMERR_INVALIDCALL;*out=&r->owner->iface;p2d_device_add_ref(r->owner);return S_OK;
}
P2DResource *p2d_resource_owned(P2DDevice *d,const void *object,D3DMRESOURCETYPE type){
 for(P2DResource *r=d->resources;r;r=r->next)if((const void*)r==object&&r->desc.type==type)return r;
 return 0;
}
HRESULT p2d_resource_create(P2DDevice *d,const P2DResourceDesc *requested,P2DResource **out){
 if(!out)return D3DMERR_INVALIDCALL;*out=0;
 if(!requested)return D3DMERR_INVALIDCALL;
 P2DResourceDesc desc=*requested;UINT stride=0;
 if(desc.pool>D3DMPOOL_MANAGED||(desc.usage&~D3DMUSAGE_VALID))return D3DMERR_INVALIDCALL;
 if(desc.type==D3DMRTYPE_VERTEXBUFFER||desc.type==D3DMRTYPE_INDEXBUFFER){
  if(!desc.size)return D3DMERR_INVALIDCALL;if(desc.size>MAX_RESOURCE_BYTES)return E_OUTOFMEMORY;
  if(desc.type==D3DMRTYPE_VERTEXBUFFER){HRESULT h=p2d_fvf_stride(desc.fvf,&stride);if(FAILED(h))return h;}
  else if(desc.format!=D3DMFMT_INDEX16)return D3DMERR_NOTAVAILABLE;
  if(desc.type==D3DMRTYPE_INDEXBUFFER&&(desc.size&1))return D3DMERR_INVALIDCALL;
 }else if(desc.type==D3DMRTYPE_TEXTURE||desc.type==D3DMRTYPE_SURFACE||desc.type==D3DMRTYPE_BACKBUFFER){
  UINT bpp=color_bytes(desc.format);
  if(!desc.width||!desc.height||desc.width>2048||desc.height>2048)return D3DMERR_INVALIDCALL;
  if(!bpp)return D3DMERR_NOTAVAILABLE;
  if(desc.type==D3DMRTYPE_TEXTURE&&((desc.width&(desc.width-1))||(desc.height&(desc.height-1))))return D3DMERR_NOTAVAILABLE;
  if(desc.usage&~D3DMUSAGE_LOCKABLE)return D3DMERR_NOTAVAILABLE;
  UINT max=1,w=desc.width,h=desc.height;while(w>1||h>1){w=w>1?w/2:1;h=h>1?h/2:1;max++;}
  if(desc.type!=D3DMRTYPE_TEXTURE)desc.levels=1;
  if(desc.levels>max)return D3DMERR_INVALIDCALL;if(!desc.levels)desc.levels=max;
 }else return D3DMERR_NOTAVAILABLE;
 P2DResource *r=p2d_alloc(sizeof(*r));if(!r)return E_OUTOFMEMORY;p2d_zero(r,sizeof(*r));r->desc=desc;r->owner=d;
 if(desc.type==D3DMRTYPE_TEXTURE||desc.type==D3DMRTYPE_SURFACE||desc.type==D3DMRTYPE_BACKBUFFER){
  r->lpVtbl=desc.type==D3DMRTYPE_TEXTURE?(const void*)&tex_vtable:(const void*)&surface_vtable;UINT w=desc.width,h=desc.height,total=0,bpp=color_bytes(desc.format);
  for(UINT i=0;i<desc.levels;i++){
   P2DMip *m=&r->mips[i];m->width=w;m->height=h;m->pitch=(w*bpp+3)&~3u;
   UINT bytes=m->pitch*h;if(bytes>MAX_RESOURCE_BYTES-total)goto oom;total+=bytes;
   m->data=p2d_alloc(bytes);if(!m->data)goto oom;p2d_zero(m->data,bytes);
   w=w>1?w/2:1;h=h>1?h/2:1;
  }
  r->desc.size=total;r->gpu_dirty=r->copy_dirty=1;
 }else{
  r->lpVtbl=desc.type==D3DMRTYPE_VERTEXBUFFER?(const void*)&vb_vtable:(const void*)&ib_vtable;
  r->data=p2d_alloc(desc.size);if(!r->data)goto oom;p2d_zero(r->data,desc.size);
 }
 r->refs=1;r->next=d->resources;d->resources=r;d->resource_count++;d->resource_pins++;*out=r;return S_OK;
oom:for(UINT i=0;i<desc.levels;i++)p2d_free(r->mips[i].data);p2d_free(r->data);p2d_free(r);return E_OUTOFMEMORY;
}
static HRESULT buffer_lock(P2DResource *r,UINT offset,UINT size,void **out,DWORD flags){
 if(!out)return D3DMERR_INVALIDCALL;*out=0;
 if(r->locked)return D3DMERR_ALREADYLOCKED;
 if((flags&~D3DMLOCK_VALID)||(flags&D3DMLOCK_NO_DIRTY_UPDATE)||
  ((flags&(D3DMLOCK_DISCARD|D3DMLOCK_NOOVERWRITE))&&!(r->desc.usage&D3DMUSAGE_DYNAMIC))||
  ((flags&D3DMLOCK_READONLY)&&(flags&D3DMLOCK_DISCARD))||
  ((flags&D3DMLOCK_READONLY)&&(r->desc.usage&D3DMUSAGE_WRITEONLY))||offset>r->desc.size)return D3DMERR_INVALIDCALL;
 if(!size)size=r->desc.size-offset;if(size>r->desc.size-offset)return D3DMERR_INVALIDCALL;
 r->locked=1;r->lock_flags=flags;*out=r->data+offset;return S_OK;
}
static HRESULT buffer_unlock(P2DResource *r){if(!r->locked)return D3DMERR_INVALIDCALL;r->locked=0;if(!(r->lock_flags&D3DMLOCK_READONLY))r->gpu_dirty=1;return S_OK;}
static HRESULT level_desc(P2DResource *r,UINT level,D3DMSURFACE_DESC *out){
 if(!out||level>=r->desc.levels)return D3DMERR_INVALIDCALL;P2DMip *m=&r->mips[level];p2d_zero(out,sizeof(*out));
 out->Format=r->desc.format;out->Type=r->desc.type==D3DMRTYPE_BACKBUFFER?D3DMRTYPE_BACKBUFFER:D3DMRTYPE_SURFACE;out->Usage=r->desc.usage;out->Pool=r->desc.pool;
 out->Width=m->width;out->Height=m->height;out->Size=m->pitch*m->height;return S_OK;
}
static HRESULT texture_lock(P2DResource *r,UINT level,D3DMLOCKED_RECT *out,const RECT *rect,DWORD flags){
 if(!out)return D3DMERR_INVALIDCALL;out->Pitch=0;out->pBits=0;
 if((r->desc.type==D3DMRTYPE_TEXTURE&&!(r->desc.usage&D3DMUSAGE_LOCKABLE))||level>=r->desc.levels||flags&~(D3DMLOCK_READONLY|D3DMLOCK_NO_DIRTY_UPDATE)||(r->desc.pool==D3DMPOOL_VIDEOMEM&&r->desc.type!=D3DMRTYPE_BACKBUFFER))return D3DMERR_INVALIDCALL;
 P2DMip *m=&r->mips[level];if(m->locked)return D3DMERR_ALREADYLOCKED;
 RECT all={0,0,(LONG)m->width,(LONG)m->height};if(!rect)rect=&all;
 if(rect->left<0||rect->top<0||rect->right<=rect->left||rect->bottom<=rect->top||
  (UINT)rect->right>m->width||(UINT)rect->bottom>m->height)return D3DMERR_INVALIDCALL;
 if(r->desc.type==D3DMRTYPE_BACKBUFFER){HRESULT h=p2d_gles_read_backbuffer(r->owner->gles,m->data,m->pitch,m->width,m->height);if(FAILED(h))return h;}
 m->locked=1;m->lock_flags=flags;out->Pitch=m->pitch;
 out->pBits=m->data+(UINT)rect->top*m->pitch+(UINT)rect->left*color_bytes(r->desc.format);return S_OK;
}
static HRESULT texture_unlock(P2DResource *r,UINT level){
 if(level>=r->desc.levels||!r->mips[level].locked)return D3DMERR_INVALIDCALL;P2DMip *m=&r->mips[level];
 m->locked=0;if(!(m->lock_flags&D3DMLOCK_READONLY)){r->gpu_dirty=1;if(!level&&!(m->lock_flags&D3DMLOCK_NO_DIRTY_UPDATE))r->copy_dirty=1;if(r->desc.type==D3DMRTYPE_BACKBUFFER)return p2d_gles_write_backbuffer(r->owner->gles,m->data,m->pitch,m->width,m->height);}return S_OK;
}
HRESULT p2d_texture_rgba(P2DResource *r,UINT level,BYTE *out,UINT bytes){
 if(r->desc.type!=D3DMRTYPE_TEXTURE||level>=r->desc.levels||!out)return D3DMERR_INVALIDCALL;
 P2DMip *m=&r->mips[level];if(bytes<m->width*m->height*4)return D3DMERR_INVALIDCALL;
 UINT bpp=color_bytes(r->desc.format);
 for(UINT y=0;y<m->height;y++)for(UINT x=0;x<m->width;x++){
  const BYTE *p=m->data+y*m->pitch+x*bpp;UINT red=0,green=0,blue=0,alpha=255,v=0;
  if(bpp>=3){blue=p[0];green=p[1];red=p[2];if(r->desc.format==D3DMFMT_A8R8G8B8)alpha=p[3];}
  else if(bpp==1){red=green=blue=255;alpha=p[0];}
  else{v=p[0]|((UINT)p[1]<<8);
   if(r->desc.format==D3DMFMT_R5G6B5){red=(v>>11)&31;green=(v>>5)&63;blue=v&31;red=(red<<3)|(red>>2);green=(green<<2)|(green>>4);blue=(blue<<3)|(blue>>2);}
   else if(r->desc.format==D3DMFMT_A1R5G5B5||r->desc.format==D3DMFMT_X1R5G5B5){red=(v>>10)&31;green=(v>>5)&31;blue=v&31;red=(red<<3)|(red>>2);green=(green<<3)|(green>>2);blue=(blue<<3)|(blue>>2);if(r->desc.format==D3DMFMT_A1R5G5B5)alpha=v&0x8000?255:0;}
   else{red=((v>>8)&15)*17;green=((v>>4)&15)*17;blue=(v&15)*17;if(r->desc.format==D3DMFMT_A4R4G4B4)alpha=((v>>12)&15)*17;}
  }
  *out++=red;*out++=green;*out++=blue;*out++=alpha;
 }
 return S_OK;
}
HRESULT p2d_resource_upload(P2DResource *r){
 if(r->owner->lost||!r->owner->gles)return D3DMERR_DEVICELOST;
 if(r->desc.type!=D3DMRTYPE_TEXTURE)return D3DMERR_INVALIDCALL;
 for(UINT i=0;i<r->desc.levels;i++)if(r->mips[i].locked)return D3DMERR_INVALIDCALL;
 if(!r->gpu_dirty&&r->gl_name)return S_OK;UINT replacement=0;
 HRESULT h=p2d_gles_upload_texture(r->owner->gles,r,&replacement);if(FAILED(h))return h;
 if(!replacement)return D3DMERR_DRIVERINTERNALERROR;
 if(r->gl_name)p2d_gles_delete_texture(r->owner->gles,r->gl_name);
 r->gl_name=replacement;r->gpu_dirty=0;return S_OK;
}
#define COMMON(prefix,Type) \
 static HRESULT prefix##_query(Type *s,REFIID iid,void **o){return res_query((void*)s,iid,o);} \
 static ULONG prefix##_add(Type *s){return res_addref((void*)s);} \
 static ULONG prefix##_release(Type *s){return res_release((void*)s);} \
 static HRESULT prefix##_device(Type *s,IDirect3DMobileDevice **o){return res_device((void*)s,o);} \
 static DWORD prefix##_priority(Type *s,DWORD v){P2DResource *r=(void*)s;DWORD old=r->priority;r->priority=v;return old;} \
 static DWORD prefix##_getpriority(Type *s){return ((P2DResource*)s)->priority;} \
 static void prefix##_preload(Type *s){P2DResource *r=(void*)s;if(r->desc.type==D3DMRTYPE_TEXTURE)p2d_resource_upload(r);} \
 static D3DMRESOURCETYPE prefix##_type(Type *s){return ((P2DResource*)s)->desc.type;}
COMMON(vb,IDirect3DMobileVertexBuffer)
COMMON(ib,IDirect3DMobileIndexBuffer)
COMMON(tex,IDirect3DMobileTexture)
#define BUFFER(prefix,Iface,Desc) \
 static HRESULT prefix##_lock(Iface *s,UINT a,UINT b,void **o,DWORD f){return buffer_lock((void*)s,a,b,o,f);} \
 static HRESULT prefix##_unlock(Iface *s){return buffer_unlock((void*)s);} \
 static HRESULT prefix##_desc(Iface *s,Desc *out){if(!out)return D3DMERR_INVALIDCALL;P2DResource *r=(void*)s;p2d_zero(out,sizeof(*out));out->Format=r->desc.format;out->Type=r->desc.type;out->Usage=r->desc.usage;out->Pool=r->desc.pool;out->Size=r->desc.size;return S_OK;}
BUFFER(vb,IDirect3DMobileVertexBuffer,D3DMVERTEXBUFFER_DESC)
BUFFER(ib,IDirect3DMobileIndexBuffer,D3DMINDEXBUFFER_DESC)
static HRESULT vb_description(IDirect3DMobileVertexBuffer *s,D3DMVERTEXBUFFER_DESC *out){HRESULT h=vb_desc(s,out);if(SUCCEEDED(h))out->FVF=((P2DResource*)s)->desc.fvf;return h;}
#define COMMON_TABLE(p) .QueryInterface=p##_query,.AddRef=p##_add,.Release=p##_release,.GetDevice=p##_device,.SetPriority=p##_priority,.GetPriority=p##_getpriority,.PreLoad=p##_preload,.GetType=p##_type
static const IDirect3DMobileVertexBufferVtbl vb_vtable={COMMON_TABLE(vb),.Lock=vb_lock,.Unlock=vb_unlock,.GetDesc=vb_description};
static const IDirect3DMobileIndexBufferVtbl ib_vtable={COMMON_TABLE(ib),.Lock=ib_lock,.Unlock=ib_unlock,.GetDesc=ib_desc};
static DWORD tex_lod(IDirect3DMobileTexture *s,DWORD value){P2DResource *r=(void*)s;if(r->desc.pool!=D3DMPOOL_MANAGED)return 0;UINT old=r->lod;r->lod=value<r->desc.levels?value:r->desc.levels-1;if(r->lod!=old)r->gpu_dirty=1;return old;}
static DWORD tex_getlod(IDirect3DMobileTexture *s){return ((P2DResource*)s)->lod;}
static DWORD tex_levels(IDirect3DMobileTexture *s){return ((P2DResource*)s)->desc.levels;}
static HRESULT tex_desc(IDirect3DMobileTexture *s,UINT level,D3DMSURFACE_DESC *out){return level_desc((void*)s,level,out);}
static HRESULT tex_lock(IDirect3DMobileTexture *s,UINT level,D3DMLOCKED_RECT *out,const RECT *rect,DWORD flags){return texture_lock((void*)s,level,out,rect,flags);}
static HRESULT tex_unlock(IDirect3DMobileTexture *s,UINT level){return texture_unlock((void*)s,level);}
static HRESULT tex_dirty(IDirect3DMobileTexture *s,const RECT *rect){
 P2DResource *r=(void*)s;if(rect&&(rect->left<0||rect->top<0||rect->right<=rect->left||rect->bottom<=rect->top||(UINT)rect->right>r->desc.width||(UINT)rect->bottom>r->desc.height))return D3DMERR_INVALIDCALL;
 r->copy_dirty=r->gpu_dirty=1;return S_OK;
}
static HRESULT tex_surface(IDirect3DMobileTexture *s,UINT level,IDirect3DMobileSurface **out){
 if(!out)return D3DMERR_INVALIDCALL;*out=0;P2DResource *r=(void*)s;if(level>=r->desc.levels)return D3DMERR_INVALIDCALL;
 P2DResource *surface=r->mips[level].surface;
 if(surface){res_addref(surface);*out=(void*)surface;return S_OK;}
 surface=p2d_alloc(sizeof(*surface));if(!surface)return E_OUTOFMEMORY;p2d_zero(surface,sizeof(*surface));
 surface->lpVtbl=&surface_vtable;surface->refs=1;surface->owner=r->owner;surface->parent=r;surface->level=level;
 surface->desc=r->desc;surface->desc.type=D3DMRTYPE_SURFACE;surface->desc.levels=0;
 P2DDevice *d=r->owner;surface->next=d->resources;d->resources=surface;d->resource_count++;d->resource_pins++;
 internal_hold(r);r->mips[level].surface=surface;*out=(void*)surface;return S_OK;
}
static const IDirect3DMobileTextureVtbl tex_vtable={COMMON_TABLE(tex),.SetLOD=tex_lod,.GetLOD=tex_getlod,.GetLevelCount=tex_levels,.GetLevelDesc=tex_desc,.GetSurfaceLevel=tex_surface,.LockRect=tex_lock,.UnlockRect=tex_unlock,.AddDirtyRect=tex_dirty};
static HRESULT surf_query(IDirect3DMobileSurface *s,REFIID iid,void **out){return res_query((void*)s,iid,out);}
static ULONG surf_add(IDirect3DMobileSurface *s){return res_addref((void*)s);}
static ULONG surf_release(IDirect3DMobileSurface *s){return res_release((void*)s);}
static HRESULT surf_device(IDirect3DMobileSurface *s,IDirect3DMobileDevice **out){return res_device((void*)s,out);}
static HRESULT surf_container(IDirect3DMobileSurface *s,REFIID iid,void **out){P2DResource *r=(void*)s;if(!out)return E_POINTER;*out=0;return r->parent?res_query(r->parent,iid,out):D3DMERR_NOTAVAILABLE;}
static HRESULT surf_desc(IDirect3DMobileSurface *s,D3DMSURFACE_DESC *out){P2DResource *r=(void*)s;return r->parent?level_desc(r->parent,r->level,out):level_desc(r,0,out);}
static HRESULT surf_lock(IDirect3DMobileSurface *s,D3DMLOCKED_RECT *out,const RECT *rect,DWORD flags){P2DResource *r=(void*)s;if(r->desc.type==D3DMRTYPE_BACKBUFFER&&!(r->owner->pp.Flags&1))return D3DMERR_INVALIDCALL;return r->parent?texture_lock(r->parent,r->level,out,rect,flags):texture_lock(r,0,out,rect,flags);}
static HRESULT surf_unlock(IDirect3DMobileSurface *s){P2DResource *r=(void*)s;return r->parent?texture_unlock(r->parent,r->level):texture_unlock(r,0);}
static HRESULT surf_dc(IDirect3DMobileSurface *s,HDC *out){(void)s;if(out)*out=0;return D3DMERR_NOTAVAILABLE;}
static HRESULT surf_releasedc(IDirect3DMobileSurface *s,HDC dc){(void)s;(void)dc;return D3DMERR_NOTAVAILABLE;}
static const IDirect3DMobileSurfaceVtbl surface_vtable={.QueryInterface=surf_query,.AddRef=surf_add,.Release=surf_release,.GetDevice=surf_device,.GetContainer=surf_container,.GetDesc=surf_desc,.LockRect=surf_lock,.UnlockRect=surf_unlock,.GetDC=surf_dc,.ReleaseDC=surf_releasedc};
HRESULT d_CreateVertexBuffer(IDirect3DMobileDevice *s,UINT length,DWORD usage,DWORD fvf,D3DMPOOL pool,IDirect3DMobileVertexBuffer **out){
 P2DResourceDesc desc={.type=D3DMRTYPE_VERTEXBUFFER,.format=D3DMFMT_VERTEXDATA,.pool=pool,.size=length,.usage=usage,.fvf=fvf};return p2d_resource_create((void*)s,&desc,(void*)out);
}
HRESULT d_CreateIndexBuffer(IDirect3DMobileDevice *s,UINT length,DWORD usage,D3DMFORMAT format,D3DMPOOL pool,IDirect3DMobileIndexBuffer **out){
 P2DResourceDesc desc={.type=D3DMRTYPE_INDEXBUFFER,.format=format,.pool=pool,.size=length,.usage=usage};return p2d_resource_create((void*)s,&desc,(void*)out);
}
HRESULT d_CreateTexture(IDirect3DMobileDevice *s,UINT width,UINT height,UINT levels,DWORD usage,D3DMFORMAT format,D3DMPOOL pool,IDirect3DMobileTexture **out){
 P2DResourceDesc desc={.type=D3DMRTYPE_TEXTURE,.format=format,.pool=pool,.width=width,.height=height,.levels=levels,.usage=usage};return p2d_resource_create((void*)s,&desc,(void*)out);
}
static void bind(P2DResource **slot,P2DResource *r){if(*slot==r)return;P2DResource *old=*slot;internal_hold(r);*slot=r;internal_drop(old);}
HRESULT d_SetStreamSource(IDirect3DMobileDevice *s,UINT stream,IDirect3DMobileVertexBuffer *buffer,UINT stride){
 P2DDevice *d=(void*)s;if(stream)return D3DMERR_INVALIDCALL;P2DResource *r=buffer?p2d_resource_owned(d,buffer,D3DMRTYPE_VERTEXBUFFER):0;
 if(buffer&&!r)return D3DMERR_INVALIDCALL;if(r&&(!stride||stride>r->desc.size))return D3DMERR_INVALIDCALL;
 bind(&d->stream,r);d->stride=stride;return S_OK;
}
HRESULT d_GetStreamSource(IDirect3DMobileDevice *s,UINT stream,IDirect3DMobileVertexBuffer **out,UINT *stride){
 if(!out||!stride||stream)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;*out=(void*)d->stream;*stride=d->stride;if(d->stream)res_addref(d->stream);return S_OK;
}
HRESULT d_SetIndices(IDirect3DMobileDevice *s,IDirect3DMobileIndexBuffer *buffer){
 P2DDevice *d=(void*)s;P2DResource *r=buffer?p2d_resource_owned(d,buffer,D3DMRTYPE_INDEXBUFFER):0;if(buffer&&!r)return D3DMERR_INVALIDCALL;bind(&d->indices,r);return S_OK;
}
HRESULT d_GetIndices(IDirect3DMobileDevice *s,IDirect3DMobileIndexBuffer **out){
 if(!out)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;*out=(void*)d->indices;if(d->indices)res_addref(d->indices);return S_OK;
}
HRESULT d_SetTexture(IDirect3DMobileDevice *s,DWORD stage,IDirect3DMobileBaseTexture *texture){
 if(stage>=2)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;P2DResource *r=texture?p2d_resource_owned(d,texture,D3DMRTYPE_TEXTURE):0;
 if(texture&&!r)return D3DMERR_INVALIDCALL;if(d->textures[stage]!=r){bind(&d->textures[stage],r);if(!++d->state.generation)++d->state.generation;}return S_OK;
}
HRESULT d_GetTexture(IDirect3DMobileDevice *s,DWORD stage,IDirect3DMobileBaseTexture **out){
 if(!out||stage>=2)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;*out=(void*)d->textures[stage];if(*out)res_addref((void*)*out);return S_OK;
}
HRESULT d_UpdateTexture(IDirect3DMobileDevice *s,IDirect3DMobileBaseTexture *source,IDirect3DMobileBaseTexture *dest){
 P2DDevice *d=(void*)s;P2DResource *a=p2d_resource_owned(d,source,D3DMRTYPE_TEXTURE),*b=p2d_resource_owned(d,dest,D3DMRTYPE_TEXTURE);
 if(!a||!b||a==b||a->desc.pool!=D3DMPOOL_SYSTEMMEM||a->desc.format!=b->desc.format||a->desc.levels<b->desc.levels)return D3DMERR_INVALIDCALL;
 UINT offset=a->desc.levels-b->desc.levels;
 for(UINT i=0;i<b->desc.levels;i++)if(a->mips[i+offset].width!=b->mips[i].width||a->mips[i+offset].height!=b->mips[i].height||a->mips[i+offset].locked||b->mips[i].locked)return D3DMERR_INVALIDCALL;
 if(!a->copy_dirty)return S_OK;
 for(UINT i=0;i<b->desc.levels;i++){UINT n=b->mips[i].pitch*b->mips[i].height;BYTE *to=b->mips[i].data,*from=a->mips[i+offset].data;while(n--)*to++=*from++;}
 a->copy_dirty=0;b->gpu_dirty=b->copy_dirty=1;return S_OK;
}
void p2d_resource_unbind_all(P2DDevice *d){
 bind(&d->stream,0);bind(&d->indices,0);for(UINT i=0;i<2;i++)bind(&d->textures[i],0);d->stride=0;
}

BOOL p2d_backbuffer_locked(P2DDevice *d){for(P2DResource *r=d->resources;r;r=r->next)if(r->desc.type==D3DMRTYPE_BACKBUFFER&&r->mips[0].locked)return 1;return 0;}
void p2d_resource_reset_gpu(P2DDevice *d){
 for(P2DResource *r=d->resources;r;r=r->next){
  if(r->gl_name){if(r->desc.type==D3DMRTYPE_TEXTURE)p2d_gles_delete_texture(d->gles,r->gl_name);else p2d_gles_delete_buffer(d->gles,r->gl_name);}
  r->gl_name=0;r->gpu_dirty=1;
 }
}
HRESULT d_GetBackBuffer(IDirect3DMobileDevice *s,UINT index,D3DMBACKBUFFER_TYPE type,IDirect3DMobileSurface **out){
 if(!out)return D3DMERR_INVALIDCALL;*out=0;P2DDevice *d=(void*)s;
 if(index||type!=D3DMBACKBUFFER_TYPE_MONO)return D3DMERR_INVALIDCALL;if(d->lost)return D3DMERR_DEVICELOST;
 for(P2DResource *r=d->resources;r;r=r->next)if(r->desc.type==D3DMRTYPE_BACKBUFFER){res_addref(r);*out=(void*)r;return S_OK;}
 P2DResourceDesc desc={.type=D3DMRTYPE_BACKBUFFER,.format=d->pp.BackBufferFormat,.pool=D3DMPOOL_VIDEOMEM,.width=d->pp.BackBufferWidth,.height=d->pp.BackBufferHeight,.levels=1};
 return p2d_resource_create(d,&desc,(void*)out);
}
HRESULT d_GetRenderTarget(IDirect3DMobileDevice *s,IDirect3DMobileSurface **out){return d_GetBackBuffer(s,0,D3DMBACKBUFFER_TYPE_MONO,out);}
HRESULT d_CreateImageSurface(IDirect3DMobileDevice *s,UINT w,UINT h,D3DMFORMAT f,IDirect3DMobileSurface **out){
 P2DResourceDesc desc={.type=D3DMRTYPE_SURFACE,.format=f,.pool=D3DMPOOL_SYSTEMMEM,.width=w,.height=h,.levels=1};
 return p2d_resource_create((void*)s,&desc,(void*)out);
}
static P2DResource *owned_surface(P2DDevice *d,IDirect3DMobileSurface *s){
 for(P2DResource *r=d->resources;r;r=r->next)if(r==(void*)s&&(r->desc.type==D3DMRTYPE_SURFACE||r->desc.type==D3DMRTYPE_BACKBUFFER))return r;return 0;
}
HRESULT d_CopyRects(IDirect3DMobileDevice *s,IDirect3DMobileSurface *source,const RECT *rects,UINT count,IDirect3DMobileSurface *dest,const POINT *points){
 P2DDevice *d=(void*)s;P2DResource *a=owned_surface(d,source),*b=owned_surface(d,dest);
 if(!a||!b||a==b)return D3DMERR_INVALIDCALL;if(a->desc.format!=b->desc.format)return D3DMERR_NOTAVAILABLE;
 P2DResource *pa=a->parent?a->parent:a,*pb=b->parent?b->parent:b;
 P2DMip *ma=&pa->mips[a->parent?a->level:0],*mb=&pb->mips[b->parent?b->level:0];
 if(ma->locked||mb->locked||ma==mb)return D3DMERR_INVALIDCALL;
 RECT full={0,0,ma->width,ma->height};POINT zero={0,0};
 if(!count){count=1;rects=&full;points=&zero;}else if(!rects||!points)return D3DMERR_INVALIDCALL;
 for(UINT i=0;i<count;i++){
  const RECT *r=&rects[i];const POINT *p=&points[i];
  if(r->left<0||r->top<0||r->right<=r->left||r->bottom<=r->top||(UINT)r->right>ma->width||(UINT)r->bottom>ma->height||p->x<0||p->y<0||(UINT)p->x>mb->width||(UINT)p->y>mb->height||(UINT)(r->right-r->left)>mb->width-p->x||(UINT)(r->bottom-r->top)>mb->height-p->y)return D3DMERR_INVALIDCALL;
 }
 HRESULT h=S_OK;
 if(a->desc.type==D3DMRTYPE_BACKBUFFER)h=p2d_gles_read_backbuffer(d->gles,ma->data,ma->pitch,ma->width,ma->height);
 if(SUCCEEDED(h)&&b->desc.type==D3DMRTYPE_BACKBUFFER)h=p2d_gles_read_backbuffer(d->gles,mb->data,mb->pitch,mb->width,mb->height);
 if(FAILED(h))return h;
 UINT bpp=color_bytes(a->desc.format);
 for(UINT i=0;i<count;i++){
  const RECT *r=&rects[i];const POINT *p=&points[i];
  for(INT y=0;y<r->bottom-r->top;y++){
   BYTE *to=mb->data+(p->y+y)*mb->pitch+p->x*bpp;
   const BYTE *from=ma->data+(r->top+y)*ma->pitch+r->left*bpp;
   UINT bytes=(r->right-r->left)*bpp;while(bytes--)*to++=*from++;
  }
 }
 pb->gpu_dirty=pb->copy_dirty=1;
 return b->desc.type==D3DMRTYPE_BACKBUFFER?p2d_gles_write_backbuffer(d->gles,mb->data,mb->pitch,mb->width,mb->height):S_OK;
}
