#include "internal.h"
void p2d_draw_discard(P2DDevice *d){p2d_free(d->index_scratch);d->index_scratch=0;d->index_capacity=0;}
static void attribute(P2DAttribute *a,UINT size,UINT type,UINT *offset,BOOL norm){a->size=size;a->type=type;a->offset=*offset;a->normalized=norm;*offset+=size*(type==0x1401?1:4);}
HRESULT p2d_draw(P2DDevice *d,D3DMPRIMITIVETYPE type,INT base,UINT min,UINT vertices,UINT start,UINT primitives,BOOL indexed){
 if(d->lost)return D3DMERR_DEVICELOST;
 if(p2d_backbuffer_locked(d))return D3DMERR_INVALIDCALL;
 if(!d->scene||!d->stream||d->stream->locked||(indexed&&(!d->indices||d->indices->locked)))return D3DMERR_INVALIDCALL;
 static const UINT modes[]={0,0,1,3,4,5,6};
 if(type<1||type>6)return D3DMERR_INVALIDCALL;
 UINT count=primitives;
 if(type==2||type==4){UINT factor=type==2?2:3;if(count>0x7fffffffu/factor)return D3DMERR_INVALIDCALL;count*=factor;}
 else if(type==3||type>=5){UINT extra=type==3?1:2;if(count>0x7fffffffu-extra)return D3DMERR_INVALIDCALL;count+=extra;}
 if(!primitives)return S_OK;
 P2DDraw call={0};P2DResource *vb=d->stream;DWORD f=vb->desc.fvf;UINT stride=0;
 HRESULT result=p2d_fvf_stride(f,&stride);if(FAILED(result))return result;
 if(d->stride<stride||!d->stride)return D3DMERR_INVALIDCALL;
 UINT available=vb->desc.size<stride?0:1+(vb->desc.size-stride)/d->stride;
 call.mode=modes[type];call.count=count;call.indexed=indexed;call.vertex_buffer=vb;
 UINT origin=start;
 if(indexed){
  P2DResource *ib=d->indices;UINT total=ib->desc.size/2;
  if(start>total||count>total-start||!vertices||min>0xffff||vertices>0x10000-min)return D3DMERR_INVALIDCALL;
  int64_t first=(int64_t)base+min,last=first+vertices;
  if(first<0||last>available)return D3DMERR_INVALIDCALL;
  const WORD *p=(const WORD*)ib->data+start;
  for(UINT i=0;i<count;i++)if(p[i]<min||p[i]-min>=vertices)return D3DMERR_INVALIDCALL;
  origin=(UINT)first;call.vertices=vertices;call.index_buffer=ib;call.index_offset=start*2;call.indices=p;
  /* Rebase only when the API's signed base cannot be an attribute offset. */
  if(min){
   if(count>d->index_capacity){if(count>0x7fffffffu/2)return E_OUTOFMEMORY;WORD *q=p2d_alloc(count*2);if(!q)return E_OUTOFMEMORY;p2d_free(d->index_scratch);d->index_scratch=q;d->index_capacity=count;}
   for(UINT i=0;i<count;i++)d->index_scratch[i]=p[i]-min;
   call.indices=d->index_scratch;call.temporary_indices=1;
  }
 }else{if(start>available||count>available-start)return D3DMERR_INVALIDCALL;call.vertices=count;}
 call.base_offset=origin*d->stride;UINT offset=0,position=f&7,normal=f&0x38;
 call.position_type=(position==1||position==2)?0x140c:0x1406;
 attribute(&call.attribute[0],position>=2?4:3,call.position_type,&offset,0);
 if(normal)attribute(&call.attribute[1],3,normal==0x10?0x140c:0x1406,&offset,0);
 if(f&0x40){attribute(&call.attribute[2],4,0x1401,&offset,1);call.colors=vb->data+call.base_offset+call.attribute[2].offset;call.color_bgra=1;}
 if(f&0x80)attribute(&call.attribute[3],4,0x1401,&offset,1);
 for(UINT i=0;i<((f>>8)&7);i++){UINT dim=(f>>(16+i*2))&3;attribute(&call.attribute[4+i],dim==3?1:dim==1?3:2,((f>>(24+i*2))&3)?0x140c:0x1406,&offset,0);}
 for(UINT i=0;i<2;i++)if(d->textures[i])for(UINT j=0;j<d->textures[i]->desc.levels;j++)if(d->textures[i]->mips[j].locked)return D3DMERR_INVALIDCALL;
 result=p2d_apply_state(d);if(FAILED(result))return result;
 return p2d_gles_draw(d->gles,d,&call);
}
HRESULT d_DrawPrimitive(IDirect3DMobileDevice *s,D3DMPRIMITIVETYPE t,UINT start,UINT n){return p2d_draw((void*)s,t,0,0,0,start,n,0);}
HRESULT d_DrawIndexedPrimitive(IDirect3DMobileDevice *s,D3DMPRIMITIVETYPE t,INT b,UINT min,UINT vertices,UINT start,UINT n){return p2d_draw((void*)s,t,b,min,vertices,start,n,1);}
