#include "picograph/module.h"

#include "framework/scanout_shared.h"
#include "modules/dlodirty.h"
#include "pcem_cl5429_bios_rom.h"

#include "hardware/timer.h"
#include "pico/stdlib.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>


namespace picograph {
namespace {

#if !PICOGRAPH_ENABLE_DISPLAYLINK || !PICOGRAPH_ENABLE_GC
#error "The CL5429 module requires PICOGRAPH_ENABLE_DISPLAYLINK and PICOGRAPH_ENABLE_GC"
#endif

constexpr uint16_t kVgaIoBase = 0x03a0;
constexpr uint16_t kVgaIoSize = 0x0040;
constexpr uint32_t kVgaMemBase = 0x000a0000;
constexpr uint32_t kVgaMemSize = 0x00020000;
constexpr uint32_t kVgaRomBase = 0x000c0000;
constexpr uint32_t kVgaRomImageSize = sizeof(kPcemCl5429BiosRom);
constexpr uint32_t kVgaRomSize = 0x00008000;

#ifndef PICOGRAPH_CL5429_MEMORY_KB
#define PICOGRAPH_CL5429_MEMORY_KB 256
#endif

static_assert(PICOGRAPH_CL5429_MEMORY_KB == 256,
              "PICOGRAPH_CL5429_MEMORY_KB is currently limited to 256 without PSRAM");

constexpr uint32_t kVgaVramSize = (uint32_t)PICOGRAPH_CL5429_MEMORY_KB * 1024u;

constexpr unsigned kMaxVgaWidth = 1024;
constexpr unsigned kMaxVgaLines = 512;
constexpr uint32_t kVgaPixelClock0Hz = 25175000u;
constexpr uint32_t kVgaPixelClock1Hz = 28322000u;
using scanout::kDirtyUnmapped;
using scanout::kDirtyNoVisibleChange;
using scanout::kDirtyMapped;

constexpr uint8_t kClTypeGd5429 = 3;
constexpr uint8_t kClGd5429IsaBus = 7;

constexpr uint8_t kClGrbX8Addressing = 1u << 1;
constexpr uint8_t kClGrbWriteModeExt = 1u << 2;
constexpr uint8_t kClGrb8bLatches = 1u << 3;
constexpr uint8_t kClGrbEnhanced16Bit = 1u << 4;

struct Vga {
    uint8_t crtcreg;
    uint8_t crtc[128];
    uint8_t gdcreg[64];
    uint8_t gdcaddr;
    uint8_t attrregs[32];
    uint8_t attraddr;
    uint8_t attrff;
    uint8_t attr_palette_enable;
    uint8_t seqregs[64];
    uint8_t seqaddr;

    uint8_t miscout;
    uint8_t vidclock;

    uint8_t la, lb, lc, ld;
    uint8_t stat;

    uint8_t colourcompare, colournocare;
    uint8_t readmode, writemode, readplane;
    uint8_t chain2_read, chain2_write, chain4;
    uint8_t packed_chain4;
    uint8_t writemask;
    uint32_t charseta, charsetb;

    uint8_t dac_mask, dac_status;
    uint8_t dac_read, dac_write, dac_pos;
    uint8_t dac_r, dac_g;
    uint8_t plane_mask;
    uint8_t vgapal[256][3];
    uint8_t egapal[16];
    GCCOLOR *pallook;
    GCCOLOR active_palette[16];

    int vtotal, dispend, vsyncstart, split, vblankstart;
    int hdisp, htotal, hdisp_time, rowoffset;
    int lowres, linedbl, rowcount;

    uint64_t dispontime, dispofftime;
    uint32_t timer_frac;
    uint32_t next_poll_us;
    bool timing_started;

    uint8_t scrblank;
    int dispon;

    uint32_t ma, maback, ca;
    int vc, sc;
    int linepos, vslines, linecountff, hsync_divisor;
    int con, cursoron, blink;
    int scrollcache;

    int firstline, lastline;
    int displine;

    uint32_t vrammask;
    uint32_t vram_limit;
    uint32_t bank[2];
    uint32_t banked_mask;
    uint32_t decode_mask;

    uint8_t hidden_dac_reg;
    uint8_t dac_3c6_count;
    uint8_t sr10_read, sr11_read;
    uint8_t latch_ext[4];

    struct {
        uint32_t bg_col, fg_col;
        uint16_t width, height;
        uint16_t dst_pitch, src_pitch;
        uint32_t dst_addr, src_addr;
        uint8_t mask, mode, rop;
        uint32_t dst_addr_backup, src_addr_backup;
        uint16_t width_backup, height_internal;
        int x_count, y_count;
        int depth;
    } blt;

    uint8_t truecolor_bpp;
    uint8_t mmio_enabled;
    uint8_t mmio_vram_overlap;

    int video_res_x, video_res_y, video_bpp;
    uint32_t frames;

    uint8_t vram[kVgaVramSize];
};

using RenderContext = DloDirtyDisplay::RenderContext;

Vga vga;
uint8_t vga_rotate[8][256];
uint8_t edatlookup[4][4];
GCCOLOR pallook256[256];
GCCOLOR line_buffer[kMaxVgaWidth];
DloDirtyDisplay display;
scanout::DeferredTiming deferred_timing;

int xsize = 1;
int ysize = 1;

GCCOLOR makecol(uint8_t r, uint8_t g, uint8_t b)
{
    return RGB(r, g, b);
}





void vga_recalctimings(Vga *e);

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
    scanout::request_timing_recalc(deferred_timing);
}

bool cl_packed_linear(const Vga *e)
{
    return e->chain4 && e->packed_chain4;
}

void cl_recalc_banking(Vga *e)
{
    if (e->gdcreg[0x0b] & 0x20) {
        e->bank[0] = (uint32_t)e->gdcreg[0x09] << 14;
    } else {
        e->bank[0] = (uint32_t)e->gdcreg[0x09] << 12;
    }

    if (e->gdcreg[0x0b] & 0x01) {
        if (e->gdcreg[0x0b] & 0x20) {
            e->bank[1] = (uint32_t)e->gdcreg[0x0a] << 14;
        } else {
            e->bank[1] = (uint32_t)e->gdcreg[0x0a] << 12;
        }
    } else {
        e->bank[1] = e->bank[0] + 0x8000u;
    }
    e->bank[0] &= e->vrammask;
    e->bank[1] &= e->vrammask;
}

void cl_recalc_mapping(Vga *e)
{
    e->banked_mask = ((e->gdcreg[6] & 0x0c) >= 0x08) ? 0x7fffu : 0xffffu;
    e->mmio_vram_overlap = ((e->gdcreg[6] & 0x0c) == 0x0c) ? 1u : 0u;
    e->mmio_enabled = (e->seqregs[0x17] & 0x04) ? 1u : 0u;
}

uint32_t cl_banked_offset(uint32_t address)
{
    uint32_t offset = address & vga.banked_mask;
    return ((offset & 0x7fffu) + vga.bank[(offset >> 15) & 1u]) & vga.vrammask;
}

void handle_deferred_requests()
{
    scanout::handle_deferred_requests(deferred_timing, [] { vga_recalctimings(&vga); });
}

void vga_recalctimings(Vga *e)
{
    e->vtotal = e->crtc[6];
    e->dispend = e->crtc[0x12];
    e->vsyncstart = e->crtc[0x10];
    e->split = e->crtc[0x18];
    e->vblankstart = e->crtc[0x15];

    if (e->crtc[7] & 1) e->vtotal |= 0x100;
    if (e->crtc[7] & 32) e->vtotal |= 0x200;
    e->vtotal += 2;

    if (e->crtc[7] & 2) e->dispend |= 0x100;
    if (e->crtc[7] & 64) e->dispend |= 0x200;
    e->dispend++;

    if (e->crtc[7] & 4) e->vsyncstart |= 0x100;
    if (e->crtc[7] & 128) e->vsyncstart |= 0x200;
    e->vsyncstart++;

    if (e->crtc[7] & 0x10) e->split |= 0x100;
    if (e->crtc[9] & 0x40) e->split |= 0x200;
    e->split++;

    if (e->crtc[7] & 0x08) e->vblankstart |= 0x100;
    if (e->crtc[9] & 0x20) e->vblankstart |= 0x200;
    e->vblankstart++;
    if (e->vblankstart < e->dispend) {
        e->dispend = e->vblankstart;
    }

    e->hdisp = e->crtc[1] + 1;
    e->hdisp_time = e->hdisp;
    e->htotal = e->crtc[0] + 6;
    e->rowoffset = e->crtc[0x13];
    if (e->crtc[0x1b] & 0x10) {
        e->rowoffset |= 0x100;
    }
    if (!e->rowoffset) {
        e->rowoffset = 0x100;
    }
    e->lowres = e->attrregs[0x10] & 0x40;
    e->linedbl = e->crtc[9] & 0x80;
    e->rowcount = e->crtc[9] & 31;

    e->truecolor_bpp = 0;
    if (e->hidden_dac_reg & 0x80) {
        if (e->hidden_dac_reg & 0x40) {
            switch (e->hidden_dac_reg & 0x0f) {
            case 0x00:
                e->truecolor_bpp = 15;
                break;
            case 0x01:
                e->truecolor_bpp = 16;
                break;
            case 0x05:
                e->truecolor_bpp = 24;
                break;
            }
        } else {
            e->truecolor_bpp = 15;
        }
    }

    uint32_t hz = e->vidclock ? kVgaPixelClock1Hz : kVgaPixelClock0Hz;
    int disptime = e->htotal;
    int dispontime = e->hdisp_time;
    if (e->seqregs[1] & 8) {
        disptime *= 2;
        dispontime *= 2;
    }
    int dispofftime = disptime - dispontime;
    unsigned char_width = (e->seqregs[1] & 1) ? 8u : 9u;
    __atomic_store_n(&e->dispontime, scanout::time_from_chars(dispontime, hz, char_width), __ATOMIC_RELEASE);
    __atomic_store_n(&e->dispofftime, scanout::time_from_chars(dispofftime, hz, char_width), __ATOMIC_RELEASE);
}

void __time_critical_func(refresh_active_palette)(Vga *e)
{
    for (int c = 0; c < 16; ++c) {
        e->active_palette[c] = e->pallook[e->egapal[c]];
    }
}

void __time_critical_func(rebuild_egapal)(Vga *e)
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
    return scanout::vram_load(vga, addr);
}

inline void __time_critical_func(vram_store)(uint32_t addr, uint8_t val)
{
    scanout::vram_store(vga, addr, val);
}

inline bool __time_critical_func(vram_store_changed)(uint32_t addr, uint8_t val)
{
    return scanout::vram_store_changed(vga, addr, val);
}

inline void __time_critical_func(vram_store_write)(uint32_t addr, uint8_t val, bool track_changes, bool *changed)
{
    scanout::vram_store_write(vga, addr, val, track_changes, changed);
}

unsigned text_char_width(const Vga *e)
{
    if (e->seqregs[1] & 8) {
        return (e->seqregs[1] & 1) ? 16u : 18u;
    }
    return (e->seqregs[1] & 1) ? 8u : 9u;
}

unsigned current_line_width(const Vga *e)
{
    if (!(e->gdcreg[6] & 1) && !(e->attrregs[0x10] & 1)) {
        return std::min<unsigned>((unsigned)e->hdisp * text_char_width(e), kMaxVgaWidth);
    }
    if ((e->gdcreg[5] & 0x60) == 0x20) {
        return std::min<unsigned>((unsigned)e->hdisp * 16u, kMaxVgaWidth);
    }
    if (e->seqregs[1] & 8) {
        return std::min<unsigned>((unsigned)e->hdisp * 16u, kMaxVgaWidth);
    }
    return std::min<unsigned>((unsigned)e->hdisp * 8u, kMaxVgaWidth);
}

unsigned truecolor_bytes_per_pixel(const Vga *e)
{
    switch (e->truecolor_bpp) {
    case 15:
    case 16:
        return 2;
    case 24:
        return 3;
    default:
        return 1;
    }
}

void put_pixel(unsigned x, GCCOLOR color)
{
    if (x < kMaxVgaWidth) {
        line_buffer[x] = color;
    }
}

void put_visible_pixel(int x, unsigned width, GCCOLOR color)
{
    if (x >= 0 && (unsigned)x < width && (unsigned)x < kMaxVgaWidth) {
        line_buffer[x] = color;
    }
}

uint32_t vga_remap_address(const Vga *e, uint32_t in_addr);

void vga_draw_text(Vga *e)
{
    unsigned cw = text_char_width(e);
    unsigned scale = (e->seqregs[1] & 8) ? 2u : 1u;
    bool nine_dot = !(e->seqregs[1] & 1);
    const GCCOLOR *palette = e->active_palette;

    uint32_t ma = e->ma;
    for (int x = 0; x < e->hdisp; ++x) {
        int drawcursor = ((ma == e->ca) && e->con && e->cursoron);
        uint32_t addr = vga_remap_address(e, ma);
        uint8_t chr = vram_load(addr);
        uint8_t attr = vram_load(addr + 1u);
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
        if (base + cw <= kMaxVgaWidth) {
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

uint32_t vga_remap_address(const Vga *e, uint32_t in_addr)
{
    uint32_t out_addr;
    if (e->crtc[0x14] & 0x40) {
        out_addr = ((in_addr << 2) & 0x3fff0u) | ((in_addr >> 14) & 0x0cu) | (in_addr & ~0x3ffffu);
    } else if (e->crtc[0x17] & 0x40) {
        out_addr = in_addr;
    } else if (e->crtc[0x17] & 0x20) {
        out_addr = ((in_addr << 1) & 0x1fff8u) | ((in_addr >> 15) & 0x04u) | (in_addr & ~0x1ffffu);
    } else {
        out_addr = ((in_addr << 1) & 0x1fff8u) | ((in_addr >> 13) & 0x04u) | (in_addr & ~0x1ffffu);
    }
    if (!(e->crtc[0x17] & 0x01)) {
        out_addr = (out_addr & ~(1u << 15)) | ((e->sc & 1) ? (1u << 15) : 0);
    }
    if (!(e->crtc[0x17] & 0x02)) {
        out_addr = (out_addr & ~(1u << 16)) | ((e->sc & 2) ? (1u << 16) : 0);
    }
    return out_addr & e->vrammask;
}

void vga_draw_2bpp(Vga *e, unsigned width)
{
    int hscroll = (e->scrollcache & 7) << 1;
    constexpr uint32_t step = 4u;
    const GCCOLOR *palette = e->active_palette;
    if (hscroll == 0) {
        unsigned visible_groups = std::min<unsigned>((unsigned)e->hdisp, width / 16u);
        for (unsigned x = 0; x < visible_groups; ++x) {
            uint32_t addr = vga_remap_address(e, e->ma);
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
        uint32_t addr = vga_remap_address(e, e->ma);
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

void vga_draw_4bpp(Vga *e, unsigned width)
{
    bool lowres = (e->seqregs[1] & 8) != 0;
    int pixels_per_group = lowres ? 16 : 8;
    int hscroll = (e->scrollcache & 7) * (lowres ? 2 : 1);
    constexpr uint32_t step = 4u;
    const GCCOLOR *palette = e->active_palette;
    uint8_t attr_mask = e->plane_mask;
    if (hscroll == 0) {
        unsigned visible_groups = std::min<unsigned>((unsigned)e->hdisp, width / (unsigned)pixels_per_group);
        if (lowres) {
            for (unsigned x = 0; x < visible_groups; ++x) {
                uint32_t addr = vga_remap_address(e, e->ma);
                uint8_t edat[4];
                edat[0] = vram_load(addr);
                edat[1] = vram_load(addr | 1u);
                edat[2] = vram_load(addr | 2u);
                edat[3] = vram_load(addr | 3u);
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
                uint32_t addr = vga_remap_address(e, e->ma);
                uint8_t edat[4];
                edat[0] = vram_load(addr);
                edat[1] = vram_load(addr | 1u);
                edat[2] = vram_load(addr | 2u);
                edat[3] = vram_load(addr | 3u);
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
        uint32_t addr = vga_remap_address(e, e->ma);
        uint8_t edat[4];
        edat[0] = vram_load(addr);
        edat[1] = vram_load(addr | 1u);
        edat[2] = vram_load(addr | 2u);
        edat[3] = vram_load(addr | 3u);
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

void vga_draw_8bpp(Vga *e, unsigned width)
{
    const GCCOLOR *palette = e->pallook;
    unsigned out = 0;

    if (e->lowres) {
        int hscroll = e->scrollcache & 6;
        std::fill(line_buffer, line_buffer + width, palette[0]);
        while (out < width + (unsigned)hscroll) {
            uint32_t addr = vga_remap_address(e, e->ma);
            e->ma = (e->ma + 4u) & e->vrammask;
            for (unsigned i = 0; i < 4; ++i) {
                GCCOLOR color = palette[vram_load(addr + i)];
                int dst = (int)out - hscroll;
                put_visible_pixel(dst, width, color);
                put_visible_pixel(dst + 1, width, color);
                out += 2;
            }
        }
        return;
    }

    int hscroll = (e->scrollcache & 6) >> 1;
    std::fill(line_buffer, line_buffer + width, palette[0]);
    while (out < width + (unsigned)hscroll) {
        uint32_t addr = vga_remap_address(e, e->ma);
        e->ma = (e->ma + 4u) & e->vrammask;
        for (unsigned i = 0; i < 4; ++i) {
            int dst = (int)out - hscroll;
            put_visible_pixel(dst, width, palette[vram_load(addr + i)]);
            ++out;
        }
    }
}

void vga_draw_truecolor(Vga *e, unsigned width)
{
    unsigned bytes_per_pixel = truecolor_bytes_per_pixel(e);
    unsigned hscroll = ((unsigned)e->scrollcache & 6u) >> 1;
    unsigned out = 0;
    std::fill(line_buffer, line_buffer + width, makecol(0, 0, 0));
    while (out < width + hscroll) {
        uint32_t addr = vga_remap_address(e, e->ma);
        GCCOLOR color = makecol(0, 0, 0);
        if (e->truecolor_bpp == 15) {
            uint16_t pixel = (uint16_t)vram_load(addr) | ((uint16_t)vram_load(addr + 1u) << 8);
            color = makecol((uint8_t)(((pixel >> 10) & 0x1f) * 255u / 31u),
                            (uint8_t)(((pixel >> 5) & 0x1f) * 255u / 31u),
                            (uint8_t)((pixel & 0x1f) * 255u / 31u));
        } else if (e->truecolor_bpp == 16) {
            uint16_t pixel = (uint16_t)vram_load(addr) | ((uint16_t)vram_load(addr + 1u) << 8);
            color = makecol((uint8_t)(((pixel >> 11) & 0x1f) * 255u / 31u),
                            (uint8_t)(((pixel >> 5) & 0x3f) * 255u / 63u),
                            (uint8_t)((pixel & 0x1f) * 255u / 31u));
        } else if (e->truecolor_bpp == 24) {
            color = makecol(vram_load(addr + 2u), vram_load(addr + 1u), vram_load(addr));
        }
        int dst = (int)out - (int)hscroll;
        put_visible_pixel(dst, width, color);
        e->ma = (e->ma + bytes_per_pixel) & e->vrammask;
        ++out;
    }
}

void advance_current_line_without_render(Vga *e)
{
    if (!e || e->scrblank || e->hdisp <= 0) {
        return;
    }

    if (!(e->gdcreg[6] & 1) && !(e->attrregs[0x10] & 1)) {
        e->ma = (e->ma + (uint32_t)e->hdisp * 4u) & e->vrammask;
    } else if (e->truecolor_bpp) {
        e->ma = (e->ma + current_line_width(e) * truecolor_bytes_per_pixel(e)) & e->vrammask;
    } else if ((e->gdcreg[5] & 0x60) == 0x40 || (e->gdcreg[5] & 0x60) == 0x60) {
        uint32_t bytes = current_line_width(e);
        if (e->lowres) {
            bytes = (bytes + 1u) / 2u;
        }
        e->ma = (e->ma + bytes) & e->vrammask;
    } else {
        e->ma = (e->ma + ((uint32_t)e->hdisp + 1u) * 4u) & e->vrammask;
    }
}

bool should_skip_current_line_render(const Vga *e)
{
    if (!e) {
        return false;
    }
    return display.should_skip_line((unsigned)e->displine);
}

void draw_current_line(RenderContext *ctx, Vga *e)
{
    unsigned width = current_line_width(e);
    if (width == 0 || e->displine < 0 || e->displine >= (int)kMaxVgaLines) {
        return;
    }

    if (display.render_frame) {
        display.ensure_draw_page_ready_for_partial(ctx);
    }
    if (should_skip_current_line_render(e)) {
        advance_current_line_without_render(e);
        return;
    }
    if (display.render_frame) {
        display.ensure_draw_page_ready_for_partial(ctx);
    }

    uint32_t rendered_version = display_line_dirty_version((unsigned)e->displine);
    if (e->scrblank || !e->attr_palette_enable) {
        std::fill(line_buffer, line_buffer + width, makecol(0, 0, 0));
    } else if (!(e->gdcreg[6] & 1) && !(e->attrregs[0x10] & 1)) {
        vga_draw_text(e);
    } else if (e->truecolor_bpp) {
        vga_draw_truecolor(e, width);
    } else if ((e->gdcreg[5] & 0x60) == 0x20) {
        vga_draw_2bpp(e, width);
    } else if (e->gdcreg[5] & 0x40) {
        vga_draw_8bpp(e, width);
    } else {
        vga_draw_4bpp(e, width);
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

void publish_frame(Vga *e)
{
    int height = std::max(0, e->lastline - e->firstline);
    unsigned scale = (height <= 240) ? 2u : 1u;
    unsigned width = current_line_width(e);
    unsigned source_height = std::min<unsigned>((unsigned)height * scale, kMaxVgaLines);
    display.publish_frame(e->firstline,
                          width,
                          source_height,
                          scale,
                          PICOGRAPH_DISPLAYLINK_WIDTH,
                          PICOGRAPH_DISPLAYLINK_HEIGHT);

    e->frames++;
    e->video_res_x = xsize;
    e->video_res_y = ysize + 1;
    if (!(e->gdcreg[6] & 1) && !(e->attrregs[0x10] & 1)) {
        e->video_res_x /= (e->seqregs[1] & 1) ? 8 : 9;
        e->video_res_y /= (e->crtc[9] & 31) + 1;
        e->video_bpp = 0;
    } else {
        if (e->crtc[9] & 0x80) e->video_res_y /= 2;
        if (!(e->crtc[0x17] & 2)) {
            e->video_res_y *= 4;
        } else if (!(e->crtc[0x17] & 1)) {
            e->video_res_y *= 2;
        }
        e->video_res_y /= (e->crtc[9] & 31) + 1;
        if ((e->gdcreg[5] & 0x40) && e->lowres) {
            e->video_res_x /= 2;
        }
        if (e->truecolor_bpp) {
            e->video_bpp = e->truecolor_bpp;
        } else {
        switch (e->gdcreg[5] & 0x60) {
        case 0x20:
            e->video_bpp = 2;
            break;
        case 0x40:
        case 0x60:
            e->video_bpp = 8;
            break;
        default:
            e->video_bpp = 4;
            break;
        }
        }
    }
}

uint32_t vga_poll(Vga *e, RenderContext *ctx)
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
        return scanout::delay_us(e, scanout::load_dispofftime(e));
    }

    if (e->dispon) {
        e->stat &= (uint8_t)~1u;
    }
    e->linepos = 0;
    if (e->sc == (e->crtc[11] & 31)) {
        e->con = 0;
    }
    if (e->dispon) {
        if (e->linedbl && !e->linecountff) {
            e->linecountff = 1;
            e->ma = e->maback;
        } else if (e->sc == e->rowcount) {
            e->linecountff = 0;
            e->sc = 0;
            if (e->sc == (e->crtc[11] & 31)) {
                e->con = 0;
            }
            e->maback = (e->maback + ((uint32_t)e->rowoffset << 3)) & e->vrammask;
            e->ma = e->maback;
        } else {
            e->linecountff = 0;
            e->sc++;
            e->sc &= 31;
            e->ma = e->maback;
        }
    }
    e->vc++;
    e->vc &= 2047;
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
        if (!(e->gdcreg[6] & 1) && !(e->attrregs[0x10] & 1) && old_blink_phase != (e->blink & 16)) {
            request_display_dirty();
        }
    }
    if (e->vc == e->vsyncstart) {
        e->dispon = 0;
        e->stat |= 8;
        x = (int)current_line_width(e);
        if (x != xsize || (e->lastline - e->firstline) != ysize) {
            xsize = x;
            ysize = e->lastline - e->firstline;
            if (xsize < 64) xsize = 640;
            if (ysize < 32) ysize = 200;
        }
        display.queue_frame(ctx);
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
        e->linecountff = 0;
    }
    if (e->sc == (e->crtc[10] & 31)) {
        e->con = 1;
    }
    return scanout::delay_us(e, scanout::load_dispontime(e));
}

void vga_advance_state(RenderContext *ctx)
{
    scanout::advance_state(vga, ctx, vga_poll);
}

uint32_t __time_critical_func(normalize_vram_address)(uint32_t addr, int *readplane_or_mask)
{
    if (addr >= kVgaMemBase) {
        if (addr >= 0xb0000) {
            addr &= 0x7fffu;
        } else {
            addr &= 0xffffu;
        }
    }

    if (vga.gdcreg[0x0b] & kClGrbEnhanced16Bit) {
        addr <<= 4;
    } else if (vga.gdcreg[0x0b] & kClGrbX8Addressing) {
        addr <<= 3;
    } else if (cl_packed_linear(&vga)) {
        if (readplane_or_mask) {
            *readplane_or_mask = 0;
        }
        return addr & vga.decode_mask;
    } else if (vga.chain4) {
        if (readplane_or_mask) {
            *readplane_or_mask = addr & 3u;
        }
        addr = ((addr & 0xfffcu) << 2) | ((addr & 0x30000u) >> 14) | (addr & ~0x3ffffu);
    } else if (vga.chain2_read) {
        if (readplane_or_mask) {
            *readplane_or_mask = (*readplane_or_mask & 2) | (addr & 1);
        }
        addr &= ~1u;
        addr <<= 2;
    } else {
        addr <<= 2;
    }

    return addr & vga.decode_mask;
}

uint32_t __time_critical_func(normalize_vram_write_address)(uint32_t addr, int *writemask2)
{
    if (addr >= kVgaMemBase) {
        if (addr >= 0xb0000) {
            addr &= 0x7fffu;
        } else {
            addr &= 0xffffu;
        }
    }

    if (vga.gdcreg[0x0b] & kClGrbEnhanced16Bit) {
        addr <<= 4;
    } else if (vga.gdcreg[0x0b] & kClGrbX8Addressing) {
        addr <<= 3;
    } else if (cl_packed_linear(&vga) && vga.writemode < 4) {
        *writemask2 = 1 << (addr & 3);
        addr &= ~3u;
    } else if (vga.chain4) {
        *writemask2 = 1 << (addr & 3);
        addr &= ~3u;
        addr = ((addr & 0xfffcu) << 2) | ((addr & 0x30000u) >> 14) | (addr & ~0x3ffffu);
    } else if (vga.chain2_write) {
        *writemask2 &= ~0x0a;
        if (addr & 1) {
            *writemask2 <<= 1;
        }
        addr &= ~1u;
        addr <<= 2;
    } else {
        addr <<= 2;
    }

    return addr & vga.decode_mask;
}

bool __time_critical_func(vga_vram_active)(uint32_t address)
{
    if (vga.mmio_enabled && address >= 0xb8000 && address < 0xb8100) {
        return true;
    }
    switch (vga.gdcreg[6] & 0x0c) {
    case 0x00:
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
    bool mode_inactive = (vga.gdcreg[6] & 1) || (vga.attrregs[0x10] & 1);
    return scanout::mark_text_cell_dirty(vga, display, kMaxVgaLines, mode_inactive, cell);
}

int __time_critical_func(mark_text_vram_write_dirty)(uint32_t address)
{
    return scanout::mark_text_vram_write_dirty(address, [](uint32_t cell) { return mark_text_cell_dirty(cell); });
}

void __time_critical_func(mark_cursor_cells_dirty)(uint32_t old_cell, uint32_t new_cell)
{
    if ((vga.gdcreg[6] & 1) || (vga.attrregs[0x10] & 1) || (vga.crtc[10] & 0x20)) {
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
    if ((vga.gdcreg[6] & 1) || (vga.attrregs[0x10] & 1)) {
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
    case 0x15:
    case 0x18:
    case 0x1a:
    case 0x1b:
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
    case 0x14:
    case 0x17:
    case 0x18:
    case 0x1a:
    case 0x1b:
        return true;
    default:
        return false;
    }
}

int __time_critical_func(mark_graphics_vram_write_dirty)(uint32_t address)
{
    bool mode_inactive = !(vga.gdcreg[6] & 1) || vga.chain4;
    return scanout::mark_graphics_vram_write_dirty(vga, display, kMaxVgaLines, mode_inactive,
                                                   [address] { return normalize_vram_address(address, nullptr); });
}

bool __time_critical_func(vga_write)(uint32_t addr, uint8_t val, bool track_changes)
{
    uint8_t vala, valb, valc, vald;
    int writemask2 = vga.writemask;
    bool changed = false;

    addr = normalize_vram_write_address(addr, &writemask2);
    if (addr >= vga.vram_limit) {
        return false;
    }

    switch (vga.writemode) {
    case 1:
        if (writemask2 & 1) vram_store_write(addr, vga.la, track_changes, &changed);
        if (writemask2 & 2) vram_store_write(addr | 1u, vga.lb, track_changes, &changed);
        if (writemask2 & 4) vram_store_write(addr | 2u, vga.lc, track_changes, &changed);
        if (writemask2 & 8) vram_store_write(addr | 3u, vga.ld, track_changes, &changed);
        break;
    case 0:
        if (vga.gdcreg[3] & 7) {
            val = vga_rotate[vga.gdcreg[3] & 7][val];
        }
        if (vga.gdcreg[1] & 1) vala = (vga.gdcreg[0] & 1) ? 0xff : 0; else vala = val;
        if (vga.gdcreg[1] & 2) valb = (vga.gdcreg[0] & 2) ? 0xff : 0; else valb = val;
        if (vga.gdcreg[1] & 4) valc = (vga.gdcreg[0] & 4) ? 0xff : 0; else valc = val;
        if (vga.gdcreg[1] & 8) vald = (vga.gdcreg[0] & 8) ? 0xff : 0; else vald = val;
        [[fallthrough]];
    case 2:
        if (vga.writemode == 2) {
            vala = (val & 1) ? 0xff : 0;
            valb = (val & 2) ? 0xff : 0;
            valc = (val & 4) ? 0xff : 0;
            vald = (val & 8) ? 0xff : 0;
        }
        switch (vga.gdcreg[3] & 0x18) {
        case 0x00:
            if (writemask2 & 1) vram_store_write(addr, (vala & vga.gdcreg[8]) | (vga.la & ~vga.gdcreg[8]), track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & vga.gdcreg[8]) | (vga.lb & ~vga.gdcreg[8]), track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & vga.gdcreg[8]) | (vga.lc & ~vga.gdcreg[8]), track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & vga.gdcreg[8]) | (vga.ld & ~vga.gdcreg[8]), track_changes, &changed);
            break;
        case 0x08:
            if (writemask2 & 1) vram_store_write(addr, (vala | ~vga.gdcreg[8]) & vga.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb | ~vga.gdcreg[8]) & vga.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc | ~vga.gdcreg[8]) & vga.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald | ~vga.gdcreg[8]) & vga.ld, track_changes, &changed);
            break;
        case 0x10:
            if (writemask2 & 1) vram_store_write(addr, (vala & vga.gdcreg[8]) | vga.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & vga.gdcreg[8]) | vga.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & vga.gdcreg[8]) | vga.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & vga.gdcreg[8]) | vga.ld, track_changes, &changed);
            break;
        case 0x18:
            if (writemask2 & 1) vram_store_write(addr, (vala & vga.gdcreg[8]) ^ vga.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & vga.gdcreg[8]) ^ vga.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & vga.gdcreg[8]) ^ vga.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & vga.gdcreg[8]) ^ vga.ld, track_changes, &changed);
            break;
        }
        break;
    case 3: {
        if (vga.gdcreg[3] & 7) {
            val = vga_rotate[vga.gdcreg[3] & 7][val];
        }
        uint8_t old_mask = vga.gdcreg[8];
        uint8_t bit_mask = old_mask & val;
        vala = (vga.gdcreg[0] & 1) ? 0xff : 0;
        valb = (vga.gdcreg[0] & 2) ? 0xff : 0;
        valc = (vga.gdcreg[0] & 4) ? 0xff : 0;
        vald = (vga.gdcreg[0] & 8) ? 0xff : 0;
        switch (vga.gdcreg[3] & 0x18) {
        case 0x00:
            if (writemask2 & 1) vram_store_write(addr, (vala & bit_mask) | (vga.la & ~bit_mask), track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & bit_mask) | (vga.lb & ~bit_mask), track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & bit_mask) | (vga.lc & ~bit_mask), track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & bit_mask) | (vga.ld & ~bit_mask), track_changes, &changed);
            break;
        case 0x08:
            if (writemask2 & 1) vram_store_write(addr, (vala | ~bit_mask) & vga.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb | ~bit_mask) & vga.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc | ~bit_mask) & vga.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald | ~bit_mask) & vga.ld, track_changes, &changed);
            break;
        case 0x10:
            if (writemask2 & 1) vram_store_write(addr, (vala & bit_mask) | vga.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & bit_mask) | vga.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & bit_mask) | vga.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & bit_mask) | vga.ld, track_changes, &changed);
            break;
        case 0x18:
            if (writemask2 & 1) vram_store_write(addr, (vala & bit_mask) ^ vga.la, track_changes, &changed);
            if (writemask2 & 2) vram_store_write(addr | 1u, (valb & bit_mask) ^ vga.lb, track_changes, &changed);
            if (writemask2 & 4) vram_store_write(addr | 2u, (valc & bit_mask) ^ vga.lc, track_changes, &changed);
            if (writemask2 & 8) vram_store_write(addr | 3u, (vald & bit_mask) ^ vga.ld, track_changes, &changed);
            break;
        }
        vga.gdcreg[8] = old_mask;
        break;
    }
    }
    return changed;
}

uint8_t __time_critical_func(vga_read)(uint32_t addr)
{
    int readplane = vga.readplane;
    addr = normalize_vram_address(addr, &readplane);
    if (addr >= vga.vram_limit) {
        return 0xff;
    }

    vga.la = vram_load(addr);
    vga.lb = vram_load(addr | 1u);
    vga.lc = vram_load(addr | 2u);
    vga.ld = vram_load(addr | 3u);
    if (vga.readmode) {
        uint8_t a = (vga.la ^ ((vga.colourcompare & 1) ? 0xff : 0)) & ((vga.colournocare & 1) ? 0xff : 0);
        uint8_t b = (vga.lb ^ ((vga.colourcompare & 2) ? 0xff : 0)) & ((vga.colournocare & 2) ? 0xff : 0);
        uint8_t c = (vga.lc ^ ((vga.colourcompare & 4) ? 0xff : 0)) & ((vga.colournocare & 4) ? 0xff : 0);
        uint8_t d = (vga.ld ^ ((vga.colourcompare & 8) ? 0xff : 0)) & ((vga.colournocare & 8) ? 0xff : 0);
        return (uint8_t)~(a | b | c | d);
    }
    return vram_load(addr | (uint32_t)(readplane & 3));
}

void set_dac_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    vga.vgapal[index][0] = r & 0x3fu;
    vga.vgapal[index][1] = g & 0x3fu;
    vga.vgapal[index][2] = b & 0x3fu;
    pallook256[index] = makecol((uint8_t)((r & 0x3fu) * 4u),
                                (uint8_t)((g & 0x3fu) * 4u),
                                (uint8_t)((b & 0x3fu) * 4u));
    if (vga.pallook) {
        refresh_active_palette(&vga);
    }
}

void cl_update_writemode()
{
    if (vga.gdcreg[0x0b] & kClGrbWriteModeExt) {
        vga.writemode = vga.gdcreg[5] & 7u;
    } else {
        vga.writemode = vga.gdcreg[5] & 3u;
    }
}

void cl_write_sequencer_data(uint8_t val)
{
    uint8_t index = vga.seqaddr & 0x3f;
    if (vga.seqaddr > 5) {
        uint8_t old = vga.seqregs[index];
        vga.seqregs[index] = val;
        switch (vga.seqaddr & 0x1f) {
        case 0x07:
            vga.packed_chain4 = val & 1u;
            cl_recalc_mapping(&vga);
            request_timing_recalc();
            request_display_dirty();
            break;
        case 0x10:
            vga.sr10_read = vga.seqaddr & 0xe0u;
            break;
        case 0x11:
            vga.sr11_read = vga.seqaddr & 0xe0u;
            break;
        case 0x17:
            cl_recalc_mapping(&vga);
            break;
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x1b:
        case 0x1c:
        case 0x1d:
        case 0x1e:
            request_timing_recalc();
            break;
        }
        if (old != val) {
            request_display_dirty();
        }
        return;
    }

    uint8_t old = vga.seqregs[index];
    vga.seqregs[index] = val;
    if (old != val) {
        switch (index) {
        case 1:
            request_timing_recalc();
            request_display_dirty();
            break;
        case 3:
            request_display_dirty();
            break;
        }
    }
    switch (index) {
    case 1:
        vga.scrblank = (vga.scrblank & ~0x20u) | (val & 0x20u);
        break;
    case 2:
        vga.writemask = val & 0x0f;
        break;
    case 3:
        vga.charsetb = (((val >> 2) & 3) * 0x10000u) + 2u;
        vga.charseta = ((val & 3) * 0x10000u) + 2u;
        if (val & 0x10) {
            vga.charseta += 0x8000u;
        }
        if (val & 0x20) {
            vga.charsetb += 0x8000u;
        }
        break;
    case 4:
        vga.chain2_write = !(val & 4);
        vga.chain4 = val & 8;
        break;
    }
}

uint8_t cl_read_sequencer_address()
{
    if ((vga.seqaddr & 0x1f) == 0x10) {
        return (vga.seqaddr & 0x1f) | vga.sr10_read;
    }
    if ((vga.seqaddr & 0x1f) == 0x11) {
        return (vga.seqaddr & 0x1f) | vga.sr11_read;
    }
    return vga.seqaddr & 0x1f;
}

uint8_t cl_read_sequencer_data()
{
    if (vga.seqaddr > 5) {
        switch (vga.seqaddr & 0x3f) {
        case 0x06:
            return ((vga.seqregs[6] & 0x17u) == 0x12u) ? 0x12u : 0x0fu;
        case 0x17: {
            uint8_t temp = vga.seqregs[0x17] & ~(7u << 3);
            temp |= kClGd5429IsaBus << 3;
            return temp;
        }
        default:
            return vga.seqregs[vga.seqaddr & 0x3f];
        }
    }
    return vga.seqregs[vga.seqaddr & 0x0f];
}

void cl_mmio_write(uint32_t address, uint8_t val);
uint8_t cl_mmio_read(uint32_t address);

void cl_write_graphics_data(uint8_t val)
{
    uint8_t index = vga.gdcaddr & 0x3f;
    uint8_t old = vga.gdcreg[index];
    vga.gdcreg[index] = val;

    switch (index) {
    case 2:
        vga.colourcompare = val;
        break;
    case 4:
        vga.readplane = val & 3;
        break;
    case 5:
        cl_update_writemode();
        vga.readmode = val & 8;
        vga.chain2_read = val & 0x10;
        break;
    case 6:
        cl_recalc_mapping(&vga);
        break;
    case 7:
        vga.colournocare = val;
        break;
    case 0x09:
    case 0x0a:
    case 0x0b:
        cl_recalc_banking(&vga);
        cl_update_writemode();
        break;
    case 0x10:
        cl_mmio_write(0xb8001, val);
        break;
    case 0x11:
        cl_mmio_write(0xb8005, val);
        break;
    case 0x12:
        cl_mmio_write(0xb8002, val);
        break;
    case 0x13:
        cl_mmio_write(0xb8006, val);
        break;
    case 0x14:
        cl_mmio_write(0xb8003, val);
        break;
    case 0x15:
        cl_mmio_write(0xb8007, val);
        break;
    case 0x20:
        cl_mmio_write(0xb8008, val);
        break;
    case 0x21:
        cl_mmio_write(0xb8009, val);
        break;
    case 0x22:
        cl_mmio_write(0xb800a, val);
        break;
    case 0x23:
        cl_mmio_write(0xb800b, val);
        break;
    case 0x24:
        cl_mmio_write(0xb800c, val);
        break;
    case 0x25:
        cl_mmio_write(0xb800d, val);
        break;
    case 0x26:
        cl_mmio_write(0xb800e, val);
        break;
    case 0x27:
        cl_mmio_write(0xb800f, val);
        break;
    case 0x28:
        cl_mmio_write(0xb8010, val);
        break;
    case 0x29:
        cl_mmio_write(0xb8011, val);
        break;
    case 0x2a:
        cl_mmio_write(0xb8012, val);
        break;
    case 0x2c:
        cl_mmio_write(0xb8014, val);
        break;
    case 0x2d:
        cl_mmio_write(0xb8015, val);
        break;
    case 0x2e:
        cl_mmio_write(0xb8016, val);
        break;
    case 0x2f:
        cl_mmio_write(0xb8017, val);
        break;
    case 0x30:
        cl_mmio_write(0xb8018, val);
        break;
    case 0x31:
        cl_mmio_write(0xb8040, val);
        break;
    case 0x32:
        cl_mmio_write(0xb801a, val);
        break;
    }

    if (old != val) {
        if ((index == 5 && ((old ^ val) & 0x70)) ||
            (index == 6 && ((old ^ val) & 0x0d)) ||
            index == 0x0b) {
            request_timing_recalc();
        }
        request_display_dirty();
    }
}

uint8_t cl_apply_rop(uint8_t src, uint8_t dst, uint8_t rop)
{
    switch (rop) {
    case 0x00:
        return 0;
    case 0x05:
        return src & dst;
    case 0x06:
        return dst;
    case 0x09:
        return src & (uint8_t)~dst;
    case 0x0b:
        return (uint8_t)~dst;
    case 0x0d:
        return src;
    case 0x0e:
        return 0xff;
    case 0x50:
        return (uint8_t)~src & dst;
    case 0x59:
        return src ^ dst;
    case 0x6d:
        return src | dst;
    case 0x90:
        return (uint8_t)~(src | dst);
    case 0x95:
        return (uint8_t)~(src ^ dst);
    case 0xad:
        return src | (uint8_t)~dst;
    case 0xd0:
        return (uint8_t)~src;
    case 0xd6:
        return (uint8_t)~src | dst;
    case 0xda:
        return (uint8_t)~(src & dst);
    default:
        return src;
    }
}

int cl_blt_x_max()
{
    switch (vga.blt.depth) {
    case 1:
        return 16;
    case 3:
        return 32;
    default:
        return 8;
    }
}

void __time_critical_func(cl_start_blit)()
{
    int x_max = cl_blt_x_max();
    vga.blt.dst_addr_backup = vga.blt.dst_addr;
    vga.blt.src_addr_backup = vga.blt.src_addr;
    vga.blt.width_backup = vga.blt.width;
    vga.blt.height_internal = vga.blt.height;
    vga.blt.x_count = 0;
    vga.blt.y_count = ((vga.blt.mode & 0xc0) == 0xc0) ? (int)(vga.blt.src_addr & 7u) : 0;

    if (vga.blt.mode & 0x04) {
        return;
    }

    while (vga.blt.height_internal != 0xffffu) {
        uint8_t src = 0;
        int mask = 0;
        int shift;
        if (vga.blt.depth == 3) {
            shift = (vga.blt.x_count & 3) * 8;
        } else if (vga.blt.depth == 1) {
            shift = (vga.blt.x_count & 1) * 8;
        } else {
            shift = 0;
        }

        switch (vga.blt.mode & 0xc0) {
        case 0x00:
            src = vram_load(vga.blt.src_addr);
            vga.blt.src_addr += (vga.blt.mode & 1) ? -1 : 1;
            mask = 1;
            break;
        case 0x40:
            src = vram_load((vga.blt.src_addr & ~7u) + ((uint32_t)vga.blt.y_count << 3) +
                            ((uint32_t)vga.blt.x_count & 7u));
            mask = 1;
            break;
        case 0x80:
            mask = vram_load(vga.blt.src_addr) & (0x80 >> vga.blt.x_count);
            src = mask ? (uint8_t)(vga.blt.fg_col >> shift) : (uint8_t)(vga.blt.bg_col >> shift);
            break;
        case 0xc0:
            mask = vram_load((vga.blt.src_addr & ~7u) | (uint32_t)vga.blt.y_count) &
                   (0x80 >> (vga.blt.depth == 0 ? vga.blt.x_count :
                             (vga.blt.depth == 1 ? (vga.blt.x_count >> 1) : (vga.blt.x_count >> 2))));
            src = mask ? (uint8_t)(vga.blt.fg_col >> shift) : (uint8_t)(vga.blt.bg_col >> shift);
            break;
        }

        uint32_t dst_addr = vga.blt.dst_addr & vga.vrammask;
        uint8_t dst = vram_load(dst_addr);
        if (mask) {
            dst = cl_apply_rop(src, dst, vga.blt.rop);
            vram_store(dst_addr, dst);
        }

        vga.blt.dst_addr += (vga.blt.mode & 1) ? -1 : 1;
        ++vga.blt.x_count;
        if (vga.blt.x_count == x_max) {
            vga.blt.x_count = 0;
            if ((vga.blt.mode & 0xc0) == 0x80) {
                ++vga.blt.src_addr;
            }
        }

        --vga.blt.width;
        if (vga.blt.width == 0xffffu) {
            vga.blt.width = vga.blt.width_backup;
            vga.blt.dst_addr = vga.blt.dst_addr_backup =
                vga.blt.dst_addr_backup + ((vga.blt.mode & 1) ? -(int32_t)vga.blt.dst_pitch : vga.blt.dst_pitch);

            if ((vga.blt.mode & 0xc0) == 0x00) {
                vga.blt.src_addr = vga.blt.src_addr_backup =
                    vga.blt.src_addr_backup + ((vga.blt.mode & 1) ? -(int32_t)vga.blt.src_pitch : vga.blt.src_pitch);
            } else if ((vga.blt.mode & 0xc0) == 0x80 && vga.blt.x_count != 0) {
                ++vga.blt.src_addr;
            }

            vga.blt.x_count = 0;
            vga.blt.y_count = (vga.blt.mode & 1) ? ((vga.blt.y_count - 1) & 7) : ((vga.blt.y_count + 1) & 7);
            --vga.blt.height_internal;
        }
    }
    request_display_dirty();
}

void cl_mmio_write(uint32_t address, uint8_t val)
{
    switch (address & 0xffu) {
    case 0x00:
        vga.blt.bg_col = (vga.blt.bg_col & 0xff00u) | val;
        break;
    case 0x01:
        vga.blt.bg_col = (vga.blt.bg_col & 0x00ffu) | ((uint32_t)val << 8);
        break;
    case 0x04:
        vga.blt.fg_col = (vga.blt.fg_col & 0xff00u) | val;
        break;
    case 0x05:
        vga.blt.fg_col = (vga.blt.fg_col & 0x00ffu) | ((uint32_t)val << 8);
        break;
    case 0x08:
        vga.blt.width = (vga.blt.width & 0xff00u) | val;
        break;
    case 0x09:
        vga.blt.width = ((vga.blt.width & 0x00ffu) | ((uint16_t)val << 8)) & 0x07ffu;
        break;
    case 0x0a:
        vga.blt.height = (vga.blt.height & 0xff00u) | val;
        break;
    case 0x0b:
        vga.blt.height = ((vga.blt.height & 0x00ffu) | ((uint16_t)val << 8)) & 0x03ffu;
        break;
    case 0x0c:
        vga.blt.dst_pitch = (vga.blt.dst_pitch & 0xff00u) | val;
        break;
    case 0x0d:
        vga.blt.dst_pitch = (vga.blt.dst_pitch & 0x00ffu) | ((uint16_t)val << 8);
        break;
    case 0x0e:
        vga.blt.src_pitch = (vga.blt.src_pitch & 0xff00u) | val;
        break;
    case 0x0f:
        vga.blt.src_pitch = (vga.blt.src_pitch & 0x00ffu) | ((uint16_t)val << 8);
        break;
    case 0x10:
        vga.blt.dst_addr = (vga.blt.dst_addr & 0xffff00u) | val;
        break;
    case 0x11:
        vga.blt.dst_addr = (vga.blt.dst_addr & 0xff00ffu) | ((uint32_t)val << 8);
        break;
    case 0x12:
        vga.blt.dst_addr = ((vga.blt.dst_addr & 0x00ffffu) | ((uint32_t)val << 16)) & vga.vrammask;
        break;
    case 0x14:
        vga.blt.src_addr = (vga.blt.src_addr & 0xffff00u) | val;
        break;
    case 0x15:
        vga.blt.src_addr = (vga.blt.src_addr & 0xff00ffu) | ((uint32_t)val << 8);
        break;
    case 0x16:
        vga.blt.src_addr = ((vga.blt.src_addr & 0x00ffffu) | ((uint32_t)val << 16)) & vga.vrammask;
        break;
    case 0x17:
        vga.blt.mask = val;
        break;
    case 0x18:
        vga.blt.mode = val;
        vga.blt.depth = (val >> 4) & 1;
        break;
    case 0x1a:
        vga.blt.rop = val;
        break;
    case 0x40:
        if (val & 0x02) {
            cl_start_blit();
        }
        break;
    }
}

uint8_t cl_mmio_read(uint32_t address)
{
    if ((address & 0xffu) == 0x40) {
        return 0;
    }
    return 0xff;
}

void __time_critical_func(vga_out)(uint16_t addr, uint8_t val)
{
    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(vga.miscout & 1)) {
        addr ^= 0x60;
    }

    switch (addr) {
    case 0x3c0:
        if (!vga.attrff) {
            vga.attraddr = val & 31;
            uint8_t old_enable = vga.attr_palette_enable;
            vga.attr_palette_enable = val & 0x20;
            if (old_enable != vga.attr_palette_enable) {
                request_timing_recalc();
                request_display_dirty();
            }
        } else {
            uint8_t index = vga.attraddr & 31;
            uint8_t old = vga.attrregs[index];
            vga.attrregs[index] = val;
            if (index == 0x10 || index == 0x14 || index < 0x10) {
                rebuild_egapal(&vga);
            }
            if (index == 0x12) {
                vga.plane_mask = val & 0x0f;
            }
            if (old != val && (index < 0x10 || index == 0x10 || index == 0x12 ||
                               index == 0x13 || index == 0x14)) {
                request_display_dirty();
            }
            if (old != val && index == 0x10) {
                request_timing_recalc();
            }
        }
        vga.attrff ^= 1;
        break;
    case 0x3c2:
    {
        uint8_t old_vidclock = vga.vidclock;
        vga.vidclock = val & 4;
        vga.miscout = val;
        if (old_vidclock != vga.vidclock) {
            request_timing_recalc();
        }
        break;
    }
    case 0x3c4:
        vga.seqaddr = val;
        break;
    case 0x3c5:
        cl_write_sequencer_data(val);
        break;
    case 0x3c6:
        if (vga.dac_3c6_count == 4) {
            vga.dac_3c6_count = 0;
            vga.hidden_dac_reg = val;
            request_timing_recalc();
            request_display_dirty();
            break;
        }
        vga.dac_3c6_count = 0;
        vga.dac_mask = val;
        request_display_dirty();
        break;
    case 0x3c7:
        vga.dac_3c6_count = 0;
        vga.dac_read = val;
        vga.dac_pos = 0;
        break;
    case 0x3c8:
        vga.dac_3c6_count = 0;
        vga.dac_write = val;
        vga.dac_read = val - 1u;
        vga.dac_pos = 0;
        break;
    case 0x3c9:
        vga.dac_3c6_count = 0;
        vga.dac_status = 0;
        switch (vga.dac_pos) {
        case 0:
            vga.dac_r = val;
            vga.dac_pos = 1;
            break;
        case 1:
            vga.dac_g = val;
            vga.dac_pos = 2;
            break;
        default:
            set_dac_entry(vga.dac_write, vga.dac_r, vga.dac_g, val);
            vga.dac_write++;
            vga.dac_pos = 0;
            request_display_dirty();
            break;
        }
        break;
    case 0x3ce:
        vga.gdcaddr = val;
        break;
    case 0x3cf:
        cl_write_graphics_data(val);
        break;
    case 0x3d4:
        vga.crtcreg = val & 0x3f;
        break;
    case 0x3d5: {
        uint8_t index = vga.crtcreg;
        if (index < 7 && (vga.crtc[0x11] & 0x80)) {
            return;
        }
        if (index == 7 && (vga.crtc[0x11] & 0x80)) {
            val = (vga.crtc[7] & ~0x10u) | (val & 0x10u);
        }
        uint32_t old_cursor = ((uint32_t)vga.crtc[0x0e] << 8) | vga.crtc[0x0f];
        bool old_cursor_visible = !(vga.crtc[0x0a] & 0x20);
        uint8_t old = vga.crtc[index];
        vga.crtc[index] = val;
        if (old == val) {
            break;
        }
        if (index == 0x0e || index == 0x0f) {
            uint32_t new_cursor = ((uint32_t)vga.crtc[0x0e] << 8) | vga.crtc[0x0f];
            mark_cursor_cells_dirty(old_cursor, new_cursor);
        } else if (index == 0x0a || index == 0x0b) {
            if (old_cursor_visible || !(vga.crtc[0x0a] & 0x20)) {
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


uint8_t __time_critical_func(vga_in)(uint16_t addr)
{
    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(vga.miscout & 1)) {
        addr ^= 0x60;
    }

    switch (addr) {
    case 0x3c0:
        return vga.attraddr | vga.attr_palette_enable;
    case 0x3c1:
        return vga.attrregs[vga.attraddr & 31];
    case 0x3c2:
        return (uint8_t)((vga.vgapal[0][0] + vga.vgapal[0][1] + vga.vgapal[0][2]) >= 0x50 ? 0 : 0x10);
    case 0x3c4:
        return cl_read_sequencer_address();
    case 0x3c5:
        return cl_read_sequencer_data();
    case 0x3c6:
        if (vga.dac_3c6_count == 4) {
            vga.dac_3c6_count = 0;
            return vga.hidden_dac_reg;
        }
        ++vga.dac_3c6_count;
        return vga.dac_mask;
    case 0x3c7:
        vga.dac_3c6_count = 0;
        return vga.dac_status;
    case 0x3c8:
        vga.dac_3c6_count = 0;
        return vga.dac_write;
    case 0x3c9:
        vga.dac_3c6_count = 0;
        vga.dac_status = 3;
        switch (vga.dac_pos) {
        case 0:
            vga.dac_pos = 1;
            return vga.vgapal[vga.dac_read][0] & 0x3f;
        case 1:
            vga.dac_pos = 2;
            return vga.vgapal[vga.dac_read][1] & 0x3f;
        default: {
            uint8_t value = vga.vgapal[vga.dac_read][2] & 0x3f;
            vga.dac_read++;
            vga.dac_pos = 0;
            return value;
        }
        }
    case 0x3cc:
        return vga.miscout;
    case 0x3ce:
        return vga.gdcaddr;
    case 0x3cf:
        return vga.gdcreg[vga.gdcaddr & 0x3f];
    case 0x3d4:
        return vga.crtcreg;
    case 0x3d5:
        switch (vga.crtcreg) {
        case 0x27:
            return 0x9c;
        case 0x28:
            return 0x00;
        default:
            return vga.crtc[vga.crtcreg];
        }
    case 0x3da:
        vga.attrff = 0;
        vga.stat ^= 0x30;
        return scanout::extrapolate_stat(vga, vga.stat);
    default:
        return 0xff;
    }
}

bool __time_critical_func(vga_io_read)(uint16_t port, uint8_t *data)
{
    *data = vga_in(port);
    return true;
}

void __time_critical_func(vga_io_write)(uint16_t port, uint8_t data)
{
    vga_out(port, data);
}

bool __time_critical_func(vga_mem_read)(uint32_t address, uint8_t *data)
{
    if (!vga_vram_active(address)) {
        return false;
    }
    if (vga.mmio_enabled && address >= 0xb8000 && address < 0xb8100) {
        *data = cl_mmio_read(address);
        return true;
    }
    *data = vga_read(cl_banked_offset(address));
    return true;
}

void __time_critical_func(vga_mem_write)(uint32_t address, uint8_t data)
{
    if (!vga_vram_active(address)) {
        return;
    }
    if (vga.mmio_enabled && address >= 0xb8000 && address < 0xb8100) {
        cl_mmio_write(address, data);
        return;
    }
    uint32_t offset = cl_banked_offset(address);
    scanout::mem_write(display, offset, data,
                       [](uint32_t a, uint8_t d, bool track) { return vga_write(a, d, track); },
                       [](uint32_t) { return kDirtyUnmapped; },
                       [](uint32_t) { return kDirtyUnmapped; });
}

bool __time_critical_func(vga_rom_read)(uint32_t address, uint8_t *data)
{
    return scanout::rom_read(address, kVgaRomBase, kVgaRomSize, kPcemCl5429BiosRom, kVgaRomImageSize, data);
}

void build_tables()
{
    for (int c = 0; c < 256; ++c) {
        int e = c;
        for (int d = 0; d < 8; ++d) {
            vga_rotate[d][c] = (uint8_t)e;
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
    static constexpr uint8_t base16[16][3] = {
        {0x00, 0x00, 0x00}, {0x00, 0x00, 0x2a}, {0x00, 0x2a, 0x00}, {0x00, 0x2a, 0x2a},
        {0x2a, 0x00, 0x00}, {0x2a, 0x00, 0x2a}, {0x2a, 0x15, 0x00}, {0x2a, 0x2a, 0x2a},
        {0x15, 0x15, 0x15}, {0x15, 0x15, 0x3f}, {0x15, 0x3f, 0x15}, {0x15, 0x3f, 0x3f},
        {0x3f, 0x15, 0x15}, {0x3f, 0x15, 0x3f}, {0x3f, 0x3f, 0x15}, {0x3f, 0x3f, 0x3f},
    };
    for (int c = 0; c < 16; ++c) {
        set_dac_entry((uint8_t)c, base16[c][0], base16[c][1], base16[c][2]);
    }
    for (int c = 16; c < 232; ++c) {
        int n = c - 16;
        uint8_t r = (uint8_t)((n / 36) * 51 / 4);
        uint8_t g = (uint8_t)(((n / 6) % 6) * 51 / 4);
        uint8_t b = (uint8_t)((n % 6) * 51 / 4);
        set_dac_entry((uint8_t)c, r, g, b);
    }
    for (int c = 232; c < 256; ++c) {
        uint8_t level = (uint8_t)(8 + (c - 232) * 10 / 4);
        set_dac_entry((uint8_t)c, level, level, level);
    }
}

void init()
{
    std::memset(&vga, 0, sizeof(vga));
    std::memset(line_buffer, 0, sizeof(line_buffer));
    display.reset(kMaxVgaWidth, kMaxVgaLines, line_buffer, "cl5429");
    deferred_timing.handled = 0;
    __atomic_store_n(&deferred_timing.requested, 0u, __ATOMIC_RELEASE);
    build_tables();

    vga.pallook = pallook256;
    vga.vram_limit = kVgaVramSize;
    vga.vrammask = kVgaVramSize - 1u;
    vga.decode_mask = 0x7fffffu;
    vga.banked_mask = 0xffffu;
    vga.bank[1] = 0x8000u;
    vga.writemask = 0x0f;
    vga.colournocare = 0x0f;
    vga.dac_mask = 0xff;
    vga.plane_mask = 0x0f;
    vga.miscout = 1;
    vga.crtc[0] = 63;
    vga.crtc[6] = 255;
    vga.seqregs[0x0b] = 0x66;
    vga.seqregs[0x0c] = 0x5b;
    vga.seqregs[0x0d] = 0x45;
    vga.seqregs[0x0e] = 0x7e;
    vga.seqregs[0x1b] = 0x3b;
    vga.seqregs[0x1c] = 0x2f;
    vga.seqregs[0x1d] = 0x30;
    vga.seqregs[0x1e] = 0x33;
    vga.firstline = 2000;
    vga.dispon = 1;
    cl_recalc_banking(&vga);
    cl_recalc_mapping(&vga);
    rebuild_egapal(&vga);
    vga_recalctimings(&vga);

    xsize = 1;
    ysize = 1;

    printf("cl5429: PCem CL-GD5429 I/O 0x%03x-0x%03x, VRAM %u KB, ROM 0x%05lx-0x%05lx\n",
           kVgaIoBase,
           kVgaIoBase + kVgaIoSize - 1,
           (unsigned)PICOGRAPH_CL5429_MEMORY_KB,
           (unsigned long)kVgaRomBase,
           (unsigned long)(kVgaRomBase + kVgaRomSize - 1));
}

void tick()
{
    scanout::tick(display,
                  [] { handle_deferred_requests(); },
                  [](RenderContext *ctx) { vga_advance_state(ctx); });
}

IoTrap io_traps[] = {
    {kVgaIoBase, kVgaIoSize, true, vga_io_read, vga_io_write},
};

MemTrap mem_traps[] = {
    {kVgaMemBase, kVgaMemSize, true, vga_mem_read, vga_mem_write, vga_vram_active},
    {kVgaRomBase, kVgaRomSize, true, vga_rom_read, nullptr, nullptr},
};

const Module module = {
    "cl5429-displaylink",
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

const Module &cl5429_module()
{
    return module;
}

}  // namespace picograph
