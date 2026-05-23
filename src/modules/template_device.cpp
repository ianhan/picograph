#include "picomem/module.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace picomem {
namespace {

constexpr uint16_t kIoBase = 0x300;
constexpr uint16_t kIoLength = 0x8;
constexpr uint32_t kMemBase = 0xd0000u;
constexpr uint32_t kMemLength = 0x2000;

uint8_t selected_register;
uint8_t registers[8];
uint8_t memory_window[kMemLength];

bool io_read(uint16_t port, uint8_t *data) {
    switch ((port - kIoBase) & 0x7) {
    case 0:
        *data = selected_register;
        return true;
    case 1:
        *data = registers[selected_register & 0x7];
        return true;
    case 2:
        *data = 0x50;
        return true;
    case 3:
        *data = 0x47;
        return true;
    default:
        *data = 0xff;
        return true;
    }
}

void io_write(uint16_t port, uint8_t data) {
    switch ((port - kIoBase) & 0x7) {
    case 0:
        selected_register = data & 0x7;
        break;
    case 1:
        registers[selected_register & 0x7] = data;
        break;
    default:
        break;
    }
}

bool mem_read(uint32_t address, uint8_t *data) {
    if (address < kMemBase || address >= kMemBase + kMemLength) {
        return false;
    }

    *data = memory_window[address - kMemBase];
    return true;
}

void mem_write(uint32_t address, uint8_t data) {
    if (address < kMemBase || address >= kMemBase + kMemLength) {
        return;
    }

    memory_window[address - kMemBase] = data;
}

void post_snoop_write(uint16_t port, uint8_t data) {
    if (port == 0x80) {
        printf("POST %02x\n", data);
    }
}

void init() {
    for (size_t i = 0; i < kMemLength; ++i) {
        memory_window[i] = static_cast<uint8_t>((i ^ 0x5a) & 0xff);
    }

    registers[0] = 0x01;
    registers[1] = 0x00;
    printf("template module: I/O %03x-%03x MEM %05lx-%05lx\n",
           kIoBase,
           kIoBase + kIoLength - 1,
           static_cast<unsigned long>(kMemBase),
           static_cast<unsigned long>(kMemBase + kMemLength - 1));
}

void tick() {
}

const IoTrap io_traps[] = {
    {
        kIoBase,
        kIoLength,
        true,
        io_read,
        io_write,
    },
};

const MemTrap mem_traps[] = {
    {
        kMemBase,
        kMemLength,
        true,
        mem_read,
        mem_write,
    },
};

const IoSnoop io_snoops[] = {
    {
        0x80,
        0x1,
        post_snoop_write,
    },
};

const Module module = {
    "template",
    io_traps,
    sizeof(io_traps) / sizeof(io_traps[0]),
    //mem_traps,
    //sizeof(mem_traps) / sizeof(mem_traps[0]),
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
