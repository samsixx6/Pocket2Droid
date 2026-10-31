/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef P2D_FRAME_H
#define P2D_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Initial allocation for existing clients, not a resolution limit. */
#define P2D_FRAME_SIZE (64u + 1024u * 800u * 4u)
typedef struct P2DFrame {
    int fd;
    uint8_t *map;
    size_t map_size;
    uint32_t width, height;
    uint64_t sequence;
} P2DFrame;
bool p2d_frame_open(P2DFrame *frame, const char *path);
bool p2d_frame_publish(P2DFrame *frame, const uint32_t *pixels,
                       unsigned width, unsigned height, size_t stride);
/* -1: failed/busy, 0: unchanged, 1: published. Existing bool ABI retained. */
int p2d_frame_publish_status(P2DFrame *, const uint32_t *, unsigned, unsigned, size_t);
void p2d_frame_close(P2DFrame *frame);
#endif
