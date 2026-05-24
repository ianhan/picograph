#include "picomem/module.h"

#include <stdint.h>
#include <stdio.h>

extern "C" {
#include "gcdlo.h"
}

namespace picomem {
namespace {

#if !PICOMEM_ENABLE_DISPLAYLINK || !PICOMEM_ENABLE_GC
#error "The register view module requires PICOMEM_ENABLE_DISPLAYLINK and PICOMEM_ENABLE_GC"
#endif

#define DLO_HANDLE(bitmap) ((dlo_dev_t)(uintptr_t)((bitmap)->handle))

constexpr uint16_t kMcgaBase = 0x3c0;
constexpr uint16_t kMcgaLength = 0x20;
constexpr uint16_t kVgaMonoCrtcBase = 0x3b4;
constexpr uint16_t kVgaMonoCrtcLength = 0x2;
constexpr uint16_t kMcgaDacMask = 0x3c6;
constexpr uint16_t kMcgaDacWriteIndex = 0x3c8;
constexpr uint16_t kMcgaDacData = 0x3c9;
constexpr uint16_t kPostCode = 0x80;
constexpr uint16_t kKeyboardData = 0x60;
constexpr uint8_t kKeyboardSetLeds = 0xed;
constexpr uint8_t kCapsLockLed = 0x04;

uint32_t syspalette2[256];
uint32_t syspalette[256];
uint32_t palette_data;
uint8_t palette_index;
uint8_t palette_position;
uint8_t palette_mask = 0xff;
void pal_dac_mask(uint8_t mask)
{
    palette_mask = mask;
}

void pal_dac_write(uint8_t index)
{
    palette_index = index;
    palette_position = 0;
}
#define PAL6_TO_8(d) ((((uint32_t)d * 259 + 33) >> 6) & 0xFF)
void pal_dac_data(uint8_t data)
{
    switch(palette_position)
    {
        case 0:
            palette_data = ((data * 259 + 33) >> 6) & 0xFF;
            palette_position++;
            break;
        case 1:
            palette_data |= (((data * 259 + 33) >> 6) & 0xFF) << 8;
            palette_position++;
            break;
        case 2:
            palette_position = 0;
            syspalette[palette_index] = palette_data | PAL6_TO_8(data) << 16;
            palette_index++;
            break;
    }
}

uint8_t post_code;
uint8_t post_code_current;
uint8_t post_code_history[4];
uint8_t post_code_history_count;
uint8_t post_code_history_next;
bool post_code_dirty = true;
void post_code_write(uint8_t value)
{
    post_code = post_code_current;
    post_code_current = value;
    post_code_history[post_code_history_next] = value;
    post_code_history_next = (post_code_history_next + 1) & 0x03;
    if (post_code_history_count < 4) {
        post_code_history_count++;
    }
    post_code_dirty = true;
}

enum {
    MCGA_ATTR_REGS = 0x15,
    MCGA_SEQ_REGS = 0x08,
    MCGA_GDC_REGS = 0x09,
    MCGA_CRTC_REGS = 0x40
};

typedef enum McgaRegisterKind {
    MCGA_ROW_DIRECT,
    MCGA_ROW_ATTR,
    MCGA_ROW_SEQ,
    MCGA_ROW_GDC,
    MCGA_ROW_VGA_CRTC,
    MCGA_ROW_CRTC,
    MCGA_ROW_MFGREG
} McgaRegisterKind;

typedef enum McgaRegisterSource {
    MCGA_SOURCE_VGA,
    MCGA_SOURCE_MCGA
} McgaRegisterSource;

typedef struct McgaRegisterRow {
    McgaRegisterKind kind;
    McgaRegisterSource source;
    uint8_t pages;
    uint16_t port;
    uint8_t index;
    const char *name;
} McgaRegisterRow;

enum {
    MCGA_REGISTER_PAGE_MCGA = 0,
    MCGA_REGISTER_PAGE_VGA = 1,
    MCGA_REGISTER_PAGE_COUNT = 2,
    MCGA_ROW_PAGE_MCGA = 1u << MCGA_REGISTER_PAGE_MCGA,
    MCGA_ROW_PAGE_VGA = 1u << MCGA_REGISTER_PAGE_VGA,
    MCGA_ROW_PAGE_ALL = MCGA_ROW_PAGE_MCGA | MCGA_ROW_PAGE_VGA
};

static const McgaRegisterRow mcga_register_rows[] = {
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_ALL,  0x3c2, 0x00, "Misc output"},
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_ALL,  0x3c6, 0x00, "DAC pixel mask"},
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_ALL,  0x3c9, 0x00, "DAC data last"},
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d8, 0x00, "CGA mode"},
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d9, 0x00, "Border/color"},
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3dd, 0x00, "Extended mode"},
    {MCGA_ROW_DIRECT,   MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3df, 0x04, "MFG data[04]"},

    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x00, "Palette 00"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x01, "Palette 01"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x02, "Palette 02"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x03, "Palette 03"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x04, "Palette 04"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x05, "Palette 05"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x06, "Palette 06"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x07, "Palette 07"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x08, "Palette 08"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x09, "Palette 09"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x0a, "Palette 0A"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x0b, "Palette 0B"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x0c, "Palette 0C"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x0d, "Palette 0D"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x0e, "Palette 0E"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x0f, "Palette 0F"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x10, "ATTR mode"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x11, "ATTR overscan"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x12, "ATTR plane en"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x13, "ATTR pel pan"},
    {MCGA_ROW_ATTR,     MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c0, 0x14, "ATTR color sel"},

    {MCGA_ROW_SEQ,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c4, 0x01, "SEQ clock"},
    {MCGA_ROW_SEQ,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c4, 0x02, "SEQ map mask"},
    {MCGA_ROW_SEQ,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c4, 0x03, "SEQ char map"},
    {MCGA_ROW_SEQ,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3c4, 0x04, "SEQ memory"},

    {MCGA_ROW_GDC,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3ce, 0x00, "Set/reset"},
    {MCGA_ROW_GDC,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3ce, 0x01, "Enable SR"},
    {MCGA_ROW_GDC,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3ce, 0x03, "Data rotate"},
    {MCGA_ROW_GDC,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3ce, 0x05, "GDC mode"},
    {MCGA_ROW_GDC,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3ce, 0x06, "GDC misc"},
    {MCGA_ROW_GDC,      MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3ce, 0x08, "Bit mask"},

    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x00, "HTOT"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x01, "HCHAR"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x02, "HSYNCS"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x03, "HSYNCW"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x04, "VTOT"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x05, "VTOTADJ"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x06, "VCHAR"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x07, "VSYNCS"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x09, "CHARLINES"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x0a, "CURSTART"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x0b, "CUREND"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x0c, "SOS hi"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x0d, "SOS lo"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x0e, "CPOS hi"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x0f, "CPOS lo"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x10, "MODECONT"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x11, "INTCON"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x12, "CHARGEN"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x13, "CHARFONT"},
    {MCGA_ROW_CRTC,     MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x14, "NUMCHARS"},
    {MCGA_ROW_MFGREG,   MCGA_SOURCE_MCGA, MCGA_ROW_PAGE_MCGA, 0x3d4, 0x20, "8300 MFGREG"},

    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x00, "HTOT"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x01, "HDISP"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x02, "HBLANK start"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x03, "HBLANK end"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x04, "HRETRACE st"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x05, "HRETRACE end"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x06, "VTOT"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x07, "Overflow"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x08, "Preset row"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x09, "Max scanline"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x0a, "Cursor start"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x0b, "Cursor end"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x0c, "Start addr hi"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x0d, "Start addr lo"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x0e, "Cursor loc hi"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x0f, "Cursor loc lo"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x10, "VRETRACE st"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x11, "VRETRACE end"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x12, "VDISP end"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x13, "Offset"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x14, "Underline"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x15, "VBLANK start"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x16, "VBLANK end"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x17, "CRTC mode"},
    {MCGA_ROW_VGA_CRTC, MCGA_SOURCE_VGA,  MCGA_ROW_PAGE_VGA,  0x3d4, 0x18, "Line compare"}
};

enum {
    MCGA_REGISTER_ROW_COUNT = sizeof(mcga_register_rows) / sizeof(mcga_register_rows[0])
};

typedef enum McgaRegisterGroup {
    MCGA_GROUP_DIRECT,
    MCGA_GROUP_ATTR,
    MCGA_GROUP_SEQ,
    MCGA_GROUP_GDC,
    MCGA_GROUP_CRTC
} McgaRegisterGroup;

static uint8_t mcga_dac_mask;
static bool mcga_dac_mask_valid;
static uint8_t mcga_dac_read_index;
static bool mcga_dac_read_index_valid;
static uint8_t mcga_dac_write_index;
static bool mcga_dac_write_index_valid;
static uint8_t mcga_dac_data_last;
static bool mcga_dac_data_last_valid;
static uint8_t mcga_dac_phase;
static uint8_t mcga_crtc_index;
static bool mcga_crtc_index_valid;
static uint8_t vga_attr_index;
static bool vga_attr_index_valid;
static bool vga_attr_expect_data;
static uint8_t vga_attr_regs[MCGA_ATTR_REGS];
static bool vga_attr_valid[MCGA_ATTR_REGS];
static uint8_t vga_misc_output;
static bool vga_misc_output_valid;
static uint8_t vga_seq_index;
static bool vga_seq_index_valid;
static uint8_t vga_seq_regs[MCGA_SEQ_REGS];
static bool vga_seq_valid[MCGA_SEQ_REGS];
static uint8_t vga_gdc_index;
static bool vga_gdc_index_valid;
static uint8_t vga_gdc_regs[MCGA_GDC_REGS];
static bool vga_gdc_valid[MCGA_GDC_REGS];
static uint8_t vga_crtc_regs[MCGA_CRTC_REGS];
static bool vga_crtc_valid[MCGA_CRTC_REGS];
static uint8_t mcga_crtc_regs[MCGA_CRTC_REGS];
static bool mcga_crtc_valid[MCGA_CRTC_REGS];
static uint8_t mcga_8300_mfgreg;
static bool mcga_8300_mfgreg_valid;
static uint8_t mcga_cgamode;
static bool mcga_cgamode_valid;
static uint8_t mcga_border;
static bool mcga_border_valid;
static uint8_t mcga_extmode;
static bool mcga_extmode_valid;
static uint8_t mcga_formatter_mfg_addr;
static bool mcga_formatter_mfg_addr_valid;
static uint8_t mcga_formatter_mfg_ctrl;
static bool mcga_formatter_mfg_ctrl_valid;
static bool mcga_register_dirty = true;
static bool mcga_register_row_dirty[MCGA_REGISTER_ROW_COUNT];
static bool mcga_register_row_drawn[MCGA_REGISTER_ROW_COUNT];
static bool mcga_mode_dirty = true;
static bool mcga_overlay_initialized;
static long mcga_overlay_x;
static long mcga_overlay_y;
static long mcga_overlay_width;
static long mcga_overlay_height;
static uint8_t mcga_register_page = MCGA_REGISTER_PAGE_MCGA;
static bool mcga_overlay_force_redraw;

static bool mcga_is_mode_register(McgaRegisterKind kind, uint16_t port, uint8_t index)
{
    if (kind == MCGA_ROW_DIRECT) {
        return port == 0x3c2 || port == 0x3d8 || port == 0x3dd;
    }
    if (kind == MCGA_ROW_CRTC) {
        return index == 0x01 || index == 0x07 || index == 0x09 || index == 0x10 ||
               index == 0x12 || index == 0x13 || index == 0x14;
    }
    if (kind == MCGA_ROW_VGA_CRTC) {
        return index == 0x01 || index == 0x07 || index == 0x09 || index == 0x12 ||
               index == 0x13 || index == 0x17;
    }
    if (kind == MCGA_ROW_ATTR) {
        return index == 0x10 || index == 0x13;
    }
    if (kind == MCGA_ROW_SEQ) {
        return index == 0x01 || index == 0x04;
    }
    if (kind == MCGA_ROW_GDC) {
        return index == 0x05 || index == 0x06;
    }
    return false;
}

static void mcga_mark_register_dirty(McgaRegisterKind kind, uint16_t port, uint8_t index)
{
    for (unsigned i = 0; i < MCGA_REGISTER_ROW_COUNT; ++i) {
        const McgaRegisterRow *row = &mcga_register_rows[i];
        if (row->kind == kind && row->port == port && row->index == index) {
            mcga_register_row_dirty[i] = true;
            mcga_register_dirty = true;
            break;
        }
    }

    if (mcga_is_mode_register(kind, port, index)) {
        mcga_mode_dirty = true;
        mcga_register_dirty = true;
    }
}

static void mcga_mark_all_register_rows_dirty(void)
{
    for (unsigned i = 0; i < MCGA_REGISTER_ROW_COUNT; ++i) {
        mcga_register_row_dirty[i] = true;
        mcga_register_row_drawn[i] = false;
    }
    mcga_mode_dirty = true;
    mcga_register_dirty = true;
}

static void mcga_store_direct(uint16_t port, uint8_t index, uint8_t *reg, bool *valid, uint8_t value)
{
    if (!*valid || *reg != value) {
        *reg = value;
        *valid = true;
        mcga_mark_register_dirty(MCGA_ROW_DIRECT, port, index);
    }
}

static void mcga_store_indexed(McgaRegisterKind kind,
                               uint16_t port,
                               uint8_t index,
                               uint8_t *regs,
                               bool *valid,
                               unsigned count,
                               uint8_t value)
{
    if (index >= count) {
        return;
    }

    if (!valid[index] || regs[index] != value) {
        regs[index] = value;
        valid[index] = true;
        mcga_mark_register_dirty(kind, port, index);
    }
}

static void mcga_store_crtc(uint8_t index, uint8_t value)
{
    if (index >= MCGA_CRTC_REGS) {
        return;
    }

    if (!mcga_crtc_valid[index] || mcga_crtc_regs[index] != value) {
        mcga_crtc_regs[index] = value;
        mcga_crtc_valid[index] = true;
        mcga_mark_register_dirty(MCGA_ROW_CRTC, 0x3d4, index);
    }
}

static void mcga_store_vga_crtc(uint8_t index, uint8_t value)
{
    if (index >= MCGA_CRTC_REGS) {
        return;
    }

    if (!vga_crtc_valid[index] || vga_crtc_regs[index] != value) {
        vga_crtc_regs[index] = value;
        vga_crtc_valid[index] = true;
        mcga_mark_register_dirty(MCGA_ROW_VGA_CRTC, 0x3d4, index);
    }
}

static void mcga_store_8300_mfgreg(uint8_t value)
{
    if (!mcga_8300_mfgreg_valid || mcga_8300_mfgreg != value) {
        mcga_8300_mfgreg = value;
        mcga_8300_mfgreg_valid = true;
        mcga_mark_register_dirty(MCGA_ROW_MFGREG, 0x3d4, 0x20);
    }
}

static void mcga_store_crtc_data(uint8_t index, uint8_t value)
{
    uint8_t canonical = index;
    uint8_t stored = value;
    bool writable = true;

    mcga_store_vga_crtc(index, value);

    if (index & 0x20) {
        mcga_store_8300_mfgreg(value);
    }

    switch (index) {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x06:
    case 0x07:
    case 0x09:
    case 0x0c:
    case 0x0d:
    case 0x0f:
    case 0x10:
    case 0x12:
    case 0x13:
    case 0x14:
        break;
    case 0x05:
        stored = value & 0x3f;
        break;
    case 0x0b:
    case 0x0e:
        stored = value & 0x0f;
        break;
    case 0x0a:
        stored = (value & 0x20) | (value & 0x0f);
        break;
    case 0x11:
        stored = value & 0xb0;
        break;
    case 0x30:
    case 0x32:
    case 0x33:
    case 0x34:
        canonical = index - 0x20;
        break;
    case 0x31:
        canonical = 0x11;
        stored = value & 0xb0;
        break;
    default:
        writable = false;
        break;
    }

    if (writable) {
        mcga_store_crtc(canonical, stored);
    }
}

void mcga_set_register_page(uint8_t page)
{
    if (page >= MCGA_REGISTER_PAGE_COUNT || mcga_register_page == page) {
        return;
    }

    mcga_register_page = page;
    mcga_overlay_force_redraw = true;
    mcga_mark_all_register_rows_dirty();
}

void mcga_register_write(uint16_t port, uint8_t value)
{
    switch (port) {
    case 0x3c0:
        if (!vga_attr_expect_data) {
            vga_attr_expect_data = true;
            mcga_store_direct(0x3c0, 0x00, &vga_attr_index, &vga_attr_index_valid, value & 0x1f);
        } else {
            vga_attr_expect_data = false;
            if (vga_attr_index_valid) {
                mcga_store_indexed(MCGA_ROW_ATTR, 0x3c0, vga_attr_index & 0x1f,
                                   vga_attr_regs, vga_attr_valid, MCGA_ATTR_REGS, value);
            }
        }
        break;
    case 0x3c2:
        mcga_store_direct(0x3c2, 0x00, &vga_misc_output, &vga_misc_output_valid, value);
        break;
    case 0x3c4:
        mcga_store_direct(0x3c4, 0x00, &vga_seq_index, &vga_seq_index_valid, value & 0x07);
        break;
    case 0x3c5:
        if (vga_seq_index_valid) {
            mcga_store_indexed(MCGA_ROW_SEQ, 0x3c4, vga_seq_index & 0x07,
                               vga_seq_regs, vga_seq_valid, MCGA_SEQ_REGS, value);
        }
        break;
    case 0x3c6:
        mcga_store_direct(0x3c6, 0x00, &mcga_dac_mask, &mcga_dac_mask_valid, value);
        break;
    case 0x3c7:
        mcga_dac_phase = 0;
        mcga_store_direct(0x3c7, 0x00, &mcga_dac_read_index, &mcga_dac_read_index_valid, value);
        break;
    case 0x3c8:
        mcga_dac_phase = 0;
        mcga_store_direct(0x3c8, 0x00, &mcga_dac_write_index, &mcga_dac_write_index_valid, value);
        break;
    case 0x3c9:
        mcga_store_direct(0x3c9, 0x00, &mcga_dac_data_last, &mcga_dac_data_last_valid, value);
        if (mcga_dac_phase == 2) {
            mcga_dac_phase = 0;
            if (mcga_dac_write_index_valid) {
                uint8_t next_index = mcga_dac_write_index + 1u;
                mcga_store_direct(0x3c8, 0x00, &mcga_dac_write_index, &mcga_dac_write_index_valid, next_index);
            }
        } else {
            mcga_dac_phase++;
        }
        break;
    case 0x3ce:
        mcga_store_direct(0x3ce, 0x00, &vga_gdc_index, &vga_gdc_index_valid, value & 0x0f);
        break;
    case 0x3cf:
        if (vga_gdc_index_valid) {
            mcga_store_indexed(MCGA_ROW_GDC, 0x3ce, vga_gdc_index & 0x0f,
                               vga_gdc_regs, vga_gdc_valid, MCGA_GDC_REGS, value);
        }
        break;
    case 0x3b4:
    case 0x3d4:
        mcga_store_direct(0x3d4, 0x00, &mcga_crtc_index, &mcga_crtc_index_valid, value & 0x3f);
        break;
    case 0x3b5:
    case 0x3d5:
        if (mcga_crtc_index_valid) {
            mcga_store_crtc_data(mcga_crtc_index & 0x3f, value);
        }
        break;
    case 0x3d8:
        mcga_store_direct(0x3d8, 0x00, &mcga_cgamode, &mcga_cgamode_valid, value & 0x3f);
        break;
    case 0x3d9:
        mcga_store_direct(0x3d9, 0x00, &mcga_border, &mcga_border_valid, value & 0x3f);
        break;
    case 0x3dd:
        mcga_store_direct(0x3dd, 0x00, &mcga_extmode, &mcga_extmode_valid, value & 0x87);
        break;
    case 0x3de:
        mcga_store_direct(0x3de, 0x00, &mcga_formatter_mfg_addr, &mcga_formatter_mfg_addr_valid, value & 0x07);
        break;
    case 0x3df:
        if (mcga_formatter_mfg_addr_valid && mcga_formatter_mfg_addr == 4) {
            mcga_store_direct(0x3df, 0x04, &mcga_formatter_mfg_ctrl, &mcga_formatter_mfg_ctrl_valid, value & 0x03);
        }
        break;
    default:
        break;
    }
}

static bool mcga_get_register(const McgaRegisterRow *row, uint8_t *value)
{
    switch (row->kind) {
    case MCGA_ROW_ATTR:
        if (row->index < MCGA_ATTR_REGS && vga_attr_valid[row->index]) {
            *value = vga_attr_regs[row->index];
            return true;
        }
        break;
    case MCGA_ROW_SEQ:
        if (row->index < MCGA_SEQ_REGS && vga_seq_valid[row->index]) {
            *value = vga_seq_regs[row->index];
            return true;
        }
        break;
    case MCGA_ROW_GDC:
        if (row->index < MCGA_GDC_REGS && vga_gdc_valid[row->index]) {
            *value = vga_gdc_regs[row->index];
            return true;
        }
        break;
    case MCGA_ROW_VGA_CRTC:
        if (row->index < MCGA_CRTC_REGS && vga_crtc_valid[row->index]) {
            *value = vga_crtc_regs[row->index];
            return true;
        }
        break;
    case MCGA_ROW_CRTC:
        if (row->index < MCGA_CRTC_REGS && mcga_crtc_valid[row->index]) {
            *value = mcga_crtc_regs[row->index];
            return true;
        }
        break;
    case MCGA_ROW_MFGREG:
        if (mcga_8300_mfgreg_valid) {
            *value = mcga_8300_mfgreg;
            return true;
        }
        break;
    case MCGA_ROW_DIRECT:
        switch (row->port) {
        case 0x3c0:
            if (vga_attr_index_valid) {
                *value = vga_attr_index;
                return true;
            }
            break;
        case 0x3c2:
            if (vga_misc_output_valid) {
                *value = vga_misc_output;
                return true;
            }
            break;
        case 0x3c4:
            if (vga_seq_index_valid) {
                *value = vga_seq_index;
                return true;
            }
            break;
        case 0x3c6:
            if (mcga_dac_mask_valid) {
                *value = mcga_dac_mask;
                return true;
            }
            break;
        case 0x3c7:
            if (mcga_dac_read_index_valid) {
                *value = mcga_dac_read_index;
                return true;
            }
            break;
        case 0x3c8:
            if (mcga_dac_write_index_valid) {
                *value = mcga_dac_write_index;
                return true;
            }
            break;
        case 0x3c9:
            if (mcga_dac_data_last_valid) {
                *value = mcga_dac_data_last;
                return true;
            }
            break;
        case 0x3ce:
            if (vga_gdc_index_valid) {
                *value = vga_gdc_index;
                return true;
            }
            break;
        case 0x3d4:
            if (mcga_crtc_index_valid) {
                *value = mcga_crtc_index;
                return true;
            }
            break;
        case 0x3d8:
            if (mcga_cgamode_valid) {
                *value = mcga_cgamode;
                return true;
            }
            break;
        case 0x3d9:
            if (mcga_border_valid) {
                *value = mcga_border;
                return true;
            }
            break;
        case 0x3dd:
            if (mcga_extmode_valid) {
                *value = mcga_extmode;
                return true;
            }
            break;
        case 0x3de:
            if (mcga_formatter_mfg_addr_valid) {
                *value = mcga_formatter_mfg_addr;
                return true;
            }
            break;
        case 0x3df:
            if (mcga_formatter_mfg_ctrl_valid) {
                *value = mcga_formatter_mfg_ctrl;
                return true;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return false;
}

typedef struct McgaModeInfo {
    bool valid;
    const char *mode;
    uint16_t colors;
    uint32_t xres;
    uint32_t yres;
    uint32_t vxres;
    uint32_t vyres;
    uint32_t xpages10;
    uint32_t ypages10;
} McgaModeInfo;

static uint32_t mcga_vga_visible_scanlines(void)
{
    uint32_t overflow = vga_crtc_regs[0x07];
    return (uint32_t)vga_crtc_regs[0x12] +
           ((overflow & 0x02u) << 7) +
           ((overflow & 0x40u) << 3) +
           1u;
}

static uint32_t mcga_vga_scanline_rows(void)
{
    uint32_t max_scan = vga_crtc_regs[0x09];
    uint32_t rows = (max_scan & 0x1fu) + 1u;

    if (max_scan & 0x80u) {
        rows <<= 1;
    }
    return rows;
}

static bool mcga_try_decode_vga_mode(McgaModeInfo *info)
{
    bool have_crtc_geometry = vga_crtc_valid[0x01] &&
                              vga_crtc_valid[0x07] &&
                              vga_crtc_valid[0x09] &&
                              vga_crtc_valid[0x12];
    bool have_vga_mode_regs = vga_gdc_valid[0x05] ||
                              vga_gdc_valid[0x06] ||
                              vga_attr_valid[0x10] ||
                              vga_seq_valid[0x01] ||
                              vga_seq_valid[0x04] ||
                              vga_misc_output_valid;
    bool graphics_mode;

    if (!have_crtc_geometry || !have_vga_mode_regs) {
        return false;
    }

    if (vga_gdc_valid[0x06]) {
        graphics_mode = (vga_gdc_regs[0x06] & 0x01u) != 0;
    } else if (vga_attr_valid[0x10]) {
        graphics_mode = (vga_attr_regs[0x10] & 0x01u) != 0;
    } else {
        return false;
    }

    uint32_t h_clocks = (uint32_t)vga_crtc_regs[0x01] + 1u;
    uint32_t visible_scanlines = mcga_vga_visible_scanlines();
    uint32_t scanline_rows = mcga_vga_scanline_rows();

    if (h_clocks == 0 || visible_scanlines == 0 || scanline_rows == 0) {
        info->mode = "invalid VGA decode";
        return true;
    }

    if (!graphics_mode) {
        uint32_t char_width = 9u;
        uint32_t text_rows = visible_scanlines / scanline_rows;

        if (vga_seq_valid[0x01] && (vga_seq_regs[0x01] & 0x01u)) {
            char_width = 8u;
        }

        info->colors = 16;
        info->xres = h_clocks * char_width;
        info->yres = visible_scanlines;
        info->vxres = h_clocks;
        info->vyres = text_rows;
        if (h_clocks == 80u) {
            info->mode = "VGA 80-column text";
        } else if (h_clocks == 40u) {
            info->mode = "VGA 40-column text";
        } else {
            info->mode = "VGA text";
        }
        info->xpages10 = 10;
        info->ypages10 = text_rows ? 10 : 0;
        info->valid = true;
        return true;
    }

    if (!vga_gdc_valid[0x05]) {
        return false;
    }

    uint32_t mode_bits = (vga_gdc_regs[0x05] >> 5) & 0x03u;
    uint32_t h_pixels_per_clock;
    uint32_t bytes_per_line;
    bool chain4 = vga_seq_valid[0x04] && ((vga_seq_regs[0x04] & 0x08u) != 0);

    switch (mode_bits) {
    case 0:
        info->colors = 16;
        h_pixels_per_clock = 8u;
        break;
    case 1:
        info->colors = 4;
        h_pixels_per_clock = 16u;
        break;
    case 2:
        info->colors = 256;
        h_pixels_per_clock = 4u;
        break;
    default:
        info->mode = "invalid VGA decode";
        return true;
    }

    info->xres = h_clocks * h_pixels_per_clock;
    info->yres = visible_scanlines / scanline_rows;
    info->mode = chain4 ? "VGA linear graphics" : "VGA planar graphics";

    if (vga_crtc_valid[0x13] && vga_crtc_valid[0x17]) {
        uint32_t count_by_2 = (vga_crtc_regs[0x17] & 0x08u) ? 1u : 0u;
        uint32_t address_offset = (uint32_t)vga_crtc_regs[0x13] * 2u * (count_by_2 + 1u);
        info->vxres = address_offset * h_pixels_per_clock;
    } else {
        info->vxres = info->xres;
    }

    bytes_per_line = info->vxres;
    if (info->colors == 16) {
        bytes_per_line /= 8u;
    } else if (info->colors == 256 && !chain4) {
        bytes_per_line /= 4u;
    }

    if (bytes_per_line == 0 || info->yres == 0) {
        info->mode = "invalid VGA decode";
        return true;
    }

    info->vyres = 65536u / bytes_per_line;
    info->xpages10 = info->xres ? (info->vxres * 10u + info->xres / 2u) / info->xres : 0;
    info->ypages10 = (info->vyres * 10u + info->yres / 2u) / info->yres;
    info->valid = true;
    return true;
}

static McgaModeInfo mcga_decode_mode(void)
{
    McgaModeInfo info = {0};
    uint8_t cgamode = mcga_cgamode_valid ? mcga_cgamode : 0;
    uint8_t extmode = mcga_extmode_valid ? mcga_extmode : 0;
    uint8_t modecont = mcga_crtc_valid[0x10] ? mcga_crtc_regs[0x10] : 0;
    uint8_t charlines = mcga_crtc_valid[0x09] ? mcga_crtc_regs[0x09] : 0x0f;
    bool mode_written = mcga_cgamode_valid || mcga_extmode_valid || mcga_crtc_valid[0x10];

    if (mcga_register_page == MCGA_REGISTER_PAGE_VGA) {
        if (mcga_try_decode_vga_mode(&info)) {
            return info;
        }
        info.mode = "waiting for VGA regs";
        return info;
    }

    if (!mode_written) {
        info.mode = "waiting for VGA/MCGA regs";
        return info;
    }

    bool m80x25alpha = (cgamode & 0x01) != 0;
    bool mode_graphics = (cgamode & 0x02) != 0;
    bool mode_bw = (cgamode & 0x04) != 0;
    bool m640x200mono = (cgamode & 0x10) != 0;
    bool m256colors = (extmode & 0x04) != 0;
    bool emr_res1 = (extmode & 0x02) != 0;
    bool mode_11 = (modecont & 0x02) != 0;
    bool modecont_bit6 = (modecont & 0x40) != 0;
    bool ctl_graphics = mode_graphics || m256colors || m640x200mono || emr_res1;
    bool text_mode = !ctl_graphics;
    bool packed4_mode = emr_res1 && !m256colors;
    bool mono_mode = m640x200mono;
    uint32_t scanout_y = mode_11 ? 480u : 400u;
    uint32_t bytes_per_line = 0;

    if (m256colors) {
        info.colors = 256;
        info.xres = 320;
        info.yres = scanout_y / 2u;
        info.mode = "linear graphics";
        bytes_per_line = 320;
    } else if (mono_mode) {
        info.colors = 2;
        info.xres = 640;
        info.yres = mode_11 ? 480u : 200u;
        info.mode = mode_11 ? "640x480 mono graphics" : "640x200 mono graphics";
        bytes_per_line = 80;
    } else if (packed4_mode) {
        info.colors = 16;
        info.xres = 320;
        info.yres = scanout_y / 2u;
        info.mode = "packed 4bpp graphics";
        bytes_per_line = 160;
    } else if (!text_mode) {
        info.colors = mode_bw ? 2 : 4;
        info.xres = 320;
        info.yres = scanout_y / 2u;
        info.mode = mode_bw ? "CGA mono graphics" : "CGA graphics";
        bytes_per_line = 80;
    } else {
        uint32_t columns = m80x25alpha ? 80u : 40u;
        uint32_t line_terminal = charlines & 0x0f;
        uint32_t cell_height;

        if (!modecont_bit6 && !mode_11) {
            line_terminal = ((charlines & 0x07) << 1) | 1u;
        }
        cell_height = line_terminal + 1u;
        if (!mode_11 && line_terminal < 8u) {
            cell_height <<= 1;
        }
        if (cell_height == 0) {
            cell_height = 16;
        }

        info.colors = 16;
        info.xres = 640;
        info.yres = scanout_y;
        info.vxres = columns;
        info.vyres = scanout_y / cell_height;
        info.mode = m80x25alpha ? "80-column text" : "40-column text";
        info.xpages10 = 10;
        info.ypages10 = info.vyres ? ((16384u / columns) * 10u + info.vyres / 2u) / info.vyres : 0;
        info.valid = true;
        return info;
    }

    if (bytes_per_line == 0 || info.yres == 0) {
        info.mode = "invalid MCGA decode";
        return info;
    }

    info.vxres = info.xres;
    info.vyres = 65536u / bytes_per_line;
    info.xpages10 = 10;
    info.ypages10 = (info.vyres * 10u + info.yres / 2u) / info.yres;
    info.valid = true;
    return info;
}

static void draw_overlay_line(GC *pGC, char *text, long x, long y, long endX, GCCOLOR foreground)
{
    GCSetForegroundColor(pGC, foreground);
    GCDrawText(pGC, text, x, y, endX);
}

static void draw_overlay_separator(GC *pGC, long x, long y, long width, long height)
{
    long gcWidth = GCWidth(pGC);
    long gcHeight = GCHeight(pGC);

    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= gcWidth || y >= gcHeight) {
        return;
    }
    if (x + width > gcWidth) {
        width = gcWidth - x;
    }
    if (y + height > gcHeight) {
        height = gcHeight - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    GCFastFill(pGC, x, y, width, height, RGB(255, 255, 255));
}

static void mcga_pad_line(char *line, unsigned width)
{
    unsigned len = 0;
    while (line[len] && len < width) {
        ++len;
    }
    while (len < width && len < 63) {
        line[len++] = ' ';
    }
    line[len] = '\0';
}

static McgaRegisterGroup mcga_register_group(const McgaRegisterRow *row)
{
    switch (row->kind) {
    case MCGA_ROW_ATTR:
        return MCGA_GROUP_ATTR;
    case MCGA_ROW_SEQ:
        return MCGA_GROUP_SEQ;
    case MCGA_ROW_GDC:
        return MCGA_GROUP_GDC;
    case MCGA_ROW_VGA_CRTC:
    case MCGA_ROW_CRTC:
    case MCGA_ROW_MFGREG:
        return MCGA_GROUP_CRTC;
    case MCGA_ROW_DIRECT:
    default:
        return MCGA_GROUP_DIRECT;
    }
}

static const char *mcga_register_group_title(McgaRegisterGroup group)
{
    switch (group) {
    case MCGA_GROUP_ATTR:
        return "ATTRIBUTE CONTROLLER 3C0";
    case MCGA_GROUP_SEQ:
        return "SEQUENCER 3C4/3C5";
    case MCGA_GROUP_GDC:
        return "GRAPHICS CONTROLLER 3CE/3CF";
    case MCGA_GROUP_CRTC:
        return mcga_register_page == MCGA_REGISTER_PAGE_VGA ? "VGA CRTC 3D4/3D5" : "MCGA 72X8300 3D4/3D5";
    case MCGA_GROUP_DIRECT:
    default:
        return "COMMON DIRECT PORTS";
    }
}

static bool mcga_row_visible(const McgaRegisterRow *row)
{
    return (row->pages & (1u << mcga_register_page)) != 0;
}

static unsigned mcga_next_visible_row(unsigned start)
{
    while (start < MCGA_REGISTER_ROW_COUNT && !mcga_row_visible(&mcga_register_rows[start])) {
        ++start;
    }
    return start;
}

static unsigned mcga_register_group_end(unsigned start)
{
    McgaRegisterGroup group = mcga_register_group(&mcga_register_rows[start]);
    unsigned end = start + 1;

    while (end < MCGA_REGISTER_ROW_COUNT) {
        const McgaRegisterRow *row = &mcga_register_rows[end];
        if (mcga_row_visible(row) && mcga_register_group(row) != group) {
            break;
        }
        ++end;
    }
    return end;
}

static unsigned mcga_visible_rows_in_group(unsigned start, unsigned end)
{
    unsigned rows = 0;
    for (unsigned i = start; i < end; ++i) {
        if (mcga_row_visible(&mcga_register_rows[i])) {
            ++rows;
        }
    }
    return rows;
}

static void mcga_format_register_label(const McgaRegisterRow *row,
                                       char *line,
                                       size_t lineSize,
                                       bool compact,
                                       unsigned valueColumnChars)
{
    if (compact) {
        if (row->kind == MCGA_ROW_ATTR || row->kind == MCGA_ROW_SEQ ||
            row->kind == MCGA_ROW_GDC || row->kind == MCGA_ROW_VGA_CRTC ||
            row->kind == MCGA_ROW_CRTC || row->kind == MCGA_ROW_MFGREG) {
            snprintf(line, lineSize, "%02X %s:", row->index, row->name);
        } else if (row->port == 0x3df) {
            snprintf(line, lineSize, "%03X(04) %s:", row->port, row->name);
        } else {
            snprintf(line, lineSize, "%03X %s:", row->port, row->name);
        }
        mcga_pad_line(line, valueColumnChars);
        return;
    }

    if (row->kind == MCGA_ROW_ATTR || row->kind == MCGA_ROW_SEQ ||
        row->kind == MCGA_ROW_GDC || row->kind == MCGA_ROW_VGA_CRTC ||
        row->kind == MCGA_ROW_CRTC || row->kind == MCGA_ROW_MFGREG) {
        snprintf(line, lineSize, "%02X %-18s:", row->index, row->name);
    } else if (row->port == 0x3df) {
        snprintf(line, lineSize, "%03X(04) %-14s:", row->port, row->name);
    } else {
        snprintf(line, lineSize, "%03X %-18s:", row->port, row->name);
    }
    mcga_pad_line(line, valueColumnChars);
}

static bool mcga_register_layout_fits(long rowsY,
                                      long bottomY,
                                      unsigned columns,
                                      long lineAdvance)
{
    unsigned column = 0;
    long groupY = rowsY;

    for (unsigned start = mcga_next_visible_row(0); start < MCGA_REGISTER_ROW_COUNT;) {
        unsigned end = mcga_register_group_end(start);
        unsigned visibleRows = mcga_visible_rows_in_group(start, end);
        long boxHeight = (long)(visibleRows + 1) * lineAdvance + 3;

        if (columns > 1 && groupY > rowsY && groupY + boxHeight > bottomY) {
            ++column;
            groupY = rowsY;
        }
        if (column >= columns || groupY + boxHeight > bottomY) {
            return false;
        }
        groupY += boxHeight - 1;
        start = mcga_next_visible_row(end);
    }
    return true;
}

static void DrawMcgaRegisters(GC *pGC, long x, long y, long width, long height)
{
    const GCCOLOR background = RGB(0, 0, 0);
    const GCCOLOR infoColor = RGB(255, 255, 255);
    const GCCOLOR vgaLabelColor = RGB(80, 200, 255);
    const GCCOLOR mcgaLabelColor = RGB(255, 120, 255);
    const GCCOLOR regEnableColor = RGB(255, 255, 0);
    const GCCOLOR regDisableColor = RGB(170, 170, 170);
    GCCOLOR oldBackground = pGC->backgroundColor;
    GCCOLOR oldForeground = pGC->foregroundColor;
    long oldFillMode = pGC->fillMode;
    long oldTextMode = pGC->textMode;
    GCFONT *oldFont = (GCFONT *)pGC->pFont;
    long textHeight = pGC->pFont ? pGC->pFont->charHeight : 8;
    long charWidth = pGC->pFont ? pGC->pFont->maxCharWidth : 8;
    long lineAdvance = textHeight + 1;
    long headerY = y;
    long legendY = y + lineAdvance;
    long modeY = y + lineAdvance * 2;
    long physY = y + lineAdvance * 3;
    long rowsY = y + lineAdvance * 5;
    long columnChars = 31;
    long valueColumnChars = 25;
    long minColumnWidth = columnChars * charWidth;
    long columnWidth = width;
    unsigned columns = (width >= (minColumnWidth * 2 - 1)) ? 2 : 1;
    bool compactLayout = false;
    bool forceRedraw;
    char line[64];

    if (textHeight <= 0 || charWidth <= 0 || width <= 0 || height <= 0) {
        return;
    }

    if (!mcga_register_layout_fits(rowsY, y + height, columns, lineAdvance)) {
        GCFONT *compactFont = GetFont5x7();
        if (compactFont) {
            textHeight = compactFont->charHeight;
            charWidth = compactFont->maxCharWidth;
            lineAdvance = textHeight + 1;
            columnChars = 29;
            valueColumnChars = 23;
            minColumnWidth = columnChars * charWidth;
            columns = (width >= (minColumnWidth * 2 - 1)) ? 2 : 1;
            headerY = y;
            legendY = y + lineAdvance;
            modeY = y + lineAdvance * 2;
            physY = y + lineAdvance * 3;
            rowsY = y + lineAdvance * 5;
            compactLayout = true;
            GCSetFont(pGC, compactFont);
        }
    }
    columnWidth = columns > 1 ? (width + 1) / 2 : width;

    forceRedraw = mcga_overlay_force_redraw ||
                  !mcga_overlay_initialized ||
                  mcga_overlay_x != x ||
                  mcga_overlay_y != y ||
                  mcga_overlay_width != width ||
                  mcga_overlay_height != height;
    if (forceRedraw) {
        mcga_overlay_initialized = true;
        mcga_overlay_x = x;
        mcga_overlay_y = y;
        mcga_overlay_width = width;
        mcga_overlay_height = height;
        mcga_overlay_force_redraw = false;
        mcga_mark_all_register_rows_dirty();
    }

    GCSetBackgroundColor(pGC, background);
    GCSetFillMode(pGC, GCOPAQUE | GCOPAQUEFG);
    GCSetTextMode(pGC, GCTEXT_SINGLELINE);

    if (forceRedraw) {
        GCFastFill(pGC, x, y, width, height, background);
        snprintf(line, sizeof(line), "%-40s",
                 mcga_register_page == MCGA_REGISTER_PAGE_VGA ?
                 "VGA REGISTER SNOOP" :
                 "MCGA REGISTER SNOOP");
        draw_overlay_line(pGC, line, x, headerY, x + width, infoColor);
        snprintf(line, sizeof(line), "%-12s", "Common");
        draw_overlay_line(pGC, line, x, legendY, x + width, vgaLabelColor);
        snprintf(line, sizeof(line), "%-12s", "MCGA-only");
        draw_overlay_line(pGC, line, x + 13 * charWidth, legendY, x + width, mcgaLabelColor);
    }

    if (mcga_mode_dirty) {
        McgaModeInfo mode = mcga_decode_mode();
        if (mode.valid) {
            snprintf(line, sizeof(line), "%u-color %s mode.",
                     mode.colors, mode.mode);
            mcga_pad_line(line, 40);
            draw_overlay_line(pGC, line, x, modeY, x + width, infoColor);

            snprintf(line, sizeof(line), "Physical resolution: %lu x %lu",
                     (unsigned long)mode.xres,
                     (unsigned long)mode.yres);
            mcga_pad_line(line, 40);
            draw_overlay_line(pGC, line, x, physY, x + width, infoColor);

        } else {
            snprintf(line, sizeof(line), "%-40s", mode.mode);
            draw_overlay_line(pGC, line, x, modeY, x + width, infoColor);
            snprintf(line, sizeof(line), "%-40s", "Physical resolution: -- x --");
            draw_overlay_line(pGC, line, x, physY, x + width, infoColor);
        }
        mcga_mode_dirty = false;
    }

    {
        unsigned column = 0;
        long groupY = rowsY;

        if (forceRedraw && columns > 1) {
            draw_overlay_separator(pGC, x + columnWidth - 1, rowsY, 1, y + height - rowsY);
        }

        for (unsigned start = mcga_next_visible_row(0); start < MCGA_REGISTER_ROW_COUNT;) {
            unsigned end = mcga_register_group_end(start);
            McgaRegisterGroup group = mcga_register_group(&mcga_register_rows[start]);
            unsigned visibleRows = mcga_visible_rows_in_group(start, end);
            long boxHeight = (long)(visibleRows + 1) * lineAdvance + 3;
            long groupX;
            long groupWidth;

            if (columns > 1 && groupY > rowsY && groupY + boxHeight > y + height) {
                ++column;
                groupY = rowsY;
            }
            if (column >= columns) {
                break;
            }

            groupX = x + (long)column * (columnWidth - 1);
            groupWidth = columns > 1 ? columnWidth : width;
            if (columns > 1 && column == columns - 1) {
                groupWidth = x + width - groupX;
            }
            if (groupX + groupWidth > x + width) {
                groupWidth = x + width - groupX;
            }

            if (groupWidth <= charWidth * 4) {
                start = mcga_next_visible_row(end);
                groupY += boxHeight - 1;
                continue;
            }

            if (forceRedraw) {
                draw_overlay_separator(pGC, groupX, groupY, groupWidth, 1);
                snprintf(line, sizeof(line), "%s", mcga_register_group_title(group));
                mcga_pad_line(line, (unsigned)((groupWidth / charWidth) - 2));
                draw_overlay_line(pGC, line, groupX + charWidth, groupY + 2, groupX + groupWidth - 1, infoColor);
            }

            unsigned visibleRow = 0;
            for (unsigned i = start; i < end; ++i) {
                const McgaRegisterRow *row = &mcga_register_rows[i];
                uint8_t value = 0;
                bool valid = mcga_get_register(row, &value);
                char valueText[3] = "--";
                GCCOLOR labelColor = row->source == MCGA_SOURCE_MCGA ? mcgaLabelColor : vgaLabelColor;
                long rowX = groupX + charWidth;
                long valueX = rowX + valueColumnChars * charWidth;
                long rowY;

                if (!mcga_row_visible(row)) {
                    continue;
                }
                rowY = groupY + lineAdvance + 2 + (long)visibleRow * lineAdvance;
                ++visibleRow;

                if (rowY + textHeight > y + height) {
                    continue;
                }

                if (!mcga_register_row_drawn[i]) {
                    mcga_format_register_label(row, line, sizeof(line), compactLayout, valueColumnChars);
                    draw_overlay_line(pGC, line, rowX, rowY, groupX + groupWidth - 1, labelColor);
                    mcga_register_row_drawn[i] = true;
                    if (!valid) {
                        draw_overlay_line(pGC, valueText, valueX, rowY, groupX + groupWidth - 1, regDisableColor);
                    }
                }

                if (!mcga_register_row_dirty[i]) {
                    continue;
                }

                if (valid) {
                    snprintf(valueText, sizeof(valueText), "%02X", value);
                }
                draw_overlay_line(pGC, valueText, valueX, rowY, groupX + groupWidth - 1,
                                  valid ? regEnableColor : regDisableColor);
                mcga_register_row_dirty[i] = false;
            }

            groupY += boxHeight - 1;
            start = mcga_next_visible_row(end);
        }
    }

    mcga_register_dirty = false;
    GCSetTextMode(pGC, oldTextMode);
    GCSetFillMode(pGC, oldFillMode);
    GCSetForegroundColor(pGC, oldForeground);
    GCSetBackgroundColor(pGC, oldBackground);
    GCSetFont(pGC, oldFont);
}



void DrawPalette(GC *pGC, long paletteX, long paletteY, long paletteWidth, long paletteHeight)
{
    uint8_t uiPalOffset = 0;
    long uiBorderWidth;
    long uiCellSizeX, uiCellSizeY;
    long fillWidth, fillHeight;

    uiCellSizeX = paletteWidth/16;
    uiCellSizeY = paletteHeight/16;
    if (uiCellSizeX < 1 || uiCellSizeY < 1) {
        return;
    }
    if (uiCellSizeX > 1) {
        uiCellSizeX &= ~1L;
    }
    if (uiCellSizeY > 1) {
        uiCellSizeY &= ~1L;
    }

    uiBorderWidth = paletteHeight/100;
    long minCellSize = uiCellSizeX < uiCellSizeY ? uiCellSizeX : uiCellSizeY;
    if (uiBorderWidth < 1 && minCellSize > 2) {
        uiBorderWidth = 1;
    }
    if (uiBorderWidth >= minCellSize) {
        uiBorderWidth = minCellSize > 1 ? minCellSize - 1 : 0;
    }

    fillWidth = uiCellSizeX - uiBorderWidth;
    fillHeight = uiCellSizeY - uiBorderWidth;
    if (fillWidth < 1) {
        fillWidth = 1;
    }
    if (fillHeight < 1) {
        fillHeight = 1;
    }

    GCSetOffset(pGC, paletteX, paletteY);

    bool drewsomething = false;
    for (int y = 0; y < 16; y++)
    {
        GCPBeginAccess(pGC);
        for (int x = 0; x < 16; x++)
        {
            uint32_t cur = syspalette[uiPalOffset];
            if (cur != syspalette2[uiPalOffset])
            {
                syspalette2[uiPalOffset] = cur;
                long xpos, ypos;
                xpos = x*uiCellSizeX;
                ypos = y*uiCellSizeY;
                GCFastFill(pGC, xpos+uiBorderWidth, ypos+uiBorderWidth, fillWidth, fillHeight, syspalette2[uiPalOffset]);
                drewsomething = true;
            }
            uiPalOffset++;
        }
        GCPEndAccess(pGC);
    }
    GCSetOffset(pGC, 0, 0);
}

void DrawPostCodes(GC *pGC, long textX, long textY, long textWidth)
{
    const char hex[] = "0123456789ABCDEF";
    char text[] = "POST<-- -- -- --";

    for (uint8_t i = 0; i < post_code_history_count; ++i) {
        uint8_t slot = (post_code_history_next + 4 - post_code_history_count + i) & 0x03;
        uint8_t value = post_code_history[slot];
        uint8_t text_offset = 5 + i*3;
        text[text_offset] = hex[value >> 4];
        text[text_offset + 1] = hex[value & 0x0f];
    }

    GCCOLOR oldBackground = pGC->backgroundColor;
    GCCOLOR oldForeground = pGC->foregroundColor;
    long oldFillMode = pGC->fillMode;
    long oldTextMode = pGC->textMode;

    GCSetBackgroundColor(pGC, RGB(0, 0, 0));
    GCSetForegroundColor(pGC, RGB(255, 255, 255));
    GCSetFillMode(pGC, GCOPAQUE | GCOPAQUEFG);
    GCSetTextMode(pGC, GCTEXT_SINGLELINE);
    GCDrawText(pGC, text, textX, textY, textX + textWidth);

    GCSetTextMode(pGC, oldTextMode);
    GCSetFillMode(pGC, oldFillMode);
    GCSetForegroundColor(pGC, oldForeground);
    GCSetBackgroundColor(pGC, oldBackground);
}

void Task_UpdatePalette()
{
    GC* pGC = GCDisplay();
    if (pGC && pGC->bitmap.handle) {

        long textHeight = pGC->pFont ? pGC->pFont->charHeight : 8;
        long paletteWidth = 128;
        long paletteHeight = 128;
        long paletteX = GCWidth(pGC) - paletteWidth;
        long mainSeparatorX = paletteX - 2;
        long postX = paletteX;
        long postY = 0;
        long paletteY = postY + textHeight + 2;
        long postSeparatorY = paletteY - 1;
        long registerX = 3;
        long registerY = 0;
        long registerWidth = mainSeparatorX >= registerX ? mainSeparatorX - registerX + 1 : GCWidth(pGC);
        long registerHeight = GCHeight(pGC);
        bool redrawPanelSeparators = mcga_register_dirty || post_code_dirty;

        if (mcga_register_dirty) {
            DrawMcgaRegisters(pGC, registerX, registerY, registerWidth, registerHeight);
            mcga_register_dirty = false;
        }
        if (post_code_dirty) {
            DrawPostCodes(pGC, postX, postY, paletteWidth);
            post_code_dirty = false;
        }
        DrawPalette(pGC, paletteX, paletteY, paletteWidth, paletteHeight);
        if (redrawPanelSeparators) {
            draw_overlay_separator(pGC, mainSeparatorX, 0, 1, GCHeight(pGC));
            draw_overlay_separator(pGC, mainSeparatorX, postSeparatorY, GCWidth(pGC) - mainSeparatorX, 1);
        }
        dlo_flush_usb(DLO_HANDLE(&pGC->bitmap), true);
    }
}

bool keyboard_expect_led_mask;
bool caps_lock_enabled;
bool caps_lock_seen;
uint8_t current_register_page = MCGA_REGISTER_PAGE_MCGA;

void mcga_snoop_write(uint16_t port, uint8_t data)
{
    switch (port) {
    case 0x3c0:
    case 0x3c2:
    case 0x3c4:
    case 0x3c5:
    case 0x3ce:
    case 0x3cf:
        mcga_register_write(port, data);
        break;
    case kMcgaDacMask:
        pal_dac_mask(data);
        mcga_register_write(port, data);
        break;
    case 0x3c7:
        mcga_register_write(port, data);
        break;
    case kMcgaDacWriteIndex:
        pal_dac_write(data);
        mcga_register_write(port, data);
        break;
    case kMcgaDacData:
        pal_dac_data(data);
        mcga_register_write(port, data);
        break;
    case 0x3d4:
    case 0x3d5:
    case 0x3b4:
    case 0x3b5:
    case 0x3d8:
    case 0x3d9:
    case 0x3dd:
    case 0x3de:
    case 0x3df:
        mcga_register_write(port, data);
        break;
    default:
        break;
    }
}

void select_register_page(uint8_t page)
{
    if (current_register_page == page) {
        return;
    }
    current_register_page = page;
    mcga_set_register_page(page);
    printf("register page: %s\n", page == MCGA_REGISTER_PAGE_VGA ? "vga" : "mcga");
}

void post_snoop_write(uint16_t port, uint8_t data)
{
    switch (port) {
    case kPostCode:
        post_code_write(data);
        break;
    default:
        break;
    }
}

void keyboard_snoop_write(uint16_t port, uint8_t data)
{
    if (port != kKeyboardData) {
        return;
    }

    if (keyboard_expect_led_mask) {
        keyboard_expect_led_mask = false;
        bool enabled = (data & kCapsLockLed) != 0;
        if (!caps_lock_seen || caps_lock_enabled != enabled) {
            caps_lock_seen = true;
            caps_lock_enabled = enabled;
            printf("caps lock: %s\n", enabled ? "on" : "off");
            select_register_page(enabled ? MCGA_REGISTER_PAGE_VGA : MCGA_REGISTER_PAGE_MCGA);
        }
        return;
    }

    if (data == kKeyboardSetLeds) {
        keyboard_expect_led_mask = true;
    }
}

void init()
{
    printf("register view: MCGA DAC/register writes %03x-%03x -> DisplayLink\n",
           kMcgaBase,
           kMcgaBase + kMcgaLength - 1u);
    printf("register view: VGA mono CRTC writes %03x-%03x -> DisplayLink\n",
           kVgaMonoCrtcBase,
           kVgaMonoCrtcBase + kVgaMonoCrtcLength - 1u);
    printf("register pages: caps lock LED off MCGA/common, on VGA/common\n");
    printf("caps lock snooper: keyboard LED writes on %03x\n", kKeyboardData);
}

void tick()
{
    Task_UpdatePalette();
}

const IoSnoop io_snoops[] = {
    {
        kMcgaBase,
        kMcgaLength,
        mcga_snoop_write,
    },
    {
        kVgaMonoCrtcBase,
        kVgaMonoCrtcLength,
        mcga_snoop_write,
    },
    {
        kPostCode,
        0x1,
        post_snoop_write,
    },
    {
        kKeyboardData,
        0x1,
        keyboard_snoop_write,
    },
};

const Module module = {
    "register-view",
    nullptr,
    0,
    nullptr,
    0,
    io_snoops,
    sizeof(io_snoops) / sizeof(io_snoops[0]),
    nullptr,
    0,
    init,
    tick,
};

#undef DLO_HANDLE

}  // namespace

const Module &register_view_module()
{
    return module;
}

}  // namespace picomem
