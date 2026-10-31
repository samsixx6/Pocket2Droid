#include "internal.h"
int _start(H module,U32 reason,H reserved){(void)reserved;if(reason==1)S.module=module;if(reason==0&&S.ready)shutdown_guest();return 1;}
U32 bridge2(U32 op,const U32*registers,const U32*stack,U32 arity){
 if(!initialize())return 0;U32 a[16]={0};for(U32 i=0;i<arity&&i<16;i++)a[i]=i<4?registers[i]:stack[i-4];
 if(op>=0x20100&&op<0x20200)return guest_egl(op,a);
 return guest_gl(op,a);
}
U32 p2d2_egl_dispatch(U32 op,const U32*registers,const U32*stack,U32 arity){return bridge2(op,registers,stack,arity);}
