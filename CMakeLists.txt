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
set(PICOGRAPH_BOARD "PICOMEM_M1" CACHE STRING "PicoMEM hardware profile: PICOMEM_M1, PICOMEM_M2, PICOMEM_LP, PICOMEM_15")
set(PICOGRAPH_MODULE "CL5429" CACHE STRING "Single module to build into this firmware")
set(PICOGRAPH_SYS_CLK_KHZ "280000" CACHE STRING "RP2040/RP2350 system clock in kHz")
set(PICOGRAPH_ENABLE_USB_HOST ON CACHE BOOL "Enable TinyUSB host support")
set(PICOGRAPH_ENABLE_XINPUT ON CACHE BOOL "Enable the TinyUSB XInput host class")
set(PICOGRAPH_ENABLE_PSRAM OFF CACHE BOOL "Enable rp2040-psram support")
set(PICOGRAPH_ENABLE_DISPLAYLINK ON CACHE BOOL "Enable DisplayLink USB display support")
set(PICOGRAPH_ENABLE_GC ON CACHE BOOL "Enable GC graphics support")
set(LOG "0" CACHE STRING "TinyUSB debug log level: 0 off, 1 error, 2 warning, 3 info")
set(PICOGRAPH_DISPLAYLINK_WIDTH_DEFAULT "720")
set(PICOGRAPH_DISPLAYLINK_HEIGHT_DEFAULT "400")
set(PICOGRAPH_DISPLAYLINK_REFRESH_DEFAULT "70")
if(PICOGRAPH_MODULE STREQUAL "EGA" OR PICOGRAPH_MODULE STREQUAL "VGA" OR PICOGRAPH_MODULE STREQUAL "CL5429")
    set(PICOGRAPH_DISPLAYLINK_WIDTH_DEFAULT "640")
    set(PICOGRAPH_DISPLAYLINK_HEIGHT_DEFAULT "480")
    set(PICOGRAPH_DISPLAYLINK_REFRESH_DEFAULT "60")
endif()
set(PICOGRAPH_DISPLAYLINK_WIDTH "${PICOGRAPH_DISPLAYLINK_WIDTH_DEFAULT}" CACHE STRING "DisplayLink output mode width")
set(PICOGRAPH_DISPLAYLINK_HEIGHT "${PICOGRAPH_DISPLAYLINK_HEIGHT_DEFAULT}" CACHE STRING "DisplayLink output mode height")
set(PICOGRAPH_DISPLAYLINK_REFRESH "${PICOGRAPH_DISPLAYLINK_REFRESH_DEFAULT}" CACHE STRING "DisplayLink output mode refresh rate")
set(PICOGRAPH_DISPLAYLINK_COMMAND_BUFFER_SIZE "65472" CACHE STRING "DisplayLink USB command buffer size")
set(PICOGRAPH_MONOCHROME_DISPLAY_COLOR "GREEN" CACHE STRING "Monochrome display color: WHITE, GREEN, AMBER")
set(PICOGRAPH_EGA_MEMORY_KB "256" CACHE STRING "EGA memory size: 64, 128, 256")
set(PICOGRAPH_EGA_MONITOR_TYPE "COLOR_ECD" CACHE STRING "EGA monitor type: COLOR_40, COLOR_80, COLOR_ECD, MONO")
set(PICOGRAPH_SAMPLE_IO_BASE "0x300" CACHE STRING "Base I/O port for the sample module")
set(PICOGRAPH_SAMPLE_MEM_BASE "0xd0000" CACHE STRING "Base memory address for the sample module trap window")
set(PICOGRAPH_SAMPLE_MEM_SNOOP_BASE "0xb8000" CACHE STRING "Base memory address for the sample module write snoop")
set(PICOGRAPH_PSRAM_PIO "1" CACHE STRING "PIO block used by rp2040-psram")
set(PICOGRAPH_PSRAM_SM "-1" CACHE STRING "PIO state machine for rp2040-psram, or -1 to claim one")
set(PICOGRAPH_PSRAM_TARGET_MHZ "220" CACHE STRING "Target PSRAM PIO clock in MHz, matching ISA-PicoMEM's DMA path")
set(PICOGRAPH_PSRAM_PIN_CS "5" CACHE STRING "PSRAM chip-select GPIO")
set(PICOGRAPH_PSRAM_PIN_SCK "6" CACHE STRING "PSRAM clock GPIO")
set(PICOGRAPH_PSRAM_PIN_MOSI "7" CACHE STRING "PSRAM MOSI GPIO")
set(PICOGRAPH_PSRAM_PIN_MISO "4" CACHE STRING "PSRAM MISO GPIO")

if(PICOGRAPH_ENABLE_USB_HOST)
    set(PICO_TINYUSB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/tinyusb CACHE PATH "TinyUSB checkout used by the Pico SDK" FORCE)
endif()

include(pico_sdk_import.cmake)

project(picograph
    VERSION 0.1.0
    LANGUAGES C CXX ASM
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set_property(CACHE PICOGRAPH_MODULE PROPERTY STRINGS REGISTER_VIEW HERCULES EGA VGA CL5429 MDA SAMPLE)
set_property(CACHE PICOGRAPH_MONOCHROME_DISPLAY_COLOR PROPERTY STRINGS WHITE GREEN AMBER)
set_property(CACHE PICOGRAPH_EGA_MEMORY_KB PROPERTY STRINGS 64 128 256)
set_property(CACHE PICOGRAPH_EGA_MONITOR_TYPE PROPERTY STRINGS COLOR_40 COLOR_80 COLOR_ECD MONO)

pico_sdk_init()

if(PICOGRAPH_ENABLE_USB_HOST)
    if(NOT EXISTS ${PICO_TINYUSB_PATH}/src/tusb.h)
        message(FATAL_ERROR "PICOGRAPH_ENABLE_USB_HOST requires the lib/tinyusb submodule")
    endif()
    if(PICOGRAPH_ENABLE_XINPUT)
        add_library(tinyusb INTERFACE)
        target_link_libraries(tinyusb INTERFACE tinyusb_host)
        add_subdirectory(lib/tusb_xinput)
    endif()
endif()

if(PICOGRAPH_ENABLE_PSRAM)
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/rp2040-psram/CMakeLists.txt)
        message(FATAL_ERROR "PICOGRAPH_ENABLE_PSRAM requires the lib/rp2040-psram submodule")
    endif()
    add_subdirectory(lib/rp2040-psram)
endif()

if(PICOGRAPH_ENABLE_DISPLAYLINK)
    if(NOT PICOGRAPH_ENABLE_USB_HOST)
        message(FATAL_ERROR "PICOGRAPH_ENABLE_DISPLAYLINK requires PICOGRAPH_ENABLE_USB_HOST")
    endif()
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/libdlo/CMakeLists.txt)
        message(FATAL_ERROR "PICOGRAPH_ENABLE_DISPLAYLINK requires lib/libdlo")
    endif()
    add_subdirectory(lib/libdlo)
    if(TARGET libdlo AND NOT TARGET tusb_libdlo)
        add_library(tusb_libdlo ALIAS libdlo)
    endif()
    if(TARGET libdlo)
        target_compile_definitions(libdlo INTERFACE
            DLO_USB_COMMAND_BUFFER_SIZE=${PICOGRAPH_DISPLAYLINK_COMMAND_BUFFER_SIZE}
        )
    else()
        target_compile_definitions(tusb_libdlo INTERFACE
            DLO_USB_COMMAND_BUFFER_SIZE=${PICOGRAPH_DISPLAYLINK_COMMAND_BUFFER_SIZE}
        )
    endif()
endif()

if(PICOGRAPH_ENABLE_GC)
    if(NOT EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/gc/CMakeLists.txt)
        message(FATAL_ERROR "PICOGRAPH_ENABLE_GC requires lib/gc")
    endif()
    add_subdirectory(lib/gc)
    if(PICOGRAPH_ENABLE_DISPLAYLINK AND NOT TARGET gc::displaylink)
        message(FATAL_ERROR "PICOGRAPH_ENABLE_GC with PICOGRAPH_ENABLE_DISPLAYLINK requires gc::displaylink")
    endif()
endif()

set(PICOGRAPH_MODULE_SOURCE "")
set(PICOGRAPH_MODULE_SUPPORT_SOURCE "")
if(PICOGRAPH_MODULE STREQUAL "REGISTER_VIEW")
    set(PICOGRAPH_MODULE_SOURCE src/modules/register_view.cpp)
elseif(PICOGRAPH_MODULE STREQUAL "HERCULES")
    set(PICOGRAPH_MODULE_SOURCE src/modules/hercules_device.cpp)
    list(APPEND PICOGRAPH_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOGRAPH_MODULE STREQUAL "EGA")
    set(PICOGRAPH_MODULE_SOURCE src/modules/ega_device.cpp)
    list(APPEND PICOGRAPH_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOGRAPH_MODULE STREQUAL "VGA")
    set(PICOGRAPH_MODULE_SOURCE src/modules/vga_device.cpp)
    list(APPEND PICOGRAPH_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOGRAPH_MODULE STREQUAL "CL5429")
    set(PICOGRAPH_MODULE_SOURCE src/modules/cl5429_device.cpp)
    list(APPEND PICOGRAPH_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOGRAPH_MODULE STREQUAL "MDA")
    set(PICOGRAPH_MODULE_SOURCE src/modules/mda_device.cpp)
    list(APPEND PICOGRAPH_MODULE_SUPPORT_SOURCE src/modules/dlodirty.cpp)
elseif(PICOGRAPH_MODULE STREQUAL "SAMPLE")
    set(PICOGRAPH_MODULE_SOURCE src/modules/sample_device.cpp)
else()
    message(FATAL_ERROR "Unknown PICOGRAPH_MODULE=${PICOGRAPH_MODULE}")
endif()

add_executable(picograph
    src/main.cpp
    src/framework/module_registry.cpp
    src/hw/picograph_bus.cpp
    ${PICOGRAPH_MODULE_SOURCE}
    ${PICOGRAPH_MODULE_SUPPORT_SOURCE}
)

target_include_directories(picograph PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(picograph PRIVATE
    PICOGRAPH_VERSION="${PROJECT_VERSION}"
    PICOGRAPH_SYS_CLK_KHZ=${PICOGRAPH_SYS_CLK_KHZ}
    PICOGRAPH_SAMPLE_IO_BASE=${PICOGRAPH_SAMPLE_IO_BASE}
    PICOGRAPH_SAMPLE_MEM_BASE=${PICOGRAPH_SAMPLE_MEM_BASE}
    PICOGRAPH_SAMPLE_MEM_SNOOP_BASE=${PICOGRAPH_SAMPLE_MEM_SNOOP_BASE}
    PICO_DEFAULT_UART=0
    PICO_DEFAULT_UART_BAUD_RATE=115200
    PICO_DEFAULT_UART_TX_PIN=28
    PICO_DEFAULT_UART_RX_PIN=-1
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=250
)

if(PICOGRAPH_ENABLE_USB_HOST)
    target_sources(picograph PRIVATE
        src/hw/usb_host.cpp
    )
    target_include_directories(picograph PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/hw/usb_host
    )
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_ENABLE_USB_HOST=1
        PICOGRAPH_ENABLE_XINPUT=$<BOOL:${PICOGRAPH_ENABLE_XINPUT}>
        CFG_TUSB_CONFIG_FILE=\"picograph_tusb_config.h\"
    )
else()
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_ENABLE_USB_HOST=0
        PICOGRAPH_ENABLE_XINPUT=0
    )
endif()

if(PICOGRAPH_ENABLE_PSRAM)
    target_sources(picograph PRIVATE
        src/hw/psram.cpp
    )
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_ENABLE_PSRAM=1
        PICOGRAPH_PSRAM_PIO=${PICOGRAPH_PSRAM_PIO}
        PICOGRAPH_PSRAM_SM=${PICOGRAPH_PSRAM_SM}
        PICOGRAPH_PSRAM_TARGET_MHZ=${PICOGRAPH_PSRAM_TARGET_MHZ}
        PSRAM_ASYNC=1
        PSRAM_SPINLOCK=1
        PSRAM_PIN_CS=${PICOGRAPH_PSRAM_PIN_CS}
        PSRAM_PIN_SCK=${PICOGRAPH_PSRAM_PIN_SCK}
        PSRAM_PIN_MOSI=${PICOGRAPH_PSRAM_PIN_MOSI}
        PSRAM_PIN_MISO=${PICOGRAPH_PSRAM_PIN_MISO}
    )
else()
    target_compile_definitions(picograph PRIVATE PICOGRAPH_ENABLE_PSRAM=0)
endif()

if(PICOGRAPH_ENABLE_DISPLAYLINK)
    target_link_libraries(picograph tusb_libdlo)
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_ENABLE_DISPLAYLINK=1
        PICOGRAPH_DISPLAYLINK_WIDTH=${PICOGRAPH_DISPLAYLINK_WIDTH}
        PICOGRAPH_DISPLAYLINK_HEIGHT=${PICOGRAPH_DISPLAYLINK_HEIGHT}
        PICOGRAPH_DISPLAYLINK_REFRESH=${PICOGRAPH_DISPLAYLINK_REFRESH}
        GCDLO_FORCED_RES_WIDTH=${PICOGRAPH_DISPLAYLINK_WIDTH}
        GCDLO_FORCED_RES_HEIGHT=${PICOGRAPH_DISPLAYLINK_HEIGHT}
        GCDLO_FORCED_RES_HZ=${PICOGRAPH_DISPLAYLINK_REFRESH}
        CFG_TUH_API_EDPT_XFER=1
    )
else()
    target_compile_definitions(picograph PRIVATE PICOGRAPH_ENABLE_DISPLAYLINK=0)
endif()

if(PICOGRAPH_ENABLE_GC)
    if(PICOGRAPH_ENABLE_DISPLAYLINK)
        target_link_libraries(picograph gc::displaylink)
    else()
        target_link_libraries(picograph gc::core)
    endif()
    target_compile_definitions(picograph PRIVATE PICOGRAPH_ENABLE_GC=1)
else()
    target_compile_definitions(picograph PRIVATE PICOGRAPH_ENABLE_GC=0)
endif()

if(PICOGRAPH_MONOCHROME_DISPLAY_COLOR STREQUAL "WHITE")
    set(PICOGRAPH_MONOCHROME_DISPLAY_COLOR_VALUE 5)
elseif(PICOGRAPH_MONOCHROME_DISPLAY_COLOR STREQUAL "GREEN")
    set(PICOGRAPH_MONOCHROME_DISPLAY_COLOR_VALUE 3)
elseif(PICOGRAPH_MONOCHROME_DISPLAY_COLOR STREQUAL "AMBER")
    set(PICOGRAPH_MONOCHROME_DISPLAY_COLOR_VALUE 4)
else()
    message(FATAL_ERROR "Unknown PICOGRAPH_MONOCHROME_DISPLAY_COLOR=${PICOGRAPH_MONOCHROME_DISPLAY_COLOR}")
endif()

if(PICOGRAPH_EGA_MEMORY_KB STREQUAL "64")
    set(PICOGRAPH_EGA_MEMORY_KB_VALUE 64)
elseif(PICOGRAPH_EGA_MEMORY_KB STREQUAL "128")
    set(PICOGRAPH_EGA_MEMORY_KB_VALUE 128)
elseif(PICOGRAPH_EGA_MEMORY_KB STREQUAL "256")
    set(PICOGRAPH_EGA_MEMORY_KB_VALUE 256)
else()
    message(FATAL_ERROR "Unknown PICOGRAPH_EGA_MEMORY_KB=${PICOGRAPH_EGA_MEMORY_KB}")
endif()

if(PICOGRAPH_EGA_MONITOR_TYPE STREQUAL "COLOR_40")
    set(PICOGRAPH_EGA_MONITOR_TYPE_VALUE 6)
elseif(PICOGRAPH_EGA_MONITOR_TYPE STREQUAL "COLOR_80")
    set(PICOGRAPH_EGA_MONITOR_TYPE_VALUE 7)
elseif(PICOGRAPH_EGA_MONITOR_TYPE STREQUAL "COLOR_ECD")
    set(PICOGRAPH_EGA_MONITOR_TYPE_VALUE 9)
elseif(PICOGRAPH_EGA_MONITOR_TYPE STREQUAL "MONO")
    set(PICOGRAPH_EGA_MONITOR_TYPE_VALUE 10)
else()
    message(FATAL_ERROR "Unknown PICOGRAPH_EGA_MONITOR_TYPE=${PICOGRAPH_EGA_MONITOR_TYPE}")
endif()

if(PICOGRAPH_MODULE STREQUAL "REGISTER_VIEW")
    target_compile_definitions(picograph PRIVATE PICOGRAPH_MODULE_REGISTER_VIEW=1)
elseif(PICOGRAPH_MODULE STREQUAL "HERCULES")
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_MODULE_HERCULES=1
        PICOGRAPH_MONOCHROME_DISPLAY_COLOR=${PICOGRAPH_MONOCHROME_DISPLAY_COLOR_VALUE}
    )
elseif(PICOGRAPH_MODULE STREQUAL "EGA")
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_MODULE_EGA=1
        PICOGRAPH_EGA_MEMORY_KB=${PICOGRAPH_EGA_MEMORY_KB_VALUE}
        PICOGRAPH_EGA_MONITOR_TYPE=${PICOGRAPH_EGA_MONITOR_TYPE_VALUE}
        PICOGRAPH_MONOCHROME_DISPLAY_COLOR=${PICOGRAPH_MONOCHROME_DISPLAY_COLOR_VALUE}
    )
elseif(PICOGRAPH_MODULE STREQUAL "VGA")
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_MODULE_VGA=1
        PICOGRAPH_VGA_MEMORY_KB=256
    )
elseif(PICOGRAPH_MODULE STREQUAL "CL5429")
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_MODULE_CL5429=1
        PICOGRAPH_CL5429_MEMORY_KB=256
    )
elseif(PICOGRAPH_MODULE STREQUAL "MDA")
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_MODULE_MDA=1
        PICOGRAPH_MONOCHROME_DISPLAY_COLOR=${PICOGRAPH_MONOCHROME_DISPLAY_COLOR_VALUE}
    )
elseif(PICOGRAPH_MODULE STREQUAL "SAMPLE")
    target_compile_definitions(picograph PRIVATE PICOGRAPH_MODULE_SAMPLE=1)
endif()

if(PICOGRAPH_BOARD STREQUAL "PICOMEM_M1")
    set(PICOGRAPH_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_m1.pio)
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_BOARD_PICOMEM_M1=1
        PICOGRAPH_MUX_V2=0
        PICOGRAPH_PIO_NEEDS_INVALID_CYCLE=1
    )
elseif(PICOGRAPH_BOARD STREQUAL "PICOMEM_M2")
    set(PICOGRAPH_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_m2.pio)
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_BOARD_PICOMEM_M2=1
        PICOGRAPH_MUX_V2=1
        PICOGRAPH_PIO_NEEDS_INVALID_CYCLE=0
    )
elseif(PICOGRAPH_BOARD STREQUAL "PICOMEM_LP")
    set(PICOGRAPH_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_noaen.pio)
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_BOARD_PICOMEM_LP=1
        PICOGRAPH_MUX_V2=0
        PICOGRAPH_PIO_NEEDS_INVALID_CYCLE=1
    )
elseif(PICOGRAPH_BOARD STREQUAL "PICOMEM_15")
    set(PICOGRAPH_ISA_PIO ${CMAKE_CURRENT_LIST_DIR}/pio/isa_iomem_m2_v15.pio)
    target_compile_definitions(picograph PRIVATE
        PICOGRAPH_BOARD_PICOMEM_15=1
        PICOGRAPH_MUX_V2=1
        PICOGRAPH_PIO_NEEDS_INVALID_CYCLE=0
        PICO_PIO_USE_GPIO_BASE=1
    )
else()
    message(FATAL_ERROR "Unknown PICOGRAPH_BOARD=${PICOGRAPH_BOARD}")
endif()

pico_generate_pio_header(picograph ${PICOGRAPH_ISA_PIO})

target_link_libraries(picograph
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_clocks
    hardware_vreg
)

if(PICOGRAPH_ENABLE_USB_HOST)
    target_link_libraries(picograph
        tinyusb_board
        tinyusb_host
    )
    if(PICOGRAPH_ENABLE_XINPUT)
        target_link_libraries(picograph xinput_host)
    endif()
endif()

if(PICOGRAPH_ENABLE_PSRAM)
    target_link_libraries(picograph
        rp2040-psram
        hardware_dma
        hardware_sync
    )
endif()

pico_set_program_name(picograph "picograph")
pico_set_program_version(picograph ${PROJECT_VERSION})
pico_enable_stdio_uart(picograph 1)
pico_enable_stdio_usb(picograph 0)
pico_enable_stdio_semihosting(picograph 0)
pico_add_extra_outputs(picograph)

if(PICO_PLATFORM MATCHES "rp2350")
    set(PICOGRAPH_OPENOCD_TARGET target/rp2350.cfg)
else()
    set(PICOGRAPH_OPENOCD_TARGET target/rp2040.cfg)
endif()

add_custom_target(
    program
    DEPENDS picograph
    COMMAND openocd -f interface/picoprobe.cfg -f ${PICOGRAPH_OPENOCD_TARGET} -c "program picograph.elf verify reset exit"
)
