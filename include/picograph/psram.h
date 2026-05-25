#pragma once

#include <stddef.h>
#include <stdint.h>

namespace picograph {

constexpr uint8_t kPsramDisabled = 0xff;
constexpr uint8_t kPsramInitFailed = 0xfd;

uint8_t psram_start();
bool psram_available();
uint8_t psram_size_mb();
uint8_t psram_read8(uint32_t address);
void psram_write8(uint32_t address, uint8_t value);
void psram_read(uint32_t address, uint8_t *data, size_t length);
void psram_write(uint32_t address, const uint8_t *data, size_t length);

}  // namespace picograph
