#include "picograph/psram.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "psram_spi.h"

#if defined(PSRAM_ASYNC)
extern "C" {
psram_spi_inst_t *async_spi_inst;
}
#endif

namespace picograph {
namespace {

psram_spi_inst_t psram_spi;
bool available;
uint8_t size_mb;

PIO psram_pio() {
#if PICOGRAPH_PSRAM_PIO == 0
    return pio0;
#elif PICOGRAPH_PSRAM_PIO == 1
    return pio1;
#else
#error "PICOGRAPH_PSRAM_PIO must be 0 or 1"
#endif
}

}  // namespace

uint8_t psram_start() {
    if (available) {
        return size_mb;
    }

    float div = static_cast<float>(clock_get_hz(clk_sys)) / (PICOGRAPH_PSRAM_TARGET_MHZ * 1000000.0f);
    printf("psram: target=%d MHz div=%f\n", PICOGRAPH_PSRAM_TARGET_MHZ, div);

    psram_spi = psram_spi_init_clkdiv(psram_pio(), PICOGRAPH_PSRAM_SM, div, true);

    gpio_set_drive_strength(PSRAM_PIN_CS, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PSRAM_PIN_SCK, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PSRAM_PIN_MOSI, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_slew_rate(PSRAM_PIN_SCK, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PSRAM_PIN_MOSI, GPIO_SLEW_RATE_FAST);

    uint8_t first_error = 8;
    uint8_t mb = 0;
    do {
        uint32_t address = mb * 1024u * 1024u;
        ::psram_write8(&psram_spi, address, 0xaa);
        if (::psram_read8(&psram_spi, address) != 0xaa) {
            first_error = mb;
        }
        ::psram_write8(&psram_spi, address, 0x55);
        if (::psram_read8(&psram_spi, address) != 0x55) {
            first_error = mb;
        }
    } while (first_error == 8 && ++mb < 8);

    if (first_error != 8) {
        printf("psram: init error\n");
        return kPsramInitFailed;
    }

    size_mb = mb;
    available = true;
    printf("psram: ok, %u MB pio=%d sm=%d cs=%d sck=%d mosi=%d miso=%d\n",
           size_mb,
           PICOGRAPH_PSRAM_PIO,
           psram_spi.sm,
           PSRAM_PIN_CS,
           PSRAM_PIN_SCK,
           PSRAM_PIN_MOSI,
           PSRAM_PIN_MISO);
    return size_mb;
}

bool psram_available() {
    return available;
}

uint8_t psram_size_mb() {
    return size_mb;
}

uint8_t psram_read8(uint32_t address) {
    if (!available) {
        return 0xff;
    }
    return ::psram_read8(&psram_spi, address);
}

void psram_write8(uint32_t address, uint8_t value) {
    if (!available) {
        return;
    }
    ::psram_write8(&psram_spi, address, value);
}

void psram_read(uint32_t address, uint8_t *data, size_t length) {
    if (!available || data == nullptr || length == 0) {
        return;
    }
    ::psram_read(&psram_spi, address, data, length);
}

void psram_write(uint32_t address, const uint8_t *data, size_t length) {
    if (!available || data == nullptr || length == 0) {
        return;
    }
    ::psram_write(&psram_spi, address, data, length);
}

}  // namespace picograph
