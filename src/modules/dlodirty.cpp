#include "modules/dlodirty.h"

#include "pico/platform/sections.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace picograph {

void DloDirtyDisplay::reset(unsigned max_width, unsigned max_lines, GCCOLOR *line_buffer, const char *log_prefix)
{
    max_width_ = std::min<unsigned>(max_width ? max_width : kMaxWidth, kMaxWidth);
    max_lines_ = std::min<unsigned>(max_lines ? max_lines : kMaxLines, kMaxLines);
    line_buffer_ = line_buffer;
    log_prefix_ = log_prefix ? log_prefix : "display";

    std::memset(line_hash_, 0, sizeof(line_hash_));
    std::memset(line_width_, 0, sizeof(line_width_));
    std::memset(line_dirty_version_, 0, sizeof(line_dirty_version_));
    std::memset(line_page_version_, 0, sizeof(line_page_version_));
    __atomic_store_n(&line_dirty_next_version_, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&content_dirty_, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&full_render_dirty_, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&dirty_line_marks_, 0u, __ATOMIC_RELEASE);

    first_line = 0;
    source_width = 0;
    source_height = 0;
    canvas_width = 0;
    canvas_height = 0;
    vertical_scale = 1;
    target_width_px = 0;
    target_height_px = 0;
    target_origin_x_px = 0;
    target_origin_y_px = 0;
    frame_valid = false;
    initialized = false;
    window_checked_for_frame = false;
    render_frame = true;
    render_full_frame = true;
    render_full_frame_from_start = true;
    draw_page_needs_clone = false;
    last_gc_width = 0;
    last_gc_height = 0;
    last_source_width = 0;
    last_source_height = 0;
    last_canvas_width = 0;
    last_canvas_height = 0;

    usb_pending = false;
    pages_ready = false;
    content_generation = 1;
    present_pending_flag = false;
    visible_page = 0;
    pending_page = kInvalidPage;
    draw_page = 0;
    for (unsigned page = 0; page < kPageCount; ++page) {
        page_base[page] = 0;
        page_clear_pending[page] = true;
        page_content_valid[page] = false;
        page_generation[page] = 0;
    }
}

void DloDirtyDisplay::set_line_buffer(GCCOLOR *line_buffer)
{
    line_buffer_ = line_buffer;
}

unsigned DloDirtyDisplay::command_chunks(unsigned pixels) const
{
    return pixels ? ((pixels + 255u) / 256u) : 0u;
}

unsigned DloDirtyDisplay::fill_run_bytes(unsigned pixels) const
{
    return 17u * command_chunks(pixels);
}

unsigned DloDirtyDisplay::copy_row_bytes(unsigned pixels) const
{
    return 18u * command_chunks(pixels);
}

unsigned DloDirtyDisplay::raw_line_bytes(unsigned pixels) const
{
    return pixels ? (3u * pixels + 12u * command_chunks(pixels)) : 0u;
}

uint32_t DloDirtyDisplay::measure_line_hash(const GCCOLOR *data, unsigned width) const
{
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < width; ++i) {
        hash ^= (uint32_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

unsigned DloDirtyDisplay::cap_fill_bytes(unsigned fill_bytes, unsigned fill_stop_bytes) const
{
    if (fill_stop_bytes != 0 && fill_bytes > fill_stop_bytes) {
        return fill_stop_bytes + 1u;
    }
    return fill_bytes;
}

DloDirtyDisplay::LineMeasure DloDirtyDisplay::measure_line_hash_and_fill(const GCCOLOR *data,
                                                                         unsigned width,
                                                                         unsigned fill_stop_bytes) const
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
                fill_bytes = cap_fill_bytes(fill_bytes + fill_run_bytes(run_length), fill_stop_bytes);
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
        fill_bytes = cap_fill_bytes(fill_bytes + fill_run_bytes(run_length), fill_stop_bytes);
    }
    return {hash, fill_bytes};
}

void DloDirtyDisplay::advance_scaled_sample(unsigned *sample_x,
                                            unsigned *error,
                                            unsigned src_width,
                                            unsigned target_width) const
{
    *error += src_width;
    while (*error >= target_width) {
        *error -= target_width;
        ++*sample_x;
    }
}

uint32_t DloDirtyDisplay::downscale_line_in_place(unsigned src_width, unsigned target_width)
{
    uint32_t hash = 2166136261u;
    if (target_width == 0 || !line_buffer_) {
        return hash;
    }

    unsigned sample_x = 0;
    unsigned error = 0;

    GCCOLOR pixel = line_buffer_[0];
    line_buffer_[0] = pixel;
    hash ^= (uint32_t)pixel;
    hash *= 16777619u;
    for (unsigned x = 1; x < target_width; ++x) {
        advance_scaled_sample(&sample_x, &error, src_width, target_width);
        pixel = line_buffer_[sample_x];
        line_buffer_[x] = pixel;
        hash ^= (uint32_t)pixel;
        hash *= 16777619u;
    }
    return hash;
}

DloDirtyDisplay::LineMeasure DloDirtyDisplay::downscale_line_in_place_and_measure(unsigned src_width,
                                                                                 unsigned target_width,
                                                                                 unsigned fill_stop_bytes)
{
    uint32_t hash = 2166136261u;
    if (target_width == 0 || !line_buffer_) {
        return {hash, 0};
    }

    unsigned sample_x = 0;
    unsigned error = 0;
    GCCOLOR color = line_buffer_[0];
    unsigned run_length = 1;
    unsigned fill_bytes = 0;
    bool measure_fill = true;

    line_buffer_[0] = color;
    hash ^= (uint32_t)color;
    hash *= 16777619u;
    for (unsigned x = 1; x < target_width; ++x) {
        advance_scaled_sample(&sample_x, &error, src_width, target_width);
        GCCOLOR pixel = line_buffer_[sample_x];
        line_buffer_[x] = pixel;
        if (measure_fill) {
            if (pixel != color) {
                fill_bytes = cap_fill_bytes(fill_bytes + fill_run_bytes(run_length), fill_stop_bytes);
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
        fill_bytes = cap_fill_bytes(fill_bytes + fill_run_bytes(run_length), fill_stop_bytes);
    }
    return {hash, fill_bytes};
}

unsigned DloDirtyDisplay::measure_line_fill_bytes(const GCCOLOR *data,
                                                  unsigned width,
                                                  unsigned fill_stop_bytes) const
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
        fill_bytes = cap_fill_bytes(fill_bytes + fill_run_bytes(run_length), fill_stop_bytes);
        if (fill_stop_bytes != 0 && fill_bytes > fill_stop_bytes) {
            return fill_bytes;
        }
        color = pixel;
        run_length = 1;
    }
    return cap_fill_bytes(fill_bytes + fill_run_bytes(run_length), fill_stop_bytes);
}

void __time_critical_func(DloDirtyDisplay::request_dirty)()
{
    __atomic_store_n(&full_render_dirty_, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&content_dirty_, 1u, __ATOMIC_RELEASE);
}

void __time_critical_func(DloDirtyDisplay::set_content_dirty)()
{
    __atomic_store_n(&content_dirty_, 1u, __ATOMIC_RELEASE);
}

bool DloDirtyDisplay::content_dirty_pending() const
{
    return __atomic_load_n(&content_dirty_, __ATOMIC_ACQUIRE) != 0;
}

bool DloDirtyDisplay::full_render_dirty_pending() const
{
    return __atomic_load_n(&full_render_dirty_, __ATOMIC_ACQUIRE) != 0;
}

bool DloDirtyDisplay::take_content_dirty()
{
    return __atomic_exchange_n(&content_dirty_, 0u, __ATOMIC_ACQ_REL) != 0;
}

bool DloDirtyDisplay::take_full_render_dirty()
{
    return __atomic_exchange_n(&full_render_dirty_, 0u, __ATOMIC_ACQ_REL) != 0;
}

void DloDirtyDisplay::clear_dirty_line_marks()
{
    __atomic_store_n(&dirty_line_marks_, 0u, __ATOMIC_RELEASE);
}

uint32_t __time_critical_func(DloDirtyDisplay::next_dirty_version)()
{
    uint32_t version = __atomic_add_fetch(&line_dirty_next_version_, 1u, __ATOMIC_ACQ_REL);
    if (version == 0) {
        version = __atomic_add_fetch(&line_dirty_next_version_, 1u, __ATOMIC_ACQ_REL);
    }
    return version;
}

void __time_critical_func(DloDirtyDisplay::mark_line_dirty_with_version)(unsigned line, uint32_t version)
{
    if (line >= max_lines_) {
        return;
    }
    __atomic_store_n(&line_dirty_version_[line], version, __ATOMIC_RELEASE);
}

void __time_critical_func(DloDirtyDisplay::mark_line_dirty)(unsigned line)
{
    mark_line_dirty_with_version(line, next_dirty_version());
    set_content_dirty();
}

void __time_critical_func(DloDirtyDisplay::mark_line_range_dirty)(unsigned first, unsigned end)
{
    if (first >= end || first >= max_lines_) {
        return;
    }

    end = std::min<unsigned>(end, max_lines_);
    unsigned count = end - first;
    uint32_t total = __atomic_add_fetch(&dirty_line_marks_, count, __ATOMIC_ACQ_REL);
    if (total > max_lines_ * 2u) {
        request_dirty();
        return;
    }

    uint32_t version = next_dirty_version();
    for (unsigned line = first; line < end; ++line) {
        mark_line_dirty_with_version(line, version);
    }
    set_content_dirty();
}

uint32_t DloDirtyDisplay::line_dirty_version(unsigned line) const
{
    if (line >= max_lines_) {
        return 0;
    }
    return __atomic_load_n(&line_dirty_version_[line], __ATOMIC_ACQUIRE);
}

bool DloDirtyDisplay::is_line_dirty(unsigned page, unsigned line) const
{
    if (page >= kPageCount || line >= max_lines_) {
        return false;
    }

    uint32_t dirty_version = line_dirty_version(line);
    uint32_t page_version = __atomic_load_n(&line_page_version_[page][line], __ATOMIC_ACQUIRE);
    return dirty_version != page_version;
}

void DloDirtyDisplay::clear_line_dirty(unsigned page, unsigned line, uint32_t rendered_version)
{
    if (page >= kPageCount || line >= max_lines_) {
        return;
    }

    __atomic_store_n(&line_page_version_[page][line], rendered_version, __ATOMIC_RELEASE);
}

void DloDirtyDisplay::invalidate_page_hashes(unsigned page)
{
    if (page >= kPageCount) {
        return;
    }

    for (unsigned i = 0; i < max_lines_; ++i) {
        line_hash_[page][i] = 0;
        line_width_[page][i] = 0;
        __atomic_store_n(&line_page_version_[page][i], 0u, __ATOMIC_RELEASE);
    }
    page_generation[page] = 0;
}

void DloDirtyDisplay::invalidate_line_hashes()
{
    for (unsigned page = 0; page < kPageCount; ++page) {
        invalidate_page_hashes(page);
        page_content_valid[page] = false;
    }
    for (unsigned i = 0; i < max_lines_; ++i) {
        __atomic_store_n(&line_dirty_version_[i], 0u, __ATOMIC_RELEASE);
    }
}

void DloDirtyDisplay::mark_pages_clear_pending()
{
    for (unsigned page = 0; page < kPageCount; ++page) {
        page_clear_pending[page] = true;
        page_content_valid[page] = false;
        page_generation[page] = 0;
    }
}

void DloDirtyDisplay::begin_access(RenderContext *ctx)
{
    if (!ctx || ctx->access_open || !ctx->pGC) {
        return;
    }
    GCPBeginAccess(ctx->pGC);
    ctx->access_open = true;
}

void DloDirtyDisplay::end_access(RenderContext *ctx)
{
    if (!ctx || !ctx->access_open || !ctx->pGC) {
        return;
    }
    GCPEndAccess(ctx->pGC);
    ctx->access_open = false;
}

bool DloDirtyDisplay::set_draw_base(PGC pGC, uint32_t base)
{
    if (!pGC || !pGC->bitmap.handle || (base & 1u)) {
        return false;
    }
    GCSetDeviceBase(pGC, base);
    return true;
}

void DloDirtyDisplay::fill(RenderContext *ctx, long x, long y, long width, long height, GCCOLOR color)
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
    usb_pending = true;
}

bool DloDirtyDisplay::copy_from_base(RenderContext *ctx,
                                     unsigned long src_base,
                                     long src_x,
                                     long src_y,
                                     long width,
                                     long height,
                                     long dest_x,
                                     long dest_y,
                                     bool physical_coordinates)
{
    if (!ctx || !ctx->pGC || !ctx->pGC->bitmap.handle ||
        (src_base & 1u) || width <= 0 || height <= 0) {
        return false;
    }

    long saved_offset_x = ctx->pGC->offsetX;
    long saved_offset_y = ctx->pGC->offsetY;
    if (!physical_coordinates) {
        src_x += saved_offset_x;
        src_y += saved_offset_y;
    }

    GC srcGC;
    if (!GCCreateWithDeviceMemory(ctx->pGC, GCWidth(ctx->pGC), GCHeight(ctx->pGC), &srcGC)) {
        return false;
    }
    GCSetDeviceBase(&srcGC, src_base);

    GCRECT src = {src_x, src_y, src_x + width, src_y + height};
    GCPOINT dst = {dest_x, dest_y};
    begin_access(ctx);
    if (physical_coordinates && (saved_offset_x || saved_offset_y)) {
        GCSetOffset(ctx->pGC, 0, 0);
    }
    GCCopyBits2(ctx->pGC, &srcGC, &src, &dst);
    if (physical_coordinates && (saved_offset_x || saved_offset_y)) {
        GCSetOffset(ctx->pGC, saved_offset_x, saved_offset_y);
    }
    GCDelete(&srcGC);

    ctx->emitted = true;
    usb_pending = true;
    return true;
}

bool DloDirtyDisplay::copy_duplicate_rows(RenderContext *ctx,
                                          unsigned source_y,
                                          unsigned dest_y0,
                                          unsigned dest_y1,
                                          unsigned width)
{
    if (!ctx || !ctx->pGC || width == 0 || dest_y1 <= dest_y0) {
        return false;
    }

    bool emitted = false;
    for (unsigned y = dest_y0; y < dest_y1; ++y) {
        if (!copy_from_base(ctx,
                            GCDeviceBase(ctx->pGC),
                            (long)target_origin_x_px,
                            (long)source_y,
                            (long)width,
                            1,
                            (long)target_origin_x_px,
                            (long)y)) {
            if (emitted) {
                ctx->emitted = true;
                usb_pending = true;
            }
            return emitted;
        }
        emitted = true;
    }

    ctx->emitted = true;
    usb_pending = true;
    return true;
}

bool DloDirtyDisplay::copy_rect(RenderContext *ctx, unsigned src_y, unsigned dest_y, unsigned width, unsigned height)
{
    return copy_rect(ctx, 0, src_y, dest_y, width, height);
}

bool DloDirtyDisplay::copy_rect(RenderContext *ctx,
                                unsigned x,
                                unsigned src_y,
                                unsigned dest_y,
                                unsigned width,
                                unsigned height)
{
    if (!ctx || !ctx->pGC || width == 0 || height == 0) {
        return false;
    }

    return copy_from_base(ctx,
                          GCDeviceBase(ctx->pGC),
                          (long)x,
                          (long)src_y,
                          (long)width,
                          (long)height,
                          (long)x,
                          (long)dest_y);
}

bool DloDirtyDisplay::page_is_current(unsigned page) const
{
    return page < kPageCount &&
           page_content_valid[page] &&
           page_generation[page] == content_generation;
}

uint32_t DloDirtyDisplay::next_content_generation()
{
    ++content_generation;
    if (content_generation == 0) {
        ++content_generation;
    }
    return content_generation;
}

bool DloDirtyDisplay::clone_page(RenderContext *ctx, unsigned src_page, unsigned dest_page)
{
    if (!ctx || !ctx->pGC || src_page >= kPageCount || dest_page >= kPageCount ||
        src_page == dest_page || !page_content_valid[src_page] ||
        GCWidth(ctx->pGC) <= 0 || GCHeight(ctx->pGC) <= 0) {
        return false;
    }

    unsigned long old_base = GCDeviceBase(ctx->pGC);
    GCSetDeviceBase(ctx->pGC, page_base[dest_page]);
    bool copied = copy_from_base(ctx,
                                 page_base[src_page],
                                 0,
                                 0,
                                 GCWidth(ctx->pGC),
                                 GCHeight(ctx->pGC),
                                 0,
                                 0,
                                 true);
    GCSetDeviceBase(ctx->pGC, old_base);
    if (!copied) {
        return false;
    }

    std::memcpy(line_hash_[dest_page], line_hash_[src_page], sizeof(line_hash_[dest_page]));
    std::memcpy(line_width_[dest_page], line_width_[src_page], sizeof(line_width_[dest_page]));
    std::memcpy(line_page_version_[dest_page], line_page_version_[src_page], sizeof(line_page_version_[dest_page]));
    page_clear_pending[dest_page] = false;
    page_content_valid[dest_page] = true;
    page_generation[dest_page] = page_generation[src_page];
    ctx->emitted = true;
    usb_pending = true;
    return true;
}

unsigned DloDirtyDisplay::current_clone_source() const
{
    if (present_pending_flag &&
        pending_page < kPageCount &&
        page_is_current(pending_page)) {
        return pending_page;
    }
    if (page_is_current(visible_page)) {
        return visible_page;
    }
    for (unsigned page = 0; page < kPageCount; ++page) {
        if (page_is_current(page)) {
            return page;
        }
    }
    return kInvalidPage;
}

bool DloDirtyDisplay::valid_clone_source_available() const
{
    return current_clone_source() < kPageCount;
}

void DloDirtyDisplay::ensure_draw_page_ready_for_partial(RenderContext *ctx)
{
    if (!ctx || !ctx->pGC || render_full_frame || draw_page >= kPageCount) {
        return;
    }

    if (page_is_current(draw_page)) {
        draw_page_needs_clone = false;
        return;
    }

    unsigned src_page = current_clone_source();
    if (src_page < kPageCount && clone_page(ctx, src_page, draw_page)) {
        draw_page_needs_clone = false;
        return;
    }

    draw_page_needs_clone = false;
    invalidate_page_hashes(draw_page);
    page_content_valid[draw_page] = false;
    render_full_frame = true;
    render_full_frame_from_start = false;
    request_dirty();
}

bool DloDirtyDisplay::copy_line_raw(RenderContext *ctx, unsigned target_y0, unsigned target_y1, unsigned width)
{
    if (!ctx || !ctx->pGC || !line_buffer_ || width == 0 || target_y1 <= target_y0) {
        return false;
    }

    GC srcGC;
    if (!GCCreateWithPreallocatedMemory(ctx->pGC, (long)width, 1, line_buffer_, &srcGC)) {
        return false;
    }

    GCRECT src = {0, 0, (long)width, 1};
    GCPOINT dst = {(long)target_origin_x_px, (long)target_y0};
    begin_access(ctx);
    GCCopyBits2(ctx->pGC, &srcGC, &src, &dst);
    GCDelete(&srcGC);

    ctx->emitted = true;
    usb_pending = true;
    copy_duplicate_rows(ctx, target_y0, target_y0 + 1, target_y1, width);
    return true;
}

bool DloDirtyDisplay::setup_pages(PGC pGC)
{
    uint32_t frame_bytes;
    bool bases_match = true;

    if (!pGC || !pGC->bitmap.handle || GCWidth(pGC) <= 0 || GCHeight(pGC) <= 0) {
        pages_ready = false;
        return false;
    }

    frame_bytes = (uint32_t)GCDeviceFrameBytes(pGC);
    if (!frame_bytes) {
        pages_ready = false;
        return false;
    }

    for (unsigned page = 0; page < kPageCount; ++page) {
        if (page_base[page] != frame_bytes * page) {
            bases_match = false;
            break;
        }
    }

    if (pages_ready && bases_match) {
        return true;
    }

    for (unsigned page = 0; page < kPageCount; ++page) {
        page_base[page] = frame_bytes * page;
    }
    visible_page = 0;
    draw_page = 1;
    pending_page = kInvalidPage;
    present_pending_flag = false;
    draw_page_needs_clone = false;
    pages_ready =
        set_draw_base(pGC, page_base[0]) &&
        GCPresentDeviceDrawBase(pGC) &&
        set_draw_base(pGC, page_base[draw_page]);
    if (!pages_ready) {
        visible_page = 0;
        draw_page = 0;
        pending_page = kInvalidPage;
        present_pending_flag = false;
        draw_page_needs_clone = false;
        set_draw_base(pGC, page_base[0]);
    }
    invalidate_line_hashes();
    mark_pages_clear_pending();
    return pages_ready;
}

unsigned DloDirtyDisplay::choose_next_draw_page(unsigned fallback) const
{
    for (unsigned page = 0; page < kPageCount; ++page) {
        if (page == visible_page) {
            continue;
        }
        if (present_pending_flag && page == pending_page) {
            continue;
        }
        if (page_is_current(page)) {
            return page;
        }
    }

    for (unsigned page = 0; page < kPageCount; ++page) {
        if (page == visible_page) {
            continue;
        }
        if (present_pending_flag && page == pending_page) {
            continue;
        }
        return page;
    }
    return fallback;
}

void DloDirtyDisplay::clear_draw_page_if_needed(RenderContext *ctx, GCCOLOR clear_color)
{
    if (!ctx || !ctx->pGC || draw_page >= kPageCount || !page_clear_pending[draw_page]) {
        return;
    }

    page_clear_pending[draw_page] = false;
    long saved_offset_x = ctx->pGC->offsetX;
    long saved_offset_y = ctx->pGC->offsetY;
    if (saved_offset_x || saved_offset_y) {
        GCSetOffset(ctx->pGC, 0, 0);
    }
    fill(ctx, 0, 0, GCWidth(ctx->pGC), GCHeight(ctx->pGC), clear_color);
    if (saved_offset_x || saved_offset_y) {
        GCSetOffset(ctx->pGC, saved_offset_x, saved_offset_y);
    }
}

void DloDirtyDisplay::queue_frame(RenderContext *ctx)
{
    unsigned next_draw_page;
    unsigned queued_page;

    if (!ctx || !ctx->pGC || !ctx->pGC->bitmap.handle) {
        return;
    }
    end_access(ctx);
    if (!usb_pending) {
        ctx->emitted = false;
        return;
    }
    if (pages_ready &&
        drop_partial_full_frames &&
        render_full_frame &&
        !render_full_frame_from_start) {
        if (draw_page < kPageCount) {
            invalidate_page_hashes(draw_page);
            page_content_valid[draw_page] = false;
        }
        usb_pending = false;
        ctx->emitted = false;
        return;
    }

    if (pages_ready) {
        queued_page = draw_page;
        pending_page = queued_page;
        present_pending_flag = true;

        present_pending(ctx->pGC);
        if (!pages_ready) {
            pending_page = kInvalidPage;
            present_pending_flag = false;
            draw_page = visible_page;
            set_draw_base(ctx->pGC, page_base[draw_page]);
            usb_pending = false;
            ctx->emitted = false;
            render_full_frame_from_start = false;
            return;
        }
        if (queued_page < kPageCount && visible_page == queued_page) {
            page_generation[queued_page] = next_content_generation();
            page_content_valid[queued_page] = true;
        }

        next_draw_page = choose_next_draw_page(draw_page);

        if (next_draw_page < kPageCount) {
            draw_page = next_draw_page;
            if (!set_draw_base(ctx->pGC, page_base[draw_page])) {
                printf("%s: DisplayLink draw page switch failed\n", log_prefix_);
                pages_ready = false;
                present_pending_flag = false;
                pending_page = kInvalidPage;
            }
        }
    }
    usb_pending = false;
    ctx->emitted = false;
    render_full_frame_from_start = false;
}

void DloDirtyDisplay::present_pending(PGC pGC)
{
    if (!pGC || !pGC->bitmap.handle || !pages_ready ||
        !present_pending_flag || pending_page >= kPageCount) {
        return;
    }

    if (!GCPresentDeviceBase(pGC, page_base[pending_page])) {
        printf("%s: DisplayLink page present failed\n", log_prefix_);
        pages_ready = false;
        present_pending_flag = false;
        pending_page = kInvalidPage;
        draw_page = visible_page;
        set_draw_base(pGC, page_base[draw_page]);
        return;
    }

    visible_page = pending_page;
    pending_page = kInvalidPage;
    present_pending_flag = false;
}

bool DloDirtyDisplay::layout_changed(long gc_width, long gc_height, unsigned width, unsigned height) const
{
    return layout_changed(gc_width, gc_height, width, height, width, height);
}

bool DloDirtyDisplay::layout_changed(long gc_width,
                                     long gc_height,
                                     unsigned width,
                                     unsigned height,
                                     unsigned canvas_width,
                                     unsigned canvas_height) const
{
    return !initialized ||
           gc_width != last_gc_width ||
           gc_height != last_gc_height ||
           width != last_source_width ||
           height != last_source_height ||
           canvas_width != last_canvas_width ||
           canvas_height != last_canvas_height;
}

bool DloDirtyDisplay::update_window(RenderContext *ctx, unsigned width, unsigned height, GCCOLOR clear_color)
{
    return update_window(ctx, width, height, width, height, clear_color);
}

bool DloDirtyDisplay::update_window(RenderContext *ctx,
                                    unsigned width,
                                    unsigned height,
                                    unsigned new_canvas_width,
                                    unsigned new_canvas_height,
                                    GCCOLOR clear_color)
{
    long gc_width = ctx && ctx->pGC ? GCWidth(ctx->pGC) : 0;
    long gc_height = ctx && ctx->pGC ? GCHeight(ctx->pGC) : 0;

    if (!ctx || !ctx->pGC || width == 0 || height == 0 ||
        gc_width <= 0 || gc_height <= 0) {
        target_width_px = 0;
        target_height_px = 0;
        target_origin_x_px = 0;
        target_origin_y_px = 0;
        return false;
    }

    width = std::min<unsigned>(width, max_width_);
    height = std::min<unsigned>(height, max_lines_);
    new_canvas_width = std::min<unsigned>(new_canvas_width ? new_canvas_width : width, max_width_);
    new_canvas_height = std::min<unsigned>(new_canvas_height ? new_canvas_height : height, max_lines_);

    unsigned visible_canvas_width = std::min<unsigned>(new_canvas_width, (unsigned)gc_width);
    unsigned visible_canvas_height = std::min<unsigned>(new_canvas_height, (unsigned)gc_height);
    target_width_px = std::min<unsigned>(width, visible_canvas_width);
    target_height_px = std::min<unsigned>(height, visible_canvas_height);
    target_origin_x_px = (visible_canvas_width > target_width_px) ?
                         (visible_canvas_width - target_width_px) / 2u :
                         0u;
    target_origin_y_px = (visible_canvas_height > target_height_px) ?
                         (visible_canvas_height - target_height_px) / 2u :
                         0u;
    bool changed = layout_changed(gc_width,
                                  gc_height,
                                  width,
                                  height,
                                  new_canvas_width,
                                  new_canvas_height);

    if (!pages_ready || changed) {
        setup_pages(ctx->pGC);
    }

    if (!changed) {
        clear_draw_page_if_needed(ctx, clear_color);
        return false;
    }

    last_gc_width = gc_width;
    last_gc_height = gc_height;
    last_source_width = width;
    last_source_height = height;
    last_canvas_width = new_canvas_width;
    last_canvas_height = new_canvas_height;
    initialized = true;
    invalidate_line_hashes();
    mark_pages_clear_pending();
    clear_draw_page_if_needed(ctx, clear_color);
    return true;
}

void DloDirtyDisplay::draw_line(RenderContext *ctx,
                                unsigned buffer_line,
                                unsigned src_line,
                                unsigned src_span,
                                unsigned width,
                                uint32_t rendered_version)
{
    if (!ctx || !ctx->pGC || !line_buffer_ || !frame_valid ||
        buffer_line >= max_lines_ || width == 0 || src_line >= source_height) {
        return;
    }

    width = std::min<unsigned>(width, max_width_);
    if (!window_checked_for_frame) {
        update_window(ctx, source_width, source_height, canvas_width, canvas_height, RGB(0, 0, 0));
        window_checked_for_frame = true;
    }
    unsigned target_width = target_width_px;
    unsigned target_height = target_height_px;
    if (target_width == 0 || target_height == 0) {
        return;
    }

    unsigned source_end = std::min<unsigned>(src_line + std::max(1u, src_span), source_height);
    unsigned target_y0;
    unsigned target_y1;
    if (target_height == source_height) {
        target_y0 = src_line;
        target_y1 = source_end;
    } else {
        target_y0 = (src_line * target_height) / source_height;
        target_y1 = (source_end * target_height) / source_height;
    }
    if (target_y1 <= target_y0) {
        return;
    }
    target_y1 = std::min<unsigned>(target_y1, target_height);
    if (target_y0 >= target_y1) {
        return;
    }
    unsigned screen_y0 = target_origin_y_px + target_y0;
    unsigned screen_y1 = target_origin_y_px + target_y1;

    unsigned page = (draw_page < kPageCount) ? draw_page : 0u;
    uint32_t hash;
    unsigned fill_bytes = 0;
    unsigned raw_bytes = raw_line_bytes(target_width);
    bool fill_bytes_measured = false;
    if (render_full_frame) {
        if (line_width_[page][buffer_line] == target_width) {
            hash = (target_width == width) ?
                   measure_line_hash(line_buffer_, target_width) :
                   downscale_line_in_place(width, target_width);
        } else {
            LineMeasure measure = (target_width == width) ?
                                  measure_line_hash_and_fill(line_buffer_, target_width, raw_bytes) :
                                  downscale_line_in_place_and_measure(width, target_width, raw_bytes);
            hash = measure.hash;
            fill_bytes = measure.fill_bytes;
            fill_bytes_measured = true;
        }
    } else {
        LineMeasure measure = (target_width == width) ?
                              measure_line_hash_and_fill(line_buffer_, target_width, raw_bytes) :
                              downscale_line_in_place_and_measure(width, target_width, raw_bytes);
        hash = measure.hash;
        fill_bytes = measure.fill_bytes;
        fill_bytes_measured = true;
    }
    if (line_hash_[page][buffer_line] == hash && line_width_[page][buffer_line] == target_width) {
        clear_line_dirty(page, buffer_line, rendered_version);
        return;
    }

    if (src_line >= src_span && buffer_line > 0 &&
        line_hash_[page][buffer_line - 1u] == hash &&
        line_width_[page][buffer_line - 1u] == target_width) {
        unsigned prev_source_line = src_line - src_span;
        unsigned prev_target_y0;
        unsigned prev_target_y1;
        if (target_height == source_height) {
            prev_target_y0 = prev_source_line;
            prev_target_y1 = src_line;
        } else {
            prev_target_y0 = (prev_source_line * target_height) / source_height;
            prev_target_y1 = (src_line * target_height) / source_height;
        }
        unsigned target_height_px = target_y1 - target_y0;
        if (prev_target_y1 > prev_target_y0 &&
            prev_target_y1 - prev_target_y0 == target_height_px &&
            copy_rect(ctx,
                      target_origin_x_px,
                      target_origin_y_px + prev_target_y0,
                      screen_y0,
                      target_width,
                      target_height_px)) {
            line_hash_[page][buffer_line] = hash;
            line_width_[page][buffer_line] = (uint16_t)target_width;
            clear_line_dirty(page, buffer_line, rendered_version);
            return;
        }
    }

    if (!fill_bytes_measured) {
        fill_bytes = measure_line_fill_bytes(line_buffer_, target_width, raw_bytes);
    }
    line_hash_[page][buffer_line] = hash;
    line_width_[page][buffer_line] = (uint16_t)target_width;

    bool copy_duplicate = (target_y1 - target_y0) > 1 &&
                          fill_bytes > copy_row_bytes(target_width);
    unsigned fill_y1 = copy_duplicate ? screen_y0 + 1 : screen_y1;

    if (raw_bytes < fill_bytes && copy_line_raw(ctx, screen_y0, screen_y1, target_width)) {
        clear_line_dirty(page, buffer_line, rendered_version);
        return;
    }

    long run_start = 0;
    GCCOLOR run_color = line_buffer_[0];
    for (unsigned x = 1; x < target_width; ++x) {
        GCCOLOR color = line_buffer_[x];
        if (color == run_color) {
            continue;
        }
        fill(ctx,
             target_origin_x_px + run_start,
             screen_y0,
             (long)x - run_start,
             fill_y1 - screen_y0,
             run_color);
        run_start = x;
        run_color = color;
    }
    fill(ctx,
         target_origin_x_px + run_start,
         screen_y0,
         (long)target_width - run_start,
         fill_y1 - screen_y0,
         run_color);
    if (copy_duplicate) {
        copy_duplicate_rows(ctx, screen_y0, screen_y0 + 1, screen_y1, target_width);
    }
    clear_line_dirty(page, buffer_line, rendered_version);
}

bool DloDirtyDisplay::should_skip_line(unsigned line)
{
    if (!render_full_frame && full_render_dirty_pending()) {
        render_frame = true;
        render_full_frame = true;
        render_full_frame_from_start = false;
    }

    if (render_frame || !frame_valid || !initialized || !pages_ready) {
        if (!render_frame) {
            return false;
        }
    } else if (content_dirty_pending()) {
        render_frame = true;
        render_full_frame = full_render_dirty_pending();
    } else {
        return true;
    }

    if (render_full_frame && !render_full_frame_from_start) {
        return true;
    }

    if (render_full_frame) {
        return false;
    }

    unsigned page = (draw_page < kPageCount) ? draw_page : 0u;
    return !is_line_dirty(page, line);
}

void DloDirtyDisplay::publish_frame(int new_first_line,
                                    unsigned new_source_width,
                                    unsigned new_source_height,
                                    unsigned new_vertical_scale,
                                    unsigned new_canvas_width,
                                    unsigned new_canvas_height)
{
    new_canvas_width = new_canvas_width ? new_canvas_width : new_source_width;
    new_canvas_height = new_canvas_height ? new_canvas_height : new_source_height;
    bool was_valid = frame_valid;
    bool changed = new_source_width != source_width ||
                   new_source_height != source_height ||
                   new_canvas_width != canvas_width ||
                   new_canvas_height != canvas_height ||
                   new_first_line != first_line ||
                   new_vertical_scale != vertical_scale;
    bool dirty = take_content_dirty();
    bool full_dirty = take_full_render_dirty();
    clear_dirty_line_marks();
    bool draw_page_needs_current_contents =
        pages_ready &&
        draw_page < kPageCount &&
        !page_is_current(draw_page);

    if (changed) {
        invalidate_line_hashes();
        draw_page_needs_current_contents = false;
    }

    bool clone_source_available = valid_clone_source_available();
    bool draw_page_must_repaint = dirty &&
                                  draw_page_needs_current_contents &&
                                  !clone_source_available;

    first_line = new_first_line;
    source_width = new_source_width;
    source_height = new_source_height;
    canvas_width = new_canvas_width;
    canvas_height = new_canvas_height;
    vertical_scale = new_vertical_scale ? new_vertical_scale : 1u;
    window_checked_for_frame = false;
    frame_valid = true;
    render_full_frame = !was_valid || changed || full_dirty ||
                        !initialized || !pages_ready ||
                        draw_page_must_repaint;
    render_full_frame_from_start = render_full_frame;
    draw_page_needs_clone = !render_full_frame &&
                            dirty &&
                            draw_page_needs_current_contents &&
                            clone_source_available;
    render_frame = render_full_frame || dirty;
}

}  // namespace picograph
