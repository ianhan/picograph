#include "picomem/module.h"

#include "pcem_mda_rom.h"

#include "hardware/timer.h"
#include "pico/platform/sections.h"
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
#error "The Hercules module requires PICOMEM_ENABLE_DISPLAYLINK and PICOMEM_ENABLE_GC"
#endif

constexpr uint16_t kHercIoBase = 0x03b0;
constexpr uint16_t kHercIoSize = 0x0010;
constexpr uint32_t kHercMemBase = 0x000b0000;
constexpr uint32_t kHercVramSize = 0x10000;
constexpr uint32_t kHercDefaultMappedSize = 0x8000;
constexpr uint16_t kCrtcAddressMask = 0x3fff;

constexpr unsigned kMaxPcemWidth = 1024;
constexpr unsigned kMaxPcemLines = 500;
constexpr unsigned kPackedPixelsPerByte = 4;
constexpr unsigned kBitsPerPackedPixel = 2;
constexpr unsigned kFrameLineBytes = (kMaxPcemWidth + kPackedPixelsPerByte - 1) / kPackedPixelsPerByte;
constexpr unsigned kFrameBufferBytes = kFrameLineBytes * kMaxPcemLines;

constexpr uint32_t kMdaCharClockHz = 2032125u;
constexpr unsigned kTimingFracBits = 16;
constexpr uint64_t kTimingOne = 1ull << kTimingFracBits;
constexpr unsigned kMaxPollStepsPerTick = 2048;
constexpr unsigned kScanlineQueueSize = 1024;
constexpr unsigned kScanlineQueueMask = kScanlineQueueSize - 1u;
constexpr unsigned kDirtyLineQueueSize = 512;
constexpr unsigned kDirtyLineQueueMask = kDirtyLineQueueSize - 1u;
constexpr unsigned kMaxRenderLinesPerTick = 24;
constexpr unsigned kMaxDisplayLinesPerTick = 48;

static_assert((kScanlineQueueSize & kScanlineQueueMask) == 0, "scanline queue size must be a power of two");
static_assert((kDirtyLineQueueSize & kDirtyLineQueueMask) == 0, "dirty line queue size must be a power of two");
static_assert(kDirtyLineQueueSize > kMaxPcemLines, "dirty line queue must hold every display line");

constexpr int DISPLAY_RGB = 0;
constexpr int DISPLAY_RGB_NO_BROWN = 2;
constexpr int DISPLAY_GREEN = 3;
constexpr int DISPLAY_AMBER = 4;
constexpr int DISPLAY_WHITE = 5;

#ifndef PICOMEM_HERCULES_DISPLAY_TYPE
#define PICOMEM_HERCULES_DISPLAY_TYPE DISPLAY_WHITE
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

struct RenderContext {
    PGC pGC;
    bool access_open;
    bool emitted;
};

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
uint8_t frame_buffer[kMaxPcemLines][kFrameLineBytes];
uint8_t fontdatm[2048][16];
uint8_t fontdat[2048][8];

uint32_t line_hash[kMaxPcemLines];
uint16_t line_width[kMaxPcemLines];
uint8_t line_dirty[kMaxPcemLines];
uint16_t line_dirty_min[kMaxPcemLines];
uint16_t line_dirty_max[kMaxPcemLines];
volatile uint32_t frame_invalidate_requests;
uint32_t handled_frame_invalidate_requests;
ScanlineSnapshot scanline_queue[kScanlineQueueSize];
uint32_t scanline_head;
uint32_t scanline_tail;
uint32_t scanline_dropped;
uint32_t handled_scanline_dropped;
uint16_t dirty_line_queue[kDirtyLineQueueSize];
uint32_t dirty_line_head;
uint32_t dirty_line_tail;

bool display_initialized;
long last_gc_width;
long last_gc_height;
unsigned last_source_width;
unsigned last_source_height;
long origin_x;
long origin_y;
long dest_width;
long dest_height;
int display_firstline;
unsigned display_source_width;
unsigned display_source_height;
bool display_frame_valid;

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

uint32_t fnv1a(const uint8_t *data, size_t length)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

uint8_t get_frame_code(unsigned line, unsigned x)
{
    uint8_t packed = frame_buffer[line][x / kPackedPixelsPerByte];
    unsigned shift = (x % kPackedPixelsPerByte) * kBitsPerPackedPixel;
    return (packed >> shift) & 0x3u;
}

void clear_dirty_line_queue()
{
    dirty_line_tail = dirty_line_head;
}

void queue_dirty_line(unsigned line)
{
    if ((dirty_line_head - dirty_line_tail) >= kDirtyLineQueueSize) {
        return;
    }
    dirty_line_queue[dirty_line_head & kDirtyLineQueueMask] = (uint16_t)line;
    dirty_line_head++;
}

void mark_frame_line_dirty_range(unsigned line, unsigned x0, unsigned x1)
{
    if (line >= kMaxPcemLines || x0 >= x1) {
        return;
    }

    x0 = std::min<unsigned>(x0, kMaxPcemWidth);
    x1 = std::min<unsigned>(x1, kMaxPcemWidth);
    if (x0 >= x1) {
        return;
    }

    if (!line_dirty[line]) {
        line_dirty[line] = 1;
        line_dirty_min[line] = (uint16_t)x0;
        line_dirty_max[line] = (uint16_t)x1;
        queue_dirty_line(line);
        return;
    }

    line_dirty_min[line] = (uint16_t)std::min<unsigned>(line_dirty_min[line], x0);
    line_dirty_max[line] = (uint16_t)std::max<unsigned>(line_dirty_max[line], x1);
}

bool frame_line_has_visible_pixels(unsigned line)
{
    if (line >= kMaxPcemLines || line_width[line] == 0) {
        return false;
    }

    unsigned bytes = (line_width[line] + kPackedPixelsPerByte - 1u) / kPackedPixelsPerByte;
    for (unsigned i = 0; i < bytes; ++i) {
        if (frame_buffer[line][i] != 0) {
            return true;
        }
    }
    return false;
}

void mark_nonblank_frame_lines_dirty()
{
    for (unsigned i = 0; i < kMaxPcemLines; ++i) {
        if (frame_line_has_visible_pixels(i)) {
            mark_frame_line_dirty_range(i, 0, line_width[i]);
        }
    }
}

void invalidate_frame_lines()
{
    for (unsigned i = 0; i < kMaxPcemLines; ++i) {
        line_hash[i] = 0;
        line_width[i] = 0;
        line_dirty[i] = 0;
        line_dirty_min[i] = 0;
        line_dirty_max[i] = 0;
    }
    clear_dirty_line_queue();
}

void __time_critical_func(request_frame_invalidate)()
{
    __atomic_fetch_add(&frame_invalidate_requests, 1u, __ATOMIC_RELEASE);
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

void clear_scanline_queue()
{
    scanline_tail = scanline_head;
}

void handle_frame_invalidation()
{
    uint32_t requests = __atomic_load_n(&frame_invalidate_requests, __ATOMIC_ACQUIRE);
    if (requests == handled_frame_invalidate_requests) {
        return;
    }

    invalidate_frame_lines();
    display_initialized = false;
    display_frame_valid = false;
    clear_scanline_queue();
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

uint8_t encode_frame_color(GCCOLOR color)
{
    if (color == cgapal[0]) {
        return 0;
    }
    if (color == (cgapal[0x7] ^ cgapal[0xf])) {
        return 3;
    }
    if (color == cgapal[0xf]) {
        return 2;
    }
    return 1;
}

GCCOLOR decode_frame_color(uint8_t code)
{
    if (code == 2) {
        return cgapal[0xf];
    }
    if (code == 3) {
        return cgapal[0x7] ^ cgapal[0xf];
    }
    if (code == 1) {
        return cgapal[0x7];
    }
    return cgapal[0];
}

void set_frame_pixel(unsigned line, unsigned x, uint8_t code)
{
    uint8_t &packed = frame_buffer[line][x / kPackedPixelsPerByte];
    unsigned shift = (x % kPackedPixelsPerByte) * kBitsPerPackedPixel;
    packed = (uint8_t)((packed & ~(0x3u << shift)) | ((code & 0x3u) << shift));
}

GCCOLOR get_frame_pixel(unsigned line, unsigned x)
{
    uint8_t packed = frame_buffer[line][x / kPackedPixelsPerByte];
    unsigned shift = (x % kPackedPixelsPerByte) * kBitsPerPackedPixel;
    return decode_frame_color((packed >> shift) & 0x3u);
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

bool layout_changed(PGC pGC, unsigned source_width, unsigned source_height)
{
    return !display_initialized ||
           GCWidth(pGC) != last_gc_width ||
           GCHeight(pGC) != last_gc_height ||
           source_width != last_source_width ||
           source_height != last_source_height;
}

void update_layout(RenderContext *ctx, unsigned source_width, unsigned source_height)
{
    PGC pGC = ctx->pGC;
    long gc_width = GCWidth(pGC);
    long gc_height = GCHeight(pGC);

    dest_width = source_width;
    dest_height = source_height;
    origin_x = 0;
    origin_y = 0;

    last_gc_width = gc_width;
    last_gc_height = gc_height;
    last_source_width = source_width;
    last_source_height = source_height;
    display_initialized = true;

    fill_display(ctx, 0, 0, gc_width, gc_height, cgapal[0]);
    mark_nonblank_frame_lines_dirty();
}

void updatewindowsize(RenderContext *ctx, int width, int height)
{
    if (!ctx || !ctx->pGC || width <= 0 || height <= 0 ||
        GCWidth(ctx->pGC) <= 0 || GCHeight(ctx->pGC) <= 0) {
        return;
    }

    unsigned source_width = std::min<unsigned>((unsigned)width, kMaxPcemWidth);
    unsigned source_height = std::min<unsigned>((unsigned)height, kMaxPcemLines);
    if (layout_changed(ctx->pGC, source_width, source_height)) {
        update_layout(ctx, source_width, source_height);
    }
}

void video_wait_for_buffer()
{
}

void enqueue_scanline_snapshot(hercules_t *h, int scanline)
{
    unsigned columns = h->crtc[1];
    uint16_t ma = h->ma;
    h->ma = (uint16_t)(h->ma + columns);

    if (columns == 0 || h->displine < 0 || h->displine >= (int)kMaxPcemLines) {
        return;
    }

    uint32_t head = scanline_head;
    if (head - scanline_tail >= kScanlineQueueSize) {
        ++scanline_dropped;
        return;
    }

    ScanlineSnapshot &line = scanline_queue[head & kScanlineQueueMask];
    line.ma = ma;
    line.ca = (h->crtc[15] | (h->crtc[14] << 8)) & kCrtcAddressMask;
    line.displine = (uint16_t)h->displine;
    line.sc = (uint8_t)scanline;
    line.columns = (uint8_t)columns;
    line.ctrl = h->ctrl;
    line.ctrl2 = h->ctrl2;
    line.blink = (uint8_t)h->blink;
    line.con = (uint8_t)h->con;
    line.cursoron = (uint8_t)h->cursoron;
    line.graphics = graphics_mode(h);
    scanline_head = head + 1u;
}

void handle_scanline_drops()
{
    if (handled_scanline_dropped == scanline_dropped) {
        return;
    }

    clear_scanline_queue();
    handled_scanline_dropped = scanline_dropped;
}

void store_line_if_changed(unsigned line, unsigned width)
{
    if (line >= kMaxPcemLines || width == 0) {
        return;
    }

    width = std::min<unsigned>(width, kMaxPcemWidth);
    size_t bytes = width * sizeof(GCCOLOR);
    uint32_t hash = fnv1a(reinterpret_cast<const uint8_t *>(line_buffer), bytes);

    if (line_hash[line] == hash && line_width[line] == width) {
        return;
    }

    unsigned old_width = line_width[line];
    unsigned compare_width = std::max<unsigned>(width, old_width);
    unsigned dirty_min = kMaxPcemWidth;
    unsigned dirty_max = 0;

    for (unsigned x = 0; x < compare_width; ++x) {
        uint8_t new_code = 0;
        if (x < width) {
            new_code = encode_frame_color(line_buffer[x]);
        }
        uint8_t old_code = 0;
        if (x < old_width) {
            old_code = get_frame_code(line, x);
        }
        if (new_code != old_code) {
            dirty_min = std::min<unsigned>(dirty_min, x);
            dirty_max = x + 1u;
        }
    }

    std::memset(frame_buffer[line], 0, kFrameLineBytes);
    for (unsigned x = 0; x < width; ++x) {
        set_frame_pixel(line, x, encode_frame_color(line_buffer[x]));
    }
    line_hash[line] = hash;
    line_width[line] = (uint16_t)width;
    if (dirty_min < dirty_max) {
        mark_frame_line_dirty_range(line, dirty_min, dirty_max);
    }
}

void draw_frame_line(RenderContext *ctx, unsigned buffer_line, unsigned screen_line,
                     unsigned source_width, unsigned source_height,
                     unsigned dirty_min, unsigned dirty_max)
{
    if (buffer_line >= kMaxPcemLines ||
        source_width == 0 || source_height == 0 ||
        dest_width <= 0 || dest_height <= 0 ||
        dirty_min >= dirty_max) {
        return;
    }

    unsigned effective_width = std::min<unsigned>(source_width, kMaxPcemWidth);
    if (effective_width == 0 || screen_line >= source_height) {
        return;
    }

    long dy = origin_y + (long)screen_line;
    if (dy < 0 || dy >= GCHeight(ctx->pGC)) {
        return;
    }

    long draw_width = std::min<long>((long)effective_width, GCWidth(ctx->pGC) - origin_x);
    if (draw_width <= 0) {
        return;
    }

    long draw_start = std::min<long>((long)dirty_min, draw_width);
    long draw_end = std::min<long>((long)dirty_max, draw_width);
    if (draw_start >= draw_end) {
        return;
    }

    auto pixel_color = [buffer_line](long x) -> GCCOLOR {
        if (x >= 0 && (unsigned)x < line_width[buffer_line]) {
            return get_frame_pixel(buffer_line, (unsigned)x);
        }
        return cgapal[0];
    };

    long run_start = draw_start;
    GCCOLOR run_color = pixel_color(draw_start);

    for (long dx = draw_start + 1; dx <= draw_end; ++dx) {
        GCCOLOR color = cgapal[0];
        if (dx < draw_end) {
            color = pixel_color(dx);
        }

        if (dx < draw_end && color == run_color) {
            continue;
        }

        fill_display(ctx, origin_x + run_start, dy, dx - run_start, 1, run_color);
        run_start = dx;
        run_color = color;
    }
}

void render_graphics_scanline(const ScanlineSnapshot &line)
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

    store_line_if_changed(line.displine, width);
}

void render_text_scanline(const ScanlineSnapshot &line)
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

    store_line_if_changed(line.displine, width);
}

void render_pending_lines(unsigned max_lines)
{
    unsigned rendered = 0;
    while (scanline_tail != scanline_head && rendered < max_lines) {
        const ScanlineSnapshot line = scanline_queue[scanline_tail & kScanlineQueueMask];
        scanline_tail++;

        if (line.graphics) {
            render_graphics_scanline(line);
        } else {
            render_text_scanline(line);
        }
        rendered++;
    }
}

void blit_dirty_visible_lines(RenderContext *ctx, unsigned max_lines)
{
    if (!ctx || !ctx->pGC || !display_frame_valid ||
        display_source_width == 0 || display_source_height == 0 ||
        GCWidth(ctx->pGC) <= 0 || GCHeight(ctx->pGC) <= 0) {
        return;
    }

    updatewindowsize(ctx, display_source_width, display_source_height);

    unsigned drawn = 0;
    while (drawn < max_lines && dirty_line_tail != dirty_line_head) {
        unsigned buffer_line = dirty_line_queue[dirty_line_tail & kDirtyLineQueueMask];
        dirty_line_tail++;
        if (buffer_line >= kMaxPcemLines || !line_dirty[buffer_line]) {
            continue;
        }

        if (buffer_line < (unsigned)display_firstline) {
            line_dirty[buffer_line] = 0;
            line_dirty_min[buffer_line] = 0;
            line_dirty_max[buffer_line] = 0;
            continue;
        }

        unsigned screen_line = buffer_line - (unsigned)display_firstline;
        if (screen_line >= display_source_height) {
            line_dirty[buffer_line] = 0;
            line_dirty_min[buffer_line] = 0;
            line_dirty_max[buffer_line] = 0;
            continue;
        }

        unsigned dirty_min = line_dirty_min[buffer_line];
        unsigned dirty_max = line_dirty_max[buffer_line];
        line_dirty[buffer_line] = 0;
        line_dirty_min[buffer_line] = 0;
        line_dirty_max[buffer_line] = 0;
        draw_frame_line(ctx,
                        buffer_line,
                        screen_line,
                        display_source_width,
                        display_source_height,
                        dirty_min,
                        dirty_max);
        drawn++;
    }
}

uint32_t hercules_poll(hercules_t *h)
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
            enqueue_scanline_snapshot(h, h->sc);
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

                display_firstline = h->firstline;
                display_source_width = (unsigned)xsize;
                display_source_height = (unsigned)ysize;
                display_frame_valid = true;

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

void hercules_advance_state()
{
    hercules_t *h = &hercules;
    uint32_t now = time_us_32();
    if (!h->timing_started) {
        h->next_poll_us = now;
        h->timing_started = true;
    }

    unsigned steps = 0;
    while ((int32_t)(now - h->next_poll_us) >= 0 && steps < kMaxPollStepsPerTick) {
        h->next_poll_us += hercules_poll(h);
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
}

void init()
{
    std::memset(&hercules, 0, sizeof(hercules));
    std::memset(hercules.vram, 0, sizeof(hercules.vram));
    std::memset(line_buffer, 0, sizeof(line_buffer));
    std::memset(frame_buffer, 0, sizeof(frame_buffer));

    loadfont_mda_from_rom();
    cgapal_rebuild(PICOMEM_HERCULES_DISPLAY_TYPE, 0);
    build_mdacols();
    invalidate_frame_lines();
    handled_frame_invalidate_requests = 0;
    __atomic_store_n(&frame_invalidate_requests, 1u, __ATOMIC_RELEASE);

    hercules.firstline = 1000;
    hercules_recalctimings(&hercules);

    display_initialized = false;
    last_gc_width = 0;
    last_gc_height = 0;
    last_source_width = 0;
    last_source_height = 0;
    origin_x = 0;
    origin_y = 0;
    dest_width = 0;
    dest_height = 0;
    display_firstline = 0;
    display_source_width = 0;
    display_source_height = 0;
    display_frame_valid = false;
    scanline_head = 0;
    scanline_tail = 0;
    scanline_dropped = 0;
    handled_scanline_dropped = 0;
    dirty_line_head = 0;
    dirty_line_tail = 0;
    xsize = 1;
    ysize = 1;
    video_res_x = 0;
    video_res_y = 0;
    video_bpp = 0;
    frames = 0;

    printf("hercules: PCem I/O 0x%03x-0x%03x, VRAM 0x%05lx-0x%05lx, SRAM frame=%u bytes, font=/opt/pico/PCem-ROMs/mda.rom\r\n",
           kHercIoBase,
           kHercIoBase + kHercIoSize - 1,
           (unsigned long)kHercMemBase,
           (unsigned long)(kHercMemBase + kHercVramSize - 1),
           kFrameBufferBytes);
}

void tick()
{
    handle_frame_invalidation();
    handle_scanline_drops();
    hercules_advance_state();
    handle_scanline_drops();
    render_pending_lines(kMaxRenderLinesPerTick);

    if (!display_frame_valid) {
        return;
    }

    PGC pGC = GCDisplay();
    RenderContext ctx = {pGC, false, false};
    if (!pGC || !pGC->bitmap.handle || GCWidth(pGC) <= 0 || GCHeight(pGC) <= 0) {
        return;
    }

    blit_dirty_visible_lines(&ctx, kMaxDisplayLinesPerTick);

    if (ctx.access_open) {
        GCPEndAccess(pGC);
    }
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

}  // namespace picomem
