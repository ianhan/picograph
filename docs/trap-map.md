# PicoMEM Trap Map

This template uses PicoMEM's ISA hardware as a generic trap fabric. The template does not port PicoGUS devices or PicoMEM devices; it gives a single compiled module a place to claim I/O and memory regions.

## I/O Space

PicoMEM hardware can observe the 10-bit XT/ISA I/O address space:

- Range: `0x000` through `0x3ff`
- Width: 8-bit data bus
- Cycles: `IOR` and `IOW`
- Decode granularity in this framework: 8 ports per slot, `port >> 3`
- Slot count: 128 slots

Any port in `0x000-0x3ff` can be trapped. A module may still inspect the exact port inside its 8-port slot, so a single-port device such as joystick `0x201` is represented as a claim on `0x200-0x207`.

Examples from the PicoMEM firmware that are useful references, but are not ported here:

- PicoMEM command window: commonly `0x2a0-0x2a7`
- POST display: `0x80` or `0x81`
- Lotech-style EMS bank registers: `0x268`, `0x288`, `0x298`, or `0x2a8`, using offsets `+0` through `+3`
- AdLib: `0x388-0x389`
- Game port: `0x201`
- Tandy PSG: commonly `0x2c0-0x2c7`
- CMS/Game Blaster: commonly `0x220-0x22f`
- Sound Blaster DSP: commonly `0x220-0x22f`
- NE2000: commonly 32 ports at `0x300-0x31f`

## Memory Space

PicoMEM hardware can observe the 20-bit PC/XT memory space:

- Range: `0x00000` through `0xfffff`
- Width: 8-bit data bus
- Cycles: `MEMR` and `MEMW`
- Decode granularity in this framework: 8 KiB per slot, `address >> 13`
- Slot count: 128 slots

The original PicoMEM firmware's user configuration is mostly expressed in 16 KiB blocks, but its fast decode tables are 8 KiB wide. This template exposes the 8 KiB granularity and leaves larger alignment choices to the module.

Common regions a module can claim:

- Conventional memory: `0x00000-0x9ffff`
- Video memory windows: `0xa0000-0xbffff`
- Option ROM and UMB area: `0xc0000-0xeffff`
- System BIOS area: `0xf0000-0xfffff`
- EMS page frame examples: `0xd0000-0xdffff` or `0xe0000-0xeffff`

The hardware can trap all of these addresses. Practical use is more limited: video emulation is not useful on PicoMEM 1.x because the board has no display path, and current PicoMEM 1.x hardware does not support ISA DMA memory cycles.

## Hardware Signals

The bus layer handles:

- `IOR`
- `IOW`
- `MEMR`
- `MEMW`
- `IOCHRDY` wait-state insertion
- One IRQ output line on PicoMEM 1.x hardware

PicoMEM 1.x ignores DMA cycles through `AEN` where that signal is available. PicoMEM LP/no-AEN builds cannot filter DMA the same way. PicoMEM 2 hardware adds capabilities such as DMA and more IRQs, but this template only models the PicoMEM 1.x style trap fabric plus the checked-in PicoMEM 1.5 PIO asset.

## Write Snoops

`io_snoops` and `mem_snoops` observe writes without claiming the bus cycle. They use the same 8-port and 8 KiB table slots as traps, so callbacks should still inspect the exact port or address when claiming a smaller range.

Snoops are write-only:

- They do not answer `IOR` or `MEMR`.
- They do not drive data onto the ISA data bus.
- They always release the cycle without adding `IOCHRDY` wait states unless a normal trap on the same slot also handles the cycle.
