/* Known Device Emulator VCEFSD FILE_END subtraction compatibility correction.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef P2D_CE_SEEK_FIX_H
#define P2D_CE_SEEK_FIX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define P2D_CE_SEEK_WORDS 46u
#define P2D_CE_SEEK_BYTES (P2D_CE_SEEK_WORDS * 4u)
#define P2D_CE_SEEK_CHANGE 0x70u
#define P2D_CE_SEEK_SUB UINT32_C(0xe0435006)
#define P2D_CE_SEEK_ADD UINT32_C(0xe0835006)

/* Complete captured FSD_SetFilePointer control flow, including its FCB magic.
 * Only four external BL displacements and the relocated critical-section
 * pointer vary. Unknown code is deliberately left untouched.
 */
static inline bool p2d_ce_seek_signature(const uint8_t *code, size_t size,
                                         bool *already_fixed)
{
    static const uint32_t expected[P2D_CE_SEEK_WORDS] = {
        0xe92d43f0, 0xe1a07003, 0xe1a08002, 0xe1a06001,
        0xe1a04000, 0xe59f9098, 0xe3e05000, 0xe5990000,
        0xeb000000, 0xe3580000, 0x13a03000, 0x15883000,
        0xe3540000, 0x0a000018, 0xe594e000, 0xe59f206c,
        0xe15e0002, 0x1a000014, 0xe3570000, 0x0a00000e,
        0xe3570001, 0x0a000008, 0xe3570002, 0x0a000002,
        0xe3a00057, 0xeb000000, 0xea000009, 0xe5943014,
        P2D_CE_SEEK_SUB, 0xe5845010, 0xea000005, 0xe5943010,
        P2D_CE_SEEK_ADD, 0xe5845010, 0xea000001, 0xe5846010,
        0xe1a05006, 0xe3a00000, 0xeb000000, 0xe5990000,
        0xeb000000, 0xe1a00005, 0xe8bd43f0, 0xe12fff1e,
        0x64664644, 0
    };
    if (size < P2D_CE_SEEK_BYTES) {
        return false;
    }
    for (size_t i = 0; i < P2D_CE_SEEK_WORDS; ++i) {
        const uint8_t *p = code + i * 4;
        uint32_t word = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        if (i == 8 || i == 25 || i == 38 || i == 40) {
            if ((word & 0xff000000u) != 0xeb000000u) {
                return false;
            }
        } else if (i == 45) {
            if (!word || (word & 3)) {
                return false;
            }
        } else if (i == P2D_CE_SEEK_CHANGE / 4) {
            if (word != P2D_CE_SEEK_SUB && word != P2D_CE_SEEK_ADD) {
                return false;
            }
            *already_fixed = word == P2D_CE_SEEK_ADD;
        } else if (word != expected[i]) {
            return false;
        }
    }
    return true;
}
#endif
