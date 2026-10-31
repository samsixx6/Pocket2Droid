/* Disposable PC transport/display probe. Shipping DLLs remain untouched. */
#include "framebuffer_ce.c"
static volatile U32 *shm_control;
static U32 *shm_request;
static unsigned char *shm_response,*shm_descriptor;
static U32 shm_flags;
static P2DFBOps shm_fb_ops;
static U32 shm_fb_ready;
static void shm_mark(U32 stage){if(shm_control)shm_control[14]=stage;}
static void shm_init(void){
 H ddk=LoadLibraryW(L"ceddk.dll");
 void*(*map)(unsigned long long,U32,int)=ddk?(void*)GetProcAddressW(ddk,L"MmMapIoSpace"):0;
 if(!map){text("SHM mapping API unavailable; file fallback\n");return;}
 shm_control=map(0x61000000ULL,4096,0);
 if(!shm_control||shm_control[0]!=0x314d4853u){shm_control=0;text("SHM endpoint absent; file fallback\n");return;}
 shm_flags=shm_control[1];
 if(!(shm_flags&1)){text("SHM markers only; file transport baseline\n");return;}
 shm_request=map(0x60000000ULL,4194304,0);
 shm_response=map(0x60400000ULL,0x201000,0);
 shm_descriptor=map(0x60c01000ULL,4096,0);
 if(!shm_request||!shm_response||!shm_descriptor){shm_flags=0;shm_request=0;shm_response=0;shm_descriptor=0;text("SHM arena mapping failed; file fallback\n");return;}
 shm_control[7]=1;
 text("SHM transport enabled, flags\n");hex(shm_flags);
}
static int shm_copy_to(void *target,U32 bytes_count){
 shm_control[8]=(U32)target;shm_control[9]=bytes_count;shm_control[10]=sequence;
 return shm_control[5]==0;
}
static int shm_flush(int read_pixels){
 U32 header[6];int compact=compact_supported&&!read_pixels;U32 seq=++sequence;
 packet[0]=compact?P2D_REQUEST_COMPACT:P2D_REQUEST_FULL;packet[1]=seq;packet[2]=records;packet[3]=width;packet[4]=height;packet[5]=used;
 if(packet!=shm_request)bytes(shm_request,packet,used);
 shm_control[2]=used;shm_control[3]=seq;
 used=24;records=0;
 if(shm_control[5]||shm_control[6]!=seq){failed=1;text("SHM submission failed\n");return 0;}
 U32 response_size=shm_control[4];
 if(response_size<24||response_size>0x201000){failed=1;text("SHM response size unsupported\n");return 0;}
 bytes(header,shm_response,24);
 if(header[0]!=(compact?P2D_REPLY_COMPACT:P2D_REPLY_FULL)||header[1]!=seq||
    !width||!height||width>2048||height>2048||header[2]!=width||header[3]!=height||
    header[4]!=(compact?0:width*height*2)||24+header[4]>response_size){failed=1;text("SHM reply mismatch\n");return 0;}
 if(query_pending){
  query_valid=response_size>=24+header[4]+sizeof(query_reply);
  if(query_valid){bytes(query_reply,shm_response+24+header[4],sizeof(query_reply));query_valid=query_reply[0]==0x31514750&&query_reply[1]==query_pending&&query_reply[2]==(probing?1:p2d_query_count(query_pending));}
  if(!query_valid&&!header[5]&&!probing){failed=1;text("SHM query mismatch\n");return 0;}
 }
 if(!compact){pixels=shm_response+24;pixels_bytes=header[4];}
 if(probing)probe_error=header[5];
 if(header[5]&&!probing){p2d_command_invalidate(&command_cache);error(header[5]);text("host GL error\n");hex(header[5]);}
 return 1;
}
int p2d_fb_copy(const P2DFBRaw*r,P2DFBU32 capacity,unsigned rotation,const void*source,P2DFBU32 count,P2DFBU32 w,P2DFBU32 h,P2DFBU32 stride){
 if(!(shm_flags&2)||!r||source!=pixels||count!=w*h*2||stride!=w*2||r->format!=1||r->bpp!=16||r->x_stride!=2)return 0;
 U32 *d=(U32*)shm_descriptor;d[0]=(U32)r->pixels;d[1]=capacity;d[2]=r->width;d[3]=r->height;d[4]=r->y_stride;d[5]=rotation;d[6]=w;d[7]=h;
 shm_control[11]=sequence;return shm_control[12]==1;
}
static int shm_native_present(void){
 if(!(shm_flags&2))return 0;
 if(!shm_fb_ready){
#define FB_LOAD(n,s) shm_fb_ops.n=(void*)sym(L##s)
  FB_LOAD(get_dc,"GetDC");FB_LOAD(release_dc,"ReleaseDC");FB_LOAD(foreground,"GetForegroundWindow");
  FB_LOAD(visible,"IsWindowVisible");FB_LOAD(client_rect,"GetClientRect");FB_LOAD(client_to_screen,"ClientToScreen");
  FB_LOAD(metrics,"GetSystemMetrics");FB_LOAD(clip_box,"GetClipBox");FB_LOAD(escape,"ExtEscape");
  FB_LOAD(enum_mode,"EnumDisplaySettings");FB_LOAD(test_mode,"ChangeDisplaySettingsEx");FB_LOAD(query_memory,"VirtualQuery");
#undef FB_LOAD
  shm_fb_ready=1;
 }
 return p2d_fb_present(&shm_fb_ops,window,pixels,pixels_bytes,width,height);
}
static int shm_prepare_gdi(void){
 void *backing=0;P2DPresentU32 stride=0;
 if(!presentation_init()||!p2d_present_prepare(&presentation,window,width,height,&backing,&stride))return 0;
 if(stride!=width*2)return 0;
 if(!shm_copy_to(backing,pixels_bytes))return 0;
 pixels=backing;return 1;
}
