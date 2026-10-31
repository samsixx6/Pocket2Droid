#ifndef P2D2_GUEST_INTERNAL_H
#define P2D2_GUEST_INTERNAL_H
#include <stdint.h>
typedef uint32_t U32;typedef int32_t I32;typedef unsigned short W;typedef void*H;
#include "../../gles-android/guest/bulk.h"
#include "../../gles-android/guest/presentation.h"
#include "../api_generated.h"
#define CAP (4u*1024*1024)
#define RPC L"\\Storage Card\\P2DGLES\\"
extern H CreateFileW(const W*,U32,U32,void*,U32,U32,H),LoadLibraryW(const W*);
extern int WriteFile(H,const void*,U32,U32*,void*),ReadFile(H,void*,U32,U32*,void*),CloseHandle(H);
extern void*GetProcAddressW(H,const W*);extern void Sleep(U32);
typedef struct {U32 name,size;unsigned char*data;} GuestBuffer;
typedef struct {U32 size,type,normalized,stride,pointer,buffer,enabled;} GuestAttribute;
typedef struct {
 H shm_module;int(*shm_free_library)(H);volatile U32 *shm_control;unsigned char *shm_request,*shm_response;void(*shm_unmap)(void*,U32);
 H module,core,log,guard,window;U32 compact,frame_pending,ready,failed,sequence,width,height,used,records,error,eglerror,eglinit,context,surface,current,unpack,array_buffer,element_buffer,context_dead,surface_dead;
 unsigned char *packet,*pixels,*aux;U32 pixels_capacity,reply[19];
 U32 packet_capacity,aux_capacity;
 void*(*alloc)(void*,U32,U32,U32);int(*release)(void*,U32,U32);int(*rect)(H,I32*);I32(*metrics)(int);
 P2DPresentation presentation;GuestAttribute attr[16];GuestBuffer buffers[1024];U32 buffer_bytes,num_formats,num_shader_formats,compat_profile;
} Guest;
extern Guest S;
static inline void copy(void*d,const void*s,U32 n){p2d_bulk_copy(d,s,n);}
static inline void zero(void*d,U32 n){p2d_bulk_zero(d,n);}
void*symbol(const W*);void logtext(const char*);void lognumber(U32);void fail(U32);
void p2d2_shm_init(void),p2d2_shm_close(void);int p2d2_shm_can_submit(int),p2d2_shm_flush(U32,U32,int);
int initialize(void),record(U32,const U32*,const void*,U32),flush(U32),query(U32,U32*,const void*,U32,void*,U32);
int reserve_packet(U32),reserve_aux(U32);
void dimensions(void),present(void),shutdown_guest(void);
U32 guest_gl(U32,U32*),guest_egl(U32,U32*);
#endif
