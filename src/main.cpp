#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "picograph/module.h"
#include "picograph/picograph_bus.h"

#if PICOGRAPH_ENABLE_PSRAM
#include "picograph/psram.h"
#endif

#if PICOGRAPH_ENABLE_USB_HOST
#include "picograph/usb_host.h"
#endif

namespace {

void configure_clock() {
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(PICOGRAPH_SYS_CLK_KHZ, true);
}

}  // namespace

int main() {
    configure_clock();
    stdio_init_all();

    const picograph::Module &module = picograph::active_module();
    printf("picograph %s module=%s\n", PICOGRAPH_VERSION, module.name);

#if PICOGRAPH_ENABLE_PSRAM
    uint8_t psram_status = picograph::psram_start();
    if (psram_status == picograph::kPsramInitFailed) {
        printf("psram init failed\n");
    }
#endif

    picograph::picograph_bus_init();

    if (module.init) {
        module.init();
    }

    for (size_t i = 0; i < module.io_trap_count; ++i) {
        picograph::TrapResult result = picograph::register_io_trap(module.io_traps[i]);
        if (result != picograph::TrapResult::Ok) {
            printf("I/O trap %u failed: %s\n", static_cast<unsigned>(i), picograph::trap_result_name(result));
        }
    }

    for (size_t i = 0; i < module.mem_trap_count; ++i) {
        picograph::TrapResult result = picograph::register_mem_trap(module.mem_traps[i]);
        if (result != picograph::TrapResult::Ok) {
            printf("MEM trap %u failed: %s\n", static_cast<unsigned>(i), picograph::trap_result_name(result));
        }
    }

    for (size_t i = 0; i < module.io_snoop_count; ++i) {
        picograph::TrapResult result = picograph::register_io_snoop(module.io_snoops[i]);
        if (result != picograph::TrapResult::Ok) {
            printf("I/O snoop %u failed: %s\n", static_cast<unsigned>(i), picograph::trap_result_name(result));
        }
    }

    for (size_t i = 0; i < module.mem_snoop_count; ++i) {
        picograph::TrapResult result = picograph::register_mem_snoop(module.mem_snoops[i]);
        if (result != picograph::TrapResult::Ok) {
            printf("MEM snoop %u failed: %s\n", static_cast<unsigned>(i), picograph::trap_result_name(result));
        }
    }

    multicore_launch_core1(picograph::picograph_bus_loop);

#if PICOGRAPH_ENABLE_USB_HOST
    picograph::usb_host_start(false);
#endif

    for (;;) {
        picograph::picograph_bus_task();
#if PICOGRAPH_ENABLE_USB_HOST
        picograph::usb_host_task();
#endif
        picograph::picograph_bus_task();
        if (module.tick) {
            module.tick();
        }
        tight_loop_contents();
    }
}
