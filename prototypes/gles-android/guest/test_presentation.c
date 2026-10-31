#define _GNU_SOURCE
#include "presentation.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct Object {
    int kind;
    void *selected, *bits, *window;
    size_t bytes;
    struct Object *next;
} Object;
static Object stock = { .kind = 3 }, *memory_dcs;
static struct {
    unsigned get, release, create_dc, create_dib, select, blit, del_dc, del_obj;
    unsigned live_window, live_dc, live_bitmap;
    int fail_get, fail_dc, fail_dib, null_bits, fail_select, fail_restore, fail_blit;
    int fail_delete_dc, fail_delete_object;
    void *expected_window;
    unsigned width, height, stride;
    const unsigned char *expected;
} sim;

static void *get_dc(void *window) {
    sim.get++; assert(window == sim.expected_window);
    if (sim.fail_get) return NULL;
    Object *o = calloc(1, sizeof(*o)); assert(o);
    o->kind = 1; o->window = window; sim.live_window++; return o;
}
static int release_dc(void *window, void *handle) {
    Object *o = handle; assert(o && o->kind == 1 && o->window == window);
    sim.release++; sim.live_window--; free(o); return 1;
}
static void *create_dc(void *handle) {
    Object *w = handle; assert(w && w->kind == 1); sim.create_dc++;
    if (sim.fail_dc) return NULL;
    Object *o = calloc(1, sizeof(*o)); assert(o);
    o->kind = 2; o->selected = &stock; o->next = memory_dcs; memory_dcs = o;
    sim.live_dc++; return o;
}
static void *create_dib(void *handle, P2DPresentBitmap *b, unsigned usage,
                        void **bits, void *section, unsigned offset) {
    Object *w = handle; assert(w && w->kind == 1); sim.create_dib++;
    assert(!usage && !section && !offset && b->size == 40);
    assert(b->width > 0 && b->height < 0 && b->planes == 1 && b->bpp == 16);
    assert(b->compression == 3 && b->masks[0] == 0xf800 &&
           b->masks[1] == 0x7e0 && b->masks[2] == 31);
    assert(b->xppm == 0 && b->yppm == 0 && !b->used && !b->important);
    sim.width = (unsigned)b->width; sim.height = (unsigned)-b->height;
    sim.stride = (sim.width * 2 + 3) & ~3u;
    assert(b->image_bytes == sim.stride * sim.height);
    if (sim.fail_dib) return NULL;
    Object *o = calloc(1, sizeof(*o)); assert(o);
    o->kind = 4; o->bytes = b->image_bytes;
    o->bits = mmap(NULL, o->bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(o->bits != MAP_FAILED); memset(o->bits, 0xa5, o->bytes);
    *bits = sim.null_bits ? NULL : o->bits; sim.live_bitmap++; return o;
}
static void *select_object(void *handle, void *selected) {
    Object *o = handle, *b = selected; assert(o && o->kind == 2);
    assert(b && (b->kind == 3 || b->kind == 4)); sim.select++;
    if (b == &stock && sim.fail_restore)
        return sim.fail_restore == 2 ? (void *)-1 : NULL;
    if (b != &stock && sim.fail_select)
        return sim.fail_select == 2 ? (void *)-1 : NULL;
    void *prior = o->selected; o->selected = selected; return prior;
}
static int bit_blt(void *dst, int x, int y, int width, int height,
                    void *source, int sx, int sy, unsigned rop) {
    Object *w = dst, *dc = source, *b = dc->selected;
    assert(w && w->kind == 1 && dc->kind == 2 && b && b->kind == 4);
    assert(!x && !y && !sx && !sy && rop == 0xcc0020);
    assert(width == (int)sim.width && height == (int)sim.height); sim.blit++;
    if (sim.expected) for (unsigned row = 0; row < sim.height; row++) {
        assert(!memcmp((char *)b->bits + row * sim.stride,
                       sim.expected + row * sim.width * 2, sim.width * 2));
        for (unsigned col = sim.width * 2; col < sim.stride; col++)
            assert(((unsigned char *)b->bits)[row * sim.stride + col] == 0xa5);
    }
    return !sim.fail_blit;
}
static int delete_object(void *handle) {
    Object *b = handle; assert(b && b->kind == 4);
    for (Object *o = memory_dcs; o; o = o->next) assert(o->selected != b);
    if (sim.fail_delete_object) return 0;
    sim.del_obj++; sim.live_bitmap--; assert(!munmap(b->bits, b->bytes)); free(b); return 1;
}
static int delete_dc(void *handle) {
    Object *dc = handle; assert(dc && dc->kind == 2);
    if (sim.fail_delete_dc) return 0;
    Object **p = &memory_dcs; while (*p != dc) { assert(*p); p = &(*p)->next; }
    *p = dc->next; sim.del_dc++; sim.live_dc--; free(dc); return 1;
}
static const P2DPresentOps ops = {get_dc, release_dc, create_dc, create_dib,
                                 select_object, bit_blt, delete_object, delete_dc};
static void clean(void) {
    assert(!sim.live_window && !sim.live_dc && !sim.live_bitmap && !memory_dcs);
}
static void reset(P2DPresentation *p) {
    clean(); memset(&sim, 0, sizeof(sim)); sim.expected_window = (void *)(uintptr_t)10;
    p2d_present_init(p, &ops);
}
static void prepare(P2DPresentation *p, unsigned width, unsigned height,
                     void **bits, unsigned *stride) {
    assert(p2d_present_prepare(p, sim.expected_window, width, height, bits, stride));
    assert(*bits && *stride == ((width * 2 + 3) & ~3u));
    assert(!sim.live_window && sim.live_dc == 1 && sim.live_bitmap == 1);
}
static void repeated_direct_frames(void) {
    P2DPresentation p; void *bits, *same; unsigned stride, same_stride;
    reset(&p); prepare(&p, 480, 640, &bits, &stride);
    assert(!mprotect(bits, stride * 640, PROT_READ));
    for (unsigned i = 0; i < 100; i++) {
        prepare(&p, 480, 640, &same, &same_stride); assert(same == bits);
        /* Read-only backing makes ANY redundant same-buffer copy fail. */
        assert(p2d_present_blit(&p, sim.expected_window, bits));
    }
    assert(sim.create_dc == 1 && sim.create_dib == 1 && sim.select == 1);
    assert(sim.get == 101 && sim.release == 101 && sim.blit == 100);
    assert(!mprotect(bits, stride * 640, PROT_READ | PROT_WRITE));
    p2d_present_invalidate(&p); clean();
    assert(sim.select == 2 && sim.del_obj == 1 && sim.del_dc == 1);
    p2d_present_invalidate(&p); clean(); puts("PASS repeated/direct: 100 frames, 1 DIB/DC, 0 pixel-copy writes");
}
static void copy_rows(void) {
    for (unsigned width = 1; width <= 9; width++) for (unsigned offset = 0; offset < 4; offset++) {
        P2DPresentation p; void *bits; unsigned stride;
        unsigned char source[256]; for (unsigned i = 0; i < sizeof(source); i++) source[i] = (unsigned char)(i * 17 + 3);
        reset(&p); prepare(&p, width, 3, &bits, &stride);
        sim.expected = source + offset;
        assert(p2d_present_blit(&p, sim.expected_window, source + offset));
        p2d_present_invalidate(&p); clean();
    }
    puts("PASS packed rows: odd/even widths, padding, all source alignments");
}
static void changes(void) {
    P2DPresentation p; void *bits; unsigned stride;
    reset(&p); prepare(&p, 4, 3, &bits, &stride);
    prepare(&p, 8, 4, &bits, &stride);
    assert(sim.del_dc == 1 && sim.del_obj == 1 && sim.create_dib == 2);
    sim.expected_window = (void *)(uintptr_t)11;
    prepare(&p, 8, 4, &bits, &stride);
    assert(sim.del_dc == 2 && sim.del_obj == 2 && sim.create_dib == 3);
    assert(p2d_present_blit(&p, sim.expected_window, bits));
    p2d_present_invalidate(&p); clean(); puts("PASS dimensions/window recreate and exact cleanup");
}
static void failures(void) {
    for (unsigned fail = 0; fail < 7; fail++) {
        P2DPresentation p; void *bits = (void *)(uintptr_t)1; unsigned stride = 1;
        reset(&p);
        switch (fail) {
        case 0: sim.fail_get = 1; break;
        case 1: sim.fail_dc = 1; break;
        case 2: sim.fail_dib = 1; break;
        case 3: sim.null_bits = 1; break;
        case 4: sim.fail_select = 1; break;
        case 5: p.ops.bit_blt = NULL; break;
        case 6: sim.fail_select = 2; break;
        }
        assert(!p2d_present_prepare(&p, sim.expected_window, 8, 4, &bits, &stride));
        assert(!bits && !stride && !p.bits); clean();
        p2d_present_invalidate(&p); clean();
    }
    for (unsigned fail = 0; fail < 4; fail++) {
        P2DPresentation p; void *bits; unsigned stride;
        reset(&p); prepare(&p, 8, 4, &bits, &stride);
        if (fail == 0) sim.fail_get = 1;
        if (fail == 1) sim.fail_blit = 1;
        if (fail == 2) sim.expected_window = (void *)(uintptr_t)11;
        assert(!p2d_present_blit(&p, sim.expected_window, fail == 3 ? NULL : bits));
        assert(!p.bits); clean();
    }
    P2DPresentation p; void *bits; unsigned stride;
    reset(&p); prepare(&p, 8, 4, &bits, &stride); sim.fail_restore = 1;
    p2d_present_invalidate(&p); clean(); assert(!p.bits);
    reset(&p); prepare(&p, 8, 4, &bits, &stride); sim.fail_restore = 2;
    p2d_present_invalidate(&p); clean(); assert(!p.bits);
    puts("PASS allocation/select/GetDC/BitBlt failures and restore-failure ownership");
}
static void deletion_retry(void) {
    for (unsigned fail = 0; fail < 3; fail++) {
        P2DPresentation p; void *bits; unsigned stride;
        reset(&p); prepare(&p, 8, 4, &bits, &stride);
        sim.fail_delete_dc = fail != 1;
        sim.fail_delete_object = fail == 1;
        sim.fail_restore = fail == 2;
        p2d_present_invalidate(&p); assert(!p.bits && !p.window);
        assert(p.dc || p.bitmap);
        assert(!p2d_present_prepare(&p, sim.expected_window, 8, 4, &bits, &stride));
        assert(!bits && !stride && sim.create_dib == 1 && sim.create_dc == 1);
        sim.fail_delete_dc = sim.fail_delete_object = sim.fail_restore = 0;
        p2d_present_invalidate(&p); clean();
        prepare(&p, 8, 4, &bits, &stride); assert(sim.create_dib == 2);
        p2d_present_invalidate(&p); clean();
    }
    puts("PASS failed deletion retains ownership, blocks reallocation, and retries cleanup");
}
static void bounds(void) {
    const unsigned sizes[][2] = {{0, 1}, {1, 0}, {2049, 1}, {1, 2049}, {~0u, ~0u}};
    P2DPresentation p; void *bits; unsigned stride;
    reset(&p);
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        bits = (void *)(uintptr_t)1; stride = 1;
        assert(!p2d_present_prepare(&p, sim.expected_window, sizes[i][0], sizes[i][1], &bits, &stride));
        assert(!bits && !stride); clean();
    }
    prepare(&p, 2048, 2048, &bits, &stride); p2d_present_invalidate(&p); clean();
    assert(sim.create_dib == 1); puts("PASS dimensions bounded, maximum RGB565 DIB size accepted");
    prepare(&p, 8, 4, &bits, &stride);
    assert(!p2d_present_prepare(&p, NULL, 8, 4, &bits, &stride));
    assert(!bits && !stride && !p.bits); clean();
    prepare(&p, 8, 4, &bits, &stride);
    assert(!p2d_present_prepare(&p, sim.expected_window, 0, 4, &bits, &stride));
    assert(!bits && !stride && !p.bits); clean();
}
int main(void) {
    assert(sizeof(P2DPresentU32) == 4 && sizeof(P2DPresentBitmap) == 52);
    repeated_direct_frames(); copy_rows(); changes(); failures(); deletion_retry(); bounds();
    puts("PASS presentation lifecycle and pixels"); return 0;
}
