#include "picomem/module.h"

#include "pcem_ega_bios_rom.h"

#include "hardware/timer.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "gc.h"
}

namespace picomem {
namespace {

#if !PICOMEM_ENABLE_DISPLAYLINK || !PICOMEM_ENABLE_GC
#error "The EGA module requires PICOMEM_ENABLE_DISPLAYLINK and PICOMEM_ENABLE_GC"
#endif

constexpr uint16_t kEgaIoBase = 0x03a0;
constexpr uint16_t kEgaIoSize = 0x0040;
constexpr uint32_t kEgaMemBase = 0x000a0000;
constexpr uint32_t kEgaMemSize = 0x00020000;
constexpr uint32_t kEgaRomBase = 0x000c0000;
constexpr uint32_t kEgaRomImageSize = sizeof(kPcemEgaBiosRom);
constexpr uint32_t kEgaRomSize = 0x00008000;

#ifndef PICOMEM_EGA_MEMORY_KB
#define PICOMEM_EGA_MEMORY_KB 256
#endif

#ifndef PICOMEM_EGA_MONITOR_TYPE
#define PICOMEM_EGA_MONITOR_TYPE 9
#endif

static_assert(PICOMEM_EGA_MEMORY_KB == 64 || PICOMEM_EGA_MEMORY_KB == 128 || PICOMEM_EGA_MEMORY_KB == 256,
              "PICOMEM_EGA_MEMORY_KB must be 64, 128, or 256");

constexpr uint32_t kEgaVramSize = (uint32_t)PICOMEM_EGA_MEMORY_KB * 1024u;

constexpr unsigned kMaxEgaWidth = 1024;
constexpr unsigned kMaxEgaLines = 512;
constexpr uint32_t kCgaCharClockHz = 1789773u;
constexpr uint32_t kMdaCharClockHz = 2032125u;
constexpr unsigned kTimingFracBits = 16;
constexpr uint64_t kTimingOne = 1ull << kTimingFracBits;
constexpr unsigned kMaxPollStepsPerTick = 2048;
constexpr unsigned kDisplayPageCount = 3;
constexpr unsigned kInvalidDisplayPage = kDisplayPageCount;
constexpr unsigned kDisplayDirtyLineMarkThreshold = kMaxEgaLines * 2u;
constexpr int kDirtyUnmapped = -1;
constexpr int kDirtyNoVisibleChange = 0;
constexpr int kDirtyMapped = 1;

constexpr int kDisplayGreen = 3;
constexpr int kDisplayAmber = 4;
constexpr int kDisplayWhite = 5;
constexpr int kDefaultMonitorType = PICOMEM_EGA_MONITOR_TYPE;

struct Ega {
    uint8_t crtcreg;
    uint8_t crtc[32];
    uint8_t gdcreg[16];
    uint8_t gdcaddr;
    uint8_t attrregs[32];
    uint8_t attraddr;
    uint8_t attrff;
    uint8_t seqregs[64];
    uint8_t seqaddr;

    uint8_t miscout;
    uint8_t vidclock;

    uint8_t la, lb, lc, ld;
    uint8_t stat;

    uint8_t colourcompare, colournocare;
    uint8_t readmode, writemode, readplane;
    uint8_t chain2_read, chain2_write;
    uint8_t writemask;
    uint32_t charseta, charsetb;

    uint8_t egapal[16];
    GCCOLOR *pallook;
    GCCOLOR active_palette[16];

    int vtotal, dispend, vsyncstart, split;
    int hdisp, rowoffset;
    int vres;

    uint64_t dispontime, dispofftime;
    uint32_t timer_frac;
    uint32_t next_poll_us;
    bool timing_started;

    uint8_t scrblank;
    int dispon;

    uint32_t ma, maback, ca;
    int vc, sc;
    int linepos, vslines;
    int con, cursoron, blink;
    int scrollcache;

    int firstline, lastline;
    int displine;

    uint32_t vrammask;
    uint32_t vram_limit;

    int video_res_x, video_res_y, video_bpp;
    uint32_t frames;

    uint8_t vram[kEgaVramSize];
};

struct RenderContext {
    PGC pGC;
    bool access_open;
    bool emitted;
};

struct LineMeasure {
    uint32_t hash;
    unsigned fill_bytes;
};

Ega ega;
uint8_t ega_rotate[8][256];
uint8_t edatlookup[4][4];
GCCOLOR pallook16[256];
GCCOLOR pallook64[256];
GCCOLOR line_buffer[kMaxEgaWidth];
uint32_t line_hash[kDisplayPageCount][kMaxEgaLines];
uint16_t line_width[kDisplayPageCount][kMaxEgaLines];
uint32_t line_dirty_version[kMaxEgaLines];
uint32_t line_page_version[kDisplayPageCount][kMaxEgaLines];
volatile uint32_t line_dirty_next_version;
volatile uint32_t timing_recalc_requests;
uint32_t handled_timing_recalc_requests;
volatile uint32_t display_content_dirty;
volatile uint32_t display_full_render_dirty;
volatile uint32_t display_dirty_line_marks;

int egaswitchread;
int egaswitches;
int xsize = 1;
int ysize = 1;
int display_firstline;
unsigned display_source_width;
unsigned display_source_height;
unsigned display_vertical_scale = 1;
unsigned display_target_width_px;
unsigned display_target_height_px;
bool display_frame_valid;
bool display_initialized;
bool display_window_checked_for_frame;
bool display_render_frame;
bool display_render_full_frame;
long last_gc_width;
long last_gc_height;
unsigned last_source_width;
unsigned last_source_height;
bool display_usb_pending;
bool display_pages_ready;
uint32_t display_page_base[kDisplayPageCount];
bool display_page_clear_pending[kDisplayPageCount];
bool display_page_content_valid[kDisplayPageCount];
uint32_t display_page_generation[kDisplayPageCount];
uint32_t display_content_generation;
bool display_present_pending;
unsigned display_visible_page;
unsigned display_pending_page;
unsigned display_draw_page;
bool display_render_full_frame_from_start;
bool display_draw_page_needs_clone;

GCCOLOR makecol(uint8_t r, uint8_t g, uint8_t b)
{
    return RGB(r, g, b);
}

GCCOLOR ega_mono_color(uint8_t c)
{
    switch (kDefaultMonitorType >> 4) {
    case kDisplayGreen:
        switch ((c >> 3) & 3) {
        case 0: return makecol(0x00, 0x00, 0x00);
        case 1: return makecol(0x08, 0xc7, 0x2c);
        case 2: return makecol(0x04, 0x8a, 0x20);
        default: return makecol(0x34, 0xff, 0x5d);
        }
    case kDisplayAmber:
        switch ((c >> 3) & 3) {
        case 0: return makecol(0x00, 0x00, 0x00);
        case 1: return makecol(0xef, 0x79, 0x00);
        case 2: return makecol(0xb2, 0x4d, 0x00);
        default: return makecol(0xff, 0xe3, 0x34);
        }
    case kDisplayWhite:
    default:
        switch ((c >> 3) & 3) {
        case 0: return makecol(0x00, 0x00, 0x00);
        case 1: return makecol(0xaf, 0xb3, 0xb0);
        case 2: return makecol(0x7a, 0x81, 0x83);
        default: return makecol(0xff, 0xfd, 0xed);
        }
    }
}

uint64_t ega_time_from_chars(int chars, uint32_t hz, bool nine_dot_chars)
{
    if (chars <= 0) {
        return 0;
    }

    uint64_t numerator = (uint64_t)chars * 1000000ull * kTimingOne;
    uint64_t denominator = hz;
    if (nine_dot_chars) {
        numerator *= 9u;
        denominator *= 8u;
    }
    return numerator / denominator;
}

uint32_t ega_delay_us(Ega *e, uint64_t delay)
{
    uint32_t us = (uint32_t)(delay >> kTimingFracBits);
    uint32_t frac = (uint32_t)(delay & (kTimingOne - 1u));
    uint32_t old_frac = e->timer_frac;
    e->timer_frac = (old_frac + frac) & (uint32_t)(kTimingOne - 1u);
    if (old_frac + frac >= kTimingOne) {
        ++us;
    }
    return us ? us : 1;
}

uint64_t load_dispontime(const Ega *e)
{
    return __atomic_load_n(&e->dispontime, __ATOMIC_ACQUIRE);
}

uint64_t load_dispofftime(const Ega *e)
{
    return __atomic_load_n(&e->dispofftime, __ATOMIC_ACQUIRE);
}

unsigned display_command_chunks(unsigned pixels)
{
    return pixels ? ((pixels + 255u) / 256u) : 0u;
}

unsigned display_fill_run_bytes(unsigned pixels)
{
    return 17u * display_command_chunks(pixels);
}

unsigned display_copy_row_bytes(unsigned pixels)
{
    return 18u * display_command_chunks(pixels);
}

unsigned display_raw_line_bytes(unsigned pixels)
{
    return pixels ? (3u * pixels + 12u * display_command_chunks(pixels)) : 0u;
}

uint32_t measure_line_hash(const GCCOLOR *data, unsigned width)
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < width; ++i) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

unsigned cap_fill_bytes(unsigned fill_bytes, unsigned fill_stop_bytes)
{
    if (fill_stop_bytes != 0 && fill_bytes > fill_stop_bytes) {
        return fill_stop_bytes + 1u;
    }
    return fill_bytes;
}

LineMeasure measure_line_hash_and_fill(const GCCOLOR *data, unsigned width, unsigned fill_stop_bytes)
{
    uint32_t hash = 2166136261u;
    if (width == 0) {
        return {hash, 0};
    }

    GCCOLOR color = data[0];
    unsigned run_length = 1;
    unsigned fill_bytes = 0;
    bool measure_fill = true;

    hash ^= (uint32_t)color;
    hash *= 16777619u;
    for (unsigned i = 1; i < width; ++i) {
        GCCOLOR pixel = data[i];
        if (measure_fill) {
            if (pixel != color) {
                fill_bytes = cap_fill_bytes(fill_bytes + display_fill_run_bytes(run_length), fill_stop_bytes);
                if (fill_stop_bytes != 0 && fill_bytes > fill_stop_bytes) {
                    measure_fill = false;
                } else {
                    color = pixel;
                    run_length = 1;
                }
            } else {
                ++run_length;
            }
        }
        hash ^= (uint32_t)pixel;
        hash *= 16777619u;
    }
    if (measure_fill) {
        fill_bytes = cap_fill_bytes(fill_bytes + display_fill_run_bytes(run_length), fill_stop_bytes);
    }
    return {hash, fill_bytes};
}

void advance_scaled_sample(unsigned *sample_x, unsigned *error, unsigned source_width, unsigned target_width)
{
    *error += source_width;
    while (*error >= target_width) {
        *error -= target_width;
        ++*sample_x;
    }
}

uint32_t downscale_line_in_place(unsigned source_width, unsigned target_width)
{
    uint32_t hash = 2166136261u;
    if (target_width == 0) {
        return hash;
    }

    unsigned sample_x = 0;
    unsigned error = 0;

    GCCOLOR pixel = line_buffer[0];
    line_buffer[0] = pixel;
    hash ^= (uint32_t)pixel;
    hash *= 16777619u;
    for (unsigned x = 1; x < target_width; ++x) {
        advance_scaled_sample(&sample_x, &error, source_width, target_width);
        GCCOLOR pixel = line_buffer[sample_x];
        line_buffer[x] = pixel;
        hash ^= (uint32_t)pixel;
        hash *= 16777619u;
    }
    return hash;
}

LineMeasure downscale_line_in_place_and_measure(unsigned source_width, unsigned target_width, unsigned fill_stop_bytes)
{
    uint32_t hash = 2166136261u;
    if (target_width == 0) {
        return {hash, 0};
    }

    unsigned sample_x = 0;
    unsigned error = 0;
    GCCOLOR color = line_buffer[0];
    unsigned run_length = 1;
    unsigned fill_bytes = 0;
    bool measure_fill = true;

    line_buffer[0] = color;
    hash ^= (uint32_t)color;
    hash *= 16777619u;
    for (unsigned x = 1; x < target_width; ++x) {
        advance_scaled_sample(&sample_x, &error, source_width, target_width);
        GCCOLOR pixel = line_buffer[sample_x];
        line_buffer[x] = pixel;
        if (measure_fill) {
            if (pixel != color) {
                fill_bytes = cap_fill_bytes(fill_bytes + display_fill_run_bytes(run_length), fill_stop_bytes);
                if (fill_stop_bytes != 0 && fill_bytes > fill_stop_bytes) {
                    measure_fill = false;
                } else {
                    color = pixel;
                    run_length = 1;
                }
            } else {
                ++run_length;
            }
        }
        hash ^= (uint32_t)pixel;
        hash *= 16777619u;
    }
    if (measure_fill) {
        fill_bytes = cap_fill_bytes(fill_bytes + display_fill_run_bytes(run_length), fill_stop_bytes);
    }
    return {hash, fill_bytes};
}

unsigned measure_line_fill_bytes(const GCCOLOR *data, unsigned width, unsigned fill_stop_bytes)
{
    if (width == 0) {
        return 0;
    }

    GCCOLOR color = data[0];
    unsigned run_length = 1;
    unsigned fill_bytes = 0;

    for (unsigned i = 1; i < width; ++i) {
        GCCOLOR pixel = data[i];
        if (pixel == color) {
            ++run_length;
            continue;
        }
        fill_bytes = cap_fill_bytes(fill_bytes + display_fill_run_bytes(run_length), fill_stop_bytes);
        if (fill_stop_bytes != 0 && fill_bytes > fill_stop_bytes) {
            return fill_bytes;
        }
        color = pixel;
        run_length = 1;
    }
    return cap_fill_bytes(fill_bytes + display_fill_run_bytes(run_length), fill_stop_bytes);
}

void ega_recalctimings(Ega *e);
void present_pending_display_frame(PGC pGC);

void __time_critical_func(request_display_dirty)()
{
    __atomic_store_n(&display_full_render_dirty, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&display_content_dirty, 1u, __ATOMIC_RELEASE);
}

uint32_t __time_critical_func(next_display_dirty_version)()
{
    uint32_t version = __atomic_add_fetch(&line_dirty_next_version, 1u, __ATOMIC_ACQ_REL);
    if (version == 0) {
        version = __atomic_add_fetch(&line_dirty_next_version, 1u, __ATOMIC_ACQ_REL);
    }
    return version;
}

void __time_critical_func(mark_display_line_dirty_with_version)(unsigned line, uint32_t version)
{
    if (line >= kMaxEgaLines) {
        return;
    }
    __atomic_store_n(&line_dirty_version[line], version, __ATOMIC_RELEASE);
}

void __time_critical_func(mark_display_line_dirty)(unsigned line)
{
    mark_display_line_dirty_with_version(line, next_display_dirty_version());
    __atomic_store_n(&display_content_dirty, 1u, __ATOMIC_RELEASE);
}

void __time_critical_func(mark_display_line_range_dirty)(unsigned first_line, unsigned end_line)
{
    if (first_line >= end_line || first_line >= kMaxEgaLines) {
        return;
    }

    end_line = std::min<unsigned>(end_line, kMaxEgaLines);
    unsigned count = end_line - first_line;
    uint32_t total = __atomic_add_fetch(&display_dirty_line_marks, count, __ATOMIC_ACQ_REL);
    if (total > kDisplayDirtyLineMarkThreshold) {
        request_display_dirty();
        return;
    }

    uint32_t version = next_display_dirty_version();
    for (unsigned line = first_line; line < end_line; ++line) {
        mark_display_line_dirty_with_version(line, version);
    }
    __atomic_store_n(&display_content_dirty, 1u, __ATOMIC_RELEASE);
}

uint32_t display_line_dirty_version(unsigned line)
{
    if (line >= kMaxEgaLines) {
        return 0;
    }
    return __atomic_load_n(&line_dirty_version[line], __ATOMIC_ACQUIRE);
}

bool is_display_line_dirty(unsigned page, unsigned line)
{
    if (page >= kDisplayPageCount || line >= kMaxEgaLines) {
        return false;
    }

    uint32_t dirty_version = display_line_dirty_version(line);
    uint32_t page_version = __atomic_load_n(&line_page_version[page][line], __ATOMIC_ACQUIRE);
    return dirty_version != page_version;
}

void clear_display_line_dirty(unsigned page, unsigned line, uint32_t rendered_version)
{
    if (page >= kDisplayPageCount || line >= kMaxEgaLines) {
        return;
    }

    __atomic_store_n(&line_page_version[page][line], rendered_version, __ATOMIC_RELEASE);
}

void invalidate_display_page_hashes(unsigned page)
{
    if (page >= kDisplayPageCount) {
        return;
    }

    for (unsigned i = 0; i < kMaxEgaLines; ++i) {
        line_hash[page][i] = 0;
        line_width[page][i] = 0;
        __atomic_store_n(&line_page_version[page][i], 0u, __ATOMIC_RELEASE);
    }
    display_page_generation[page] = 0;
}

void invalidate_line_hashes()
{
    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        invalidate_display_page_hashes(page);
        display_page_content_valid[page] = false;
    }
    for (unsigned i = 0; i < kMaxEgaLines; ++i) {
        __atomic_store_n(&line_dirty_version[i], 0u, __ATOMIC_RELEASE);
    }
}

void mark_display_pages_clear_pending()
{
    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        display_page_clear_pending[page] = true;
        display_page_content_valid[page] = false;
        display_page_generation[page] = 0;
    }
}

void __time_critical_func(request_timing_recalc)()
{
    __atomic_fetch_add(&timing_recalc_requests, 1u, __ATOMIC_RELEASE);
}

void handle_deferred_requests()
{
    uint32_t timing_requests = __atomic_load_n(&timing_recalc_requests, __ATOMIC_ACQUIRE);
    if (timing_requests != handled_timing_recalc_requests) {
        ega_recalctimings(&ega);
        handled_timing_recalc_requests = timing_requests;
    }
}

void begin_access(RenderContext *ctx)
{
    if (!ctx || ctx->access_open || !ctx->pGC) {
        return;
    }
    GCPBeginAccess(ctx->pGC);
    ctx->access_open = true;
}

bool set_display_draw_base(PGC pGC, uint32_t base)
{
    if (!pGC || !pGC->bitmap.handle || (base & 1u)) {
        return false;
    }
    GCSetDeviceBase(pGC, base);
    return true;
}

void fill_display(RenderContext *ctx, long x, long y, long width, long height, GCCOLOR color)
{
    if (!ctx || !ctx->pGC || width <= 0 || height <= 0) {
        return;
    }
    long gc_width = GCWidth(ctx->pGC);
    long gc_height = GCHeight(ctx->pGC);
    if (gc_width <= 0 || gc_height <= 0) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= gc_width || y >= gc_height) {
        return;
    }
    if (x + width > gc_width) {
        width = gc_width - x;
    }
    if (y + height > gc_height) {
        height = gc_height - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    begin_access(ctx);
    GCFastFill(ctx->pGC, x, y, width, height, color);
    ctx->emitted = true;
    display_usb_pending = true;
}

void fill_display_direct(RenderContext *ctx, long x, long y, long width, long height, GCCOLOR color)
{
    fill_display(ctx, x, y, width, height, color);
}

bool copy_display_from_base(RenderContext *ctx,
                            unsigned long src_base,
                            long src_x,
                            long src_y,
                            long width,
                            long height,
                            long dest_x,
                            long dest_y)
{
    if (!ctx || !ctx->pGC || !ctx->pGC->bitmap.handle ||
        (src_base & 1u) || width <= 0 || height <= 0) {
        return false;
    }

    GC srcGC;
    if (!GCCreateWithDeviceMemory(ctx->pGC, GCWidth(ctx->pGC), GCHeight(ctx->pGC), &srcGC)) {
        return false;
    }
    GCSetDeviceBase(&srcGC, src_base);

    GCRECT src = {src_x, src_y, src_x + width, src_y + height};
    GCPOINT dst = {dest_x, dest_y};
    begin_access(ctx);
    GCCopyBits2(ctx->pGC, &srcGC, &src, &dst);
    GCDelete(&srcGC);

    ctx->emitted = true;
    display_usb_pending = true;
    return true;
}

bool copy_display_duplicate_rows(RenderContext *ctx, unsigned source_y, unsigned dest_y0, unsigned dest_y1, unsigned width)
{
    if (!ctx || !ctx->pGC || width == 0 || dest_y1 <= dest_y0) {
        return false;
    }

    bool emitted = false;
    for (unsigned y = dest_y0; y < dest_y1; ++y) {
        if (!copy_display_from_base(ctx,
                                    GCDeviceBase(ctx->pGC),
                                    0,
                                    (long)source_y,
                                    (long)width,
                                    1,
                                    0,
                                    (long)y)) {
            if (emitted) {
                ctx->emitted = true;
                display_usb_pending = true;
            }
            return emitted;
        }
        emitted = true;
    }

    ctx->emitted = true;
    display_usb_pending = true;
    return true;
}

bool copy_display_rect(RenderContext *ctx,
                       unsigned src_y,
                       unsigned dest_y,
                       unsigned width,
                       unsigned height)
{
    if (!ctx || !ctx->pGC || width == 0 || height == 0) {
        return false;
    }

    return copy_display_from_base(ctx,
                                  GCDeviceBase(ctx->pGC),
                                  0,
                                  (long)src_y,
                                  (long)width,
                                  (long)height,
                                  0,
                                  (long)dest_y);
}

bool display_page_is_current(unsigned page)
{
    return page < kDisplayPageCount &&
           display_page_content_valid[page] &&
           display_page_generation[page] == display_content_generation;
}

uint32_t next_display_content_generation()
{
    ++display_content_generation;
    if (display_content_generation == 0) {
        ++display_content_generation;
    }
    return display_content_generation;
}

bool clone_display_page(RenderContext *ctx, unsigned src_page, unsigned dest_page)
{
    if (!ctx || !ctx->pGC || src_page >= kDisplayPageCount || dest_page >= kDisplayPageCount ||
        src_page == dest_page || !display_page_content_valid[src_page] ||
        GCWidth(ctx->pGC) <= 0 || GCHeight(ctx->pGC) <= 0) {
        return false;
    }

    unsigned long old_base = GCDeviceBase(ctx->pGC);
    GCSetDeviceBase(ctx->pGC, display_page_base[dest_page]);
    bool copied = copy_display_from_base(ctx,
                                         display_page_base[src_page],
                                         0,
                                         0,
                                         GCWidth(ctx->pGC),
                                         GCHeight(ctx->pGC),
                                         0,
                                         0);
    GCSetDeviceBase(ctx->pGC, old_base);
    if (!copied) {
        return false;
    }

    std::memcpy(line_hash[dest_page], line_hash[src_page], sizeof(line_hash[dest_page]));
    std::memcpy(line_width[dest_page], line_width[src_page], sizeof(line_width[dest_page]));
    std::memcpy(line_page_version[dest_page], line_page_version[src_page], sizeof(line_page_version[dest_page]));
    display_page_clear_pending[dest_page] = false;
    display_page_content_valid[dest_page] = true;
    display_page_generation[dest_page] = display_page_generation[src_page];
    ctx->emitted = true;
    display_usb_pending = true;
    return true;
}

unsigned current_display_clone_source()
{
    if (display_present_pending &&
        display_pending_page < kDisplayPageCount &&
        display_page_is_current(display_pending_page)) {
        return display_pending_page;
    }
    if (display_page_is_current(display_visible_page)) {
        return display_visible_page;
    }
    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        if (display_page_is_current(page)) {
            return page;
        }
    }
    return kInvalidDisplayPage;
}

bool valid_display_clone_source_available()
{
    return current_display_clone_source() < kDisplayPageCount;
}

void ensure_draw_page_ready_for_partial(RenderContext *ctx)
{
    if (!ctx || !ctx->pGC || display_render_full_frame ||
        display_draw_page >= kDisplayPageCount) {
        return;
    }

    if (display_page_is_current(display_draw_page)) {
        display_draw_page_needs_clone = false;
        return;
    }

    unsigned src_page = current_display_clone_source();
    if (src_page < kDisplayPageCount &&
        clone_display_page(ctx, src_page, display_draw_page)) {
        display_draw_page_needs_clone = false;
        return;
    }

    display_draw_page_needs_clone = false;
    invalidate_display_page_hashes(display_draw_page);
    display_page_content_valid[display_draw_page] = false;
    display_render_full_frame = true;
    display_render_full_frame_from_start = false;
    request_display_dirty();
}

bool copy_display_line_raw(RenderContext *ctx, unsigned target_y0, unsigned target_y1, unsigned width)
{
    if (!ctx || !ctx->pGC || width == 0 || target_y1 <= target_y0) {
        return false;
    }

    GC srcGC;
    if (!GCCreateWithPreallocatedMemory(ctx->pGC, (long)width, 1, line_buffer, &srcGC)) {
        return false;
    }

    GCRECT src = {0, 0, (long)width, 1};
    GCPOINT dst = {0, (long)target_y0};
    begin_access(ctx);
    GCCopyBits2(ctx->pGC, &srcGC, &src, &dst);
    GCDelete(&srcGC);

    ctx->emitted = true;
    display_usb_pending = true;
    copy_display_duplicate_rows(ctx, target_y0, target_y0 + 1, target_y1, width);
    return true;
}

bool setup_display_pages(PGC pGC)
{
    uint32_t frame_bytes;
    bool bases_match = true;

    if (!pGC || !pGC->bitmap.handle || GCWidth(pGC) <= 0 || GCHeight(pGC) <= 0) {
        display_pages_ready = false;
        return false;
    }

    frame_bytes = (uint32_t)GCDeviceFrameBytes(pGC);
    if (!frame_bytes) {
        display_pages_ready = false;
        return false;
    }

    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        if (display_page_base[page] != frame_bytes * page) {
            bases_match = false;
            break;
        }
    }

    if (display_pages_ready && bases_match) {
        return true;
    }

    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        display_page_base[page] = frame_bytes * page;
    }
    display_visible_page = 0;
    display_draw_page = 1;
    display_pending_page = kInvalidDisplayPage;
    display_present_pending = false;
    display_draw_page_needs_clone = false;
    display_pages_ready =
        set_display_draw_base(pGC, display_page_base[0]) &&
        GCPresentDeviceDrawBase(pGC) &&
        set_display_draw_base(pGC, display_page_base[display_draw_page]);
    if (!display_pages_ready) {
        display_visible_page = 0;
        display_draw_page = 0;
        display_pending_page = kInvalidDisplayPage;
        display_present_pending = false;
        display_draw_page_needs_clone = false;
        set_display_draw_base(pGC, display_page_base[0]);
    }
    invalidate_line_hashes();
    mark_display_pages_clear_pending();
    return display_pages_ready;
}

unsigned choose_next_draw_page(unsigned fallback)
{
    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        if (page == display_visible_page) {
            continue;
        }
        if (display_present_pending && page == display_pending_page) {
            continue;
        }
        if (display_page_is_current(page)) {
            return page;
        }
    }

    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        if (page == display_visible_page) {
            continue;
        }
        if (display_present_pending && page == display_pending_page) {
            continue;
        }
        return page;
    }
    return fallback;
}

void clear_draw_page_if_needed(RenderContext *ctx)
{
    if (!ctx || !ctx->pGC || display_draw_page >= kDisplayPageCount ||
        !display_page_clear_pending[display_draw_page]) {
        return;
    }

    display_page_clear_pending[display_draw_page] = false;
    fill_display(ctx, 0, 0, GCWidth(ctx->pGC), GCHeight(ctx->pGC), makecol(0, 0, 0));
}

void queue_display_frame(RenderContext *ctx)
{
    unsigned next_draw_page;
    unsigned queued_page;

    if (!ctx || !ctx->pGC || !ctx->pGC->bitmap.handle) {
        return;
    }
    if (ctx->access_open) {
        GCPEndAccess(ctx->pGC);
        ctx->access_open = false;
    }
    if (!display_usb_pending) {
        ctx->emitted = false;
        return;
    }
    if (display_pages_ready &&
        display_render_full_frame &&
        !display_render_full_frame_from_start) {
        if (display_draw_page < kDisplayPageCount) {
            invalidate_display_page_hashes(display_draw_page);
            display_page_content_valid[display_draw_page] = false;
        }
        display_usb_pending = false;
        ctx->emitted = false;
        return;
    }

    if (display_pages_ready) {
        queued_page = display_draw_page;
        display_pending_page = queued_page;
        display_present_pending = true;

        present_pending_display_frame(ctx->pGC);
        if (!display_pages_ready) {
            display_pending_page = kInvalidDisplayPage;
            display_present_pending = false;
            display_draw_page = display_visible_page;
            set_display_draw_base(ctx->pGC, display_page_base[display_draw_page]);
            display_usb_pending = false;
            ctx->emitted = false;
            display_render_full_frame_from_start = false;
            return;
        }
        if (queued_page < kDisplayPageCount && display_visible_page == queued_page) {
            display_page_generation[queued_page] = next_display_content_generation();
            display_page_content_valid[queued_page] = true;
        }

        next_draw_page = choose_next_draw_page(display_draw_page);

        if (next_draw_page < kDisplayPageCount) {
            display_draw_page = next_draw_page;
            if (!set_display_draw_base(ctx->pGC, display_page_base[display_draw_page])) {
                printf("ega: DisplayLink draw page switch failed\n");
                display_pages_ready = false;
                display_present_pending = false;
                display_pending_page = kInvalidDisplayPage;
            }
        }
    }
    display_usb_pending = false;
    ctx->emitted = false;
    display_render_full_frame_from_start = false;
}

void present_pending_display_frame(PGC pGC)
{
    if (!pGC || !pGC->bitmap.handle || !display_pages_ready ||
        !display_present_pending || display_pending_page >= kDisplayPageCount) {
        return;
    }

    if (!GCPresentDeviceBase(pGC, display_page_base[display_pending_page])) {
        printf("ega: DisplayLink page present failed\n");
        display_pages_ready = false;
        display_present_pending = false;
        display_pending_page = kInvalidDisplayPage;
        display_draw_page = display_visible_page;
        set_display_draw_base(pGC, display_page_base[display_draw_page]);
        return;
    }

    display_visible_page = display_pending_page;
    display_pending_page = kInvalidDisplayPage;
    display_present_pending = false;
}

bool layout_changed(long gc_width, long gc_height, unsigned width, unsigned height)
{
    return !display_initialized ||
           gc_width != last_gc_width ||
           gc_height != last_gc_height ||
           width != last_source_width ||
           height != last_source_height;
}

void updatewindowsize(RenderContext *ctx, unsigned width, unsigned height)
{
    long gc_width = ctx && ctx->pGC ? GCWidth(ctx->pGC) : 0;
    long gc_height = ctx && ctx->pGC ? GCHeight(ctx->pGC) : 0;

    if (!ctx || !ctx->pGC || width == 0 || height == 0 ||
        gc_width <= 0 || gc_height <= 0) {
        display_target_width_px = 0;
        display_target_height_px = 0;
        return;
    }

    width = std::min<unsigned>(width, kMaxEgaWidth);
    height = std::min<unsigned>(height, kMaxEgaLines);
    display_target_width_px = std::min<unsigned>(width, (unsigned)gc_width);
    display_target_height_px = std::min<unsigned>(height, (unsigned)gc_height);
    bool changed = layout_changed(gc_width, gc_height, width, height);

    if (!display_pages_ready || changed) {
        setup_display_pages(ctx->pGC);
    }

    if (!changed) {
        clear_draw_page_if_needed(ctx);
        return;
    }

    last_gc_width = gc_width;
    last_gc_height = gc_height;
    last_source_width = width;
    last_source_height = height;
    display_initialized = true;
    invalidate_line_hashes();
    mark_display_pages_clear_pending();
    clear_draw_page_if_needed(ctx);
}

void draw_line(RenderContext *ctx,
               unsigned buffer_line,
               unsigned source_line,
               unsigned source_span,
               unsigned width,
               uint32_t rendered_version)
{
    if (!ctx || !ctx->pGC || !display_frame_valid ||
        buffer_line >= kMaxEgaLines || width == 0 ||
        source_line >= display_source_height) {
        return;
    }

    width = std::min<unsigned>(width, kMaxEgaWidth);
    if (!display_window_checked_for_frame) {
        updatewindowsize(ctx, display_source_width, display_source_height);
        display_window_checked_for_frame = true;
    }
    unsigned target_width = display_target_width_px;
    unsigned target_height = display_target_height_px;
    if (target_width == 0 || target_height == 0) {
        return;
    }

    unsigned source_end = std::min<unsigned>(source_line + std::max(1u, source_span), display_source_height);
    unsigned target_y0;
    unsigned target_y1;
    if (target_height == display_source_height) {
        target_y0 = source_line;
        target_y1 = source_end;
    } else {
        target_y0 = (source_line * target_height) / display_source_height;
        target_y1 = (source_end * target_height) / display_source_height;
    }
    if (target_y1 <= target_y0) {
        return;
    }
    target_y1 = std::min<unsigned>(target_y1, target_height);
    if (target_y0 >= target_y1) {
        return;
    }

    unsigned page = (display_draw_page < kDisplayPageCount) ? display_draw_page : 0u;
    uint32_t hash;
    unsigned fill_bytes = 0;
    unsigned raw_bytes = display_raw_line_bytes(target_width);
    bool fill_bytes_measured = false;
    if (display_render_full_frame) {
        if (line_width[page][buffer_line] == target_width) {
            hash = (target_width == width) ?
                   measure_line_hash(line_buffer, target_width) :
                   downscale_line_in_place(width, target_width);
        } else {
            LineMeasure measure = (target_width == width) ?
                                  measure_line_hash_and_fill(line_buffer, target_width, raw_bytes) :
                                  downscale_line_in_place_and_measure(width, target_width, raw_bytes);
            hash = measure.hash;
            fill_bytes = measure.fill_bytes;
            fill_bytes_measured = true;
        }
    } else {
        LineMeasure measure = (target_width == width) ?
                              measure_line_hash_and_fill(line_buffer, target_width, raw_bytes) :
                              downscale_line_in_place_and_measure(width, target_width, raw_bytes);
        hash = measure.hash;
        fill_bytes = measure.fill_bytes;
        fill_bytes_measured = true;
    }
    if (line_hash[page][buffer_line] == hash && line_width[page][buffer_line] == target_width) {
        clear_display_line_dirty(page, buffer_line, rendered_version);
        return;
    }

    if (source_line >= source_span && buffer_line > 0 &&
        line_hash[page][buffer_line - 1u] == hash &&
        line_width[page][buffer_line - 1u] == target_width) {
        unsigned prev_source_line = source_line - source_span;
        unsigned prev_target_y0;
        unsigned prev_target_y1;
        if (target_height == display_source_height) {
            prev_target_y0 = prev_source_line;
            prev_target_y1 = source_line;
        } else {
            prev_target_y0 = (prev_source_line * target_height) / display_source_height;
            prev_target_y1 = (source_line * target_height) / display_source_height;
        }
        unsigned target_height_px = target_y1 - target_y0;
        if (prev_target_y1 > prev_target_y0 &&
            prev_target_y1 - prev_target_y0 == target_height_px &&
            copy_display_rect(ctx, prev_target_y0, target_y0, target_width, target_height_px)) {
            line_hash[page][buffer_line] = hash;
            line_width[page][buffer_line] = (uint16_t)target_width;
            clear_display_line_dirty(page, buffer_line, rendered_version);
            return;
        }
    }

    if (!fill_bytes_measured) {
        fill_bytes = measure_line_fill_bytes(line_buffer, target_width, raw_bytes);
    }
    line_hash[page][buffer_line] = hash;
    line_width[page][buffer_line] = (uint16_t)target_width;

    bool copy_duplicate_rows = (target_y1 - target_y0) > 1 &&
                               fill_bytes > display_copy_row_bytes(target_width);
    unsigned fill_y1 = copy_duplicate_rows ? target_y0 + 1 : target_y1;

    if (raw_bytes < fill_bytes &&
        copy_display_line_raw(ctx, target_y0, target_y1, target_width)) {
        clear_display_line_dirty(page, buffer_line, rendered_version);
        return;
    }

    long run_start = 0;
    GCCOLOR run_color = line_buffer[0];
    for (unsigned x = 1; x < target_width; ++x) {
        GCCOLOR color = line_buffer[x];
        if (color == run_color) {
            continue;
        }
        fill_display_direct(ctx,
                            run_start,
                            target_y0,
                            (long)x - run_start,
                            fill_y1 - target_y0,
                            run_color);
        run_start = x;
        run_color = color;
    }
    fill_display_direct(ctx,
                        run_start,
                        target_y0,
                        (long)target_width - run_start,
                        fill_y1 - target_y0,
                        run_color);
    if (copy_duplicate_rows) {
        copy_display_duplicate_rows(ctx, target_y0, target_y0 + 1, target_y1, target_width);
    }
    clear_display_line_dirty(page, buffer_line, rendered_version);
}

void ega_recalctimings(Ega *e)
{
    e->vtotal = e->crtc[6];
    e->dispend = e->crtc[0x12];
    e->vsyncstart = e->crtc[0x10];
    e->split = e->crtc[0x18];

    if (e->crtc[7] & 1) e->vtotal |= 0x100;
    if (e->crtc[7] & 32) e->vtotal |= 0x200;
    e->vtotal++;

    if (e->crtc[7] & 2) e->dispend |= 0x100;
    if (e->crtc[7] & 64) e->dispend |= 0x200;
    e->dispend++;

    if (e->crtc[7] & 4) e->vsyncstart |= 0x100;
    if (e->crtc[7] & 128) e->vsyncstart |= 0x200;
    e->vsyncstart++;

    if (e->crtc[7] & 0x10) e->split |= 0x100;
    if (e->crtc[9] & 0x40) e->split |= 0x200;
    e->split += 2;

    e->hdisp = e->crtc[1] + 1;
    e->rowoffset = e->crtc[0x13];

    uint32_t hz = e->vidclock ? kMdaCharClockHz : kCgaCharClockHz;
    int disptime = e->crtc[0] + 2;
    int dispontime = e->crtc[1] + 1;
    if (e->seqregs[1] & 8) {
        disptime *= 2;
        dispontime *= 2;
    }
    int dispofftime = disptime - dispontime;
    bool nine_dot_chars = !(e->seqregs[1] & 1);
    __atomic_store_n(&e->dispontime, ega_time_from_chars(dispontime, hz, nine_dot_chars), __ATOMIC_RELEASE);
    __atomic_store_n(&e->dispofftime, ega_time_from_chars(dispofftime, hz, nine_dot_chars), __ATOMIC_RELEASE);
}

void __time_critical_func(refresh_active_palette)(Ega *e)
{
    for (int c = 0; c < 16; ++c) {
        e->active_palette[c] = e->pallook[e->egapal[c]];
    }
}

void __time_critical_func(rebuild_egapal)(Ega *e)
{
    for (int c = 0; c < 16; ++c) {
        if (e->attrregs[0x10] & 0x80) {
            e->egapal[c] = (e->attrregs[c] & 0x0f) | ((e->attrregs[0x14] & 0x0f) << 4);
        } else {
            e->egapal[c] = (e->attrregs[c] & 0x3f) | ((e->attrregs[0x14] & 0x0c) << 4);
        }
    }
    refresh_active_palette(e);
}

inline uint8_t __time_critical_func(vram_load)(uint32_t addr)
{
    return __atomic_load_n(&ega.vram[addr & ega.vrammask], __ATOMIC_RELAXED);
}

inline void __time_critical_func(vram_store)(uint32_t addr, uint8_t val)
{
    __atomic_store_n(&ega.vram[addr & ega.vrammask], val, __ATOMIC_RELAXED);
}

inline bool __time_critical_func(vram_store_changed)(uint32_t addr, uint8_t val)
{
    uint32_t masked = addr & ega.vrammask;
    uint8_t old = __atomic_load_n(&ega.vram[masked], __ATOMIC_RELAXED);
    if (old == val) {
        return false;
    }
    __atomic_store_n(&ega.vram[masked], val, __ATOMIC_RELAXED);
    return true;
}

inline void __time_critical_func(vram_store_write)(uint32_t addr, uint8_t val, bool track_changes, bool *changed)
{
    if (track_changes) {
        *changed |= vram_store_changed(addr, val);
    } else {
        vram_store(addr, val);
    }
}

unsigned text_char_width(const Ega *e)
{
    if (e->seqregs[1] & 8) {
        return (e->seqregs[1] & 1) ? 16u : 18u;
    }
    return (e->seqregs[1] & 1) ? 8u : 9u;
}

unsigned current_line_width(const Ega *e)
{
    if (!(e->gdcreg[6] & 1)) {
        return std::min<unsigned>((unsigned)e->hdisp * text_char_width(e), kMaxEgaWidth);
    }
    if (e->gdcreg[5] & 0x20) {
        return std::min<unsigned>((unsigned)e->hdisp * 16u, kMaxEgaWidth);
    }
    if (e->seqregs[1] & 8) {
        return std::min<unsigned>((unsigned)e->hdisp * 16u, kMaxEgaWidth);
    }
    return std::min<unsigned>((unsigned)e->hdisp * 8u, kMaxEgaWidth);
}

void put_pixel(unsigned x, GCCOLOR color)
{
    if (x < kMaxEgaWidth) {
        line_buffer[x] = color;
    }
}

void put_visible_pixel(int x, unsigned width, GCCOLOR color)
{
    if (x >= 0 && (unsigned)x < width && (unsigned)x < kMaxEgaWidth) {
        line_buffer[x] = color;
    }
}

void ega_draw_text(Ega *e)
{
    unsigned cw = text_char_width(e);
    unsigned scale = (e->seqregs[1] & 8) ? 2u : 1u;
    bool nine_dot = !(e->seqregs[1] & 1);
    const GCCOLOR *palette = e->active_palette;

    uint32_t ma = e->ma;
    for (int x = 0; x < e->hdisp; ++x) {
        int drawcursor = ((ma == e->ca) && e->con && e->cursoron);
        uint8_t chr = vram_load((ma << 1) & e->vrammask);
        uint8_t attr = vram_load(((ma << 1) + 1) & e->vrammask);
        uint32_t charaddr = (attr & 8) ? e->charsetb + (chr * 128u) : e->charseta + (chr * 128u);
        GCCOLOR fg;
        GCCOLOR bg;

        if (drawcursor) {
            bg = palette[attr & 15];
            fg = palette[attr >> 4];
        } else {
            fg = palette[attr & 15];
            bg = palette[attr >> 4];
            if ((attr & 0x80) && (e->attrregs[0x10] & 8)) {
                bg = palette[(attr >> 4) & 7];
                if (e->blink & 16) {
                    fg = bg;
                }
            }
        }

        uint8_t dat = vram_load(charaddr + ((uint32_t)e->sc << 2));
        unsigned base = (unsigned)x * cw;
        GCCOLOR ninth_color = (((chr & ~0x1f) == 0xc0) && (e->attrregs[0x10] & 4) && (dat & 1)) ? fg : bg;
        if (base + cw <= kMaxEgaWidth) {
            GCCOLOR *dst = line_buffer + base;
            if (scale == 1) {
                dst[0] = (dat & 0x80u) ? fg : bg;
                dst[1] = (dat & 0x40u) ? fg : bg;
                dst[2] = (dat & 0x20u) ? fg : bg;
                dst[3] = (dat & 0x10u) ? fg : bg;
                dst[4] = (dat & 0x08u) ? fg : bg;
                dst[5] = (dat & 0x04u) ? fg : bg;
                dst[6] = (dat & 0x02u) ? fg : bg;
                dst[7] = (dat & 0x01u) ? fg : bg;
                if (nine_dot) {
                    dst[8] = ninth_color;
                }
            } else {
                for (unsigned bit = 0; bit < 8; ++bit) {
                    GCCOLOR color = (dat & (0x80u >> bit)) ? fg : bg;
                    dst[bit * 2] = color;
                    dst[bit * 2 + 1] = color;
                }
                if (nine_dot) {
                    dst[16] = ninth_color;
                    dst[17] = ninth_color;
                }
            }
        } else {
            for (unsigned bit = 0; bit < 8; ++bit) {
                GCCOLOR color = (dat & (0x80u >> bit)) ? fg : bg;
                for (unsigned s = 0; s < scale; ++s) {
                    put_pixel(base + bit * scale + s, color);
                }
            }
            if (nine_dot) {
                for (unsigned s = 0; s < scale; ++s) {
                    put_pixel(base + 8 * scale + s, ninth_color);
                }
            }
        }
        ma = (ma + 4u) & e->vrammask;
    }
    e->ma = ma;
}

uint32_t ega_fetch_addr(Ega *e, int *oddeven)
{
    uint32_t addr = e->ma;
    *oddeven = 0;
    if (!(e->crtc[0x17] & 0x40)) {
        addr = (addr << 1) & e->vrammask;
        if (e->seqregs[1] & 4) {
            *oddeven = (addr & 4) ? 1 : 0;
        }
        addr &= ~7u;
        if ((e->crtc[0x17] & 0x20) && (e->ma & 0x20000)) addr |= 4;
        if (!(e->crtc[0x17] & 0x20) && (e->ma & 0x8000)) addr |= 4;
    }
    if (!(e->crtc[0x17] & 0x01)) addr = (addr & ~0x8000u) | ((e->sc & 1) ? 0x8000u : 0);
    if (!(e->crtc[0x17] & 0x02)) addr = (addr & ~0x10000u) | ((e->sc & 2) ? 0x10000u : 0);
    return addr & e->vrammask;
}

void ega_draw_2bpp(Ega *e, unsigned width)
{
    int hscroll = (e->scrollcache & 7) << 1;
    uint32_t step = (e->seqregs[1] & 4) ? 2u : 4u;
    const GCCOLOR *palette = e->active_palette;
    if (hscroll == 0) {
        unsigned visible_groups = std::min<unsigned>((unsigned)e->hdisp, width / 16u);
        for (unsigned x = 0; x < visible_groups; ++x) {
            int oddeven;
            uint32_t addr = ega_fetch_addr(e, &oddeven);
            uint8_t ed0 = vram_load(addr);
            uint8_t ed1 = vram_load(addr | 1u);
            e->ma = (e->ma + step) & e->vrammask;

            GCCOLOR *dst = line_buffer + x * 16u;
            GCCOLOR color = palette[(ed0 >> 6) & 3];
            dst[0] = color;
            dst[1] = color;
            color = palette[(ed0 >> 4) & 3];
            dst[2] = color;
            dst[3] = color;
            color = palette[(ed0 >> 2) & 3];
            dst[4] = color;
            dst[5] = color;
            color = palette[ed0 & 3];
            dst[6] = color;
            dst[7] = color;
            color = palette[(ed1 >> 6) & 3];
            dst[8] = color;
            dst[9] = color;
            color = palette[(ed1 >> 4) & 3];
            dst[10] = color;
            dst[11] = color;
            color = palette[(ed1 >> 2) & 3];
            dst[12] = color;
            dst[13] = color;
            color = palette[ed1 & 3];
            dst[14] = color;
            dst[15] = color;
        }
        e->ma = (e->ma + (((uint32_t)e->hdisp + 1u - visible_groups) * step)) & e->vrammask;
        return;
    }

    std::fill(line_buffer, line_buffer + width, palette[0]);
    for (int x = 0; x <= e->hdisp; ++x) {
        int base = x * 16 - hscroll;
        int oddeven;
        uint32_t addr = ega_fetch_addr(e, &oddeven);
        uint8_t ed0 = vram_load(addr);
        uint8_t ed1 = vram_load(addr | 1u);
        e->ma = (e->ma + step) & e->vrammask;
        for (int pair = 0; pair < 8; ++pair) {
            uint8_t src = (pair < 4) ? ed0 : ed1;
            uint8_t pix = (src >> ((3 - (pair & 3)) * 2)) & 3;
            GCCOLOR color = palette[pix];
            int dst = base + pair * 2;
            put_visible_pixel(dst, width, color);
            put_visible_pixel(dst + 1, width, color);
        }
    }
}

void ega_draw_4bpp(Ega *e, unsigned width)
{
    bool lowres = (e->seqregs[1] & 8) != 0;
    int pixels_per_group = lowres ? 16 : 8;
    int hscroll = (e->scrollcache & 7) * (lowres ? 2 : 1);
    uint32_t step = (e->seqregs[1] & 4) ? 2u : 4u;
    const GCCOLOR *palette = e->active_palette;
    uint8_t attr_mask = e->attrregs[0x12];
    if (hscroll == 0) {
        unsigned visible_groups = std::min<unsigned>((unsigned)e->hdisp, width / (unsigned)pixels_per_group);
        if (lowres) {
            for (unsigned x = 0; x < visible_groups; ++x) {
                int oddeven;
                uint32_t addr = ega_fetch_addr(e, &oddeven);
                uint8_t edat[4];
                if (e->seqregs[1] & 4) {
                    edat[0] = vram_load(addr | (uint32_t)oddeven);
                    edat[2] = vram_load(addr | (uint32_t)oddeven | 2u);
                    edat[1] = edat[3] = 0;
                } else {
                    edat[0] = vram_load(addr);
                    edat[1] = vram_load(addr | 1u);
                    edat[2] = vram_load(addr | 2u);
                    edat[3] = vram_load(addr | 3u);
                }
                e->ma = (e->ma + step) & e->vrammask;

                GCCOLOR *dst = line_buffer + x * 16u;
                for (int group = 0; group < 4; ++group) {
                    int shift = 6 - (group * 2);
                    uint8_t dat = edatlookup[(edat[0] >> shift) & 3][(edat[1] >> shift) & 3] |
                                  (uint8_t)(edatlookup[(edat[2] >> shift) & 3][(edat[3] >> shift) & 3] << 2);
                    GCCOLOR color0 = palette[(dat >> 4) & attr_mask];
                    GCCOLOR color1 = palette[(dat & 0x0f) & attr_mask];
                    unsigned base = (unsigned)group * 4u;
                    dst[base] = color0;
                    dst[base + 1] = color0;
                    dst[base + 2] = color1;
                    dst[base + 3] = color1;
                }
            }
        } else {
            for (unsigned x = 0; x < visible_groups; ++x) {
                int oddeven;
                uint32_t addr = ega_fetch_addr(e, &oddeven);
                uint8_t edat[4];
                if (e->seqregs[1] & 4) {
                    edat[0] = vram_load(addr | (uint32_t)oddeven);
                    edat[2] = vram_load(addr | (uint32_t)oddeven | 2u);
                    edat[1] = edat[3] = 0;
                } else {
                    edat[0] = vram_load(addr);
                    edat[1] = vram_load(addr | 1u);
                    edat[2] = vram_load(addr | 2u);
                    edat[3] = vram_load(addr | 3u);
                }
                e->ma = (e->ma + step) & e->vrammask;

                GCCOLOR *dst = line_buffer + x * 8u;
                for (int group = 0; group < 4; ++group) {
                    int shift = 6 - (group * 2);
                    uint8_t dat = edatlookup[(edat[0] >> shift) & 3][(edat[1] >> shift) & 3] |
                                  (uint8_t)(edatlookup[(edat[2] >> shift) & 3][(edat[3] >> shift) & 3] << 2);
                    dst[group * 2] = palette[(dat >> 4) & attr_mask];
                    dst[group * 2 + 1] = palette[(dat & 0x0f) & attr_mask];
                }
            }
        }
        e->ma = (e->ma + (((uint32_t)e->hdisp + 1u - visible_groups) * step)) & e->vrammask;
        return;
    }

    std::fill(line_buffer, line_buffer + width, palette[0]);
    for (int x = 0; x <= e->hdisp; ++x) {
        int group_base = x * pixels_per_group - hscroll;
        int oddeven;
        uint32_t addr = ega_fetch_addr(e, &oddeven);
        uint8_t edat[4];
        if (e->seqregs[1] & 4) {
            edat[0] = vram_load(addr | (uint32_t)oddeven);
            edat[2] = vram_load(addr | (uint32_t)oddeven | 2u);
            edat[1] = edat[3] = 0;
        } else {
            edat[0] = vram_load(addr);
            edat[1] = vram_load(addr | 1u);
            edat[2] = vram_load(addr | 2u);
            edat[3] = vram_load(addr | 3u);
        }
        e->ma = (e->ma + step) & e->vrammask;

        for (int group = 0; group < 4; ++group) {
            int shift = 6 - (group * 2);
            uint8_t dat = edatlookup[(edat[0] >> shift) & 3][(edat[1] >> shift) & 3] |
                          (uint8_t)(edatlookup[(edat[2] >> shift) & 3][(edat[3] >> shift) & 3] << 2);
            uint8_t pix0 = (dat >> 4) & attr_mask;
            uint8_t pix1 = (dat & 0x0f) & attr_mask;
            int base = group_base + group * (lowres ? 4 : 2);
            GCCOLOR color0 = palette[pix0];
            GCCOLOR color1 = palette[pix1];
            put_visible_pixel(base, width, color0);
            if (lowres) {
                put_visible_pixel(base + 1, width, color0);
                put_visible_pixel(base + 2, width, color1);
                put_visible_pixel(base + 3, width, color1);
            } else {
                put_visible_pixel(base + 1, width, color1);
            }
        }
    }
}

void advance_current_line_without_render(Ega *e)
{
    if (!e || e->scrblank || e->hdisp <= 0) {
        return;
    }

    if (!(e->gdcreg[6] & 1)) {
        e->ma = (e->ma + (uint32_t)e->hdisp * 4u) & e->vrammask;
    } else {
        uint32_t step = (e->seqregs[1] & 4) ? 2u : 4u;
        e->ma = (e->ma + ((uint32_t)e->hdisp + 1u) * step) & e->vrammask;
    }
}

bool should_skip_current_line_render(const Ega *e)
{
    if (!display_render_full_frame &&
        __atomic_load_n(&display_full_render_dirty, __ATOMIC_ACQUIRE) != 0) {
        display_render_frame = true;
        display_render_full_frame = true;
        display_render_full_frame_from_start = false;
    }

    if (display_render_frame || !display_frame_valid || !display_initialized || !display_pages_ready) {
        if (!display_render_frame || !e) {
            return false;
        }
    } else if (__atomic_load_n(&display_content_dirty, __ATOMIC_ACQUIRE) != 0) {
        display_render_frame = true;
        display_render_full_frame =
            __atomic_load_n(&display_full_render_dirty, __ATOMIC_ACQUIRE) != 0;
    } else {
        return true;
    }

    if (display_render_full_frame && !display_render_full_frame_from_start) {
        return true;
    }

    if (display_render_full_frame || !e) {
        return false;
    }

    unsigned page = (display_draw_page < kDisplayPageCount) ? display_draw_page : 0u;
    return !is_display_line_dirty(page, (unsigned)e->displine);
}

void draw_current_line(RenderContext *ctx, Ega *e)
{
    unsigned width = current_line_width(e);
    if (width == 0 || e->displine < 0 || e->displine >= (int)kMaxEgaLines) {
        return;
    }

    if (display_render_frame) {
        ensure_draw_page_ready_for_partial(ctx);
    }
    if (should_skip_current_line_render(e)) {
        advance_current_line_without_render(e);
        return;
    }
    if (display_render_frame) {
        ensure_draw_page_ready_for_partial(ctx);
    }

    uint32_t rendered_version = display_line_dirty_version((unsigned)e->displine);
    if (e->scrblank) {
        std::fill(line_buffer, line_buffer + width, makecol(0, 0, 0));
    } else if (!(e->gdcreg[6] & 1)) {
        ega_draw_text(e);
    } else if (e->gdcreg[5] & 0x20) {
        ega_draw_2bpp(e, width);
    } else {
        ega_draw_4bpp(e, width);
    }

    if (!ctx || !ctx->pGC || !display_frame_valid ||
        e->displine < display_firstline) {
        return;
    }
    unsigned source_line = ((unsigned)e->displine - (unsigned)display_firstline) * display_vertical_scale;
    draw_line(ctx,
              (unsigned)e->displine,
              source_line,
              display_vertical_scale,
              width,
              rendered_version);
}

void publish_frame(Ega *e)
{
    int height = std::max(0, e->lastline - e->firstline);
    unsigned scale = (e->vres || height <= 200) ? 2u : 1u;
    unsigned width = current_line_width(e);
    unsigned source_height = std::min<unsigned>((unsigned)height * scale, kMaxEgaLines);
    bool was_valid = display_frame_valid;
    bool changed = width != display_source_width ||
                   source_height != display_source_height ||
                   e->firstline != display_firstline ||
                   scale != display_vertical_scale;
    bool dirty = __atomic_exchange_n(&display_content_dirty, 0u, __ATOMIC_ACQ_REL) != 0;
    bool full_dirty = __atomic_exchange_n(&display_full_render_dirty, 0u, __ATOMIC_ACQ_REL) != 0;
    __atomic_store_n(&display_dirty_line_marks, 0u, __ATOMIC_RELEASE);
    bool draw_page_needs_current_contents =
        display_pages_ready &&
        display_draw_page < kDisplayPageCount &&
        !display_page_is_current(display_draw_page);

    if (changed) {
        invalidate_line_hashes();
        draw_page_needs_current_contents = false;
    }

    bool clone_source_available = valid_display_clone_source_available();
    bool draw_page_must_repaint = dirty &&
                                  draw_page_needs_current_contents &&
                                  !clone_source_available;

    display_firstline = e->firstline;
    display_source_width = width;
    display_source_height = source_height;
    display_vertical_scale = scale;
    display_window_checked_for_frame = false;
    display_frame_valid = true;
    display_render_full_frame = !was_valid || changed || full_dirty ||
                                !display_initialized || !display_pages_ready ||
                                draw_page_must_repaint;
    display_render_full_frame_from_start = display_render_full_frame;
    display_draw_page_needs_clone = !display_render_full_frame &&
                                    dirty &&
                                    draw_page_needs_current_contents &&
                                    clone_source_available;
    display_render_frame = display_render_full_frame ||
                           dirty;

    e->frames++;
    e->video_res_x = xsize;
    e->video_res_y = ysize + 1;
    if (!(e->gdcreg[6] & 1)) {
        e->video_res_x /= (e->seqregs[1] & 1) ? 8 : 9;
        e->video_res_y /= (e->crtc[9] & 31) + 1;
        e->video_bpp = 0;
    } else {
        if (e->crtc[9] & 0x80) e->video_res_y /= 2;
        if (!(e->crtc[0x17] & 1)) e->video_res_y *= 2;
        e->video_res_y /= (e->crtc[9] & 31) + 1;
        if (e->seqregs[1] & 8) e->video_res_x /= 2;
        e->video_bpp = (e->gdcreg[5] & 0x20) ? 2 : 4;
    }
}

uint32_t ega_poll(Ega *e, RenderContext *ctx)
{
    int x;
    if (!e->linepos) {
        e->stat |= 1;
        e->linepos = 1;
        if (e->dispon) {
            if (e->firstline == 2000) {
                e->firstline = e->displine;
            }
            draw_current_line(ctx, e);
            if (e->lastline < e->displine) {
                e->lastline = e->displine;
            }
        }
        e->displine++;
        if ((e->stat & 8) && ((e->displine & 15) == (e->crtc[0x11] & 15)) && e->vslines) {
            e->stat &= (uint8_t)~8u;
        }
        e->vslines++;
        if (e->displine > 500) {
            e->displine = 0;
        }
        return ega_delay_us(e, load_dispofftime(e));
    }

    if (e->dispon) {
        e->stat &= (uint8_t)~1u;
    }
    e->linepos = 0;
    if (e->sc == (e->crtc[11] & 31)) {
        e->con = 0;
    }
    if (e->dispon) {
        if (e->sc == (e->crtc[9] & 31)) {
            e->sc = 0;
            if (e->sc == (e->crtc[11] & 31)) {
                e->con = 0;
            }
            e->maback = (e->maback + ((uint32_t)e->rowoffset << 3)) & e->vrammask;
            e->ma = e->maback;
        } else {
            e->sc++;
            e->sc &= 31;
            e->ma = e->maback;
        }
    }
    e->vc++;
    e->vc &= 1023;
    if (e->vc == e->split) {
        e->ma = e->maback = 0;
        if (e->attrregs[0x10] & 0x20) {
            e->scrollcache = 0;
        }
    }
    if (e->vc == e->dispend) {
        e->dispon = 0;
        e->cursoron = (e->crtc[10] & 0x20) ? 0 : (e->blink & 16);
        uint8_t old_blink_phase = e->blink & 16;
        e->blink++;
        if (!(e->gdcreg[6] & 1) && old_blink_phase != (e->blink & 16)) {
            request_display_dirty();
        }
    }
    if (e->vc == e->vsyncstart) {
        e->dispon = 0;
        e->stat |= 8;
        if (e->seqregs[1] & 8) {
            x = e->hdisp * ((e->seqregs[1] & 1) ? 8 : 9) * 2;
        } else {
            x = e->hdisp * ((e->seqregs[1] & 1) ? 8 : 9);
        }
        if (x != xsize || (e->lastline - e->firstline) != ysize) {
            xsize = x;
            ysize = e->lastline - e->firstline;
            if (xsize < 64) xsize = 656;
            if (ysize < 32) ysize = 200;
        }
        queue_display_frame(ctx);
        publish_frame(e);
        e->firstline = 2000;
        e->lastline = 0;
        e->maback = e->ma = (e->crtc[0xc] << 8) | e->crtc[0xd];
        e->ca = (e->crtc[0xe] << 8) | e->crtc[0xf];
        e->ma <<= 2;
        e->maback <<= 2;
        e->ca <<= 2;
        e->vslines = 0;
    }
    if (e->vc == e->vtotal) {
        e->vc = 0;
        e->sc = e->crtc[8] & 0x1f;
        e->dispon = 1;
        e->displine = 0;
        e->scrollcache = e->attrregs[0x13] & 7;
    }
    if (e->sc == (e->crtc[10] & 31)) {
        e->con = 1;
    }
    return ega_delay_us(e, load_dispontime(e));
}

void ega_advance_state(RenderContext *ctx)
{
    uint32_t now = time_us_32();
    if (!ega.timing_started) {
        ega.next_poll_us = now;
        ega.timing_started = true;
    }

    unsigned steps = 0;
    while ((int32_t)(now - ega.next_poll_us) >= 0 && steps < kMaxPollStepsPerTick) {
        ega.next_poll_us += ega_poll(&ega, ctx);
        ++steps;
    }
    if (steps == kMaxPollStepsPerTick && (int32_t)(now - ega.next_poll_us) > 0) {
        ega.next_poll_us = now + 1;
    }
}

uint32_t __time_critical_func(normalize_vram_address)(uint32_t addr, int *readplane_or_mask)
{
    if (addr >= 0xb0000) {
        addr &= 0x7fffu;
    } else {
        addr &= 0xffffu;
    }

    if (readplane_or_mask && ega.chain2_read) {
        *readplane_or_mask = (*readplane_or_mask & 2) | (addr & 1);
        addr &= ~1u;
        if (addr & 0x4000) addr |= 1;
        addr &= ~0x4000u;
    }

    return addr << 2;
}

bool __time_critical_func(ega_vram_active)(uint32_t address)
{
    switch (ega.gdcreg[6] & 0x0c) {
    case 0x00:
        return address >= 0xa0000 && address < 0xc0000;
    case 0x04:
        return address >= 0xa0000 && address < 0xb0000;
    case 0x08:
        return address >= 0xb0000 && address < 0xb8000;
    case 0x0c:
        return address >= 0xb8000 && address < 0xc0000;
    default:
        return false;
    }
}

int __time_critical_func(mark_text_cell_dirty)(uint32_t cell)
{
    if ((ega.gdcreg[6] & 1) || !ega.chain2_write ||
        !display_frame_valid || display_source_height == 0 ||
        display_vertical_scale == 0 || ega.hdisp <= 0 || ega.rowoffset <= 0 ||
        display_firstline < 0 || display_firstline >= (int)kMaxEgaLines ||
        ega.split <= ega.dispend) {
        return kDirtyUnmapped;
    }

    unsigned visible_lines = display_source_height / display_vertical_scale;
    unsigned char_height = (unsigned)(ega.crtc[9] & 31) + 1u;
    if (visible_lines == 0) {
        return kDirtyUnmapped;
    }

    uint32_t start_cell = ((uint32_t)ega.crtc[0x0c] << 8) | ega.crtc[0x0d];
    uint32_t row_cells = (uint32_t)ega.rowoffset * 2u;
    uint32_t visible_rows = (visible_lines + char_height - 1u) / char_height;
    if (row_cells == 0 ||
        start_cell >= 0x2000u ||
        visible_rows > 0x2000u / row_cells ||
        start_cell + visible_rows * row_cells > 0x2000u) {
        return kDirtyUnmapped;
    }

    if (cell < start_cell) {
        return kDirtyNoVisibleChange;
    }

    uint32_t relative = cell - start_cell;
    uint32_t row = relative / row_cells;
    uint32_t column = relative - row * row_cells;
    if (column >= (uint32_t)ega.hdisp) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line_offset = row * char_height;
    if (first_line_offset >= visible_lines) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line = (unsigned)display_firstline + first_line_offset;
    unsigned end_offset = std::min<unsigned>(first_line_offset + char_height, visible_lines);
    unsigned end_line = (unsigned)display_firstline + end_offset;
    if (first_line >= kMaxEgaLines) {
        return kDirtyNoVisibleChange;
    }
    end_line = std::min<unsigned>(end_line, kMaxEgaLines);
    mark_display_line_range_dirty(first_line, end_line);
    return kDirtyMapped;
}

int __time_critical_func(mark_text_vram_write_dirty)(uint32_t address)
{
    uint32_t offset = (address >= 0xb0000) ? (address & 0x7fffu) : (address & 0xffffu);
    if (offset & 0x4000u) {
        return kDirtyUnmapped;
    }

    return mark_text_cell_dirty(offset >> 1);
}

void __time_critical_func(mark_cursor_cells_dirty)(uint32_t old_cell, uint32_t new_cell)
{
    if ((ega.gdcreg[6] & 1) || (ega.crtc[10] & 0x20)) {
        return;
    }

    int old_result = mark_text_cell_dirty(old_cell);
    int new_result = (new_cell == old_cell) ? old_result : mark_text_cell_dirty(new_cell);
    if (old_result == kDirtyUnmapped || new_result == kDirtyUnmapped) {
        request_display_dirty();
    }
}

void __time_critical_func(mark_cursor_cell_dirty)(uint32_t cell)
{
    if (ega.gdcreg[6] & 1) {
        return;
    }

    if (mark_text_cell_dirty(cell) == kDirtyUnmapped) {
        request_display_dirty();
    }
}

bool __time_critical_func(crtc_reg_affects_timing)(uint8_t index)
{
    switch (index) {
    case 0x00:
    case 0x01:
    case 0x06:
    case 0x07:
    case 0x09:
    case 0x10:
    case 0x12:
    case 0x13:
    case 0x18:
        return true;
    default:
        return false;
    }
}

bool __time_critical_func(crtc_reg_affects_pixels)(uint8_t index)
{
    switch (index) {
    case 0x01:
    case 0x07:
    case 0x08:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x12:
    case 0x13:
    case 0x17:
    case 0x18:
        return true;
    default:
        return false;
    }
}

int __time_critical_func(mark_graphics_vram_write_dirty)(uint32_t address)
{
    if (!(ega.gdcreg[6] & 1) || ega.chain2_write || (ega.seqregs[1] & 4) ||
        (ega.crtc[0x17] & 0x43) != 0x43 ||
        !display_frame_valid || display_source_height == 0 ||
        display_vertical_scale == 0 || ega.hdisp <= 0 || ega.rowoffset <= 0 ||
        display_firstline < 0 || display_firstline >= (int)kMaxEgaLines ||
        ega.split <= ega.dispend) {
        return kDirtyUnmapped;
    }

    unsigned visible_lines = display_source_height / display_vertical_scale;
    unsigned scanlines_per_row = (unsigned)(ega.crtc[9] & 31) + 1u;
    if (visible_lines == 0) {
        return kDirtyUnmapped;
    }

    uint32_t base = normalize_vram_address(address, nullptr) & ~3u;
    uint32_t start_base = (((uint32_t)ega.crtc[0x0c] << 8) | ega.crtc[0x0d]) << 2;
    uint32_t row_bytes = (uint32_t)ega.rowoffset << 3;
    uint32_t visible_rows = (visible_lines + scanlines_per_row - 1u) / scanlines_per_row;
    if (row_bytes == 0 ||
        start_base >= ega.vram_limit ||
        visible_rows > (ega.vram_limit - start_base) / row_bytes ||
        base >= ega.vram_limit) {
        return kDirtyUnmapped;
    }

    if (base < start_base) {
        return kDirtyNoVisibleChange;
    }

    uint32_t relative = base - start_base;
    uint32_t row = relative / row_bytes;
    uint32_t row_offset = relative - row * row_bytes;
    uint32_t visible_groups = (uint32_t)ega.hdisp + ((ega.scrollcache & 7) ? 1u : 0u);
    if (visible_groups == 0 || row_offset / 4u >= visible_groups) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line_offset = row * scanlines_per_row;
    if (first_line_offset >= visible_lines) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line = (unsigned)display_firstline + first_line_offset;
    unsigned end_offset = std::min<unsigned>(first_line_offset + scanlines_per_row, visible_lines);
    unsigned end_line = (unsigned)display_firstline + end_offset;
    if (first_line >= kMaxEgaLines) {
        return kDirtyNoVisibleChange;
    }
    end_line = std::min<unsigned>(end_line, kMaxEgaLines);
    mark_display_line_range_dirty(first_line, end_line);
    return kDirtyMapped;
}

bool __time_critical_func(ega_write)(uint32_t addr, uint8_t val, bool track_changes)
{
    uint8_t vala, valb, valc, vald;
    int writemask2 = ega.writemask;
    bool changed = false;

    if (ega.chain2_write) {
        writemask2 &= ~0x0a;
        if (addr & 1) writemask2 <<= 1;
        addr &= ~1u;
        if (addr & 0x4000) addr |= 1;
        addr &= ~0x4000u;
    }

    addr = normalize_vram_address(addr, nullptr);
    if (addr >= ega.vram_limit) {
        return false;
    }

    switch (ega.writemode) {
    case 1:
        if (writemask2 & 1) vram_store_write(addr, ega.la, track_changes, &changed);
        if (writemask2 & 2) vram_store_write(addr | 1u, ega.lb, track_changes, &changed);
        if (writemask2 & 4) vram_store_write(addr | 2u, ega.lc, track_changes, &changed);
        if (writemask2 & 8) vram_store_write(addr | 3u, ega.ld, track_changes, &changed);
        break;
    case 0:
        if (ega.gdcreg[3] & 7) {
            val = ega_rotate[ega.gdcreg[3] & 7][val];
        }
        if (ega.gdcreg[1] & 1) vala = (ega.gdcreg[0] & 1) ? 0xff : 0; else vala = val;
        if (ega.gdcreg[1] & 2) valb = (ega.gdcreg[0] & 2) ? 0xff : 0; else valb = val;
        if (ega.gdcreg[1] & 4) valc = (ega.gdcreg[0] & 4) ? 0xff : 0; else valc = val;
        if (ega.gdcreg[1] & 8) vald = (ega.gdcreg[0] & 8) ? 0xff : 0; else vald = val;
        [[fallthrough]];
    case 2:
        if (ega.writemode == 2) {
            vala = (val & 1) ? 0xff : 0;
            valb = (val & 2) ? 0xff : 0;
            valc = (val & 4) ? 0xff : 0;
            vald = (val & 8) ? 0xff : 0;
        }
        switch (ega.gdcreg[3] & 0x18) {
        case 0x00:
            if (writemask2 & 1) vram_store_write(addr, (vala & ega.gdcreg[8]) | (ega.la & ~ega.gdcreg[8]), track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & ega.gdcreg[8]) | (ega.lb & ~ega.gdcreg[8]), track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & ega.gdcreg[8]) | (ega.lc & ~ega.gdcreg[8]), track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & ega.gdcreg[8]) | (ega.ld & ~ega.gdcreg[8]), track_changes, &changed);
            break;
        case 0x08:
            if (writemask2 & 1) vram_store_write(addr, (vala | ~ega.gdcreg[8]) & ega.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb | ~ega.gdcreg[8]) & ega.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc | ~ega.gdcreg[8]) & ega.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald | ~ega.gdcreg[8]) & ega.ld, track_changes, &changed);
            break;
        case 0x10:
            if (writemask2 & 1) vram_store_write(addr, (vala & ega.gdcreg[8]) | ega.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & ega.gdcreg[8]) | ega.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & ega.gdcreg[8]) | ega.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & ega.gdcreg[8]) | ega.ld, track_changes, &changed);
            break;
        case 0x18:
            if (writemask2 & 1) vram_store_write(addr, (vala & ega.gdcreg[8]) ^ ega.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & ega.gdcreg[8]) ^ ega.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & ega.gdcreg[8]) ^ ega.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & ega.gdcreg[8]) ^ ega.ld, track_changes, &changed);
            break;
        }
        break;
    }
    return changed;
}

uint8_t __time_critical_func(ega_read)(uint32_t addr)
{
    int readplane = ega.readplane;
    addr = normalize_vram_address(addr, &readplane);
    if (addr >= ega.vram_limit) {
        return 0xff;
    }

    ega.la = vram_load(addr);
    ega.lb = vram_load(addr | 1u);
    ega.lc = vram_load(addr | 2u);
    ega.ld = vram_load(addr | 3u);
    if (ega.readmode) {
        uint8_t a = (ega.la ^ ((ega.colourcompare & 1) ? 0xff : 0)) & ((ega.colournocare & 1) ? 0xff : 0);
        uint8_t b = (ega.lb ^ ((ega.colourcompare & 2) ? 0xff : 0)) & ((ega.colournocare & 2) ? 0xff : 0);
        uint8_t c = (ega.lc ^ ((ega.colourcompare & 4) ? 0xff : 0)) & ((ega.colournocare & 4) ? 0xff : 0);
        uint8_t d = (ega.ld ^ ((ega.colourcompare & 8) ? 0xff : 0)) & ((ega.colournocare & 8) ? 0xff : 0);
        return (uint8_t)~(a | b | c | d);
    }
    return vram_load(addr | (uint32_t)(readplane & 3));
}

void __time_critical_func(ega_out)(uint16_t addr, uint8_t val)
{
    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega.miscout & 1)) {
        addr ^= 0x60;
    }

    switch (addr) {
    case 0x3c0:
        if (!ega.attrff) {
            ega.attraddr = val & 31;
        } else {
            uint8_t index = ega.attraddr & 31;
            uint8_t old = ega.attrregs[index];
            ega.attrregs[index] = val;
            if (index == 0x10 || index == 0x14 || index < 0x10) {
                rebuild_egapal(&ega);
            }
            if (old != val && (index < 0x10 || index == 0x10 || index == 0x12 ||
                               index == 0x13 || index == 0x14)) {
                request_display_dirty();
            }
        }
        ega.attrff ^= 1;
        break;
    case 0x3c2:
    {
        uint8_t old_vres = ega.vres;
        uint8_t old_vidclock = ega.vidclock;
        egaswitchread = val & 0x0c;
        ega.vres = !(val & 0x80);
        ega.pallook = ega.vres ? pallook16 : pallook64;
        refresh_active_palette(&ega);
        ega.vidclock = val & 4;
        ega.miscout = val;
        if (old_vidclock != ega.vidclock) {
            request_timing_recalc();
        }
        if (old_vres != ega.vres) {
            request_display_dirty();
        }
        break;
    }
    case 0x3c4:
        ega.seqaddr = val;
        break;
    case 0x3c5: {
        uint8_t old = ega.seqregs[ega.seqaddr & 0x0f];
        ega.seqregs[ega.seqaddr & 0x0f] = val;
        if (old != val) {
            switch (ega.seqaddr & 0x0f) {
            case 1:
                request_timing_recalc();
                request_display_dirty();
                break;
            case 3:
                request_display_dirty();
                break;
            }
        }
        switch (ega.seqaddr & 0x0f) {
        case 1:
            ega.scrblank = (ega.scrblank & ~0x20u) | (val & 0x20u);
            break;
        case 2:
            ega.writemask = val & 0x0f;
            break;
        case 3:
            ega.charsetb = (((val >> 2) & 3) * 0x10000u) + 2u;
            ega.charseta = ((val & 3) * 0x10000u) + 2u;
            break;
        case 4:
            ega.chain2_write = !(val & 4);
            break;
        }
        break;
    }
    case 0x3ce:
        ega.gdcaddr = val;
        break;
    case 0x3cf:
    {
        uint8_t index = ega.gdcaddr & 0x0f;
        uint8_t old = ega.gdcreg[index];
        ega.gdcreg[index] = val;
        switch (index) {
        case 2:
            ega.colourcompare = val;
            break;
        case 4:
            ega.readplane = val & 3;
            break;
        case 5:
            ega.writemode = val & 3;
            ega.readmode = val & 8;
            ega.chain2_read = val & 0x10;
            break;
        case 7:
            ega.colournocare = val;
            break;
        }
        if (old != val) {
            if ((index == 5 && ((old ^ val) & 0x20)) ||
                (index == 6 && ((old ^ val) & 0x01))) {
                request_display_dirty();
            }
        }
        break;
    }
    case 0x3d4:
        ega.crtcreg = val & 31;
        break;
    case 0x3d5: {
        uint8_t index = ega.crtcreg;
        if (index <= 7 && (ega.crtc[0x11] & 0x80)) {
            return;
        }
        uint32_t old_cursor = ((uint32_t)ega.crtc[0x0e] << 8) | ega.crtc[0x0f];
        bool old_cursor_visible = !(ega.crtc[0x0a] & 0x20);
        uint8_t old = ega.crtc[index];
        ega.crtc[index] = val;
        if (old == val) {
            break;
        }
        if (index == 0x0e || index == 0x0f) {
            uint32_t new_cursor = ((uint32_t)ega.crtc[0x0e] << 8) | ega.crtc[0x0f];
            mark_cursor_cells_dirty(old_cursor, new_cursor);
        } else if (index == 0x0a || index == 0x0b) {
            if (old_cursor_visible || !(ega.crtc[0x0a] & 0x20)) {
                mark_cursor_cell_dirty(old_cursor);
            }
        }
        if (crtc_reg_affects_timing(index)) {
            request_timing_recalc();
        }
        if (crtc_reg_affects_pixels(index)) {
            request_display_dirty();
        }
        break;
    }
    }
}

uint8_t __time_critical_func(ega_in)(uint16_t addr)
{
    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(ega.miscout & 1)) {
        addr ^= 0x60;
    }

    switch (addr) {
    case 0x3c0:
        return ega.attraddr;
    case 0x3c1:
        return ega.attrregs[ega.attraddr & 31];
    case 0x3c2:
        switch (egaswitchread) {
        case 0x0c: return (egaswitches & 1) ? 0x10 : 0;
        case 0x08: return (egaswitches & 2) ? 0x10 : 0;
        case 0x04: return (egaswitches & 4) ? 0x10 : 0;
        case 0x00: return (egaswitches & 8) ? 0x10 : 0;
        }
        return 0;
    case 0x3c4:
        return ega.seqaddr;
    case 0x3c5:
        return ega.seqregs[ega.seqaddr & 0x0f];
    case 0x3ce:
        return ega.gdcaddr;
    case 0x3cf:
        return ega.gdcreg[ega.gdcaddr & 0x0f];
    case 0x3d4:
        return ega.crtcreg;
    case 0x3d5:
        return ega.crtc[ega.crtcreg];
    case 0x3da:
        ega.attrff = 0;
        ega.stat ^= 0x30;
        return ega.stat;
    default:
        return 0xff;
    }
}

bool __time_critical_func(ega_io_read)(uint16_t port, uint8_t *data)
{
    *data = ega_in(port);
    return true;
}

void __time_critical_func(ega_io_write)(uint16_t port, uint8_t data)
{
    ega_out(port, data);
}

bool __time_critical_func(ega_mem_read)(uint32_t address, uint8_t *data)
{
    if (!ega_vram_active(address)) {
        return false;
    }
    *data = ega_read(address);
    return true;
}

void __time_critical_func(ega_mem_write)(uint32_t address, uint8_t data)
{
    if (ega_vram_active(address)) {
        bool full_dirty_pending =
            __atomic_load_n(&display_full_render_dirty, __ATOMIC_ACQUIRE) != 0;
        bool track_changes =
            !full_dirty_pending &&
            __atomic_load_n(&display_content_dirty, __ATOMIC_ACQUIRE) == 0;
        bool changed = ega_write(address, data, track_changes);
        if (full_dirty_pending) {
            return;
        }
        if (!track_changes || changed) {
            int dirty_result = mark_text_vram_write_dirty(address);
            if (dirty_result == kDirtyUnmapped) {
                dirty_result = mark_graphics_vram_write_dirty(address);
            }
            if (dirty_result != kDirtyUnmapped) {
                if (dirty_result == kDirtyNoVisibleChange) {
                    return;
                }
                __atomic_store_n(&display_content_dirty, 1u, __ATOMIC_RELEASE);
                return;
            }
            request_display_dirty();
        }
    }
}

bool __time_critical_func(ega_rom_read)(uint32_t address, uint8_t *data)
{
    if (address < kEgaRomBase || address >= kEgaRomBase + kEgaRomSize) {
        return false;
    }
    uint32_t offset = address - kEgaRomBase;
    *data = (offset < kEgaRomImageSize) ? kPcemEgaBiosRom[offset] : 0xff;
    return true;
}

void build_tables()
{
    for (int c = 0; c < 256; ++c) {
        int e = c;
        for (int d = 0; d < 8; ++d) {
            ega_rotate[d][c] = (uint8_t)e;
            e = (e >> 1) | ((e & 1) ? 0x80 : 0);
        }
    }
    for (int c = 0; c < 4; ++c) {
        for (int d = 0; d < 4; ++d) {
            edatlookup[c][d] = 0;
            if (c & 1) edatlookup[c][d] |= 1;
            if (d & 1) edatlookup[c][d] |= 2;
            if (c & 2) edatlookup[c][d] |= 0x10;
            if (d & 2) edatlookup[c][d] |= 0x20;
        }
    }
    if ((kDefaultMonitorType & 0x0f) == 10) {
        for (int c = 0; c < 256; ++c) {
            GCCOLOR color = ega_mono_color((uint8_t)c);
            pallook64[c] = color;
            pallook16[c] = color;
        }
    } else {
        for (int c = 0; c < 256; ++c) {
            pallook64[c] = makecol(((c >> 2) & 1) * 0xaa + ((c >> 5) & 1) * 0x55,
                                   ((c >> 1) & 1) * 0xaa + ((c >> 4) & 1) * 0x55,
                                   (c & 1) * 0xaa + ((c >> 3) & 1) * 0x55);
            pallook16[c] = makecol(((c >> 2) & 1) * 0xaa + ((c >> 4) & 1) * 0x55,
                                   ((c >> 1) & 1) * 0xaa + ((c >> 4) & 1) * 0x55,
                                   (c & 1) * 0xaa + ((c >> 4) & 1) * 0x55);
            if ((c & 0x17) == 6) {
                pallook16[c] = makecol(0xaa, 0x55, 0);
            }
        }
    }
}

void init()
{
    std::memset(&ega, 0, sizeof(ega));
    std::memset(line_buffer, 0, sizeof(line_buffer));
    std::memset(line_dirty_version, 0, sizeof(line_dirty_version));
    std::memset(line_page_version, 0, sizeof(line_page_version));
    invalidate_line_hashes();
    __atomic_store_n(&line_dirty_next_version, 1u, __ATOMIC_RELEASE);
    handled_timing_recalc_requests = 0;
    __atomic_store_n(&timing_recalc_requests, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&display_content_dirty, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&display_full_render_dirty, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&display_dirty_line_marks, 0u, __ATOMIC_RELEASE);
    build_tables();

    ega.pallook = pallook16;
    ega.vram_limit = kEgaVramSize;
    ega.vrammask = kEgaVramSize - 1u;
    ega.writemask = 0x0f;
    ega.colournocare = 0x0f;
    ega.firstline = 2000;
    ega.dispon = 1;
    egaswitches = kDefaultMonitorType & 0x0f;
    rebuild_egapal(&ega);
    ega_recalctimings(&ega);

    display_firstline = 0;
    display_source_width = 0;
    display_source_height = 0;
    display_vertical_scale = 1;
    display_target_width_px = 0;
    display_target_height_px = 0;
    display_frame_valid = false;
    display_initialized = false;
    display_window_checked_for_frame = false;
    display_render_frame = true;
    display_render_full_frame = true;
    last_gc_width = last_gc_height = 0;
    last_source_width = last_source_height = 0;
    display_usb_pending = false;
    display_pages_ready = false;
    display_content_generation = 1;
    for (unsigned page = 0; page < kDisplayPageCount; ++page) {
        display_page_base[page] = 0;
        display_page_clear_pending[page] = true;
        display_page_content_valid[page] = false;
        display_page_generation[page] = 0;
    }
    display_present_pending = false;
    display_visible_page = 0;
    display_pending_page = kInvalidDisplayPage;
    display_draw_page = 0;
    display_render_full_frame_from_start = true;
    display_draw_page_needs_clone = false;
    xsize = 1;
    ysize = 1;

    printf("ega: PCem EGA I/O 0x%03x-0x%03x, VRAM 0x%05lx-0x%05lx, ROM 0x%05lx-0x%05lx\n",
           kEgaIoBase,
           kEgaIoBase + kEgaIoSize - 1,
           (unsigned long)kEgaMemBase,
           (unsigned long)(kEgaMemBase + kEgaMemSize - 1),
           (unsigned long)kEgaRomBase,
           (unsigned long)(kEgaRomBase + kEgaRomSize - 1));
}

void tick()
{
    PGC pGC = GCDisplay();
    RenderContext ctx = {pGC, false, false};
    RenderContext *pCtx = (pGC && pGC->bitmap.handle && GCWidth(pGC) > 0 && GCHeight(pGC) > 0) ? &ctx : nullptr;
    handle_deferred_requests();
    ega_advance_state(pCtx);

    if (ctx.access_open) {
        GCPEndAccess(pGC);
        ctx.access_open = false;
    }
    present_pending_display_frame(pGC);
}

IoTrap io_traps[] = {
    {kEgaIoBase, kEgaIoSize, true, ega_io_read, ega_io_write},
};

MemTrap mem_traps[] = {
    {kEgaMemBase, kEgaMemSize, true, ega_mem_read, ega_mem_write, ega_vram_active},
    {kEgaRomBase, kEgaRomSize, true, ega_rom_read, nullptr, nullptr},
};

const Module module = {
    "ega-displaylink",
    io_traps,
    sizeof(io_traps) / sizeof(io_traps[0]),
    mem_traps,
    sizeof(mem_traps) / sizeof(mem_traps[0]),
    nullptr,
    0,
    nullptr,
    0,
    init,
    tick,
};

}  // namespace

const Module &ega_module()
{
    return module;
}

}  // namespace picomem
