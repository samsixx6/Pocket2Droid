#include "qemu/timer.h"
#include "hw/core/boards.h"
/* Shared-memory GLES transport; legacy file submissions remain available. */
#define SHM_BASE UINT64_C(0x60000000)
#define SHM_CONTROL UINT64_C(0x61000000)
#define SHM_SIZE 0xd00000u
#define SHM_RESPONSE 0x400000u
#define SHM_DESCRIPTOR 0xc01000u
#define SHM_REQUEST_MAX 0x400000u
#define SHM_MAGIC 0x314d4853u

typedef struct SHMSpan { uint64_t pa; uint32_t size, offset; } SHMSpan;
static GArray *shm_spans(P2DImport *s, uint32_t va, uint32_t size)
{
    uint64_t end = (uint64_t)va + size, current = va;
    GArray *spans = g_array_new(false, false, sizeof(SHMSpan));
    if (!size || end > UINT64_C(0x100000000)) goto fail;
    while (current < end) {
        TranslateForDebugResult tr;
        if (!cpu_translate_for_debug(s->cpu, current, &tr)) goto fail;
        uint64_t pa = tr.physaddr;
        uint64_t page = UINT64_C(1) << MIN(tr.lg_page_size, 12);
        uint32_t n = MIN(end - current, page - (pa & (page - 1)));
        if (pa < 0x30000000 || pa + n > 0x38000000 ||
            (pa < 0x36000000 && pa + n > 0x34000000)) goto fail;
        MemoryRegionSection sec = memory_region_find(get_system_memory(), pa, n);
        bool valid = sec.mr && memory_region_is_ram(sec.mr) &&
                     !memory_region_is_rom(sec.mr) &&
                     memory_region_get_fd(sec.mr) < 0 &&
                     int128_get64(sec.size) >= n;
        if (sec.mr) memory_region_unref(sec.mr);
        if (!valid) goto fail;
        SHMSpan span = {pa, n, current - va};
        g_array_append_val(spans, span);current += n;
    }
    return spans;
fail:
    g_array_unref(spans);return NULL;
}
static bool shm_transfer(GArray *spans, uint8_t *bytes, bool writing)
{
    for (unsigned i = 0; i < spans->len; i++) {
        SHMSpan *span = &g_array_index(spans, SHMSpan, i);
        if (address_space_rw(&address_space_memory, span->pa,
            MEMTXATTRS_UNSPECIFIED, bytes + span->offset, span->size, writing) != MEMTX_OK) return false;
    }
    return true;
}
static bool shm_full_pixels(P2DGLESFiles *g, uint32_t seq, uint32_t *w, uint32_t *h)
{
    uint8_t *b = memory_region_get_ram_ptr(&g->shm_ram);
    b += SHM_RESPONSE;
    if (!g->shm_response_size || seq != g->sequence || seq != g->shm_full_seq ||
        ldl_le_p(b) != P2D_REPLY_FULL || ldl_le_p(b + 4) != seq) return false;
    *w = ldl_le_p(b + 8);*h = ldl_le_p(b + 12);
    return *w && *h && *w <= 2048 && *h <= 2048 &&
           ldl_le_p(b + 16) == *w * *h * 2 &&
           g->shm_response_size >= 24u + *w * *h * 2;
}
static void shm_copy_pixels(P2DImport *s, uint32_t seq)
{
    P2DGLESFiles *g = &s->gles;uint32_t w,h;
    g->shm_status = EINVAL;
    if (!shm_full_pixels(g, seq, &w, &h) || g->shm_copy_bytes != w * h * 2) return;
    GArray *spans = shm_spans(s, g->shm_copy_va, g->shm_copy_bytes);
    if (!spans) return;
    uint8_t *src = memory_region_get_ram_ptr(&g->shm_ram);
    if (shm_transfer(spans, src + SHM_RESPONSE + 24, true)) g->shm_status = 0;
    g_array_unref(spans);
}
static void shm_present(P2DImport *s, uint32_t seq)
{
    P2DGLESFiles *g = &s->gles;uint32_t w,h;
    g->shm_present_ok = 0;
    if (!(g->shm_flags & 2) || !shm_full_pixels(g, seq, &w, &h)) return;
    uint8_t *ram = memory_region_get_ram_ptr(&g->shm_ram);
    uint8_t *d = ram + SHM_DESCRIPTOR;
    uint32_t va=ldl_le_p(d),cap=ldl_le_p(d+4),pw=ldl_le_p(d+8),ph=ldl_le_p(d+12);
    uint32_t stride=ldl_le_p(d+16),rot=ldl_le_p(d+20),lw=ldl_le_p(d+24),lh=ldl_le_p(d+28);
    if (!pw || !ph || pw > 2048 || ph > 2048 || rot > 3 || lw != w || lh != h ||
        stride < pw * 2 || stride > 16384 || (stride & 1) || (va & 1) ||
        ((rot & 1) ? (w != ph || h != pw) : (w != pw || h != ph))) return;
    uint32_t need = (ph - 1) * stride + pw * 2;
    if (need > cap || need > 32u * 1024 * 1024) return;
    GArray *spans = shm_spans(s, va, need);
    if (!spans) return;
    g_autofree uint8_t *dst = g_try_malloc(need);
    if (!dst || !shm_transfer(spans, dst, false)) {g_array_unref(spans);return;}
    const uint8_t *src = ram + SHM_RESPONSE + 24;
    for (unsigned y=0;y<ph;y++) for (unsigned x=0;x<pw;x++) {
        unsigned sx,sy;
        switch (rot) {
        case 0:sx=x;sy=y;break;
        case 1:sx=y;sy=pw-1-x;break;
        case 2:sx=pw-1-x;sy=ph-1-y;break;
        default:sx=ph-1-y;sy=x;break;
        }
        memcpy(dst+y*stride+x*2,src+(sy*w+sx)*2,2);
    }
    if (shm_transfer(spans,dst,true)) g->shm_present_ok=1;
    g_array_unref(spans);
}
static void p2d_shm_reset(P2DGLESFiles *g)
{
    g->shm_request_size=g->shm_response_size=g->shm_done=g->shm_full_seq=0;
    g->shm_copy_va=g->shm_copy_bytes=g->shm_present_ok=g->shm_status=0;
    g->shm_frames=0;g->shm_begin=g->shm_flush=g->shm_last_end=0;
}
static void shm_submit(P2DImport *s, uint32_t seq)
{
    P2DGLESFiles *g=&s->gles;
    uint8_t *request=memory_region_get_ram_ptr(&g->shm_ram);
    const unsigned char *response=NULL;size_t response_size=0;
    uint32_t size=g->shm_request_size;g->shm_response_size=g->shm_full_seq=0;g->shm_status=EINVAL;
    if (!(g->shm_flags & 1) || !g->execute || size < 24 || size > SHM_REQUEST_MAX) goto fail;
    if (seq==1) p2d_gles_context_reset(g);
    else if (!seq || !g->sequence || g->sequence==UINT32_MAX || seq!=g->sequence+1) goto fail;
    if (!p2d_gles_request_valid(request,size,seq)) goto fail;
    if (!g->context) {g->context=g->create();if(!g->context){g->shm_status=ENOMEM;goto fail;}}
    bool compact=ldl_le_p(request)==P2D_REQUEST_COMPACT;
    if (compact && !g->compact) goto fail;
    bool probe=!compact && seq==1 && size==96 && ldl_le_p(request+8)==1 && ldl_le_p(request+24)==P2D_COMPACT_PROBE;
    for(unsigned i=28;probe && i<96;i+=4) probe=ldl_le_p(request+i)==0;
    if(probe && g->compact_endpoint) stl_le_p(request+28,P2D_COMPACT_HOST);
    int result=g->execute(g->context,request,size,&response,&response_size);
    if((result!=0 && result!=1)||!p2d_gles_response_valid(response,response_size,seq,compact)||
       response_size>SHM_DESCRIPTOR-SHM_RESPONSE){g->shm_status=EIO;goto fail;}
    if(address_space_write(&address_space_memory,SHM_BASE+SHM_RESPONSE,MEMTXATTRS_UNSPECIFIED,response,response_size)!=MEMTX_OK){g->shm_status=EIO;goto fail;}
    if(probe && !ldl_le_p(response+20)) {
        size_t at=24u+ldl_le_p(response+16);
        g->compact=response_size==at+76 && ldl_le_p(response+at)==0x31514750 &&
            ldl_le_p(response+at+4)==P2D_COMPACT_PROBE && ldl_le_p(response+at+8)==1 &&
            ldl_le_p(response+at+12)==P2D_COMPACT_CAPABILITY;
    }
    g->sequence=g->shm_done=seq;g->shm_status=0;g->shm_response_size=response_size;
    if(!compact)g->shm_full_seq=seq;
    return;
fail:
    p2d_gles_context_reset(g);g->shm_done=0;
}
static uint64_t shm_read(void *opaque,hwaddr off,unsigned size)
{
    P2DImport *s=opaque;P2DGLESFiles *g=&s->gles;
    switch(off){case 0:return SHM_MAGIC;case 4:return g->shm_flags;case 8:return g->shm_request_size;
    case 16:return g->shm_response_size;case 20:return g->shm_status;case 24:return g->shm_done;
    case 48:return g->shm_present_ok;case 60:return g->shm_frames;default:return 0;}
}
static void shm_write(void *opaque,hwaddr off,uint64_t value,unsigned size)
{
    P2DImport *s=opaque;P2DGLESFiles *g=&s->gles;
    switch(off){
    case 8:g->shm_request_size=value;break;
    case 12:shm_submit(s,value);break;
    case 28:p2d_gles_context_reset(g);p2d_shm_reset(g);break;
    case 32:g->shm_copy_va=value;break;
    case 36:g->shm_copy_bytes=value;break;
    case 40:shm_copy_pixels(s,value);break;
    case 44:shm_present(s,value);break;
    case 56:{
        int64_t now=qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        if(value==1){g->shm_begin=now;g->shm_present_ok=0;}
        if(value==2)g->shm_flush=now;
        if(value==3){
            g->shm_frames++;
            if(getenv("P2D_SHM_STATS"))fprintf(stderr,"P2D_SHM_FRAME mode=%u frame=%u seq=%u now=%"PRId64" outside=%"PRId64" flush=%"PRId64" present=%"PRId64" native=%u\n",
                g->shm_flags,g->shm_frames,g->sequence,now,
                g->shm_last_end?g->shm_begin-g->shm_last_end:0,
                g->shm_flush-g->shm_begin,now-g->shm_flush,g->shm_present_ok);
            g->shm_last_end=now;
        }break;}
    }
}
static const MemoryRegionOps shm_ops={.read=shm_read,.write=shm_write,.endianness=DEVICE_LITTLE_ENDIAN,
    .valid={.min_access_size=4,.max_access_size=4},.impl={.min_access_size=4,.max_access_size=4}};
static void p2d_shm_init(P2DImport *s)
{
    P2DGLESFiles *g=&s->gles;
    if(!g->execute)return;
    /* RAM mappings are installed after SOC realization: use the configured property. */
    g_autofree char *retained_ram=object_property_get_str(OBJECT(qdev_get_machine()),"ram-path",NULL);
    if(retained_ram && *retained_ram){
        fprintf(stderr,"p2d GLES shared memory unavailable for retained RAM; using file transport\n");
        return;
    }
#ifdef __ANDROID__
    /* The Android candidate enables the validated endpoint without a UI change. */
    if(!getenv("P2D_SHM_ENABLE"))setenv("P2D_SHM_ENABLE","1",0);
    if(!getenv("P2D_SHM_PRESENT"))setenv("P2D_SHM_PRESENT","1",0);
#endif
    if(!getenv("P2D_SHM_ENABLE") || !strcmp(getenv("P2D_SHM_ENABLE"),"0"))return;
    g->shm_flags=(!getenv("P2D_SHM_TRANSPORT")||strcmp(getenv("P2D_SHM_TRANSPORT"),"0"))?1:0;
    if(g->shm_flags && getenv("P2D_SHM_PRESENT") && strcmp(getenv("P2D_SHM_PRESENT"),"0"))g->shm_flags|=2;
    memory_region_init_ram(&g->shm_ram,NULL,"p2d.prototype.gles-ram",SHM_SIZE,&error_fatal);
    memory_region_init_io(&g->shm_control,NULL,&shm_ops,s,"p2d.prototype.gles-control",4096);
    memory_region_add_subregion(get_system_memory(),SHM_BASE,&g->shm_ram);
    memory_region_add_subregion(get_system_memory(),SHM_CONTROL,&g->shm_control);
    g->shm_initialized=true;p2d_shm_reset(g);
    fprintf(stderr,"p2d GLES shared memory ready flags=%u\n",g->shm_flags);
}
static void p2d_shm_cleanup(P2DGLESFiles *g)
{
    if(!g->shm_initialized)return;
    memory_region_del_subregion(get_system_memory(),&g->shm_control);
    memory_region_del_subregion(get_system_memory(),&g->shm_ram);
    object_unparent(OBJECT(&g->shm_control));object_unparent(OBJECT(&g->shm_ram));
    g->shm_initialized=false;
}
