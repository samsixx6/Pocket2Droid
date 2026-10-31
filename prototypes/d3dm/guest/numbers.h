#ifndef P2D_D3DM_NUMBERS_H
#define P2D_D3DM_NUMBERS_H
#include <stdint.h>
/* IEEE754 round-to-nearest-even packing; no guest software FP runtime. */
static uint32_t p2d_i32_float_bits(int32_t n){
 uint32_t sign=n<0?0x80000000u:0,magnitude=n<0?0u-(uint32_t)n:(uint32_t)n;
 if(!magnitude)return 0;
 uint32_t high=0,v=magnitude;while(v>>1){high++;v>>=1;}
 uint32_t mantissa;
 if(high<=23)mantissa=magnitude<<(23-high);
 else{
  uint32_t shift=high-23,remaining=magnitude&((1u<<shift)-1),half=1u<<(shift-1);
  mantissa=magnitude>>shift;
  if(remaining>half||(remaining==half&&(mantissa&1)))mantissa++;
  if(mantissa==0x1000000u){mantissa>>=1;high++;}
 }
 return sign|((high+127)<<23)|(mantissa&0x7fffffu);
}
static uint32_t p2d_fixed_float_bits(int32_t n){
 uint32_t value=p2d_i32_float_bits(n);return value?value-(16u<<23):0;
}
static uint32_t p2d_u8_float_bits(uint32_t n){
 if(!n)return 0;if(n==255)return 0x3f800000u;
 uint32_t high=0,v=n;while(v>>1){v>>=1;high++;}
 uint32_t mantissa=((n<<(31-high))+127u)/255u;
 return ((119+high)<<23)|(mantissa&0x7fffffu);
}
static float p2d_word_float(uint32_t n){union{uint32_t u;float f;}v={n};return v.f;}
static uint32_t p2d_float_word(float n){union{float f;uint32_t u;}v={n};return v.u;}
#endif
