/* Bounded ARMv4-safe transfers. Word accesses only when both pointers align.
 * Preserve the old forward-byte behavior for overlapping ranges. */
#ifndef P2D_GUEST_BULK_H
#define P2D_GUEST_BULK_H
typedef unsigned int P2DBulkWord __attribute__((__may_alias__));
typedef __UINTPTR_TYPE__ P2DBulkAddress;
static void p2d_bulk_copy(void *destination,const void *source,unsigned int n){
 unsigned char *d=destination;const unsigned char*s=source;
 P2DBulkAddress da=(P2DBulkAddress)d,sa=(P2DBulkAddress)s;
 if(((da^sa)&3u)||((da>sa?da-sa:sa-da)<n)){
  while(n--)*d++=*s++;return;
 }
 while(n&&((P2DBulkAddress)d&3u)){*d++=*s++;n--;}
 while(n>=16){P2DBulkWord*dw=(P2DBulkWord*)d;const P2DBulkWord*sw=(const P2DBulkWord*)s;
  dw[0]=sw[0];dw[1]=sw[1];dw[2]=sw[2];dw[3]=sw[3];d+=16;s+=16;n-=16;
 }
 while(n>=4){*(P2DBulkWord*)d=*(const P2DBulkWord*)s;d+=4;s+=4;n-=4;}
 while(n--)*d++=*s++;
}
static void p2d_bulk_zero(void *destination,unsigned int n){
 volatile unsigned char*d=destination;
 while(n&&((P2DBulkAddress)d&3u)){*d++=0;n--;}
 while(n>=16){volatile P2DBulkWord*w=(volatile P2DBulkWord*)d;w[0]=0;w[1]=0;w[2]=0;w[3]=0;d+=16;n-=16;}
 while(n>=4){*(volatile P2DBulkWord*)d=0;d+=4;n-=4;}
 while(n--)*d++=0;
}
#endif
