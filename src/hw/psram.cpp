#include "picograph/psram.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/platform.h"
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

// The rp2040-psram accessors patch the address into shared static command
// buffers before taking the library's own lock, so calls from both cores must
// be serialized out here. spin_lock_blocking keeps IRQs off while held, so a
// core 0 holder can't stall a core 1 ISA trap for longer than one transaction.
spin_lock_t *psram_lock;

// The PIO program takes 8-bit bit counts: a read can carry at most 255 bits
// (31 bytes), a write 255 bits including the 4 command/address bytes (27 bytes
// of data). 16-byte chunks keep every transaction inside those limits and
// under the PSRAM's 8 us CE-low ceiling.
constexpr size_t kChunkBytes = 16;

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

    psram_lock = spin_lock_init(spin_lock_claim_unused(true));
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

uint8_t __time_critical_func(psram_read8)(uint32_t address) {
    if (!available) {
        return 0xff;
    }
    uint32_t irq_state = spin_lock_blocking(psram_lock);
    uint8_t value = ::psram_read8(&psram_spi, address);
    spin_unlock(psram_lock, irq_state);
    return value;
}

void __time_critical_func(psram_write8)(uint32_t address, uint8_t value) {
    if (!available) {
        return;
    }
    uint32_t irq_state = spin_lock_blocking(psram_lock);
    ::psram_write8(&psram_spi, address, value);
    spin_unlock(psram_lock, irq_state);
}

void __time_critical_func(psram_read)(uint32_t address, uint8_t *data, size_t length) {
    if (!available || data == nullptr || length == 0) {
        return;
    }
    while (length != 0) {
        size_t chunk = (length < kChunkBytes) ? length : kChunkBytes;
        uint32_t irq_state = spin_lock_blocking(psram_lock);
        ::psram_read(&psram_spi, address, data, chunk);
        spin_unlock(psram_lock, irq_state);
        address += chunk;
        data += chunk;
        length -= chunk;
    }
}

void __time_critical_func(psram_write)(uint32_t address, const uint8_t *data, size_t length) {
    if (!available || data == nullptr || length == 0) {
        return;
    }
    while (length != 0) {
        size_t chunk = (length < kChunkBytes) ? length : kChunkBytes;
        uint32_t irq_state = spin_lock_blocking(psram_lock);
        ::psram_write(&psram_spi, address, data, chunk);
        spin_unlock(psram_lock, irq_state);
        address += chunk;
        data += chunk;
        length -= chunk;
    }
}

}  // namespace picograph
