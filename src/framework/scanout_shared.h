#pragma once

#include <algorithm>
#include <cstdint>

#include "hardware/timer.h"

#include "modules/dlodirty.h"

extern "C" PGC GCDisplay(void);

// Shared scaffolding for the scanline display modules (EGA, VGA, CL5429):
// the real-time pacing engine, wall-clock status extrapolation, VRAM store
// helpers, dirty-line mapping, and the ISA memory-write trap body. The
// PCem-derived emulation (register semantics, write modes, renderers) stays
// in the modules; everything here is picograph plumbing that used to be
// copied into each of them.
//
// Functions are duck-typed on the module's device struct and force-inlined
// so they compile into the module's RAM-resident (__time_critical_func)
// callers instead of landing in flash, where an XIP miss can stall the ISA
// service path on core 1.

#define PICOGRAPH_SCANOUT_INLINE __attribute__((always_inline)) inline

namespace picograph {
namespace scanout {

constexpr unsigned kTimingFracBits = 16;
constexpr uint64_t kTimingOne = 1ull << kTimingFracBits;
constexpr unsigned kMaxPollStepsPerTick = 2048;

// Result of mapping a VRAM write to display lines.
constexpr int kDirtyUnmapped = -1;
constexpr int kDirtyNoVisibleChange = 0;
constexpr int kDirtyMapped = 1;

/* ---- timing / pacing ------------------------------------------------- */

inline uint64_t time_from_chars(int chars, uint32_t pixel_clock_hz, unsigned char_width)
{
    if (chars <= 0) {
        return 0;
    }

    uint64_t numerator = (uint64_t)chars * char_width * 1000000ull * kTimingOne;
    return numerator / pixel_clock_hz;
}

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE uint32_t delay_us(Dev *e, uint64_t delay)
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

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE uint64_t load_dispontime(const Dev *e)
{
    return __atomic_load_n(&e->dispontime, __ATOMIC_ACQUIRE);
}

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE uint64_t load_dispofftime(const Dev *e)
{
    return __atomic_load_n(&e->dispofftime, __ATOMIC_ACQUIRE);
}

template <typename Dev, typename PollFn>
inline void advance_state(Dev &dev, DloDirtyDisplay::RenderContext *ctx, PollFn &&poll)
{
    uint32_t now = time_us_32();
    if (!dev.timing_started) {
        dev.next_poll_us = now;
        dev.timing_started = true;
    }

    unsigned steps = 0;
    while ((int32_t)(now - dev.next_poll_us) >= 0 && steps < kMaxPollStepsPerTick) {
        dev.next_poll_us += poll(&dev, ctx);
        ++steps;
    }
    if (steps == kMaxPollStepsPerTick && (int32_t)(now - dev.next_poll_us) > 0) {
        dev.next_poll_us = now + 1;
    }
}

// The scanline engine on core 0 advances in real time only while it gets CPU
// time; during DisplayLink upload bursts it can stall for milliseconds with
// the status register frozen mid-phase. Host timing loops key off the status
// port: id Software's SyncVBL hunts for the vertical front porch (display
// disabled, vsync inactive) with tight IN loops and treats a long reading of
// that state as vertical blank, so a frozen blanking phase makes the game
// flip and draw over the page still being scanned. When the engine is
// behind, derive display-enable and vretrace from the wall clock instead of
// the frozen state. Runs on core 1; the racy field reads can at worst
// produce one odd sample, which persistence-checking host loops reject.
template <typename Dev>
PICOGRAPH_SCANOUT_INLINE uint8_t extrapolate_stat(const Dev &dev, uint8_t stat)
{
#if !PICOGRAPH_REALTIME_STATUS
    return stat;
#endif
    if (!dev.timing_started) {
        return stat;
    }
    int32_t behind = (int32_t)(time_us_32() - dev.next_poll_us);
    if (behind <= 0) {
        return stat;
    }

    uint32_t blank_us = (uint32_t)(load_dispofftime(&dev) >> kTimingFracBits);
    uint32_t active_us = (uint32_t)(load_dispontime(&dev) >> kTimingFracBits);
    uint32_t line_us = blank_us + active_us;
    uint32_t vtotal = (uint32_t)dev.vtotal;
    if (line_us == 0 || vtotal == 0) {
        return stat;
    }

    // next_poll_us is the deadline of the phase the engine is stuck in:
    // linepos==1 means the blanking phase of the current line is ending,
    // otherwise the active phase (end of line) is. Advance from there.
    uint32_t pos = dev.linepos ? blank_us : line_us;
    uint32_t total = pos + (uint32_t)behind;
    uint32_t line = ((uint32_t)dev.vc + total / line_us) % vtotal;
    uint32_t rem = total % line_us;

    stat &= (uint8_t)~0x09u;
    if (line >= (uint32_t)dev.dispend || rem < blank_us) {
        stat |= 0x01;
    }
    uint32_t vsyncstart = (uint32_t)dev.vsyncstart;
    if (line >= vsyncstart && line < vsyncstart + 4u) {
        stat |= 0x08;
    }
    return stat;
}

/* ---- deferred cross-core requests ------------------------------------ */

// Register writes handled on core 1 may need work that must run on core 0
// (retiming touches state the render loop owns); they bump a counter that
// the module tick consumes.
struct DeferredTiming {
    volatile uint32_t requested;
    uint32_t handled;
};

PICOGRAPH_SCANOUT_INLINE void request_timing_recalc(DeferredTiming &timing)
{
    __atomic_fetch_add(&timing.requested, 1u, __ATOMIC_RELEASE);
}

template <typename RecalcFn>
inline void handle_deferred_requests(DeferredTiming &timing, RecalcFn &&recalc)
{
    uint32_t requests = __atomic_load_n(&timing.requested, __ATOMIC_ACQUIRE);
    if (requests != timing.handled) {
        recalc();
        timing.handled = requests;
    }
}

/* ---- VRAM store helpers ----------------------------------------------- */

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE uint8_t vram_load(const Dev &dev, uint32_t addr)
{
    return __atomic_load_n(&dev.vram[addr & dev.vrammask], __ATOMIC_RELAXED);
}

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE void vram_store(Dev &dev, uint32_t addr, uint8_t val)
{
    __atomic_store_n(&dev.vram[addr & dev.vrammask], val, __ATOMIC_RELAXED);
}

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE bool vram_store_changed(Dev &dev, uint32_t addr, uint8_t val)
{
    uint32_t masked = addr & dev.vrammask;
    uint8_t old = __atomic_load_n(&dev.vram[masked], __ATOMIC_RELAXED);
    if (old == val) {
        return false;
    }
    __atomic_store_n(&dev.vram[masked], val, __ATOMIC_RELAXED);
    return true;
}

template <typename Dev>
PICOGRAPH_SCANOUT_INLINE void vram_store_write(Dev &dev, uint32_t addr, uint8_t val, bool track_changes, bool *changed)
{
    if (track_changes) {
        *changed |= vram_store_changed(dev, addr, val);
    } else {
        vram_store(dev, addr, val);
    }
}

/* ---- dirty-line mapping ------------------------------------------------ */

// Map a text cell index to the display lines it covers. mode_inactive is the
// module's check that the current mode is not the text layout this mapping
// assumes (the EGA and VGA differ in which register bits participate).
template <typename Dev>
PICOGRAPH_SCANOUT_INLINE int mark_text_cell_dirty(Dev &dev,
                                                  DloDirtyDisplay &display,
                                                  unsigned max_lines,
                                                  bool mode_inactive,
                                                  uint32_t cell)
{
    if (mode_inactive || !dev.chain2_write ||
        !display.frame_valid || display.source_height == 0 ||
        display.vertical_scale == 0 || dev.hdisp <= 0 || dev.rowoffset <= 0 ||
        display.first_line < 0 || display.first_line >= (int)max_lines ||
        dev.split <= dev.dispend) {
        return kDirtyUnmapped;
    }

    unsigned visible_lines = display.source_height / display.vertical_scale;
    unsigned char_height = (unsigned)(dev.crtc[9] & 31) + 1u;
    if (visible_lines == 0) {
        return kDirtyUnmapped;
    }

    uint32_t start_cell = ((uint32_t)dev.crtc[0x0c] << 8) | dev.crtc[0x0d];
    uint32_t row_cells = (uint32_t)dev.rowoffset * 2u;
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
    if (column >= (uint32_t)dev.hdisp) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line_offset = row * char_height;
    if (first_line_offset >= visible_lines) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line = (unsigned)display.first_line + first_line_offset;
    unsigned end_offset = std::min<unsigned>(first_line_offset + char_height, visible_lines);
    unsigned end_line = (unsigned)display.first_line + end_offset;
    if (first_line >= max_lines) {
        return kDirtyNoVisibleChange;
    }
    end_line = std::min<unsigned>(end_line, max_lines);
    display.mark_line_range_dirty(first_line, end_line);
    return kDirtyMapped;
}

// Map a VRAM write in the text window to its cell, then to display lines.
template <typename TextCellFn>
PICOGRAPH_SCANOUT_INLINE int mark_text_vram_write_dirty(uint32_t address, TextCellFn &&text_cell)
{
    uint32_t offset = (address >= 0xb0000) ? (address & 0x7fffu) : (address & 0xffffu);
    if (offset & 0x4000u) {
        return kDirtyUnmapped;
    }

    return text_cell(offset >> 1);
}

// Map a planar graphics VRAM write to the display lines it covers.
// normalize is the module's address normalization, evaluated only once the
// cheap precondition checks pass; mode_inactive is the module's check that
// the current mode is not the planar layout this mapping assumes.
template <typename Dev, typename NormalizeFn>
PICOGRAPH_SCANOUT_INLINE int mark_graphics_vram_write_dirty(Dev &dev,
                                                            DloDirtyDisplay &display,
                                                            unsigned max_lines,
                                                            bool mode_inactive,
                                                            NormalizeFn &&normalize)
{
    if (mode_inactive || dev.chain2_write || (dev.seqregs[1] & 4) ||
        (dev.crtc[0x17] & 0x43) != 0x43 ||
        !display.frame_valid || display.source_height == 0 ||
        display.vertical_scale == 0 || dev.hdisp <= 0 || dev.rowoffset <= 0 ||
        display.first_line < 0 || display.first_line >= (int)max_lines ||
        dev.split <= dev.dispend) {
        return kDirtyUnmapped;
    }

    unsigned visible_lines = display.source_height / display.vertical_scale;
    unsigned scanlines_per_row = (unsigned)(dev.crtc[9] & 31) + 1u;
    if (visible_lines == 0) {
        return kDirtyUnmapped;
    }

    uint32_t base = normalize() & ~3u;
    uint32_t start_base = (((uint32_t)dev.crtc[0x0c] << 8) | dev.crtc[0x0d]) << 2;
    uint32_t row_bytes = (uint32_t)dev.rowoffset << 3;
    uint32_t visible_rows = (visible_lines + scanlines_per_row - 1u) / scanlines_per_row;
    if (row_bytes == 0 ||
        start_base >= dev.vram_limit ||
        visible_rows > (dev.vram_limit - start_base) / row_bytes ||
        base >= dev.vram_limit) {
        return kDirtyUnmapped;
    }

    if (base < start_base) {
        return kDirtyNoVisibleChange;
    }

    uint32_t relative = base - start_base;
    uint32_t row = relative / row_bytes;
    uint32_t row_offset = relative - row * row_bytes;
    uint32_t visible_groups = (uint32_t)dev.hdisp + ((dev.scrollcache & 7) ? 1u : 0u);
    if (visible_groups == 0 || row_offset / 4u >= visible_groups) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line_offset = row * scanlines_per_row;
    if (first_line_offset >= visible_lines) {
        return kDirtyNoVisibleChange;
    }

    unsigned first_line = (unsigned)display.first_line + first_line_offset;
    unsigned end_offset = std::min<unsigned>(first_line_offset + scanlines_per_row, visible_lines);
    unsigned end_line = (unsigned)display.first_line + end_offset;
    if (first_line >= max_lines) {
        return kDirtyNoVisibleChange;
    }
    end_line = std::min<unsigned>(end_line, max_lines);
    display.mark_line_range_dirty(first_line, end_line);
    return kDirtyMapped;
}

/* ---- ISA trap bodies ---------------------------------------------------- */

// Body of the memory-write trap: apply the write with change tracking, then
// map it to dirty display lines, falling back to a full-display dirty request
// when the write cannot be attributed. Runs on core 1 inside a wait-stated
// ISA cycle; every callable must be RAM-resident.
template <typename WriteFn, typename TextDirtyFn, typename GfxDirtyFn>
PICOGRAPH_SCANOUT_INLINE void mem_write(DloDirtyDisplay &display,
                                        uint32_t address,
                                        uint8_t data,
                                        WriteFn &&write,
                                        TextDirtyFn &&text_dirty,
                                        GfxDirtyFn &&gfx_dirty)
{
    bool full_dirty_pending = display.full_render_dirty_pending();
    bool track_changes =
        !full_dirty_pending &&
        !display.content_dirty_pending();
    bool changed = write(address, data, track_changes);
    if (full_dirty_pending) {
        return;
    }
    if (!track_changes || changed) {
        int dirty_result = text_dirty(address);
        if (dirty_result == kDirtyUnmapped) {
            dirty_result = gfx_dirty(address);
        }
        if (dirty_result != kDirtyUnmapped) {
            if (dirty_result == kDirtyNoVisibleChange) {
                return;
            }
            display.set_content_dirty();
            return;
        }
        display.request_dirty();
    }
}

PICOGRAPH_SCANOUT_INLINE bool rom_read(uint32_t address,
                                       uint32_t rom_base,
                                       uint32_t rom_size,
                                       const uint8_t *image,
                                       uint32_t image_size,
                                       uint8_t *data)
{
    if (address < rom_base || address >= rom_base + rom_size) {
        return false;
    }
    uint32_t offset = address - rom_base;
    *data = (offset < image_size) ? image[offset] : 0xff;
    return true;
}

/* ---- module tick --------------------------------------------------------- */

template <typename DeferredFn, typename AdvanceFn>
inline void tick(DloDirtyDisplay &display, DeferredFn &&deferred, AdvanceFn &&advance)
{
    PGC pGC = GCDisplay();
    DloDirtyDisplay::RenderContext ctx = {pGC, false, false};
    DloDirtyDisplay::RenderContext *pCtx =
        (pGC && pGC->bitmap.handle && GCWidth(pGC) > 0 && GCHeight(pGC) > 0) ? &ctx : nullptr;
    deferred();
    advance(pCtx);

    if (ctx.access_open) {
        display.end_access(&ctx);
    }
    display.present_pending(pGC);
}

}  // namespace scanout
}  // namespace picograph
