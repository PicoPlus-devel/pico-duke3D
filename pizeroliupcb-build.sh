#!/bin/sh
# Build pico-duke3D for Liu Szuping's PCB for the Waveshare RP2350-PiZero
# (HW_CONFIG 18) — HSTX video on the PCB's HDMI port, HDMI + PCM5102 audio,
# PIO-USB host on the PiZero's PIO-USB port, two NES/SNES controller ports and
# the PiZero's SD card slot on SPI1.
#
# Differences from ./fruitjam-build.sh, all of which have to move together:
#   * -DDUKE_BOARD=pizeroliupcb force-includes pizeroliupcb_cflags.h instead of
#     fruitjam_cflags.h. Everything the C code branches on comes from there.
#   * -DPICO_BOARD=waveshare_rp2350_pizero — from boards/ (copied from
#     pico_shared; the pico-sdk does not ship it). It declares the RP2350B
#     that the SD pins (SPI1 on GPIO 30/31/40) need.
#   * -DDUKE_NESPAD=ON — links the PIO NES/SNES controller-port driver.
#   ENABLE_PIO_USB keeps its default (ON), matching HAS_USBPIO in
#   pizeroliupcb_cflags.h, so PICO_PIO_USB_PATH must be set.
#
# PSRAM must be soldered onto the PiZero (QMI CS1 = GPIO 47): a stock board has
# none, and Duke halts with a message on screen without it.
#
# Install: copy the UF2 to the board with picotool or BOOTSEL drag-and-drop, and
# put DUKE3D.GRP at /roms/duke3d/ on the SD card.
#
# Artifact: build_pizeroliupcb/src/pico/duke3d_game.uf2
set -e
TAG=pizeroliupcb
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
    -DPICO_BOARD=waveshare_rp2350_pizero \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DDUKE_NESPAD=ON \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2"
