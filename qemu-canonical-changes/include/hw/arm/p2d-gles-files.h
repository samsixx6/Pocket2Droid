/* Private, optional VCEFS GLES transport. SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef P2D_GLES_FILES_H
#define P2D_GLES_FILES_H
#include "system/memory.h"

typedef struct P2DGLESFiles {
    bool initialized, blocked, root_stamp_valid, compact, compact_endpoint;
    int fd;
    int64_t root_mtime, root_mtime_nsec, root_ctime, root_ctime_nsec;
    void *library, *context;
    void *(*create)(void);
    void (*destroy)(void *);
    int (*execute)(void *, const unsigned char *, size_t,
                   const unsigned char **, size_t *);
    uint32_t sequence;
    MemoryRegion shm_ram,shm_control;
    bool shm_initialized;
    uint32_t shm_flags,shm_request_size,shm_response_size,shm_status,shm_done,shm_full_seq;
    uint32_t shm_copy_va,shm_copy_bytes,shm_present_ok,shm_frames;
    int64_t shm_begin,shm_flush,shm_last_end;
} P2DGLESFiles;

#endif
