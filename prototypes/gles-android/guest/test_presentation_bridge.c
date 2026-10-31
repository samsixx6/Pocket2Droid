/* Included after the actual bridge (only the guest scalar widths are adapted). */
#define main p2d_unused_module_regressions
#include "test_presentation.c"
#undef main

static unsigned fixture_width = 480, fixture_height = 640, response_offset;
static unsigned gdi_lookups, payload_calls, payload_bytes, fail_payload, bad_header;
static unsigned fail_payload_after, missing_gdi, fail_present_dc;
static int expect_direct;
static unsigned char *fixture_fallback;
static void *allocations[16];
static size_t allocation_sizes[16];
static unsigned allocation_count;
static unsigned char *expected_frame;
static unsigned fixture_mode_offset;

static int wide_is(const W *p, const char *s) {
    while (*p && *s) if (*p++ != (unsigned char)*s++) return 0;
    return !*p && !*s;
}
H CreateFileW(const W *path, U32 access, U32 share, void *security,
              U32 disposition, U32 flags, H template) {
    (void)access; (void)share; (void)security; (void)disposition; (void)flags; (void)template;
    if (wide_is(path, "\\Storage Card\\P2DGLES\\mode.txt")) { fixture_mode_offset = 0; return (H)25; }
    if (wide_is(path, "\\Storage Card\\P2DGLES\\guest.log")) return (H)23;
    if (wide_is(path, "\\Storage Card\\P2DGLES\\session.reset")) return (H)24;
    if (wide_is(path, "\\Storage Card\\P2DGLES\\request.bin")) return (H)20;
    if (wide_is(path, "\\Storage Card\\P2DGLES\\request.ready")) return (H)21;
    assert(wide_is(path, "\\Storage Card\\P2DGLES\\response.bin"));
    response_offset = 0; return (H)22;
}
int WriteFile(H h, const void *p, U32 n, U32 *done, void *overlap) {
    (void)p; (void)overlap; assert(h == (H)20 || h == (H)21 || h == (H)23 || h == (H)24);
    *done = n; return 1;
}
int ReadFile(H h, void *p, U32 n, U32 *done, void *overlap) {
    (void)overlap;
    if (h == (H)25) {
        static const char mode[] = "on gpu";
        unsigned remaining = sizeof(mode) - 1 - fixture_mode_offset;
        if (n > remaining) n = remaining;
        memcpy(p, mode + fixture_mode_offset, n); fixture_mode_offset += n; *done = n; return 1;
    }
    assert(h == (H)22); *done = 0;
    if (!response_offset) {
        U32 header[] = {0x31524750, sequence, width + bad_header, height, width * height * 2, 0};
        assert(n == sizeof(header)); memcpy(p, header, n); response_offset += n; *done = n; return 1;
    }
    if (fail_payload && response_offset - 24 >= fail_payload_after) return 0;
    unsigned offset = response_offset - 24;
    if (expect_direct) {
        Object *b = memory_dcs ? memory_dcs->selected : NULL;
        assert(b && b->kind == 4 && "response payload must be read directly into a prepared DIB");
        assert(p == (unsigned char *)b->bits + offset && "response payload destination must equal DIB backing");
    } else assert(p == fixture_fallback + offset);
    assert(n <= width * height * 2 - offset);
    for (U32 i = 0; i < n; i++) ((unsigned char *)p)[i] = (unsigned char)((offset + i) * 17 + sequence);
    response_offset += n; payload_calls++; payload_bytes += n; *done = n; return 1;
}
int CloseHandle(H h) { (void)h; return 1; }
H LoadLibraryW(const W *name) { assert(wide_is(name, "coredll.dll")); return (H)30; }
static H mock_mutex(void *security, int owned, const W *name) {
    (void)security; (void)name; assert(owned); return (H)31;
}
static U32 mock_error(void) { return 0; }
static U32 mock_pid(void) { return 123; }
static I32 mock_metrics(int coordinate) { return coordinate ? fixture_height : fixture_width; }
static int mock_rect(H h, I32 *r) {
    assert(h == sim.expected_window); r[0] = r[1] = 0; r[2] = fixture_width; r[3] = fixture_height; return 1;
}
static void *mock_alloc(void *address, U32 n, U32 type, U32 protection) {
    (void)address; assert(type == 0x3000 && protection == 4 && allocation_count < 16);
    void *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(p != MAP_FAILED && (uintptr_t)p + n <= UINT32_MAX);
    allocations[allocation_count] = p; allocation_sizes[allocation_count++] = n;
    if (n == 2u * 1024 * 1024) fixture_fallback = p;
    return p;
}
static H bridge_get_dc(H h) {
    if (fail_present_dc && sim.get) sim.fail_get = 1;
    return get_dc(h);
}
void *GetProcAddressW(H h, const W *name) {
    assert(h == (H)30);
    if (wide_is(name, "CreateMutexW")) return mock_mutex;
    if (wide_is(name, "GetLastError")) return mock_error;
    if (wide_is(name, "GetCurrentProcessId")) return mock_pid;
    if (wide_is(name, "VirtualAlloc")) return mock_alloc;
    if (wide_is(name, "GetClientRect")) return mock_rect;
    if (wide_is(name, "GetSystemMetrics")) return mock_metrics;
    gdi_lookups++;
    if (wide_is(name, "GetDC")) return missing_gdi ? NULL : bridge_get_dc;
    if (wide_is(name, "ReleaseDC")) return release_dc;
    if (wide_is(name, "CreateCompatibleDC")) return create_dc;
    if (wide_is(name, "CreateDIBSection")) return create_dib;
    if (wide_is(name, "SelectObject")) return select_object;
    if (wide_is(name, "BitBlt")) return bit_blt;
    if (wide_is(name, "DeleteObject")) return delete_object;
    assert(wide_is(name, "DeleteDC")); return delete_dc;
}
void Sleep(U32 delay) { (void)delay; assert(!"synchronous response must not poll/sleep"); }

static U32 call(U32 op, const U32 *args, U32 count) {
    U32 r[4] = {0}, s[12] = {0};
    for (U32 i = 0; i < count; i++) { if (i < 4) r[i] = args[i]; else s[i-4] = args[i]; }
    return bridge(op, r, s, count);
}
static void make_window(unsigned w, unsigned h, uintptr_t handle) {
    fixture_width = w; fixture_height = h; sim.expected_window = (void *)handle;
    U32 args[] = {1, 1, (U32)handle, 0}; assert(call(OP_eglCreateWindowSurface, args, 4) == 2);
}
static void frame(void) {
    free(expected_frame); expected_frame = malloc((size_t)width * height * 2); assert(expected_frame);
    for (U32 i = 0; i < width * height * 2; i++) expected_frame[i] = (unsigned char)(i * 17 + sequence + 1);
    sim.expected = expected_frame; assert(call(OP_eglSwapBuffers, NULL, 0) == 1);
}
static void read_one_pixel(void) {
    unsigned char *out = mock_alloc(0, 4096, 0x3000, 4);
    U32 args[] = {0, 0, 1, 1, 0x1908, 0x1401, (U32)(uintptr_t)out};
    U32 seq = sequence + 1, offset = (height - 1) * width * 2;
    unsigned c = ((offset * 17 + seq) & 255) | ((((offset + 1) * 17 + seq) & 255) << 8);
    call(OP_glReadPixels, args, 7);
    assert(out[0] == ((c >> 11) & 31) * 255 / 31 &&
           out[1] == ((c >> 5) & 63) * 255 / 63 && out[2] == (c & 31) * 255 / 31 && out[3] == 255);
}
int main(int argc, char **argv) {
    assert(argc == 2); sim.expected_window = (void *)10;
    if (!strcmp(argv[1], "direct")) {
        expect_direct = 1; make_window(480, 640, 10);
        for (unsigned i = 0; i < 50; i++) frame();
        assert(gdi_lookups == 8 && sim.create_dc == 1 && sim.create_dib == 1);
        assert(sim.get == 51 && sim.release == 51 && sim.blit == 50);
        assert(payload_calls == 950 && payload_bytes == 50u * 480 * 640 * 2);
    } else if (!strcmp(argv[1], "odd")) {
        make_window(5, 3, 10); frame(); frame();
        assert(pixels == fixture_fallback && gdi_lookups == 8 && sim.create_dib == 1);
        read_one_pixel();
    } else if (!strcmp(argv[1], "pbuffer")) {
        U32 *attrs = mock_alloc(0, 4096, 0x3000, 4);
        attrs[0] = 0x3057; attrs[1] = 8; attrs[2] = 0x3056; attrs[3] = 4; attrs[4] = 0x3038;
        U32 args[] = {1, 1, (U32)(uintptr_t)attrs}; assert(call(OP_eglCreatePbufferSurface, args, 3) == 2);
        frame(); read_one_pixel();
        assert(pixels == fixture_fallback && !gdi_lookups && !sim.blit && !sim.create_dib);
    } else if (!strcmp(argv[1], "lifecycle")) {
        expect_direct = 1; make_window(8, 4, 10); frame();
        make_window(16, 8, 10); frame(); assert(sim.del_obj == 1);
        make_window(16, 8, 11); frame(); assert(sim.del_obj == 2);
        assert(call(OP_eglDestroySurface, NULL, 0) == 1);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
        read_one_pixel(); /* Reacquires fresh backing, never reads the deleted DIB. */
        assert(call(OP_eglDestroyContext, NULL, 0) == 1);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
        make_window(8, 4, 11); frame();
        assert(call(OP_eglTerminate, NULL, 0) == 1);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
        assert(gdi_lookups == 8);
    } else if (!strcmp(argv[1], "transfer-failure")) {
        expect_direct = 1; make_window(8, 4, 10); frame(); fail_payload = 1;
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
    } else if (!strcmp(argv[1], "partial-transfer-failure")) {
        expect_direct = 1; make_window(480, 640, 10);
        fail_payload = 1; fail_payload_after = 32768;
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed);
        assert(payload_calls == 1 && payload_bytes == 32768);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
    } else if (!strcmp(argv[1], "prepare-failure")) {
        expect_direct = 1; make_window(8, 4, 10); sim.fail_dib = 1;
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed);
        assert(pixels == fixture_fallback && !pixels_bytes); clean(); assert(!payload_calls);
    } else if (!strcmp(argv[1], "missing-gdi")) {
        expect_direct = 1; make_window(8, 4, 10); missing_gdi = 1;
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed);
        assert(gdi_lookups == 8 && !payload_calls && !sim.create_dib);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
    } else if (!strcmp(argv[1], "blit-failure") || !strcmp(argv[1], "present-dc-failure")) {
        expect_direct = 1; make_window(8, 4, 10);
        if (!strcmp(argv[1], "blit-failure")) sim.fail_blit = 1; else fail_present_dc = 1;
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed && payload_calls == 1);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
    } else if (!strcmp(argv[1], "header-failure")) {
        expect_direct = 1; make_window(8, 4, 10); bad_header = 1;
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed);
        assert(!payload_calls && !sim.create_dib); clean();
    } else if (!strcmp(argv[1], "large-window")) {
        expect_direct = 1; make_window(2048, 2048, 10); frame();
        assert(pixels_bytes == 8u * 1024 * 1024 && pixels != fixture_fallback);
    } else if (!strcmp(argv[1], "large-odd")) {
        make_window(2047, 2048, 10);
        assert(!call(OP_eglSwapBuffers, NULL, 0) && failed && !payload_calls);
        assert(pixels == fixture_fallback && !pixels_bytes); clean();
    } else assert(!"unknown case");
    if (!failed) assert(call(OP_eglTerminate, NULL, 0) == 1);
    clean();
    free(expected_frame);
    for (unsigned i = 0; i < allocation_count; i++) assert(!munmap(allocations[i], allocation_sizes[i]));
    puts("PASS actual bridge presentation routing/lifecycle"); return 0;
}
