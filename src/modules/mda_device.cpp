#include "picomem/module.h"

#include <stdint.h>
#include <stdio.h>

#include "hardware/timer.h"
#include "pico/platform/sections.h"
#include "pico/stdlib.h"

#include "mda_font_rom.h"

extern "C" {
#include "gcdlo.h"
}

namespace picomem {
namespace {

#if !PICOMEM_ENABLE_DISPLAYLINK || !PICOMEM_ENABLE_GC
#error "The MDA module requires PICOMEM_ENABLE_DISPLAYLINK and PICOMEM_ENABLE_GC"
#endif

constexpr uint32_t kMdaMemBase = 0xb0000;
constexpr uint32_t kMdaMemSize = 0x8000;
constexpr uint16_t kMdaIoBase = 0x3b0;
constexpr uint16_t kMdaIoLength = 0x10;
constexpr unsigned kColumns = 80;
constexpr unsigned kRows = 25;
constexpr unsigned kCellCount = kColumns * kRows;
constexpr unsigned kNativeCellWidth = 9;
constexpr unsigned kGlyphWidth = 8;
constexpr unsigned kFontSlotHeight = 16;
constexpr unsigned kCrtcAddressMask = 0x3fff;
constexpr unsigned kMdaCellStorage = kMdaMemSize / 2;

#define DLO_HANDLE(bitmap) ((dlo_dev_t)(uintptr_t)((bitmap)->handle))

uint16_t mda_cells[kMdaCellStorage];
volatile uint8_t dirty_cells[kCellCount];
volatile uint32_t full_redraw_requests = 1;
volatile uint32_t attr_high_dirty_requests;

volatile uint8_t crtc_index;
volatile uint8_t crtc_regs[18];
volatile uint8_t mda_control_reg = 0x28;
volatile uint8_t page_reg;

bool display_initialized;
bool screen_dark;
long last_gc_width;
long last_gc_height;
long last_character_height;
long origin_x;
long origin_y;
long cell_width = kNativeCellWidth;
long cell_height = 14;
uint8_t last_blink_phase = 0xff;
uint32_t handled_full_redraw_request;
uint32_t handled_attr_high_dirty_request;

const GCCOLOR kTextPalette[16] = {
    RGB(0x00, 0x00, 0x00),
    RGB(0x00, 0x00, 0xaa),
    RGB(0x00, 0xaa, 0x00),
    RGB(0x00, 0xaa, 0xaa),
    RGB(0xaa, 0x00, 0x00),
    RGB(0xaa, 0x00, 0xaa),
    RGB(0xaa, 0x55, 0x00),
    RGB(0xaa, 0xaa, 0xaa),
    RGB(0x55, 0x55, 0x55),
    RGB(0x55, 0x55, 0xff),
    RGB(0x55, 0xff, 0x55),
    RGB(0x55, 0xff, 0xff),
    RGB(0xff, 0x55, 0x55),
    RGB(0xff, 0x55, 0xff),
    RGB(0xff, 0xff, 0x55),
    RGB(0xff, 0xff, 0xff),
};

void reset_crtc()
{
    crtc_regs[0] = 99;
    crtc_regs[1] = 80;
    crtc_regs[2] = 82;
    crtc_regs[3] = 12;
    crtc_regs[4] = 31;
    crtc_regs[5] = 1;
    crtc_regs[6] = 25;
    crtc_regs[7] = 27;
    crtc_regs[8] = 0;
    crtc_regs[9] = 13;
    crtc_regs[10] = 11;
    crtc_regs[11] = 12;
    crtc_regs[12] = 0;
    crtc_regs[13] = 0;
    crtc_regs[14] = 0;
    crtc_regs[15] = 92;
    crtc_regs[16] = 0;
    crtc_regs[17] = 0;
    crtc_index = 0;
}

uint16_t __time_critical_func(crtc_start_address)()
{
    return ((((uint16_t)crtc_regs[12] & 0x3f) << 8) | crtc_regs[13]) & kCrtcAddressMask;
}

uint16_t __time_critical_func(crtc_cursor_address)()
{
    return ((((uint16_t)crtc_regs[14] & 0x3f) << 8) | crtc_regs[15]) & kCrtcAddressMask;
}

unsigned __time_critical_func(character_height)()
{
    unsigned height = ((unsigned)crtc_regs[9] & 0x1fu) + 1u;
    if (height > kFontSlotHeight) {
        height = kFontSlotHeight;
    }
    return height;
}

void __time_critical_func(request_full_redraw)()
{
    __atomic_fetch_add(&full_redraw_requests, 1u, __ATOMIC_RELEASE);
}

void __time_critical_func(request_attr_high_dirty)()
{
    __atomic_fetch_add(&attr_high_dirty_requests, 1u, __ATOMIC_RELEASE);
}

void __time_critical_func(mark_cell_dirty)(unsigned cell)
{
    if (cell < kCellCount) {
        __atomic_store_n(&dirty_cells[cell], 1u, __ATOMIC_RELEASE);
    }
}

void mark_all_cells_dirty()
{
    for (unsigned i = 0; i < kCellCount; ++i) {
        mark_cell_dirty(i);
    }
}

bool __time_critical_func(visible_cell_from_address)(uint16_t cell_address, unsigned *cell)
{
    uint16_t visible = (cell_address - crtc_start_address()) & kCrtcAddressMask;
    if (visible >= kCellCount) {
        return false;
    }

    *cell = visible;
    return true;
}

void __time_critical_func(mark_visible_cell_dirty_from_address)(uint16_t cell_address)
{
    unsigned cell;
    if (visible_cell_from_address(cell_address, &cell)) {
        mark_cell_dirty(cell);
    }
}

void __time_critical_func(mark_visible_cell_dirty_from_offset)(uint32_t offset)
{
    uint16_t cell_address = (offset >> 1) & kCrtcAddressMask;
    mark_visible_cell_dirty_from_address(cell_address);
}

void mark_cursor_cell_dirty()
{
    mark_visible_cell_dirty_from_address(crtc_cursor_address());
}

bool visible_cell_uses_blink(unsigned cell)
{
    uint16_t cell_address = (crtc_start_address() + cell) & kCrtcAddressMask;
    uint16_t word = __atomic_load_n(&mda_cells[cell_address], __ATOMIC_ACQUIRE);
    return (word & 0x8000u) != 0;
}

void mark_attr_high_cells_dirty()
{
    for (unsigned cell = 0; cell < kCellCount; ++cell) {
        if (visible_cell_uses_blink(cell)) {
            mark_cell_dirty(cell);
        }
    }
}

void mark_blink_cells_dirty()
{
    if ((mda_control_reg & 0x20u) != 0) {
        mark_attr_high_cells_dirty();
    }
}

bool __time_critical_func(mda_mem_read)(uint32_t address, uint8_t *data)
{
    if (address < kMdaMemBase || address >= kMdaMemBase + kMdaMemSize) {
        return false;
    }

    uint32_t offset = address - kMdaMemBase;
    uint16_t word = __atomic_load_n(&mda_cells[offset >> 1], __ATOMIC_ACQUIRE);
    *data = (offset & 1u) ? (uint8_t)(word >> 8) : (uint8_t)word;
    return true;
}

void __time_critical_func(mda_mem_write)(uint32_t address, uint8_t data)
{
    if (address < kMdaMemBase || address >= kMdaMemBase + kMdaMemSize) {
        return;
    }

    uint32_t offset = address - kMdaMemBase;
    uint16_t old_word = __atomic_load_n(&mda_cells[offset >> 1], __ATOMIC_ACQUIRE);
    uint16_t new_word = (offset & 1u)
        ? (uint16_t)((old_word & 0x00ffu) | ((uint16_t)data << 8))
        : (uint16_t)((old_word & 0xff00u) | data);

    if (old_word == new_word) {
        return;
    }

    __atomic_store_n(&mda_cells[offset >> 1], new_word, __ATOMIC_RELEASE);
    mark_visible_cell_dirty_from_offset(offset);
}

void __time_critical_func(write_crtc_data)(uint8_t data)
{
    uint8_t index = crtc_index & 0x1f;
    if (index >= 18) {
        return;
    }

    uint16_t old_start = crtc_start_address();
    uint16_t old_cursor = crtc_cursor_address();

    switch (index) {
    case 3:
        data &= 0x0f;
        break;
    case 4:
    case 6:
    case 7:
    case 10:
        data &= 0x7f;
        break;
    case 5:
    case 9:
    case 11:
        data &= 0x1f;
        break;
    case 12:
    case 14:
        data &= 0x3f;
        break;
    default:
        break;
    }

    if (crtc_regs[index] != data) {
        crtc_regs[index] = data;
        switch (index) {
        case 9:
            request_full_redraw();
            break;
        case 10:
        case 11:
            mark_cursor_cell_dirty();
            break;
        case 12:
        case 13:
            if (old_start != crtc_start_address()) {
                request_full_redraw();
            }
            break;
        case 14:
        case 15:
            mark_visible_cell_dirty_from_address(old_cursor);
            mark_cursor_cell_dirty();
            break;
        default:
            break;
        }
    }
}

uint8_t __time_critical_func(read_crtc_data)()
{
    uint8_t index = crtc_index & 0x1f;
    if (index >= 18) {
        return 0;
    }

    return crtc_regs[index];
}

uint8_t __time_critical_func(read_status)()
{
    uint32_t now = timer_hw->timerawl;
    bool hsync = (now & 0x20u) != 0;
    bool video = (now & 0x08u) != 0;
    return 0xf0u | (video ? 0x08u : 0x00u) | (hsync ? 0x01u : 0x00u);
}

bool __time_critical_func(mda_io_read)(uint16_t port, uint8_t *data)
{
    uint8_t local = port & 0x0f;

    if (local <= 7 && (local & 1u)) {
        *data = read_crtc_data();
        return true;
    }

    if (local == 0x0a) {
        *data = read_status();
        return true;
    }

    return false;
}

void __time_critical_func(mda_io_write)(uint16_t port, uint8_t data)
{
    uint8_t local = port & 0x0f;

    if (local <= 7) {
        if (local & 1u) {
            write_crtc_data(data);
        } else {
            crtc_index = data & 0x1f;
        }
        return;
    }

    switch (local) {
    case 0x08:
        if (mda_control_reg != data) {
            uint8_t old_control = mda_control_reg;
            mda_control_reg = data;
            if ((old_control ^ data) & 0x08u) {
                request_full_redraw();
            } else if ((old_control ^ data) & 0x20u) {
                request_attr_high_dirty();
            }
        }
        break;
    case 0x09:
        page_reg = data;
        break;
    default:
        break;
    }
}

bool font_pixel(uint8_t ch, unsigned col, unsigned row)
{
    if (row >= kFontSlotHeight) {
        return false;
    }

    if (col >= kGlyphWidth) {
        return (ch & 0xe0u) == 0xc0u && font_pixel(ch, kGlyphWidth - 1, row);
    }

    uint16_t rom_address = (uint16_t)(((row & 0x08u) << 8) | ((uint16_t)ch << 3) | (row & 0x07u));
    uint8_t row_bits = kMdaFontRom[rom_address];
    return (row_bits & (0x80u >> col)) != 0;
}

uint8_t blink_phase()
{
    uint32_t now = time_us_32();
    return (uint8_t)(((now / 250000u) & 1u) | (((now / 500000u) & 1u) << 1));
}

bool cursor_visible_for_phase(uint8_t phase)
{
    uint8_t cursor_start = crtc_regs[10];
    uint8_t mode = cursor_start & 0x60u;
    if (mode == 0x20u) {
        return false;
    }

    if (mode == 0x00u) {
        return true;
    }

    return (phase & 0x01u) != 0;
}

bool cell_cursor_row(uint16_t cell_address, unsigned row, uint8_t phase)
{
    if (cell_address != crtc_cursor_address() || !cursor_visible_for_phase(phase)) {
        return false;
    }

    unsigned start = crtc_regs[10] & 0x1fu;
    unsigned end = crtc_regs[11] & 0x1fu;
    return row >= start && row <= end;
}

bool layout_changed(GC *pGC)
{
    return !display_initialized ||
           last_gc_width != GCWidth(pGC) ||
           last_gc_height != GCHeight(pGC) ||
           last_character_height != (long)character_height();
}

void update_layout(GC *pGC)
{
    long width = GCWidth(pGC);
    long height = GCHeight(pGC);
    long current_character_height = (long)character_height();
    long fit_cell_width = width / (long)kColumns;
    long fit_cell_height = height / (long)kRows;

    if (fit_cell_width < 1) {
        fit_cell_width = 1;
    }
    if (fit_cell_height < 1) {
        fit_cell_height = 1;
    }

    cell_width = fit_cell_width > (long)kNativeCellWidth ? (long)kNativeCellWidth : fit_cell_width;
    cell_height = fit_cell_height > current_character_height ? current_character_height : fit_cell_height;
    origin_x = (width - (long)kColumns * cell_width) / 2;
    origin_y = (height - (long)kRows * cell_height) / 2;
    if (origin_x < 0) {
        origin_x = 0;
    }
    if (origin_y < 0) {
        origin_y = 0;
    }

    last_gc_width = width;
    last_gc_height = height;
    last_character_height = current_character_height;
    display_initialized = true;
}

void draw_cell(GC *pGC, unsigned cell, uint8_t phase)
{
    uint16_t start = crtc_start_address();
    uint16_t cell_address = (start + cell) & kCrtcAddressMask;
    uint16_t word = __atomic_load_n(&mda_cells[cell_address], __ATOMIC_ACQUIRE);
    uint8_t ch = (uint8_t)word;
    uint8_t attr = (uint8_t)(word >> 8);
    bool blink_enabled = (mda_control_reg & 0x20u) != 0;
    uint8_t fg = attr & 0x0fu;
    uint8_t bg = blink_enabled ? ((attr >> 4) & 0x07u) : ((attr >> 4) & 0x0fu);
    bool nodisp = fg == 0 && bg == 0;
    bool blink_area = blink_enabled && (attr & 0x80u) && (phase & 0x02u);
    long cell_x = origin_x + (long)(cell % kColumns) * cell_width;
    long cell_y = origin_y + (long)(cell / kColumns) * cell_height;
    GCCOLOR fg_color = kTextPalette[fg];
    GCCOLOR bg_color = kTextPalette[bg];
    unsigned current_character_height = character_height();

    GCFastFill(pGC, cell_x, cell_y, cell_width, cell_height, bg_color);
    if (fg_color == bg_color) {
        return;
    }

    for (long dy = 0; dy < cell_height; ++dy) {
        unsigned src_row = (unsigned)(dy * (long)current_character_height / cell_height);
        bool underline = fg == 1u && src_row == 12u;
        bool cursor = cell_cursor_row(cell_address, src_row, phase);
        long run_start = -1;

        for (long dx = 0; dx <= cell_width; ++dx) {
            bool draw = false;
            if (dx < cell_width) {
                unsigned src_col = (unsigned)(dx * (long)kNativeCellWidth / cell_width);
                bool glyph = font_pixel(ch, src_col, src_row) || underline;
                draw = ((glyph && !nodisp && !blink_area) || cursor);
            }

            if (draw && run_start < 0) {
                run_start = dx;
            } else if (!draw && run_start >= 0) {
                GCFastFill(pGC, cell_x + run_start, cell_y + dy, dx - run_start, 1, fg_color);
                run_start = -1;
            }
        }
    }
}

void render_mda()
{
    GC *pGC = GCDisplay();
    if (!pGC || !pGC->bitmap.handle || GCWidth(pGC) <= 0 || GCHeight(pGC) <= 0) {
        return;
    }

    uint32_t redraw_requests = __atomic_load_n(&full_redraw_requests, __ATOMIC_ACQUIRE);
    uint32_t attr_dirty_requests = __atomic_load_n(&attr_high_dirty_requests, __ATOMIC_ACQUIRE);
    bool full_redraw = redraw_requests != handled_full_redraw_request;
    uint8_t phase = blink_phase();
    bool clear_screen = false;
    bool emitted = false;

    if (layout_changed(pGC)) {
        update_layout(pGC);
        full_redraw = true;
        clear_screen = true;
    }

    if (phase != last_blink_phase) {
        uint8_t previous_phase = last_blink_phase;
        uint8_t changed_phase = phase ^ previous_phase;
        last_blink_phase = phase;
        if (!full_redraw) {
            if ((changed_phase & 0x01u) &&
                cursor_visible_for_phase(previous_phase) != cursor_visible_for_phase(phase)) {
                mark_cursor_cell_dirty();
            }
            if (changed_phase & 0x02u) {
                mark_blink_cells_dirty();
            }
        }
    }

    if (full_redraw) {
        handled_full_redraw_request = redraw_requests;
        mark_all_cells_dirty();
        handled_attr_high_dirty_request = attr_dirty_requests;
    } else if (attr_dirty_requests != handled_attr_high_dirty_request) {
        handled_attr_high_dirty_request = attr_dirty_requests;
        mark_attr_high_cells_dirty();
    }

    GCPBeginAccess(pGC);
    if (clear_screen) {
        GCFastFill(pGC, 0, 0, GCWidth(pGC), GCHeight(pGC), RGB(0, 0, 0));
        screen_dark = true;
        emitted = true;
    }

    if ((mda_control_reg & 0x08u) == 0) {
        if (!screen_dark) {
            GCFastFill(pGC, 0, 0, GCWidth(pGC), GCHeight(pGC), RGB(0, 0, 0));
            screen_dark = true;
            emitted = true;
        }
        GCPEndAccess(pGC);
        if (emitted) {
            dlo_flush_usb(DLO_HANDLE(&pGC->bitmap), true);
        }
        return;
    }

    screen_dark = false;
    for (unsigned i = 0; i < kCellCount; ++i) {
        if (!__atomic_exchange_n(&dirty_cells[i], 0u, __ATOMIC_ACQ_REL)) {
            continue;
        }
        draw_cell(pGC, i, phase);
        emitted = true;
    }
    GCPEndAccess(pGC);

    if (emitted) {
        dlo_flush_usb(DLO_HANDLE(&pGC->bitmap), true);
    }
}

void init()
{
    reset_crtc();
    for (uint32_t i = 0; i < kMdaCellStorage; ++i) {
        mda_cells[i] = 0;
    }
    for (unsigned cell = 0; cell < kCellCount; ++cell) {
        mda_cells[cell] = 0x0700u | ' ';
    }
    mark_all_cells_dirty();
    request_full_redraw();

    printf("mda: active traps %05lx-%05lx and I/O %03x-%03x -> DisplayLink\n",
           (unsigned long)kMdaMemBase,
           (unsigned long)(kMdaMemBase + kMdaMemSize - 1u),
           kMdaIoBase,
           kMdaIoBase + kMdaIoLength - 1u);
}

void tick()
{
    render_mda();
}

IoTrap io_traps[] = {
    {
        kMdaIoBase,
        kMdaIoLength,
        true,
        mda_io_read,
        mda_io_write,
    },
};

MemTrap mem_traps[] = {
    {
        kMdaMemBase,
        kMdaMemSize,
        true,
        mda_mem_read,
        mda_mem_write,
    },
};

const Module module = {
    "mda-displaylink",
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

const Module &mda_module()
{
    return module;
}

}  // namespace picomem
