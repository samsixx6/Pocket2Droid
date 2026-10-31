#include "internal.h"
Guest S;
#include "shm_transport.c"
void __aeabi_memclr4(void*d,U32 n){zero(d,n);}void __aeabi_memclr(void*d,U32 n){zero(d,n);}void __aeabi_memcpy4(void*d,const void*s,U32 n){copy(d,s,n);}void __aeabi_memcpy(void*d,const void*s,U32 n){copy(d,s,n);}
U32 __aeabi_uidiv(U32 n,U32 d){U32 q=0,r=0;if(!d)return 0;for(int i=31;i>=0;i--){U32 hi=r>>31;r=(r<<1)|((n>>i)&1);if(hi||r>=d){r-=d;q|=1u<<i;}}return q;}
void*symbol(const W*n){return GetProcAddressW(S.core,n);}
void logtext(const char*s){if(!S.log||S.log==(H)-1)return;U32 n=0,w;while(s[n])n++;WriteFile(S.log,s,n,&w,0);}
void lognumber(U32 v){char b[12]={'0','x'};for(U32 i=0;i<8;i++)b[2+i]="0123456789abcdef"[(v>>(28-4*i))&15];b[10]='\n';b[11]=0;logtext(b);}
void fail(U32 e){if(!S.error)S.error=e;}
static int transfer(H h,void*p,U32 n,int write){unsigned char*b=p;while(n){U32 size=n>32768?32768:n,got=0;int ok=write?WriteFile(h,b,size,&got,0):ReadFile(h,b,size,&got,0);if(!ok||!got||got>size)return 0;b+=got;n-=got;}return 1;}
int initialize(void){
 if(S.ready)return !S.failed;S.ready=1;S.width=240;S.height=320;S.used=24;S.unpack=4;S.eglerror=0x3000;
 H mode=CreateFileW(RPC L"mode.txt",0x80000000,3,0,3,0x80,0);char b[17];U32 n=0;if(mode==(H)-1||!ReadFile(mode,b,17,&n,0)){if(mode!=(H)-1)CloseHandle(mode);S.failed=1;return 0;}CloseHandle(mode);while(n&&(b[n-1]=='\n'||b[n-1]=='\r'||b[n-1]==' '))n--;if(!(n==2&&b[0]=='o'&&b[1]=='n')&&!(n==6&&b[0]=='o'&&b[1]=='n'&&b[2]==' '&&b[3]=='g'&&b[4]=='p'&&b[5]=='u')){S.failed=1;return 0;}
 S.core=LoadLibraryW(L"coredll.dll");if(!S.core){S.failed=1;return 0;}
 W path[260]={0};U32(*filename)(H,W*,U32)=(void*)symbol(L"GetModuleFileNameW");
 U32 path_length=filename?filename(0,path,260):0;
 S.compat_profile=path_length&&path_length<260?p2d2_profile_path(path):0;

 H(*mutex)(void*,int,const W*)=(void*)symbol(L"CreateMutexW");U32(*error)(void)=(void*)symbol(L"GetLastError");if(!mutex||!error){S.failed=1;return 0;}S.guard=mutex(0,1,L"P2DGLES_RendererSession_v1");if(!S.guard||error()==183){if(S.guard)CloseHandle(S.guard);S.guard=0;S.failed=1;return 0;}
 S.log=CreateFileW(RPC L"guest.log",0x40000000,3,0,2,0x80,0);logtext("Pocket2Droid isolated GLES2 bridge\n");S.alloc=(void*)symbol(L"VirtualAlloc");S.release=(void*)symbol(L"VirtualFree");S.rect=(void*)symbol(L"GetClientRect");S.metrics=(void*)symbol(L"GetSystemMetrics");if(!S.alloc||!S.release){S.failed=1;return 0;}
 void(*memory)(U32*)=(void*)symbol(L"GlobalMemoryStatus");if(memory){U32 status[8]={32};memory(status);logtext("GLES2 initial virtual total/free\n");lognumber(status[6]);lognumber(status[7]);}
 if(!reserve_packet(4096)){S.failed=1;return 0;}
 P2DPresentOps o={0};o.get_dc=(void*)symbol(L"GetDC");o.release_dc=(void*)symbol(L"ReleaseDC");o.create_dc=(void*)symbol(L"CreateCompatibleDC");o.create_dib=(void*)symbol(L"CreateDIBSection");o.select_object=(void*)symbol(L"SelectObject");o.bit_blt=(void*)symbol(L"BitBlt");o.delete_object=(void*)symbol(L"DeleteObject");o.delete_dc=(void*)symbol(L"DeleteDC");p2d_present_init(&S.presentation,&o);
 for(U32 i=0;i<16;i++){S.attr[i].size=4;S.attr[i].type=0x1406;}
 if(S.metrics){S.width=S.metrics(0);S.height=S.metrics(1);}dimensions();
 U32(*pid)(void)=(void*)symbol(L"GetCurrentProcessId");U32 id=pid?pid():1,done=0;H reset=CreateFileW(RPC L"session.reset",0x40000000,3,0,2,0x80,0);if(reset==(H)-1){S.failed=1;return 0;}int ok=WriteFile(reset,&id,4,&done,0)&&done==4;CloseHandle(reset);if(!ok){S.failed=1;return 0;}p2d2_shm_init();U32 probe[16]={0};if(!record(0x10001u,probe,0,0)||!flush(0x10001u)){S.failed=1;return 0;}S.compact=S.reply[2]==1&&S.reply[3]==0x43525031u;U32 profile[16]={0};profile[0]=S.compat_profile;if(!record(P2D2_APP_PROFILE,profile,0,0)){S.failed=1;return 0;}logtext("GLES2 compatibility profile\n");lognumber(S.compat_profile);return !S.failed;
}
void dimensions(void){I32 r[4];if(S.window&&S.rect&&S.rect(S.window,r)&&r[2]>0&&r[3]>0){S.width=r[2];S.height=r[3];}if(!S.width||!S.height||S.width>2048||S.height>2048)S.failed=1;}
int flush(U32 tag){
 if(S.failed)return 0;U32*head=(U32*)S.packet;U32 seq=++S.sequence;int compact=S.compact&&!S.frame_pending;head[0]=compact?0x324c4750u:0x314c4750u;head[1]=seq;head[2]=S.records;head[3]=S.width;head[4]=S.height;head[5]=S.used;
 if(p2d2_shm_can_submit(compact))return p2d2_shm_flush(tag,seq,compact);
 H f=CreateFileW(RPC L"request.bin",0x40000000,3,0,2,0x80,0);if(f==(H)-1)return S.failed=1,0;int ok=transfer(f,S.packet,S.used,1);CloseHandle(f);if(!ok)return S.failed=1,0;
 f=CreateFileW(RPC L"request.ready",0x40000000,3,0,2,0x80,0);U32 done=0;if(f==(H)-1)return S.failed=1,0;ok=WriteFile(f,&seq,4,&done,0)&&done==4;CloseHandle(f);S.used=24;S.records=0;S.frame_pending=0;if(!ok)return S.failed=1,0;
 for(U32 tries=0;tries<6000;tries++){
  f=CreateFileW(RPC L"response.bin",0x80000000,3,0,3,0x80,0);
  if(f!=(H)-1){U32 h[6],got=0;ok=ReadFile(f,h,24,&got,0)&&got==24;if(ok&&h[0]==(compact?0x32524750u:0x31524750u)&&h[1]==seq){
    U32 bytes=S.width*S.height*2;if(h[2]!=S.width||h[3]!=S.height||h[4]!=(compact?0:bytes)){CloseHandle(f);logtext("bad GLES2 response dimensions\n");S.failed=1;return 0;}
    if(h[4]){
    if(bytes>S.pixels_capacity){if(S.pixels)S.release(S.pixels,0,0x8000);S.pixels=S.alloc(0,bytes,0x3000,4);S.pixels_capacity=S.pixels?bytes:0;}if(!S.pixels){CloseHandle(f);S.failed=1;return 0;}
    ok=transfer(f,S.pixels,bytes,0);
    }else ok=1;if(ok&&tag){ok=transfer(f,S.reply,76,0)&&S.reply[0]==0x31514750&&S.reply[1]==tag&&S.reply[2]>=1&&S.reply[2]<=16;}CloseHandle(f);
    if(!ok){logtext("short GLES2 response\n");S.failed=1;return 0;}if(h[5]){fail(h[5]);logtext("host GLES2 error\n");lognumber(h[5]);}if(seq<=4){logtext("GLES2 batch\n");lognumber(seq);}return 1;
   }CloseHandle(f);
  }Sleep(10);
 }logtext("GLES2 timeout\n");S.failed=1;fail(0x502);return 0;
}
int record(U32 op,const U32*a,const void*data,U32 n){
 if(n>CAP-96){fail(0x505);return 0;}U32 pad=(n+3)&~3u;if(S.used+72+pad>S.packet_capacity&&S.records&&!flush(0))return 0;if(!reserve_packet(S.used+72+pad))return 0;U32*r=(U32*)(S.packet+S.used);r[0]=op;for(U32 i=0;i<16;i++)r[i+1]=a?a[i]:0;r[17]=n;if(n){if(data)copy(r+18,data,n);else zero(r+18,n);}if(pad>n)zero((unsigned char*)(r+18)+n,pad-n);S.used+=72+pad;S.records++;if(op==P2D2_p2dD3DMReadBackBuffer565||op==P2D2_eglSwapBuffers)S.frame_pending=1;return 1;
}
int query(U32 op,U32*a,const void*data,U32 n,void*out,U32 bytes){
 if(bytes>1024*1024){fail(0x505);return 0;}if(!record(op,a,data,n)||!flush(op))return 0;
 unsigned char*p=out;for(U32 at=0;at<bytes;){U32 part=bytes-at>64?64:bytes-at;copy(p+at,S.reply+3,part);at+=part;if(at<bytes){U32 arg[16]={0};arg[0]=at;if(!record(P2D2_QUERY_CHUNK,arg,0,0)||!flush(P2D2_QUERY_CHUNK))return 0;}}return 1;
}
void present(void){if(S.window&&S.pixels){void*bits=0;U32 stride=0;if(!p2d_present_prepare(&S.presentation,S.window,S.width,S.height,&bits,&stride)||!p2d_present_blit(&S.presentation,S.window,S.pixels)){S.failed=1;logtext("GLES2 GDI presentation failed\n");}}}
void shutdown_guest(void){
 H module=S.module;p2d_present_invalidate(&S.presentation);p2d2_shm_close();if(S.release){for(U32 i=0;i<1024;i++)if(S.buffers[i].data)S.release(S.buffers[i].data,0,0x8000);if(S.packet)S.release(S.packet,0,0x8000);if(S.aux)S.release(S.aux,0,0x8000);if(S.pixels)S.release(S.pixels,0,0x8000);}if(S.guard)CloseHandle(S.guard);if(S.log&&S.log!=(H)-1)CloseHandle(S.log);zero(&S,sizeof(S));S.module=module;
}
