#include "presentation.h"

typedef char P2DPresentWordMustBe32Bits[sizeof(P2DPresentU32) == 4 ? 1 : -1];
typedef char P2DPresentBitmapMustMatchWinCE[sizeof(P2DPresentBitmap) == 52 ? 1 : -1];
typedef __UINTPTR_TYPE__ P2DPresentAddress;
/* may_alias allows aligned words to copy arbitrary packed pixel storage. */
typedef P2DPresentU32 P2DPresentCopyWord __attribute__((__may_alias__));

static int valid_handle(P2DPresentHandle h)
{
    return h && h != (P2DPresentHandle)-1;
}

static int valid_ops(const P2DPresentOps *o)
{
    return o->get_dc && o->release_dc && o->create_dc && o->create_dib &&
           o->select_object && o->bit_blt && o->delete_object && o->delete_dc;
}

void p2d_present_init(P2DPresentation *p, const P2DPresentOps *o)
{
    if (!p) return;
    p->ops.get_dc = o ? o->get_dc : 0;
    p->ops.release_dc = o ? o->release_dc : 0;
    p->ops.create_dc = o ? o->create_dc : 0;
    p->ops.create_dib = o ? o->create_dib : 0;
    p->ops.select_object = o ? o->select_object : 0;
    p->ops.bit_blt = o ? o->bit_blt : 0;
    p->ops.delete_object = o ? o->delete_object : 0;
    p->ops.delete_dc = o ? o->delete_dc : 0;
    p->window = p->dc = p->bitmap = p->previous = 0;
    p->bits = 0;
    p->width = p->height = p->stride = 0;
}

void p2d_present_invalidate(P2DPresentation *p)
{
    if (!p) return;
    /* Never leave a writable frame pointer published during teardown. */
    p->bits = 0;
    p->window = 0;
    p->width = p->height = p->stride = 0;
    if (p->dc && p->bitmap && p->previous) {
        if (valid_handle(p->ops.select_object(p->dc, p->previous)))
            p->previous = 0;
        else {
            /* Deleting a DC releases its selection. Do this before attempting
             * to delete a bitmap whose original selection could not restore. */
            if (!p->ops.delete_dc(p->dc)) return;
            p->dc = p->previous = 0;
        }
    }
    if (p->bitmap && p->ops.delete_object(p->bitmap)) p->bitmap = 0;
    if (p->dc && p->ops.delete_dc(p->dc)) p->dc = 0;
    /* Failed deletion retains its owned handle for a later cleanup attempt;
     * prepare cannot reuse or overwrite this incomplete state. */
}

int p2d_present_prepare(P2DPresentation *p, P2DPresentHandle window,
                        P2DPresentU32 width, P2DPresentU32 height,
                        void **bits, P2DPresentU32 *stride)
{
    P2DPresentHandle target;
    P2DPresentBitmap b;
    void *storage = 0;
    if (bits) *bits = 0;
    if (stride) *stride = 0;
    if (!p) return 0;
    if (!bits || !stride || !valid_handle(window) || !width || !height ||
        width > 2048 || height > 2048 || !valid_ops(&p->ops)) {
        p2d_present_invalidate(p);
        return 0;
    }
    if (p->bits && p->window == window && p->width == width && p->height == height) {
        *bits = p->bits;
        *stride = p->stride;
        return 1;
    }
    p2d_present_invalidate(p);
    if (p->dc || p->bitmap) return 0;
    target = p->ops.get_dc(window);
    if (!valid_handle(target)) return 0;
    p->dc = p->ops.create_dc(target);
    if (!valid_handle(p->dc)) {
        p->dc = 0;
        p->ops.release_dc(window, target);
        return 0;
    }
    b.size = 40;
    b.width = (P2DPresentI32)width;
    b.height = -(P2DPresentI32)height;
    b.planes = 1;
    b.bpp = 16;
    b.compression = 3;
    b.image_bytes = ((width * 2 + 3) & ~3u) * height;
    b.xppm = b.yppm = 0;
    b.used = b.important = 0;
    b.masks[0] = 0xf800;
    b.masks[1] = 0x7e0;
    b.masks[2] = 31;
    p->bitmap = p->ops.create_dib(target, &b, 0, &storage, 0, 0);
    p->ops.release_dc(window, target);
    if (!valid_handle(p->bitmap)) p->bitmap = 0;
    if (!p->bitmap || !storage) {
        p2d_present_invalidate(p);
        return 0;
    }
    p->previous = p->ops.select_object(p->dc, p->bitmap);
    if (!valid_handle(p->previous)) {
        p->previous = 0;
        p2d_present_invalidate(p);
        return 0;
    }
    p->window = window;
    p->width = width;
    p->height = height;
    p->stride = (width * 2 + 3) & ~3u;
    p->bits = storage;
    *bits = storage;
    *stride = p->stride;
    return 1;
}

static void copy_row(unsigned char *dst, const unsigned char *src, P2DPresentU32 bytes)
{
    /* ARMv4 requires aligned word accesses. Incompatible alignments take the
     * byte path; aligned RGB565 rows use words without a libc dependency. */
    if (!(((P2DPresentAddress)dst ^ (P2DPresentAddress)src) & 3u)) {
        while (bytes && ((P2DPresentAddress)dst & 3u)) {
            *dst++ = *src++;
            bytes--;
        }
        while (bytes >= 4) {
            *(P2DPresentCopyWord *)dst = *(const P2DPresentCopyWord *)src;
            dst += 4;
            src += 4;
            bytes -= 4;
        }
    }
    while (bytes--) *dst++ = *src++;
}

int p2d_present_blit(P2DPresentation *p, P2DPresentHandle window,
                     const void *packed_pixels)
{
    P2DPresentHandle target;
    int result;
    if (!p) return 0;
    if (!p->bits || p->window != window || !packed_pixels) {
        p2d_present_invalidate(p);
        return 0;
    }
    target = p->ops.get_dc(window);
    if (!valid_handle(target)) {
        p2d_present_invalidate(p);
        return 0;
    }
    if (packed_pixels != p->bits) {
        const unsigned char *src = packed_pixels;
        unsigned char *dst = p->bits;
        P2DPresentU32 row_bytes = p->width * 2;
        for (P2DPresentU32 row = 0; row < p->height; row++) {
            copy_row(dst, src, row_bytes);
            src += row_bytes;
            dst += p->stride;
        }
    }
    result = p->ops.bit_blt(target, 0, 0, (int)p->width, (int)p->height,
                            p->dc, 0, 0, 0xcc0020);
    p->ops.release_dc(window, target);
    if (!result) p2d_present_invalidate(p);
    return result != 0;
}
