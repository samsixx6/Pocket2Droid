#ifndef P2D_PRESENTATION_H
#define P2D_PRESENTATION_H

/* WinCE GDI ABI, also usable by the host ownership tests. No OS headers needed. */
typedef unsigned int P2DPresentU32;
typedef int P2DPresentI32;
typedef void *P2DPresentHandle;
typedef struct {
    P2DPresentU32 size;
    P2DPresentI32 width, height;
    unsigned short planes, bpp;
    P2DPresentU32 compression, image_bytes;
    P2DPresentI32 xppm, yppm;
    P2DPresentU32 used, important, masks[3];
} P2DPresentBitmap;

typedef struct {
    P2DPresentHandle (*get_dc)(P2DPresentHandle);
    int (*release_dc)(P2DPresentHandle, P2DPresentHandle);
    P2DPresentHandle (*create_dc)(P2DPresentHandle);
    P2DPresentHandle (*create_dib)(P2DPresentHandle, P2DPresentBitmap *,
                                 P2DPresentU32, void **, P2DPresentHandle,
                                 P2DPresentU32);
    P2DPresentHandle (*select_object)(P2DPresentHandle, P2DPresentHandle);
    int (*bit_blt)(P2DPresentHandle, int, int, int, int,
                   P2DPresentHandle, int, int, P2DPresentU32);
    int (*delete_object)(P2DPresentHandle);
    int (*delete_dc)(P2DPresentHandle);
} P2DPresentOps;

typedef struct {
    P2DPresentOps ops;
    P2DPresentHandle window, dc, bitmap, previous;
    void *bits;
    P2DPresentU32 width, height, stride;
} P2DPresentation;

/* init requires new or already-invalidated state. Ops are copied once. */
void p2d_present_init(P2DPresentation *, const P2DPresentOps *);
/* Returned bits remain valid until a different prepare, invalidate, or failed
 * blit. No window DC is retained. A failed prepare always returns null/zero. */
int p2d_present_prepare(P2DPresentation *, P2DPresentHandle window,
                        P2DPresentU32 width, P2DPresentU32 height,
                        void **bits, P2DPresentU32 *stride);
/* packed_pixels must hold width*height*2 readable bytes, or equal prepared
 * bits (already filled with the prepared stride). Errors invalidate backing. */
int p2d_present_blit(P2DPresentation *, P2DPresentHandle window,
                     const void *packed_pixels);
void p2d_present_invalidate(P2DPresentation *);

#endif
