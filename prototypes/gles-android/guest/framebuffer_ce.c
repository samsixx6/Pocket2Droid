#include "framebuffer_ce.h"
typedef __UINTPTR_TYPE__ P2DFBCEAddress;
typedef char P2DFBModeMustMatchCE[sizeof(P2DFBMode)==192?1:-1];
#if __SIZEOF_POINTER__ == 4
typedef char P2DFBRawMustMatchCE[sizeof(P2DFBRaw)==24?1:-1];
typedef char P2DFBMemoryMustMatchCE[sizeof(P2DFBMemory)==28?1:-1];
#endif
static int fb_rect_full(const P2DFBRect*r,unsigned w,unsigned h){
    return !r->left&&!r->top&&r->right==(int)w&&r->bottom==(int)h;
}
static int fb_window_full(const P2DFBOps*o,void*window,unsigned w,unsigned h){
    P2DFBRect rect;P2DFBPoint origin={0,0};
    return o->foreground()==window&&o->visible(window)&&
           o->client_rect(window,&rect)&&fb_rect_full(&rect,w,h)&&
           o->metrics(0)==(int)w&&o->metrics(1)==(int)h&&
           o->client_to_screen(window,&origin)&&!origin.x&&!origin.y;
}
static void fb_mode_clear(P2DFBMode*m){
    volatile unsigned char*p=(volatile unsigned char*)m;
    for(unsigned i=0;i<sizeof(*m);i++)p[i]=0;
    m->size=sizeof(*m);
}
static int fb_rotation(const P2DFBOps*o,unsigned w,unsigned h,unsigned*rotation){
    P2DFBMode mode;
    fb_mode_clear(&mode);
    int current_mode=o->enum_mode&&o->enum_mode(0,0xffffffffu,&mode)&&
        mode.size>=sizeof(mode)&&(mode.fields&0x00800000u)&&
        mode.width==w&&mode.height==h&&mode.bpp==16;
    if(!current_mode){
        if(!o->test_mode)return 0;
        fb_mode_clear(&mode);mode.fields=0x00800000u;
        /* CDS_TEST is the documented read-only current-orientation query.
         * Never call the mode-setting form or alter the guest's display mode. */
        if(o->test_mode(0,&mode,0,2,0)!=0)return 0;
    }
    switch(mode.orientation){case 0:*rotation=0;break;case 1:*rotation=3;break;
        case 2:*rotation=2;break;case 4:*rotation=1;break;default:return 0;}
    return 1;
}
int p2d_fb_present(const P2DFBOps*o,void*window,const void*source,unsigned bytes,unsigned w,unsigned h){
    if(!o||!window||window==(void*)-1||!source||!w||!h||w>2048||h>2048||
       !o->get_dc||!o->release_dc||!o->foreground||!o->visible||
       !o->client_rect||!o->client_to_screen||!o->metrics||!o->clip_box||
       !o->escape||!o->query_memory||(!o->enum_mode&&!o->test_mode))return 0;
    if(!fb_window_full(o,window,w,h))return 0;
    void*dc=o->get_dc(window);if(!dc||dc==(void*)-1)return 0;
    int result=0;P2DFBRect clip;unsigned rotation,current_rotation;
    if(o->clip_box(dc,&clip)!=2||!fb_rect_full(&clip,w,h)||!fb_rotation(o,w,h,&rotation))goto done;
    P2DFBRaw raw={0};P2DFBMemory memory={0};
    if(o->escape(dc,0x20001,0,0,sizeof(raw),&raw)<=0||!raw.pixels||
       raw.format!=1||raw.bpp!=16||raw.width<=0||raw.height<=0||
       raw.width>2048||raw.height>2048||raw.x_stride!=2||
       raw.y_stride<raw.width*2||raw.y_stride>16384||(raw.y_stride&1))goto done;
    if(o->query_memory(raw.pixels,&memory,sizeof(memory))<sizeof(memory)||
       memory.state!=0x1000||(memory.protect&0x100)||
       !(memory.protect&(4u|8u|0x40u|0x80u)))goto done;
    P2DFBCEAddress base=(P2DFBCEAddress)memory.base,at=(P2DFBCEAddress)raw.pixels;
    if(at<base||memory.region_size>~(P2DFBCEAddress)0-base||at-base>=memory.region_size)goto done;
    unsigned capacity=memory.region_size-(unsigned)(at-base);
    /* No framebuffer/DC/mode snapshot is cached across frames. Recheck the
     * visible region after the driver and mapping queries before any store. */
    if(!fb_rotation(o,w,h,&current_rotation)||current_rotation!=rotation||
       !fb_window_full(o,window,w,h)||o->clip_box(dc,&clip)!=2||
       !fb_rect_full(&clip,w,h)||o->foreground()!=window)goto done;
    result=p2d_fb_copy(&raw,capacity,rotation,source,bytes,w,h,w*2);
done:
    o->release_dc(window,dc);return result;
}
