#include "internal.h"
#include "numbers.h"
static BOOL numeric(D3DMFORMAT f){return f==D3DMFMT_D3DMVALUE_FLOAT||f==D3DMFMT_D3DMVALUE_FIXED;}
static void changed(P2DDevice *d){if(!++d->state.generation)++d->state.generation;}
void p2d_state_discard(P2DDevice *d){P2DLight *p=d->state.lights;while(p){P2DLight *n=p->next;p2d_free(p);p=n;}d->state.lights=0;}
void p2d_state_init(P2DDevice *d){
 static const DWORD defaults[50]={3,2,1,0,1,2,1,3,4,0,8,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,8,0,0xffffffff,0xffffffff,0xffffffff,0,0,0,0,0,1,1,0,0,1,1,0,1,2,0,15,1,1,0};
 p2d_state_discard(d);p2d_zero(&d->state,sizeof(d->state));
 for(UINT i=0;i<50;i++)d->state.render[i]=defaults[i];
 d->state.render[D3DMRS_ZENABLE]=d->pp.EnableAutoDepthStencil?1:0;
 for(UINT i=0;i<5;i++){DWORD *m=(void*)&d->state.matrix[i];for(UINT j=0;j<16;j++)m[j]=j%5?0:0x3f800000;d->state.matrix_format[i]=D3DMFMT_D3DMVALUE_FLOAT;}
 for(UINT i=0;i<2;i++){
  DWORD *s=d->state.texture[i];s[1]=i?1:4;s[2]=2;s[3]=1;s[4]=i?1:2;s[5]=2;s[6]=1;
  s[7]=i;s[8]=s[9]=s[18]=1;s[11]=s[12]=1;s[16]=1;s[19]=s[20]=s[21]=1;
 }
 d->state.material_format=D3DMFMT_D3DMVALUE_FIXED;
 DWORD *m=(void*)&d->state.material;m[0]=m[1]=m[2]=65536;
 d->state.viewport=(D3DMVIEWPORT){0,0,d->pp.BackBufferWidth,d->pp.BackBufferHeight,0,1};changed(d);
}
HRESULT d_SetRenderState(IDirect3DMobileDevice *s,D3DMRENDERSTATETYPE t,DWORD v){
 P2DDevice *d=(void*)s;if((UINT)t>=50)return D3DMERR_INVALIDCALL;
 switch(t){
 case D3DMRS_FILLMODE:if(v<1||v>3)return D3DMERR_INVALIDCALL;if(v!=3)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_SHADEMODE:if(v<1||v>2)return D3DMERR_INVALIDCALL;if(v!=2)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_SRCBLEND:case D3DMRS_DESTBLEND:if(v<1||v>11)return D3DMERR_INVALIDCALL;if(t==D3DMRS_DESTBLEND&&v==11)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_STENCILENABLE:case D3DMRS_RANGEFOGENABLE:if(v)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_CLIPPING:if(!v)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_CULLMODE:if(v<1||v>3)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_ZFUNC:case D3DMRS_ALPHAFUNC:case D3DMRS_STENCILFUNC:if(v<1||v>8)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_STENCILFAIL:case D3DMRS_STENCILZFAIL:case D3DMRS_STENCILPASS:if(v<1||v>8)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_ALPHAREF:if(v>255)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_BLENDOP:if(v<1||v>5)return D3DMERR_INVALIDCALL;if(v>3)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_COLORWRITEENABLE:if(v&~15u)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_DIFFUSEMATERIALSOURCE:case D3DMRS_SPECULARMATERIALSOURCE:case D3DMRS_AMBIENTMATERIALSOURCE:if(v>2)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_FOGTABLEMODE:case D3DMRS_FOGVERTEXMODE:if(v>3)return D3DMERR_INVALIDCALL;break;
 case D3DMRS_DEPTHBIAS:case D3DMRS_SLOPESCALEDEPTHBIAS:case D3DMRS_WRAP0:case D3DMRS_WRAP1:case D3DMRS_WRAP2:case D3DMRS_WRAP3:if(v)return D3DMERR_NOTAVAILABLE;break;
 case D3DMRS_ZENABLE:if(v>2)return D3DMERR_INVALIDCALL;if(v==2)return D3DMERR_NOTAVAILABLE;break;
 default:break;
 }
 if(d->state.render[t]!=v){d->state.render[t]=v;changed(d);}return S_OK;
}
HRESULT d_GetRenderState(IDirect3DMobileDevice *s,D3DMRENDERSTATETYPE t,DWORD *v){if(!v||(UINT)t>=50)return D3DMERR_INVALIDCALL;*v=((P2DDevice*)s)->state.render[t];return S_OK;}
HRESULT d_SetTransform(IDirect3DMobileDevice *s,D3DMTRANSFORMSTATETYPE t,const D3DMMATRIX *m,D3DMFORMAT f){
 if(!m||(UINT)t>=5||!numeric(f))return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;d->state.matrix[t]=*m;d->state.matrix_format[t]=f;changed(d);return S_OK;
}
HRESULT d_GetTransform(IDirect3DMobileDevice *s,D3DMTRANSFORMSTATETYPE t,D3DMMATRIX *m,D3DMFORMAT *f){
 if(!m||!f||(UINT)t>=5)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;*m=d->state.matrix[t];*f=d->state.matrix_format[t];return S_OK;
}
HRESULT d_SetViewport(IDirect3DMobileDevice *s,const D3DMVIEWPORT *v){
 P2DDevice *d=(void*)s;if(!v||!v->Width||!v->Height||v->X>d->pp.BackBufferWidth||v->Y>d->pp.BackBufferHeight||v->Width>d->pp.BackBufferWidth-v->X||v->Height>d->pp.BackBufferHeight-v->Y)return D3DMERR_INVALIDCALL;
 DWORD min=p2d_float_word(v->MinZ),max=p2d_float_word(v->MaxZ);
 if(min>0x3f800000||max>0x3f800000||min>max)return D3DMERR_INVALIDCALL;
 d->state.viewport=*v;changed(d);return S_OK;
}
HRESULT d_GetViewport(IDirect3DMobileDevice *s,D3DMVIEWPORT *v){if(!v)return D3DMERR_INVALIDCALL;*v=((P2DDevice*)s)->state.viewport;return S_OK;}
static BOOL texture_op(DWORD v){return (v>=1&&v<=16)||(v>=18&&v<=21)||v==24||v==25||v==26;}
HRESULT d_SetTextureStageState(IDirect3DMobileDevice *s,DWORD stage,D3DMTEXTURESTAGESTATETYPE t,DWORD v){
 if(stage>=2||!t||(UINT)t>=22)return D3DMERR_INVALIDCALL;
 switch(t){
 case D3DMTSS_COLOROP:case D3DMTSS_ALPHAOP:if(!texture_op(v))return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_COLORARG0:case D3DMTSS_COLORARG1:case D3DMTSS_COLORARG2:case D3DMTSS_ALPHAARG0:case D3DMTSS_ALPHAARG1:case D3DMTSS_ALPHAARG2:if((v&15)>5||(v&~63u))return D3DMERR_INVALIDCALL;break;
 case D3DMTSS_RESULTARG:if(v!=1&&v!=5)return D3DMERR_INVALIDCALL;break;
 case D3DMTSS_TEXCOORDINDEX:if(v>1)return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_ADDRESSU:case D3DMTSS_ADDRESSV:case D3DMTSS_ADDRESSW:if(v<1||v>5)return D3DMERR_INVALIDCALL;if(v>3)return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_MAGFILTER:case D3DMTSS_MINFILTER:if(v<1||v>2)return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_MIPFILTER:if(v>2)return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_TEXTURETRANSFORMFLAGS:if(v!=0&&v!=1&&v!=2&&v!=3&&v!=(3|256))return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_MAXANISOTROPY:if(v!=1)return D3DMERR_NOTAVAILABLE;break;
 case D3DMTSS_MIPMAPLODBIAS:case D3DMTSS_MAXMIPLEVEL:if(v)return D3DMERR_NOTAVAILABLE;break;
 default:break;
 }
 P2DDevice *d=(void*)s;if(d->state.texture[stage][t]!=v){d->state.texture[stage][t]=v;changed(d);}return S_OK;
}
HRESULT d_GetTextureStageState(IDirect3DMobileDevice *s,DWORD stage,D3DMTEXTURESTAGESTATETYPE t,DWORD *v){if(!v||stage>=2||!t||(UINT)t>=22)return D3DMERR_INVALIDCALL;*v=((P2DDevice*)s)->state.texture[stage][t];return S_OK;}
HRESULT d_SetMaterial(IDirect3DMobileDevice *s,const D3DMMATERIAL *m,D3DMFORMAT f){if(!m||!numeric(f))return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;d->state.material=*m;d->state.material_format=f;changed(d);return S_OK;}
HRESULT d_GetMaterial(IDirect3DMobileDevice *s,D3DMMATERIAL *m,D3DMFORMAT *f){if(!m||!f)return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;*m=d->state.material;*f=d->state.material_format;return S_OK;}
static P2DLight *light(P2DDevice *d,DWORD n){for(P2DLight *p=d->state.lights;p;p=p->next)if(p->index==n)return p;return 0;}
HRESULT d_SetLight(IDirect3DMobileDevice *s,DWORD n,const D3DMLIGHT *v,D3DMFORMAT f){
 if(!v||!numeric(f)||(v->Type!=1&&v->Type!=3))return D3DMERR_INVALIDCALL;P2DDevice *d=(void*)s;P2DLight *p=light(d,n);
 if(!p){p=p2d_alloc(sizeof(*p));if(!p)return E_OUTOFMEMORY;p2d_zero(p,sizeof(*p));p->index=n;p->next=d->state.lights;d->state.lights=p;}
 p->value=*v;p->format=f;changed(d);return S_OK;
}
HRESULT d_GetLight(IDirect3DMobileDevice *s,DWORD n,D3DMLIGHT *v,D3DMFORMAT *f){P2DLight *p=light((void*)s,n);if(!p||!v||!f)return D3DMERR_INVALIDCALL;*v=p->value;*f=p->format;return S_OK;}
HRESULT d_LightEnable(IDirect3DMobileDevice *s,DWORD n,BOOL on){P2DDevice *d=(void*)s;P2DLight *p=light(d,n);if(!p)return D3DMERR_INVALIDCALL;UINT count=0;for(P2DLight *q=d->state.lights;q;q=q->next)if(q->enabled&&q!=p)count++;if(on&&count>=8)return D3DMERR_NOTAVAILABLE;p->enabled=!!on;changed(d);return S_OK;}
HRESULT d_GetLightEnable(IDirect3DMobileDevice *s,DWORD n,BOOL *v){P2DLight *p=light((void*)s,n);if(!p||!v)return D3DMERR_INVALIDCALL;*v=p->enabled;return S_OK;}
HRESULT p2d_apply_state(P2DDevice *d){
 static const UINT blend[]={0,0,1,0x300,0x301,0x302,0x303,0x304,0x305,0x306,0x307,0x308};
 P2DGLState *g=&d->gl_state;D3DMVIEWPORT *v=&d->state.viewport;
 g->viewport[0]=v->X;g->viewport[1]=d->pp.BackBufferHeight-v->Y-v->Height;g->viewport[2]=v->Width;g->viewport[3]=v->Height;
 g->src_blend=blend[d->state.render[5]];g->dest_blend=blend[d->state.render[6]];g->depth_func=0x1ff+d->state.render[8];g->alpha_func=d->state.render[10];
 p2d_zero(g->depth_correction,sizeof(g->depth_correction));g->depth_correction[0]=g->depth_correction[5]=g->depth_correction[15]=0x3f800000;g->depth_correction[10]=0x40000000;g->depth_correction[14]=0xbf800000;
 for(UINT i=0;i<2;i++){DWORD *t=d->state.texture[i];P2DStage *p=&g->stage[i];p->color_op=t[1];p->alpha_op=t[4];p->color_arg[0]=t[19];p->color_arg[1]=t[2];p->color_arg[2]=t[3];p->alpha_arg[0]=t[20];p->alpha_arg[1]=t[5];p->alpha_arg[2]=t[6];p->result=t[21];}
 return S_OK;
}
