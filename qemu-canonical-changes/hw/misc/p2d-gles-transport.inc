/* Optional private VCEFS files and synchronous PocketHLE batch transport.
 * Included by p2d-devemu-import.c; no global worker, polling or root-fd swap.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <dlfcn.h>
#include "hw/arm/p2d-gles-compact.h"

#define GLES_REQUEST_MAX (16u * 1024 * 1024)
#define GLES_RESPONSE_MAX (24u + 2048u * 2048 * 2 + 76)
#define GLES_LOG_MAX (1024u * 1024)

enum {
    GLES_CL = 1, GLES_CM, GLES_SETUP, GLES_AUTORUN, GLES_NETWORK, GLES_MODE,
    GLES_REQUEST, GLES_READY, GLES_RESPONSE, GLES_LOG, GLES_SESSION, GLES_SETUP_LOG,
};

static const struct {
    const char *name;
    uint32_t limit;
    bool writable, optional;
} gles_files[] = {
    { "libGLES_CL.dll", GLES_REQUEST_MAX, false, false },
    { "libGLES_CM.dll", GLES_REQUEST_MAX, false, false },
    { "setup.exe", GLES_REQUEST_MAX, false, false },
    { "autorun.exe", GLES_REQUEST_MAX, false, false },
    { "network.exe", GLES_REQUEST_MAX, false, true },
    { "mode.txt", 16, false, false },
    { "request.bin", GLES_REQUEST_MAX, true, true },
    { "request.ready", 4, true, true },
    { "response.bin", GLES_RESPONSE_MAX, true, true },
    { "guest.log", GLES_LOG_MAX, true, true },
    { "session.reset", 4, true, true },
    { "setup.log", 4096, true, true },
};

static int p2d_gles_read_all(int fd, uint8_t *bytes, size_t length);

static bool p2d_gles_active(P2DGLESFiles *s)
{
    return s->initialized && s->fd >= 0;
}

static bool p2d_gles_handles(P2DGLESFiles *s, GPtrArray *parts)
{
    return p2d_gles_active(s) && parts && parts->len &&
           !g_ascii_strcasecmp(parts->pdata[0], "P2DGLES");
}

/* The ordinary card normalizer supports '..'. The private namespace never
 * accepts it, even if normalization would leave or enter that namespace. */
static bool p2d_gles_path_safe(P2DGLESFiles *s, const char *path)
{
    bool private = false, traversal = false;
    g_auto(GStrv) split = NULL;
    if (!p2d_gles_active(s)) {
        return true;
    }
    split = g_strsplit_set(path, "\\/", -1);
    for (unsigned i = 0; split[i]; i++) {
        private |= !g_ascii_strcasecmp(split[i], "P2DGLES");
        traversal |= !strcmp(split[i], "..");
    }
    return !(private && traversal);
}

static unsigned p2d_gles_id(const char *name)
{
    for (unsigned i = 0; i < ARRAY_SIZE(gles_files); i++) {
        if (!g_ascii_strcasecmp(name, gles_files[i].name)) {
            return i + 1;
        }
    }
    return 0;
}

static int p2d_gles_stat(P2DGLESFiles *s, unsigned id, struct stat *st)
{
    if (!id || id > ARRAY_SIZE(gles_files)) {
        return ENOENT;
    }
    if (fstatat(s->fd, gles_files[id - 1].name, st, AT_SYMLINK_NOFOLLOW)) {
        return errno;
    }
    if (!S_ISREG(st->st_mode) || st->st_nlink != 1) {
        return EACCES;
    }
    return st->st_size < 0 || st->st_size > gles_files[id - 1].limit ?
           EFBIG : 0;
}

/* A real case-insensitive P2DGLES entry always wins by disabling the endpoint,
 * never by redirecting it. Cache only the real root's mutation timestamps;
 * handle I/O does not need to rescan a potentially large card on every chunk. */
static int p2d_gles_collision(P2DImport *s)
{
    P2DGLESFiles *g = &s->gles;
    struct stat st;
    struct dirent *entry;
    DIR *directory;
    int fd, error = 0;
    if (g->blocked) {
        return EACCES;
    }
    if (fstat(s->root_fd, &st)) {
        return errno;
    }
    if (g->root_stamp_valid && g->root_mtime == st.st_mtim.tv_sec &&
        g->root_mtime_nsec == st.st_mtim.tv_nsec &&
        g->root_ctime == st.st_ctim.tv_sec &&
        g->root_ctime_nsec == st.st_ctim.tv_nsec) {
        return 0;
    }
    fd = folder_directory(s->root_fd);
    if (fd < 0) {
        return errno;
    }
    directory = fdopendir(fd);
    if (!directory) {
        error = errno;
        close(fd);
        return error;
    }
    while (true) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            error = errno;
            break;
        }
        if (!g_ascii_strcasecmp(entry->d_name, "P2DGLES")) {
            error = EACCES;
            g->blocked = true;
            break;
        }
    }
    closedir(directory);
    if (!error) {
        g->root_stamp_valid = true;
        g->root_mtime = st.st_mtim.tv_sec;
        g->root_mtime_nsec = st.st_mtim.tv_nsec;
        g->root_ctime = st.st_ctim.tv_sec;
        g->root_ctime_nsec = st.st_ctim.tv_nsec;
    }
    return error;
}

static void p2d_gles_context_reset(P2DGLESFiles *s)
{
    if (s->context) {
        s->destroy(s->context);
        s->context = NULL;
    }
    s->sequence = 0;
    s->compact = false;
}

static void p2d_gles_remove(P2DGLESFiles *s, unsigned id)
{
    if (p2d_gles_active(s)) {
        /* unlinkat does not follow a replaced file or touch any other name. */
        unlinkat(s->fd, gles_files[id - 1].name, 0);
    }
}

static void p2d_gles_reset(P2DGLESFiles *s)
{
    struct stat st;
    if (!s->initialized) {
        return;
    }
    p2d_gles_context_reset(s);
    p2d_gles_remove(s, GLES_REQUEST);
    p2d_gles_remove(s, GLES_READY);
    p2d_gles_remove(s, GLES_RESPONSE);
    p2d_gles_remove(s, GLES_SESSION);
    if (p2d_gles_active(s) &&
        !fstatat(s->fd, ".p2d-response.tmp", &st, AT_SYMLINK_NOFOLLOW) &&
        S_ISREG(st.st_mode) && st.st_nlink == 1 &&
        st.st_size >= 0 && st.st_size <= GLES_RESPONSE_MAX) {
        unlinkat(s->fd, ".p2d-response.tmp", 0);
    }
}

static void p2d_gles_cleanup(P2DGLESFiles *s)
{
    if (!s->initialized) {
        return;
    }
    p2d_gles_reset(s);
    if (s->library) {
        dlclose(s->library);
    }
    if (s->fd >= 0) {
        close(s->fd);
    }
    memset(s, 0, sizeof(*s));
    s->fd = -1;
}

static bool p2d_gles_load(P2DImport *s, const char *assets,
                          const char *renderer, Error **errp)
{
    P2DGLESFiles *g = &s->gles;
    struct stat st, root;
    int error, fd;
    assets = assets && *assets ? assets : NULL;
    renderer = renderer && *renderer ? renderer : NULL;
    if (!assets) {
        if (renderer) {
            error_setg(errp, "gles-renderer requires gles-assets");
            return false;
        }
        return true; /* Baseline Off: no private files and no dlopen. */
    }
    error = p2d_gles_collision(s);
    if (error) {
        error_setg_errno(errp, error, "Private GLES conflicts with real card P2DGLES");
        return false;
    }
    g->fd = open(assets, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (g->fd < 0 || fstat(g->fd, &st) || fstat(s->root_fd, &root)) {
        error_setg_errno(errp, errno, "Cannot open private gles-assets directory");
        return false;
    }
    if (st.st_dev == root.st_dev && st.st_ino == root.st_ino) {
        error_setg(errp, "gles-assets must not be the real card directory");
        return false;
    }
    fd = folder_open_object(g->fd, ".p2d-gles-owned", O_RDONLY, true);
    if (fd < 0) {
        error_setg_errno(errp, errno, "Missing private GLES ownership marker");
        return false;
    }
    char marker[64] = { 0 };
    error = fstat(fd, &st) ? errno : 0;
    if (!error && (st.st_nlink != 1 || st.st_size < 1 || st.st_size >= sizeof(marker))) {
        error = EINVAL;
    }
    if (!error) {
        error = p2d_gles_read_all(fd, (uint8_t *)marker, st.st_size);
    }
    close(fd);
    if (error || strcmp(g_strstrip(marker), "P2D_GLES_ANDROID_V1")) {
        error_setg(errp, "Private GLES ownership marker mismatch");
        return false;
    }
    for (unsigned id = 1; id <= ARRAY_SIZE(gles_files); id++) {
        error = p2d_gles_stat(g, id, &st);
        if (error == ENOENT && gles_files[id - 1].optional) {
            continue;
        }
        if (error) {
            error_setg_errno(errp, error, "Invalid GLES file %s",
                             gles_files[id - 1].name);
            return false;
        }
    }
    if (!fstatat(g->fd, ".p2d-response.tmp", &st, AT_SYMLINK_NOFOLLOW)) {
        if (!S_ISREG(st.st_mode) || st.st_nlink != 1 || st.st_size < 0 ||
            st.st_size > GLES_RESPONSE_MAX) {
            error_setg(errp, "Invalid private GLES temporary response");
            return false;
        }
    } else if (errno != ENOENT) {
        error_setg_errno(errp, errno, "Cannot inspect private GLES temporary response");
        return false;
    }
    if (renderer) {
        g_autofree char *anchored = NULL;
        fd = open(renderer, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            error_setg_errno(errp, errno, "Cannot open gles-renderer");
            return false;
        }
        if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
            close(fd);
            error_setg(errp, "gles-renderer must be a regular file");
            return false;
        }
        /* Keep the validated inode selected across dlopen, without a raced
         * pathname lookup. Linux and Android expose these open descriptors. */
        anchored = g_strdup_printf("/proc/self/fd/%d", fd);
        g->library = dlopen(anchored, RTLD_NOW | RTLD_LOCAL);
        close(fd);
        if (!g->library) {
            error_setg(errp, "Cannot load gles-renderer: %s", dlerror());
            return false;
        }
        g->create = (void *(*)(void))dlsym(g->library, "p2d_gles_create");
        g->destroy = (void (*)(void *))dlsym(g->library, "p2d_gles_destroy");
        g->execute = (int (*)(void *, const unsigned char *, size_t,
                       const unsigned char **, size_t *))
                       dlsym(g->library, "p2d_gles_execute");
        uint32_t (*reply_capability)(void) = (uint32_t (*)(void))
            dlsym(g->library, "p2d_gles_reply_capability");
        g->compact_endpoint = reply_capability &&
                              reply_capability() == P2D_COMPACT_CAPABILITY;
        if (!g->create || !g->destroy || !g->execute) {
            error_setg(errp, "gles-renderer is missing the PocketHLE C ABI");
            return false;
        }
    }
    p2d_gles_reset(g); /* No stale request/response can survive an engine start. */
    return true;
}

static int p2d_gles_read_all(int fd, uint8_t *bytes, size_t length)
{
    size_t done = 0;
    while (done < length) {
        ssize_t count = pread(fd, bytes + done, length - done, done);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return count ? errno : EIO;
        }
        done += count;
    }
    return 0;
}

static bool p2d_gles_request_valid(const uint8_t *p, size_t size, uint32_t seq)
{
    size_t offset = 24;
    uint32_t records;
    if (size < 24 || size > GLES_REQUEST_MAX || (ldl_le_p(p) != P2D_REQUEST_FULL && ldl_le_p(p) != P2D_REQUEST_COMPACT) ||
        ldl_le_p(p + 4) != seq || ldl_le_p(p + 20) != size ||
        !ldl_le_p(p + 12) || ldl_le_p(p + 12) > 2048 ||
        !ldl_le_p(p + 16) || ldl_le_p(p + 16) > 2048) {
        return false;
    }
    records = ldl_le_p(p + 8);
    if (records > (size - 24) / 72) {
        return false;
    }
    for (unsigned i = 0; i < records; i++) {
        uint64_t payload;
        if (size - offset < 72) {
            return false;
        }
        payload = ((uint64_t)ldl_le_p(p + offset + 68) + 3) & ~UINT64_C(3);
        if (payload > size - offset - 72) {
            return false;
        }
        offset += 72 + payload;
    }
    return offset == size;
}

static bool p2d_gles_response_valid(const uint8_t *p, size_t size, uint32_t seq, bool compact)
{
    uint32_t width, height, pixels;
    if (!p || size < 24 || size > GLES_RESPONSE_MAX ||
        ldl_le_p(p) != (compact ? P2D_REPLY_COMPACT : P2D_REPLY_FULL) || ldl_le_p(p + 4) != seq) {
        return false;
    }
    width = ldl_le_p(p + 8);
    height = ldl_le_p(p + 12);
    pixels = ldl_le_p(p + 16);
    if (!width || width > 2048 || !height || height > 2048 || pixels != (compact ? 0 : width * height * 2)) {
        return false;
    }
    if (size == 24u + pixels) {
        return true;
    }
    /* Optional bounded float-query trailer; legacy frame bytes are unchanged. */
    return size == 24u + pixels + 76 &&
           ldl_le_p(p + 24u + pixels) == 0x31514750 &&
           ldl_le_p(p + 32u + pixels) >= 1 &&
           ldl_le_p(p + 32u + pixels) <= 16;
}

static int p2d_gles_publish(P2DGLESFiles *s, const uint8_t *bytes, size_t size)
{
    size_t done = 0;
    int error = 0;
    /* The fixed temporary name is never visible through VCEFS. O_EXCL makes
     * unexpected stale/symlink/FIFO collisions fail without touching them. */
    const char *temporary = ".p2d-response.tmp";
    int fd = openat(s->fd, temporary, O_WRONLY | O_CREAT | O_EXCL |
                    O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) {
        return errno;
    }
    while (done < size) {
        ssize_t count = write(fd, bytes + done, size - done);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            error = count ? errno : EIO;
            break;
        }
        done += count;
    }
    if (close(fd) && !error) {
        error = errno;
    }
    if (!error && renameat(s->fd, temporary, s->fd, "response.bin")) {
        error = errno;
    }
    if (error) {
        unlinkat(s->fd, temporary, 0);
    }
    return error;
}

static int p2d_gles_ready(P2DGLESFiles *s, uint32_t seq)
{
    struct stat st;
    g_autofree uint8_t *request = NULL;
    const unsigned char *response = NULL;
    size_t response_size = 0;
    int fd = -1, error = EINVAL, result;
    p2d_gles_remove(s, GLES_RESPONSE);
    if (!s->execute) {
        return EACCES; /* Assets-only restoration never executes native code. */
    }
    if (seq == 1) {
        /* Compatibility fallback for a new bridge process or a wrapped seq. */
        p2d_gles_context_reset(s);
    } else if (!seq || !s->sequence || s->sequence == UINT32_MAX ||
               seq != s->sequence + 1) {
        goto failed;
    }
    error = p2d_gles_stat(s, GLES_REQUEST, &st);
    if (error) {
        goto failed;
    }
    fd = folder_open_object(s->fd, "request.bin", O_RDONLY, true);
    if (fd < 0) {
        error = errno;
        goto failed;
    }
    /* Check the actual opened inode too; a pathname replacement cannot turn
     * the bounded allocation into an unbounded read. */
    if (fstat(fd, &st)) {
        error = errno;
        goto failed;
    }
    if (st.st_size < 24 || st.st_size > GLES_REQUEST_MAX || st.st_nlink != 1) {
        error = EFBIG;
        goto failed;
    }
    request = g_try_malloc(st.st_size);
    if (!request) {
        error = ENOMEM;
        goto failed;
    }
    error = p2d_gles_read_all(fd, request, st.st_size);
    if (error || !p2d_gles_request_valid(request, st.st_size, seq)) {
        error = error ? error : EINVAL;
        goto failed;
    }
    close(fd);
    fd = -1;
    if (!s->context) {
        s->context = s->create();
        if (!s->context) {
            error = ENOMEM;
            goto failed;
        }
    }
    bool compact = ldl_le_p(request) == P2D_REQUEST_COMPACT;
    if (compact && !s->compact) {
        error = EINVAL;
        goto failed;
    }
    /* Stamp only a pristine first-packet probe. A new renderer on an old
     * emulator therefore cannot accidentally advertise an unsupported reply. */
    bool probe = !compact && seq == 1 && st.st_size == 96 &&
                 ldl_le_p(request + 8) == 1 &&
                 ldl_le_p(request + 24) == P2D_COMPACT_PROBE;
    for (unsigned i = 28; probe && i < 96; i += 4) {
        probe = ldl_le_p(request + i) == 0;
    }
    if (probe && s->compact_endpoint) {
        stl_le_p(request + 28, P2D_COMPACT_HOST);
    }
    result = s->execute(s->context, request, st.st_size,
                         &response, &response_size);
    if ((result != 0 && result != 1) ||
        !p2d_gles_response_valid(response, response_size, seq, compact)) {
        error = EIO;
        goto failed;
    }
    error = p2d_gles_publish(s, response, response_size);
    if (error) {
        goto failed;
    }
    if (probe && !ldl_le_p(response + 20)) {
        size_t trailer = 24u + ldl_le_p(response + 16);
        s->compact = response_size == trailer + 76 &&
                     ldl_le_p(response + trailer) == 0x31514750 &&
                     ldl_le_p(response + trailer + 4) == P2D_COMPACT_PROBE &&
                     ldl_le_p(response + trailer + 8) == 1 &&
                     ldl_le_p(response + trailer + 12) == P2D_COMPACT_CAPABILITY;
    }
    s->sequence = seq;
    return 0; /* Full response is published before the guest WriteFile returns. */
failed:
    if (fd >= 0) {
        close(fd);
    }
    p2d_gles_context_reset(s);
    return error;
}

static bool p2d_gles_info(uint8_t *pb, const struct stat *st,
                          const char *name, bool write_name, bool readonly)
{
    if (!folder_info(pb, st, name, write_name)) {
        return false;
    }
    if (readonly) {
        stw_le_p(pb + 28, lduw_le_p(pb + 28) | 1);
    }
    return true;
}

static int p2d_gles_next(P2DGLESFiles *s, P2DFolderSearch *find,
                          uint8_t *pb, bool write_name)
{
    struct stat st;
    if (find->gles_root) {
        if (find->gles_next++) {
            return ENOENT;
        }
        if (fstat(s->fd, &st)) {
            return errno;
        }
        if (!p2d_gles_info(pb, &st, "P2DGLES", write_name, true)) {
            return EINVAL;
        }
        stw_le_p(pb + 28, lduw_le_p(pb + 28) | 2);
        return 0;
    }
    while (find->gles_next < ARRAY_SIZE(gles_files)) {
        unsigned id = ++find->gles_next;
        int error;
        if (!folder_match(find->pattern, gles_files[id - 1].name)) {
            continue;
        }
        error = p2d_gles_stat(s, id, &st);
        if (error == ENOENT) {
            continue;
        }
        if (error) {
            return error;
        }
        return p2d_gles_info(pb, &st, gles_files[id - 1].name, write_name,
                              !gles_files[id - 1].writable) ? 0 : EINVAL;
    }
    return ENOENT;
}

static int p2d_gles_getinfo(P2DImport *s, uint8_t *pb, GPtrArray *parts)
{
    uint32_t txn = ldl_le_p(pb + 4);
    int index = (int16_t)lduw_le_p(pb + 8), error;
    P2DFolderSearch temporary = { 0 }, *find;
    struct stat st;
    if (index < -1 || (index == -1 && txn != UINT32_MAX) ||
        (index >= 0 && txn != UINT32_MAX && txn >= ARRAY_SIZE(s->find)) ||
        (index > 0 && txn >= ARRAY_SIZE(s->find))) {
        return EINVAL;
    }
    if (index > 0) {
        find = &s->find[txn];
        if (!find->gles_dir) {
            return EINVAL;
        }
    } else if (index == -1) {
        if (parts->len == 1) {
            if (fstat(s->gles.fd, &st)) {
                return errno;
            }
            if (!p2d_gles_info(pb, &st, "P2DGLES", false, true)) {
                return EINVAL;
            }
            stw_le_p(pb + 28, lduw_le_p(pb + 28) | 2);
            return 0;
        }
        unsigned id = parts->len == 2 ? p2d_gles_id(parts->pdata[1]) : 0;
        error = p2d_gles_stat(&s->gles, id, &st);
        return error ? error : p2d_gles_info(pb, &st, gles_files[id - 1].name,
                            false, !gles_files[id - 1].writable) ? 0 : EINVAL;
    } else {
        if (parts->len > 2) {
            return ENOTDIR;
        }
        find = txn == UINT32_MAX ? &temporary : &s->find[txn];
        folder_search_close(find);
        find->gles_dir = true;
        find->gles_root = parts->len == 1;
        find->pattern = g_strdup(parts->len == 2 ? parts->pdata[1] : "*");
    }
    error = p2d_gles_next(&s->gles, find, pb, txn != UINT32_MAX);
    if (error || txn == UINT32_MAX) {
        folder_search_close(find);
    }
    return error;
}

/* All operations on virtual names or handles finish here. Ordinary card I/O
 * returns false without changing its PB, root descriptor, handles or errno. */
static bool folder_gles_execute(P2DImport *s, uint32_t address, uint32_t cmd,
                                uint8_t *pb, uint8_t *io, GPtrArray *parts,
                                GPtrArray *parts2, uint32_t *result)
{
    uint16_t h = lduw_le_p(pb + 10), mode = lduw_le_p(pb + 30);
    uint32_t count = MIN(ldl_le_p(pb + 16), 65536u);
    uint32_t position = ldl_le_p(pb + 20), txn = ldl_le_p(pb + 4);
    int index = (int16_t)lduw_le_p(pb + 8), fd = -1, error;
    unsigned id = 0;
    bool path = p2d_gles_handles(&s->gles, parts);
    bool destination = p2d_gles_handles(&s->gles, parts2);
    bool handle = ((cmd >= 7 && cmd <= 0xa) || cmd == 0x13) &&
                  h < ARRAY_SIZE(s->files) && s->files[h].fd >= 0 &&
                  s->files[h].gles_id;
    bool search = cmd == 0x11 && index > 0 && txn < ARRAY_SIZE(s->find) &&
                  s->find[txn].gles_dir;
    struct stat st;
    ssize_t actual;
    if (!path && !destination && !handle && !search) {
        return false;
    }
    *result = 5;
    /* Closing a blocked handle still releases its owned descriptor. */
    error = cmd == 0xa && handle ? 0 : p2d_gles_collision(s);
    if (error) {
        *result = folder_error(error, false);
        return true;
    }
    if (cmd == 0xf || destination) {
        return true; /* Never move between namespaces or replace an asset. */
    }
    if (cmd == 0x11) {
        error = p2d_gles_getinfo(s, pb, parts);
        *result = error ? folder_error(error, index > 0) :
                  guest_copy(s, address, pb, PB_SIZE, true) ? 0 : 30;
        return true;
    }
    if (handle) {
        id = s->files[h].gles_id;
        fd = s->files[h].fd;
    } else {
        if (!parts || parts->len != 2) {
            *result = cmd == 0xc && parts && parts->len == 1 ? 80 : 5;
            return true;
        }
        id = p2d_gles_id(parts->pdata[1]);
        if (!id) {
            *result = 2;
            return true;
        }
    }
    bool writable = gles_files[id - 1].writable;
    if ((!writable && cmd != 7 && cmd != 0xa && cmd != 0x13 &&
         !(cmd == 6 && !(mode & 15))) ||
        (cmd == 8 && (!(s->files[h].mode & 15) || !writable)) ||
        (cmd == 9 && (!(s->files[h].mode & 15) || !writable))) {
        return true;
    }
    switch (cmd) {
    case 5:
        fd = folder_open_object(s->gles.fd, gles_files[id - 1].name,
                                 O_WRONLY | O_CREAT | O_EXCL, true);
        error = fd < 0 ? errno : 0;
        if (fd >= 0) {
            close(fd);
        }
        break;
    case 6:
        error = p2d_gles_stat(&s->gles, id, &st);
        if (error) {
            break;
        }
        for (h = 0; h < ARRAY_SIZE(s->files) && s->files[h].fd >= 0; h++) {}
        if (h == ARRAY_SIZE(s->files)) {
            error = EMFILE;
            break;
        }
        fd = folder_open_object(s->gles.fd, gles_files[id - 1].name,
                                 !(mode & 15) ? O_RDONLY :
                                 (mode & 15) == 1 ? O_WRONLY : O_RDWR, true);
        error = fd < 0 ? errno : 0;
        if (!error && (fstat(fd, &st) || st.st_nlink != 1 ||
                       st.st_size > gles_files[id - 1].limit)) {
            close(fd);
            fd = -1;
            error = EACCES;
        }
        if (!error) {
            s->files[h].fd = fd;
            s->files[h].mode = mode;
            s->files[h].position = 0;
            s->files[h].gles_id = id;
            s->files[h].name = g_strdup(gles_files[id - 1].name);
            stw_le_p(pb + 10, h);
        }
        break;
    case 7:
        if ((s->files[h].mode & 15) == 1) {
            return true;
        }
        error = fstat(fd, &st) ? errno : 0;
        if (!error && st.st_size > gles_files[id - 1].limit) {
            error = EFBIG;
        }
        if (error) {
            break;
        }
        if (!guest_copy(s, ldl_le_p(pb + 24), NULL, count, true)) {
            *result = 30;
            return true;
        }
        do {
            actual = pread(fd, io, count, position);
        } while (actual < 0 && errno == EINTR);
        error = actual < 0 ? errno : 0;
        if (!error && !guest_copy(s, ldl_le_p(pb + 24), io, actual, true)) {
            *result = 30;
            return true;
        }
        if (!error) {
            s->files[h].position = position + actual;
            stl_le_p(pb + 16, actual);
        }
        break;
    case 8:
        error = 0;
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
            error = EACCES;
            break;
        }
        if ((uint64_t)position + count > gles_files[id - 1].limit) {
            error = EFBIG;
            break;
        }
        if ((id == GLES_READY || id == GLES_SESSION) &&
            (position || count != 4)) {
            error = EINVAL;
            break;
        }
        if (!guest_copy(s, ldl_le_p(pb + 24), io, count, false)) {
            *result = 29;
            return true;
        }
        if (id == GLES_SESSION && !ldl_le_p(io)) {
            error = EINVAL;
            break;
        }
        do {
            actual = pwrite(fd, io, count, position);
        } while (actual < 0 && errno == EINTR);
        error = actual < 0 ? errno : actual != count ? EIO : 0;
        if (!error && id == GLES_READY) {
            error = p2d_gles_ready(&s->gles, ldl_le_p(io));
        } else if (!error && id == GLES_SESSION) {
            p2d_gles_context_reset(&s->gles);
            p2d_gles_remove(&s->gles, GLES_REQUEST);
            p2d_gles_remove(&s->gles, GLES_READY);
            p2d_gles_remove(&s->gles, GLES_RESPONSE);
        }
        if (!error) {
            s->files[h].position = position + actual;
            stl_le_p(pb + 16, actual);
        }
        break;
    case 9:
        error = position > gles_files[id - 1].limit ? EFBIG :
                ftruncate(fd, position) ? errno : 0;
        break;
    case 0xa:
        error = close(fd) ? errno : 0;
        s->files[h].fd = -1;
        s->files[h].gles_id = 0;
        g_clear_pointer(&s->files[h].name, g_free);
        break;
    case 0x10:
        error = p2d_gles_stat(&s->gles, id, &st);
        if (!error) {
            error = unlinkat(s->gles.fd, gles_files[id - 1].name, 0) ? errno : 0;
        }
        break;
    case 0xe:
        /* Synthetic transport metadata is fixed: assets remain read-only and
         * RPC stays writable. WinCE CREATE_ALWAYS may clear archive attrs. */
        error = p2d_gles_stat(&s->gles, id, &st);
        break;
    case 0x13:
        error = fstat(fd, &st) ? errno : 0;
        if (!error) {
            stl_le_p(pb + 20, s->files[h].position);
            stw_le_p(pb + 30, s->files[h].mode);
            error = p2d_gles_info(pb, &st, s->files[h].name, true,
                                   !writable) ? 0 : EINVAL;
        }
        break;
    default:
        return true;
    }
    if (!error && !guest_copy(s, address, pb, PB_SIZE, true)) {
        if (cmd == 6) {
            close(s->files[h].fd);
            s->files[h].fd = -1;
            s->files[h].gles_id = 0;
            g_clear_pointer(&s->files[h].name, g_free);
        }
        *result = 30;
    } else {
        *result = folder_error(error, false);
    }
    return true;
}
