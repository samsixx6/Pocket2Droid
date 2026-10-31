#include "framebuffer.h"
typedef __UINTPTR_TYPE__ P2DFBAddress;
typedef unsigned short P2DFBHalf __attribute__((__may_alias__));
typedef unsigned int P2DFBWord __attribute__((__may_alias__));
typedef char P2DFBWordMustBe32Bits[sizeof(P2DFBWord)==4?1:-1];

static void fb_copy_row(unsigned char*d,const unsigned char*s,unsigned n){
    if(!(((P2DFBAddress)d^(P2DFBAddress)s)&3u)){
        while(n&&((P2DFBAddress)d&3u)){*d++=*s++;n--;}
        while(n>=16){
            P2DFBWord*to=(P2DFBWord*)d;const P2DFBWord*from=(const P2DFBWord*)s;
            to[0]=from[0];to[1]=from[1];to[2]=from[2];to[3]=from[3];d+=16;s+=16;n-=16;
        }
        while(n>=4){*(P2DFBWord*)d=*(const P2DFBWord*)s;d+=4;s+=4;n-=4;}
    }
    while(n--)*d++=*s++;
}
static unsigned fb_pixel(P2DFBAddress address){
    if(!(address&1u))return *(const P2DFBHalf*)address;
    const unsigned char*p=(const unsigned char*)address;return p[0]|(unsigned)p[1]<<8;
}
int p2d_fb_copy(const P2DFBRaw*r,unsigned capacity,unsigned rotation,
               const void*source,unsigned source_bytes,unsigned w,unsigned h,unsigned source_stride){
    enum {MAX_DIM=2048,MAX_STRIDE=16384};
    if(!r||!source||!r->pixels||r->format!=1||r->bpp!=16||rotation>3||
       !w||!h||w>MAX_DIM||h>MAX_DIM||r->width<=0||r->height<=0||
       r->width>MAX_DIM||r->height>MAX_DIM||r->x_stride!=2||
       r->y_stride<r->width*2||r->y_stride>MAX_STRIDE||(r->y_stride&1)||
       source_stride<w*2||source_stride>MAX_STRIDE)return 0;
    unsigned pw=(unsigned)r->width,ph=(unsigned)r->height,stride=(unsigned)r->y_stride;
    if((rotation&1)?(w!=ph||h!=pw):(w!=pw||h!=ph))return 0;
    unsigned src_need=(h-1)*source_stride+w*2,dst_need=(ph-1)*stride+pw*2;
    if(source_bytes<src_need||capacity<dst_need)return 0;
    P2DFBAddress src=(P2DFBAddress)source,dst=(P2DFBAddress)r->pixels,limit=~(P2DFBAddress)0;
    if((dst&1u)||src_need>limit-src||dst_need>limit-dst)return 0;
    if(dst>=src?(dst-src<src_need):(src-dst<dst_need))return 0;
    /* All metadata, extents and overlap are checked before the first store. */
    for(unsigned y=0;y<ph;y++){
        unsigned char*d=(unsigned char*)r->pixels+y*stride;
        if(!rotation){fb_copy_row(d,(const unsigned char*)source+y*source_stride,pw*2);continue;}
        P2DFBAddress at;P2DFBI32 step;
        if(rotation==1){at=src+(pw-1)*source_stride+y*2;step=-(P2DFBI32)source_stride;}
        else if(rotation==2){at=src+(ph-1-y)*source_stride+(pw-1)*2;step=-2;}
        else {at=src+(ph-1-y)*2;step=(P2DFBI32)source_stride;}
        unsigned x=0;
        if(((P2DFBAddress)d&3u)&&pw){*(P2DFBHalf*)d=(P2DFBHalf)fb_pixel(at);d+=2;at+=(P2DFBAddress)step;x++;}
        for(;x+1<pw;x+=2){
            unsigned lo=fb_pixel(at);at+=(P2DFBAddress)step;
            unsigned hi=fb_pixel(at);at+=(P2DFBAddress)step;
            *(P2DFBWord*)d=lo|(hi<<16);d+=4;
        }
        if(x<pw)*(P2DFBHalf*)d=(P2DFBHalf)fb_pixel(at);
    }
    return 1;
}
