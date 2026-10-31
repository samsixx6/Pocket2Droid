/* SPDX-License-Identifier: GPL-2.0-or-later
 * Standalone shared-frame producer; fcntl locks interoperate with FileChannel.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "hw/arm/p2d-frame.h"

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) { p[i] = v >> (i * 8); }
}
static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; ++i) { p[i] = v >> (i * 8); }
}

bool p2d_frame_open(P2DFrame *f, const char *path)
{
    struct stat st;
    int fd = open(path, O_RDWR | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { return false; }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 64 || (uintmax_t)st.st_size > SIZE_MAX) {
        close(fd); return false;
    }
    uint8_t *map = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return false; }
    *f = (P2DFrame){ .fd = fd, .map = map, .map_size = st.st_size };
    return true;
}

int p2d_frame_publish_status(P2DFrame *f, const uint32_t *pixels,
                       unsigned w, unsigned h, size_t stride)
{
    struct flock lock = { .l_type = F_WRLCK, .l_whence = SEEK_SET };
    if (!f->map || !pixels || !w || !h || (uint64_t)w * 4 > SIZE_MAX ||
        h > (SIZE_MAX - 64) / ((size_t)w * 4) || stride < (size_t)w * 4 ||
        (h > 1 && stride > (SIZE_MAX - (size_t)w * 4) / (h - 1)) ||
        fcntl(f->fd, F_SETLK, &lock) < 0) { return -1; }
    size_t bytes = 64 + (size_t)w * h * 4;
    if (bytes > f->map_size) {
        if (bytes > INT64_MAX || ftruncate(f->fd, bytes) < 0) { goto unlock; }
        uint8_t *map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, f->fd, 0);
        if (map == MAP_FAILED) { goto unlock; }
        munmap(f->map, f->map_size);
        f->map = map;
        f->map_size = bytes;
    }
    bool changed = f->width != w || f->height != h;
    for (unsigned y = 0; !changed && y < h; ++y) {
        const uint32_t *src = (const uint32_t *)((const uint8_t *)pixels + y * stride);
        for (unsigned x = 0; x < w; ++x) {
            uint32_t pixel;
            memcpy(&pixel, f->map + 64 + ((size_t)y * w + x) * 4, 4);
            if (pixel != (src[x] | 0xff000000u)) { changed = true; break; }
        }
    }
    if (changed) {
        for (unsigned y = 0; y < h; ++y) {
            const uint32_t *src = (const uint32_t *)((const uint8_t *)pixels + y * stride);
            for (unsigned x = 0; x < w; ++x) {
                put32(f->map + 64 + ((size_t)y * w + x) * 4, src[x] | 0xff000000u);
            }
        }
        memcpy(f->map, "P2DF", 4);
        put32(f->map + 4, 1);
        put32(f->map + 8, w);
        put32(f->map + 12, h);
        put32(f->map + 16, 1);
        put64(f->map + 24, ++f->sequence);
        f->width = w; f->height = h;
    }
    lock.l_type = F_UNLCK;
    if (fcntl(f->fd, F_SETLK, &lock) < 0) { return -1; }
    return changed;
unlock:
    lock.l_type = F_UNLCK;
    fcntl(f->fd, F_SETLK, &lock);
    return -1;
}

bool p2d_frame_publish(P2DFrame *f, const uint32_t *pixels,
                       unsigned w, unsigned h, size_t stride)
{
    return p2d_frame_publish_status(f, pixels, w, h, stride) > 0;
}

void p2d_frame_close(P2DFrame *f)
{
    if (f->map) { munmap(f->map, f->map_size); }
    if (f->fd >= 0) { close(f->fd); }
    *f = (P2DFrame){ .fd = -1 };
}
