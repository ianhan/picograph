#include "picomem/module.h"

#include <stdint.h>
#include <stdio.h>

namespace picomem {
namespace {

#if !PICOMEM_ENABLE_DISPLAYLINK || !PICOMEM_ENABLE_GC
#error "The default template is a DisplayLink palette snooper and requires PICOMEM_ENABLE_DISPLAYLINK and PICOMEM_ENABLE_GC"
#endif

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
constexpr uint8_t kRegisterPageMcga = 0;
constexpr uint8_t kRegisterPageVga = 1;

bool keyboard_expect_led_mask;
bool caps_lock_enabled;
bool caps_lock_seen;
uint8_t current_register_page = kRegisterPageMcga;

extern "C" {
void pal_dac_mask(uint8_t mask);
void pal_dac_write(uint8_t index);
void pal_dac_data(uint8_t data);
void post_code_write(uint8_t value);
void mcga_register_write(uint16_t port, uint8_t value);
void mcga_set_register_page(uint8_t page);
void Task_UpdatePalette();
}

void mcga_snoop_write(uint16_t port, uint8_t data) {
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
    printf("register page: %s\n", page == kRegisterPageVga ? "vga" : "mcga");
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
            select_register_page(enabled ? kRegisterPageVga : kRegisterPageMcga);
        }
        return;
    }

    if (data == kKeyboardSetLeds) {
        keyboard_expect_led_mask = true;
    }
}

void init() {
    printf("palette snooper: MCGA DAC/register writes %03x-%03x -> DisplayLink\n",
           kMcgaBase,
           kMcgaBase + kMcgaLength - 1u);
    printf("palette snooper: VGA mono CRTC writes %03x-%03x -> DisplayLink\n",
           kVgaMonoCrtcBase,
           kVgaMonoCrtcBase + kVgaMonoCrtcLength - 1u);
    printf("register pages: caps lock LED off MCGA/common, on VGA/common\n");
    printf("caps lock snooper: keyboard LED writes on %03x\n", kKeyboardData);
}

void tick() {
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
    }
};

const Module module = {
    "palette-snooper",
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

}  // namespace

const Module &template_module() {
    return module;
}

}  // namespace picomem
