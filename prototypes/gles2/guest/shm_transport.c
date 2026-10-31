/* Existing PC engine shared-memory ABI. File fallback stays in transport.c. */
#include "internal.h"
#define SHM_REPLY_MAX 0x201000u
static void barrier(void){__asm__ volatile("":::"memory");}
void p2d2_shm_close(void){
 if(S.shm_unmap){
  if(S.shm_response)S.shm_unmap(S.shm_response,SHM_REPLY_MAX);
  if(S.shm_request)S.shm_unmap(S.shm_request,CAP);
  if(S.shm_control)S.shm_unmap((void*)S.shm_control,4096);
 }
 if(S.shm_module&&S.shm_free_library)S.shm_free_library(S.shm_module);
 S.shm_control=0;S.shm_request=S.shm_response=0;S.shm_unmap=0;S.shm_module=0;S.shm_free_library=0;
}
void p2d2_shm_init(void){
 if(S.shm_control)return;
 S.shm_free_library=(void*)symbol(L"FreeLibrary");if(!S.shm_free_library)return;
 S.shm_module=LoadLibraryW(L"ceddk.dll");if(!S.shm_module){p2d2_shm_close();return;}
 void*(*map)(unsigned long long,U32,int)=(void*)GetProcAddressW(S.shm_module,L"MmMapIoSpace");
 S.shm_unmap=(void*)GetProcAddressW(S.shm_module,L"MmUnmapIoSpace");
 if(!map||!S.shm_unmap){p2d2_shm_close();return;}
 S.shm_control=map(0x61000000ULL,4096,0);
 if(!S.shm_control||S.shm_control[0]!=0x314d4853u||!(S.shm_control[1]&1)){p2d2_shm_close();return;}
 S.shm_request=map(0x60000000ULL,CAP,0);
 if(!S.shm_request){p2d2_shm_close();return;}
 S.shm_response=map(0x60400000ULL,SHM_REPLY_MAX,0);
 if(!S.shm_response){p2d2_shm_close();return;}
 S.shm_control[7]=1;barrier();logtext("GLES2 shared-memory transport enabled\n");
}
int p2d2_shm_can_submit(int compact){
 return S.shm_control&&S.shm_request&&S.shm_response&&S.used>=24&&S.used<=CAP&&
  (compact||(S.width&&S.height&&S.width<=2048&&S.height<=2048&&(unsigned long long)S.width*S.height*2+100<=SHM_REPLY_MAX));
}
static int invalid(void){S.failed=1;fail(0x502);logtext("GLES2 shared-memory reply failed\n");return 0;}
int p2d2_shm_flush(U32 tag,U32 seq,int compact){
 copy(S.shm_request,S.packet,S.used);barrier();
 S.shm_control[2]=S.used;S.shm_control[3]=seq;barrier();
 S.used=24;S.records=0;S.frame_pending=0;
 U32 n=S.shm_control[4];
 if(S.shm_control[5]||S.shm_control[6]!=seq||n<24||n>SHM_REPLY_MAX)return invalid();
 U32 h[6];copy(h,S.shm_response,24);
 U32 pixels=compact?0:S.width*S.height*2;
 if(h[0]!=(compact?0x32524750u:0x31524750u)||h[1]!=seq||!S.width||!S.height||S.width>2048||S.height>2048||
  h[2]!=S.width||h[3]!=S.height||h[4]!=pixels||n<24+pixels||
  (n!=24+pixels&&n!=24+pixels+76)||(tag&&n!=24+pixels+76))return invalid();
 if(pixels){
  if(pixels>S.pixels_capacity){if(S.pixels)S.release(S.pixels,0,0x8000);S.pixels=S.alloc(0,pixels,0x3000,4);S.pixels_capacity=S.pixels?pixels:0;}
  if(!S.pixels)return invalid();
  copy(S.pixels,S.shm_response+24,pixels);
 }
 if(tag){
  copy(S.reply,S.shm_response+24+pixels,76);
  if(S.reply[0]!=0x31514750||S.reply[1]!=tag||S.reply[2]<1||S.reply[2]>16)return invalid();
 }
 if(h[5]){fail(h[5]);logtext("host GLES2 error\n");lognumber(h[5]);}
 return 1;
}
