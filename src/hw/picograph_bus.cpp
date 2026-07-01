#include "picograph/picograph_bus.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/platform/sections.h"
#include "pico/stdlib.h"

#if PICOGRAPH_BOARD_PICOMEM_M1
#include "isa_iomem_m1.pio.h"
#elif PICOGRAPH_BOARD_PICOMEM_M2
#include "isa_iomem_m2.pio.h"
#elif PICOGRAPH_BOARD_PICOMEM_LP
#include "isa_iomem_noaen.pio.h"
#elif PICOGRAPH_BOARD_PICOMEM_15
#include "isa_iomem_m2_v15.pio.h"
#else
#error "Unsupported PICOGRAPH_BOARD"
#endif

namespace picograph {
namespace {

#define PICOGRAPH_BUS_HOT __scratch_x("picograph_bus")

constexpr uint32_t kCycleMemRead = 0x01;
constexpr uint32_t kCycleMemWrite = 0x02;
constexpr uint32_t kCycleIoRead = 0x04;
constexpr uint32_t kCycleIoWrite = 0x08;

constexpr uint32_t kNoWaitState = 0x00000000u;
constexpr uint32_t kAddWaitState = 0xffffffffu;
constexpr uint32_t kReadValue = 0x00ffff00u;

#if PICOGRAPH_BOARD_PICOMEM_15
constexpr uint32_t kDoIor = 0x380eu;
constexpr uint32_t kDoMemr = 0x380cu;
#else
constexpr uint32_t kDoIor = 1u;
constexpr uint32_t kDoMemr = 2u;
#endif

PIO isa_pio = pio0;
constexpr uint kIsaBusSm = 0;

const IoTrap *io_table[kIsaIoSlotCount];
const MemTrap *mem_table[kIsaMemSlotCount];
const IoSnoop *io_snoop_table[kIsaIoSlotCount];
const MemSnoop *mem_snoop_table[kIsaMemSlotCount];

constexpr uint32_t kIoSnoopQueueSize = 4096;
constexpr uint32_t kIoSnoopQueueMask = kIoSnoopQueueSize - 1u;
constexpr uint32_t kMemSnoopQueueSize = 256;
constexpr uint32_t kMemSnoopQueueMask = kMemSnoopQueueSize - 1u;

struct IoSnoopEvent {
    const IoSnoop *snoop;
    uint16_t port;
    uint8_t data;
};

struct MemSnoopEvent {
    const MemSnoop *snoop;
    uint32_t address;
    uint8_t data;
};

static_assert((kIoSnoopQueueSize & kIoSnoopQueueMask) == 0, "I/O snoop queue size must be a power of two");
static_assert((kMemSnoopQueueSize & kMemSnoopQueueMask) == 0, "MEM snoop queue size must be a power of two");

IoSnoopEvent io_snoop_queue[kIoSnoopQueueSize];
MemSnoopEvent mem_snoop_queue[kMemSnoopQueueSize];
volatile uint32_t io_snoop_head;
volatile uint32_t io_snoop_tail;
volatile uint32_t mem_snoop_head;
volatile uint32_t mem_snoop_tail;
volatile uint32_t io_snoop_dropped;
volatile uint32_t mem_snoop_dropped;

static_assert(kIsaIoSlotCount * kIsaIoSlotSize == 0x400, "I/O trap table must cover 10-bit ISA I/O");
static_assert(kIsaMemSlotCount * kIsaMemSlotSize == 0x100000, "Memory trap table must cover 20-bit ISA memory");

uint32_t PICOGRAPH_BUS_HOT control_mask() {
#if PICOGRAPH_MUX_V2
    return 0x0fu << PIN_A8_MR;
#else
    return 0x0fu << PIN_A16_MR;
#endif
}

uint32_t PICOGRAPH_BUS_HOT control_shift() {
#if PICOGRAPH_MUX_V2
    return PIN_A8_MR;
#else
    return PIN_A16_MR;
#endif
}

uint32_t PICOGRAPH_BUS_HOT read_cycle_type() {
    return (gpio_get_all() & control_mask()) >> control_shift();
}

uint16_t PICOGRAPH_BUS_HOT decode_io_port(uint32_t pio_value) {
#if PICOGRAPH_MUX_V2
    return static_cast<uint16_t>((pio_value ^ 0x0300u) & kIsaIoLast);
#else
    return static_cast<uint16_t>((pio_value >> 12) & kIsaIoLast);
#endif
}

uint32_t PICOGRAPH_BUS_HOT decode_io_slot(uint32_t pio_value) {
#if PICOGRAPH_MUX_V2
    return decode_io_port(pio_value) / kIsaIoSlotSize;
#else
    return (pio_value << 9) >> 24;
#endif
}

uint32_t PICOGRAPH_BUS_HOT decode_mem_address(uint32_t pio_value) {
#if PICOGRAPH_MUX_V2
    return (pio_value ^ 0x000f00u) & kIsaMemLast;
#else
    return ((pio_value >> 12) ^ 0x0f0000u) & kIsaMemLast;
#endif
}

uint8_t PICOGRAPH_BUS_HOT read_data_bus() {
    return static_cast<uint8_t>((gpio_get_all() >> PIN_AD0) & 0xffu);
}

void PICOGRAPH_BUS_HOT put_wait(bool add_wait_state) {
    pio_sm_put(isa_pio, kIsaBusSm, add_wait_state ? kAddWaitState : kNoWaitState);
}

void PICOGRAPH_BUS_HOT finish_without_read() {
    pio_sm_put(isa_pio, kIsaBusSm, 0x00u);
}

void PICOGRAPH_BUS_HOT finish_io_read(uint8_t data) {
    pio_sm_put(isa_pio, kIsaBusSm, kDoIor);
    pio_sm_put(isa_pio, kIsaBusSm, kReadValue | data);
    (void)pio_sm_get_blocking(isa_pio, kIsaBusSm);
}

void PICOGRAPH_BUS_HOT finish_mem_read(uint8_t data) {
    pio_sm_put(isa_pio, kIsaBusSm, kDoMemr);
    pio_sm_put(isa_pio, kIsaBusSm, kReadValue | data);
    (void)pio_sm_get_blocking(isa_pio, kIsaBusSm);
}

bool valid_io_range(uint16_t base, uint16_t length) {
    if (length == 0 || base > kIsaIoLast) {
        return false;
    }

    uint32_t end = static_cast<uint32_t>(base) + length - 1u;
    return end <= kIsaIoLast;
}

bool valid_mem_range(uint32_t base, uint32_t length) {
    if (length == 0 || base > kIsaMemLast) {
        return false;
    }

    uint32_t end = base + length - 1u;
    return end <= kIsaMemLast && end >= base;
}

bool PICOGRAPH_BUS_HOT io_snoop_matches(const IoSnoop *snoop, uint16_t port) {
    if (!snoop || !snoop->write) {
        return false;
    }

    uint32_t end = static_cast<uint32_t>(snoop->base) + snoop->length;
    return port >= snoop->base && port < end;
}

bool PICOGRAPH_BUS_HOT mem_snoop_matches(const MemSnoop *snoop, uint32_t address) {
    if (!snoop || !snoop->write) {
        return false;
    }

    uint32_t end = snoop->base + snoop->length;
    return address >= snoop->base && address < end;
}

void PICOGRAPH_BUS_HOT enqueue_io_snoop(const IoSnoop *snoop, uint16_t port, uint8_t data) {
    if (!io_snoop_matches(snoop, port)) {
        return;
    }

    uint32_t head = io_snoop_head;
    if (head - io_snoop_tail >= kIoSnoopQueueSize) {
        ++io_snoop_dropped;
        return;
    }

    IoSnoopEvent &event = io_snoop_queue[head & kIoSnoopQueueMask];
    event.snoop = snoop;
    event.port = port;
    event.data = data;
    __mem_fence_release();
    io_snoop_head = head + 1u;
}

void PICOGRAPH_BUS_HOT enqueue_mem_snoop(const MemSnoop *snoop, uint32_t address, uint8_t data) {
    if (!mem_snoop_matches(snoop, address)) {
        return;
    }

    uint32_t head = mem_snoop_head;
    if (head - mem_snoop_tail >= kMemSnoopQueueSize) {
        ++mem_snoop_dropped;
        return;
    }

    MemSnoopEvent &event = mem_snoop_queue[head & kMemSnoopQueueMask];
    event.snoop = snoop;
    event.address = address;
    event.data = data;
    __mem_fence_release();
    mem_snoop_head = head + 1u;
}

}  // namespace

void picograph_bus_init() {
    for (const IoTrap *&trap : io_table) {
        trap = nullptr;
    }
    for (const MemTrap *&trap : mem_table) {
        trap = nullptr;
    }
    for (const IoSnoop *&snoop : io_snoop_table) {
        snoop = nullptr;
    }
    for (const MemSnoop *&snoop : mem_snoop_table) {
        snoop = nullptr;
    }
    io_snoop_head = 0;
    io_snoop_tail = 0;
    mem_snoop_head = 0;
    mem_snoop_tail = 0;
    io_snoop_dropped = 0;
    mem_snoop_dropped = 0;

    gpio_init(PIN_IORDY);
    gpio_set_dir(PIN_IORDY, GPIO_OUT);
    gpio_put(PIN_IORDY, 0);

    gpio_init(PIN_AD);
    gpio_set_dir(PIN_AD, GPIO_OUT);
    gpio_put(PIN_AD, SEL_ADR);

    gpio_init(PIN_AS);
    gpio_set_dir(PIN_AS, GPIO_OUT);
#if PICOGRAPH_BOARD_PICOMEM_15
    gpio_put(PIN_AS, SEL_CTRL);
#else
    gpio_put(PIN_AS, SEL_ADL);
#endif

    gpio_set_slew_rate(PIN_AD, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PIN_AS, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PIN_IORDY, GPIO_SLEW_RATE_FAST);

    for (int i = PIN_AD0; i < PIN_AD0 + 12; ++i) {
        gpio_init(i);
        gpio_disable_pulls(i);
        gpio_set_dir(i, GPIO_IN);
    }

#if PICOGRAPH_BOARD_PICOMEM_15
    pio_set_gpio_base(isa_pio, 16);
#endif

    uint offset = pio_add_program(isa_pio, &isa_bus_program);
    uint claimed = pio_claim_unused_sm(isa_pio, true);
    if (claimed != kIsaBusSm) {
        printf("picograph: expected ISA SM %u, got %u\n", kIsaBusSm, claimed);
    }
    isa_bus_program_init(isa_pio, kIsaBusSm, offset);

#if PICOGRAPH_MUX_V2
    gpio_set_inover(PIN_A8_MR, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(PIN_A9_MW, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(PIN_A10_IR, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(PIN_A11_IW, GPIO_OVERRIDE_INVERT);
#else
    gpio_set_inover(PIN_A16_MR, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(PIN_A17_MW, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(PIN_A18_IR, GPIO_OVERRIDE_INVERT);
    gpio_set_inover(PIN_A19_IW, GPIO_OVERRIDE_INVERT);
#endif

#if PICOGRAPH_PIO_NEEDS_INVALID_CYCLE
    pio_sm_put(isa_pio, kIsaBusSm, 0xfffff0f0u);
#endif
}

void picograph_bus_task() {
    while (io_snoop_tail != io_snoop_head) {
        uint32_t tail = io_snoop_tail;
        __mem_fence_acquire();
        const IoSnoopEvent &event = io_snoop_queue[tail & kIoSnoopQueueMask];
        if (event.snoop && event.snoop->write) {
            event.snoop->write(event.port, event.data);
        }
        io_snoop_tail = tail + 1u;
    }

    while (mem_snoop_tail != mem_snoop_head) {
        uint32_t tail = mem_snoop_tail;
        __mem_fence_acquire();
        const MemSnoopEvent &event = mem_snoop_queue[tail & kMemSnoopQueueMask];
        if (event.snoop && event.snoop->write) {
            event.snoop->write(event.address, event.data);
        }
        mem_snoop_tail = tail + 1u;
    }
}

TrapResult register_io_trap(const IoTrap &trap) {
    if (!valid_io_range(trap.base, trap.length)) {
        return TrapResult::InvalidRange;
    }

    uint16_t first = trap.base / kIsaIoSlotSize;
    uint16_t last = (trap.base + trap.length - 1u) / kIsaIoSlotSize;

    for (uint16_t slot = first; slot <= last; ++slot) {
        if (io_table[slot] != nullptr) {
            return TrapResult::Overlap;
        }
    }

    for (uint16_t slot = first; slot <= last; ++slot) {
        io_table[slot] = &trap;
    }

    return TrapResult::Ok;
}

TrapResult register_mem_trap(const MemTrap &trap) {
    if (!valid_mem_range(trap.base, trap.length)) {
        return TrapResult::InvalidRange;
    }

    uint32_t first = trap.base / kIsaMemSlotSize;
    uint32_t last = (trap.base + trap.length - 1u) / kIsaMemSlotSize;

    for (uint32_t slot = first; slot <= last; ++slot) {
        if (mem_table[slot] != nullptr) {
            return TrapResult::Overlap;
        }
    }

    for (uint32_t slot = first; slot <= last; ++slot) {
        mem_table[slot] = &trap;
    }

    return TrapResult::Ok;
}

TrapResult register_io_snoop(const IoSnoop &snoop) {
    if (!valid_io_range(snoop.base, snoop.length)) {
        return TrapResult::InvalidRange;
    }

    uint16_t first = snoop.base / kIsaIoSlotSize;
    uint16_t last = (snoop.base + snoop.length - 1u) / kIsaIoSlotSize;

    for (uint16_t slot = first; slot <= last; ++slot) {
        if (io_snoop_table[slot] != nullptr) {
            return TrapResult::Overlap;
        }
    }

    for (uint16_t slot = first; slot <= last; ++slot) {
        io_snoop_table[slot] = &snoop;
    }

    return TrapResult::Ok;
}

TrapResult register_mem_snoop(const MemSnoop &snoop) {
    if (!valid_mem_range(snoop.base, snoop.length)) {
        return TrapResult::InvalidRange;
    }

    uint32_t first = snoop.base / kIsaMemSlotSize;
    uint32_t last = (snoop.base + snoop.length - 1u) / kIsaMemSlotSize;

    for (uint32_t slot = first; slot <= last; ++slot) {
        if (mem_snoop_table[slot] != nullptr) {
            return TrapResult::Overlap;
        }
    }

    for (uint32_t slot = first; slot <= last; ++slot) {
        mem_snoop_table[slot] = &snoop;
    }

    return TrapResult::Ok;
}

const char *trap_result_name(TrapResult result) {
    switch (result) {
    case TrapResult::Ok:
        return "ok";
    case TrapResult::InvalidRange:
        return "invalid range";
    case TrapResult::Overlap:
        return "overlap";
    default:
        return "unknown";
    }
}

[[noreturn]] void PICOGRAPH_BUS_HOT picograph_bus_loop() {
    for (;;) {
        uint32_t cycle;
        do {
            cycle = read_cycle_type();
            tight_loop_contents();
        } while (cycle != 0);

        do {
            cycle = read_cycle_type();
            tight_loop_contents();
        } while (cycle == 0);

        pio_sm_put(isa_pio, kIsaBusSm, gpio_get_all());

        uint32_t raw_address = pio_sm_get_blocking(isa_pio, kIsaBusSm);

        if (read_cycle_type() == 0) {
            // The cycle ended before the address handshake completed, so the
            // captured address (and any data) may be garbage from the muxes
            // collapsing mid-capture. Drop it: losing the cycle corrupts at
            // most the byte the host intended, while acting on it could write
            // anywhere.
            put_wait(false);
            finish_without_read();
            continue;
        }

        if (cycle == kCycleIoRead || cycle == kCycleIoWrite) {
            uint16_t port = decode_io_port(raw_address);
            uint32_t slot = decode_io_slot(raw_address);
            if (slot >= kIsaIoSlotCount) {
                put_wait(false);
                finish_without_read();
                continue;
            }

            const IoTrap *trap = io_table[slot];
            const IoSnoop *snoop = io_snoop_table[slot];
            if (!trap) {
                put_wait(false);
                uint8_t data = read_data_bus();
                finish_without_read();
                if (cycle == kCycleIoWrite) {
                    enqueue_io_snoop(snoop, port, data);
                }
                continue;
            }

            put_wait(trap->add_wait_state);
            if (cycle == kCycleIoWrite) {
                uint8_t data = read_data_bus();
                if (trap->write) {
                    trap->write(port, data);
                }
                // Enqueue while the cycle is still stretched so the epilogue
                // after releasing the bus stays as short as possible.
                enqueue_io_snoop(snoop, port, data);
                finish_without_read();
                continue;
            }

            uint8_t data = 0xff;
            bool handled = trap->read && trap->read(port, &data);
            if (handled) {
                finish_io_read(data);
            } else {
                finish_without_read();
            }
            continue;
        }

        if (cycle == kCycleMemRead || cycle == kCycleMemWrite) {
            uint32_t address = decode_mem_address(raw_address);
            const MemTrap *trap = mem_table[address / kIsaMemSlotSize];
            const MemSnoop *snoop = mem_snoop_table[address / kIsaMemSlotSize];
            if (!trap) {
                put_wait(false);
                uint8_t data = read_data_bus();
                finish_without_read();
                if (cycle == kCycleMemWrite) {
                    enqueue_mem_snoop(snoop, address, data);
                }
                continue;
            }

            // Assert the wait state before consulting the trap's active()
            // hook: every nanosecond shaved here is margin against the host
            // chipset sampling CHRDY before the stretch takes effect.
            put_wait(trap->add_wait_state);
            if (trap->active && !trap->active(address)) {
                // The range is not currently mapped; complete like an
                // untrapped cycle (the extra stretch is harmless).
                if (cycle == kCycleMemWrite) {
                    uint8_t data = read_data_bus();
                    enqueue_mem_snoop(snoop, address, data);
                }
                finish_without_read();
                continue;
            }

            if (cycle == kCycleMemWrite) {
                uint8_t data = read_data_bus();
                if (trap->write) {
                    trap->write(address, data);
                }
                // Enqueue while the cycle is still stretched so the epilogue
                // after releasing the bus stays as short as possible.
                enqueue_mem_snoop(snoop, address, data);
                finish_without_read();
                continue;
            }

            uint8_t data = 0xff;
            bool handled = trap->read && trap->read(address, &data);
            if (handled) {
                finish_mem_read(data);
            } else {
                finish_without_read();
            }
            continue;
        }

        put_wait(false);
        finish_without_read();
    }
}

#undef PICOGRAPH_BUS_HOT

}  // namespace picograph
