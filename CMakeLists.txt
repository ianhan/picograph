# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD "pico2" CACHE STRING "Pico SDK board target")
set(PICOMEM_BOARD "PICOMEM_M1" CACHE STRING "PicoMEM hardware profile: PICOMEM_M1, PICOMEM_M2, PICOMEM_LP, PICOMEM_15")
set(PICOMEM_MODULE "HERCULES" CACHE STRING "Single module to build into this firmware")
set(PICOMEM_SYS_CLK_KHZ "280000" CACHE STRING "RP2040/RP2350 system clock in kHz")
set(PICOMEM_ENABLE_USB_HOST ON CACHE BOOL "Enable TinyUSB host support")
set(PICOMEM_ENABLE_XINPUT ON CACHE BOOL "Enable the TinyUSB XInput host class")
set(PICOMEM_ENABLE_PSRAM OFF CACHE BOOL "Enable rp2040-psram support")
set(PICOMEM_ENABLE_DISPLAYLINK ON CACHE BOOL "Enable DisplayLink USB display support")
set(PICOMEM_ENABLE_GC ON CACHE BOOL "Enable GC graphics support")
set(PICOMEM_DISPLAYLINK_WIDTH_DEFAULT "720")
set(PICOMEM_DISPLAYLINK_HEIGHT_DEFAULT "400")
set(PICOMEM_DISPLAYLINK_REFRESH_DEFAULT "70")
if(PICOMEM_MODULE STREQUAL "EGA")
    set(PICOMEM_DISPLAYLINK_WIDTH_DEFAULT "640")
    set(PICOMEM_DISPLAYLINK_HEIGHT_DEFAULT "480")
    set(PICOMEM_DISPLAYLINK_REFRESH_DEFAULT "60")
endif()
set(PICOMEM_DISPLAYLINK_WIDTH "${PICOMEM_DISPLAYLINK_WIDTH_DEFAULT}" CACHE STRING "DisplayLink output mode width")
set(PICOMEM_DISPLAYLINK_HEIGHT "${PICOMEM_DISPLAYLINK_HEIGHT_DEFAULT}" CACHE STRING "DisplayLink output mode height")
set(PICOMEM_DISPLAYLINK_REFRESH "${PICOMEM_DISPLAYLINK_REFRESH_DEFAULT}" CACHE STRING "DisplayLink output mode refresh rate")
set(PICOMEM_HERCULES_DISPLAY_TYPE "GREEN" CACHE STRING "Hercules display type: WHITE, GREEN, AMBER, RGB, RGB_NO_BROWN")
set(PICOMEM_EGA_MEMORY_KB "256" CACHE STRING "EGA memory size: 64, 128, 256")
set(PICOMEM_EGA_MONITOR_TYPE "COLOR_ECD" CACHE STRING "EGA monitor type: COLOR_40, COLOR_80, COLOR_ECD, MONO_WHITE, MONO_GREEN, MONO_AMBER")
set(PICOMEM_SAMPLE_IO_BASE "0x300" CACHE STRING "Base I/O port for the sample module")
set(PICOMEM_SAMPLE_MEM_BASE "0xd0000" CACHE STRING "Base memory address for the sample module trap window")
set(PICOMEM_SAMPLE_MEM_SNOOP_BASE "0xb8000" CACHE STRING "Base memory address for the sample module write snoop")
set(PICOMEM_PSRAM_PIO "1" CACHE STRING "PIO block used by rp2040-psram")
set(PICOMEM_PSRAM_SM "-1" CACHE STRING "PIO state machine for rp2040-psram, or -1 to claim one")
set(PICOMEM_PSRAM_TARGET_MHZ "220" CACHE STRING "Target PSRAM PIO clock in MHz, matching ISA-PicoMEM's DMA path")
set(PICOMEM_PSRAM_PIN_CS "5" CACHE STRING "PSRAM chip-select GPIO")
set(PICOMEM_PSRAM_PIN_SCK "6" CACHE STRING "PSRAM clock GPIO")
set(PICOMEM_PSRAM_PIN_MOSI "7" CACHE STRING "PSRAM MOSI GPIO")
set(PICOMEM_PSRAM_PIN_MISO "4" CACHE STRING "PSRAM MISO GPIO")

if(PICOMEM_ENABLE_USB_HOST)
    set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb CACHE PATH "TinyUSB checkout used by the Pico SDK" FORCE)
endif()

include(pico_sdk_import.cmake)

project(picomem
    VERSION 0.1.0
    LANGUAGES C CXX ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set_property(CACHE PICOMEM_MODULE PROPERTY STRINGS REGISTER_VIEW HERCULES EGA MDA SAMPLE)
set_property(CACHE PICOMEM_HERCULES_DISPLAY_TYPE PROPERTY STRINGS WHITE GREEN AMBER RGB RGB_NO_BROWN)
set_property(CACHE PICOMEM_EGA_MEMORY_KB PROPERTY STRINGS 64 128 256)
set_property(CACHE PICOMEM_EGA_MONITOR_TYPE PROPERTY STRINGS COLOR_40 COLOR_80 COLOR_ECD MONO_WHITE MONO_GREEN MONO_AMBER)

pico_sdk_init()

if(PICOMEM_ENABLE_USB_HOST)
    if(NOT EXISTS ${PICO_TINYUSB_PATH}/src/tusb.h)
        message(FATAL_ERROR "PICOMEM_ENABLE_USB_HOST requires the lib/tinyusb submodule")
    endif()
    if(PICOMEM_ENABLE_XINPUT)
        add_library(tinyusb INTERFACE)
        target_link_libraries(tinyusb INTERFACE tinyusb_host)
        add_subdirectory(lib/tusb_xinput)
    endif()
endif()

if(PICOMEM_ENABLE_PSRAM)
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/rp2040-psram/CMakeLists.txt)
        message(FATAL_ERROR "PICOMEM_ENABLE_PSRAM requires the lib/rp2040-psram submodule")
    endif()
    add_subdirectory(lib/rp2040-psram)
endif()

if(PICOMEM_ENABLE_DISPLAYLINK)
    if(NOT PICOMEM_ENABLE_USB_HOST)
        message(FATAL_ERROR "PICOMEM_ENABLE_DISPLAYLINK requires PICOMEM_ENABLE_USB_HOST")
    endif()
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/libdlo/CMakeLists.txt)
        message(FATAL_ERROR "PICOMEM_ENABLE_DISPLAYLINK requires lib/libdlo")
    endif()
    add_subdirectory(lib/libdlo)
endif()

if(PICOMEM_ENABLE_GC)
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/libgc/CMakeLists.txt)
        message(FATAL_ERROR "PICOMEM_ENABLE_GC requires lib/libgc")
    endif()
    add_subdirectory(lib/libgc)
endif()

set(PICOMEM_MODULE_SOURCE "")
set(PICOMEM_MODULE_SUPPORT_SOURCE "")
if(PICOMEM_MODULE STREQUAL "REGISTER_VIEW")
    set(PICOMEM_MODULE_SOURCE src/modules/register_view.cpp)
elseif(PICOMEM_MODULE STREQUAL "HERCULES")
    set(PICOMEM_MODULE_SOURCE src/modules/hercules_device.cpp)
    list(APPEND PICOMEM_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOMEM_MODULE STREQUAL "EGA")
    set(PICOMEM_MODULE_SOURCE src/modules/ega_device.cpp)
    list(APPEND PICOMEM_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOMEM_MODULE STREQUAL "MDA")
    set(PICOMEM_MODULE_SOURCE src/modules/mda_device.cpp)
    list(APPEND PICOMEM_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOMEM_MODULE STREQUAL "SAMPLE")
    set(PICOMEM_MODULE_SOURCE src/modules/sample_device.cpp)
else()
    message(FATAL_ERROR "Unknown PICOMEM_MODULE=${PICOMEM_MODULE}")
endif()

add_executable(picomem
    src/main.cpp
    src/framework/module_registry.cpp
    src/hw/picomem_bus.cpp
    ${PICOMEM_MODULE_SOURCE}
    ${PICOMEM_MODULE_SUPPORT_SOURCE}
)

target_include_directories(picomem PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(picomem PRIVATE
    PICOMEM_VERSION="${PROJECT_VERSION}"
    PICOMEM_SYS_CLK_KHZ=${PICOMEM_SYS_CLK_KHZ}
    PICOMEM_SAMPLE_IO_BASE=${PICOMEM_SAMPLE_IO_BASE}
    PICOMEM_SAMPLE_MEM_BASE=${PICOMEM_SAMPLE_MEM_BASE}
    PICOMEM_SAMPLE_MEM_SNOOP_BASE=${PICOMEM_SAMPLE_MEM_SNOOP_BASE}
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_BAUD_RATE=115200
    PICO_DEFAULT_UART_TX_PIN=28
    PICO_DEFAULT_UART_RX_PIN=-1
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=250
)

if(PICOMEM_ENABLE_USB_HOST)
    target_sources(picomem PRIVATE
        src/hw/usb_host.cpp
    )
    target_include_directories(picomem PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/hw/usb_host
    )
    target_compile_definitions(picomem PRIVATE
        PICOMEM_ENABLE_USB_HOST=1
        PICOMEM_ENABLE_XINPUT=$<BOOL:${PICOMEM_ENABLE_XINPUT}>
        CFG_TUSB_CONFIG_FILE=\"picomem_tusb_config.h\"
    )
else()
    target_compile_definitions(picomem PRIVATE
        PICOMEM_ENABLE_USB_HOST=0
        PICOMEM_ENABLE_XINPUT=0
    )
endif()

if(PICOMEM_ENABLE_PSRAM)
    target_sources(picomem PRIVATE
        src/hw/psram.cpp
    )
    target_compile_definitions(picomem PRIVATE
        PICOMEM_ENABLE_PSRAM=1
        PICOMEM_PSRAM_PIO=${PICOMEM_PSRAM_PIO}
        PICOMEM_PSRAM_SM=${PICOMEM_PSRAM_SM}
        PICOMEM_PSRAM_TARGET_MHZ=${PICOMEM_PSRAM_TARGET_MHZ}
        PSRAM_ASYNC=1
        PSRAM_SPINLOCK=1
        PSRAM_PIN_CS=${PICOMEM_PSRAM_PIN_CS}
        PSRAM_PIN_SCK=${PICOMEM_PSRAM_PIN_SCK}
        PSRAM_PIN_MOSI=${PICOMEM_PSRAM_PIN_MOSI}
        PSRAM_PIN_MISO=${PICOMEM_PSRAM_PIN_MISO}
    )
else()
    target_compile_definitions(picomem PRIVATE PICOMEM_ENABLE_PSRAM=0)
endif()

if(PICOMEM_ENABLE_DISPLAYLINK)
    target_link_libraries(picomem libdlo)
    target_include_directories(picomem PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lib/libdlo)
    target_compile_definitions(picomem PRIVATE
        PICOMEM_ENABLE_DISPLAYLINK=1
        PICOMEM_DISPLAYLINK_WIDTH=${PICOMEM_DISPLAYLINK_WIDTH}
        PICOMEM_DISPLAYLINK_HEIGHT=${PICOMEM_DISPLAYLINK_HEIGHT}
        PICOMEM_DISPLAYLINK_REFRESH=${PICOMEM_DISPLAYLINK_REFRESH}
        CFG_TUH_API_EDPT_XFER=1
    )
else()
    target_compile_definitions(picomem PRIVATE PICOMEM_ENABLE_DISPLAYLINK=0)
endif()

if(PICOMEM_ENABLE_GC)
    target_link_libraries(picomem libgc)
    target_include_directories(picomem PRIVATE ${CMAKE_CURRENT_LIST_DIR}/lib/libgc)
    target_compile_definitions(picomem PRIVATE PICOMEM_ENABLE_GC=1)
else()
    target_compile_definitions(picomem PRIVATE PICOMEM_ENABLE_GC=0)
endif()

if(PICOMEM_HERCULES_DISPLAY_TYPE STREQUAL "WHITE")
    set(PICOMEM_HERCULES_DISPLAY_TYPE_VALUE 5)
elseif(PICOMEM_HERCULES_DISPLAY_TYPE STREQUAL "GREEN")
    set(PICOMEM_HERCULES_DISPLAY_TYPE_VALUE 3)
elseif(PICOMEM_HERCULES_DISPLAY_TYPE STREQUAL "AMBER")
    set(PICOMEM_HERCULES_DISPLAY_TYPE_VALUE 4)
elseif(PICOMEM_HERCULES_DISPLAY_TYPE STREQUAL "RGB")
    set(PICOMEM_HERCULES_DISPLAY_TYPE_VALUE 0)
elseif(PICOMEM_HERCULES_DISPLAY_TYPE STREQUAL "RGB_NO_BROWN")
    set(PICOMEM_HERCULES_DISPLAY_TYPE_VALUE 2)
else()
    message(FATAL_ERROR "Unknown PICOMEM_HERCULES_DISPLAY_TYPE=${PICOMEM_HERCULES_DISPLAY_TYPE}")
endif()

if(PICOMEM_EGA_MEMORY_KB STREQUAL "64")
    set(PICOMEM_EGA_MEMORY_KB_VALUE 64)
elseif(PICOMEM_EGA_MEMORY_KB STREQUAL "128")
    set(PICOMEM_EGA_MEMORY_KB_VALUE 128)
elseif(PICOMEM_EGA_MEMORY_KB STREQUAL "256")
    set(PICOMEM_EGA_MEMORY_KB_VALUE 256)
else()
    message(FATAL_ERROR "Unknown PICOMEM_EGA_MEMORY_KB=${PICOMEM_EGA_MEMORY_KB}")
endif()

if(PICOMEM_EGA_MONITOR_TYPE STREQUAL "COLOR_40")
    set(PICOMEM_EGA_MONITOR_TYPE_VALUE 6)
elseif(PICOMEM_EGA_MONITOR_TYPE STREQUAL "COLOR_80")
    set(PICOMEM_EGA_MONITOR_TYPE_VALUE 7)
elseif(PICOMEM_EGA_MONITOR_TYPE STREQUAL "COLOR_ECD")
    set(PICOMEM_EGA_MONITOR_TYPE_VALUE 9)
elseif(PICOMEM_EGA_MONITOR_TYPE STREQUAL "MONO_WHITE")
    set(PICOMEM_EGA_MONITOR_TYPE_VALUE 90)
elseif(PICOMEM_EGA_MONITOR_TYPE STREQUAL "MONO_GREEN")
    set(PICOMEM_EGA_MONITOR_TYPE_VALUE 58)
elseif(PICOMEM_EGA_MONITOR_TYPE STREQUAL "MONO_AMBER")
    set(PICOMEM_EGA_MONITOR_TYPE_VALUE 74)
else()
    message(FATAL_ERROR "Unknown PICOMEM_EGA_MONITOR_TYPE=${PICOMEM_EGA_MONITOR_TYPE}")
endif()

if(PICOMEM_MODULE STREQUAL "REGISTER_VIEW")
    target_compile_definitions(picomem PRIVATE PICOMEM_MODULE_REGISTER_VIEW=1)
elseif(PICOMEM_MODULE STREQUAL "HERCULES")
    target_compile_definitions(picomem PRIVATE
        PICOMEM_MODULE_HERCULES=1
        PICOMEM_HERCULES_DISPLAY_TYPE=${PICOMEM_HERCULES_DISPLAY_TYPE_VALUE}
    )
elseif(PICOMEM_MODULE STREQUAL "EGA")
    target_compile_definitions(picomem PRIVATE
        PICOMEM_MODULE_EGA=1
        PICOMEM_EGA_MEMORY_KB=${PICOMEM_EGA_MEMORY_KB_VALUE}
        PICOMEM_EGA_MONITOR_TYPE=${PICOMEM_EGA_MONITOR_TYPE_VALUE}
    )
elseif(PICOMEM_MODULE STREQUAL "MDA")
    target_compile_definitions(picomem PRIVATE PICOMEM_MODULE_MDA=1)
elseif(PICOMEM_MODULE STREQUAL "SAMPLE")
    target_compile_definitions(picomem PRIVATE PICOMEM_MODULE_SAMPLE=1)
endif()

if(PICOMEM_BOARD STREQUAL "PICOMEM_M1")
    set(PICOMEM_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_m1.pio)
    target_compile_definitions(picomem PRIVATE
        PICOMEM_BOARD_PICOMEM_M1=1
        PICOMEM_MUX_V2=0
        PICOMEM_PIO_NEEDS_INVALID_CYCLE=1
    )
elseif(PICOMEM_BOARD STREQUAL "PICOMEM_M2")
    set(PICOMEM_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_m2.pio)
    target_compile_definitions(picomem PRIVATE
        PICOMEM_BOARD_PICOMEM_M2=1
        PICOMEM_MUX_V2=1
        PICOMEM_PIO_NEEDS_INVALID_CYCLE=0
    )
elseif(PICOMEM_BOARD STREQUAL "PICOMEM_LP")
    set(PICOMEM_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_noaen.pio)
    target_compile_definitions(picomem PRIVATE
        PICOMEM_BOARD_PICOMEM_LP=1
        PICOMEM_MUX_V2=0
        PICOMEM_PIO_NEEDS_INVALID_CYCLE=1
    )
elseif(PICOMEM_BOARD STREQUAL "PICOMEM_15")
    set(PICOMEM_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_m2_v15.pio)
    target_compile_definitions(picomem PRIVATE
        PICOMEM_BOARD_PICOMEM_15=1
        PICOMEM_MUX_V2=1
        PICOMEM_PIO_NEEDS_INVALID_CYCLE=0
        PICO_PIO_USE_GPIO_BASE=1
    )
else()
    message(FATAL_ERROR "Unknown PICOMEM_BOARD=${PICOMEM_BOARD}")
endif()

pico_generate_pio_header(picomem ${PICOMEM_ISA_PIO})

target_link_libraries(picomem
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_clocks
    hardware_vreg
)

if(PICOMEM_ENABLE_USB_HOST)
    target_link_libraries(picomem
        tinyusb_board
        tinyusb_host
    )
    if(PICOMEM_ENABLE_XINPUT)
        target_link_libraries(picomem xinput_host)
    endif()
endif()

if(PICOMEM_ENABLE_PSRAM)
    target_link_libraries(picomem
        rp2040-psram
        hardware_dma
        hardware_sync
    )
endif()

pico_set_program_name(picomem "picomem")
pico_set_program_version(picomem ${PROJECT_VERSION})
pico_enable_stdio_uart(picomem 1)
pico_enable_stdio_usb(picomem 0)
pico_enable_stdio_semihosting(picomem 0)
pico_add_extra_outputs(picomem)

if(PICO_PLATFORM MATCHES "rp2350")
    set(PICOMEM_OPENOCD_TARGET target/rp2350.cfg)
else()
    set(PICOMEM_OPENOCD_TARGET target/rp2040.cfg)
endif()

add_custom_target(
    program
    DEPENDS picomem
    COMMAND openocd -f interface/picoprobe.cfg -f ${PICOMEM_OPENOCD_TARGET} -c "program picomem.elf verify reset exit"
)
