#pragma once

#include <stdint.h>

#include "picograph/module.h"

namespace picograph {

constexpr uint16_t kIsaIoFirst = 0x000;
constexpr uint16_t kIsaIoLast = 0x3ff;
constexpr uint16_t kIsaIoSlotSize = 0x8;
constexpr uint16_t kIsaIoSlotCount = 0x80;

constexpr uint32_t kIsaMemFirst = 0x00000;
constexpr uint32_t kIsaMemLast = 0xfffff;
constexpr uint32_t kIsaMemSlotSize = 0x2000;
constexpr uint32_t kIsaMemSlotCount = 0x80;

enum class TrapResult : uint8_t {
    Ok,
    InvalidRange,
    Overlap,
};

void picograph_bus_init();
void picograph_bus_task();
TrapResult register_io_trap(const IoTrap &trap);
TrapResult register_mem_trap(const MemTrap &trap);
TrapResult register_io_snoop(const IoSnoop &snoop);
TrapResult register_mem_snoop(const MemSnoop &snoop);
[[noreturn]] void picograph_bus_loop();

const char *trap_result_name(TrapResult result);

}  // namespace picograph
