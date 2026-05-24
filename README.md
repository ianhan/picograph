# picomem_template

This template is a firmware project that combines PicoGUS's build-time modular shape with PicoMEM's ISA bus hardware for experimentation

It intentionally does not port PicoGUS's GUS, SB, AdLib, MPU, USB, or NE2000 device implementations. Instead, one module is compiled into the firmware at a time and that module claims the I/O and memory windows it wants to trap on PicoMEM hardware.

## Layout

- `CMakeLists.txt`: Pico SDK build with `PICOMEM_MODULE` and `PICOMEM_BOARD` selection.
- `src/framework`: module registry, mirroring PicoGUS's one-firmware-per-device model.
- `src/hw`: PicoMEM ISA trap table and bus loop.
- `src/modules/register_view.cpp`: passive VGA/MCGA register, POST, and palette display module.
- `lib`: submodules for optional PSRAM and USB-host support.
- `pio`: PicoMEM ISA PIO programs copied as hardware assets.
- `docs/trap-map.md`: all I/O and memory spaces the PicoMEM trap fabric can expose.

## Build

```sh
cmake -S . -B build -DPICO_SDK_PATH=~/pico-sdk
cmake --build build
```

Useful options:

```sh
-DPICOMEM_BOARD=PICOMEM_M2
-DPICOMEM_MODULE=REGISTER_VIEW
-DPICOMEM_TEMPLATE_IO_BASE=0x300
-DPICOMEM_TEMPLATE_MEM_BASE=0xd0000
-DPICOMEM_ENABLE_USB_HOST=ON
-DPICOMEM_ENABLE_PSRAM=ON
```

Supported board profiles:

- `PICOMEM_M1`: PicoMEM 1.0 through 1.14, original mux
- `PICOMEM_M2`: PicoMEM mux v2 style, default
- `PICOMEM_LP`: no-AEN PicoMEM LP / 1.2A style
- `PICOMEM_15`: PicoMEM 1.5 PIO asset, included as a starting point for RP2350B work

## Trap Capability

PicoMEM can trap the full 10-bit ISA I/O space, `0x000-0x3ff`, in 8-port slots.

PicoMEM can trap the full 20-bit PC memory space, `0x00000-0xfffff`, in 8 KiB slots.

Modules can also register `io_snoops` and `mem_snoops`. Snoop ranges use the same slot granularity as traps, but only observe write cycles. A snoop does not insert a wait state, does not answer read cycles, and does not drive data back onto the ISA bus.

See `docs/trap-map.md` for the concrete ranges and caveats.

## Adding A Module

Create a new file in `src/modules`, define a `picomem::Module`, add a `PICOMEM_MODULE_*` branch in `src/framework/module_registry.cpp`, then add the file and compile definition branch in `CMakeLists.txt`.

The important rule is the PicoGUS one: build one hardware personality into the firmware. Do not make every device active at once unless you are explicitly designing a combined firmware.

## Optional Hardware Helpers

`picomem/psram.h` wraps the checked-in `lib/rp2040-psram` submodule with a small template-facing API. The start path follows ISA-PicoMEM's RP2040 DMA path: PIO1, an auto-claimed state machine, a target 220 MHz PSRAM PIO clock, stronger output drive on CS/SCK/MOSI, and an 8 MB boundary probe using `0xaa`/`0x55`. Defaults match ISA-PicoMEM's PSRAM pins: CS 5, SCK 6, MOSI 7, MISO 4. Override them with the `PICOMEM_PSRAM_PIN_*` CMake cache values.

`picomem/usb_host.h` wraps TinyUSB host polling and keeps the latest keyboard, mouse, and XInput gamepad state. Its lifecycle mirrors ISA-PicoMEM: `usb_host_start()` refuses to start when USB serial stdio is active, starts TinyUSB with `tuh_init(BOARD_TUH_RHPORT)`, and `usb_host_task()` polls `tuh_task()` only while enabled. The TinyUSB submodule is the same USB library ISA-PicoMEM keeps under `src/lib/tinyusb`; `lib/tusb_xinput` supplies the XInput class driver.
