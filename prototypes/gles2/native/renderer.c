#include "state.h"
uint32_t p2d_gles_reply_capability(void){return 0x43525031u;}
void *p2d_gles_create(void){Native*g=calloc(1,sizeof(*g));if(!g)return 0;if(!p2d2_context_init(g)){p2d2_context_close(g);free(g);return 0;}return g;}
void p2d_gles_destroy(void*p){if(p){p2d2_context_close(p);free(p);}}
void p2d2_result(Native*g,uint32_t tag,const void*data,size_t n){
 if(n>P2D2_MAX_RESULT){p2d2_error(g,0x505);return;}
 uint8_t *p=realloc(g->result,n?n:4);if(!p){p2d2_error(g,0x505);return;}
 g->result=p;g->result_size=n;g->reply_tag=tag;g->reply_offset=0;g->has_reply=1;if(n)memcpy(p,data,n);else memset(p,0,4);
}
int p2d_gles_execute(void*p,const uint8_t*request,size_t n,const uint8_t**response,size_t*size){
 if(!response||!size)return -1;*response=0;*size=0;Native*g=p;if(!g)return -1;
 P2D2Packet packet;int valid=p2d2_packet_parse(request,n,&packet);uint32_t seq=request&&n>=8?p2d2_u32(request+4):0;
 int compact=request&&n>=24&&p2d2_u32(request)==0x324c4750u;
 g->has_reply=0;g->present=0;
 if(!valid)p2d2_error(g,0x502);
 else if(!p2d2_resize(g,packet.width,packet.height))return -2;
 else{
  size_t at=24;
  for(uint32_t i=0;i<packet.count;i++){
   P2D2Record r;r.op=p2d2_u32(request+at);for(unsigned k=0;k<16;k++)r.args[k]=p2d2_u32(request+at+4+k*4);r.length=p2d2_u32(request+at+68);r.payload=request+at+72;at+=72+((r.length+3u)&~3u);
   p2d2_dispatch(g,&r);unsigned e;while((e=g2GetError()))p2d2_error(g,e);
  }
 }
 if(g->present){p2d2_readback(g);unsigned e;while((e=g2GetError()))p2d2_error(g,e);}
 size_t pixels=compact?0:(size_t)g->width*g->height*2,reply_at=24+pixels;
 uint32_t header[]={compact?0x32524750u:0x31524750u,seq,g->width,g->height,(uint32_t)pixels,g->glerror};memcpy(g->response,header,24);
 if(g->has_reply){P2D2Reply q={{0x31514750,g->reply_tag,g->reply_tag==0x10001u?1u:16u}};size_t remain=g->reply_offset<g->result_size?g->result_size-g->reply_offset:0;if(remain>64)remain=64;if(remain)memcpy(q.words+3,g->result+g->reply_offset,remain);memcpy(g->response+reply_at,&q,76);}
 *response=g->response;*size=reply_at+(g->has_reply?76:0);return valid?0:1;
}
