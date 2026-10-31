#include "protocol.h"
#include "api_generated.h"
int p2d2_packet_parse(const uint8_t*b,size_t n,P2D2Packet*out){
 if(!b||!out||n<24||n>P2D2_MAX_PACKET||(p2d2_u32(b)!=0x314c4750&&p2d2_u32(b)!=0x324c4750)||p2d2_u32(b+20)!=n)return 0;
 uint32_t seq=p2d2_u32(b+4),count=p2d2_u32(b+8),w=p2d2_u32(b+12),h=p2d2_u32(b+16);
 if(!seq||!w||!h||w>2048||h>2048||count>(n-24)/72)return 0;
 size_t at=24;
 for(uint32_t i=0;i<count;i++){
  if(n-at<72||!p2d2_known_op(p2d2_u32(b+at)))return 0;
  uint64_t payload=((uint64_t)p2d2_u32(b+at+68)+3)&~(uint64_t)3;
  if(payload>n-at-72)return 0;
  at+=72+(size_t)payload;
 }
 if(at!=n)return 0;
 *out=(P2D2Packet){b,n,seq,count,w,h};return 1;
}
int p2d2_record_at(const P2D2Packet*p,uint32_t index,P2D2Record*out){
 if(!p||!out||index>=p->count)return 0;
 size_t at=24;
 for(uint32_t i=0;i<index;i++)at+=72+(((size_t)p2d2_u32(p->bytes+at+68)+3)&~(size_t)3);
 out->op=p2d2_u32(p->bytes+at);
 for(unsigned j=0;j<16;j++)out->args[j]=p2d2_u32(p->bytes+at+4+j*4);
 out->length=p2d2_u32(p->bytes+at+68);out->payload=p->bytes+at+72;return 1;
}
