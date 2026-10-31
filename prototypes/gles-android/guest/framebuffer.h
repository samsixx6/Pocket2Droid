#ifndef P2D_FRAMEBUFFER_H
#define P2D_FRAMEBUFFER_H
/* Pure bounded RGB565 copy. Caller must own a current, writable OS mapping and
 * prove fullscreen visibility. Unsupported layouts decline before any write. */
typedef unsigned int P2DFBU32;
typedef int P2DFBI32;
typedef struct {
    unsigned short format, bpp;
    void *pixels;
    P2DFBI32 x_stride, y_stride, width, height;
} P2DFBRaw;

/* rotation is the number of clockwise turns of the logical image into physical
 * storage. capacity covers physical bytes from raw.pixels, including padding. */
int p2d_fb_copy(const P2DFBRaw *, P2DFBU32 capacity, unsigned rotation,
               const void *source, P2DFBU32 source_bytes,
               P2DFBU32 width, P2DFBU32 height, P2DFBU32 source_stride);
#endif
