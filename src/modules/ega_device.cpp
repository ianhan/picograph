#include "picomem/module.h"

#include "pcem_ega_bios_rom.h"

#include "hardware/timer.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "gcdlo.h"
}

#define DLO_HANDLE(bitmap) ((dlo_dev_t)(uintptr_t)((bitmap)->handle))

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

Ega ega;
uint8_t ega_rotate[8][256];
uint8_t edatlookup[4][4];
GCCOLOR pallook16[256];
GCCOLOR pallook64[256];
GCCOLOR line_buffer[kMaxEgaWidth];
uint32_t line_hash[kMaxEgaLines];
uint16_t line_width[kMaxEgaLines];
volatile uint32_t line_hash_invalidate_requests;
uint32_t handled_line_hash_invalidate_requests;
volatile uint32_t timing_recalc_requests;
uint32_t handled_timing_recalc_requests;

int egaswitchread;
int egaswitches;
int xsize = 1;
int ysize = 1;
int display_firstline;
unsigned display_source_width;
unsigned display_source_height;
unsigned display_vertical_scale = 1;
bool display_frame_valid;
bool display_initialized;
long last_gc_width;
long last_gc_height;
unsigned last_source_width;
unsigned last_source_height;

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

uint32_t fnv1a(const GCCOLOR *data, unsigned width)
{
    uint32_t hash = 2166136261u;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data);
    size_t length = width * sizeof(GCCOLOR);
    for (size_t i = 0; i < length; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

void ega_recalctimings(Ega *e);

void invalidate_line_hashes()
{
    for (unsigned i = 0; i < kMaxEgaLines; ++i) {
        line_hash[i] = 0;
        line_width[i] = 0;
    }
}

void __time_critical_func(request_line_hash_invalidate)()
{
    __atomic_fetch_add(&line_hash_invalidate_requests, 1u, __ATOMIC_RELEASE);
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

    uint32_t requests = __atomic_load_n(&line_hash_invalidate_requests, __ATOMIC_ACQUIRE);
    if (requests == handled_line_hash_invalidate_requests) {
        return;
    }
    invalidate_line_hashes();
    handled_line_hash_invalidate_requests = requests;
}

void begin_access(RenderContext *ctx)
{
    if (!ctx || ctx->access_open || !ctx->pGC) {
        return;
    }
    GCPBeginAccess(ctx->pGC);
    ctx->access_open = true;
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
}

bool layout_changed(PGC pGC, unsigned width, unsigned height)
{
    return !display_initialized ||
           GCWidth(pGC) != last_gc_width ||
           GCHeight(pGC) != last_gc_height ||
           width != last_source_width ||
           height != last_source_height;
}

unsigned display_target_width(PGC pGC)
{
    long gc_width = pGC ? GCWidth(pGC) : 0;
    if (gc_width <= 0 || display_source_width == 0) {
        return 0;
    }
    return std::min<unsigned>(display_source_width, (unsigned)gc_width);
}

unsigned display_target_height(PGC pGC)
{
    long gc_height = pGC ? GCHeight(pGC) : 0;
    if (gc_height <= 0 || display_source_height == 0) {
        return 0;
    }
    return std::min<unsigned>(display_source_height, (unsigned)gc_height);
}

void updatewindowsize(RenderContext *ctx, unsigned width, unsigned height)
{
    if (!ctx || !ctx->pGC || width == 0 || height == 0 ||
        GCWidth(ctx->pGC) <= 0 || GCHeight(ctx->pGC) <= 0) {
        return;
    }

    width = std::min<unsigned>(width, kMaxEgaWidth);
    height = std::min<unsigned>(height, kMaxEgaLines);
    if (!layout_changed(ctx->pGC, width, height)) {
        return;
    }

    last_gc_width = GCWidth(ctx->pGC);
    last_gc_height = GCHeight(ctx->pGC);
    last_source_width = width;
    last_source_height = height;
    display_initialized = true;
    invalidate_line_hashes();
    fill_display(ctx, 0, 0, last_gc_width, last_gc_height, makecol(0, 0, 0));
}

void draw_line(RenderContext *ctx, unsigned buffer_line, unsigned source_line, unsigned source_span, unsigned width)
{
    if (!ctx || !ctx->pGC || !display_frame_valid ||
        buffer_line >= kMaxEgaLines || width == 0 ||
        source_line >= display_source_height) {
        return;
    }

    width = std::min<unsigned>(width, kMaxEgaWidth);
    updatewindowsize(ctx, display_source_width, display_source_height);
    unsigned target_width = display_target_width(ctx->pGC);
    unsigned target_height = display_target_height(ctx->pGC);
    if (target_width == 0 || target_height == 0) {
        return;
    }

    uint32_t hash = fnv1a(line_buffer, width);
    if (line_hash[buffer_line] == hash && line_width[buffer_line] == width) {
        return;
    }

    line_hash[buffer_line] = hash;
    line_width[buffer_line] = (uint16_t)width;

    unsigned target_y0 = (source_line * target_height) / display_source_height;
    unsigned source_end = std::min<unsigned>(source_line + std::max(1u, source_span), display_source_height);
    unsigned target_y1 = (source_end * target_height) / display_source_height;
    if (target_y1 <= target_y0) {
        return;
    }
    target_y1 = std::min<unsigned>(target_y1, target_height);
    if (target_y0 >= target_y1) {
        return;
    }

    long run_start = 0;
    GCCOLOR run_color = line_buffer[0];
    for (unsigned x = 1; x <= target_width; ++x) {
        unsigned sample_x = (x < target_width) ? ((uint64_t)x * width) / target_width : 0;
        GCCOLOR color = (x < target_width) ? line_buffer[sample_x] : 0;
        if (x < target_width && color == run_color) {
            continue;
        }
        fill_display(ctx,
                     run_start,
                     target_y0,
                     (long)x - run_start,
                     target_y1 - target_y0,
                     run_color);
        run_start = x;
        run_color = color;
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

void __time_critical_func(rebuild_egapal)(Ega *e)
{
    for (int c = 0; c < 16; ++c) {
        if (e->attrregs[0x10] & 0x80) {
            e->egapal[c] = (e->attrregs[c] & 0x0f) | ((e->attrregs[0x14] & 0x0f) << 4);
        } else {
            e->egapal[c] = (e->attrregs[c] & 0x3f) | ((e->attrregs[0x14] & 0x0c) << 4);
        }
    }
}

uint8_t __time_critical_func(vram_load)(uint32_t addr)
{
    return __atomic_load_n(&ega.vram[addr & ega.vrammask], __ATOMIC_RELAXED);
}

void __time_critical_func(vram_store)(uint32_t addr, uint8_t val)
{
    __atomic_store_n(&ega.vram[addr & ega.vrammask], val, __ATOMIC_RELAXED);
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
    unsigned width = current_line_width(e);
    std::fill(line_buffer, line_buffer + width, e->pallook[e->egapal[0]]);

    uint32_t ma = e->ma;
    for (int x = 0; x < e->hdisp; ++x) {
        int drawcursor = ((ma == e->ca) && e->con && e->cursoron);
        uint8_t chr = vram_load((ma << 1) & e->vrammask);
        uint8_t attr = vram_load(((ma << 1) + 1) & e->vrammask);
        uint32_t charaddr = (attr & 8) ? e->charsetb + (chr * 128u) : e->charseta + (chr * 128u);
        GCCOLOR fg;
        GCCOLOR bg;

        if (drawcursor) {
            bg = e->pallook[e->egapal[attr & 15]];
            fg = e->pallook[e->egapal[attr >> 4]];
        } else {
            fg = e->pallook[e->egapal[attr & 15]];
            bg = e->pallook[e->egapal[attr >> 4]];
            if ((attr & 0x80) && (e->attrregs[0x10] & 8)) {
                bg = e->pallook[e->egapal[(attr >> 4) & 7]];
                if (e->blink & 16) {
                    fg = bg;
                }
            }
        }

        uint8_t dat = vram_load(charaddr + ((uint32_t)e->sc << 2));
        unsigned base = (unsigned)x * cw;
        unsigned scale = (e->seqregs[1] & 8) ? 2u : 1u;
        bool nine_dot = !(e->seqregs[1] & 1);
        for (unsigned bit = 0; bit < 8; ++bit) {
            GCCOLOR color = (dat & (0x80u >> bit)) ? fg : bg;
            for (unsigned s = 0; s < scale; ++s) {
                put_pixel(base + bit * scale + s, color);
            }
        }
        if (nine_dot) {
            GCCOLOR color = (((chr & ~0x1f) == 0xc0) && (e->attrregs[0x10] & 4) && (dat & 1)) ? fg : bg;
            for (unsigned s = 0; s < scale; ++s) {
                put_pixel(base + 8 * scale + s, color);
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

void ega_draw_2bpp(Ega *e)
{
    unsigned width = current_line_width(e);
    std::fill(line_buffer, line_buffer + width, e->pallook[e->egapal[0]]);
    int hscroll = (e->scrollcache & 7) << 1;
    for (int x = 0; x <= e->hdisp; ++x) {
        int oddeven;
        uint32_t addr = ega_fetch_addr(e, &oddeven);
        uint8_t ed0 = vram_load(addr);
        uint8_t ed1 = vram_load(addr | 1u);
        e->ma = (e->ma + ((e->seqregs[1] & 4) ? 2u : 4u)) & e->vrammask;
        int base = x * 16 - hscroll;
        for (int pair = 0; pair < 8; ++pair) {
            uint8_t src = (pair < 4) ? ed0 : ed1;
            uint8_t pix = (src >> ((3 - (pair & 3)) * 2)) & 3;
            GCCOLOR color = e->pallook[e->egapal[pix]];
            int dst = base + pair * 2;
            put_visible_pixel(dst, width, color);
            put_visible_pixel(dst + 1, width, color);
        }
    }
}

void ega_draw_4bpp(Ega *e)
{
    unsigned width = current_line_width(e);
    std::fill(line_buffer, line_buffer + width, e->pallook[e->egapal[0]]);
    bool lowres = (e->seqregs[1] & 8) != 0;
    int pixels_per_group = lowres ? 16 : 8;
    int hscroll = (e->scrollcache & 7) * (lowres ? 2 : 1);
    for (int x = 0; x <= e->hdisp; ++x) {
        int oddeven;
        uint32_t addr = ega_fetch_addr(e, &oddeven);
        uint8_t edat[4];
        if (e->seqregs[1] & 4) {
            edat[0] = vram_load(addr | (uint32_t)oddeven);
            edat[2] = vram_load(addr | (uint32_t)oddeven | 2u);
            edat[1] = edat[3] = 0;
            e->ma = (e->ma + 2u) & e->vrammask;
        } else {
            edat[0] = vram_load(addr);
            edat[1] = vram_load(addr | 1u);
            edat[2] = vram_load(addr | 2u);
            edat[3] = vram_load(addr | 3u);
            e->ma = (e->ma + 4u) & e->vrammask;
        }

        for (int group = 0; group < 4; ++group) {
            int shift = 6 - (group * 2);
            uint8_t dat = edatlookup[(edat[0] >> shift) & 3][(edat[1] >> shift) & 3] |
                          (uint8_t)(edatlookup[(edat[2] >> shift) & 3][(edat[3] >> shift) & 3] << 2);
            uint8_t pix0 = (dat >> 4) & e->attrregs[0x12];
            uint8_t pix1 = (dat & 0x0f) & e->attrregs[0x12];
            int base = x * pixels_per_group + group * (lowres ? 4 : 2) - hscroll;
            GCCOLOR color0 = e->pallook[e->egapal[pix0]];
            GCCOLOR color1 = e->pallook[e->egapal[pix1]];
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

void draw_current_line(RenderContext *ctx, Ega *e)
{
    unsigned width = current_line_width(e);
    if (width == 0 || e->displine < 0 || e->displine >= (int)kMaxEgaLines) {
        return;
    }

    if (e->scrblank) {
        std::fill(line_buffer, line_buffer + width, makecol(0, 0, 0));
    } else if (!(e->gdcreg[6] & 1)) {
        ega_draw_text(e);
    } else if (e->gdcreg[5] & 0x20) {
        ega_draw_2bpp(e);
    } else {
        ega_draw_4bpp(e);
    }

    if (!ctx || !ctx->pGC || !display_frame_valid ||
        e->displine < display_firstline) {
        return;
    }
    unsigned source_line = ((unsigned)e->displine - (unsigned)display_firstline) * display_vertical_scale;
    draw_line(ctx, (unsigned)e->displine, source_line, display_vertical_scale, width);
}

void publish_frame(Ega *e)
{
    int height = std::max(0, e->lastline - e->firstline);
    unsigned scale = (e->vres || height <= 200) ? 2u : 1u;
    unsigned width = current_line_width(e);
    unsigned source_height = std::min<unsigned>((unsigned)height * scale, kMaxEgaLines);

    if (width != display_source_width ||
        source_height != display_source_height ||
        e->firstline != display_firstline ||
        scale != display_vertical_scale) {
        invalidate_line_hashes();
    }

    display_firstline = e->firstline;
    display_source_width = width;
    display_source_height = source_height;
    display_vertical_scale = scale;
    display_frame_valid = true;

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
        e->blink++;
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

void __time_critical_func(ega_write)(uint32_t addr, uint8_t val)
{
    uint8_t vala, valb, valc, vald;
    int writemask2 = ega.writemask;

    if (ega.chain2_write) {
        writemask2 &= ~0x0a;
        if (addr & 1) writemask2 <<= 1;
        addr &= ~1u;
        if (addr & 0x4000) addr |= 1;
        addr &= ~0x4000u;
    }

    addr = normalize_vram_address(addr, nullptr);
    if (addr >= ega.vram_limit) {
        return;
    }

    switch (ega.writemode) {
    case 1:
        if (writemask2 & 1) vram_store(addr, ega.la);
        if (writemask2 & 2) vram_store(addr | 1u, ega.lb);
        if (writemask2 & 4) vram_store(addr | 2u, ega.lc);
        if (writemask2 & 8) vram_store(addr | 3u, ega.ld);
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
            if (writemask2 & 1) vram_store(addr, (vala & ega.gdcreg[8]) | (ega.la & ~ega.gdcreg[8]));
            if (writemask2 & 2) vram_store(addr | 1u, (valb & ega.gdcreg[8]) | (ega.lb & ~ega.gdcreg[8]));
            if (writemask2 & 4) vram_store(addr | 2u, (valc & ega.gdcreg[8]) | (ega.lc & ~ega.gdcreg[8]));
            if (writemask2 & 8) vram_store(addr | 3u, (vald & ega.gdcreg[8]) | (ega.ld & ~ega.gdcreg[8]));
            break;
        case 0x08:
            if (writemask2 & 1) vram_store(addr, (vala | ~ega.gdcreg[8]) & ega.la);
            if (writemask2 & 2) vram_store(addr | 1u, (valb | ~ega.gdcreg[8]) & ega.lb);
            if (writemask2 & 4) vram_store(addr | 2u, (valc | ~ega.gdcreg[8]) & ega.lc);
            if (writemask2 & 8) vram_store(addr | 3u, (vald | ~ega.gdcreg[8]) & ega.ld);
            break;
        case 0x10:
            if (writemask2 & 1) vram_store(addr, (vala & ega.gdcreg[8]) | ega.la);
            if (writemask2 & 2) vram_store(addr | 1u, (valb & ega.gdcreg[8]) | ega.lb);
            if (writemask2 & 4) vram_store(addr | 2u, (valc & ega.gdcreg[8]) | ega.lc);
            if (writemask2 & 8) vram_store(addr | 3u, (vald & ega.gdcreg[8]) | ega.ld);
            break;
        case 0x18:
            if (writemask2 & 1) vram_store(addr, (vala & ega.gdcreg[8]) ^ ega.la);
            if (writemask2 & 2) vram_store(addr | 1u, (valb & ega.gdcreg[8]) ^ ega.lb);
            if (writemask2 & 4) vram_store(addr | 2u, (valc & ega.gdcreg[8]) ^ ega.lc);
            if (writemask2 & 8) vram_store(addr | 3u, (vald & ega.gdcreg[8]) ^ ega.ld);
            break;
        }
        break;
    }
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
                request_line_hash_invalidate();
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
        ega.vidclock = val & 4;
        ega.miscout = val;
        if (old_vidclock != ega.vidclock) {
            request_timing_recalc();
        }
        if (old_vres != ega.vres) {
            request_line_hash_invalidate();
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
                request_line_hash_invalidate();
                break;
            case 3:
                request_line_hash_invalidate();
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
        if (old != val && (index == 5 || index == 6)) {
            request_line_hash_invalidate();
        }
        break;
    }
    case 0x3d4:
        ega.crtcreg = val & 31;
        break;
    case 0x3d5: {
        if (ega.crtcreg <= 7 && (ega.crtc[0x11] & 0x80)) {
            return;
        }
        uint8_t old = ega.crtc[ega.crtcreg];
        ega.crtc[ega.crtcreg] = val;
        if (old != val && (ega.crtcreg < 0x0e || ega.crtcreg > 0x10)) {
            request_timing_recalc();
            request_line_hash_invalidate();
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
        ega_write(address, data);
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
    invalidate_line_hashes();
    handled_line_hash_invalidate_requests = 0;
    __atomic_store_n(&line_hash_invalidate_requests, 0u, __ATOMIC_RELEASE);
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

    display_firstline = 0;
    display_source_width = 0;
    display_source_height = 0;
    display_vertical_scale = 1;
    display_frame_valid = false;
    display_initialized = false;
    last_gc_width = last_gc_height = 0;
    last_source_width = last_source_height = 0;
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
    }
    if (ctx.emitted) {
        dlo_flush_usb(DLO_HANDLE(&pGC->bitmap), true);
    }
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

#undef DLO_HANDLE
