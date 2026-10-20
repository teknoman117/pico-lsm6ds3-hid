# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico-lsm6ds3 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_library(lsm6ds3_pid STATIC lib/lsm6ds3-pid/lsm6ds3_reg.c)
target_include_directories(lsm6ds3_pid PUBLIC lib/lsm6ds3-pid)

add_executable(pico-lsm6ds3
        src/main.c
        src/imu.c
        src/gamepad.c
        src/usb_descriptors.c
        src/status_led.c
        )

pico_set_program_name(pico-lsm6ds3 "pico-lsm6ds3")
pico_set_program_version(pico-lsm6ds3 "0.1")

pico_generate_pio_header(pico-lsm6ds3 ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

# stdio over SEGGER RTT (debug probe) in Debug builds only
pico_enable_stdio_uart(pico-lsm6ds3 0)
pico_enable_stdio_usb(pico-lsm6ds3 0)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    pico_enable_stdio_rtt(pico-lsm6ds3 1)
endif()

target_include_directories(pico-lsm6ds3 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        )

# picotool reset interface alongside our own TinyUSB descriptors
target_compile_definitions(pico-lsm6ds3 PRIVATE
        PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
        PICO_USB_RESET_INCLUDE_DEFAULT_APP_DRIVER_CB=1
        PICO_USB_RESET_SUPPORT_MS_OS_20_DESCRIPTOR=0
        )

target_link_libraries(pico-lsm6ds3
        pico_stdlib
        pico_unique_id
        pico_usb_reset
        hardware_spi
        hardware_pio
        tinyusb_device
        lsm6ds3_pid
        )

pico_add_extra_outputs(pico-lsm6ds3)
