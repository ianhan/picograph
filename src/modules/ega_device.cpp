#include "picograph/module.h"

#include "modules/dlodirty.h"
#include "pcem_ega_bios_rom.h"

#include "hardware/timer.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>


namespace picograph {
namespace {

#if !PICOGRAPH_ENABLE_DISPLAYLINK || !PICOGRAPH_ENABLE_GC
#error "The EGA module requires PICOGRAPH_ENABLE_DISPLAYLINK and PICOGRAPH_ENABLE_GC"
#endif

constexpr uint16_t kEgaIoBase = 0x03a0;
constexpr uint16_t kEgaIoSize = 0x0040;
constexpr uint32_t kEgaMemBase = 0x000a0000;
constexpr uint32_t kEgaMemSize = 0x00020000;
constexpr uint32_t kEgaRomBase = 0x000c0000;
constexpr uint32_t kEgaRomImageSize = sizeof(kPcemEgaBiosRom);
constexpr uint32_t kEgaRomSize = 0x00008000;

#ifndef PICOGRAPH_EGA_MEMORY_KB
#define PICOGRAPH_EGA_MEMORY_KB 256
#endif

#ifndef PICOGRAPH_EGA_MONITOR_TYPE
#define PICOGRAPH_EGA_MONITOR_TYPE 9
#endif

static_assert(PICOGRAPH_EGA_MEMORY_KB == 64 || PICOGRAPH_EGA_MEMORY_KB == 128 || PICOGRAPH_EGA_MEMORY_KB == 256,
              "PICOGRAPH_EGA_MEMORY_KB must be 64, 128, or 256");

constexpr uint32_t kEgaVramSize = (uint32_t)PICOGRAPH_EGA_MEMORY_KB * 1024u;

constexpr unsigned kMaxEgaWidth = 1024;
constexpr unsigned kMaxEgaLines = 512;
constexpr uint32_t kCgaCharClockHz = 1789773u;
constexpr uint32_t kMdaCharClockHz = 2032125u;
constexpr unsigned kTimingFracBits = 16;
constexpr uint64_t kTimingOne = 1ull << kTimingFracBits;
constexpr unsigned kMaxPollStepsPerTick = 2048;
constexpr int kDirtyUnmapped = -1;
constexpr int kDirtyNoVisibleChange = 0;
constexpr int kDirtyMapped = 1;
constexpr unsigned kKeenEgaPageStrideLines = 240;
constexpr unsigned kMaxDeferredDirtyPages = 8;
constexpr unsigned kMaxScrollExposeRegions = 3;
constexpr unsigned kDeferredPageSearchDistance = 1;
constexpr int kMaxPreservedScrollPixels = 64;
constexpr int kMaxPreservedScrollLines = 24;
constexpr bool kEnableRegisterScrollPreserve = false;
constexpr int kDeferredDirtyLineBias = kMaxPreservedScrollLines * 4;
constexpr unsigned kDeferredDirtyLines = kMaxEgaLines + (unsigned)kDeferredDirtyLineBias * 2u;
constexpr unsigned kDeferredDirtyWords = (kDeferredDirtyLines + 31u) / 32u;

constexpr int kDisplayGreen = 3;
constexpr int kDisplayAmber = 4;
constexpr int kDisplayWhite = 5;
constexpr int kDefaultMonitorType = PICOGRAPH_EGA_MONITOR_TYPE;

#ifndef PICOGRAPH_MONOCHROME_DISPLAY_COLOR
#define PICOGRAPH_MONOCHROME_DISPLAY_COLOR kDisplayWhite
#endif

constexpr int kMonochromeDisplayColor = PICOGRAPH_MONOCHROME_DISPLAY_COLOR;

struct ScrollExposeRegion {
    unsigned x;
    unsigned y;
    unsigned width;
    unsigned height;
};

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
    uint16_t latched_display_start;
    uint8_t latched_pelpan;
    bool latched_origin_valid;
    unsigned scroll_expose_count;
    ScrollExposeRegion scroll_exposes[kMaxScrollExposeRegions];
    bool deferred_scroll_publish_origin_valid;
    uint32_t deferred_scroll_publish_origin;
    int deferred_scroll_line_adjust;
    bool deferred_scroll_origin_valid[kMaxDeferredDirtyPages];
    uint32_t deferred_scroll_origin[kMaxDeferredDirtyPages];
    uint32_t deferred_scroll_dirty[kMaxDeferredDirtyPages][kDeferredDirtyWords];

    int firstline, lastline;
    int displine;

    uint32_t vrammask;
    uint32_t vram_limit;

    int video_res_x, video_res_y, video_bpp;
    uint32_t frames;

    uint8_t vram[kEgaVramSize];
};

using RenderContext = DloDirtyDisplay::RenderContext;

Ega ega;
uint8_t ega_rotate[8][256];
uint8_t edatlookup[4][4];
GCCOLOR pallook16[256];
GCCOLOR pallook64[256];
GCCOLOR line_buffer[kMaxEgaWidth];
DloDirtyDisplay display;
volatile uint32_t timing_recalc_requests;
uint32_t handled_timing_recalc_requests;

int egaswitchread;
int egaswitches;
int xsize = 1;
int ysize = 1;

GCCOLOR makecol(uint8_t r, uint8_t g, uint8_t b)
{
    return RGB(r, g, b);
}

GCCOLOR ega_mono_color(uint8_t c)
{
    switch (kMonochromeDisplayColor) {
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

void ega_recalctimings(Ega *e);

void __time_critical_func(request_display_dirty)()
{
    display.request_dirty();
}

void __time_critical_func(mark_display_line_range_dirty)(unsigned first_line, unsigned end_line)
{
    display.mark_line_range_dirty(first_line, end_line);
}

uint32_t display_line_dirty_version(unsigned line)
{
    return display.line_dirty_version(line);
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

uint16_t current_display_start(const Ega *e)
{
    return (uint16_t)((((uint16_t)e->crtc[0x0c] << 8) | e->crtc[0x0d]) & 0xffffu);
}

uint16_t visible_graphics_display_start(const Ega *e)
{
    return e->latched_origin_valid ? e->latched_display_start : current_display_start(e);
}

uint32_t vram_wrapped_base(const Ega *e, uint32_t base)
{
    return e->vram_limit ? (base & e->vrammask) : base;
}

unsigned graphics_pixels_per_crtc_unit(const Ega *e)
{
    return ((e->gdcreg[5] & 0x20) || (e->seqregs[1] & 8)) ? 16u : 8u;
}

unsigned graphics_pixels_per_pelpan_unit(const Ega *e)
{
    return ((e->gdcreg[5] & 0x20) || (e->seqregs[1] & 8)) ? 2u : 1u;
}

uint8_t effective_pelpan(uint8_t value)
{
    return value & 7u;
}

int32_t normalize_wrapped_delta(int32_t delta, int32_t period)
{
    if (period <= 0) {
        return delta;
    }
    while (delta > period / 2) {
        delta -= period;
    }
    while (delta < -period / 2) {
        delta += period;
    }
    return delta;
}

int32_t floor_divide(int32_t value, int32_t divisor)
{
    if (divisor <= 0 || value >= 0) {
        return divisor ? value / divisor : 0;
    }
    return -(((-value) + divisor - 1) / divisor);
}

void clear_scroll_exposes(Ega *e)
{
    e->scroll_expose_count = 0;
}

void clear_deferred_scroll_publish(Ega *e)
{
    e->deferred_scroll_publish_origin_valid = false;
    e->deferred_scroll_publish_origin = 0;
    e->deferred_scroll_line_adjust = 0;
}

void add_scroll_expose(Ega *e, unsigned x, unsigned y, unsigned width, unsigned height)
{
    if (width == 0 || height == 0 || e->scroll_expose_count >= kMaxScrollExposeRegions) {
        return;
    }
    e->scroll_exposes[e->scroll_expose_count++] = {x, y, width, height};
}

bool scroll_exposes_line(const Ega *e, unsigned visible_line)
{
    for (unsigned i = 0; i < e->scroll_expose_count; ++i) {
        const ScrollExposeRegion &region = e->scroll_exposes[i];
        if (visible_line >= region.y && visible_line < region.y + region.height) {
            return true;
        }
    }
    return false;
}

unsigned find_deferred_scroll_slot(const Ega *e, uint32_t origin)
{
    for (unsigned slot = 0; slot < kMaxDeferredDirtyPages; ++slot) {
        if (e->deferred_scroll_origin_valid[slot] &&
            e->deferred_scroll_origin[slot] == origin) {
            return slot;
        }
    }
    return kMaxDeferredDirtyPages;
}

unsigned allocate_deferred_scroll_slot(Ega *e, uint32_t origin)
{
    unsigned slot = find_deferred_scroll_slot(e, origin);
    if (slot < kMaxDeferredDirtyPages) {
        return slot;
    }
    for (slot = 0; slot < kMaxDeferredDirtyPages; ++slot) {
        if (!e->deferred_scroll_origin_valid[slot]) {
            e->deferred_scroll_origin_valid[slot] = true;
            e->deferred_scroll_origin[slot] = origin;
            std::memset(e->deferred_scroll_dirty[slot], 0, sizeof(e->deferred_scroll_dirty[slot]));
            return slot;
        }
    }
    return kMaxDeferredDirtyPages;
}

uint32_t wrapped_delta(uint32_t base, uint32_t origin, uint32_t limit)
{
    if (limit == 0) {
        return base - origin;
    }
    base %= limit;
    origin %= limit;
    return (base >= origin) ? (base - origin) : (limit - origin + base);
}

int32_t signed_wrapped_delta(uint32_t base, uint32_t origin, uint32_t limit)
{
    if (limit == 0) {
        return (int32_t)(base - origin);
    }
    uint32_t forward = wrapped_delta(base, origin, limit);
    if (forward > limit / 2u) {
        return (int32_t)forward - (int32_t)limit;
    }
    return (int32_t)forward;
}

uint32_t wrap_forward(uint32_t value, uint32_t step, uint32_t limit)
{
    value += step;
    return (value >= limit) ? (value - limit) : value;
}

uint32_t wrap_backward(uint32_t value, uint32_t step, uint32_t limit)
{
    return (value >= step) ? (value - step) : (limit - (step - value));
}

uint32_t wrap_add_signed(uint32_t value, int32_t delta, uint32_t limit)
{
    if (limit == 0) {
        return value + (uint32_t)delta;
    }
    int64_t wrapped = (int64_t)(value % limit) + (int64_t)delta;
    wrapped %= (int64_t)limit;
    if (wrapped < 0) {
        wrapped += limit;
    }
    return (uint32_t)wrapped;
}

void mark_deferred_scroll_line_dirty(Ega *e, unsigned slot, int line)
{
    int stored_line = line + kDeferredDirtyLineBias;
    if (slot >= kMaxDeferredDirtyPages || stored_line < 0 ||
        stored_line >= (int)kDeferredDirtyLines) {
        return;
    }
    e->deferred_scroll_dirty[slot][(unsigned)stored_line / 32u] |=
        1u << ((unsigned)stored_line & 31u);
}

bool mark_deferred_scroll_line_range_dirty(Ega *e, uint32_t origin, int first, int end)
{
    if (first >= end) {
        return true;
    }
    unsigned slot = allocate_deferred_scroll_slot(e, origin);
    if (slot >= kMaxDeferredDirtyPages) {
        return false;
    }
    first = std::max(first, -kDeferredDirtyLineBias);
    end = std::min(end, (int)kMaxEgaLines + kDeferredDirtyLineBias);
    for (int line = first; line < end; ++line) {
        mark_deferred_scroll_line_dirty(e, slot, line);
    }
    return true;
}

void clear_deferred_scroll_dirty(Ega *e, unsigned slot)
{
    if (slot >= kMaxDeferredDirtyPages) {
        return;
    }
    std::memset(e->deferred_scroll_dirty[slot], 0, sizeof(e->deferred_scroll_dirty[slot]));
    e->deferred_scroll_origin_valid[slot] = false;
    e->deferred_scroll_origin[slot] = 0;
}

void clear_deferred_scroll_origin_dirty(Ega *e, uint32_t origin)
{
    clear_deferred_scroll_dirty(e, find_deferred_scroll_slot(e, origin));
}

void clear_deferred_scroll_dirty(Ega *e)
{
    std::memset(e->deferred_scroll_dirty, 0, sizeof(e->deferred_scroll_dirty));
    std::memset(e->deferred_scroll_origin_valid, 0, sizeof(e->deferred_scroll_origin_valid));
    std::memset(e->deferred_scroll_origin, 0, sizeof(e->deferred_scroll_origin));
    clear_deferred_scroll_publish(e);
}

bool has_deferred_scroll_dirty(const Ega *e, uint32_t origin)
{
    unsigned slot = find_deferred_scroll_slot(e, origin);
    if (slot >= kMaxDeferredDirtyPages) {
        return false;
    }
    for (uint32_t word : e->deferred_scroll_dirty[slot]) {
        if (word != 0) {
            return true;
        }
    }
    return false;
}

bool publish_deferred_scroll_dirty(Ega *e, uint32_t origin, int line_adjust)
{
    unsigned slot = find_deferred_scroll_slot(e, origin);
    if (slot >= kMaxDeferredDirtyPages) {
        clear_deferred_scroll_publish(e);
        return false;
    }

    unsigned line = 0;
    while (line < kDeferredDirtyLines) {
        while (line < kDeferredDirtyLines &&
               !(e->deferred_scroll_dirty[slot][line / 32u] & (1u << (line & 31u)))) {
            ++line;
        }
        unsigned first = line;
        while (line < kDeferredDirtyLines &&
               (e->deferred_scroll_dirty[slot][line / 32u] & (1u << (line & 31u)))) {
            ++line;
        }
        if (first < line) {
            int adjusted_first = (int)first - kDeferredDirtyLineBias + line_adjust;
            int adjusted_end = (int)line - kDeferredDirtyLineBias + line_adjust;
            if (adjusted_end > 0 && adjusted_first < (int)kMaxEgaLines) {
                unsigned clipped_first = (unsigned)std::max(adjusted_first, 0);
                unsigned clipped_end = (unsigned)std::min(adjusted_end, (int)kMaxEgaLines);
                mark_display_line_range_dirty(clipped_first, clipped_end);
            }
        }
    }
    clear_deferred_scroll_dirty(e, slot);
    clear_deferred_scroll_publish(e);
    return true;
}

bool publish_visible_origin_deferred_dirty(Ega *e, uint32_t origin)
{
    if (!has_deferred_scroll_dirty(e, origin)) {
        return false;
    }
    return publish_deferred_scroll_dirty(e, origin, 0);
}

void prune_deferred_scroll_dirty_for_visible_origin(Ega *e, uint32_t visible_origin)
{
    uint32_t row_bytes = (uint32_t)e->rowoffset << 3;
    uint32_t page_stride_bytes = row_bytes * kKeenEgaPageStrideLines;
    if (row_bytes == 0 || page_stride_bytes == 0 ||
        page_stride_bytes >= e->vram_limit) {
        clear_deferred_scroll_dirty(e);
        return;
    }

    visible_origin = vram_wrapped_base(e, visible_origin);
    uint32_t plus_origin = wrap_forward(visible_origin, page_stride_bytes, e->vram_limit);
    uint32_t minus_origin = wrap_backward(visible_origin, page_stride_bytes, e->vram_limit);

    for (unsigned slot = 0; slot < kMaxDeferredDirtyPages; ++slot) {
        if (!e->deferred_scroll_origin_valid[slot]) {
            continue;
        }
        uint32_t origin = e->deferred_scroll_origin[slot];
        if (origin != plus_origin && origin != minus_origin) {
            clear_deferred_scroll_dirty(e, slot);
        }
    }
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

void put_region_pixel(int x, unsigned width, unsigned region_x, unsigned region_end, GCCOLOR color)
{
    if (x >= 0 && (unsigned)x < width && (unsigned)x >= region_x &&
        (unsigned)x < region_end && (unsigned)x < kMaxEgaWidth) {
        line_buffer[x] = color;
    }
}

bool pixel_group_intersects(int base, int pixels, unsigned region_x, unsigned region_end)
{
    return base < (int)region_end && base + pixels > (int)region_x;
}

uint32_t graphics_line_advance(const Ega *e)
{
    uint32_t step = (e->seqregs[1] & 4) ? 2u : 4u;
    return ((uint32_t)e->hdisp + 1u) * step;
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

void ega_draw_2bpp_region(Ega *e, unsigned width, unsigned region_x, unsigned region_width)
{
    if (region_x >= width || region_width == 0) {
        e->ma = (e->ma + graphics_line_advance(e)) & e->vrammask;
        return;
    }

    unsigned region_end = std::min<unsigned>(width, region_x + region_width);
    std::fill(line_buffer + region_x, line_buffer + region_end, e->active_palette[0]);

    int hscroll = (e->scrollcache & 7) << 1;
    uint32_t step = (e->seqregs[1] & 4) ? 2u : 4u;
    const GCCOLOR *palette = e->active_palette;
    unsigned groups = (unsigned)e->hdisp + 1u;
    for (unsigned x = 0; x < groups; ++x) {
        int base = (int)x * 16 - hscroll;
        if (!pixel_group_intersects(base, 16, region_x, region_end)) {
            e->ma = (e->ma + step) & e->vrammask;
            continue;
        }

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
            put_region_pixel(dst, width, region_x, region_end, color);
            put_region_pixel(dst + 1, width, region_x, region_end, color);
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

void ega_draw_4bpp_region(Ega *e, unsigned width, unsigned region_x, unsigned region_width)
{
    if (region_x >= width || region_width == 0) {
        e->ma = (e->ma + graphics_line_advance(e)) & e->vrammask;
        return;
    }

    unsigned region_end = std::min<unsigned>(width, region_x + region_width);
    std::fill(line_buffer + region_x, line_buffer + region_end, e->active_palette[0]);

    bool lowres = (e->seqregs[1] & 8) != 0;
    int pixels_per_group = lowres ? 16 : 8;
    int hscroll = (e->scrollcache & 7) * (lowres ? 2 : 1);
    uint32_t step = (e->seqregs[1] & 4) ? 2u : 4u;
    const GCCOLOR *palette = e->active_palette;
    uint8_t attr_mask = e->attrregs[0x12];
    unsigned groups = (unsigned)e->hdisp + 1u;
    for (unsigned x = 0; x < groups; ++x) {
        int group_base = (int)x * pixels_per_group - hscroll;
        if (!pixel_group_intersects(group_base, pixels_per_group, region_x, region_end)) {
            e->ma = (e->ma + step) & e->vrammask;
            continue;
        }

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
            put_region_pixel(base, width, region_x, region_end, color0);
            if (lowres) {
                put_region_pixel(base + 1, width, region_x, region_end, color0);
                put_region_pixel(base + 2, width, region_x, region_end, color1);
                put_region_pixel(base + 3, width, region_x, region_end, color1);
            } else {
                put_region_pixel(base + 1, width, region_x, region_end, color1);
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

void render_graphics_line_region(Ega *e, unsigned width, unsigned region_x, unsigned region_width)
{
    if (e->scrblank) {
        unsigned region_end = std::min<unsigned>(width, region_x + region_width);
        if (region_x < region_end) {
            std::fill(line_buffer + region_x, line_buffer + region_end, makecol(0, 0, 0));
        }
    } else if (e->gdcreg[5] & 0x20) {
        ega_draw_2bpp_region(e, width, region_x, region_width);
    } else {
        ega_draw_4bpp_region(e, width, region_x, region_width);
    }
}

bool should_skip_current_line_render(const Ega *e)
{
    if (!e) {
        return false;
    }
    return display.should_skip_line((unsigned)e->displine);
}

void draw_scroll_exposed_regions(RenderContext *ctx,
                                 Ega *e,
                                 unsigned visible_line,
                                 unsigned source_line,
                                 unsigned width)
{
    uint32_t line_ma = e->ma;
    bool advanced_line = false;
    for (unsigned i = 0; i < e->scroll_expose_count; ++i) {
        const ScrollExposeRegion &region = e->scroll_exposes[i];
        if (visible_line < region.y || visible_line >= region.y + region.height) {
            continue;
        }
        e->ma = line_ma;
        render_graphics_line_region(e, width, region.x, region.width);
        advanced_line = true;
        if (!display.draw_line_region(ctx,
                                      (unsigned)e->displine,
                                      source_line,
                                      display.vertical_scale,
                                      width,
                                      region.x,
                                      region.width)) {
            mark_display_line_range_dirty((unsigned)e->displine,
                                          (unsigned)e->displine + 1u);
        }
    }
    if (advanced_line) {
        e->ma = (line_ma + graphics_line_advance(e)) & e->vrammask;
    }
}

void draw_current_line(RenderContext *ctx, Ega *e)
{
    unsigned width = current_line_width(e);
    if (width == 0 || e->displine < 0 || e->displine >= (int)kMaxEgaLines) {
        return;
    }

    bool graphics_mode = (e->gdcreg[6] & 1) != 0;
    if (display.render_frame) {
        display.ensure_draw_page_ready_for_partial(ctx);
    }
    bool skip_full_line = should_skip_current_line_render(e);
    unsigned visible_line = 0;
    bool expose_only_line = false;
    if (skip_full_line && graphics_mode &&
        display.frame_valid &&
        display.vertical_scale != 0 &&
        e->displine >= display.first_line) {
        visible_line = (unsigned)e->displine - (unsigned)display.first_line;
        expose_only_line = scroll_exposes_line(e, visible_line);
    }
    if (skip_full_line && !expose_only_line) {
        advance_current_line_without_render(e);
        return;
    }
    if (display.render_frame) {
        display.ensure_draw_page_ready_for_partial(ctx);
    }

    if (skip_full_line) {
        if (!ctx || !ctx->pGC || !display.frame_valid ||
            e->displine < display.first_line) {
            advance_current_line_without_render(e);
            return;
        }
        unsigned source_line = ((unsigned)e->displine - (unsigned)display.first_line) *
                               display.vertical_scale;
        draw_scroll_exposed_regions(ctx, e, visible_line, source_line, width);
        return;
    }

    uint32_t rendered_version = display_line_dirty_version((unsigned)e->displine);
    if (e->scrblank) {
        std::fill(line_buffer, line_buffer + width, makecol(0, 0, 0));
    } else if (!graphics_mode) {
        ega_draw_text(e);
    } else if (e->gdcreg[5] & 0x20) {
        ega_draw_2bpp(e, width);
    } else {
        ega_draw_4bpp(e, width);
    }

    if (!ctx || !ctx->pGC || !display.frame_valid ||
        e->displine < display.first_line) {
        return;
    }
    unsigned source_line = ((unsigned)e->displine - (unsigned)display.first_line) * display.vertical_scale;
    display.draw_line(ctx,
                      (unsigned)e->displine,
                      source_line,
                      display.vertical_scale,
                      width,
                      rendered_version);
}

void add_scroll_expose_regions(Ega *e,
                               int delta_x_pixels,
                               int delta_y_lines,
                               unsigned width,
                               unsigned visible_lines)
{
    clear_scroll_exposes(e);
    if (width == 0 || visible_lines == 0) {
        return;
    }

    if (delta_y_lines > 0) {
        unsigned exposed = std::min<unsigned>((unsigned)delta_y_lines, visible_lines);
        add_scroll_expose(e, 0, visible_lines - exposed, width, exposed);
    } else if (delta_y_lines < 0) {
        unsigned exposed = std::min<unsigned>((unsigned)(-delta_y_lines), visible_lines);
        add_scroll_expose(e, 0, 0, width, exposed);
    }

    if (delta_x_pixels > 0) {
        unsigned exposed = std::min<unsigned>((unsigned)delta_x_pixels, width);
        add_scroll_expose(e, width - exposed, 0, exposed, visible_lines);
    } else if (delta_x_pixels < 0) {
        unsigned exposed = std::min<unsigned>((unsigned)(-delta_x_pixels), width);
        add_scroll_expose(e, 0, 0, exposed, visible_lines);
    }
}

bool preserve_register_scroll(RenderContext *ctx, Ega *e, uint16_t new_start, uint8_t new_pelpan)
{
    clear_scroll_exposes(e);
    clear_deferred_scroll_publish(e);
    if (!kEnableRegisterScrollPreserve) {
        e->latched_display_start = new_start;
        e->latched_pelpan = new_pelpan;
        e->latched_origin_valid = true;
        return false;
    }

    if (!e->latched_origin_valid) {
        e->latched_display_start = new_start;
        e->latched_pelpan = new_pelpan;
        e->latched_origin_valid = true;
        return true;
    }

    uint16_t old_start = e->latched_display_start;
    uint8_t old_pelpan = e->latched_pelpan;
    e->latched_display_start = new_start;
    e->latched_pelpan = new_pelpan;

    if (old_start == new_start && old_pelpan == new_pelpan) {
        return true;
    }
    if (!(e->gdcreg[6] & 1) || (e->seqregs[1] & 4) || e->rowoffset <= 0) {
        return false;
    }

    unsigned row_units = (unsigned)e->rowoffset * 2u;
    if (row_units == 0) {
        return false;
    }

    int32_t delta_units = (int32_t)new_start - (int32_t)old_start;
    if (delta_units > 32767) {
        delta_units -= 65536;
    } else if (delta_units < -32768) {
        delta_units += 65536;
    }
    int32_t page_stride_units = (int32_t)row_units * (int32_t)kKeenEgaPageStrideLines;
    delta_units = normalize_wrapped_delta(delta_units, page_stride_units);
    int32_t normalized_delta_units = delta_units;

    int32_t delta_y_lines = delta_units / (int32_t)row_units;
    int32_t delta_x_units = delta_units - delta_y_lines * (int32_t)row_units;
    if (delta_x_units > (int32_t)row_units / 2) {
        delta_x_units -= (int32_t)row_units;
        ++delta_y_lines;
    } else if (delta_x_units < -((int32_t)row_units / 2)) {
        delta_x_units += (int32_t)row_units;
        --delta_y_lines;
    }

    int delta_x_pixels =
        (int)delta_x_units * (int)graphics_pixels_per_crtc_unit(e) +
        ((int)effective_pelpan(new_pelpan) - (int)effective_pelpan(old_pelpan)) *
            (int)graphics_pixels_per_pelpan_unit(e);
    unsigned scanlines_per_row = (unsigned)(e->crtc[9] & 31) + 1u;
    int delta_source_lines = (int)delta_y_lines * (int)scanlines_per_row;
    int delta_y_pixels = delta_source_lines * (int)display.vertical_scale;
    uint32_t target_origin = vram_wrapped_base(e, (uint32_t)new_start << 2);
    int32_t delta_bytes = normalized_delta_units * 4;
    e->deferred_scroll_publish_origin =
        wrap_add_signed(target_origin, -delta_bytes, e->vram_limit);
    e->deferred_scroll_line_adjust = -delta_source_lines;
    e->deferred_scroll_publish_origin_valid = true;
    if (delta_x_pixels == 0 && delta_y_lines == 0) {
        return true;
    }
    if (e->split <= e->dispend || display.full_render_dirty_pending() ||
        !display.frame_valid || !display.initialized || !display.pages_ready ||
        display.vertical_scale == 0) {
        return false;
    }

    unsigned visible_lines = display.source_height / display.vertical_scale;
    unsigned width = current_line_width(e);
    if (visible_lines == 0 || width == 0) {
        return false;
    }

    int abs_delta_x = delta_x_pixels < 0 ? -delta_x_pixels : delta_x_pixels;
    int abs_delta_y = delta_source_lines < 0 ? -delta_source_lines : delta_source_lines;
    if (abs_delta_x > kMaxPreservedScrollPixels ||
        abs_delta_y > kMaxPreservedScrollLines ||
        abs_delta_x >= (int)width ||
        abs_delta_y >= (int)visible_lines) {
        return false;
    }

    if (!display.preserve_scroll_from_current(ctx, -delta_x_pixels, -delta_y_pixels)) {
        return false;
    }
    add_scroll_expose_regions(e, delta_x_pixels, delta_source_lines, width, visible_lines);
    return true;
}

void publish_frame(Ega *e)
{
    int height = std::max(0, e->lastline - e->firstline);
    unsigned scale = (e->vres || height <= 200) ? 2u : 1u;
    unsigned width = current_line_width(e);
    unsigned source_height = std::min<unsigned>((unsigned)height * scale, kMaxEgaLines);
    display.publish_frame(e->firstline,
                          width,
                          source_height,
                          scale,
                          PICOGRAPH_DISPLAYLINK_WIDTH,
                          PICOGRAPH_DISPLAYLINK_HEIGHT);

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
        display.queue_frame(ctx);
        publish_frame(e);
        e->firstline = 2000;
        e->lastline = 0;
        if (!(e->gdcreg[6] & 1)) {
            e->maback = e->ma = (current_display_start(e) << 2);
            e->ca = (((uint32_t)e->crtc[0xe] << 8) | e->crtc[0xf]) << 2;
        }
        e->vslines = 0;
    }
    if (e->vc == e->vtotal) {
        uint16_t new_start = current_display_start(e);
        uint8_t new_pelpan = effective_pelpan(e->attrregs[0x13]);
        uint32_t target_deferred_origin = vram_wrapped_base(e, (uint32_t)new_start << 2);
        bool origin_changed =
            e->latched_origin_valid &&
            (e->latched_display_start != new_start || e->latched_pelpan != new_pelpan);
        if (e->gdcreg[6] & 1) {
            if (!e->latched_origin_valid) {
                clear_scroll_exposes(e);
                e->latched_display_start = new_start;
                e->latched_pelpan = new_pelpan;
                e->latched_origin_valid = true;
                publish_visible_origin_deferred_dirty(e, target_deferred_origin);
                prune_deferred_scroll_dirty_for_visible_origin(e, target_deferred_origin);
            } else if (origin_changed) {
                if (preserve_register_scroll(ctx, e, new_start, new_pelpan)) {
                    uint32_t publish_origin = e->deferred_scroll_publish_origin_valid ?
                        e->deferred_scroll_publish_origin : target_deferred_origin;
                    int publish_line_adjust = e->deferred_scroll_publish_origin_valid ?
                        e->deferred_scroll_line_adjust : 0;
                    bool publish_origin_dirty = has_deferred_scroll_dirty(e, publish_origin);
                    bool target_origin_dirty =
                        publish_origin != target_deferred_origin &&
                        has_deferred_scroll_dirty(e, target_deferred_origin);
                    bool published_deferred_dirty = false;
                    if (publish_origin_dirty) {
                        published_deferred_dirty =
                            publish_deferred_scroll_dirty(e, publish_origin, publish_line_adjust);
                    }
                    if (target_origin_dirty) {
                        published_deferred_dirty =
                            publish_deferred_scroll_dirty(e, target_deferred_origin, 0) ||
                            published_deferred_dirty;
                    }
                    if ((publish_origin_dirty || target_origin_dirty) && !published_deferred_dirty) {
                        clear_scroll_exposes(e);
                        clear_deferred_scroll_origin_dirty(e, publish_origin);
                        if (publish_origin != target_deferred_origin) {
                            clear_deferred_scroll_origin_dirty(e, target_deferred_origin);
                        }
                        clear_deferred_scroll_publish(e);
                        display.force_full_render_from_start();
                    }
                } else {
                    clear_scroll_exposes(e);
                    if (e->deferred_scroll_publish_origin_valid) {
                        clear_deferred_scroll_origin_dirty(e, e->deferred_scroll_publish_origin);
                    }
                    clear_deferred_scroll_origin_dirty(e, target_deferred_origin);
                    clear_deferred_scroll_publish(e);
                    display.force_full_render_from_start();
                }
                prune_deferred_scroll_dirty_for_visible_origin(e, target_deferred_origin);
                clear_deferred_scroll_publish(e);
            } else {
                clear_scroll_exposes(e);
                if (publish_visible_origin_deferred_dirty(e, target_deferred_origin)) {
                    prune_deferred_scroll_dirty_for_visible_origin(e, target_deferred_origin);
                }
            }
        } else {
            clear_scroll_exposes(e);
            clear_deferred_scroll_dirty(e);
            e->latched_origin_valid = false;
        }
        e->vc = 0;
        e->sc = e->crtc[8] & 0x1f;
        e->dispon = 1;
        e->displine = 0;
        if (e->gdcreg[6] & 1) {
            e->maback = e->ma = (uint32_t)new_start << 2;
            e->ca = (((uint32_t)e->crtc[0xe] << 8) | e->crtc[0xf]) << 2;
            e->scrollcache = new_pelpan;
        } else {
            e->scrollcache = effective_pelpan(e->attrregs[0x13]);
        }
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
        !display.frame_valid || display.source_height == 0 ||
        display.vertical_scale == 0 || ega.hdisp <= 0 || ega.rowoffset <= 0 ||
        display.first_line < 0 || display.first_line >= (int)kMaxEgaLines ||
        ega.split <= ega.dispend) {
        return kDirtyUnmapped;
    }

    unsigned visible_lines = display.source_height / display.vertical_scale;
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

    unsigned first_line = (unsigned)display.first_line + first_line_offset;
    unsigned end_offset = std::min<unsigned>(first_line_offset + char_height, visible_lines);
    unsigned end_line = (unsigned)display.first_line + end_offset;
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
        !display.frame_valid || display.source_height == 0 ||
        display.vertical_scale == 0 || ega.hdisp <= 0 || ega.rowoffset <= 0 ||
        display.first_line < 0 || display.first_line >= (int)kMaxEgaLines ||
        ega.split <= ega.dispend) {
        return kDirtyUnmapped;
    }

    unsigned visible_lines = display.source_height / display.vertical_scale;
    unsigned scanlines_per_row = (unsigned)(ega.crtc[9] & 31) + 1u;
    if (visible_lines == 0) {
        return kDirtyUnmapped;
    }

    uint32_t base = normalize_vram_address(address, nullptr) & ~3u;
    uint32_t start_base =
        vram_wrapped_base(&ega, (uint32_t)visible_graphics_display_start(&ega) << 2);
    uint32_t row_bytes = (uint32_t)ega.rowoffset << 3;
    uint32_t visible_rows = (visible_lines + scanlines_per_row - 1u) / scanlines_per_row;
    uint32_t page_stride_bytes = row_bytes * kKeenEgaPageStrideLines;
    if (row_bytes == 0 ||
        base >= ega.vram_limit) {
        return kDirtyUnmapped;
    }

    uint32_t visible_bytes = visible_rows * row_bytes;
    int32_t relative = 0;
    bool maps_to_current_page = false;
    bool maps_to_deferred_page = false;
    uint32_t deferred_origin = 0;
    uint32_t current_relative = wrapped_delta(base, start_base, ega.vram_limit);
    if (current_relative < visible_bytes) {
        relative = (int32_t)current_relative;
        maps_to_current_page = true;
    } else if (kEnableRegisterScrollPreserve) {
        if (page_stride_bytes != 0 && page_stride_bytes < ega.vram_limit) {
            int32_t deferred_slack_bytes =
                (int32_t)row_bytes * kMaxPreservedScrollLines;
            int32_t deferred_before = -deferred_slack_bytes;
            int32_t deferred_after = (int32_t)visible_bytes + deferred_slack_bytes;
            uint32_t plus_origin = start_base;
            uint32_t minus_origin = start_base;
            for (unsigned distance = 1; distance <= kDeferredPageSearchDistance; ++distance) {
                plus_origin = wrap_forward(plus_origin, page_stride_bytes, ega.vram_limit);
                int32_t plus_relative = signed_wrapped_delta(base, plus_origin, ega.vram_limit);
                if (plus_relative >= deferred_before && plus_relative < deferred_after) {
                    relative = plus_relative;
                    deferred_origin = plus_origin;
                    maps_to_deferred_page = true;
                    break;
                }

                minus_origin = wrap_backward(minus_origin, page_stride_bytes, ega.vram_limit);
                int32_t minus_relative = signed_wrapped_delta(base, minus_origin, ega.vram_limit);
                if (minus_relative >= deferred_before && minus_relative < deferred_after) {
                    relative = minus_relative;
                    deferred_origin = minus_origin;
                    maps_to_deferred_page = true;
                    break;
                }
            }
        }
    }
    if (!maps_to_current_page && !maps_to_deferred_page) {
        return kDirtyNoVisibleChange;
    }

    int32_t row = floor_divide(relative, (int32_t)row_bytes);
    uint32_t row_offset = (uint32_t)(relative - row * (int32_t)row_bytes);
    uint32_t visible_groups = maps_to_deferred_page ?
        (uint32_t)ega.hdisp + 1u :
        (uint32_t)ega.hdisp + ((ega.scrollcache & 7) ? 1u : 0u);
    if (visible_groups == 0 || row_offset / 4u >= visible_groups) {
        return kDirtyNoVisibleChange;
    }

    int first_line_offset = row * (int32_t)scanlines_per_row;
    if (maps_to_current_page && (first_line_offset < 0 ||
                                 first_line_offset >= (int)visible_lines)) {
        return kDirtyNoVisibleChange;
    }

    if (maps_to_current_page) {
        unsigned first_line = (unsigned)display.first_line + (unsigned)first_line_offset;
        unsigned end_offset =
            std::min<unsigned>((unsigned)first_line_offset + scanlines_per_row, visible_lines);
        unsigned end_line = (unsigned)display.first_line + end_offset;
        if (first_line >= kMaxEgaLines) {
            return kDirtyNoVisibleChange;
        }
        end_line = std::min<unsigned>(end_line, kMaxEgaLines);
        mark_display_line_range_dirty(first_line, end_line);
        return kDirtyMapped;
    }
    int first_line = display.first_line + first_line_offset;
    int end_line = first_line + (int)scanlines_per_row;
    if (end_line <= -kDeferredDirtyLineBias ||
        first_line >= (int)kMaxEgaLines + kDeferredDirtyLineBias) {
        return kDirtyNoVisibleChange;
    }
    if (!mark_deferred_scroll_line_range_dirty(&ega, deferred_origin, first_line, end_line)) {
        return kDirtyUnmapped;
    }
    return kDirtyNoVisibleChange;
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
            bool effective_changed = old != val;
            if (index == 0x13) {
                effective_changed = effective_pelpan(old) != effective_pelpan(val);
            }
            if (effective_changed && (index < 0x10 || index == 0x10 || index == 0x12 ||
                                      index == 0x13 || index == 0x14)) {
                if (!(index == 0x13 && (ega.gdcreg[6] & 1))) {
                    request_display_dirty();
                }
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
            if (!((ega.gdcreg[6] & 1) && (index == 0x0c || index == 0x0d))) {
                request_display_dirty();
            }
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
        bool full_dirty_pending = display.full_render_dirty_pending();
        bool track_changes =
            !full_dirty_pending &&
            !display.content_dirty_pending();
        bool changed = ega_write(address, data, full_dirty_pending || track_changes);
        if (full_dirty_pending) {
            if (changed && (ega.gdcreg[6] & 1)) {
                mark_graphics_vram_write_dirty(address);
            }
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
                display.set_content_dirty();
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
    display.reset(kMaxEgaWidth, kMaxEgaLines, line_buffer, "ega");
    handled_timing_recalc_requests = 0;
    __atomic_store_n(&timing_recalc_requests, 0u, __ATOMIC_RELEASE);
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
        display.end_access(&ctx);
    }
    display.present_pending(pGC);
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

}  // namespace picograph
