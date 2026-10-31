/* Private spike. Guest registers/timers remain unchanged. QEMU owns dirty
 * accounting, including CPU/DMA writes. Resolve the physical mapping each
 * refresh so a cached section cannot survive topology changes unnoticed. */
static void lcd_release_section(P2DLcd *s)
{
    if (s->fb_section.mr) {
        memory_region_set_log(s->fb_section.mr, false, DIRTY_MEMORY_VGA);
        memory_region_unref(s->fb_section.mr);
        s->fb_section.mr = NULL;
    }
}

static void lcd_invalidate(void *opaque)
{
    P2DLcd *s = opaque;
    s->redraw = true;
}

static bool lcd_prepare_section(P2DLcd *s, hwaddr base, hwaddr length)
{
    MemoryRegionSection next = memory_region_find(get_system_memory(), base,
                                                 length);
    if (!next.mr || int128_get64(next.size) < length ||
        !memory_region_is_ram(next.mr)) {
        if (next.mr) {
            memory_region_unref(next.mr);
        }
        lcd_release_section(s);
        return false;
    }
    if (next.mr == s->fb_section.mr &&
        next.offset_within_region == s->fb_section.offset_within_region) {
        memory_region_unref(next.mr);
        return true;
    }
    /* A base flip within the same RAM region retains logging, avoiding a
     * needless whole-RAM log off/on transition and its TLB work. */
    if (s->fb_section.mr != next.mr) {
        lcd_release_section(s);
        memory_region_set_log(next.mr, true, DIRTY_MEMORY_VGA);
    } else {
        memory_region_unref(s->fb_section.mr);
    }
    s->fb_section = next;
    s->redraw = true;
    return true;
}

static void lcd_convert_row(void *opaque, uint8_t *dest, const uint8_t *src,
                            int width, int dest_step)
{
    P2DLcd *s = opaque;
    bool rgb565 = (s->regs[4] & (1u << 11)) != 0;
    for (int x = 0; x < width; x++, src += 2, dest += dest_step) {
        uint16_t pixel = lduw_le_p(src);
        unsigned r = (pixel >> (rgb565 ? 11 : 10)) & 31;
        unsigned g = (pixel >> 5) & (rgb565 ? 63 : 31);
        unsigned b = pixel & 31;
        r = (r << 3) | (r >> 2);
        g = rgb565 ? (g << 2) | (g >> 4) : (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        uint32_t out = (r << 16) | (g << 8) | b;
        memcpy(dest, &out, sizeof(out));
    }
}

static bool lcd_update(void *opaque)
{
    P2DLcd *s = opaque;
    unsigned width = ((s->regs[2] >> 8) & 0x7ff) + 1;
    unsigned height = ((s->regs[1] >> 14) & 0x3ff) + 1;
    unsigned stride = (s->regs[7] & 0x7ff) + ((s->regs[7] >> 11) & 0x7ff);
    hwaddr base = (hwaddr)(s->regs[5] & 0x3fffffff) << 1;
    unsigned format = s->regs[4] & (1u << 11);
    if (!(s->regs[0] & 1) || ((s->regs[0] >> 1) & 15) != 12 ||
        !stride || stride < width || base < 0x30000000 ||
        base + (hwaddr)stride * height * 2 > 0x38000000) {
        s->redraw = true;
        return true;
    }
    if (!lcd_prepare_section(s, base, (hwaddr)stride * height * 2)) {
        s->redraw = true;
        return lcd_update_full(opaque);
    }
    qemu_console_resize(s->console, width, height);
    DisplaySurface *surface = qemu_console_surface(s->console);
    void *ram = memory_region_get_ram_ptr(s->fb_section.mr);
    if (s->fb_base != base || s->fb_width != width || s->fb_height != height ||
        s->fb_stride != stride || s->fb_format != format ||
        s->fb_surface != surface || s->fb_pixels != surface_data(surface) ||
        s->fb_ram != ram) {
        s->redraw = true;
    }
    s->fb_base = base;
    s->fb_width = width;
    s->fb_height = height;
    s->fb_stride = stride;
    s->fb_format = format;
    s->fb_surface = surface;
    s->fb_pixels = surface_data(surface);
    s->fb_ram = ram;
    int first = 0, last = 0;
    framebuffer_update_display(surface, &s->fb_section, width, height,
                               stride * 2, surface_stride(surface), 4,
                               s->redraw, lcd_convert_row, s, &first, &last);
    s->redraw = false;
    if (first >= 0) {
        qemu_console_update(s->console, 0, first, width, last - first + 1);
        s->publish_pending = true;
    }
    if (s->frame.map && s->publish_pending &&
        p2d_frame_publish_status(&s->frame, (uint32_t *)surface_data(surface),
                                 width, height, surface_stride(surface)) >= 0) {
        s->publish_pending = false;
    }
    return true;
}
