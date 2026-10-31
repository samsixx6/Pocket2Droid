/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef P2D_IMPORT_H
#define P2D_IMPORT_H
#include "hw/core/cpu.h"
#include <dirent.h>
#include "hw/arm/p2d-net-setup-files.h"
#include "hw/arm/p2d-gles-files.h"

typedef struct P2DFolderFile {
    int fd;
    uint16_t mode;
    uint32_t position;
    char *name;
    unsigned gles_id; /* Zero is an ordinary shared-card handle. */
} P2DFolderFile;

typedef struct P2DFolderSearch {
    DIR *directory;
    char *pattern;
    bool setup_dir, setup_pending, setup_offered;
    bool gles_dir, gles_root;
    unsigned gles_next;
} P2DFolderSearch;

typedef struct P2DImport {
    CPUState *cpu;
    uint8_t *data;
    size_t size;
    bool handles[40], searches[40];
    uint16_t modes[40];
    uint32_t positions[40];
    unsigned trace_count;
    int root_fd;
    int move_state_fd, move_lock_fd;
    bool move_blocked;
    bool initialized;
    bool ce_seek_checked;
    P2DFolderFile files[40];
    P2DFolderSearch find[40];
    P2DNetSetupFiles setup;
    P2DGLESFiles gles;
} P2DImport;

bool p2d_import_load(P2DImport *s, CPUState *cpu, const char *path,
                     const char *shared_folder, const char *move_state,
                     const char *network_setup, const char *gles_assets,
                     const char *gles_renderer, Error **errp);
bool p2d_import_enabled(P2DImport *s);
void p2d_import_reset(P2DImport *s);
void p2d_import_cleanup(P2DImport *s);
uint32_t p2d_import_execute(P2DImport *s, uint32_t pb, uint32_t command);
#endif
