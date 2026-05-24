#include "picomem/module.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace picomem {
namespace {

constexpr uint16_t kIoBase = static_cast<uint16_t>(PICOMEM_SAMPLE_IO_BASE);
constexpr uint16_t kIoLength = 0x8;
constexpr uint32_t kMemBase = static_cast<uint32_t>(PICOMEM_SAMPLE_MEM_BASE);
constexpr uint32_t kMemLength = 0x2000u;
constexpr uint16_t kPostPort = 0x80;
constexpr uint32_t kMemSnoopBase = static_cast<uint32_t>(PICOMEM_SAMPLE_MEM_SNOOP_BASE);
constexpr uint32_t kMemSnoopLength = 0x2000u;

uint8_t selected_register;
uint8_t registers[8];
uint8_t memory_window[kMemLength];

volatile uint32_t io_read_count;
volatile uint32_t io_write_count;
volatile uint32_t mem_read_count;
volatile uint32_t mem_write_count;

uint32_t post_snoop_count;
uint8_t last_post_code;
bool post_code_seen;

uint32_t mem_snoop_count;
uint32_t last_mem_snoop_address;
uint8_t last_mem_snoop_data;

uint32_t printed_io_read_count;
uint32_t printed_io_write_count;
uint32_t printed_mem_read_count;
uint32_t printed_mem_write_count;
uint32_t printed_post_snoop_count;
uint32_t printed_mem_snoop_count;

bool in_io_range(uint16_t port)
{
    return port >= kIoBase && port < kIoBase + kIoLength;
}

bool in_mem_range(uint32_t address)
{
    return address >= kMemBase && address < kMemBase + kMemLength;
}

uint8_t io_offset(uint16_t port)
{
    return static_cast<uint8_t>((port - kIoBase) & 0x7u);
}

void count_bus_event(volatile uint32_t *counter)
{
    __atomic_fetch_add(counter, 1u, __ATOMIC_RELAXED);
}

bool io_read(uint16_t port, uint8_t *data)
{
    if (!data || !in_io_range(port)) {
        return false;
    }

    count_bus_event(&io_read_count);
    switch (io_offset(port)) {
    case 0:
        *data = selected_register;
        return true;
    case 1:
        *data = registers[selected_register & 0x7u];
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

void io_write(uint16_t port, uint8_t data)
{
    if (!in_io_range(port)) {
        return;
    }

    count_bus_event(&io_write_count);
    switch (io_offset(port)) {
    case 0:
        selected_register = data & 0x7u;
        break;
    case 1:
        registers[selected_register & 0x7u] = data;
        break;
    default:
        break;
    }
}

bool mem_read(uint32_t address, uint8_t *data)
{
    if (!data || !in_mem_range(address)) {
        return false;
    }

    count_bus_event(&mem_read_count);
    *data = memory_window[address - kMemBase];
    return true;
}

void mem_write(uint32_t address, uint8_t data)
{
    if (!in_mem_range(address)) {
        return;
    }

    count_bus_event(&mem_write_count);
    memory_window[address - kMemBase] = data;
}

void io_snoop_write(uint16_t port, uint8_t data)
{
    if (port != kPostPort) {
        return;
    }

    last_post_code = data;
    post_code_seen = true;
    ++post_snoop_count;
    printf("sample snoop: POST %02x\n", data);
}

void mem_snoop_write(uint32_t address, uint8_t data)
{
    if (address < kMemSnoopBase || address >= kMemSnoopBase + kMemSnoopLength) {
        return;
    }

    last_mem_snoop_address = address;
    last_mem_snoop_data = data;
    ++mem_snoop_count;
}

void init()
{
    for (size_t i = 0; i < kMemLength; ++i) {
        memory_window[i] = static_cast<uint8_t>((i ^ 0x5au) & 0xffu);
    }

    selected_register = 0;
    registers[0] = 0x01;
    registers[1] = 0x00;

    printf("sample device: I/O trap %03x-%03x MEM trap %05lx-%05lx\n",
           kIoBase,
           kIoBase + kIoLength - 1u,
           static_cast<unsigned long>(kMemBase),
           static_cast<unsigned long>(kMemBase + kMemLength - 1u));
    printf("sample device: I/O snoop %03x MEM snoop %05lx-%05lx\n",
           kPostPort,
           static_cast<unsigned long>(kMemSnoopBase),
           static_cast<unsigned long>(kMemSnoopBase + kMemSnoopLength - 1u));
}

void tick()
{
    uint32_t current_io_reads = __atomic_load_n(&io_read_count, __ATOMIC_RELAXED);
    uint32_t current_io_writes = __atomic_load_n(&io_write_count, __ATOMIC_RELAXED);
    uint32_t current_mem_reads = __atomic_load_n(&mem_read_count, __ATOMIC_RELAXED);
    uint32_t current_mem_writes = __atomic_load_n(&mem_write_count, __ATOMIC_RELAXED);

    if (current_io_reads != printed_io_read_count ||
        current_io_writes != printed_io_write_count ||
        current_mem_reads != printed_mem_read_count ||
        current_mem_writes != printed_mem_write_count ||
        post_snoop_count != printed_post_snoop_count ||
        mem_snoop_count != printed_mem_snoop_count) {
        printf("sample counts: io r/w %lu/%lu mem r/w %lu/%lu post %lu mem-snoop %lu",
               static_cast<unsigned long>(current_io_reads),
               static_cast<unsigned long>(current_io_writes),
               static_cast<unsigned long>(current_mem_reads),
               static_cast<unsigned long>(current_mem_writes),
               static_cast<unsigned long>(post_snoop_count),
               static_cast<unsigned long>(mem_snoop_count));
        if (post_code_seen) {
            printf(" last-post %02x", last_post_code);
        }
        if (mem_snoop_count) {
            printf(" last-mem %05lx=%02x",
                   static_cast<unsigned long>(last_mem_snoop_address),
                   last_mem_snoop_data);
        }
        printf("\n");

        printed_io_read_count = current_io_reads;
        printed_io_write_count = current_io_writes;
        printed_mem_read_count = current_mem_reads;
        printed_mem_write_count = current_mem_writes;
        printed_post_snoop_count = post_snoop_count;
        printed_mem_snoop_count = mem_snoop_count;
    }
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
        kPostPort,
        0x1,
        io_snoop_write,
    },
};

const MemSnoop mem_snoops[] = {
    {
        kMemSnoopBase,
        kMemSnoopLength,
        mem_snoop_write,
    },
};

const Module module = {
    "sample-device",
    io_traps,
    sizeof(io_traps) / sizeof(io_traps[0]),
    mem_traps,
    sizeof(mem_traps) / sizeof(mem_traps[0]),
    io_snoops,
    sizeof(io_snoops) / sizeof(io_snoops[0]),
    mem_snoops,
    sizeof(mem_snoops) / sizeof(mem_snoops[0]),
    init,
    tick,
};

}  // namespace

const Module &sample_module()
{
    return module;
}

}  // namespace picomem
