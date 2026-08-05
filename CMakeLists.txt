# pico-duke3D — Duke Nukem 3D (Chocolate Duke3D / BUILD engine) for RP2350.
#
# M0 (hardware bring-up skeleton): clocks, PSRAM, SD, HDMI test pattern and
# TLV320 headphone detection using Frank's vendored pico_shared drivers. The
# BUILD engine + Duke game are vendored in later milestones.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# --- Board selection --------------------------------------------------------
# DUKE_BOARD names the <tag>_cflags.h header that is force-included into every
# TU (see below) and, with it, everything the C code branches on: HSTX lanes,
# I2S driver, SD/PSRAM pins, USB transport, controller ports. Add a board by
# dropping in a <tag>_cflags.h and a <tag>-build.sh; nothing else here is
# board-specific.
#
#   fruitjam      Adafruit Fruit Jam       (HW_CONFIG 8,  PIO-USB, TLV320)
#   murmulatorm2  Murmulator M2 / Pico 2   (HW_CONFIG 13, native USB, PCM5100A)
#   adafruitdvisd Adafruit DVI + SD boards (HW_CONFIG 2,  native USB, PCM5100A;
#                 needs a Pico Plus 2 — PSRAM is on GPIO 47, an RP2350B pin)
#
set(DUKE_BOARD "fruitjam" CACHE STRING "Board tag: selects <tag>_cflags.h")
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${DUKE_BOARD}_cflags.h")
    message(FATAL_ERROR
        "DUKE_BOARD=${DUKE_BOARD} but ${DUKE_BOARD}_cflags.h does not exist. "
        "Use one of the <tag>-build.sh scripts, or add the header.")
endif()
message(STATUS "pico-duke3D board: ${DUKE_BOARD} (${DUKE_BOARD}_cflags.h)")

# --- Version stamp ----------------------------------------------------------
# Recorded in the UF2's binary info, so `picotool info duke3d_game.uf2` says
# which release an image came from. The release workflow passes the tag through
# CMAKE_ARGS (-DDUKE_RELEASE_VERSION=v1.0); an ordinary build stamps "dev".
# Nothing tracked in the repository is rewritten to cut a release.
set(DUKE_RELEASE_VERSION "dev" CACHE STRING "Version stamp recorded in the UF2 binary info")

# --- Defaults so a plain `cmake -S . -B build` works ------------------------
# (e.g. VSCode / CMake Tools, which does not pass the -D flags or CFLAGS that
# fruitjam-build.sh sets). All are set only if not already provided, so the
# build script and command-line -D overrides still win. These MUST precede the
# pico_sdk_init.cmake include, which selects the toolchain from PICO_PLATFORM.
# The PICO_BOARD default follows DUKE_BOARD's default: a plain configure builds
# for the Fruit Jam, which is what VSCode / CMake Tools gets.
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
# Every build script passes -DUSE_HSTX=1, and the video path is selected by the
# board header's USE_HSTX, not by this variable. Read it once so a configure run
# does not end with CMake's "Manually-specified variables were not used" warning.
message(STATUS "pico-duke3D video: HSTX=${USE_HSTX}")

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

# Bootloader variant. ON links duke3d_game into the pico-bootLoader's app
# partition at 0x10080000 (see cmake/BootPartition.cmake for the flash map)
# instead of owning flash from 0x10000000, so the picker can launch it and
# every reset returns to the picker. Use ./fruitjam-build-forbootloader.sh.
# The relink itself is applied by cmake/psram_linker.cmake, which owns the
# generated linker script; BUILD_FOR_BOOTLOADER is also visible to C so the
# platform layer can adapt (e.g. restart() rebooting back into the picker).
option(BUILD_FOR_BOOTLOADER "Link for the pico-bootLoader app partition" OFF)
if (BUILD_FOR_BOOTLOADER)
    add_compile_definitions(BUILD_FOR_BOOTLOADER=1)
endif()
include(cmake/BootPartition.cmake)

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
set(DUKE_BOARD_CFLAGS "${CMAKE_CURRENT_SOURCE_DIR}/${DUKE_BOARD}_cflags.h")
if (NOT CMAKE_C_FLAGS MATCHES "${DUKE_BOARD}_cflags.h")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${DUKE_BOARD_CFLAGS} -g3 -ggdb")
endif()
if (NOT CMAKE_CXX_FLAGS MATCHES "${DUKE_BOARD}_cflags.h")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${DUKE_BOARD_CFLAGS} -g3 -ggdb")
endif()

pico_sdk_init()

if (PICO_ON_DEVICE AND NOT CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    message(WARNING "Prefer -DCMAKE_BUILD_TYPE=MinSizeRel on RP2350 (binary size is tight).")
endif()

# --- USB host transport ------------------------------------------------------
# ENABLE_PIO_USB picks it at link time and MUST agree with the HAS_USBPIO macro
# in the board's cflags header, which is what tusb_config.h and duke_usb_input
# branch on. The two are cross-checked at compile time (DUKE_PIO_USB is handed
# to the C code in src/pico/CMakeLists.txt, which #errors on a mismatch) —
# without that, disagreeing settings link cleanly and produce a board with no
# input at all. Default ON so the Fruit Jam scripts keep working unchanged;
# native-USB boards pass -DENABLE_PIO_USB=0.
if (NOT DEFINED ENABLE_PIO_USB)
    set(ENABLE_PIO_USB 1)
endif()
if (ENABLE_PIO_USB)
    # The first call inside tinyusb's family.cmake ran before pico_sdk_init()
    # (no pico_generate_pio_header yet) and was a no-op; this one actually
    # creates the tinyusb_pico_pio_usb target and appends hcd_pio_usb.c to
    # tinyusb_host_base (pattern from pico-infonesPlus).
    check_and_add_pico_pio_usb_support()
    if (NOT TARGET tinyusb_pico_pio_usb)
        message(FATAL_ERROR "tinyusb_pico_pio_usb target missing — check PICO_PIO_USB_PATH (${PICO_PIO_USB_PATH})")
    endif()
    message(STATUS "USB host: Pico-PIO-USB")
else()
    message(STATUS "USB host: native RP2350 controller")
endif()

# --- Legacy NES/SNES controller ports ----------------------------------------
# Boards with wired controller ports (Murmulator M2) set this; the pin macros
# themselves come from the board cflags header. OFF leaves nespad.cpp and its
# PIO program out of the image entirely, so the Fruit Jam build is unchanged.
option(DUKE_NESPAD "Poll legacy NES/SNES controller ports over PIO" OFF)

# --- Vendored pico_shared drivers -------------------------------------------
# Video (HSTX/HDMI), audio (I2S + TLV320 codec + headphone detect), PSRAM,
# SD/FatFs, and USB HID input (keyboard/mouse/gamepads + XInput).
#
# i2c_bus_recovery goes first because tlv320dac3100 links it: the codec shares
# SDA/SCL with the Wii extension port, and an uninitialised pad on that bus can
# hold SDA low so every codec register access times out.
add_subdirectory(3rdparty/pico_shared_drivers/drivers/i2c_bus_recovery)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/tlv320dac3100)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_audio_i2s)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_hdmi)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_fatfs)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_psram)
add_subdirectory(3rdparty/tusb_xinput)
add_subdirectory(3rdparty/pico_shared_drivers/usb_hid)
if (DUKE_NESPAD)
    add_subdirectory(3rdparty/pico_shared_drivers/nespad)
endif()
# Wii-extension pads (NES/SNES Classic Mini, Wii Classic Controller Pro) over
# I2C. Unconditional, unlike nespad: the whole translation unit is wrapped in
#   #if WII_PIN_SDA >= 0 and WII_PIN_SCL >= 0
# and the board header that defines those pins is force-included above, so a
# board without a connector compiles it away to an empty object.
add_subdirectory(3rdparty/pico_shared_drivers/wiipad)

# Duke3D BUILD engine + game logic (static lib; platform layer + link in src/pico).
include(cmake/duke_engine.cmake)

add_subdirectory(src/pico)
