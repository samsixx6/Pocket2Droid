/* Device Emulator VCEFS folder-sharing endpoint, with optional CAB import.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "system/memory.h"
#include "hw/arm/p2d-import.h"
#include "hw/arm/p2d-ce-seek-fix.h"
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <sys/file.h>

#define PB_SIZE 1068
#define CAB_NAME "RagingThunder.cab"
static bool folder_move_state_load(P2DImport *s, const char *state,
                                   const char *shared, Error **errp);
static bool p2d_gles_load(P2DImport *s, const char *assets,
                          const char *renderer, Error **errp);
static void p2d_gles_reset(P2DGLESFiles *s);
static void p2d_gles_cleanup(P2DGLESFiles *s);
static void p2d_shm_init(P2DImport *s);
static void p2d_shm_reset(P2DGLESFiles *s);
static void p2d_shm_cleanup(P2DGLESFiles *s);

bool p2d_import_load(P2DImport *s, CPUState *cpu, const char *path,
                     const char *shared_folder, const char *move_state,
                     const char *network_setup, const char *gles_assets,
                     const char *gles_renderer, Error **errp)
{
    struct stat st;
    int fd;
    size_t done = 0;
    s->cpu = cpu;
    s->ce_seek_checked = false;
    s->root_fd = -1;
    s->move_state_fd = s->move_lock_fd = -1;
    s->move_blocked = false;
    s->gles.initialized = true;
    s->gles.fd = -1;
    for (unsigned i = 0; i < ARRAY_SIZE(s->files); i++) {
        s->files[i].fd = -1;
    }
    s->initialized = true;
    if (path && shared_folder) {
        error_setg(errp, "import-file and shared-folder are mutually exclusive");
        return false;
    }
    if ((gles_assets || gles_renderer) && !shared_folder) {
        error_setg(errp, "GLES transport requires shared-folder");
        return false;
    }
    if (shared_folder) {
        s->root_fd = open(shared_folder, O_RDONLY | O_DIRECTORY |
                          O_NOFOLLOW | O_CLOEXEC);
        if (s->root_fd < 0) {
            error_setg_errno(errp, errno, "Cannot open shared-folder directory");
            return false;
        }
        if (!p2d_gles_load(s, gles_assets, gles_renderer, errp)) {
            return false;
        }
        p2d_shm_init(s);
        if (network_setup) {
            Error *local = NULL;
            if (!p2d_net_setup_files_load(&s->setup, network_setup, s->root_fd, &local)) {
                warn_report_err(local);
            }
        }
        return folder_move_state_load(s, move_state, shared_folder, errp);
    }
    if (!path) {
        return true;
    }
    fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Cannot open import CAB");
        return false;
    }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 36 ||
        st.st_size > 64 * 1024 * 1024) {
        error_setg(errp, "import-file must be a regular CAB of36B..64MiB");
        close(fd);
        return false;
    }
    s->size = st.st_size;
    s->data = g_malloc(s->size);
    while (done < s->size) {
        ssize_t n = read(fd, s->data + done, s->size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            error_setg(errp, "Import CAB read failed");
            close(fd);
            g_clear_pointer(&s->data, g_free);
            return false;
        }
        done += n;
    }
    close(fd);
    if (memcmp(s->data, "MSCF", 4) || ldl_le_p(s->data + 8) != s->size) {
        error_setg(errp, "Import CAB header/length mismatch");
        g_clear_pointer(&s->data, g_free);
        return false;
    }
    return true;
}

void p2d_import_reset(P2DImport *s)
{
    s->ce_seek_checked = false;
    if (s->initialized) {
        for (unsigned i = 0; i < ARRAY_SIZE(s->files); i++) {
            if (s->files[i].fd >= 0) {
                close(s->files[i].fd);
            }
            s->files[i].fd = -1;
            s->files[i].mode = 0;
            s->files[i].position = 0;
            s->files[i].gles_id = 0;
            g_clear_pointer(&s->files[i].name, g_free);
            if (s->find[i].directory) {
                closedir(s->find[i].directory);
            }
            s->find[i].directory = NULL;
            s->find[i].gles_dir = s->find[i].gles_root = false;
            s->find[i].gles_next = 0;
            s->find[i].setup_dir = s->find[i].setup_pending = s->find[i].setup_offered = false;
            g_clear_pointer(&s->find[i].pattern, g_free);
        }
    }
    memset(s->handles, 0, sizeof(s->handles));
    memset(s->searches, 0, sizeof(s->searches));
    memset(s->modes, 0, sizeof(s->modes));
    memset(s->positions, 0, sizeof(s->positions));
    s->trace_count = 0;
    p2d_gles_reset(&s->gles);
    p2d_shm_reset(&s->gles);
}

bool p2d_import_enabled(P2DImport *s)
{
    return s->data || (s->initialized && s->root_fd >= 0);
}

void p2d_import_cleanup(P2DImport *s)
{
    p2d_import_reset(s);
    p2d_net_setup_files_cleanup(&s->setup);
    p2d_shm_cleanup(&s->gles);
    p2d_gles_cleanup(&s->gles);
    if (s->initialized && s->root_fd >= 0) {
        close(s->root_fd);
    }
    s->root_fd = -1;
    g_clear_pointer(&s->data, g_free);
    if (s->initialized && s->move_lock_fd >= 0) { close(s->move_lock_fd); }
    if (s->initialized && s->move_state_fd >= 0) { close(s->move_state_fd); }
    s->move_lock_fd = s->move_state_fd = -1;
}

/* Debug translation avoids injecting CPU faults from a host device operation.
 * Validate the entire range before copying. No MMIO/ROM or 32-bit wrap access.
 * Physical writes go through QEMU's normal RAM dirty/TB invalidation.
 */
static bool guest_copy(P2DImport *s, uint32_t address, void *buffer,
                       uint32_t size, bool write)
{
    uint64_t end = (uint64_t)address + size;
    uint64_t current;
    struct Span { uint64_t pa; uint32_t size; };
    g_autoptr(GArray) spans = g_array_new(false, false, sizeof(struct Span));
    uint32_t offset = 0;
    if (end > UINT64_C(0x100000000)) {
        return false;
    }
    /* Device Emulator ServerPB and DMA buffers may be physical SDRAM addresses.
     * The real VCEFSD driver publishes e.g.0x33eff000 even with MMU enabled.
     */
    if (address >= 0x30000000 && end <= 0x38000000) {
        if (!buffer) {
            return true;
        }
        return address_space_rw(&address_space_memory, address,
                                MEMTXATTRS_UNSPECIFIED, buffer, size, write) == MEMTX_OK;
    }
    for (current = address; current < end;) {
        TranslateForDebugResult translation;
        uint64_t pa, page_size;
        uint32_t chunk;
        if (!cpu_translate_for_debug(s->cpu, current, &translation)) {
            return false;
        }
        pa = translation.physaddr;
        page_size = UINT64_C(1) << MIN(translation.lg_page_size, 10);
        chunk = MIN(end - current, page_size - (pa & (page_size - 1)));
        if (pa < 0x30000000 || pa + chunk > 0x38000000) {
            return false;
        }
        struct Span span = { pa, chunk };
        g_array_append_val(spans, span);
        current += chunk;
    }
    /* A write can itself change the guest's page tables. Keep the validated
     * physical destinations for the whole operation rather than translating
     * again after a previous chunk has changed a later mapping.
     */
    if (!buffer) {
        return true;
    }
    for (unsigned i = 0; i < spans->len; ++i) {
        struct Span *span = &g_array_index(spans, struct Span, i);
        if (address_space_rw(&address_space_memory, span->pa,
                             MEMTXATTRS_UNSPECIFIED, (uint8_t *)buffer + offset,
                             span->size, write) != MEMTX_OK) {
            return false;
        }
        offset += span->size;
    }
    return true;
}

/* Only a known, uniquely identified driver routine in the calling module is
 * eligible. In particular, never patch persistent RAM-FMD, retained RAM, ROM,
 * MMIO, a game executable, or an arbitrary absolute file offset.
 */
static bool ce_seek_volatile_range(P2DImport *s, uint32_t va, uint32_t size,
                                    uint64_t *pa)
{
    TranslateForDebugResult translation;
    if ((va & 3) || size > 1024 || (va & 1023) + size > 1024 ||
        !cpu_translate_for_debug(s->cpu, va, &translation)) {
        return false;
    }
    *pa = translation.physaddr;
    if (*pa < 0x30000000 || *pa + size > 0x38000000 ||
        (*pa >= 0x34000000 && *pa < 0x36000000)) {
        return false;
    }
    MemoryRegionSection section = memory_region_find(get_system_memory(), *pa, size);
    bool allowed = section.mr && memory_region_is_ram(section.mr) &&
                   !memory_region_is_rom(section.mr) &&
                   int128_get64(section.size) >= size &&
                   memory_region_get_fd(section.mr) < 0;
    if (section.mr) {
        memory_region_unref(section.mr);
    }
    return allowed;
}

static void ce_seek_compatibility(P2DImport *s, uint32_t command)
{
    enum { WINDOW = 65536, PAGE = 1024 };
    /* The platform's zero-command handshake runs before VCEFSD is loaded.
     * Nonzero protocol commands are issued by the resident driver itself.
     */
    if (s->ce_seek_checked || !command) {
        return;
    }
    s->ce_seek_checked = true;
    uint64_t pc = CPU_GET_CLASS(s->cpu)->get_pc(s->cpu);
    /* This ARM target uses CF_PCREL and keeps its PC current at MMIO helpers.
     * Reading it does not restore CPU state or replay the open operation.
     */
    if (pc > UINT32_MAX) {
        return;
    }
    uint32_t base = (uint32_t)pc & ~(WINDOW - 1u);
    g_autofree uint8_t *code = g_malloc0(WINDOW);
    bool valid[WINDOW / PAGE] = { false };
    for (unsigned off = 0; off < WINDOW; off += PAGE) {
        uint64_t pa;
        if (ce_seek_volatile_range(s, base + off, PAGE, &pa) &&
            address_space_rw(&address_space_memory, pa, MEMTXATTRS_UNSPECIFIED,
                             code + off, PAGE, false) == MEMTX_OK) {
            valid[off / PAGE] = true;
        }
    }
    unsigned matches = 0, found = 0;
    bool fixed = false;
    for (unsigned off = 0; off <= WINDOW - P2D_CE_SEEK_BYTES; off += 4) {
        bool candidate_fixed = false;
        if (valid[off / PAGE] &&
            valid[(off + P2D_CE_SEEK_BYTES - 1) / PAGE] &&
            p2d_ce_seek_signature(code + off, WINDOW - off, &candidate_fixed)) {
            ++matches;
            found = off;
            fixed = candidate_fixed;
        }
    }
    if (matches != 1 || fixed ||
        (pc - base >= found && pc - base < found + P2D_CE_SEEK_BYTES)) {
        return;
    }
    uint32_t address = base + found + P2D_CE_SEEK_CHANGE;
    uint64_t pa;
    uint8_t before[4], after[4];
    if (!ce_seek_volatile_range(s, address, sizeof(before), &pa) ||
        address_space_rw(&address_space_memory, pa, MEMTXATTRS_UNSPECIFIED,
                         before, sizeof(before), false) != MEMTX_OK ||
        ldl_le_p(before) != P2D_CE_SEEK_SUB) {
        return;
    }
    stl_le_p(after, P2D_CE_SEEK_ADD);
    /* Normal RAM write: dirties RAM and invalidates previously translated TBs.
     * The matched routine has no MMIO, and is not the current caller.
     */
    if (address_space_rw(&address_space_memory, pa, MEMTXATTRS_UNSPECIFIED,
                         after, sizeof(after), true) == MEMTX_OK) {
        info_report("p2d corrected known VCEFSD FILE_END seek at 0x%08x", address);
    }
}

static bool get_name(const uint8_t *pb, char *name)
{
    unsigned len = lduw_le_p(pb + 34), i;
    if (len > 512 || len & 1) {
        return false;
    }
    for (i = 0; i < len / 2; ++i) {
        unsigned c = lduw_le_p(pb + 36 + i * 2);
        if (!c && i + 1 == len / 2) {
            break;
        }
        if (!c || c > 127) {
            return false;
        }
        name[i] = c == '\\' ? '/' : c;
    }
    name[i] = 0;
    return true;
}

static void info(uint8_t *pb, bool root, bool write_name, size_t size)
{
    stl_le_p(pb + 12, 0x5d410000); /* Valid fixed DOS timestamp. */
    stl_le_p(pb + 1064, 0x5d410000);
    stw_le_p(pb + 28, root ? 0x11 : 0x21); /* Directory/archive + read-only. */
    stl_le_p(pb + 16, root ? 0 : size);
    if (write_name) {
        unsigned i;
        memset(pb + 36, 0, 512);
        for (i = 0; i < strlen(CAB_NAME); ++i) {
            stw_le_p(pb + 36 + 2 * i, CAB_NAME[i]);
        }
        stw_le_p(pb + 34, strlen(CAB_NAME) * 2);
    }
}

static uint32_t import_execute(P2DImport *s, uint32_t address, uint32_t cmd)
{
    uint8_t pb[PB_SIZE];
    char name[257] = { 0 }, *path = name;
    uint16_t h, mode;
    uint32_t count, pos, actual, txn, result = 0;
    int index;
    bool root, file;

    if (!s->data) {
        return 1;
    }
    if (!cmd || cmd == 4) {
        return 0;
    }
    if (cmd == 0x15) {
        return 65536;
    }
    if (!guest_copy(s, address, pb, sizeof(pb), false) || lduw_le_p(pb) != PB_SIZE) {
        return 1;
    }
    if ((cmd == 6 || cmd == 0x11) && !get_name(pb, name)) {
        return 123;
    }
    while (*path == '/') {
        ++path;
    }
    root = !*path;
    file = !g_ascii_strcasecmp(path, CAB_NAME);
    h = lduw_le_p(pb + 10);
    mode = lduw_le_p(pb + 30);
    count = ldl_le_p(pb + 16);
    pos = ldl_le_p(pb + 20);

    switch (cmd) {
    case 5: case 8: case 9: case 0xc: case 0xd: case 0xe: case 0xf: case 0x10:
        return 19; /* This endpoint has no host write operations. */
    case 6:
        if (!file) {
            return 2;
        }
        if (mode & 15) {
            return 19;
        }
        for (h = 0; h < 40 && s->handles[h]; ++h) {}
        if (h == 40) {
            return 4;
        }
        s->handles[h] = true;
        s->modes[h] = mode;
        s->positions[h] = 0;
        stw_le_p(pb + 10, h);
        break;
    case 7:
        if (h >= 40 || !s->handles[h]) {
            return 6;
        }
        actual = pos < s->size ? MIN(MIN(count, 65536), s->size - pos) : 0;
        if (actual && !guest_copy(s, ldl_le_p(pb + 24), s->data + pos, actual, true)) {
            return 30;
        }
        s->positions[h] = pos + actual;
        stl_le_p(pb + 16, actual);
        break;
    case 0xa:
        if (h >= 40 || !s->handles[h]) {
            return 6;
        }
        s->handles[h] = false;
        break;
    case 0xb:
        stl_le_p(pb + 16, 0);
        stl_le_p(pb + 20, (s->size + 32767) / 32768);
        break;
    case 0x11:
        txn = ldl_le_p(pb + 4);
        index = (int16_t)lduw_le_p(pb + 8);
        if (index == -1) {
            if (txn != UINT32_MAX) {
                return 1;
            }
            if (!root && !file) {
                return 2;
            }
            info(pb, root, false, s->size);
        } else if (index == 0) {
            if (txn != UINT32_MAX && txn >= 40) {
                return 31;
            }
            if (strchr(path, '/') ||
                !(file || !strcmp(path, "*") || !strcmp(path, "*.*"))) {
                return 2;
            }
            if (txn != UINT32_MAX) {
                s->searches[txn] = true;
            }
            info(pb, false, txn != UINT32_MAX, s->size);
        } else if (index > 0 && txn < 40 && s->searches[txn]) {
            s->searches[txn] = false;
            return 18;
        } else {
            return 31;
        }
        break;
    case 0x13:
        if (h >= 40 || !s->handles[h]) {
            return 6;
        }
        stl_le_p(pb + 20, s->positions[h]);
        stw_le_p(pb + 30, s->modes[h]);
        info(pb, false, true, s->size);
        break;
    default:
        return 1;
    }
    if (!guest_copy(s, address, pb, sizeof(pb), true)) {
        result = 30;
    }
    if (s->trace_count++ < 64) {
        qemu_log_mask(LOG_UNIMP, "p2d import cmd=%x pb=%08x result=%u\n", cmd, address, result);
    }
    return result;
}

/* VCEFS errors (DOS/WinCE values), independent of the host errno ABI. */
static uint32_t folder_error(int error, bool next)
{
    switch (error) {
    case 0: return 0;
    case ENOENT: return next ? 18 : 2;
    case ENOTDIR: return 3;
    case EMFILE: case ENFILE: return 4;
    case EACCES: case EPERM: case ELOOP: case EISDIR: case ENOTEMPTY: return 5;
    case EBADF: return 6;
    case EROFS: return 19;
    case EBUSY: case EAGAIN: return 32;
    case ENOSPC: case EDQUOT: case EFBIG: return 39;
    case EEXIST: return 80;
    case EINVAL: case ENAMETOOLONG: case EILSEQ: return 123;
    default: return 31;
    }
}

static char *folder_name(const uint8_t *pb, unsigned length_off,
                         unsigned name_off)
{
    unsigned bytes = lduw_le_p(pb + length_off), count;
    gunichar2 text[256];
    if (bytes > 512 || (bytes & 1)) {
        return NULL;
    }
    count = bytes / 2;
    for (unsigned i = 0; i < count; i++) {
        text[i] = lduw_le_p(pb + name_off + 2 * i);
        if (!text[i]) {
            if (i + 1 != count) {
                return NULL;
            }
            count--;
        }
    }
    return g_utf16_to_utf8(text, count, NULL, NULL, NULL);
}

static bool folder_write_name(uint8_t *pb, const char *name)
{
    glong count;
    g_autofree gunichar2 *wide = g_utf8_to_utf16(name, -1, NULL, &count, NULL);
    if (!wide || count > 255) {
        return false;
    }
    memset(pb + 36, 0, 512);
    for (unsigned i = 0; i < count; i++) {
        stw_le_p(pb + 36 + 2 * i, wide[i]);
    }
    stw_le_p(pb + 34, count * 2);
    return true;
}

static bool folder_case_equal(const char *a, const char *b)
{
    g_autofree char *left = g_utf8_casefold(a, -1);
    g_autofree char *right = g_utf8_casefold(b, -1);
    return left && right && !strcmp(left, right);
}

/* Normalize guest-rooted paths before touching the host. Wildcards can appear
 * only in the last component of a FindFirst request. Never truncate a name. */
static GPtrArray *folder_parts(const char *path, bool pattern)
{
    g_autoptr(GPtrArray) parts = g_ptr_array_new_with_free_func(g_free);
    g_auto(GStrv) split = g_strsplit_set(path, "\\/", -1);
    for (unsigned i = 0; split[i]; i++) {
        const char *part = split[i];
        if (!*part || !strcmp(part, ".")) {
            continue;
        }
        if (!strcmp(part, "..")) {
            if (!parts->len) {
                return NULL;
            }
            g_ptr_array_remove_index(parts, parts->len - 1);
            continue;
        }
        if (strlen(part) > 255 || strchr(part, ':')) {
            return NULL;
        }
        for (const unsigned char *p = (const unsigned char *)part; *p; p++) {
            if (*p < 32 || strchr("\"<>|", *p)) {
                return NULL;
            }
        }
        g_ptr_array_add(parts, g_strdup(part));
    }
    if (pattern && *path && strchr("\\/", path[strlen(path) - 1])) {
        g_ptr_array_add(parts, g_strdup("*"));
    }
    for (unsigned i = 0; i < parts->len; i++) {
        const char *part = parts->pdata[i];
        if (strpbrk(part, "*?") && (!pattern || i + 1 != parts->len)) {
            return NULL;
        }
    }
    return g_steal_pointer(&parts);
}

/* New open file descriptions are essential: dup shares directory offsets. */
static int folder_directory(int fd)
{
    return openat(fd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static int folder_component(int dir, const char *want, bool missing, char **name)
{
    struct stat st;
    int fd, error = ENOENT;
    DIR *stream;
    struct dirent *entry;

    if (!fstatat(dir, want, &st, AT_SYMLINK_NOFOLLOW)) {
        if (S_ISLNK(st.st_mode)) {
            return ELOOP;
        }
        *name = g_strdup(want);
        return 0;
    }
    if (errno != ENOENT) {
        return errno;
    }
    fd = folder_directory(dir);
    if (fd < 0) {
        return errno;
    }
    stream = fdopendir(fd);
    if (!stream) {
        error = errno;
        close(fd);
        return error;
    }
    while (true) {
        errno = 0;
        entry = readdir(stream);
        if (!entry) {
            error = errno ? errno : ENOENT;
            break;
        }
        if (g_utf8_validate(entry->d_name, -1, NULL) &&
            folder_case_equal(entry->d_name, want)) {
            if (fstatat(dir, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
                error = errno;
            } else if (S_ISLNK(st.st_mode)) {
                error = ELOOP;
            } else {
                *name = g_strdup(entry->d_name);
                error = 0;
            }
            break;
        }
    }
    closedir(stream);
    if (error == ENOENT && missing) {
        *name = g_strdup(want);
        return 0;
    }
    return error;
}

/* Return an owned parent descriptor plus leaf. Every directory is opened with
 * NOFOLLOW; subsequent operations remain anchored if names change concurrently.
 * Root is represented by "." and is never a valid mutating-operation target. */
static int folder_parent(P2DImport *s, GPtrArray *parts, unsigned count,
                          bool missing, int *parent, char **leaf)
{
    int dir = folder_directory(s->root_fd), error;
    if (dir < 0) {
        return errno;
    }
    if (!count) {
        *parent = dir;
        *leaf = g_strdup(".");
        return 0;
    }
    for (unsigned i = 0; i + 1 < count; i++) {
        g_autofree char *name = NULL;
        int child;
        error = folder_component(dir, parts->pdata[i], false, &name);
        if (error) {
            close(dir);
            return error == ENOENT ? ENOTDIR : error;
        }
        child = openat(dir, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        error = errno;
        close(dir);
        if (child < 0) {
            return error == ENOENT ? ENOTDIR : error;
        }
        dir = child;
    }
    error = folder_component(dir, parts->pdata[count - 1], missing, leaf);
    if (error) {
        close(dir);
        return error;
    }
    *parent = dir;
    return 0;
}

/* O_NONBLOCK prevents a raced FIFO/device replacement from hanging QEMU. The
 * opened object is checked, so files, directories and symlinks cannot masquerade
 * as regular guest I/O files. */
static int folder_open_object(int dir, const char *name, int flags, bool file)
{
    struct stat st;
    int fd = openat(dir, name, flags | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0664);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st)) {
        int error = errno;
        close(fd);
        errno = error;
        return -1;
    }
    if (!(S_ISREG(st.st_mode) || (!file && S_ISDIR(st.st_mode)))) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}

static uint32_t folder_dos_time(time_t value)
{
    struct tm tm;
    if (!gmtime_r(&value, &tm)) {
        return 0;
    }
    if (tm.tm_year < 80) {
        return 0x00210000; /* 1980-01-01. */
    }
    if (tm.tm_year > 207) {
        return 0xff9fbf7d; /* 2107-12-31 23:59:58. */
    }
    return ((tm.tm_year - 80) << 25) | ((tm.tm_mon + 1) << 21) |
           (tm.tm_mday << 16) | (tm.tm_hour << 11) | (tm.tm_min << 5) |
           (tm.tm_sec / 2);
}

static bool folder_info(uint8_t *pb, const struct stat *st, const char *name,
                         bool write_name)
{
    uint16_t attrs = S_ISDIR(st->st_mode) ? 16 : 32;
    if (!(st->st_mode & S_IWUSR)) {
        attrs |= 1;
    }
    if (name[0] == '.' && strcmp(name, ".")) {
        attrs |= 2;
    }
    stl_le_p(pb + 12, folder_dos_time(st->st_mtime));
    stl_le_p(pb + 1064, folder_dos_time(st->st_ctime));
    stw_le_p(pb + 28, attrs);
    stl_le_p(pb + 16, S_ISDIR(st->st_mode) ? 0 :
             MIN((uint64_t)st->st_size, UINT32_MAX));
    return !write_name || folder_write_name(pb, name);
}

static bool folder_setup_info(uint8_t *pb, const struct stat *st,
                              const char *name, bool write_name)
{
    if (!folder_info(pb, st, name, write_name)) {
        return false;
    }
    /* Hide only the synthetic helper. Autorun still opens its required path;
     * real user entries retain the normal shared-folder metadata above. */
    stw_le_p(pb + 28, lduw_le_p(pb + 28) | 2);
    return true;
}

/* Iterative glob matching avoids recursive/exponential guest-supplied patterns.
 * Fold Unicode once; '?' consumes one Unicode code point. DOS '*.*' means '*'. */
static bool folder_match(const char *pattern, const char *name)
{
    g_autofree char *pbuf = g_utf8_casefold(pattern, -1);
    g_autofree char *nbuf = g_utf8_casefold(name, -1);
    const char *p = pbuf, *n = nbuf, *star = NULL, *retry = NULL;
    if (!strcmp(p, "*.*")) {
        return true;
    }
    while (*n) {
        if (*p == '*') {
            star = ++p;
            retry = n;
        } else if (*p == '?' || (*p && g_utf8_get_char(p) == g_utf8_get_char(n))) {
            p = g_utf8_next_char(p);
            n = g_utf8_next_char(n);
        } else if (star) {
            p = star;
            n = retry = g_utf8_next_char(retry);
        } else {
            return false;
        }
    }
    while (*p == '*') {
        p++;
    }
    return !*p;
}

static void folder_search_close(P2DFolderSearch *find)
{
    if (find->directory) {
        closedir(find->directory);
    }
    find->directory = NULL;
    find->gles_dir = find->gles_root = false;
    find->gles_next = 0;
    find->setup_dir = find->setup_pending = find->setup_offered = false;
    g_clear_pointer(&find->pattern, g_free);
}

#include "p2d-gles-transport.inc"
#include "p2d-gles-shm.inc"

static int folder_next(P2DImport *s, P2DFolderSearch *find, uint8_t *pb, bool write_name)
{
    struct dirent *entry;
    struct stat st;
    if (find->setup_dir) {
        if (!find->setup_pending) {
            return ENOENT;
        }
        find->setup_pending = false;
        if (!folder_match(find->pattern, "autorun.exe") ||
            !p2d_net_setup_files_stat(&s->setup, false, &st)) {
            return ENOENT;
        }
        return folder_setup_info(pb, &st, "autorun.exe", write_name) ? 0 : EINVAL;
    }
    while (true) {
        errno = 0;
        entry = readdir(find->directory);
        if (!entry) {
            if (!errno && find->setup_pending) {
                find->setup_pending = false;
                if (p2d_net_setup_files_active(&s->setup) &&
                    folder_match(find->pattern, "2577") &&
                    p2d_net_setup_files_stat(&s->setup, true, &st)) {
                    find->setup_offered = true;
                    return folder_setup_info(pb, &st, "2577", write_name) ? 0 : EINVAL;
                }
            }
            return errno ? errno : ENOENT;
        }
        if (find->setup_offered && !strcmp(entry->d_name, "2577")) {
            continue;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
            !g_utf8_validate(entry->d_name, -1, NULL) ||
            !folder_match(find->pattern, entry->d_name)) {
            continue;
        }
        if (fstatat(dirfd(find->directory), entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) {
            if (errno == ENOENT) {
                continue;
            }
            return errno;
        }
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            continue;
        }
        if (!folder_info(pb, &st, entry->d_name, write_name)) {
            continue;
        }
        return 0;
    }
}

static int folder_getinfo(P2DImport *s, uint8_t *pb, GPtrArray *parts)
{
    uint32_t txn = ldl_le_p(pb + 4);
    int index = (int16_t)lduw_le_p(pb + 8), dir, fd, error;
    g_autofree char *leaf = NULL;
    struct stat st;
    P2DFolderSearch temporary = { 0 }, *find;

    if (index > 0) {
        find = &s->find[txn]; /* Transaction validated by caller. */
        if (!find->directory && !find->setup_dir) {
            return EIO;
        }
        error = folder_next(s, find, pb, true);
        if (error) {
            folder_search_close(find);
        }
        return error;
    }
    if (index == -1) {
        if (parts->len && p2d_net_setup_files_handles(&s->setup, parts->pdata[0])) {
            bool directory = parts->len == 1;
            if (!directory && (parts->len != 2 || g_ascii_strcasecmp(parts->pdata[1], "autorun.exe"))) {
                return ENOENT;
            }
            return p2d_net_setup_files_stat(&s->setup, directory, &st) &&
                   folder_setup_info(pb, &st, directory ? "2577" : "autorun.exe", false) ? 0 : EIO;
        }
        error = folder_parent(s, parts, parts->len, false, &dir, &leaf);
        if (error) {
            return error;
        }
        fd = folder_open_object(dir, leaf, O_RDONLY, false);
        error = errno;
        close(dir);
        if (fd < 0) {
            return error;
        }
        error = fstat(fd, &st) ? errno : 0;
        if (!error && !folder_info(pb, &st, leaf, false)) {
            error = EINVAL;
        }
        close(fd);
        return error;
    }
    unsigned count = parts->len;
    const char *pattern = count ? parts->pdata[count - 1] : "*";
    if (count > 1 && p2d_net_setup_files_handles(&s->setup, parts->pdata[0])) {
        if (count != 2) {
            return ENOTDIR;
        }
        find = txn == UINT32_MAX ? &temporary : &s->find[txn];
        folder_search_close(find);
        find->setup_dir = find->setup_pending = true;
        find->pattern = g_strdup(pattern);
        error = folder_next(s, find, pb, txn != UINT32_MAX);
        if (error || txn == UINT32_MAX) {
            folder_search_close(find);
        }
        return error;
    }
    error = folder_parent(s, parts, count ? count - 1 : 0, false, &dir, &leaf);
    if (error) {
        return error == ENOENT ? ENOTDIR : error;
    }
    fd = openat(dir, leaf, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    error = errno;
    close(dir);
    if (fd < 0) {
        return error;
    }
    find = txn == UINT32_MAX ? &temporary : &s->find[txn];
    folder_search_close(find);
    find->directory = fdopendir(fd);
    if (!find->directory) {
        error = errno;
        close(fd);
        return error;
    }
    find->pattern = g_strdup(pattern);
    find->setup_pending = count <= 1;
    error = folder_next(s, find, pb, txn != UINT32_MAX);
    if (error || txn == UINT32_MAX) {
        folder_search_close(find);
    }
    return error;
}

static int folder_setattrs(int fd, uint16_t attrs, uint32_t dos)
{
    struct stat st;
    struct timespec times[2] = { { .tv_nsec = UTIME_OMIT }, { 0 } };
    if (fstat(fd, &st)) {
        return errno;
    }
    if (dos) {
        struct tm tm = {
            .tm_year = ((dos >> 25) & 127) + 80,
            .tm_mon = ((dos >> 21) & 15) - 1,
            .tm_mday = (dos >> 16) & 31,
            .tm_hour = (dos >> 11) & 31,
            .tm_min = (dos >> 5) & 63,
            .tm_sec = (dos & 31) * 2,
        };
        if (tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1 ||
            tm.tm_hour > 23 || tm.tm_min > 59 || tm.tm_sec > 59) {
            return EINVAL;
        }
        times[1].tv_sec = timegm(&tm);
        if (folder_dos_time(times[1].tv_sec) != dos) {
            return EINVAL;
        }
    }
    /* Validate all guest metadata before changing either mode or timestamp. */
    mode_t mode = st.st_mode & 0777;
    mode = attrs & 1 ? mode & ~0222 : mode | S_IWUSR;
    if (fchmod(fd, mode)) {
        return errno;
    }
    if (dos) {
        if (futimens(fd, times)) {
            return errno;
        }
    }
    return 0;
}

#include "p2d-folder-move.h"

static uint32_t folder_execute(P2DImport *s, uint32_t address, uint32_t cmd)
{
    uint8_t pb[PB_SIZE], io[65536];
    g_autofree char *name = NULL, *name2 = NULL, *leaf = NULL, *leaf2 = NULL;
    g_autoptr(GPtrArray) parts = NULL, parts2 = NULL;
    int error = 0, dir = -1, dir2 = -1, fd = -1;
    uint16_t h, mode;
    uint32_t count, position;
    ssize_t actual;
    struct stat st;

    if (!cmd || cmd == 4) {
        return 0;
    }
    if (cmd == 0x15) {
        return 65536;
    }
    if (s->move_blocked) { return 31; }
    if (!guest_copy(s, address, pb, sizeof(pb), false) || lduw_le_p(pb) != PB_SIZE) {
        return 1;
    }
    h = lduw_le_p(pb + 10);
    mode = lduw_le_p(pb + 30);
    count = MIN(ldl_le_p(pb + 16), sizeof(io));
    position = ldl_le_p(pb + 20);
    if (cmd == 5 || cmd == 6 || (cmd >= 0xc && cmd <= 0x11)) {
        name = folder_name(pb, 34, 36);
        if (!name) {
            return 123;
        }
        if (!p2d_gles_path_safe(&s->gles, name)) {
            return 123;
        }
        parts = folder_parts(name, cmd == 0x11 && (int16_t)lduw_le_p(pb + 8) == 0);
        if (!parts) {
            return 123;
        }
    }
    if (cmd == 0xf) {
        name2 = folder_name(pb, 548, 550);
        if (!name2 || !(parts2 = folder_parts(name2, false))) {
            return 123;
        }
        if (!p2d_gles_path_safe(&s->gles, name2)) {
            return 123;
        }
    }
    if (cmd == 6 && (mode & 15) > 2) {
        return 123;
    }
    uint32_t gles_result;
    if (folder_gles_execute(s, address, cmd, pb, io, parts, parts2,
                            &gles_result)) {
        return gles_result;
    }
    bool setup_path = parts && parts->len &&
                      p2d_net_setup_files_handles(&s->setup, parts->pdata[0]);
    bool setup_destination = parts2 && parts2->len &&
                             p2d_net_setup_files_handles(&s->setup, parts2->pdata[0]);
    if ((setup_path && (cmd == 5 || (cmd >= 0xc && cmd <= 0x10) ||
                        (cmd == 6 && (mode & 15)))) || (cmd == 0xf && setup_destination)) {
        return 5;
    }
    if (setup_path && cmd == 6) {
        if (parts->len != 2 || g_ascii_strcasecmp(parts->pdata[1], "autorun.exe")) {
            return 2;
        }
        for (h = 0; h < ARRAY_SIZE(s->files) && s->files[h].fd >= 0; h++) {}
        if (h == ARRAY_SIZE(s->files)) {
            return 4;
        }
        fd = fcntl(s->setup.fd, F_DUPFD_CLOEXEC, 0);
        if (fd < 0) {
            return 31;
        }
        s->files[h].fd = fd;
        s->files[h].mode = 0;
        s->files[h].position = 0;
        s->files[h].name = g_strdup("autorun.exe");
        s->files[h].gles_id = 0;
        stw_le_p(pb + 10, h);
        return guest_copy(s, address, pb, sizeof(pb), true) ? 0 : 30;
    }
    if ((cmd >= 7 && cmd <= 0xa) || cmd == 0x13) {
        if (h >= ARRAY_SIZE(s->files) || s->files[h].fd < 0) {
            return 6;
        }
        fd = s->files[h].fd;
    }
    if (cmd == 5 || cmd == 6 || (cmd >= 0xc && cmd <= 0x10)) {
        if (!parts->len && cmd != 6 && cmd != 0xe) {
            return 5;
        }
        error = folder_parent(s, parts, parts->len,
                               cmd == 5 || cmd == 0xc, &dir, &leaf);
        if (error) {
            return folder_error(error, false);
        }
    }
    switch (cmd) {
    case 5:
        fd = folder_open_object(dir, leaf, O_WRONLY | O_CREAT | O_EXCL, true);
        if (fd < 0) {
            error = errno;
        } else {
            close(fd);
        }
        break;
    case 6:
        for (h = 0; h < ARRAY_SIZE(s->files) && s->files[h].fd >= 0; h++) {}
        if (h == ARRAY_SIZE(s->files)) {
            error = EMFILE;
            break;
        }
        fd = folder_open_object(dir, leaf, (mode & 15) == 0 ? O_RDONLY :
                                 (mode & 15) == 1 ? O_WRONLY : O_RDWR, true);
        if (fd < 0) {
            error = errno;
            break;
        }
        s->files[h].fd = fd;
        s->files[h].mode = mode;
        s->files[h].position = 0;
        s->files[h].name = g_strdup(leaf);
        s->files[h].gles_id = 0;
        stw_le_p(pb + 10, h);
        break;
    case 7:
        if ((s->files[h].mode & 15) == 1) {
            return 5;
        }
        if (!guest_copy(s, ldl_le_p(pb + 24), NULL, count, true)) {
            return 30;
        }
        do {
            actual = pread(fd, io, count, position);
        } while (actual < 0 && errno == EINTR);
        if (actual < 0) {
            error = errno;
        } else if (!guest_copy(s, ldl_le_p(pb + 24), io, actual, true)) {
            return 30;
        } else {
            s->files[h].position = MIN((uint64_t)position + actual, UINT32_MAX);
            stl_le_p(pb + 16, actual);
        }
        break;
    case 8:
        if (!(s->files[h].mode & 15)) {
            return 5;
        }
        /* Entire RAM source validation and copy precedes all host mutation. */
        if (!guest_copy(s, ldl_le_p(pb + 24), io, count, false)) {
            return 29;
        }
        do {
            actual = pwrite(fd, io, count, position);
        } while (actual < 0 && errno == EINTR);
        if (actual < 0) {
            error = errno;
        } else {
            s->files[h].position = MIN((uint64_t)position + actual, UINT32_MAX);
            stl_le_p(pb + 16, actual);
        }
        break;
    case 9:
        if (!(s->files[h].mode & 15)) {
            return 5;
        }
        error = ftruncate(fd, position) ? errno : 0;
        break;
    case 0xa:
        error = close(fd) ? errno : 0;
        s->files[h].fd = -1;
        g_clear_pointer(&s->files[h].name, g_free);
        break;
    case 0xb: {
        struct statvfs space;
        if (fstatvfs(s->root_fd, &space)) {
            error = errno;
        } else {
            uint64_t unit = space.f_frsize ? space.f_frsize : space.f_bsize;
            stl_le_p(pb + 16, MIN(space.f_bavail * unit / 32768, UINT32_MAX));
            stl_le_p(pb + 20, MIN(space.f_blocks * unit / 32768, UINT32_MAX));
        }
        break;
    }
    case 0xc:
        error = mkdirat(dir, leaf, 0775) ? errno : 0;
        break;
    case 0xd: case 0x10:
        error = unlinkat(dir, leaf, cmd == 0xd ? AT_REMOVEDIR : 0) ? errno : 0;
        break;
    case 0xe:
        fd = folder_open_object(dir, leaf, O_RDONLY, false);
        if (fd < 0) {
            error = errno;
        } else {
            error = folder_setattrs(fd, lduw_le_p(pb + 28), ldl_le_p(pb + 12));
            close(fd);
        }
        break;
    case 0xf:
        if (!parts2->len) {
            error = EACCES;
            break;
        }
        error = folder_parent(s, parts2, parts2->len, true, &dir2, &leaf2);
        if (!error) {
            /* Windows CE MoveFile must not replace an existing destination.
             * Bionic API24 has syscall(), but no public renameat2 wrapper.
             * Kernel no-replace is atomic; never fall back to a raced check
             * followed by overwriting renameat on an unsupported filesystem. */
            error = syscall(SYS_renameat2, dir, leaf, dir2, leaf2,
                            RENAME_NOREPLACE) ? errno : 0;
            if (error == EINVAL || error == ENOSYS || error == EOPNOTSUPP) {
                error = folder_move_file(s, dir, leaf, dir2, leaf2, parts, parts2);
            }
        }
        break;
    case 0x11: {
        int index = (int16_t)lduw_le_p(pb + 8);
        uint32_t txn = ldl_le_p(pb + 4);
        if (index == -1 && txn != UINT32_MAX) {
            return 1;
        }
        if (index < -1 || (index >= 0 && txn != UINT32_MAX && txn >= 40) ||
            (index > 0 && txn >= 40)) {
            return 31;
        }
        error = folder_getinfo(s, pb, parts);
        if (error) {
            return folder_error(error, index > 0);
        }
        break;
    }
    case 0x13:
        error = fstat(fd, &st) ? errno : 0;
        if (!error) {
            stl_le_p(pb + 20, s->files[h].position);
            stw_le_p(pb + 30, s->files[h].mode);
            if (!folder_info(pb, &st, s->files[h].name, true)) {
                error = EINVAL;
            }
        }
        break;
    default:
        return 1;
    }
    if (dir >= 0) {
        close(dir);
    }
    if (dir2 >= 0) {
        close(dir2);
    }
    if (error) {
        return folder_error(error, false);
    }
    return guest_copy(s, address, pb, sizeof(pb), true) ? 0 : 30;
}

uint32_t p2d_import_execute(P2DImport *s, uint32_t address, uint32_t cmd)
{
    if (s->initialized && s->root_fd >= 0) {
        ce_seek_compatibility(s, cmd);
        return folder_execute(s, address, cmd);
    }
    return import_execute(s, address, cmd);
}
