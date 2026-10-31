#ifndef P2D_GLES_CE_API_H
#define P2D_GLES_CE_API_H
#include "setup_core.h"
typedef void *H;
extern H CreateFileW(const W*,U32,U32,void*,U32,U32,H);
extern int ReadFile(H,void*,U32,U32*,void*);
extern int WriteFile(H,const void*,U32,U32*,void*);
extern int CloseHandle(H);
extern H LoadLibraryW(const W*);
extern void *GetProcAddressW(H,const W*);
extern int CreateProcessW(const W*,W*,void*,void*,int,U32,void*,const W*,void*,void*);
extern void Sleep(U32);
extern void ExitThread(U32);
static void p2d_zero(void*v,U32 n){volatile U8*p=v;while(n--)*p++=0;}
void __aeabi_memclr4(void*v,U32 n){p2d_zero(v,n);}
void __aeabi_memclr(void*v,U32 n){p2d_zero(v,n);}
void __aeabi_memcpy4(void*v,const void*s,U32 n){U8*d=v;const U8*p=s;while(n--)*d++=*p++;}
void __aeabi_memcpy(void*v,const void*s,U32 n){__aeabi_memcpy4(v,s,n);}
static int p2d_exists(const W*name){H f=CreateFileW(name,0x80000000,3,0,3,0x80,0);if(f==(H)-1)return 0;CloseHandle(f);return 1;}
#endif
