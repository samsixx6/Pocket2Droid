#include "framebuffer_ce.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static struct {
    unsigned char memory[64];unsigned w,h,orientation;int failure,fail_enum;
    int dc_live,dc_releases,enum_incomplete,enum_physical,test_calls,transition,clip_calls;P2DFBOps ops;
} sim;
static void*fg(void){return sim.failure==1?(void*)9:(void*)7;}
static int visible(void*w){assert(w==(void*)7);return sim.failure!=2;}
static int rect(void*w,P2DFBRect*r){assert(w==(void*)7);*r=(P2DFBRect){0,0,(int)sim.w,(int)sim.h};if(sim.failure==3)r->right--;return sim.failure!=4;}
static int point(void*w,P2DFBPoint*p){assert(w==(void*)7&&p->x==0&&p->y==0);if(sim.failure==5)p->x=1;return sim.failure!=6;}
static int metric(int i){assert(i==0||i==1);return (int)(i?sim.h:sim.w)+(sim.failure==7);}
static void*dc(void*w){assert(w==(void*)7);if(sim.failure==8)return 0;sim.dc_live++;return(void*)8;}
static int release(void*w,void*d){assert(w==(void*)7&&d==(void*)8&&sim.dc_live==1);sim.dc_live--;sim.dc_releases++;return 1;}
static int clip(void*d,P2DFBRect*r){assert(d==(void*)8);*r=(P2DFBRect){0,0,(int)sim.w,(int)sim.h};if(sim.failure==9)r->left=1;if(++sim.clip_calls==2&&sim.transition==4)sim.failure=1;return sim.failure==10?3:sim.failure==11?0:2;}
static int mode(const P2DFBWchar*n,unsigned which,P2DFBMode*m){
    assert(!n&&which==0xffffffffu&&m->size==192);
    if(sim.fail_enum)return 0;
    m->fields=sim.failure==12||sim.enum_incomplete?0x007c0000:0x00800000;m->width=sim.w;m->height=sim.h;
    m->bpp=16;m->orientation=sim.orientation;
    if(sim.enum_physical){m->width=sim.h;m->height=sim.w;m->orientation=0;}
    return sim.failure!=13;
}
static int testmode(const P2DFBWchar*n,P2DFBMode*m,void*w,unsigned flags,void*p){
    assert(!n&&!w&&!p&&flags==2&&m->fields==0x00800000&&m->size==192);
    sim.test_calls++;m->orientation=sim.orientation;return sim.failure==14?-1:0;
}
static int escape(void*d,int code,int in,const void*p,int out,void*q){
    assert(d==(void*)8&&code==0x20001&&!in&&!p&&out==(int)sizeof(P2DFBRaw));
    P2DFBRaw*r=q;*r=(P2DFBRaw){1,16,sim.memory+8,2,8,3,2};
    if(sim.failure==15)r->bpp=32;if(sim.failure==16)r->pixels=0;
    if(sim.failure==17)r->width=4;return sim.failure!=18;
}
static unsigned memory(const void*p,P2DFBMemory*m,unsigned n){
    assert(p==sim.memory+8&&n==sizeof(*m));
    *m=(P2DFBMemory){sim.memory,sim.memory,4,64,0x1000,4,0};
    if(sim.failure==19)m->protect=0x104;if(sim.failure==20)m->protect=2;
    if(sim.failure==21)m->state=0x2000;if(sim.failure==22)m->region_size=20;
    if(sim.transition==1)sim.failure=1;if(sim.transition==2)sim.failure=9;if(sim.transition==3)sim.orientation=2;
    if(sim.failure==23)m->base=sim.memory+10;return sim.failure==24?0:n;
}
static void setup(unsigned orientation){
    memset(&sim,0,sizeof(sim));memset(sim.memory,0xa5,sizeof(sim.memory));
    sim.orientation=orientation;sim.w=orientation==1||orientation==4?2:3;sim.h=orientation==1||orientation==4?3:2;
    sim.ops=(P2DFBOps){dc,release,fg,visible,rect,point,metric,clip,escape,mode,0,memory};
}
int main(void){
    unsigned short src[]={1,2,3,4,5,6};
    const unsigned orientations[]={0,1,2,4};
    const unsigned short want[][6]={{1,2,3,4,5,6},{2,4,6,1,3,5},{6,5,4,3,2,1},{5,3,1,6,4,2}};
    for(unsigned k=0;k<4;k++){
        setup(orientations[k]);assert(p2d_fb_present(&sim.ops,(void*)7,src,12,sim.w,sim.h)&&"validated fullscreen presentation must draw");
        assert(!memcmp(sim.memory+8,want[k],6)&&!memcmp(sim.memory+16,want[k]+3,6));assert(!sim.dc_live&&sim.dc_releases==1);
    }
    for(int failure=1;failure<=24;failure++){
        setup(0);sim.failure=failure;
        if(failure==14){sim.fail_enum=1;sim.ops.test_mode=testmode;}
        assert(!p2d_fb_present(&sim.ops,(void*)7,src,12,3,2));
        for(unsigned i=0;i<sizeof(sim.memory);i++)assert(sim.memory[i]==0xa5);
        assert(!sim.dc_live);
    }
    setup(0);sim.fail_enum=1;sim.ops.test_mode=testmode;assert(p2d_fb_present(&sim.ops,(void*)7,src,12,3,2));
    /* Actual WM6: enumeration succeeds with native dimensions but omits the
     * orientation flag. The read-only CDS_TEST query returns current rotation. */
    setup(1);sim.enum_incomplete=1;sim.enum_physical=1;sim.ops.test_mode=testmode;
    assert(p2d_fb_present(&sim.ops,(void*)7,src,12,2,3)&&"dimensions-only enum must use current-orientation query");
    assert(sim.test_calls==2&&!memcmp(sim.memory+8,want[1],6)&&!memcmp(sim.memory+16,want[1]+3,6));
    setup(0);sim.enum_incomplete=1;sim.ops.test_mode=testmode;
    assert(p2d_fb_present(&sim.ops,(void*)7,src,12,3,2)&&sim.test_calls==2);
    setup(1);sim.enum_physical=1;sim.ops.test_mode=testmode;
    assert(p2d_fb_present(&sim.ops,(void*)7,src,12,2,3)&&sim.test_calls==2);
    setup(0);sim.failure=14;sim.ops.test_mode=testmode;
    assert(p2d_fb_present(&sim.ops,(void*)7,src,12,3,2)&&!sim.test_calls);
    setup(0);sim.enum_incomplete=1;assert(!p2d_fb_present(&sim.ops,(void*)7,src,12,3,2));
    for(unsigned i=0;i<sizeof(sim.memory);i++)assert(sim.memory[i]==0xa5);
    for(int transition=1;transition<=4;transition++){
        setup(0);sim.transition=transition;
        assert(!p2d_fb_present(&sim.ops,(void*)7,src,12,3,2)&&"foreground/clip/orientation transitions must decline before writes");
        for(unsigned i=0;i<sizeof(sim.memory);i++)assert(sim.memory[i]==0xa5);
        assert(!sim.dc_live&&sim.dc_releases==1);
    }
    setup(0);sim.orientation=3;assert(!p2d_fb_present(&sim.ops,(void*)7,src,12,3,2));
    setup(0);sim.ops.foreground=0;assert(!p2d_fb_present(&sim.ops,(void*)7,src,12,3,2));
    setup(0);assert(!p2d_fb_present(&sim.ops,0,src,12,3,2));
    puts("framebuffer CE: four rotations, 24 decline paths, dimensions-only/current-orientation fallback and handle lifetime pass");
}
