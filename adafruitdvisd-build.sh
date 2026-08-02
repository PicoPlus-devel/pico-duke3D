#!/bin/sh
# Build pico-duke3D for pico_shared HW_CONFIG 2 — an RP2350 module on a
# breadboard or Frank's PCB with the Adafruit DVI Breakout (4984) on HSTX, the
# Adafruit MicroSD breakout (254) on SPI0, an optional PCM5100A I2S DAC and two
# NES/SNES controller ports. Same board family as pico-doom's adafruitdvisd.
#
# THE MODULE MUST CARRY PSRAM. Duke relocates ~2.1 MB of engine .bss into it and
# cannot run without it, and HW_CONFIG 2 puts the PSRAM chip select on GPIO 47 =
# QMI CS1, which only exists on an RP2350B. So this targets a Pimoroni Pico
# Plus 2 (RP2350B, 8 MB PSRAM, 16 MB flash) dropped into the Pico 2 footprint —
# hence -DPICO_BOARD=pimoroni_pico_plus2_rp2350 rather than pico2, which would
# declare an RP2350A (no GPIO 47) and only 4 MB of flash.
#
# Differences from ./murmulatorm2-build.sh, all of which have to move together:
#   * -DDUKE_BOARD=adafruitdvisd force-includes adafruitdvisd_cflags.h.
#     Everything the C code branches on comes from there — note in particular
#     GPIOHSTXINVERTED 0, the only board here with non-inverted HSTX lanes.
#   * -DPICO_BOARD=pimoroni_pico_plus2_rp2350 — see above.
#   * NO -DDUKE_NO_STDIO_UART: GPIO 0/1 are free on this board (the M2 has its
#     Wii connector there), so the serial console stays on.
# Same as the M2:
#   * -DENABLE_PIO_USB=0 — USB host runs on the RP2350's NATIVE controller
#     (pads/keyboards via an OTG adapter). This MUST match the absence of
#     HAS_USBPIO in adafruitdvisd_cflags.h; duke_usb_input.cpp #errors if they
#     disagree.
#   * -DDUKE_NESPAD=ON — links the PIO NES/SNES controller-port driver.
#
# Install: copy the UF2 to the board with picotool or BOOTSEL drag-and-drop, and
# put DUKE3D.GRP at /roms/duke3d/ on the SD card. If the card, the directory,
# the GRP or the PSRAM is missing, the board now says so on the HDMI output and
# halts (src/pico/duke_fatal.c) instead of hanging with a black screen.
#
# Artifact: build_adafruitdvisd/src/pico/duke3d_game.uf2
set -e
TAG=adafruitdvisd
BUILD=build_${TAG}

if [ -z "${PICO_SDK_PATH}" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    exit 1
fi

cmake -S . -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
    -DPICO_PLATFORM=rp2350-arm-s \
    -DPICO_BOARD=pimoroni_pico_plus2_rp2350 \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DENABLE_PIO_USB=0 \
    -DDUKE_NESPAD=ON \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2"
