# pico-duke3D — Duke Nukem 3D (Chocolate Duke3D / BUILD engine) for RP2350.
#
# M0 (hardware bring-up skeleton): clocks, PSRAM, SD, HDMI test pattern and
# TLV320 headphone detection using Frank's vendored pico_shared drivers. The
# BUILD engine + Duke game are vendored in later milestones.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Defaults so a plain `cmake -S . -B build` works ------------------------
# (e.g. VSCode / CMake Tools, which does not pass the -D flags or CFLAGS that
# fruitjam-build.sh sets). All are set only if not already provided, so the
# build script and command-line -D overrides still win. These MUST precede the
# pico_sdk_init.cmake include, which selects the toolchain from PICO_PLATFORM.
if (NOT PICO_BOARD)
    set(PICO_BOARD adafruit_fruit_jam CACHE STRING "Target board (Adafruit Fruit Jam)")
endif()
if (NOT PICO_PLATFORM)
    set(PICO_PLATFORM rp2350-arm-s CACHE STRING "RP2350 ARM secure")
endif()
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type (binary size is tight on RP2350)")
endif()
if (NOT DEFINED USE_HSTX)
    set(USE_HSTX 1 CACHE STRING "Use the HSTX HDMI driver")
endif()

# PICO_SDK_PATH is expected in the environment or passed with -DPICO_SDK_PATH.
if (NOT PICO_SDK_PATH)
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH (env var or -DPICO_SDK_PATH=...)")
    endif()
endif()
# Canonicalize: /home/frank/pico/pico-sdk is a SYMLINK. If one configure run
# uses the symlink form and a later reconfigure of the same build dir uses the
# resolved form (or vice versa), the SDK's add_subdirectory calls see
# "out-of-tree" paths and configuration fails. Resolving here makes every
# configure (build script, CLI, VSCode/CMake Tools) use the same
# representation, so reconfigures are always consistent.
get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH)
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

# Pico-PIO-USB (USB host on GPIO 1/2 via PIO). The SDK's bundled tinyusb reads
# this CMake variable in check_and_add_pico_pio_usb_support(); the SDK tree has
# no bundled copy, so resolve it from the environment.
if (NOT PICO_PIO_USB_PATH AND DEFINED ENV{PICO_PIO_USB_PATH})
    set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH})
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(pico_duke3d C CXX ASM)
set(CMAKE_C_STANDARD 11)
# C++17: the vendored pico_shared usb_hid headers (used from M3) need
# inline static constexpr; harmless for M0.
set(CMAKE_CXX_STANDARD 17)

# Force-include the board config header into every C/C++ TU (NOT ASM). The
# vendored pico_shared drivers and the Duke engine read its pin / HW_CONFIG
# macros at compile time. This replaces the CFLAGS="-include ..." that
# fruitjam-build.sh used to export, so a plain IDE/CMake-Tools configure builds
# too. Appended to the (non-cache) flag vars so it does not accumulate across
# reconfigures, and guarded so an explicit CFLAGS -include stays a harmless dup.
# -g3 -ggdb: debug symbols for source-level debugging in VSCode. They live in
# the .elf only (stripped from the .uf2/.bin), so there is no flash-size cost
# even in MinSizeRel. Matches fruitjam-doom.
if (NOT CMAKE_C_FLAGS MATCHES "fruitjam_cflags.h")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/fruitjam_cflags.h -g3 -ggdb")
endif()
if (NOT CMAKE_CXX_FLAGS MATCHES "fruitjam_cflags.h")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/fruitjam_cflags.h -g3 -ggdb")
endif()

pico_sdk_init()

if (PICO_ON_DEVICE AND NOT CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    message(WARNING "Prefer -DCMAKE_BUILD_TYPE=MinSizeRel on RP2350 (binary size is tight).")
endif()

# USB host over Pico-PIO-USB: the first call inside tinyusb's family.cmake ran
# before pico_sdk_init() (no pico_generate_pio_header yet) and was a no-op;
# this one actually creates the tinyusb_pico_pio_usb target and appends
# hcd_pio_usb.c to tinyusb_host_base (pattern from pico-infonesPlus).
check_and_add_pico_pio_usb_support()
if (NOT TARGET tinyusb_pico_pio_usb)
    message(FATAL_ERROR "tinyusb_pico_pio_usb target missing — check PICO_PIO_USB_PATH (${PICO_PIO_USB_PATH})")
endif()

# --- Vendored pico_shared drivers -------------------------------------------
# Video (HSTX/HDMI), audio (I2S + TLV320 codec + headphone detect), PSRAM,
# SD/FatFs, and USB HID input (keyboard/mouse/gamepads + XInput).
add_subdirectory(3rdparty/pico_shared_drivers/drivers/tlv320dac3100)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_audio_i2s)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_hdmi)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_fatfs)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_psram)
add_subdirectory(3rdparty/tusb_xinput)
add_subdirectory(3rdparty/pico_shared_drivers/usb_hid)

# Duke3D BUILD engine + game logic (static lib; platform layer + link in src/pico).
include(cmake/duke_engine.cmake)

add_subdirectory(src/pico)
