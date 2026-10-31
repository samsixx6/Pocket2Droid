/*
 * Local ARM920T Device Emulator board devices.
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Register contract: board_contract.json, S3C2410 timer/interrupt definitions.
 * Networking/audio output are absent. Optional VCEFS shared-folder is read/write.
 */
#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/arm/pocket2droid-devemu.h"
#include "hw/arm/p2d-import.h"
#include "target/arm/cpu.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qemu/timer.h"
#include "system/rtc.h"
#include "qemu/log.h"
#include "qemu/bitops.h"
#include "system/address-spaces.h"
#include "ui/input.h"

typedef struct P2DSoc P2DSoc;
typedef struct P2DWindow {
    MemoryRegion mr;
    P2DSoc *soc;
    uint32_t base;
} P2DWindow;

struct P2DSoc {
    SysBusDevice parent;
    P2DWindow peripheral, expansion;
    qemu_irq irq, fiq;
    GHashTable *shadow, *reported;
    uint32_t intc[8];
    uint32_t timer_regs[17];
    uint32_t mpll, clkdiv;
    QEMUTimer *timer4;
    int64_t timer_start, timer_deadline;
    uint32_t timer_count;
    uint64_t timer_hz;
    bool timer_running;
    uint32_t adccon, adctsc, adcdly;
    uint32_t x, y;
    bool down, touch_pending;
    uint32_t eintmask, eintpend;
    uint8_t pd_index, pd_regs[0x45], pd_chip;
    bool pcmcia_level[2];
    uint32_t folder[4], emul_mask, emul_pending;
    ARMCPU *cpu;
    char *import_file;
    char *shared_folder;
    char *shared_move_state;
    char *network_setup;
    char *gles_assets, *gles_renderer;
    P2DImport import;
    uint32_t spi1[6];
    uint8_t keys[32], key_head, key_count;
    QemuInputHandlerState *input;
    Clock *pclk;
};
OBJECT_DECLARE_SIMPLE_TYPE(P2DSoc, P2D_SOC)

static void intc_update(P2DSoc *s)
{
    uint32_t active;
    uint32_t subs = s->intc[6] & ~s->intc[7];
    uint32_t ext;
    if (s->pcmcia_level[0]) { s->eintpend |= BIT(8); }
    if (s->pcmcia_level[1]) { s->intc[0] |= BIT(3); }
    ext = s->eintpend & ~s->eintmask;

    if (s->key_count) {
        s->intc[0] |= BIT(1); /* Keyboard queue drives EINT1 until drained. */
    }

    if (subs & (BIT(9) | BIT(10))) {
        s->intc[0] |= BIT(31);
    }
    if (ext & 0xf0) {
        s->intc[0] |= BIT(4);
    }
    if (ext & 0xffff00) {
        s->intc[0] |= BIT(5);
    }
    active = s->intc[0] & ~s->intc[2] & ~s->intc[1];
    /* INTPND and INTOFFSET latch the selected IRQ until their own W1C ack.
     * Clearing SRCPND or a new source must not replace that selection.
     * Programmable arbiter priority/rotation remains an adoption gate.
     */
    if (!s->intc[4] && active) {
        s->intc[5] = ctz32(active);
        s->intc[4] = BIT(s->intc[5]);
    }
    qemu_set_irq(s->irq, s->intc[4] != 0);
    qemu_set_irq(s->fiq, (s->intc[0] & ~s->intc[2] & s->intc[1]) != 0);
}

static void media_irq(void *opaque, int line, int level)
{
    P2DSoc *s = opaque;
    if (level) {
        s->intc[0] |= BIT(19);
        intc_update(s);
    }
}

static void pcmcia_irq(void *opaque, int line, int level)
{
    P2DSoc *s = opaque;
    s->pcmcia_level[line] = level;
    intc_update(s);
}

static uint64_t peripheral_frequency(P2DSoc *s)
{
    uint64_t f = 12000000ULL * (((s->mpll >> 12) & 255) + 8);

    f /= (((s->mpll >> 4) & 63) + 2) << (s->mpll & 3);
    if (s->clkdiv & 2) {
        f /= 2;
    }
    if (s->clkdiv & 1) {
        f /= 2;
    }
    return f;
}

static uint64_t timer_frequency(P2DSoc *s)
{
    uint64_t f = peripheral_frequency(s);
    unsigned mux = (s->timer_regs[1] >> 16) & 15;
    f /= ((s->timer_regs[0] >> 8) & 255) + 1;
    if (mux < 4) {
        f /= 2 << mux;
    } else {
        /* External TCLK is not wired on this board. */
        return 0;
    }
    return f;
}

static uint32_t timer_current(P2DSoc *s)
{
    uint64_t elapsed;
    if (!s->timer_running || !s->timer_hz) {
        return s->timer_count;
    }
    elapsed = muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->timer_start,
                       s->timer_hz, NANOSECONDS_PER_SECOND);
    return elapsed >= s->timer_count ? 0 : s->timer_count - elapsed;
}

static void timer_schedule(P2DSoc *s)
{
    int64_t delay;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* A deadline can pass again between catch-up and a clock-register write.
     * Keep that queued expiry instead of deleting the only source of future
     * system ticks. Its callback applies the updated rate on reload.
     */
    if (s->timer_running && !s->timer_count &&
        timer_pending(s->timer4) && timer_expired(s->timer4, now)) {
        s->timer_hz = timer_frequency(s);
        return;
    }
    timer_del(s->timer4);
    s->timer_hz = timer_frequency(s);
    s->timer_start = now;
    if (!s->timer_running || !s->timer_hz || !s->timer_count) {
        return;
    }
    delay = (s->timer_count * NANOSECONDS_PER_SECOND + s->timer_hz - 1) / s->timer_hz;
    s->timer_deadline = s->timer_start + MAX(delay, 1);
    timer_mod(s->timer4, s->timer_deadline);
}

static void timer_before_update(P2DSoc *s)
{
    if (qemu_clock_expired(QEMU_CLOCK_VIRTUAL)) {
        /* MMIO holds BQL. Drain logical deadlines before changing Timer4:
         * audio may be deferred by a host wake hint even if Timer4 itself
         * is unarmed or not due. Its predecessor determines insertion order
         * at future equal deadlines. Unrelated Timer0-3 writes bypass this.
         */
        qemu_clock_run_timers(QEMU_CLOCK_VIRTUAL);
    }
}

static void p2d_timer_expired(void *opaque)
{
    P2DSoc *s = opaque;
    s->intc[0] |= BIT(14);
    intc_update(s);
    if (s->timer_regs[2] & BIT(22)) {
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        int64_t period;
        s->timer_count = s->timer_regs[15] & 0xffff;
        if (!s->timer_count || !s->timer_hz) {
            timer_del(s->timer4);
            return;
        }
        period = MAX(1, (s->timer_count * NANOSECONDS_PER_SECOND +
                         s->timer_hz - 1) / s->timer_hz);
        /* Overdue reloads assert the same sticky source bit. Fold them in O(1)
         * and retain phase instead of extending the period by host lateness.
         */
        s->timer_start = s->timer_deadline +
                         ((now - s->timer_deadline) / period) * period;
        s->timer_deadline = s->timer_start + period;
        timer_mod(s->timer4, s->timer_deadline);
    } else {
        s->timer_running = false;
        s->timer_count = 0;
        timer_del(s->timer4);
    }
}

static void adc_complete(P2DSoc *s)
{
    if (s->adccon & BIT(2)) {
        return;
    }
    s->adccon = (s->adccon & ~1) | BIT(15);
    s->intc[6] |= BIT(10);
    intc_update(s);
}

static void touch_sync(DeviceState *dev)
{
    P2DSoc *s = P2D_SOC(dev);
    if (s->touch_pending && (s->adctsc & 7) == 3) {
        s->touch_pending = false;
        s->intc[6] |= BIT(9);
        intc_update(s);
    }
}

static void touch_event(DeviceState *dev, QemuConsole *src, QemuInputEvent *event)
{
    P2DSoc *s = P2D_SOC(dev);
    if (event->type == INPUT_EVENT_KIND_KEY) {
        unsigned code, count = 1;
        bool function = false;
        if (((s->spi1[0] >> 5) & 3) != 1) {
            return;
        }
        /* CE keyboard scan codes, as in Device Emulator's VK-to-SC mapping.
         * SDK phone skins use F1/F2/F3/F4 for soft keys/Talk/End. Those become
         * Fn + 1/2/3/4, with Fn released after each make OR break event.
         */
        switch (qemu_input_linux_to_qcode(event->key.key)) {
        case Q_KEY_CODE_UP: code = 0x6c; break;
        case Q_KEY_CODE_DOWN: code = 0x6a; break;
        case Q_KEY_CODE_LEFT: code = 0x6d; break;
        case Q_KEY_CODE_RIGHT: code = 0x6f; break;
        case Q_KEY_CODE_RET: code = 0x5a; break;
        case Q_KEY_CODE_ESC: code = 0x29; break;
        case Q_KEY_CODE_SPC: code = 0x6e; break;
        case Q_KEY_CODE_TAB: code = 0x0b; break;
        case Q_KEY_CODE_BACKSPACE: code = 0x69; break;
        case Q_KEY_CODE_META_L: code = 0x60; break;
        case Q_KEY_CODE_0: code = 0x49; break;
        case Q_KEY_CODE_1: code = 0x31; break;
        case Q_KEY_CODE_2: code = 0x39; break;
        case Q_KEY_CODE_3: code = 0x2f; break;
        case Q_KEY_CODE_4: code = 0x37; break;
        case Q_KEY_CODE_5: code = 0x3f; break;
        case Q_KEY_CODE_6: code = 0x47; break;
        case Q_KEY_CODE_7: code = 0x4f; break;
        case Q_KEY_CODE_8: code = 0x57; break;
        case Q_KEY_CODE_9: code = 0x41; break;
        case Q_KEY_CODE_F1: code = 0x31; function = true; break;
        case Q_KEY_CODE_F2: code = 0x39; function = true; break;
        case Q_KEY_CODE_F3: code = 0x2f; function = true; break;
        case Q_KEY_CODE_F4: code = 0x37; function = true; break;
        /* SDK Smartphone skins use F8/F9 for star/hash. Preserve the
         * original emulator's Fn + 8/9 make AND break protocol. */
        case Q_KEY_CODE_F8: code = 0x57; function = true; break;
        case Q_KEY_CODE_F9: code = 0x41; function = true; break;
        default: return;
        }
        count = function ? 3 : 1;
        /* Admit complete sequences so queue pressure cannot strand Fn down. */
        if (s->key_count + count <= ARRAY_SIZE(s->keys)) {
            unsigned tail = (s->key_head + s->key_count) % ARRAY_SIZE(s->keys);
            if (function) {
                s->keys[tail] = 0x63;
                s->keys[(tail + 1) % ARRAY_SIZE(s->keys)] = code | (event->key.down ? 0 : 0x80);
                s->keys[(tail + 2) % ARRAY_SIZE(s->keys)] = 0xe3;
            } else {
                s->keys[tail] = code | (event->key.down ? 0 : 0x80);
            }
            s->key_count += count;
            intc_update(s);
        }
        return;
    }
    if (event->type == INPUT_EVENT_KIND_ABS) {
        unsigned value = MIN(MAX(event->abs.value, 0), 0x7fff);
        if (event->abs.axis == INPUT_AXIS_X) {
            s->x = 90 + (value * 875 + 0x3fff) / 0x7fff;
        } else {
            s->y = 920 - (value * 870 + 0x3fff) / 0x7fff;
        }
    } else if (event->type == INPUT_EVENT_KIND_BTN &&
               event->btn.button == INPUT_BUTTON_LEFT) {
        s->down = event->btn.down;
    } else {
        return;
    }
    s->touch_pending = true;
}

static const QemuInputHandler touch_handler = {
    .name = "p2d touchscreen",
    .mask = INPUT_EVENT_MASK_ABS | INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_KEY,
    .event = touch_event,
    .sync = touch_sync,
};

static void report_shadow(P2DSoc *s, uint32_t addr, bool write)
{
    if (g_hash_table_size(s->reported) < 128 &&
        !g_hash_table_contains(s->reported, GUINT_TO_POINTER(addr))) {
        g_hash_table_add(s->reported, GUINT_TO_POINTER(addr));
        qemu_log_mask(LOG_UNIMP, "p2d shadow %s 0x%08x\n", write ? "write" : "read", addr);
    }
}

static uint32_t reg_read(P2DSoc *s, uint32_t addr)
{
    uint32_t value;
    if (addr >= 0x4a000000 && addr < 0x4a000020) {
        return s->intc[(addr - 0x4a000000) / 4];
    }
    if (addr >= 0x51000000 && addr < 0x51000044) {
        return addr == 0x51000040 ? timer_current(s) : s->timer_regs[(addr - 0x51000000) / 4];
    }
    if (addr == 0x4c000004) {
        return s->mpll;
    }
    if (addr == 0x4c000014) {
        return s->clkdiv;
    }
    if (addr >= 0x50000000 && addr < 0x5000c000 && (addr & 0x3fff) == 0x10) {
        return 6; /* Both transmitter FIFO and shifter empty. */
    }
    if (addr >= 0x50000000 && addr < 0x5000c000 &&
        ((addr & 0x3fff) == 0x14 || (addr & 0x3fff) == 0x18)) {
        return 0; /* No received characters or receive errors. */
    }
    if (addr == 0x560000b0) {
        return 0x32410000;
    }
    if (addr == 0x560000a4) {
        return s->eintmask;
    }
    if (addr == 0x560000a8) {
        return s->eintpend;
    }
    if (addr == 0x58000000) {
        return s->adccon;
    }
    if (addr == 0x58000004) {
        return s->adctsc;
    }
    if (addr == 0x58000008) {
        return s->adcdly;
    }
    if (addr == 0x5800000c || addr == 0x58000010) {
        value = (s->down ? 0 : BIT(15)) | ((s->adctsc & 7) << 12);
        value |= addr == 0x5800000c ? s->y : s->x;
        if (s->adccon & 2) {
            adc_complete(s);
        }
        return value;
    }
    if (addr >= 0x500f4000 && addr <= 0x500f400c) {
        return s->folder[(addr - 0x500f4000) / 4];
    }
    if (addr == 0x500f5000) {
        return s->emul_mask;
    }
    if (addr == 0x500f5004) {
        uint32_t pending = s->emul_pending;
        s->emul_pending = 0;
        s->eintpend &= ~BIT(11);
        intc_update(s);
        return pending;
    }
    if (addr == 0x500ffff0) {
        return 0; /* No host service events; AC/full battery. */
    }
    if (addr >= 0x57000070 && addr <= 0x57000088) {
        struct tm tm;

        /* Calendar and OS tick clocks must both advance. SDL's WinCE timer
         * corrects GetTickCount against GetSystemTime; a frozen RTC repeatedly
         * resets elapsed time and can prevent a timed splash from finishing.
         * Use QEMU's RTC source so -rtc base/clock and calendar carries agree.
         */
        qemu_get_timedate(&tm, 0);
        switch (addr) {
        case 0x57000070: value = tm.tm_sec; break;
        case 0x57000074: value = tm.tm_min; break;
        case 0x57000078: value = tm.tm_hour; break;
        case 0x5700007c: value = tm.tm_mday; break;
        case 0x57000080: value = tm.tm_wday + 1; break;
        case 0x57000084: value = tm.tm_mon + 1; break;
        default: value = (tm.tm_year + 1900) % 100; break;
        }
        return ((value / 10) << 4) | (value % 10);
    }
    if (addr >= 0x59000020 && addr <= 0x59000034) {
        unsigned index = (addr - 0x59000020) / 4;
        if (index == 5) {
            unsigned keycode = 0;
            if (s->spi1[4] == 0xff && s->key_count) {
                keycode = s->keys[s->key_head];
                s->key_head = (s->key_head + 1) % ARRAY_SIZE(s->keys);
                --s->key_count;
                intc_update(s);
            }
            return keycode;
        }
        return s->spi1[index];
    }
    report_shadow(s, addr, false);
    return GPOINTER_TO_UINT(g_hash_table_lookup(s->shadow, GUINT_TO_POINTER(addr)));
}

static void reg_write(P2DSoc *s, uint32_t addr, uint32_t value)
{
    if (addr >= 0x4a000000 && addr < 0x4a000020) {
        unsigned slot = (addr - 0x4a000000) / 4;
        if (slot == 0 || slot == 4 || slot == 6) {
            s->intc[slot] &= ~value;
        } else if (slot != 5) {
            s->intc[slot] = value;
        }
        intc_update(s);
        return;
    }
    if (addr >= 0x51000000 && addr < 0x51000040) {
        uint32_t previous = s->timer_regs[2];
        unsigned index = (addr - 0x51000000) / 4;
        uint32_t changed = s->timer_regs[index] ^ value;
        bool relevant = (addr == 0x51000000 && (changed & 0xff00)) ||
                        (addr == 0x51000004 && (changed & 0xf0000)) ||
                        (addr == 0x51000008 &&
                         ((value & BIT(21)) || (changed & (BIT(20) | BIT(22))))) ||
                        (addr == 0x5100003c && changed);
        bool control = addr == 0x51000008 &&
                       ((value & BIT(21)) || ((previous ^ value) & BIT(20)));
        bool reload = control && (value & BIT(21));
        if (relevant) {
            timer_before_update(s);
        }
        s->timer_regs[index] = value;
        /* Timer0-3 share this bank, including TCON. Their writes must not
         * cancel Timer4's queued expiration, even if host service is late.
         * TCNTB4 is a reload buffer; changing it takes effect at the next
         * manual update or automatic reload, not by restarting this period.
         */
        if (!control && timer_frequency(s) == s->timer_hz) {
            return;
        }
        s->timer_count = reload ? s->timer_regs[15] & 0xffff : timer_current(s);
        if (control) {
            s->timer_running = (value & BIT(20)) != 0;
        }
        timer_schedule(s);
        return;
    }
    if (addr == 0x4c000004 || addr == 0x4c000014) {
        timer_before_update(s);
        if (addr == 0x4c000004) {
            s->mpll = value;
        } else {
            s->clkdiv = value;
        }
        if (timer_frequency(s) != s->timer_hz) {
            s->timer_count = timer_current(s);
            timer_schedule(s);
        }
        clock_update_hz(s->pclk, peripheral_frequency(s));
        return;
    }
    if (addr >= 0x50000000 && addr < 0x5000c000 && (addr & 0x3fff) == 0x20) {
        /* Debug UART only; no CP15/CPU callbacks or instruction hooks. */
        if ((addr & ~0x3fff) == 0x50004000) {
            fputc(value & 255, stderr);
        }
        return;
    }
    if (addr == 0x560000a4 || addr == 0x560000a8) {
        if (addr == 0x560000a4) {
            s->eintmask = value;
        } else {
            s->eintpend &= ~value;
        }
        intc_update(s);
        return;
    }
    if (addr == 0x560000b0) {
        return;
    }
    if (addr == 0x58000000) {
        s->adccon = (s->adccon & BIT(15)) | (value & 0x7fff);
        if ((value & 3) == 1) {
            s->adccon &= ~BIT(15);
            adc_complete(s);
        }
        return;
    }
    if (addr == 0x58000004) {
        s->adctsc = value & 255;
        touch_sync(DEVICE(s));
        return;
    }
    if (addr == 0x58000008) {
        s->adcdly = value & 0xffff;
        return;
    }
    if (addr == 0x5800000c || addr == 0x58000010) {
        return;
    }
    if (addr >= 0x59000020 && addr <= 0x59000034) {
        unsigned index = (addr - 0x59000020) / 4;
        if (index != 1) {
            s->spi1[index] = index >= 4 ? (uint8_t)value : value;
        }
        return;
    }
    if (addr >= 0x500f4000 && addr <= 0x500f400c) {
        unsigned i = (addr - 0x500f4000) / 4;
        if (i == 0 || i == 3) {
            s->folder[i] = value;
        } else if (i == 1) {
            s->folder[1] = value;
            s->folder[2] = 0;
            s->folder[3] = p2d_import_execute(&s->import, s->folder[0], value);
        }
        return;
    }
    if (addr == 0x500f5000) {
        s->emul_mask = value;
        if (s->emul_mask & s->emul_pending) {
            s->eintpend |= BIT(11);
            intc_update(s);
        }
        return;
    }
    report_shadow(s, addr, true);
    g_hash_table_insert(s->shadow, GUINT_TO_POINTER(addr), GUINT_TO_POINTER(value));
}

static uint8_t pd_read(P2DSoc *s)
{
    unsigned i = s->pd_index;
    uint8_t result;
    switch (i) {
    case 0: return 0x83;
    case 1: return 0;
    case 4:
        result = s->pd_regs[4]; s->pd_regs[4] = 0; return result;
    case 0x16: return 1;
    case 0x1f:
        result = s->pd_chip; s->pd_chip &= ~0xc0; return result;
    default: return i < sizeof(s->pd_regs) ? s->pd_regs[i] : 0;
    }
}

static uint64_t window_read(void *opaque, hwaddr offset, unsigned size)
{
    P2DWindow *w = opaque;
    P2DSoc *s = w->soc;
    uint32_t addr = w->base + offset;
    uint64_t value = 0;
    unsigned i;

    if (addr <= 0x110003e1 && addr + size > 0x110003e0) {
        for (i = 0; i < size; ++i) {
            if (addr + i == 0x110003e0) {
                value |= (uint64_t)s->pd_index << (i * 8);
            } else if (addr + i == 0x110003e1) {
                value |= (uint64_t)pd_read(s) << (i * 8);
            }
        }
        return value;
    }
    return (reg_read(s, addr & ~3u) >> ((addr & 3) * 8)) &
           (size == 4 ? UINT32_MAX : (1u << (size * 8)) - 1);
}

static void window_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    P2DWindow *w = opaque;
    P2DSoc *s = w->soc;
    uint32_t addr = w->base + offset;
    unsigned i;
    if (addr <= 0x110003e1 && addr + size > 0x110003e0) {
        for (i = 0; i < size; ++i) {
            if (addr + i == 0x110003e0) {
                s->pd_index = value >> (i * 8);
            } else if (addr + i == 0x110003e1) {
                uint8_t data = value >> (i * 8);
                if (s->pd_index == 0x1f) {
                    s->pd_chip = 0xc0;
                } else if (s->pd_index < sizeof(s->pd_regs) && s->pd_index > 4) {
                    s->pd_regs[s->pd_index] = data;
                }
            }
        }
        return;
    }
    if (size != 4) {
        unsigned shift = (addr & 3) * 8;
        uint32_t mask = ((1u << (size * 8)) - 1) << shift;
        value = (reg_read(s, addr & ~3u) & ~mask) | ((value << shift) & mask);
    }
    reg_write(s, addr & ~3u, value);
}

static const MemoryRegionOps window_ops = {
    .read = window_read, .write = window_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1, .valid.max_access_size = 4,
};

static void soc_reset(DeviceState *dev)
{
    P2DSoc *s = P2D_SOC(dev);
    memset(s->intc, 0, sizeof(s->intc));
    memset(s->timer_regs, 0, sizeof(s->timer_regs));
    memset(s->folder, 0, sizeof(s->folder));
    p2d_import_reset(&s->import);
    memset(s->pd_regs, 0, sizeof(s->pd_regs));
    g_hash_table_remove_all(s->shadow);
    s->intc[2] = UINT32_MAX;
    s->intc[7] = 0x7ff;
    s->eintmask = 0xfffffff0;
    s->eintpend = 0;
    s->mpll = 0x5c080;
    s->clkdiv = 0;
    s->timer_count = 0;
    s->timer_running = false;
    s->adccon = 0x3fc4;
    s->adctsc = 0x58;
    s->adcdly = 0xff;
    s->x = 90; s->y = 920; s->down = false; s->touch_pending = false;
    s->pd_index = 0; s->pd_chip = 0xc0; s->emul_mask = 0;
    s->emul_pending = p2d_import_enabled(&s->import) ? BIT(30) : 0;
    memset(s->spi1, 0, sizeof(s->spi1));
    s->spi1[1] = 1; s->spi1[2] = 2;
    s->key_head = s->key_count = 0;
    timer_del(s->timer4);
    clock_update_hz(s->pclk, peripheral_frequency(s));
    intc_update(s);
}

/* Read-only QOM diagnostics for late-service phase tests. No polling/hook on
 * guest execution and no ability to alter time or pending device state.
 */
static void soc_clock_property(Object *obj, Visitor *visitor, const char *name,
                               void *opaque, Error **errp)
{
    P2DSoc *s = P2D_SOC(obj);
    uint64_t value = !strcmp(name, "virtual-clock-ns") ?
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) : s->timer_deadline;
    visit_type_uint64(visitor, name, &value, errp);
}

static void soc_init(Object *obj)
{
    P2DSoc *s = P2D_SOC(obj);
    SysBusDevice *bus = SYS_BUS_DEVICE(obj);
    s->shadow = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->reported = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->timer4 = timer_new_ns(QEMU_CLOCK_VIRTUAL, p2d_timer_expired, s);
    object_property_add(obj, "virtual-clock-ns", "uint64", soc_clock_property,
                        NULL, NULL, NULL);
    object_property_add(obj, "timer4-deadline-ns", "uint64", soc_clock_property,
                        NULL, NULL, NULL);
    s->pclk = qdev_init_clock_out(DEVICE(obj), "pclk");
    s->peripheral.soc = s; s->peripheral.base = 0x48000000;
    s->expansion.soc = s; s->expansion.base = 0x10000000;
    memory_region_init_io(&s->peripheral.mr, obj, &window_ops, &s->peripheral,
                         "p2d.peripherals", 0x13000000);
    memory_region_init_io(&s->expansion.mr, obj, &window_ops, &s->expansion,
                         "p2d.expansion", 0x10000000);
    sysbus_init_mmio(bus, &s->peripheral.mr);
    sysbus_init_mmio(bus, &s->expansion.mr);
    sysbus_init_irq(bus, &s->irq);
    sysbus_init_irq(bus, &s->fiq);
    qdev_init_gpio_in(DEVICE(obj), media_irq, 1);
    qdev_init_gpio_in_named(DEVICE(obj), pcmcia_irq, "pcmcia", 2);
}

static void soc_realize(DeviceState *dev, Error **errp)
{
    P2DSoc *s = P2D_SOC(dev);
    if (!p2d_import_load(&s->import, CPU(s->cpu), s->import_file,
                         s->shared_folder, s->shared_move_state, s->network_setup, s->gles_assets, s->gles_renderer, errp)) {
        return;
    }
    s->input = qemu_input_handler_register(dev, &touch_handler);
    qemu_input_handler_activate(s->input);
}

static const Property soc_properties[] = {
    DEFINE_PROP_LINK("cpu", P2DSoc, cpu, TYPE_ARM_CPU, ARMCPU *),
    DEFINE_PROP_STRING("import-file", P2DSoc, import_file),
    DEFINE_PROP_STRING("shared-folder", P2DSoc, shared_folder),
    DEFINE_PROP_STRING("shared-move-state", P2DSoc, shared_move_state),
    DEFINE_PROP_STRING("network-setup", P2DSoc, network_setup),
    DEFINE_PROP_STRING("gles-assets", P2DSoc, gles_assets),
    DEFINE_PROP_STRING("gles-renderer", P2DSoc, gles_renderer),
};

static void soc_finalize(Object *obj)
{
    P2DSoc *s = P2D_SOC(obj);
    p2d_import_cleanup(&s->import);
}

static void soc_class(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = soc_realize;
    device_class_set_legacy_reset(dc, soc_reset);
    dc->user_creatable = false;
    device_class_set_props(dc, soc_properties);
}

static const TypeInfo soc_type = {
    .name = TYPE_P2D_SOC, .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(P2DSoc), .instance_init = soc_init,
    .instance_finalize = soc_finalize,
    .class_init = soc_class,
};
static void soc_register(void)
{
    type_register_static(&soc_type);
}
type_init(soc_register)
