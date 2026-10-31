/* Disposable original-WM test scene. The adapter is loaded under a unique name. */
#include "d3dm.h"
extern HANDLE LoadLibraryW(const WCHAR*);
extern void *GetProcAddressW(HANDLE,const WCHAR*);
extern HANDLE CreateFileW(const WCHAR*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
extern BOOL WriteFile(HANDLE,const void*,DWORD,DWORD*,void*);
extern BOOL CloseHandle(HANDLE);
extern void Sleep(DWORD);
extern void ExitThread(DWORD);
static HANDLE log;
static void number(const char *s,DWORD x){char b[96];UINT n=0,w;while(*s)b[n++]=*s++;b[n++]='=';for(UINT i=0;i<8;i++)b[n++]="0123456789abcdef"[(x>>(28-4*i))&15];b[n++]='\n';WriteFile(log,b,n,&w,0);}
void __aeabi_memclr4(void *out,UINT n){volatile BYTE *p=out;while(n--)*p++=0;}
void _start(void){
 log=CreateFileW((const WCHAR*)L"\\Storage Card\\d3dm-scene.txt",0x40000000,3,0,1,0x80,0);
 if(log==(HANDLE)-1)ExitThread(0);
 HANDLE core=LoadLibraryW((const WCHAR*)L"coredll.dll");
 INT(*metrics)(INT)=GetProcAddressW(core,(const WCHAR*)L"GetSystemMetrics");
 HWND(*create)(DWORD,const WCHAR*,const WCHAR*,DWORD,INT,INT,INT,INT,HWND,HANDLE,HANDLE,void*)=GetProcAddressW(core,(const WCHAR*)L"CreateWindowExW");
 BOOL(*foreground)(HWND)=GetProcAddressW(core,(const WCHAR*)L"SetForegroundWindow");
 DWORD(*error)(void)=GetProcAddressW(core,(const WCHAR*)L"GetLastError");
 if(!metrics||!create||!foreground){number("core.error",1);CloseHandle(log);ExitThread(1);}
 HWND window=create(0,(const WCHAR*)L"STATIC",(const WCHAR*)L"P2D D3DM Scene",0x90000000,0,0,metrics(0),metrics(1),0,0,0,0);
 number("window",(DWORD)(uintptr_t)window);number("window.error",error?error():0);
 if(!window){CloseHandle(log);ExitThread(2);}foreground((HWND)((uintptr_t)window|1));
#ifdef D3DM_ORIGINAL
 HANDLE dll=LoadLibraryW((const WCHAR*)L"D3DM.dll");
#else
 HANDLE dll=LoadLibraryW((const WCHAR*)L"\\Storage Card\\Game\\p2d_d3dm.dll");
#endif
 number("adapter.module",(DWORD)(uintptr_t)dll);number("adapter.error",error?error():0);
 DWORD(*version)(void)=dll?GetProcAddressW(dll,(const WCHAR*)L"P2DD3DMAdapterVersion"):0;
 IDirect3DMobile *(*factory)(UINT)=dll?GetProcAddressW(dll,(const WCHAR*)L"Direct3DMobileCreate"):0;
 number("adapter.version",version?version():0);
 if(!factory){CloseHandle(log);ExitThread(3);}
 IDirect3DMobile *d3d=factory(120);number("factory",(DWORD)(uintptr_t)d3d);
 if(!d3d){CloseHandle(log);ExitThread(4);}
 D3DMPRESENT_PARAMETERS pp={0};pp.BackBufferFormat=D3DMFMT_R5G6B5;
 pp.SwapEffect=D3DMSWAPEFFECT_DISCARD;pp.Windowed=1;
 pp.Flags=1;pp.EnableAutoDepthStencil=1;pp.AutoDepthStencilFormat=D3DMFMT_D16;
 IDirect3DMobileDevice *device=0;
 HRESULT result=d3d->lpVtbl->CreateDevice(d3d,0,D3DMDEVTYPE_DEFAULT,window,0,&pp,&device);
 number("create",result);number("device",(DWORD)(uintptr_t)device);
 if(device){

  IDirect3DMobileVertexBuffer *vb=0;IDirect3DMobileTexture *tex=0;
  number("lighting",device->lpVtbl->SetRenderState(device,D3DMRS_LIGHTING,0));
  number("cull",device->lpVtbl->SetRenderState(device,D3DMRS_CULLMODE,D3DMCULL_NONE));
  number("vb.create",device->lpVtbl->CreateVertexBuffer(device,96,0,0x141,D3DMPOOL_SYSTEMMEM,&vb));
  void *vdata=0;number("vb.lock",vb?vb->lpVtbl->Lock(vb,0,0,&vdata,0):1);
  if(vdata){DWORD *v=vdata;for(UINT i=0;i<4;i++){
   v[i*6]=(i&1)?49152:-49152;v[i*6+1]=(i&2)?49152:-49152;v[i*6+2]=32768;v[i*6+3]=0xffffffff;
   v[i*6+4]=(i&1)?0x3f800000:0;v[i*6+5]=(i&2)?0:0x3f800000;
  }number("vb.unlock",vb->lpVtbl->Unlock(vb));}
  number("tex.create",device->lpVtbl->CreateTexture(device,2,2,1,0,D3DMFMT_R5G6B5,D3DMPOOL_SYSTEMMEM,&tex));
  IDirect3DMobileSurface *image=0,*level=0;D3DMLOCKED_RECT tl={0};
  number("image.create",device->lpVtbl->CreateImageSurface(device,2,2,D3DMFMT_R5G6B5,&image));
  number("image.lock",image?image->lpVtbl->LockRect(image,&tl,0,0):1);
  if(image&&tl.pBits){WORD *q=tl.pBits;q[0]=0xf800;q[1]=0x07e0;q[2]=0x001f;q[3]=0xffe0;number("image.unlock",image->lpVtbl->UnlockRect(image));}
  number("texture.surface",tex?tex->lpVtbl->GetSurfaceLevel(tex,0,&level):1);
  number("texture.copy",image&&level?device->lpVtbl->CopyRects(device,image,0,0,level,0):1);
  if(level)level->lpVtbl->Release(level);if(image)image->lpVtbl->Release(image);
  number("stream",device->lpVtbl->SetStreamSource(device,0,vb,24));
  number("texture",device->lpVtbl->SetTexture(device,0,(void*)tex));
  number("clear",device->lpVtbl->Clear(device,0,0,3,0xff00ff00u,.5f,0));
  number("begin",device->lpVtbl->BeginScene(device));
  number("draw",device->lpVtbl->DrawPrimitive(device,D3DMPT_TRIANGLESTRIP,0,2));
  number("end",device->lpVtbl->EndScene(device));
  number("present",device->lpVtbl->Present(device,0,0,0,0));
  IDirect3DMobileSurface *back=0;D3DMLOCKED_RECT pixels={0};
  number("back.get",device->lpVtbl->GetBackBuffer(device,0,D3DMBACKBUFFER_TYPE_MONO,&back));
  if(back){
   D3DMSURFACE_DESC bd;number("back.desc",back->lpVtbl->GetDesc(back,&bd));number("back.type",bd.Type);
   number("back.lock",back->lpVtbl->LockRect(back,&pixels,0,D3DMLOCK_READONLY));
   if(pixels.pBits&&pixels.Pitch>0){
    HANDLE image=CreateFileW((const WCHAR*)L"\\Storage Card\\d3dm-pixels.bin",0x40000000,3,0,2,0x80,0);DWORD wrote,header[2]={bd.Width,bd.Height};
    if(image!=(HANDLE)-1){WriteFile(image,header,8,&wrote,0);for(UINT y=0;y<bd.Height;y++)WriteFile(image,(BYTE*)pixels.pBits+y*pixels.Pitch,bd.Width*2,&wrote,0);CloseHandle(image);}
    number("back.unlock",back->lpVtbl->UnlockRect(back));
   }
   pixels.pBits=0;number("back.write.lock",back->lpVtbl->LockRect(back,&pixels,0,0));
   if(pixels.pBits){*(WORD*)pixels.pBits=0xf81f;number("back.write.unlock",back->lpVtbl->UnlockRect(back));}
   pixels.pBits=0;number("back.verify.lock",back->lpVtbl->LockRect(back,&pixels,0,D3DMLOCK_READONLY));
   if(pixels.pBits){number("back.verify.pixel",*(WORD*)pixels.pBits);number("back.verify.unlock",back->lpVtbl->UnlockRect(back));}
   back->lpVtbl->Release(back);
  }
  BOOL(*peek)(void*,HWND,UINT,UINT,UINT)=GetProcAddressW(core,(const WCHAR*)L"PeekMessageW");
  BOOL(*translate)(const void*)=GetProcAddressW(core,(const WCHAR*)L"TranslateMessage");
  LONG(*dispatch)(const void*)=GetProcAddressW(core,(const WCHAR*)L"DispatchMessageW");
  DWORD messages[16];UINT frames=0;
  for(UINT i=0;i<200;i++){
   while(peek&&peek(messages,0,0,0,1)){if(translate)translate(messages);if(dispatch)dispatch(messages);}
   foreground((HWND)((uintptr_t)window|1));
   result=device->lpVtbl->Clear(device,0,0,3,0xff00ff00u,.5f,0);
   if(SUCCEEDED(result))result=device->lpVtbl->BeginScene(device);
   if(SUCCEEDED(result))result=device->lpVtbl->DrawPrimitive(device,D3DMPT_TRIANGLESTRIP,0,2);
   if(SUCCEEDED(result))result=device->lpVtbl->EndScene(device);
   if(SUCCEEDED(result))result=device->lpVtbl->Present(device,0,0,0,0);
   if(FAILED(result)){number("loop.error",result);break;}frames++;Sleep(250);
  }
  number("frames",frames);CloseHandle(log);
  if(tex)tex->lpVtbl->Release(tex);if(vb)vb->lpVtbl->Release(vb);device->lpVtbl->Release(device);
 }else CloseHandle(log);
 d3d->lpVtbl->Release(d3d);ExitThread(0);
}
