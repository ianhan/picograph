#pragma once

#include <stddef.h>
#include <stdint.h>

namespace picomem {

struct IoTrap {
    uint16_t base;
    uint16_t length;
    bool add_wait_state;
    bool (*read)(uint16_t port, uint8_t *data);
    void (*write)(uint16_t port, uint8_t data);
};

struct MemTrap {
    uint32_t base;
    uint32_t length;
    bool add_wait_state;
    bool (*read)(uint32_t address, uint8_t *data);
    void (*write)(uint32_t address, uint8_t data);
};

struct IoSnoop {
    uint16_t base;
    uint16_t length;
    void (*write)(uint16_t port, uint8_t data);
};

struct MemSnoop {
    uint32_t base;
    uint32_t length;
    void (*write)(uint32_t address, uint8_t data);
};

struct Module {
    const char *name;
    const IoTrap *io_traps;
    size_t io_trap_count;
    const MemTrap *mem_traps;
    size_t mem_trap_count;
    const IoSnoop *io_snoops;
    size_t io_snoop_count;
    const MemSnoop *mem_snoops;
    size_t mem_snoop_count;
    void (*init)();
    void (*tick)();
};

const Module &active_module();

}  // namespace picomem
