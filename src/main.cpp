#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "picomem/module.h"
#include "picomem/picomem_bus.h"

#if PICOMEM_ENABLE_PSRAM
#include "picomem/psram.h"
#endif

#if PICOMEM_ENABLE_USB_HOST
#include "picomem/usb_host.h"
#endif

namespace {

void configure_clock() {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(PICOMEM_SYS_CLK_KHZ, true);
}

}  // namespace

int main() {
    configure_clock();
    stdio_init_all();

    const picomem::Module &module = picomem::active_module();
    printf("picomem %s module=%s\n", PICOMEM_VERSION, module.name);

#if PICOMEM_ENABLE_PSRAM
    uint8_t psram_status = picomem::psram_start();
    if (psram_status == picomem::kPsramInitFailed) {
        printf("psram init failed\n");
    }
#endif

#if PICOMEM_ENABLE_USB_HOST
    picomem::usb_host_start(false);
#endif

    picomem::picomem_bus_init();

    if (module.init) {
        module.init();
    }

    for (size_t i = 0; i < module.io_trap_count; ++i) {
        picomem::TrapResult result = picomem::register_io_trap(module.io_traps[i]);
        if (result != picomem::TrapResult::Ok) {
            printf("I/O trap %u failed: %s\n", static_cast<unsigned>(i), picomem::trap_result_name(result));
        }
    }

    for (size_t i = 0; i < module.mem_trap_count; ++i) {
        picomem::TrapResult result = picomem::register_mem_trap(module.mem_traps[i]);
        if (result != picomem::TrapResult::Ok) {
            printf("MEM trap %u failed: %s\n", static_cast<unsigned>(i), picomem::trap_result_name(result));
        }
    }

    for (size_t i = 0; i < module.io_snoop_count; ++i) {
        picomem::TrapResult result = picomem::register_io_snoop(module.io_snoops[i]);
        if (result != picomem::TrapResult::Ok) {
            printf("I/O snoop %u failed: %s\n", static_cast<unsigned>(i), picomem::trap_result_name(result));
        }
    }

    for (size_t i = 0; i < module.mem_snoop_count; ++i) {
        picomem::TrapResult result = picomem::register_mem_snoop(module.mem_snoops[i]);
        if (result != picomem::TrapResult::Ok) {
            printf("MEM snoop %u failed: %s\n", static_cast<unsigned>(i), picomem::trap_result_name(result));
        }
    }

    multicore_launch_core1(picomem::picomem_bus_loop);

    for (;;) {
        picomem::picomem_bus_task();
#if PICOMEM_ENABLE_USB_HOST
        picomem::usb_host_task();
#endif
        picomem::picomem_bus_task();
        if (module.tick) {
            module.tick();
        }
        tight_loop_contents();
    }
}
