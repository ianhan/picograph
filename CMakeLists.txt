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
set(PICOMEM_MODULE "TEMPLATE" CACHE STRING "Single module to build into this firmware")
set(PICOMEM_SYS_CLK_KHZ "280000" CACHE STRING "RP2040/RP2350 system clock in kHz")
set(PICOMEM_ENABLE_USB_HOST ON CACHE BOOL "Enable TinyUSB host support")
set(PICOMEM_ENABLE_XINPUT ON CACHE BOOL "Enable the TinyUSB XInput host class")
set(PICOMEM_ENABLE_PSRAM ON CACHE BOOL "Enable rp2040-psram support")
set(PICOMEM_ENABLE_DISPLAYLINK ON CACHE BOOL "Enable DisplayLink USB display support")
set(PICOMEM_ENABLE_GC ON CACHE BOOL "Enable GC graphics support")
set(PICOMEM_DISPLAYLINK_SCALE "2" CACHE STRING "Integer DisplayLink scale factor applied between GC and libdlo")
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

add_executable(picomem
    src/main.cpp
    src/framework/module_registry.cpp
    src/hw/picomem_bus.cpp
    src/modules/template_device.cpp
)

target_include_directories(picomem PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(picomem PRIVATE
    PICOMEM_VERSION="${PROJECT_VERSION}"
    PICOMEM_SYS_CLK_KHZ=${PICOMEM_SYS_CLK_KHZ}
    PICOMEM_TEMPLATE_IO_BASE=${PICOMEM_TEMPLATE_IO_BASE}
    PICOMEM_TEMPLATE_MEM_BASE=${PICOMEM_TEMPLATE_MEM_BASE}
    PICOMEM_DISPLAYLINK_SCALE=${PICOMEM_DISPLAYLINK_SCALE}
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

if(PICOMEM_MODULE STREQUAL "TEMPLATE")
    target_compile_definitions(picomem PRIVATE PICOMEM_MODULE_TEMPLATE=1)
else()
    message(FATAL_ERROR "Unknown PICOMEM_MODULE=${PICOMEM_MODULE}")
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
