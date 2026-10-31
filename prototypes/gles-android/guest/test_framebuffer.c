#define _GNU_SOURCE
#include "framebuffer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void literal_rotations(void){
    const unsigned short sources[4][6]={{1,2,3,4,5,6},{1,2,3,4,5,6},{1,2,3,4,5,6},{1,2,3,4,5,6}};
    const unsigned short want[4][6]={{1,2,3,4,5,6},{5,3,1,6,4,2},{6,5,4,3,2,1},{2,4,6,1,3,5}};
    for(unsigned rot=0;rot<4;rot++){
        unsigned char storage[32];memset(storage,0xa5,sizeof(storage));
        P2DFBRaw raw={1,16,storage+4,2,8,3,2};
        unsigned w=rot&1?2:3,h=rot&1?3:2;
        assert(p2d_fb_copy(&raw,14,rot,sources[rot],12,w,h,w*2)&&"valid framebuffer copy must render");
        for(unsigned y=0;y<2;y++)assert(!memcmp(storage+4+y*8,want[rot]+y*3,6));
        for(unsigned i=0;i<32;i++)if(i<4||i>=18||i==10||i==11)assert(storage[i]==0xa5);
    }
}
static void rejected_inputs_do_not_write(void){
    unsigned short source[6]={1,2,3,4,5,6};unsigned char storage[32],before[32];
    memset(storage,0x5a,32);memcpy(before,storage,32);
    P2DFBRaw valid={1,16,storage+4,2,8,3,2};
    for(unsigned item=0;item<17;item++){
        P2DFBRaw raw=valid;unsigned cap=14,rot=0,w=3,h=2,stride=6,n=12;const void*src=source;
        switch(item){
        case 0:raw.format=2;break;case 1:raw.bpp=32;break;
        case 2:raw.x_stride=-2;break;case 3:raw.y_stride=5;break;
        case 4:raw.y_stride=7;break;case 5:raw.width=0;break;
        case 6:raw.height=2049;break;case 7:cap=13;break;
        case 8:rot=4;break;case 9:w=2;break;case 10:stride=5;break;
        case 11:n=11;break;case 12:raw.pixels=0;break;
        case 13:raw.pixels=(void*)(uintptr_t)(UINTPTR_MAX-5);break;
        case 14:src=raw.pixels;break;case 15:src=0;break;
        case 16:raw.y_stride=0x7ffffffe;break;
        }
        assert(!p2d_fb_copy(&raw,cap,rot,src,n,w,h,stride));
        assert(!memcmp(storage,before,32));
    }
}
/* Independent oracle: physically rotate complete matrices, no copy-plan math. */
static void rotate_matrix(unsigned short *out,const unsigned short*in,unsigned w,unsigned h){
    unsigned at=0;for(unsigned x=0;x<w;x++)for(unsigned y=h;y-->0;)out[at++]=in[y*w+x];
}
static void sizes_padding_and_alignment(void){
    unsigned short a[400],b[400];unsigned char src[2000],dst[4000];
    for(unsigned w=1;w<=17;w++)for(unsigned h=1;h<=17;h++)for(unsigned rot=0;rot<4;rot++){
        for(unsigned i=0;i<w*h;i++)a[i]=(unsigned short)(i*97+31);
        unsigned ow=w,oh=h;
        for(unsigned k=0;k<rot;k++){rotate_matrix(b,a,ow,oh);memcpy(a,b,w*h*2);unsigned t=ow;ow=oh;oh=t;}
        unsigned src_stride=w*2+2,dst_stride=ow*2+4;
        memset(src,0x42,sizeof(src));memset(dst,0xa7,sizeof(dst));
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
            unsigned short v=(unsigned short)((y*w+x)*97+31);memcpy(src+1+y*src_stride+x*2,&v,2);
        }
        P2DFBRaw raw={1,16,dst+2,2,(int)dst_stride,(int)ow,(int)oh};
        assert(p2d_fb_copy(&raw,(oh-1)*dst_stride+ow*2,rot,src+1,(h-1)*src_stride+w*2,w,h,src_stride));
        for(unsigned y=0;y<oh;y++){
            assert(!memcmp(dst+2+y*dst_stride,a+y*ow,ow*2));
            for(unsigned x=ow*2;x<dst_stride;x++)assert(dst[2+y*dst_stride+x]==0xa7);
        }
        assert(dst[0]==0xa7&&dst[1]==0xa7);
    }
}
static void protected_boundaries(void){
    long page=sysconf(_SC_PAGESIZE);assert(page>=4096);
    unsigned char*s=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    unsigned char*d=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(s!=MAP_FAILED&&d!=MAP_FAILED);assert(!mprotect(s+page,page,PROT_NONE)&&!mprotect(d+page,page,PROT_NONE));
    unsigned short values[6]={5,7,11,13,17,19};memcpy(s+page-12,values,12);
    P2DFBRaw raw={1,16,d+page-12,2,6,3,2};
    assert(p2d_fb_copy(&raw,12,0,s+page-12,12,3,2,6));assert(!memcmp(d+page-12,values,12));
    munmap(s,page*2);munmap(d,page*2);
}
int main(void){literal_rotations();rejected_inputs_do_not_write();sizes_padding_and_alignment();protected_boundaries();puts("framebuffer: literals, rejection/no-write, 1156 layout cases, protected boundaries pass");return 0;}
