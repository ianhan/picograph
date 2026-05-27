#pragma once

#include <cstdint>

extern "C" {
#include "gc.h"
}

namespace picograph {

class DloDirtyDisplay {
public:
    static constexpr unsigned kPageCount = 3;
    static constexpr unsigned kInvalidPage = kPageCount;
    static constexpr unsigned kMaxWidth = 1024;
    static constexpr unsigned kMaxLines = 512;

    struct RenderContext {
        PGC pGC;
        bool access_open;
        bool emitted;
    };

    void reset(unsigned max_width, unsigned max_lines, GCCOLOR *line_buffer, const char *log_prefix);
    void set_line_buffer(GCCOLOR *line_buffer);

    void request_dirty();
    void set_content_dirty();
    bool content_dirty_pending() const;
    bool full_render_dirty_pending() const;
    bool take_content_dirty();
    bool take_full_render_dirty();
    void clear_dirty_line_marks();

    void mark_line_dirty(unsigned line);
    void mark_line_range_dirty(unsigned first_line, unsigned end_line);
    uint32_t line_dirty_version(unsigned line) const;
    bool is_line_dirty(unsigned page, unsigned line) const;
    void clear_line_dirty(unsigned page, unsigned line, uint32_t rendered_version);

    void invalidate_line_hashes();
    void mark_pages_clear_pending();

    bool setup_pages(PGC pGC);
    bool page_is_current(unsigned page) const;
    bool valid_clone_source_available() const;
    void ensure_draw_page_ready_for_partial(RenderContext *ctx);
    void clear_draw_page_if_needed(RenderContext *ctx, GCCOLOR clear_color);
    void queue_frame(RenderContext *ctx);
    void present_pending(PGC pGC);
    void force_full_render_from_start();

    void begin_access(RenderContext *ctx);
    void end_access(RenderContext *ctx);
    void fill(RenderContext *ctx, long x, long y, long width, long height, GCCOLOR color);
    bool copy_from_base(RenderContext *ctx,
                        unsigned long src_base,
                        long src_x,
                        long src_y,
                        long width,
                        long height,
                        long dest_x,
                        long dest_y,
                        bool physical_coordinates = false);
    bool copy_duplicate_rows(RenderContext *ctx,
                             unsigned source_y,
                             unsigned dest_y0,
                             unsigned dest_y1,
                             unsigned width);
    bool copy_rect(RenderContext *ctx, unsigned src_y, unsigned dest_y, unsigned width, unsigned height);
    bool copy_rect(RenderContext *ctx,
                   unsigned x,
                   unsigned src_y,
                   unsigned dest_y,
                   unsigned width,
                   unsigned height);
    bool copy_line_raw(RenderContext *ctx, unsigned target_y0, unsigned target_y1, unsigned width);
    bool preserve_scroll_from_current(RenderContext *ctx, int shift_x, int shift_y);

    bool update_window(RenderContext *ctx, unsigned width, unsigned height, GCCOLOR clear_color);
    bool update_window(RenderContext *ctx,
                       unsigned width,
                       unsigned height,
                       unsigned canvas_width,
                       unsigned canvas_height,
                       GCCOLOR clear_color);
    void draw_line(RenderContext *ctx,
                   unsigned buffer_line,
                   unsigned source_line,
                   unsigned source_span,
                   unsigned width,
                   uint32_t rendered_version);
    bool draw_line_region(RenderContext *ctx,
                          unsigned buffer_line,
                          unsigned source_line,
                          unsigned source_span,
                          unsigned width,
                          unsigned region_x,
                          unsigned region_width);

    bool should_skip_line(unsigned line);
    void publish_frame(int new_first_line,
                       unsigned new_source_width,
                       unsigned new_source_height,
                       unsigned new_vertical_scale,
                       unsigned new_canvas_width = 0,
                       unsigned new_canvas_height = 0);

    unsigned max_width() const { return max_width_; }
    unsigned max_lines() const { return max_lines_; }

    int first_line = 0;
    unsigned source_width = 0;
    unsigned source_height = 0;
    unsigned canvas_width = 0;
    unsigned canvas_height = 0;
    unsigned vertical_scale = 1;
    unsigned target_width_px = 0;
    unsigned target_height_px = 0;
    unsigned target_origin_x_px = 0;
    unsigned target_origin_y_px = 0;
    bool frame_valid = false;
    bool initialized = false;
    bool window_checked_for_frame = false;
    bool render_frame = true;
    bool render_full_frame = true;
    bool render_full_frame_from_start = true;
    bool draw_page_needs_clone = false;
    bool drop_partial_full_frames = true;
    long last_gc_width = 0;
    long last_gc_height = 0;
    unsigned last_source_width = 0;
    unsigned last_source_height = 0;
    unsigned last_canvas_width = 0;
    unsigned last_canvas_height = 0;

    bool usb_pending = false;
    bool pages_ready = false;
    uint32_t page_base[kPageCount] = {};
    bool page_clear_pending[kPageCount] = {};
    bool page_content_valid[kPageCount] = {};
    uint32_t page_generation[kPageCount] = {};
    uint32_t content_generation = 1;
    bool present_pending_flag = false;
    unsigned visible_page = 0;
    unsigned pending_page = kInvalidPage;
    unsigned draw_page = 0;

private:
    struct LineMeasure {
        uint32_t hash;
        unsigned fill_bytes;
    };

    unsigned command_chunks(unsigned pixels) const;
    unsigned fill_run_bytes(unsigned pixels) const;
    unsigned copy_row_bytes(unsigned pixels) const;
    unsigned raw_line_bytes(unsigned pixels) const;
    unsigned line_transfer_bytes(const GCCOLOR *data, unsigned width, unsigned stop_bytes = 0) const;
    unsigned cap_fill_bytes(unsigned fill_bytes, unsigned fill_stop_bytes) const;
    uint32_t measure_line_hash(const GCCOLOR *data, unsigned width) const;
    LineMeasure measure_line_hash_and_fill(const GCCOLOR *data, unsigned width, unsigned fill_stop_bytes) const;
    void advance_scaled_sample(unsigned *sample_x, unsigned *error, unsigned source_width, unsigned target_width) const;
    uint32_t downscale_line_in_place(unsigned source_width, unsigned target_width);
    LineMeasure downscale_line_in_place_and_measure(unsigned source_width, unsigned target_width, unsigned fill_stop_bytes);
    unsigned measure_line_fill_bytes(const GCCOLOR *data, unsigned width, unsigned fill_stop_bytes) const;

    uint32_t next_dirty_version();
    void mark_line_dirty_with_version(unsigned line, uint32_t version);
    void invalidate_page_hashes(unsigned page);
    bool set_draw_base(PGC pGC, uint32_t base);
    uint32_t next_content_generation();
    bool clone_page(RenderContext *ctx, unsigned src_page, unsigned dest_page);
    unsigned current_clone_source() const;
    unsigned choose_next_draw_page(unsigned fallback) const;
    bool layout_changed(long gc_width, long gc_height, unsigned width, unsigned height) const;
    bool layout_changed(long gc_width,
                        long gc_height,
                        unsigned width,
                        unsigned height,
                        unsigned canvas_width,
                        unsigned canvas_height) const;

    GCCOLOR *line_buffer_ = nullptr;
    unsigned max_width_ = kMaxWidth;
    unsigned max_lines_ = kMaxLines;
    const char *log_prefix_ = "display";
    uint32_t line_hash_[kPageCount][kMaxLines] = {};
    uint16_t line_width_[kPageCount][kMaxLines] = {};
    uint32_t line_dirty_version_[kMaxLines] = {};
    uint32_t line_page_version_[kPageCount][kMaxLines] = {};
    volatile uint32_t line_dirty_next_version_ = 1;
    volatile uint32_t content_dirty_ = 1;
    volatile uint32_t full_render_dirty_ = 1;
    volatile uint32_t dirty_line_marks_ = 0;
};

}  // namespace picograph
