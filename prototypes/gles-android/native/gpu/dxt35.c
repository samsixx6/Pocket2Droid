#include <stdint.h>
#include <stddef.h>
static unsigned u16(const unsigned char *p){return p[0]|((unsigned)p[1]<<8);}
static uint32_t u32(const unsigned char *p){return u16(p)|((uint32_t)u16(p+2)<<16);}
static unsigned five(unsigned n){n&=31;return (n<<3)|(n>>2);}
static unsigned six(unsigned n){n&=63;return (n<<2)|(n>>4);}
int p2d_dxt35_decode(unsigned fmt,unsigned w,unsigned h,const unsigned char *src,size_t n,unsigned char *out,size_t cap){
 if(fmt!=0x83f2&&fmt!=0x83f3)return -1;
 if(!src||!out||!w||!h||w>2048||h>2048||cap!=(size_t)w*h*4)return -1;
 size_t bw=(w+3)/4,bh=(h+3)/4,need=bw*bh*16;if(n<need||n>4194304)return -1;
 uintptr_t a=(uintptr_t)src,b=(uintptr_t)out;if(a>UINTPTR_MAX-n||b>UINTPTR_MAX-cap||(a<b+cap&&b<a+n))return -1;
 for(size_t by=0;by<bh;by++)for(size_t bx=0;bx<bw;bx++){
  const unsigned char *p=src+(by*bw+bx)*16;unsigned rgb[4][3],alpha[16];unsigned c0=u16(p+8),c1=u16(p+10);
  rgb[0][0]=five(c0>>11);rgb[0][1]=six(c0>>5);rgb[0][2]=five(c0);rgb[1][0]=five(c1>>11);rgb[1][1]=six(c1>>5);rgb[1][2]=five(c1);
  for(unsigned k=0;k<3;k++){rgb[2][k]=(2*rgb[0][k]+rgb[1][k])/3;rgb[3][k]=(rgb[0][k]+2*rgb[1][k])/3;}
  if(fmt==0x83f2){for(unsigned i=0;i<16;i++)alpha[i]=((p[i/2]>>(4*(i%2)))&15)*17;}
  else{unsigned table[8]={p[0],p[1]};if(p[0]>p[1]){for(unsigned i=2;i<8;i++)table[i]=((8-i)*p[0]+(i-1)*p[1])/7;}else{for(unsigned i=2;i<6;i++)table[i]=((6-i)*p[0]+(i-1)*p[1])/5;table[6]=0;table[7]=255;}
   uint64_t bits=0;for(unsigned i=0;i<6;i++)bits|=(uint64_t)p[2+i]<<(8*i);for(unsigned i=0;i<16;i++)alpha[i]=table[(bits>>(i*3))&7];}
  uint32_t selectors=u32(p+12);for(unsigned y=0;y<4&&by*4+y<h;y++)for(unsigned x=0;x<4&&bx*4+x<w;x++){unsigned i=y*4+x,c=(selectors>>(i*2))&3;unsigned char *d=out+((by*4+y)*w+bx*4+x)*4;for(unsigned k=0;k<3;k++)d[k]=(unsigned char)rgb[c][k];d[3]=(unsigned char)alpha[i];}
 }
 return 0;
}
