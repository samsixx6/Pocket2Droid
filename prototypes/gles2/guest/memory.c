#include "internal.h"
static int reserve(unsigned char**buffer,U32*capacity,U32 need,U32 limit,U32 keep){
 if(need>limit){fail(0x505);return 0;}if(need<=*capacity)return 1;U32 size=*capacity?*capacity:4096;while(size<need)size*=2;if(size>limit)size=limit;
 unsigned char*next=S.alloc(0,size,0x3000,4);if(!next){fail(0x505);return 0;}if(*buffer){copy(next,*buffer,keep<*capacity?keep:*capacity);S.release(*buffer,0,0x8000);}*buffer=next;*capacity=size;return 1;
}
int reserve_packet(U32 bytes){return reserve(&S.packet,&S.packet_capacity,bytes,CAP,S.used);}
int reserve_aux(U32 bytes){return reserve(&S.aux,&S.aux_capacity,bytes,1024*1024,S.aux_capacity);}
