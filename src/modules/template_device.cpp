#include "picomem/module.h"

#include <stdint.h>
#include <stdio.h>

namespace picomem {
namespace {

#if !PICOMEM_ENABLE_DISPLAYLINK || !PICOMEM_ENABLE_GC
#error "The default template is a DisplayLink palette snooper and requires PICOMEM_ENABLE_DISPLAYLINK and PICOMEM_ENABLE_GC"
#endif

constexpr uint16_t kVgaDacMask = 0x3c6;
constexpr uint16_t kVgaDacWriteIndex = 0x3c8;
constexpr uint16_t kVgaDacData = 0x3c9;
constexpr uint16_t kPostCode = 0x80;

extern "C" {
void pal_dac_mask(uint8_t mask);
void pal_dac_write(uint8_t index);
void pal_dac_data(uint8_t data);
void post_code_write(uint8_t value);
void Task_UpdatePalette();
}

void palette_snoop_write(uint16_t port, uint8_t data) {
    switch (port) {
    case kVgaDacMask:
        pal_dac_mask(data);
        break;
    case kVgaDacWriteIndex:
        pal_dac_write(data);
        break;
    case kVgaDacData:
        pal_dac_data(data);
        break;
    default:
        break;
    }
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

void init() {
    printf("palette snooper: VGA DAC writes %03x/%03x/%03x -> DisplayLink\n",
           kVgaDacMask,
           kVgaDacWriteIndex,
           kVgaDacData);
}

void tick() {
    Task_UpdatePalette();
}

const IoSnoop io_snoops[] = {
    {
        kVgaDacMask,
        0x1,
        palette_snoop_write,
    },
    {
        kVgaDacWriteIndex,
        0x2,
        palette_snoop_write,
    },
    {
        kPostCode,
        0x1,
        post_snoop_write
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
