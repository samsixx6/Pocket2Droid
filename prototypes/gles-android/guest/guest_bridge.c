/* Isolated Android test: proven full WM6 bridge + explicit process reset.
 * Single process/context, bounded file RPC. Native GLES renderer.
 */
typedef unsigned long U32;
typedef long I32;
#include "material.h"
typedef unsigned short W;
typedef void *H;
extern H CreateFileW(const W*,U32,U32,void*,U32,U32,H);
extern int WriteFile(H,const void*,U32,U32*,void*);
extern int ReadFile(H,void*,U32,U32*,void*);
extern int CloseHandle(H);
extern H LoadLibraryW(const W*);
extern void *GetProcAddressW(H,const W*);
extern void Sleep(U32);
#include "bridge_ops.h"
#include "bulk.h"
#include "command_cache.h"
#include "query.h"
#include "compact.h"
#include "copy_texture.h"
#include "session.c"
#include "presentation.c"
#define MAX_PACKET 4194304u
#define RPC L"\\Storage Card\\P2DGLES\\"
static int compact_known,compact_supported,probing;
static U32 probe_error;
static int flush(int read_pixels);
static int record(U32,const U32*,const void*,U32);
static int negotiate_compact(void);
static H module,core,logfile,window;
static H session_guard;
static H (*session_mutex)(void*,int,const W*);
static U32 (*last_error)(void);
static U32 *packet,used=24,records,sequence,glerror,eglerror=0x3000,next_texture=1;
static U32 width=240,height=320,initialized,failed,pbuffer,unpack_alignment=4;
static unsigned char *pixels,*pixel_storage;
static P2DPresentation presentation;
static U32 presentation_ready;
static U32 pixels_bytes,pixels_capacity,client_unit,server_unit,texture_enabled[4];
static U32 texture_units=2,palette_enabled,palette_index,transport_active;
static U32 query_pending,query_valid,query_reply[19];
static P2DCommandCache command_cache;
typedef struct {U32 pointer,size,type,stride,enabled,buffer;} Array;
static Array vertex,color,normal,texcoord[4],weight,matrix_index,point_size;
static void *(*alloc)(void*,U32,U32,U32);
static int (*getrect)(H,I32*);
static I32 (*metrics)(int);
static void bytes(void *d,const void *s,U32 n){p2d_bulk_copy(d,s,n);}
static void zero(void *d,U32 n){p2d_bulk_zero(d,n);}
void __aeabi_memclr4(void *d,U32 n){zero(d,n);}
void __aeabi_memcpy4(void *d,const void *s,U32 n){bytes(d,s,n);}
void __aeabi_memcpy(void *d,const void *s,U32 n){bytes(d,s,n);}
U32 __aeabi_uidiv(U32 n,U32 d){U32 q=0,r=0;if(!d)return 0;for(int i=31;i>=0;i--){U32 high=r>>31;r=(r<<1)|((n>>i)&1);if(high||r>=d){r-=d;q|=1u<<i;}}return q;}
static U32 length(const char*s){U32 n=0;while(s[n])n++;return n;}
static void text(const char*s){U32 n;if(logfile&&logfile!=(H)-1)WriteFile(logfile,s,length(s),&n,0);}
static void hex(U32 v){char b[11];U32 i;b[0]='0';b[1]='x';b[10]='\n';for(i=0;i<8;i++)b[i+2]="0123456789abcdef"[(v>>(28-4*i))&15];U32 n;WriteFile(logfile,b,11,&n,0);}
static void *sym(const W *s){return GetProcAddressW(core,s);}
static int presentation_init(void){
 if(presentation_ready)return 1;
 P2DPresentOps o;
 o.get_dc=(P2DPresentHandle(*)(P2DPresentHandle))sym(L"GetDC");
 o.release_dc=(int(*)(P2DPresentHandle,P2DPresentHandle))sym(L"ReleaseDC");
 o.create_dc=(P2DPresentHandle(*)(P2DPresentHandle))sym(L"CreateCompatibleDC");
 o.create_dib=(P2DPresentHandle(*)(P2DPresentHandle,P2DPresentBitmap*,P2DPresentU32,void**,P2DPresentHandle,P2DPresentU32))sym(L"CreateDIBSection");
 o.select_object=(P2DPresentHandle(*)(P2DPresentHandle,P2DPresentHandle))sym(L"SelectObject");
 o.bit_blt=(int(*)(P2DPresentHandle,int,int,int,int,P2DPresentHandle,int,int,P2DPresentU32))sym(L"BitBlt");
 o.delete_object=(int(*)(P2DPresentHandle))sym(L"DeleteObject");
 o.delete_dc=(int(*)(P2DPresentHandle))sym(L"DeleteDC");
 if(!o.get_dc||!o.release_dc||!o.create_dc||!o.create_dib||!o.select_object||!o.bit_blt||!o.delete_object||!o.delete_dc)return 0;
 p2d_present_init(&presentation,&o);presentation_ready=1;return 1;
}
static void presentation_discard(void){
 p2d_command_invalidate(&command_cache);
 pixels=pixel_storage;pixels_bytes=0;p2d_present_invalidate(&presentation);
}
static void *session_create(void *ctx,int *existing){(void)ctx;H h=session_mutex(0,1,L"P2DGLES_RendererSession_v1");if(h)*existing=last_error()==183;return h;}
static void session_close(void *ctx,void *h){(void)ctx;CloseHandle(h);}
static void error(U32 e){if(!glerror)glerror=e;}
#include "buffers.h"
static U32 mode_texture_units(void){
 transport_active=0;
 H f=CreateFileW(RPC L"mode.txt",0x80000000,3,0,3,0x80,0);if(f==(H)-1)return 2;
 char b[17];U32 n=0;int ok=1;
 while(n<sizeof(b)){U32 got=0,remaining=sizeof(b)-n;if(!ReadFile(f,b+n,remaining,&got,0)||got>remaining){ok=0;break;}if(!got)break;n+=got;}
 CloseHandle(f);if(!ok||n>16)return 2;
 while(n&&(b[n-1]=='\n'||b[n-1]=='\r'||b[n-1]==' '))n--;
 /* Only an explicit, complete CPU selection can expose the legacy four units.
    Missing, old or invalid modes stay compatible with the GPU's two stages. */
 transport_active=n>=2&&b[0]=='o'&&b[1]=='n'&&(n==2||(n==6&&b[2]==' '&&((b[3]=='g'&&b[4]=='p'&&b[5]=='u')||(b[3]=='c'&&b[4]=='p'&&b[5]=='u'))));
 return n==6&&b[0]=='o'&&b[1]=='n'&&b[2]==' '&&b[3]=='c'&&b[4]=='p'&&b[5]=='u'?4:2;
}
#include "shm_guest.c"
static int init(void){
 if(initialized)return !failed;initialized=1;
 texture_units=mode_texture_units();
 /* A ROM DLL may load while GLES is Off. Never create files in the real card
    namespace unless the host has published an active, valid transport mode. */
 if(!transport_active){failed=1;return 0;}
 core=LoadLibraryW(L"coredll.dll");if(!core){failed=1;return 0;}
 session_mutex=(H(*)(void*,int,const W*))sym(L"CreateMutexW");last_error=(U32(*)(void))sym(L"GetLastError");
 if(!session_mutex||!last_error){failed=1;return 0;}
 P2DSessionOps session_ops={0,session_create,session_close};session_guard=p2d_claim_session(&session_ops);
 if(!session_guard){failed=1;return 0;}
 logfile=CreateFileW(RPC L"guest.log",0x40000000,3,0,2,0x80,0);
 text("Full WM6 Pocket2Droid native GLES bridge started\n");
 alloc=(void*(*)(void*,U32,U32,U32))sym(L"VirtualAlloc");
 getrect=(int(*)(H,I32*))sym(L"GetClientRect");metrics=(I32(*)(int))sym(L"GetSystemMetrics");
 if(!alloc){failed=1;return 0;}
 /* Sequence numbers restart in a new process. Reset the private renderer before
    emitting any batch so prior game GL objects/client memory cannot leak. */
 U32 (*pid)(void)=(U32(*)(void))sym(L"GetCurrentProcessId");
 U32 session=pid?pid():1,wrote=0;if(!session)session=1;
 H reset=CreateFileW(RPC L"session.reset",0x40000000,3,0,2,0x80,0);
 if(reset==(H)-1){failed=1;text("session reset open failed\n");return 0;}
 int reset_ok=WriteFile(reset,&session,4,&wrote,0)&&wrote==4;
 CloseHandle(reset);if(!reset_ok){failed=1;text("session reset failed\n");return 0;}
 shm_init();packet=shm_request?shm_request:alloc(0,MAX_PACKET,0x3000,4);pixels_capacity=1024*1024*2;pixel_storage=shm_response?shm_response+24:alloc(0,pixels_capacity,0x3000,4);pixels=pixel_storage;
 if(!packet||!pixel_storage){failed=1;text("allocation failed\n");return 0;}
 vertex.size=4;vertex.type=0x1406;color.size=4;color.type=0x1406;normal.size=3;normal.type=0x1406;
 for(U32 i=0;i<4;i++){texcoord[i].size=4;texcoord[i].type=0x1406;}
 if(metrics){width=metrics(0);height=metrics(1);}
 text("initial dimensions\n");hex(width);hex(height);return 1;
}
static int transfer(H f,void *p,U32 n,int writing){
 unsigned char *b=p;while(n){U32 done=0,part=n>32768?32768:n;int ok=writing?WriteFile(f,b,part,&done,0):ReadFile(f,b,part,&done,0);if(!ok||!done||done>part){text(writing?"transfer write failed\n":"transfer read failed\n");hex(n);hex(part);hex(done);hex(ok);return 0;}b+=done;n-=done;}return 1;
}
static int flush(int read_pixels){
 H f;U32 header[6],seq,n;
 if(failed)return 0;
 if(!compact_known&&!negotiate_compact())return 0;
 /* Keep large full replies on the established streaming path before submission. */
 if((shm_flags&1)&&(!compact_supported||read_pixels)&&(width>2048||height>2048||(unsigned long long)width*height*2+100>0x201000)){shm_flags=0;text("SHM large surface; file transport fallback\n");}
 if(shm_flags&1)return shm_flush(read_pixels);
 int compact=compact_supported&&!read_pixels;seq=++sequence;
 packet[0]=compact?P2D_REQUEST_COMPACT:P2D_REQUEST_FULL;packet[1]=seq;packet[2]=records;packet[3]=width;packet[4]=height;packet[5]=used;
 f=CreateFileW(RPC L"request.bin",0x40000000,3,0,2,0x80,0);
 if(f==(H)-1||!transfer(f,packet,used,1)){if(f!=(H)-1)CloseHandle(f);failed=1;text("request write failed\n");return 0;}CloseHandle(f);
 f=CreateFileW(RPC L"request.ready",0x40000000,3,0,2,0x80,0);
 if(f==(H)-1){failed=1;return 0;}int ok=WriteFile(f,&seq,4,&n,0)&&n==4;CloseHandle(f);if(!ok){failed=1;return 0;}
 used=24;records=0;
 for(U32 tries=0;tries<6000;tries++){
  f=CreateFileW(RPC L"response.bin",0x80000000,3,0,3,0x80,0);
  if(f!=(H)-1){
   ok=ReadFile(f,header,24,&n,0)&&n==24;
   if(ok&&header[1]==seq){
    if(header[0]!=(compact?P2D_REPLY_COMPACT:P2D_REPLY_FULL)||!width||!height||width>2048||height>2048||header[2]!=width||header[3]!=height||header[4]!=(compact?0:width*height*2)){CloseHandle(f);presentation_discard();failed=1;text("bad response dimensions\n");return 0;}
    /* prepare may replace/free an old DIB. Publish only owned fallback storage
       until the complete, validated response has reached its new destination. */
    unsigned char *destination=pixels;
    if(!compact){
    destination=pixel_storage;pixels=pixel_storage;pixels_bytes=0;
    if(window&&!pbuffer){
     void *backing=0;P2DPresentU32 stride=0;
     if(!presentation_init()||!p2d_present_prepare(&presentation,window,width,height,&backing,&stride)){CloseHandle(f);presentation_discard();failed=1;text("GDI backing failed\n");return 0;}
     if(stride==width*2)destination=backing;
    }
    if(destination==pixel_storage&&header[4]>pixels_capacity){CloseHandle(f);presentation_discard();failed=1;text("response exceeds packed buffer\n");return 0;}
    ok=transfer(f,destination,header[4],0);
    }
    if(ok&&query_pending){query_valid=transfer(f,query_reply,sizeof(query_reply),0)&&query_reply[0]==0x31514750&&query_reply[1]==query_pending&&query_reply[2]==(probing?1:p2d_query_count(query_pending));if(!query_valid&&!header[5]&&!probing)ok=0;}
    CloseHandle(f);
    if(!ok){presentation_discard();failed=1;text("short response\n");return 0;}
    if(!compact){pixels=destination;pixels_bytes=header[4];}
    if(probing)probe_error=header[5];
    if(header[5]&&!probing){p2d_command_invalidate(&command_cache);text("host GL error\n");hex(header[5]);error(header[5]);}
    if(seq<=4){text("completed batch\n");hex(seq);}return 1;
   }CloseHandle(f);
  }Sleep(10);
 }
 failed=1;text("host timeout\n");error(0x502);return 0;
}
static int record(U32 op,const U32 *args,const void *data,U32 size){
 U32 padded=(size+3)&~3u;if(size>MAX_PACKET-96){error(0x505);return 0;}
 if(used+72+padded>MAX_PACKET&&!flush(0))return 0;
 U32 *r=(U32*)((unsigned char*)packet+used);r[0]=op;for(U32 i=0;i<16;i++)r[1+i]=args?args[i]:0;r[17]=size;
 if(size){if(data)bytes(r+18,data,size);else zero(r+18,size);}if(padded>size)zero((unsigned char*)(r+18)+size,padded-size);
 used+=72+padded;records++;return 1;
}
/* Probe with a separate packet so pending game commands/query state survive.
 * Unsupported probes are private and their errors never become game errors. */
static int negotiate_compact(void){
 U32 probe[24],args[16];zero(args,sizeof(args));
 U32 *saved_packet=packet,saved_used=used,saved_records=records;
 U32 saved_prefix[24];int shared_pending=packet==shm_request&&shm_request;
 if(shared_pending)bytes(saved_prefix,shm_request,sizeof(saved_prefix));
 U32 saved_pending=query_pending,saved_valid=query_valid;
 U32 saved_width=width,saved_height=height;
 compact_known=1;probing=1;probe_error=0;packet=probe;used=24;records=0;
 /* Unsupported legacy renderers reply at their initial 1x1 surface. */
 width=height=1;
 query_pending=P2D_COMPACT_PROBE;query_valid=0;
 int ok=record(P2D_COMPACT_PROBE,args,0,0)&&flush(1);
 compact_supported=ok&&!probe_error&&query_valid&&query_reply[3]==P2D_COMPACT_CAPABILITY;
 if(shared_pending)bytes(shm_request,saved_prefix,sizeof(saved_prefix));
 packet=saved_packet;used=saved_used;records=saved_records;
 width=saved_width;height=saved_height;
 query_pending=saved_pending;query_valid=saved_valid;probing=0;
 if(compact_supported)text("Compact GLES1 replies enabled\n");
 return ok;
}
static U32 component(U32 type){switch(type){case 0x1400:case 0x1401:return 1;case 0x1402:case 0x1403:return 2;case 0x1406:case 0x140c:return 4;default:return 0;}}
static int snapshot(U32 pointer,U32 size){U32 a[16];zero(a,sizeof(a));if(!pointer||!size)return 1;a[0]=pointer;a[1]=size;return record(0x10000,a,(void*)pointer,size);}
static int array_snapshot(Array *p,U32 count){
 if(!p->enabled)return 1;U32 c=component(p->type);if(!c||(!p->pointer&&!p->buffer)||!p->size||p->size>4||count>1048576){error(0x501);return 0;}
 U32 stride=p->stride?p->stride:p->size*c;if(stride>MAX_PACKET||!count)return !count;
 if(count-1>(MAX_PACKET-p->size*c)/stride){error(0x505);return 0;}
 U32 size=(count-1)*stride+p->size*c,addr=p->pointer;
 if(p->buffer){
  P2DBuffer*b=buffer_find(p->buffer);if(!b||addr>b->size||size>b->size-addr){error(0x501);return 0;}
  addr=(U32)(b->data+addr);U32 texture_array=4;for(U32 i=0;i<4;i++)if(p==&texcoord[i])texture_array=i;U32 a[16];zero(a,sizeof(a));a[0]=p->size;a[1]=p->type;a[2]=p->stride;a[3]=addr;
  U32 op=p==&vertex?OP_glVertexPointer:p==&color?OP_glColorPointer:p==&normal?OP_glNormalPointer:p==&weight?OP_glWeightPointerOES:p==&matrix_index?OP_glMatrixIndexPointerOES:OP_glTexCoordPointer;
  if(p==&normal){a[0]=p->type;a[1]=p->stride;a[2]=addr;}
  if(texture_array<4){U32 unit[16];zero(unit,sizeof(unit));unit[0]=0x84c0+texture_array;if(!record(OP_glClientActiveTexture,unit,0,0))return 0;}
  if(!record(op,a,0,0))return 0;
  if(texture_array<4){U32 unit[16];zero(unit,sizeof(unit));unit[0]=0x84c0+client_unit;if(!record(OP_glClientActiveTexture,unit,0,0))return 0;}
 }
 return snapshot(addr,size);
}
static int draw_snapshot(U32 count){
 if(!array_snapshot(&vertex,count)||!array_snapshot(&color,count)||!array_snapshot(&normal,count))return 0;
 if(palette_enabled&&(!array_snapshot(&weight,count)||!array_snapshot(&matrix_index,count)))return 0;
 /* Match the renderer's texture stages: an enabled client array on an unused
    texture unit is not consumed. Games can leave its pointer unset. */
 for(U32 i=0;i<4;i++)if(texture_enabled[i]&&!array_snapshot(&texcoord[i],count))return 0;return 1;
}
static void present(void){
 if(pbuffer||!window||!pixels_bytes)return;
 if(shm_flags&1){if(shm_native_present())return;if(!shm_prepare_gdi()){presentation_discard();failed=1;text("SHM GDI fallback failed\n");return;}}
 if(!p2d_present_blit(&presentation,window,pixels)){presentation_discard();failed=1;text("GDI presentation failed\n");}
}
static void dimensions(void){
 I32 rect[4];if(window&&getrect&&getrect(window,rect)&&rect[2]>0&&rect[3]>0){width=rect[2];height=rect[3];}
 if(!width||!height||width>2048||height>2048){failed=1;text("unsupported probe dimensions\n");}
}
static int pointer(Array *p,U32 *a){
 if((p==&color?a[0]!=4:(a[0]<2||a[0]>4))||(I32)a[2]<0){error(0x501);return 0;}
 if(!(a[1]==0x1406||a[1]==0x140c||(p==&color?a[1]==0x1401:(a[1]==0x1400||a[1]==0x1402)))){error(0x500);return 0;}
 p->size=a[0];p->type=a[1];p->stride=a[2];p->pointer=a[3];p->buffer=array_buffer;return 1;
}
static void enable_array(U32 which,U32 on){switch(which){case 0x86ad:weight.enabled=on;break;case 0x8844:matrix_index.enabled=on;break;case 0x8074:vertex.enabled=on;break;case 0x8076:color.enabled=on;break;case 0x8075:normal.enabled=on;break;case 0x8078:texcoord[client_unit].enabled=on;break;}}
static U32 config_attr(U32 attr){switch(attr){case 0x3020:return 16;case 0x3021:return 0;case 0x3022:return 5;case 0x3023:return 6;case 0x3024:return 5;case 0x3025:return 16;case 0x3028:return 1;case 0x3033:return 5;case 0x3040:return 1;case 0x3027:case 0x3034:return 0x3038;default:return 0;}}
/* Convert IEEE754 replies without introducing floating-point runtime imports
 * into the ARMv4 DLL. Finite overflow and infinities saturate; NaN maps to0. */
static U32 query_fixed(U32 bits){
 U32 magnitude=bits&0x7fffffffu,exponent=magnitude>>23;
 if(magnitude>0x7f800000u)return 0;
 if(exponent>=142)return bits>>31?0x80000000u:0x7fffffffu;
 if(exponent<111)return 0;
 U32 mantissa=(magnitude&0x7fffffu)|0x800000u;
 U32 value=exponent>=134?mantissa<<(exponent-134):mantissa>>(134-exponent);
 return bits>>31?0u-value:value;
}
U32 bridge(U32 op,const U32 *registers,const U32 *stack,U32 arity){
 if(!init())return 0;
 if(arity==1&&p2d_command_repeated(&command_cache,op,registers[0],texture_units,client_unit,server_unit))return 0;
 U32 a[16];zero(a,sizeof(a));for(U32 i=0;i<arity&&i<16;i++)a[i]=i<4?registers[i]:stack[i-4];
 switch(op){
 case OP_glGetFloatv:case OP_glGetFixedv:{int fixed_query=op==OP_glGetFixedv;if(a[1]&&(a[0]==0xd33||a[0]==0x84e2)){*(U32*)a[1]=fixed_query?(a[0]==0xd33?2048u:texture_units)*65536u:a[0]==0xd33?0x45000000:texture_units==4?0x40800000:0x40000000;return 0;}U32 count=p2d_query_count(a[0]);if(!count){error(0x500);return 0;}if(!a[1]){error(0x501);return 0;}
  U32 out=a[1];a[1]=0;if(!record(OP_glGetFloatv,a,0,0))return 0;query_pending=a[0];query_valid=0;int ok=flush(0);query_pending=0;if(ok&&query_valid){if(fixed_query){for(U32 i=0;i<count;i++)((U32*)out)[i]=query_fixed(query_reply[3+i]);}else bytes((void*)out,query_reply+3,count*4);}return 0;}
 case OP_glMatrixIndexPointerOES:case OP_glWeightPointerOES:{
  if(a[0]<1||a[0]>4||(I32)a[2]<0){error(0x501);return 0;}
  if(op==OP_glMatrixIndexPointerOES?a[1]!=0x1401:(a[1]!=0x1406&&a[1]!=0x140c)){error(0x500);return 0;}
  Array*p=op==OP_glMatrixIndexPointerOES?&matrix_index:&weight;p->size=a[0];p->type=a[1];p->stride=a[2];p->pointer=a[3];p->buffer=array_buffer;
  /* Descriptor calls are valid even while the corresponding array is disabled.
     Retain state; enabling an unimplemented palette pipeline still errors. */
  if(array_buffer)return 0;break;}
 case OP_glPointSizePointerOES:if((I32)a[1]<0){error(0x501);return 0;}if(a[0]!=0x1406&&a[0]!=0x140c){error(0x500);return 0;}point_size.size=1;point_size.type=a[0];point_size.stride=a[1];point_size.pointer=a[2];point_size.buffer=array_buffer;return 0;
 case OP_glCurrentPaletteMatrixOES:if(a[0]>=32){error(0x501);return 0;}palette_index=a[0];break;
 case OP_glLoadPaletteFromModelViewMatrixOES:break;
 case OP_glBindBuffer:buffer_bind(a[0],a[1]);return 0;
 case OP_glGenBuffers:buffer_generate(a[0],(U32*)a[1]);return 0;
 case OP_glDeleteBuffers:buffer_delete(a[0],(const U32*)a[1]);return 0;
 case OP_glBufferData:buffer_data(a[0],a[1],(const void*)a[2],a[3]);return 0;
 case OP_glBufferSubData:buffer_sub_data(a[0],a[1],a[2],(const void*)a[3]);return 0;
 case OP_eglGetDisplay:return 1;
 /* Only the OpenGL ES client API is supported; keep EGL and GL errors separate. */
 case OP_eglBindAPI:if(a[0]==0x30a0)return 1;eglerror=0x300c;return 0;
 case OP_eglInitialize:if(a[1])*(U32*)a[1]=1;if(a[2])*(U32*)a[2]=0;return 1;
 case OP_eglChooseConfig:if(a[2]&&a[3])*(U32*)a[2]=1;if(a[4])*(U32*)a[4]=a[2]&&!a[3]?0:1;return 1;
 case OP_eglGetConfigs:if(a[1]&&a[2])*(U32*)a[1]=1;if(a[3])*(U32*)a[3]=a[1]&&!a[2]?0:1;return 1;
 /* Dell Quake passes its window surface to this EGL config query for
    width/height. Keep ordinary config handles/attributes unchanged. */
 case OP_eglGetConfigAttrib:if(a[3])*(U32*)a[3]=a[0]==1&&a[1]==2&&a[2]==0x3057?width:a[0]==1&&a[1]==2&&a[2]==0x3056?height:config_attr(a[2]);return 1;
 case OP_eglCreateWindowSurface:presentation_discard();window=(H)a[2];pbuffer=0;dimensions();text("window dimensions\n");hex(width);hex(height);return 2;
 case OP_eglCreatePbufferSurface:{presentation_discard();U32 *p=(U32*)a[2];pbuffer=1;for(U32 i=0;p&&i<64&&p[i]!=0x3038;i+=2){if(p[i]==0x3057)width=p[i+1];if(p[i]==0x3056)height=p[i+1];}dimensions();return 2;}
 case OP_eglCreateContext:p2d_command_invalidate(&command_cache);return 3;
 case OP_eglMakeCurrent:p2d_command_invalidate(&command_cache);return 1;
 case OP_eglDestroyContext:case OP_eglDestroySurface:case OP_eglTerminate:presentation_discard();return 1;
 case OP_eglWaitGL:case OP_eglWaitNative:case OP_eglSwapInterval:return 1;
 case OP_eglGetError:{U32 e=eglerror;eglerror=0x3000;return e;}
 case OP_eglGetCurrentContext:return 3;case OP_eglGetCurrentDisplay:return 1;case OP_eglGetCurrentSurface:return 2;
 case OP_eglQuerySurface:if(a[3])*(U32*)a[3]=a[2]==0x3057?width:a[2]==0x3056?height:a[2]==0x3028?1:0;return 1;
 case OP_eglQueryString:return (U32)(a[1]==0x3053?"Pocket2Droid native GLES bridge":a[1]==0x3054?"1.0":"");
 case OP_eglGetProcAddress:{const char *s=(char*)a[0];W name[128];U32 i=0;while(s&&s[i]&&i<127){name[i]=(unsigned char)s[i];i++;}name[i]=0;return (U32)GetProcAddressW(module,name);}
 case OP_eglSwapBuffers:shm_mark(1);if(!flush(1))return 0;shm_mark(2);present();shm_mark(3);return !failed;
 case OP_glGenTextures:{U32 *out=(U32*)a[1];if(a[0]>65536||!out){error(0x501);return 0;}for(U32 i=0;i<a[0];i++)out[i]=next_texture++;return 0;}
 case OP_glGetString:return (U32)(a[0]==0x1f00?"Pocket2Droid":a[0]==0x1f01?"Pocket2Droid native GLES bridge":a[0]==0x1f02?"OpenGL ES-CM 1.1":a[0]==0x1f03?"GL_AMD_compressed_ATC_texture":"");
 case OP_glGetError:{if(records)flush(0);U32 e=glerror;glerror=0;return e;}
 case OP_glGetIntegerv:
  /* NUM_COMPRESSED_TEXTURE_FORMATS currently reports zero. Its corresponding
     format array therefore has no elements: even one write can corrupt a
     valid zero-length allocation's adjacent heap metadata. */
  if(a[0]==0x86a3)return 0;
  if(a[1]){U32 *p=(U32*)a[1];*p=a[0]==0xd33?2048:a[0]==0x84e2?texture_units:a[0]==0x8894?array_buffer:a[0]==0x8895?element_buffer:a[0]==0xd05?4:0;}return 0;
 case OP_glReadPixels:{if(!flush(1))return 0;U32 x=a[0],y=a[1],w=a[2],h=a[3];if(x>width||y>height||w>width-x||h>height-y||a[4]!=0x1908||a[5]!=0x1401||!a[6]){error(0x501);return 0;}
  unsigned char *d=(void*)a[6];for(U32 row=0;row<h;row++)for(U32 col=0;col<w;col++){U32 c=((unsigned short*)pixels)[(height-1-y-row)*width+x+col];*d++=((c>>11)&31)*255/31;*d++=((c>>5)&63)*255/63;*d++=(c&31)*255/31;*d++=255;}return 0;}
 case OP_glCopyTexImage2D:{U32 image[16],size,e=p2d_copy_texture_prepare(a,image,MAX_PACKET-96,&size);if(e){error(e);return 0;}
  if(size&&!flush(1))return 0;
  if(record(OP_glTexImage2D,image,0,size)&&size)p2d_copy_texture_pixels((unsigned char*)packet+used-((size+3)&~3u),(const unsigned short*)pixels,width,height,a);
  return 0;}
 case OP_glFlush:case OP_glFinish:return 0;
 case OP_glVertexPointer:if(!pointer(&vertex,a)||array_buffer)return 0;break;case OP_glColorPointer:if(!pointer(&color,a)||array_buffer)return 0;break;
 case OP_glTexCoordPointer:if(!pointer(&texcoord[client_unit],a)||array_buffer)return 0;break;
 case OP_glNormalPointer:if((I32)a[1]<0){error(0x501);return 0;}if(a[0]!=0x1400&&a[0]!=0x1402&&a[0]!=0x140c&&a[0]!=0x1406){error(0x500);return 0;}normal.type=a[0];normal.stride=a[1];normal.pointer=a[2];normal.buffer=array_buffer;if(array_buffer)return 0;break;
 case OP_glClientActiveTexture:if(a[0]<0x84c0||a[0]-0x84c0>=texture_units){error(0x500);return 0;}client_unit=a[0]-0x84c0;break;
 case OP_glActiveTexture:if(a[0]<0x84c0||a[0]-0x84c0>=texture_units){error(0x500);return 0;}server_unit=a[0]-0x84c0;break;
 case OP_glEnable:case OP_glDisable:if(a[0]==0xde1)texture_enabled[server_unit]=op==OP_glEnable;if(a[0]==0x8840)palette_enabled=op==OP_glEnable;break;
 case OP_glEnableClientState:enable_array(a[0],1);break;case OP_glDisableClientState:enable_array(a[0],0);break;
 case OP_glLoadMatrixx:case OP_glMultMatrixx:case OP_glLoadMatrixf:case OP_glMultMatrixf:if(!a[0]){error(0x501);return 0;}bytes(a,(void*)a[0],64);break;
 case OP_glMaterialf:case OP_glMaterialfv:case OP_glMaterialx:case OP_glMaterialxv:{U32 e=p2d_material_prepare(op,a,(const U32*)a[2]);if(e){error(e);return 0;}break;}
 case OP_glLightModelf:case OP_glLightModelx:if(a[0]!=0xb52){error(0x500);return 0;}break;
 case OP_glLightModelfv:case OP_glLightModelxv:{U32 n=a[0]==0xb53?4:a[0]==0xb52?1:0;if(!n){error(0x500);return 0;}U32 *p=(U32*)a[1];if(!p){error(0x501);return 0;}for(U32 i=0;i<n;i++)a[1+i]=p[i];break;}
 case OP_glLightf:case OP_glLightx:if(a[0]<0x4000||a[0]>0x4007||a[1]<0x1205||a[1]>0x1209){error(0x500);return 0;}break;
 case OP_glLightfv:case OP_glLightxv:{if(a[0]<0x4000||a[0]>0x4007||a[1]<0x1200||a[1]>0x1209){error(0x500);return 0;}U32 *p=(U32*)a[2];if(!p){error(0x501);return 0;}U32 n=a[1]<=0x1203?4:a[1]==0x1204?3:1;for(U32 i=0;i<n;i++)a[2+i]=p[i];break;}
 case OP_glTexEnvxv:{const U32 *p=(const U32*)a[2];if(!p){error(0x501);return 0;}U32 n=a[1]==0x2201?4:1;for(U32 i=0;i<n;i++)a[2+i]=p[i];op=OP_glTexEnvx;}
 /* Wire131 carries raw enum/integer scales, and fixed-point ENV_COLOR.
    Preserve existing integer alias calls while accepting proper 16.16 scales. */
 case OP_glTexEnvx:if((a[1]==0x8573||a[1]==0xd1c)&&(a[2]==0x10000||a[2]==0x20000||a[2]==0x40000))a[2]>>=16;break;
 case OP_glTexEnvfv:{U32 *p=(U32*)a[2];if(!p){error(0x501);return 0;}U32 n=a[1]==0x2201?4:1;for(U32 i=0;i<n;i++)a[2+i]=p[i];break;}
 case OP_glFogxv:case OP_glFogfv:{U32 *p=(U32*)a[1];if(!p){error(0x501);return 0;}U32 n=a[0]==0xb66?4:1;for(U32 i=0;i<n;i++)a[1+i]=p[i];break;}
 case OP_glDeleteTextures:if(a[0]>65536){error(0x501);return 0;}record(op,a,(void*)a[1],a[0]*4);return 0;
 case OP_glCompressedTexImage2D:if(a[3]>2048||a[4]>2048||(I32)a[6]<0||a[6]>MAX_PACKET-96||(!a[7]&&a[6])){error(0x501);return 0;}record(op,a,(void*)a[7],a[6]);return 0;
 case OP_glTexImage2D:{U32 channels=a[6]==0x1908?4:a[6]==0x1907?3:a[6]==0x190a?2:1;U32 packed=a[7]==0x1401?channels:2;
  if(a[3]>4096||a[4]>4096||a[3]*a[4]>MAX_PACKET/packed){error(0x501);return 0;}
  U32 row=a[3]*packed,size=row*a[4],alignment=unpack_alignment;
  if(alignment!=1&&alignment!=2&&alignment!=4&&alignment!=8){error(0x501);return 0;}
  U32 stride=(row+alignment-1)&~(alignment-1);
  if(a[8]&&stride!=row){if(!record(op,a,0,size))return 0;unsigned char *dest=(unsigned char*)packet+used-((size+3)&~3u);for(U32 y=0;y<a[4];y++)bytes(dest+y*row,(unsigned char*)a[8]+y*stride,row);}
  else record(op,a,(void*)a[8],a[8]?size:0);return 0;}
 case OP_glDrawElements:{if(a[1]>1048576||(!a[3]&&!element_buffer)){error(0x501);return 0;}U32 bytesper=a[2]==0x1401?1:a[2]==0x1403?2:0;if(!bytesper){error(0x500);return 0;}
  if(element_buffer){P2DBuffer*b=buffer_find(element_buffer);if(!b||a[3]>b->size||a[1]*bytesper>b->size-a[3]){error(0x501);return 0;}a[3]=(U32)(b->data+a[3]);}
  U32 max=0;for(U32 i=0;i<a[1];i++){U32 n=bytesper==1?((unsigned char*)a[3])[i]:((unsigned short*)a[3])[i];if(n>max)max=n;}
  if(!snapshot(a[3],a[1]*bytesper)||!draw_snapshot(a[1]?max+1:0))return 0;break;}
 case OP_glDrawArrays:if(a[1]>1048576||a[2]>1048576-a[1]||!draw_snapshot(a[1]+a[2]))return 0;break;
 case OP_glPixelStorei:if(a[0]==0xcf5)unpack_alignment=a[1];return 0;
 default:if(op>=sizeof(op_supported)||!op_supported[op]){error(0x502);text("unsupported GL opcode\n");hex(op);return 0;}
 }
 if(record(op,a,0,0)&&arity==1)p2d_command_commit(&command_cache,op,a[0],texture_units,client_unit,server_unit);return 0;
}
int _start(H h,U32 reason,void *reserved){(void)reserved;if(reason==1)module=h;if(reason==0){presentation_discard();if(session_guard){CloseHandle(session_guard);session_guard=0;}}return 1;}
