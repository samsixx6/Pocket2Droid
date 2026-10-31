/* Framebuffer copy for the advertised RGB565 EGL config. Use the existing
 * image-upload protocol so old native renderers remain compatible. */
#ifndef P2D_COPY_TEXTURE_H
#define P2D_COPY_TEXTURE_H
static U32 p2d_copy_texture_prepare(const U32 a[16],U32 out[16],U32 limit,U32 *size){
 U32 format=a[2],channels=format==0x1908?4:format==0x1907?3:format==0x190a?2:(format==0x1906||format==0x1909)?1:0;
 if(a[0]!=0xde1||!channels)return 0x500;
 if(a[1]>11||a[5]>(2048u>>a[1])||a[6]>(2048u>>a[1])||a[7])return 0x501;
 if((a[5]&&(a[5]&(a[5]-1)))||(a[6]&&(a[6]&(a[6]-1))))return 0x501;
 /* ES1.1 copies may drop framebuffer components, but cannot add alpha to
    the advertised RGB565/alpha0 color buffer (specification table3.9). */
 if(format!=0x1907&&format!=0x1909)return 0x502;
 *size=a[5]*a[6]*channels;if(*size>limit)return 0x505;
 for(U32 i=0;i<16;i++)out[i]=0;
 out[0]=a[0];out[1]=a[1];out[2]=format;out[3]=a[5];out[4]=a[6];out[6]=format;out[7]=0x1401;out[8]=*size!=0;
 return 0;
}
static void p2d_copy_texture_pixels(unsigned char *out,const unsigned short *pixels,U32 width,U32 height,const U32 a[16]){
 for(U32 row=0;row<a[6];row++)for(U32 col=0;col<a[5];col++){
  /* Unsigned addition handles negative GL origins without signed overflow.
     Out-of-frame source pixels have undefined GL values; use black/alpha1. */
  U32 x=a[3]+col,y=a[4]+row,p=x<width&&y<height?pixels[(height-1-y)*width+x]:0;
  unsigned char r=(p>>11)*255/31,g=((p>>5)&63)*255/63,b=(p&31)*255/31;
  switch(a[2]){
   case 0x1909:*out++=r;break;
   case 0x1907:*out++=r;*out++=g;*out++=b;break;
  }
 }
}
#endif
