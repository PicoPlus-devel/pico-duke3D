#!/bin/sh
# Build pico-duke3D for the Pico Computer 3 (HW_CONFIG 17) — HSTX video on its
# HDMI connector, HDMI + PCM5102 audio, native USB host behind the board's USB
# hub, two NES/SNES ports and a Wii port on the I/O header, and the SD card on
# SPI1.
#
# Differences from ./fruitjam-build.sh, all of which have to move together:
#   * -DDUKE_BOARD=picocomputer3 force-includes picocomputer3_cflags.h instead
#     of fruitjam_cflags.h. Everything the C code branches on comes from there.
#   * -DPICO_BOARD=weact_studio_rp2350b_core — as pico_shared builds this
#     config; the pico-sdk ships it. It declares the RP2350B that the SD pins
#     (SPI1 on GPIO 28/30/31) need.
#   * -DENABLE_PIO_USB=0 — USB host runs on the RP2350's NATIVE controller.
#     This MUST match the absence of HAS_USBPIO in picocomputer3_cflags.h;
#     duke_usb_input.cpp #errors if they disagree.
#   * -DDUKE_NESPAD=ON — links the PIO NES/SNES controller-port driver.
#   The serial console stays on: UART1 on GPIO 8/9 (picocomputer3_cflags.h).
#
# Install: copy the UF2 to the board with picotool or BOOTSEL drag-and-drop, and
# put DUKE3D.GRP at /roms/duke3d/ on the SD card.
#
# Artifact: build_picocomputer3/src/pico/duke3d_game.uf2
set -e
TAG=picocomputer3
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
    -DPICO_BOARD=weact_studio_rp2350b_core \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DENABLE_PIO_USB=0 \
    -DDUKE_NESPAD=ON \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2"
