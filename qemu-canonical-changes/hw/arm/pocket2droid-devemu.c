/*
 * Pocket2Droid Device Emulator compatibility experiment.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Local prototype, not an upstream contribution.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/arm/pocket2droid-devemu.h"
#include "hw/arm/p2d-frame.h"
#include "system/address-spaces.h"
#include "system/reset.h"
#include "system/system.h"
#include "system/ramblock.h"
#include "system/runstate.h"
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include "target/arm/cpu.h"
#include "qom/object.h"
#include "net/net.h"
#include "hw/net/p2d-ne2000.h"
#include <sys/file.h>

#define TYPE_P2D_MACHINE MACHINE_TYPE_NAME("p2d-devemu")
OBJECT_DECLARE_SIMPLE_TYPE(P2DMachineState, P2D_MACHINE)

struct P2DMachineState {
    MachineState parent;
    ARMCPU *cpu;
    MemoryRegion rom;
    MemoryRegion flash_id_window;
    MemoryRegion storage;
    MemoryRegion retained_ram;
    char *ram_path;
    char *ram_identity;
    char *storage_path;
    char *storage_lock_path;
    int storage_lock_fd;
    char *import_path;
    char *shared_folder;
    char *network_setup;
    char *gles_assets;
    char *gles_renderer;
    char *frame_path;
    char *audio_path;
    int64_t parent_pid;
    unsigned screen_width, screen_height;
    Notifier storage_exit;
    uint8_t flash_unlock;
    bool flash_id;
    bool network;
    uint32_t boot_pc;
};

typedef struct P2DBinSection {
    uint32_t address;
    uint32_t size;
    size_t offset;
} P2DBinSection;

/* Bound per-record loader allocations, including QEMU's retained ROM blobs.
 * The four original WM5 SDK images contain 99..110 records.
 */
#define P2D_BIN_MAX_SECTIONS 4096

static gint p2d_bin_section_compare(gconstpointer a, gconstpointer b)
{
    const P2DBinSection *left = a;
    const P2DBinSection *right = b;

    return (left->address > right->address) -
           (left->address < right->address);
}

static G_NORETURN void p2d_bin_error(const char *filename, const char *reason)
{
    error_report("Invalid Device Emulator B000FF image '%s': %s",
                 filename, reason);
    exit(EXIT_FAILURE);
}

/* Device Emulator SDK BIN files are download records, not raw NOR contents.
 * Their cached kernel VAs map to SDRAM through this board's OEM address table:
 * 0x80000000..0x83ffffff -> 0x30000000..0x33ffffff. The final record supplies
 * the entry VA. Validate the complete container before registering any RAM
 * writes; never allow records to reach the persistent RAM-FMD at 0x34000000.
 * QEMU's ROM loader retains the original record bytes and replays them at reset.
 */
static bool p2d_load_bin(P2DMachineState *s, const char *filename)
{
    uint8_t magic[7];
    FILE *file = fopen(filename, "rb");
    g_autofree char *contents = NULL;
    g_autoptr(GArray) sections = g_array_new(false, false, sizeof(P2DBinSection));
    const uint8_t *data;
    gsize size;
    size_t offset;
    int64_t file_size;
    uint32_t base, span, entry = 0;
    uint64_t end;
    bool terminated = false, entry_present = false;
    unsigned i, j;

    if (!file) {
        return false; /* The existing raw loader reports file-access errors. */
    }
    if (fread(magic, 1, sizeof(magic), file) != sizeof(magic) ||
        memcmp(magic, "B000FF\n", sizeof(magic))) {
        fclose(file);
        return false;
    }
    fclose(file);
    file_size = get_image_size(filename, NULL);
    if (file_size < 15 || file_size > 96 * MiB ||
        !g_file_get_contents(filename, &contents, &size, NULL) ||
        size != file_size || memcmp(contents, magic, sizeof(magic))) {
        p2d_bin_error(filename, "unreadable, truncated or oversized container");
    }
    data = (const uint8_t *)contents;
    base = ldl_le_p(data + 7);
    span = ldl_le_p(data + 11);
    end = (uint64_t)base + span;
    if (base < 0x80000000u || !span || end > 0x84000000u) {
        p2d_bin_error(filename, "image span is outside cached SDRAM");
    }
    offset = 15;
    while (size - offset >= 12) {
        P2DBinSection section;
        uint32_t checksum, actual = 0;

        section.address = ldl_le_p(data + offset);
        section.size = ldl_le_p(data + offset + 4);
        checksum = ldl_le_p(data + offset + 8);
        offset += 12;
        if (!section.address) {
            entry = section.size;
            if (checksum || offset != size) {
                p2d_bin_error(filename, "invalid terminator or trailing bytes");
            }
            terminated = true;
            break;
        }
        if (!section.size || section.address < base ||
            (uint64_t)section.address + section.size > end ||
            section.size > size - offset) {
            p2d_bin_error(filename, "record is truncated or outside the image span");
        }
        if (section.address < 0x8002004eu &&
            (uint64_t)section.address + section.size > 0x80020044u) {
            p2d_bin_error(filename, "record overlaps board boot arguments");
        }
        if (sections->len == P2D_BIN_MAX_SECTIONS) {
            p2d_bin_error(filename, "record count exceeds the loader limit");
        }
        for (j = 0; j < section.size; j++) {
            actual += data[offset + j];
        }
        if (actual != checksum) {
            p2d_bin_error(filename, "record checksum mismatch");
        }
        section.offset = offset;
        g_array_append_val(sections, section);
        offset += section.size;
    }
    if (!terminated || !sections->len || (entry & 3)) {
        p2d_bin_error(filename, "missing records/terminator or unaligned ARM entry");
    }
    g_array_sort(sections, p2d_bin_section_compare);
    for (i = 0; i < sections->len; i++) {
        const P2DBinSection *section = &g_array_index(sections, P2DBinSection, i);
        if (i) {
            const P2DBinSection *previous = &g_array_index(sections, P2DBinSection, i - 1);
            if ((uint64_t)previous->address + previous->size > section->address) {
                p2d_bin_error(filename, "overlapping records");
            }
        }
        if (entry >= section->address &&
            (uint64_t)entry + 4 <= (uint64_t)section->address + section->size) {
            entry_present = true;
        }
    }
    if (!entry_present) {
        p2d_bin_error(filename, "entry point is not backed by a record");
    }
    /* rom_insert maintains an ascending list. Descending insertion avoids
     * repeatedly walking all preceding records for a highly segmented image.
     */
    for (i = sections->len; i > 0; i--) {
        const P2DBinSection *section = &g_array_index(sections, P2DBinSection, i - 1);
        g_autofree char *name = g_strdup_printf("p2d.wince-bin.%u", i);

        rom_add_blob_fixed(name, data + section->offset, section->size,
                           section->address - 0x50000000u);
    }
    s->boot_pc = entry - 0x50000000u;
    return true;
}

static uint64_t flash_read(void *opaque, hwaddr addr, unsigned size)
{
    P2DMachineState *s = opaque;
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < size; ++i) {
        uint8_t b;
        if (s->flash_id && addr + i < 4) {
            static const uint8_t id[4] = { 1, 0, 0x5b, 0x22 };
            b = id[addr + i];
        } else {
            b = ((uint8_t *)memory_region_get_ram_ptr(&s->rom))[addr + i];
        }
        value |= (uint64_t)b << (8 * i);
    }
    return value;
}

static void flash_write(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size)
{
    P2DMachineState *s = opaque;

    if (size != 2) {
        return;
    }
    if (value == 0xf0f0) {
        s->flash_id = false;
        s->flash_unlock = 0;
        memory_region_set_enabled(&s->flash_id_window, false);
    } else if (s->flash_unlock == 0 && (addr & 0xffffff) == 0xaaaa && value == 0xaaaa) {
        s->flash_unlock = 1;
    } else if (s->flash_unlock == 1 && (addr & 0xffffff) == 0x5554 && value == 0x5555) {
        s->flash_unlock = 2;
    } else if (s->flash_unlock == 2 && (addr & 0xffffff) == 0xaaaa && value == 0x9090) {
        s->flash_unlock = 0;
        s->flash_id = true;
        memory_region_set_enabled(&s->flash_id_window, true);
    } else {
        s->flash_unlock = 0;
    }
}

static const MemoryRegionOps flash_ops = {
    .read = flash_read,
    .write = flash_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void p2d_reset(void *opaque)
{
    P2DMachineState *s = opaque;
    uint8_t *ram = memory_region_get_ram_ptr(s->ram_path ?
                                            &s->retained_ram : s->parent.ram);

    cpu_reset(CPU(s->cpu));
    s->cpu->env.regs[13] = 0x30010000;
    s->cpu->env.regs[15] = s->boot_pc;
    /* Device Emulator BSP boot arguments, independent of CPU CP15 reset. */
    stl_le_p(ram + 0x20044, 0xde12de34);
    stw_le_p(ram + 0x20048, s->screen_width ? s->screen_width : 240);
    stw_le_p(ram + 0x2004a, s->screen_height ? s->screen_height : 320);
    stw_le_p(ram + 0x2004c, 16);
    s->flash_id = false;
    s->flash_unlock = 0;
    memory_region_set_enabled(&s->flash_id_window, false);
}

static void storage_flush(Notifier *notify, void *data)
{
    P2DMachineState *s = container_of(notify, P2DMachineState, storage_exit);
    qemu_ram_block_writeback(s->storage.ram_block);
    /* A rejected file can exit during machine initialization, before this RAM
     * region exists. The exit notifier must still flush the established FMD.
     */
    if (s->retained_ram.ram_block) {
        qemu_ram_block_writeback(s->retained_ram.ram_block);
    }
}

static void storage_state_change(void *opaque, bool running, RunState state)
{
    P2DMachineState *s = opaque;
    /* Stop is acknowledged after the CPU stops and this writeback completes. */
    if (!running) {
        storage_flush(&s->storage_exit, NULL);
    }
}

static char *p2d_get_frame(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->frame_path);
}
static void p2d_set_frame(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->frame_path); s->frame_path = g_strdup(value);
}
static char *p2d_get_parent(Object *obj, Error **errp)
{
    return g_strdup_printf("%" PRId64, P2D_MACHINE(obj)->parent_pid);
}
static void p2d_set_parent(Object *obj, const char *value, Error **errp)
{
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || end == value || *end || parsed < 0 || parsed > INT_MAX) {
        error_setg(errp, "Invalid parent-pid"); return;
    }
    P2D_MACHINE(obj)->parent_pid = parsed;
}

static char *p2d_get_storage(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->storage_path);
}

static void p2d_set_storage(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->storage_path);
    s->storage_path = g_strdup(value);
}

static char *p2d_get_storage_lock(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->storage_lock_path);
}

static void p2d_set_storage_lock(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->storage_lock_path);
    s->storage_lock_path = g_strdup(value);
}

static void storage_guard(P2DMachineState *s, const struct stat *storage_stat)
{
    struct stat st;
    int saved;
    int fd = qemu_open_old(s->storage_lock_path,
                           O_RDWR | O_CREAT | O_NOFOLLOW, 0600);

    if (fd < 0) {
        error_report("Cannot open private storage guard: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_dev == storage_stat->st_dev && st.st_ino == storage_stat->st_ino)) {
        close(fd);
        error_report("Invalid private storage guard; must be a separately owned regular file");
        exit(EXIT_FAILURE);
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        saved = errno;
        close(fd);
        error_report("Cannot lock private storage guard: %s", strerror(saved));
        exit(EXIT_FAILURE);
    }
    /* Persistent app-private inode, never unlinked. Retain its CLOEXEC fd until
     * process exit, after the existing RAM writeback/exit notifiers complete.
     * Pausing must not release ownership of the externally mapped save.
     */
    s->storage_lock_fd = fd;
}

static char *p2d_get_import(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->import_path);
}

static void p2d_set_import(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->import_path);
    s->import_path = g_strdup(value);
}

static char *p2d_get_shared_folder(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->shared_folder);
}

static void p2d_set_shared_folder(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->shared_folder);
    s->shared_folder = g_strdup(value);
}

static char *p2d_get_gles_assets(Object *obj, Error **errp)
{ return g_strdup(P2D_MACHINE(obj)->gles_assets); }
static void p2d_set_gles_assets(Object *obj, const char *value, Error **errp)
{ P2DMachineState *s=P2D_MACHINE(obj); g_free(s->gles_assets); s->gles_assets=g_strdup(value); }
static char *p2d_get_gles_renderer(Object *obj, Error **errp)
{ return g_strdup(P2D_MACHINE(obj)->gles_renderer); }
static void p2d_set_gles_renderer(Object *obj, const char *value, Error **errp)
{ P2DMachineState *s=P2D_MACHINE(obj); g_free(s->gles_renderer); s->gles_renderer=g_strdup(value); }

static char *p2d_get_network_setup(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->network_setup);
}

static void p2d_set_network_setup(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->network_setup);
    s->network_setup = g_strdup(value);
}

static void p2d_init(MachineState *machine)
{
    P2DMachineState *s = P2D_MACHINE(machine);
    Object *cpu;
    DeviceState *soc, *lcd, *media;

    s->storage_lock_fd = -1;
    if (s->storage_lock_path && !s->storage_path) {
        error_report("private storage guard requires a storage file");
        exit(EXIT_FAILURE);
    }

    if (s->parent_pid) {
#ifdef __linux__
        if (prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != s->parent_pid) {
            error_report("Cannot bind engine lifetime to the specified app parent");
            exit(EXIT_FAILURE);
        }
#else
        error_report("parent-pid requires Linux"); exit(EXIT_FAILURE);
#endif
    }

    if (machine->ram_size != 128 * MiB || !machine->firmware) {
        error_report("p2d-devemu requires 128MiB RAM and a raw or Device Emulator B000FF -bios image");
        exit(EXIT_FAILURE);
    }
    cpu = object_new(machine->cpu_type);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);
    s->cpu = ARM_CPU(cpu);
    /* Optional experimental GLES compatibility. Disabled mode retains the
     * original ARM920T feature set, reset, MMU and device behavior. */
    if (s->gles_renderer && *s->gles_renderer) {
        if (!s->gles_assets || !*s->gles_assets) {
            error_report("GLES renderer requires private GLES assets"); exit(EXIT_FAILURE);
        }
        s->cpu->env.features |= UINT64_C(1) << ARM_FEATURE_V5;
    }
    soc = qdev_new(TYPE_P2D_SOC);
    object_property_set_link(OBJECT(soc), "cpu", cpu, &error_fatal);
    if (s->import_path) {
        qdev_prop_set_string(soc, "import-file", s->import_path);
    }
    if (s->shared_folder) {
        qdev_prop_set_string(soc, "shared-folder", s->shared_folder);
        if (s->storage_lock_path) {
            g_autofree char *private_directory = g_path_get_dirname(s->storage_lock_path);
            qdev_prop_set_string(soc, "shared-move-state", private_directory);
        }
    }
    if (s->gles_assets) qdev_prop_set_string(soc, "gles-assets", s->gles_assets);
    if (s->gles_renderer) qdev_prop_set_string(soc, "gles-renderer", s->gles_renderer);
    if (s->network_setup && (s->network || s->gles_assets) && s->shared_folder) {
        qdev_prop_set_string(soc, "network-setup", s->network_setup);
    }
    object_property_add_child(OBJECT(machine), "p2d-soc", OBJECT(soc));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(soc), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(soc), 0, 0x48000000);
    sysbus_mmio_map(SYS_BUS_DEVICE(soc), 1, 0x10000000);
    if (s->network) {
        DeviceState *nic = qdev_new(TYPE_P2D_NE2000);
        qemu_configure_nic_device(nic, true, "ne2k_pcmcia");
        object_property_add_child(OBJECT(machine), "p2d-network", OBJECT(nic));
        sysbus_realize_and_unref(SYS_BUS_DEVICE(nic), &error_fatal);
        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(nic), 0, 0x10000000, 1);
        sysbus_mmio_map_overlap(SYS_BUS_DEVICE(nic), 1, 0x11000000, 1);
        sysbus_connect_irq(SYS_BUS_DEVICE(nic), 0, qdev_get_gpio_in_named(soc, "pcmcia", 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(nic), 1, qdev_get_gpio_in_named(soc, "pcmcia", 1));
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(soc), 0,
                      qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(soc), 1,
                      qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ));
    lcd = qdev_new(TYPE_P2D_LCD);
    if (s->frame_path) { qdev_prop_set_string(lcd, "frame-file", s->frame_path); }
    object_property_add_child(OBJECT(machine), "p2d-lcd", OBJECT(lcd));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(lcd), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(lcd), 0, 0x4d000000, 1);
    media = qdev_new(TYPE_P2D_MEDIA);
    if (s->audio_path) { qdev_prop_set_string(media, "audio-file", s->audio_path); }
    object_property_add_child(OBJECT(machine), "p2d-media", OBJECT(media));
    qdev_connect_clock_in(media, "pclk", qdev_get_clock_out(soc, "pclk"));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(media), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(media), 0, 0x4b000080, 1);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(media), 1, 0x55000000, 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(media), 0, qdev_get_gpio_in(soc, 0));
    memory_region_add_subregion(get_system_memory(), 0x30000000, machine->ram);
    if (s->storage_path) {
        struct stat st;
        int fd = qemu_open_old(s->storage_path, O_RDWR | O_NOFOLLOW);
        if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size != 32 * MiB) {
            if (fd >= 0) {
                close(fd);
            }
            error_report("p2d storage must be an existing regular file of exactly 32MiB");
            exit(EXIT_FAILURE);
        }
        /* The caller selects existing storage. Never create/truncate here.
         * RAM-FMD is physical SDRAM, not a synthetic block device or CPU state.
         */
        if (s->storage_lock_path) {
            storage_guard(s, &st);
        }
        if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
            int saved = errno;
            /* Android's general emulated-storage FUSE does not implement flock.
             * Only an already-held, explicit private guard permits that case.
             * Busy locks and all other failures still fail closed. Ordinary CLI
             * storage retains its original lock requirement.
             */
            if (s->storage_lock_fd < 0 ||
                (saved != ENOSYS && saved != EOPNOTSUPP)) {
                close(fd);
                error_report("p2d storage is already owned by another engine or cannot be locked: %s",
                             strerror(saved));
                exit(EXIT_FAILURE);
            }
            info_report("p2d storage filesystem lacks flock; private storage guard held");
        }
        memory_region_init_ram_from_fd(&s->storage, NULL, "p2d.ram-fmd",
                                       32 * MiB, RAM_SHARED, fd, 0, &error_fatal);
        memory_region_add_subregion_overlap(get_system_memory(), 0x34000000,
                                            &s->storage, 2);
        s->storage_exit.notify = storage_flush;
        qemu_add_exit_notifier(&s->storage_exit);
        qemu_add_vm_change_state_handler(storage_state_change, s);
    }
    memory_region_init_rom_device(&s->rom, NULL, &flash_ops, s,
                                 "p2d.nor", 96 * MiB, &error_fatal);
    memory_region_add_subregion(get_system_memory(), 0, &s->rom);
    /* Only the first four bytes change in ID mode. Disabling ROMD for the
     * entire NOR makes every ROM instruction an uncacheable MMIO fetch,
     * forcing TCG to compile it again on every execution. A narrow overlay
     * retains the same ID/command behavior and lets unchanged ROM code reuse
     * translations. QEMU handles the mapping-change/TLB invalidation.
     */
    memory_region_init_io(&s->flash_id_window, NULL, &flash_ops, s,
                          "p2d.nor-id", 4);
    memory_region_set_enabled(&s->flash_id_window, false);
    memory_region_add_subregion_overlap(get_system_memory(), 0,
                                        &s->flash_id_window, 1);
    if (!p2d_load_bin(s, machine->firmware) &&
        load_image_mr(machine->firmware, &s->rom) < 0) {
        error_report("Cannot load raw ROM %s", machine->firmware);
        exit(EXIT_FAILURE);
    }
    if (s->ram_path) {
        struct stat st;
        char *split, *end;
        uint64_t device = 0, inode = 0;
        int fd;
        bool valid = s->boot_pc && s->storage_path &&
                     machine->ram_size == 128 * MiB && s->ram_identity;
        g_autofree char *identity = g_strdup(s->ram_identity);

        split = identity ? strchr(identity, ':') : NULL;
        if (!split || split == identity || !split[1]) {
            valid = false;
        } else {
            *split++ = 0;
            errno = 0;
            device = g_ascii_strtoull(identity, &end, 16);
            valid = valid && !errno && !*end &&
                    strspn(identity, "0123456789abcdefABCDEF") == strlen(identity);
            errno = 0;
            inode = g_ascii_strtoull(split, &end, 16);
            valid = valid && !errno && !*end &&
                    strspn(split, "0123456789abcdefABCDEF") == strlen(split);
        }
        fd = valid ? qemu_open_old(s->ram_path, O_RDWR | O_NOFOLLOW) : -1;
        if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) ||
            st.st_size != 128 * MiB || (uint64_t)st.st_dev != device ||
            (uint64_t)st.st_ino != inode) {
            if (fd >= 0) {
                close(fd);
            }
            error_report("Invalid WM5 retained RAM: missing, redirected, replaced, wrong-sized or incompatible file");
            exit(EXIT_FAILURE);
        }
        if (flock(fd, LOCK_EX | LOCK_NB) < 0 &&
            (s->storage_lock_fd < 0 || (errno != ENOSYS && errno != EOPNOTSUPP))) {
            close(fd);
            error_report("Invalid WM5 retained RAM: file already owned or cannot be locked");
            exit(EXIT_FAILURE);
        }
        memory_region_init_ram_from_fd(&s->retained_ram, NULL, "p2d.wm5-sdram",
                                       128 * MiB, RAM_SHARED, fd, 0, &error_fatal);
        memory_region_add_subregion_overlap(get_system_memory(), 0x30000000,
                                            &s->retained_ram, 1);
    }
    qemu_register_reset(p2d_reset, s);
}

static char *p2d_get_ram_path(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->ram_path);
}
static void p2d_set_ram_path(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->ram_path); s->ram_path = g_strdup(value);
}
static char *p2d_get_ram_identity(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->ram_identity);
}
static void p2d_set_ram_identity(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->ram_identity); s->ram_identity = g_strdup(value);
}

static char *p2d_get_audio(Object *obj, Error **errp)
{
    return g_strdup(P2D_MACHINE(obj)->audio_path);
}
static unsigned p2d_dimension(const char *value, unsigned limit, Error **errp)
{
    char *end;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno || end == value || *end || parsed < 1 || parsed > limit) {
        error_setg(errp, "Invalid screen-dimension; expected 1..%u", limit);
        return 0;
    }
    return parsed;
}
static char *p2d_get_width(Object *obj, Error **errp)
{
    unsigned size = P2D_MACHINE(obj)->screen_width;
    return g_strdup_printf("%u", size ? size : 240);
}
static void p2d_set_width(Object *obj, const char *value, Error **errp)
{
    /* SDK boot arguments store each dimension in an unsigned 16-bit word. */
    unsigned size = p2d_dimension(value, UINT16_MAX, errp);
    if (size) { P2D_MACHINE(obj)->screen_width = size; }
}
static char *p2d_get_height(Object *obj, Error **errp)
{
    unsigned size = P2D_MACHINE(obj)->screen_height;
    return g_strdup_printf("%u", size ? size : 320);
}
static void p2d_set_height(Object *obj, const char *value, Error **errp)
{
    unsigned size = p2d_dimension(value, UINT16_MAX, errp);
    if (size) { P2D_MACHINE(obj)->screen_height = size; }
}
static void p2d_set_audio(Object *obj, const char *value, Error **errp)
{
    P2DMachineState *s = P2D_MACHINE(obj);
    g_free(s->audio_path); s->audio_path = g_strdup(value);
}
static bool p2d_get_network(Object *obj, Error **errp)
{
    return P2D_MACHINE(obj)->network;
}
static void p2d_set_network(Object *obj, bool value, Error **errp)
{
    P2D_MACHINE(obj)->network = value;
}
static void p2d_class_init(ObjectClass *oc, const void *data)
{
    static const char * const cpu_types[] = {
        ARM_CPU_TYPE_NAME("arm920t"), NULL,
    };
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Pocket2Droid Device Emulator (ARM920T), local prototype";
    mc->init = p2d_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm920t");
    mc->valid_cpu_types = cpu_types;
    mc->default_ram_size = 128 * MiB;
    mc->default_ram_id = "p2d.sdram";
    /* Compatibility for the ROM's RAM-size probes, tested separately. */
    mc->ignore_memory_transaction_failures = true;
    object_class_property_add_str(oc, "storage", p2d_get_storage, p2d_set_storage);
    object_class_property_add_bool(oc, "network", p2d_get_network, p2d_set_network);
    object_class_property_add_str(oc, "ram-path", p2d_get_ram_path, p2d_set_ram_path);
    object_class_property_add_str(oc, "ram-identity", p2d_get_ram_identity,
                                   p2d_set_ram_identity);
    object_class_property_add_str(oc, "storage-lock", p2d_get_storage_lock,
                                   p2d_set_storage_lock);
    object_class_property_add_str(oc, "import-file", p2d_get_import, p2d_set_import);
    object_class_property_add_str(oc, "shared-folder", p2d_get_shared_folder,
                                   p2d_set_shared_folder);
    object_class_property_add_str(oc, "gles-assets", p2d_get_gles_assets, p2d_set_gles_assets);
    object_class_property_add_str(oc, "gles-renderer", p2d_get_gles_renderer, p2d_set_gles_renderer);
    object_class_property_add_str(oc, "network-setup", p2d_get_network_setup,
                                   p2d_set_network_setup);
    object_class_property_add_str(oc, "frame-file", p2d_get_frame, p2d_set_frame);
    object_class_property_add_str(oc, "audio-file", p2d_get_audio, p2d_set_audio);
    object_class_property_add_str(oc, "parent-pid", p2d_get_parent, p2d_set_parent);
    object_class_property_add_str(oc, "screen-width", p2d_get_width, p2d_set_width);
    object_class_property_add_str(oc, "screen-height", p2d_get_height, p2d_set_height);
}

static const TypeInfo p2d_type = {
    .name = TYPE_P2D_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(P2DMachineState),
    .class_init = p2d_class_init,
};

static void p2d_register(void)
{
    type_register_static(&p2d_type);
}
type_init(p2d_register)
