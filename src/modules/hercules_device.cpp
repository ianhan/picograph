#include "picograph/module.h"

#include "modules/dlodirty.h"
#include "pcem_mda_rom.h"

#include "hardware/timer.h"
#include "pico/platform/sections.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace picograph {
namespace {

#if !PICOGRAPH_ENABLE_DISPLAYLINK || !PICOGRAPH_ENABLE_GC
#error "The Hercules module requires PICOGRAPH_ENABLE_DISPLAYLINK and PICOGRAPH_ENABLE_GC"
#endif

constexpr uint16_t kHercIoBase = 0x03b0;
constexpr uint16_t kHercIoSize = 0x0010;
constexpr uint32_t kHercMemBase = 0x000b0000;
constexpr uint32_t kHercVramSize = 0x10000;
constexpr uint32_t kHercDefaultMappedSize = 0x8000;
constexpr uint16_t kCrtcAddressMask = 0x3fff;

constexpr unsigned kMaxPcemWidth = 1024;
constexpr unsigned kMaxPcemLines = 500;

constexpr uint32_t kMdaCharClockHz = 2032125u;
constexpr unsigned kTimingFracBits = 16;
constexpr uint64_t kTimingOne = 1ull << kTimingFracBits;
constexpr unsigned kMaxPollStepsPerTick = 2048;

constexpr int DISPLAY_RGB = 0;
constexpr int DISPLAY_RGB_NO_BROWN = 2;
constexpr int DISPLAY_GREEN = 3;
constexpr int DISPLAY_AMBER = 4;
constexpr int DISPLAY_WHITE = 5;

#ifndef PICOGRAPH_MONOCHROME_DISPLAY_COLOR
#define PICOGRAPH_MONOCHROME_DISPLAY_COLOR DISPLAY_WHITE
#endif

typedef struct hercules_t {
    uint8_t crtc[32];
    int crtcreg;

    uint8_t ctrl, ctrl2, stat;

    uint64_t dispontime, dispofftime;
    uint32_t timer_frac;
    uint32_t next_poll_us;
    bool timing_started;

    int firstline, lastline;

    int linepos, displine;
    int vc, sc;
    uint16_t ma, maback;
    int con, coff, cursoron;
    int dispon, blink;
    int vsynctime, vadj;

    uint8_t vram[kHercVramSize];
} hercules_t;

using RenderContext = DloDirtyDisplay::RenderContext;

struct ScanlineSnapshot {
    uint16_t ma;
    uint16_t ca;
    uint16_t displine;
    uint8_t sc;
    uint8_t columns;
    uint8_t ctrl;
    uint8_t ctrl2;
    uint8_t blink;
    uint8_t con;
    uint8_t cursoron;
    bool graphics;
};

hercules_t hercules;
GCCOLOR mdacols[256][2][2];
GCCOLOR cgapal[16];
GCCOLOR line_buffer[kMaxPcemWidth];
uint8_t fontdatm[2048][16];
uint8_t fontdat[2048][8];
DloDirtyDisplay display;

volatile uint32_t frame_invalidate_requests;
uint32_t handled_frame_invalidate_requests;

int xsize = 1;
int ysize = 1;
int video_res_x;
int video_res_y;
int video_bpp;
uint32_t frames;

GCCOLOR makecol(uint8_t r, uint8_t g, uint8_t b)
{
    return RGB(r, g, b);
}

void cgapal_rebuild(int display_type, int contrast)
{
    switch (display_type) {
    case DISPLAY_GREEN:
        if (contrast) {
            cgapal[0x0] = makecol(0x00, 0x00, 0x00);
            cgapal[0x1] = makecol(0x00, 0x34, 0x0c);
            cgapal[0x2] = makecol(0x04, 0x5d, 0x14);
            cgapal[0x3] = makecol(0x04, 0x69, 0x18);
            cgapal[0x4] = makecol(0x08, 0xa2, 0x24);
            cgapal[0x5] = makecol(0x08, 0xb2, 0x28);
            cgapal[0x6] = makecol(0x0c, 0xe7, 0x34);
            cgapal[0x7] = makecol(0x0c, 0xf3, 0x38);
            cgapal[0x8] = makecol(0x00, 0x1c, 0x04);
            cgapal[0x9] = makecol(0x04, 0x4d, 0x10);
            cgapal[0xa] = makecol(0x04, 0x7d, 0x1c);
            cgapal[0xb] = makecol(0x04, 0x8e, 0x20);
            cgapal[0xc] = makecol(0x08, 0xc7, 0x2c);
            cgapal[0xd] = makecol(0x08, 0xd7, 0x30);
            cgapal[0xe] = makecol(0x14, 0xff, 0x45);
            cgapal[0xf] = makecol(0x34, 0xff, 0x5d);
        } else {
            cgapal[0x0] = makecol(0x00, 0x00, 0x00);
            cgapal[0x1] = makecol(0x00, 0x34, 0x0c);
            cgapal[0x2] = makecol(0x04, 0x55, 0x14);
            cgapal[0x3] = makecol(0x04, 0x5d, 0x14);
            cgapal[0x4] = makecol(0x04, 0x86, 0x20);
            cgapal[0x5] = makecol(0x04, 0x92, 0x20);
            cgapal[0x6] = makecol(0x08, 0xba, 0x2c);
            cgapal[0x7] = makecol(0x08, 0xc7, 0x2c);
            cgapal[0x8] = makecol(0x04, 0x8a, 0x20);
            cgapal[0x9] = makecol(0x08, 0xa2, 0x24);
            cgapal[0xa] = makecol(0x08, 0xc3, 0x2c);
            cgapal[0xb] = makecol(0x08, 0xcb, 0x30);
            cgapal[0xc] = makecol(0x0c, 0xe7, 0x34);
            cgapal[0xd] = makecol(0x0c, 0xef, 0x38);
            cgapal[0xe] = makecol(0x24, 0xff, 0x51);
            cgapal[0xf] = makecol(0x34, 0xff, 0x5d);
        }
        break;
    case DISPLAY_AMBER:
        if (contrast) {
            cgapal[0x0] = makecol(0x00, 0x00, 0x00);
            cgapal[0x1] = makecol(0x55, 0x14, 0x00);
            cgapal[0x2] = makecol(0x82, 0x2c, 0x00);
            cgapal[0x3] = makecol(0x92, 0x34, 0x00);
            cgapal[0x4] = makecol(0xcf, 0x61, 0x00);
            cgapal[0x5] = makecol(0xdf, 0x6d, 0x00);
            cgapal[0x6] = makecol(0xff, 0x9a, 0x04);
            cgapal[0x7] = makecol(0xff, 0xae, 0x18);
            cgapal[0x8] = makecol(0x2c, 0x08, 0x00);
            cgapal[0x9] = makecol(0x6d, 0x20, 0x00);
            cgapal[0xa] = makecol(0xa6, 0x45, 0x00);
            cgapal[0xb] = makecol(0xba, 0x51, 0x00);
            cgapal[0xc] = makecol(0xef, 0x79, 0x00);
            cgapal[0xd] = makecol(0xfb, 0x86, 0x00);
            cgapal[0xe] = makecol(0xff, 0xcb, 0x28);
            cgapal[0xf] = makecol(0xff, 0xe3, 0x34);
        } else {
            cgapal[0x0] = makecol(0x00, 0x00, 0x00);
            cgapal[0x1] = makecol(0x55, 0x14, 0x00);
            cgapal[0x2] = makecol(0x79, 0x24, 0x00);
            cgapal[0x3] = makecol(0x86, 0x2c, 0x00);
            cgapal[0x4] = makecol(0xae, 0x49, 0x00);
            cgapal[0x5] = makecol(0xbe, 0x55, 0x00);
            cgapal[0x6] = makecol(0xe3, 0x71, 0x00);
            cgapal[0x7] = makecol(0xef, 0x79, 0x00);
            cgapal[0x8] = makecol(0xb2, 0x4d, 0x00);
            cgapal[0x9] = makecol(0xcb, 0x5d, 0x00);
            cgapal[0xa] = makecol(0xeb, 0x79, 0x00);
            cgapal[0xb] = makecol(0xf3, 0x7d, 0x00);
            cgapal[0xc] = makecol(0xff, 0x9e, 0x04);
            cgapal[0xd] = makecol(0xff, 0xaa, 0x10);
            cgapal[0xe] = makecol(0xff, 0xdb, 0x30);
            cgapal[0xf] = makecol(0xff, 0xe3, 0x34);
        }
        break;
    case DISPLAY_WHITE:
        if (contrast) {
            cgapal[0x0] = makecol(0x00, 0x00, 0x00);
            cgapal[0x1] = makecol(0x37, 0x3d, 0x40);
            cgapal[0x2] = makecol(0x55, 0x5c, 0x5f);
            cgapal[0x3] = makecol(0x61, 0x67, 0x6b);
            cgapal[0x4] = makecol(0x8f, 0x95, 0x95);
            cgapal[0x5] = makecol(0x9b, 0xa0, 0x9f);
            cgapal[0x6] = makecol(0xcc, 0xcf, 0xc8);
            cgapal[0x7] = makecol(0xdf, 0xde, 0xd4);
            cgapal[0x8] = makecol(0x24, 0x27, 0x29);
            cgapal[0x9] = makecol(0x42, 0x48, 0x4c);
            cgapal[0xa] = makecol(0x70, 0x76, 0x78);
            cgapal[0xb] = makecol(0x81, 0x87, 0x87);
            cgapal[0xc] = makecol(0xaf, 0xb3, 0xb0);
            cgapal[0xd] = makecol(0xbb, 0xbf, 0xba);
            cgapal[0xe] = makecol(0xef, 0xed, 0xdf);
            cgapal[0xf] = makecol(0xff, 0xfd, 0xed);
        } else {
            cgapal[0x0] = makecol(0x00, 0x00, 0x00);
            cgapal[0x1] = makecol(0x37, 0x3d, 0x40);
            cgapal[0x2] = makecol(0x4a, 0x50, 0x54);
            cgapal[0x3] = makecol(0x55, 0x5c, 0x5f);
            cgapal[0x4] = makecol(0x78, 0x7e, 0x80);
            cgapal[0x5] = makecol(0x81, 0x87, 0x87);
            cgapal[0x6] = makecol(0xa3, 0xa7, 0xa6);
            cgapal[0x7] = makecol(0xaf, 0xb3, 0xb0);
            cgapal[0x8] = makecol(0x7a, 0x81, 0x83);
            cgapal[0x9] = makecol(0x8c, 0x92, 0x92);
            cgapal[0xa] = makecol(0xac, 0xb0, 0xad);
            cgapal[0xb] = makecol(0xb3, 0xb7, 0xb4);
            cgapal[0xc] = makecol(0xd1, 0xd3, 0xcb);
            cgapal[0xd] = makecol(0xd9, 0xdb, 0xd2);
            cgapal[0xe] = makecol(0xf7, 0xf5, 0xe7);
            cgapal[0xf] = makecol(0xff, 0xfd, 0xed);
        }
        break;
    default:
        cgapal[0x0] = makecol(0x00, 0x00, 0x00);
        cgapal[0x1] = makecol(0x00, 0x00, 0xaa);
        cgapal[0x2] = makecol(0x00, 0xaa, 0x00);
        cgapal[0x3] = makecol(0x00, 0xaa, 0xaa);
        cgapal[0x4] = makecol(0xaa, 0x00, 0x00);
        cgapal[0x5] = makecol(0xaa, 0x00, 0xaa);
        cgapal[0x6] = display_type == DISPLAY_RGB_NO_BROWN
            ? makecol(0xaa, 0xaa, 0x00)
            : makecol(0xaa, 0x55, 0x00);
        cgapal[0x7] = makecol(0xaa, 0xaa, 0xaa);
        cgapal[0x8] = makecol(0x55, 0x55, 0x55);
        cgapal[0x9] = makecol(0x55, 0x55, 0xff);
        cgapal[0xa] = makecol(0x55, 0xff, 0x55);
        cgapal[0xb] = makecol(0x55, 0xff, 0xff);
        cgapal[0xc] = makecol(0xff, 0x55, 0x55);
        cgapal[0xd] = makecol(0xff, 0x55, 0xff);
        cgapal[0xe] = makecol(0xff, 0xff, 0x55);
        cgapal[0xf] = makecol(0xff, 0xff, 0xff);
        break;
    }
}

void build_mdacols()
{
    for (int c = 0; c < 256; c++) {
        mdacols[c][0][0] = mdacols[c][1][0] = mdacols[c][1][1] = cgapal[0];
        if (c & 8) {
            mdacols[c][0][1] = cgapal[0xf];
        } else {
            mdacols[c][0][1] = cgapal[0x7];
        }
    }
    mdacols[0x70][0][1] = cgapal[0];
    mdacols[0x70][0][0] = mdacols[0x70][1][0] = mdacols[0x70][1][1] = cgapal[0xf];
    mdacols[0xF0][0][1] = cgapal[0];
    mdacols[0xF0][0][0] = mdacols[0xF0][1][0] = mdacols[0xF0][1][1] = cgapal[0xf];
    mdacols[0x78][0][1] = cgapal[0x7];
    mdacols[0x78][0][0] = mdacols[0x78][1][0] = mdacols[0x78][1][1] = cgapal[0xf];
    mdacols[0xF8][0][1] = cgapal[0x7];
    mdacols[0xF8][0][0] = mdacols[0xF8][1][0] = mdacols[0xF8][1][1] = cgapal[0xf];
    mdacols[0x00][0][1] = mdacols[0x00][1][1] = cgapal[0];
    mdacols[0x08][0][1] = mdacols[0x08][1][1] = cgapal[0];
    mdacols[0x80][0][1] = mdacols[0x80][1][1] = cgapal[0];
    mdacols[0x88][0][1] = mdacols[0x88][1][1] = cgapal[0];
}

uint64_t hercules_time_from_chars(int chars)
{
    if (chars <= 0) {
        return 0;
    }
    return ((uint64_t)chars * 1000000ull * kTimingOne) / kMdaCharClockHz;
}

uint32_t hercules_delay_us(hercules_t *h, uint64_t delay)
{
    uint32_t us = (uint32_t)(delay >> kTimingFracBits);
    uint32_t frac = (uint32_t)(delay & (kTimingOne - 1u));
    uint32_t old_frac = h->timer_frac;
    h->timer_frac = (old_frac + frac) & (uint32_t)(kTimingOne - 1u);
    if (old_frac + frac >= kTimingOne) {
        ++us;
    }
    return us ? us : 1;
}

uint64_t load_dispontime(const hercules_t *h)
{
    return __atomic_load_n(&h->dispontime, __ATOMIC_ACQUIRE);
}

uint64_t load_dispofftime(const hercules_t *h)
{
    return __atomic_load_n(&h->dispofftime, __ATOMIC_ACQUIRE);
}

void hercules_recalctimings(hercules_t *h)
{
    int disptime = h->crtc[0] + 1;
    int dispontime = h->crtc[1];
    int dispofftime = disptime - dispontime;

    __atomic_store_n(&h->dispontime, hercules_time_from_chars(dispontime), __ATOMIC_RELEASE);
    __atomic_store_n(&h->dispofftime, hercules_time_from_chars(dispofftime), __ATOMIC_RELEASE);
}

bool graphics_mode(const hercules_t *h)
{
    return (h->ctrl & 2) && (h->ctrl2 & 1);
}

uint32_t active_mapping_size(const hercules_t *h)
{
    return (h->ctrl2 & 2) ? kHercVramSize : kHercDefaultMappedSize;
}

bool crtc_write_invalidates_frame(uint8_t reg)
{
    switch (reg) {
    case 0:  // horizontal total
    case 1:  // horizontal displayed
    case 4:  // vertical total
    case 5:  // vertical adjust
    case 6:  // vertical displayed
    case 7:  // vertical sync position
    case 8:  // interlace / scanline mode
    case 9:  // maximum scanline
        return true;
    default:
        return false;
    }
}

bool crtc_write_affects_pixels(uint8_t reg)
{
    switch (reg) {
    case 10:  // cursor start / cursor disable
    case 11:  // cursor end
    case 12:  // display start high
    case 13:  // display start low
    case 14:  // cursor address high
    case 15:  // cursor address low
        return true;
    default:
        return false;
    }
}

void __time_critical_func(request_frame_invalidate)()
{
    __atomic_fetch_add(&frame_invalidate_requests, 1u, __ATOMIC_RELEASE);
    display.request_dirty();
}

void __time_critical_func(set_status_bits)(hercules_t *h, uint8_t bits)
{
    __atomic_fetch_or(&h->stat, bits, __ATOMIC_RELEASE);
}

void __time_critical_func(clear_status_bits)(hercules_t *h, uint8_t bits)
{
    __atomic_fetch_and(&h->stat, (uint8_t)~bits, __ATOMIC_RELEASE);
}

uint8_t __time_critical_func(read_pcem_status)(const hercules_t *h)
{
    uint8_t stat = __atomic_load_n(&h->stat, __ATOMIC_ACQUIRE);
    return (stat & 0x0fu) | ((stat & 0x08u) << 4);
}

uint8_t __time_critical_func(read_status)(const hercules_t *h)
{
    return read_pcem_status(h);
}

void handle_frame_invalidation()
{
    uint32_t requests = __atomic_load_n(&frame_invalidate_requests, __ATOMIC_ACQUIRE);
    if (requests == handled_frame_invalidate_requests) {
        return;
    }

    display.invalidate_line_hashes();
    display.mark_pages_clear_pending();
    display.initialized = false;
    display.frame_valid = false;
    handled_frame_invalidate_requests = requests;
}

void loadfont_mda_from_rom()
{
    size_t pos = 0;

    std::memset(fontdatm, 0, sizeof(fontdatm));
    std::memset(fontdat, 0, sizeof(fontdat));

    for (int c = 0; c < 256; c++) {
        for (int d = 0; d < 8; d++) {
            fontdatm[c][d] = kPcemMdaRom[pos++];
        }
    }
    for (int c = 0; c < 256; c++) {
        for (int d = 0; d < 8; d++) {
            fontdatm[c][d + 8] = kPcemMdaRom[pos++];
        }
    }
    for (int c = 0; c < 256; c++) {
        for (int d = 0; d < 8; d++) {
            fontdat[c + 256][d] = kPcemMdaRom[pos++];
        }
    }
    for (int c = 0; c < 256; c++) {
        for (int d = 0; d < 8; d++) {
            fontdat[c][d] = kPcemMdaRom[pos++];
        }
    }
}

uint8_t pcem_fontdatm(uint8_t chr, int sc)
{
    if (sc < 0) {
        return 0;
    }

    const uint8_t *flat = &fontdatm[0][0];
    size_t offset = (size_t)chr * 16u + (size_t)sc;
    if (offset >= sizeof(fontdatm)) {
        return 0;
    }
    return flat[offset];
}

void video_wait_for_buffer()
{
}

bool capture_scanline_snapshot(hercules_t *h, int scanline, ScanlineSnapshot *line)
{
    unsigned columns = h->crtc[1];
    uint16_t ma = h->ma;
    h->ma = (uint16_t)(h->ma + columns);

    if (!line || columns == 0 || h->displine < 0 || h->displine >= (int)kMaxPcemLines) {
        return false;
    }

    line->ma = ma;
    line->ca = (h->crtc[15] | (h->crtc[14] << 8)) & kCrtcAddressMask;
    line->displine = (uint16_t)h->displine;
    line->sc = (uint8_t)scanline;
    line->columns = (uint8_t)columns;
    line->ctrl = h->ctrl;
    line->ctrl2 = h->ctrl2;
    line->blink = (uint8_t)h->blink;
    line->con = (uint8_t)h->con;
    line->cursoron = (uint8_t)h->cursoron;
    line->graphics = graphics_mode(h);
    return true;
}

void draw_rendered_scanline(RenderContext *ctx, const ScanlineSnapshot &line, unsigned width)
{
    if (!ctx || !ctx->pGC || !display.frame_valid ||
        width == 0 || line.displine >= kMaxPcemLines ||
        line.displine < (unsigned)display.first_line ||
        display.source_width == 0 || display.source_height == 0) {
        return;
    }

    width = std::min<unsigned>(width, kMaxPcemWidth);
    unsigned source_line = line.displine - (unsigned)display.first_line;
    if (source_line >= display.source_height) {
        return;
    }

    display.ensure_draw_page_ready_for_partial(ctx);
    display.draw_line(ctx,
                      line.displine,
                      source_line,
                      1,
                      width,
                      display.line_dirty_version(line.displine));
}

void render_graphics_scanline(RenderContext *ctx, const ScanlineSnapshot &line)
{
    unsigned columns = line.columns;
    unsigned visible_columns = std::min<unsigned>(columns, kMaxPcemWidth / 16u);
    unsigned width = std::min<unsigned>(columns * 16u, kMaxPcemWidth);
    if (width == 0 || line.displine >= kMaxPcemLines) {
        return;
    }

    uint16_t ma = line.ma;
    uint32_t ca = (line.sc & 3) * 0x2000u;
    if ((line.ctrl & 0x80) && (line.ctrl2 & 2)) {
        ca += 0x8000u;
    }

    for (unsigned x = 0; x < columns; ++x) {
        uint16_t dat = 0;
        uint32_t base = ((ma << 1) & 0x1fffu) + ca;
        dat = (uint16_t)(__atomic_load_n(&hercules.vram[base & 0xffffu], __ATOMIC_RELAXED) << 8);
        dat |= __atomic_load_n(&hercules.vram[(base + 1u) & 0xffffu], __ATOMIC_RELAXED);
        ma++;

        if (x >= visible_columns) {
            continue;
        }
        for (unsigned c = 0; c < 16; c++) {
            line_buffer[(x << 4) + c] = (dat & (32768u >> c)) ? cgapal[0x7] : cgapal[0];
        }
    }

    draw_rendered_scanline(ctx, line, width);
}

void render_text_scanline(RenderContext *ctx, const ScanlineSnapshot &line)
{
    unsigned columns = line.columns;
    unsigned visible_columns = std::min<unsigned>(columns, kMaxPcemWidth / 9u);
    unsigned width = std::min<unsigned>(columns * 9u, kMaxPcemWidth);
    if (width == 0 || line.displine >= kMaxPcemLines) {
        return;
    }

    uint16_t ma = line.ma;
    for (unsigned x = 0; x < columns; x++) {
        uint8_t chr = __atomic_load_n(&hercules.vram[(ma << 1) & 0x0fffu], __ATOMIC_RELAXED);
        uint8_t attr = __atomic_load_n(&hercules.vram[((ma << 1) + 1) & 0x0fffu], __ATOMIC_RELAXED);
        int drawcursor = ((ma == line.ca) && line.con && line.cursoron);
        int blink = ((line.blink & 16) && (line.ctrl & 0x20) && (attr & 0x80) && !drawcursor);
        ma++;

        if (x >= visible_columns) {
            continue;
        }

        if (line.sc == 12 && ((attr & 7) == 1)) {
            for (unsigned c = 0; c < 9; c++) {
                line_buffer[(x * 9) + c] = mdacols[attr][blink][1];
            }
        } else {
            for (unsigned c = 0; c < 8; c++) {
                line_buffer[(x * 9) + c] =
                    mdacols[attr][blink][(pcem_fontdatm(chr, line.sc) & (1 << (c ^ 7))) ? 1 : 0];
            }
            if ((chr & ~0x1f) == 0xc0) {
                line_buffer[(x * 9) + 8] = mdacols[attr][blink][pcem_fontdatm(chr, line.sc) & 1];
            } else {
                line_buffer[(x * 9) + 8] = mdacols[attr][blink][0];
            }
        }

        if (drawcursor) {
            for (unsigned c = 0; c < 9; c++) {
                line_buffer[(x * 9) + c] ^= mdacols[attr][0][1];
            }
        }
    }

    draw_rendered_scanline(ctx, line, width);
}

void render_current_scanline(RenderContext *ctx, hercules_t *h, int scanline)
{
    ScanlineSnapshot line;
    if (!capture_scanline_snapshot(h, scanline, &line) ||
        display.should_skip_line(line.displine) ||
        !ctx || !ctx->pGC) {
        return;
    }

    if (line.graphics) {
        render_graphics_scanline(ctx, line);
    } else {
        render_text_scanline(ctx, line);
    }
}

uint32_t hercules_poll(hercules_t *h, RenderContext *ctx)
{
    int x;
    int oldvc;
    int oldsc;

    if (!h->linepos) {
        set_status_bits(h, 1);
        h->linepos = 1;
        oldsc = h->sc;
        if ((h->crtc[8] & 3) == 3) {
            h->sc = (h->sc << 1) & 7;
        }
        if (h->dispon) {
            if (h->displine < h->firstline) {
                h->firstline = h->displine;
                video_wait_for_buffer();
            }
            h->lastline = h->displine;
            render_current_scanline(ctx, h, h->sc);
        }
        h->sc = oldsc;
        if (h->vc == h->crtc[7] && !h->sc) {
            set_status_bits(h, 8);
        }
        h->displine++;
        if (h->displine >= (int)kMaxPcemLines) {
            h->displine = 0;
        }
        return hercules_delay_us(h, load_dispofftime(h));
    }

    if (h->dispon) {
        clear_status_bits(h, 1);
    }
    h->linepos = 0;
    if (h->vsynctime) {
        h->vsynctime--;
        if (!h->vsynctime) {
            clear_status_bits(h, 8);
        }
    }
    if (h->sc == (h->crtc[11] & 31) ||
        ((h->crtc[8] & 3) == 3 && h->sc == ((h->crtc[11] & 31) >> 1))) {
        h->con = 0;
        h->coff = 1;
    }
    if (h->vadj) {
        h->sc++;
        h->sc &= 31;
        h->ma = h->maback;
        h->vadj--;
        if (!h->vadj) {
            h->dispon = 1;
            h->ma = h->maback = (h->crtc[13] | (h->crtc[12] << 8)) & kCrtcAddressMask;
            h->sc = 0;
        }
    } else if (h->sc == h->crtc[9] ||
               ((h->crtc[8] & 3) == 3 && h->sc == (h->crtc[9] >> 1))) {
        h->maback = h->ma;
        h->sc = 0;
        oldvc = h->vc;
        h->vc++;
        h->vc &= 127;
        if (h->vc == h->crtc[6]) {
            h->dispon = 0;
        }
        if (oldvc == h->crtc[4]) {
            h->vc = 0;
            h->vadj = h->crtc[5];
            if (!h->vadj) {
                h->dispon = 1;
            }
            if (!h->vadj) {
                h->ma = h->maback = (h->crtc[13] | (h->crtc[12] << 8)) & kCrtcAddressMask;
            }
            if ((h->crtc[10] & 0x60) == 0x20) {
                h->cursoron = 0;
            } else {
                h->cursoron = h->blink & 16;
            }
        }
        if (h->vc == h->crtc[7]) {
            h->dispon = 0;
            h->displine = 0;
            h->vsynctime = 16;
            if (h->crtc[7]) {
                if (graphics_mode(h)) {
                    x = h->crtc[1] << 4;
                } else {
                    x = h->crtc[1] * 9;
                }
                h->lastline++;
                if (x != xsize || (h->lastline - h->firstline) != ysize) {
                    xsize = x;
                    ysize = h->lastline - h->firstline;
                    if (xsize < 64) {
                        xsize = 656;
                    }
                    if (ysize < 32) {
                        ysize = 200;
                    }
                }

                if (!graphics_mode(h) && ((h->blink ^ (h->blink + 1)) & 16)) {
                    display.request_dirty();
                }
                display.queue_frame(ctx);
                display.publish_frame(h->firstline,
                                      (unsigned)xsize,
                                      (unsigned)ysize,
                                      1,
                                      PICOGRAPH_DISPLAYLINK_WIDTH,
                                      PICOGRAPH_DISPLAYLINK_HEIGHT);

                frames++;
                if (graphics_mode(h)) {
                    video_res_x = h->crtc[1] * 16;
                    video_res_y = h->crtc[6] * 4;
                    video_bpp = 1;
                } else {
                    video_res_x = h->crtc[1];
                    video_res_y = h->crtc[6];
                    video_bpp = 0;
                }
            }
            h->firstline = 1000;
            h->lastline = 0;
            h->blink++;
        }
    } else {
        h->sc++;
        h->sc &= 31;
        h->ma = h->maback;
    }
    if (h->sc == (h->crtc[10] & 31) ||
        ((h->crtc[8] & 3) == 3 && h->sc == ((h->crtc[10] & 31) >> 1))) {
        h->con = 1;
    }

    return hercules_delay_us(h, load_dispontime(h));
}

void hercules_advance_state(RenderContext *ctx)
{
    hercules_t *h = &hercules;
    uint32_t now = time_us_32();
    if (!h->timing_started) {
        h->next_poll_us = now;
        h->timing_started = true;
    }

    unsigned steps = 0;
    while ((int32_t)(now - h->next_poll_us) >= 0 && steps < kMaxPollStepsPerTick) {
        h->next_poll_us += hercules_poll(h, ctx);
        ++steps;
    }
    if (steps == kMaxPollStepsPerTick && (int32_t)(now - h->next_poll_us) > 0) {
        h->next_poll_us = now + 1;
    }
}

void hercules_out(uint16_t addr, uint8_t val, void *p)
{
    hercules_t *h = (hercules_t *)p;

    switch (addr) {
    case 0x3b0:
    case 0x3b2:
    case 0x3b4:
    case 0x3b6:
        h->crtcreg = val & 31;
        return;
    case 0x3b1:
    case 0x3b3:
    case 0x3b5:
    case 0x3b7:
    {
        uint8_t reg = (uint8_t)(h->crtcreg & 31);
        if (h->crtc[reg] == val) {
            return;
        }
        h->crtc[reg] = val;
        if (h->crtc[10] == 6 &&
            h->crtc[11] == 7) {
            h->crtc[10] = 0xb;
            h->crtc[11] = 0xc;
        }
        if (reg == 0 || reg == 1) {
            hercules_recalctimings(h);
        }
        if (crtc_write_invalidates_frame(reg)) {
            request_frame_invalidate();
        } else if (crtc_write_affects_pixels(reg)) {
            display.request_dirty();
        }
        return;
    }
    case 0x3b8:
        if (h->ctrl == val) {
            return;
        }
        h->ctrl = val;
        request_frame_invalidate();
        return;
    case 0x3bf:
        if (h->ctrl2 == val) {
            return;
        }
        h->ctrl2 = val;
        request_frame_invalidate();
        return;
    }
}

uint8_t hercules_in(uint16_t addr, void *p)
{
    hercules_t *h = (hercules_t *)p;

    switch (addr) {
    case 0x3b0:
    case 0x3b2:
    case 0x3b4:
    case 0x3b6:
        return h->crtcreg;
    case 0x3b1:
    case 0x3b3:
    case 0x3b5:
    case 0x3b7:
        return h->crtc[h->crtcreg];
    case 0x3ba:
        return read_status(h);
    }
    return 0xff;
}

void hercules_write(uint32_t addr, uint8_t val, void *p)
{
    hercules_t *h = (hercules_t *)p;
    __atomic_store_n(&h->vram[addr & 0xffffu], val, __ATOMIC_RELAXED);
}

uint8_t hercules_read(uint32_t addr, void *p)
{
    hercules_t *h = (hercules_t *)p;
    return __atomic_load_n(&h->vram[addr & 0xffffu], __ATOMIC_RELAXED);
}

bool __time_critical_func(herc_io_read)(uint16_t port, uint8_t *data)
{
    *data = hercules_in(port, &hercules);
    return true;
}

void __time_critical_func(herc_io_write)(uint16_t port, uint8_t data)
{
    hercules_out(port, data, &hercules);
}

bool __time_critical_func(herc_mem_active)(uint32_t address)
{
    if (address < kHercMemBase || address >= kHercMemBase + kHercVramSize) {
        return false;
    }
    return (address - kHercMemBase) < active_mapping_size(&hercules);
}

bool __time_critical_func(herc_mem_read)(uint32_t address, uint8_t *data)
{
    if (!herc_mem_active(address)) {
        return false;
    }
    *data = hercules_read(address, &hercules);
    return true;
}

void __time_critical_func(herc_mem_write)(uint32_t address, uint8_t data)
{
    if (!herc_mem_active(address)) {
        return;
    }
    hercules_write(address, data, &hercules);
    display.request_dirty();
}

void init()
{
    std::memset(&hercules, 0, sizeof(hercules));
    std::memset(hercules.vram, 0, sizeof(hercules.vram));
    std::memset(line_buffer, 0, sizeof(line_buffer));
    display.reset(kMaxPcemWidth, kMaxPcemLines, line_buffer, "hercules");

    loadfont_mda_from_rom();
    cgapal_rebuild(PICOGRAPH_MONOCHROME_DISPLAY_COLOR, 0);
    build_mdacols();
    handled_frame_invalidate_requests = 0;
    __atomic_store_n(&frame_invalidate_requests, 1u, __ATOMIC_RELEASE);

    hercules.firstline = 1000;
    hercules_recalctimings(&hercules);

    xsize = 1;
    ysize = 1;
    video_res_x = 0;
    video_res_y = 0;
    video_bpp = 0;
    frames = 0;

    printf("hercules: PCem I/O 0x%03x-0x%03x, VRAM 0x%05lx-0x%05lx, font=/opt/pico/PCem-ROMs/mda.rom\r\n",
           kHercIoBase,
           kHercIoBase + kHercIoSize - 1,
           (unsigned long)kHercMemBase,
           (unsigned long)(kHercMemBase + kHercVramSize - 1));
}

void tick()
{
    PGC pGC = GCDisplay();
    RenderContext ctx = {pGC, false, false};
    RenderContext *pCtx =
        (pGC && pGC->bitmap.handle && GCWidth(pGC) > 0 && GCHeight(pGC) > 0) ? &ctx : nullptr;

    handle_frame_invalidation();
    hercules_advance_state(pCtx);

    if (ctx.access_open) {
        display.end_access(&ctx);
    }
    display.present_pending(pGC);
}

IoTrap io_traps[] = {
    {kHercIoBase, kHercIoSize, true, herc_io_read, herc_io_write},
};

MemTrap mem_traps[] = {
    {kHercMemBase, kHercVramSize, true, herc_mem_read, herc_mem_write, herc_mem_active},
};

const Module module = {
    "hercules-displaylink",
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

const Module &hercules_module()
{
    return module;
}

}  // namespace picograph
