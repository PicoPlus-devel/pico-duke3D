#!/bin/sh
# Build pico-duke3D for the Olimex RP2040-PICO-PC with a Raspberry Pi Pico 2
# (HW_CONFIG 15) — HSTX video, HDMI + PWM audio jack (no I2S DAC), a USB-A
# host port on the Pico's native USB and one NES/SNES pad on UEXT.
#
# Differences from ./fruitjam-build.sh, all of which have to move together:
#   * -DDUKE_BOARD=olimexpicopc force-includes olimexpicopc_cflags.h instead of
#     fruitjam_cflags.h. Everything the C code branches on comes from there.
#   * -DPICO_BOARD=pico2 — the module on the board.
#   * -DENABLE_PIO_USB=0 — USB host runs on the RP2350's NATIVE controller,
#     wired to the board's USB-A socket. This MUST match the absence of
#     HAS_USBPIO in olimexpicopc_cflags.h; duke_usb_input.cpp #errors if they
#     disagree.
#   * -DDUKE_NESPAD=ON — links the PIO NES/SNES controller-port driver.
#   * -DDUKE_NO_STDIO_UART=1 — GPIO 0/1 are the board's PS/2 port, so stdio
#     never claims uart0. The DOS startup screen on HDMI is the log.
#
# PSRAM must be fitted on GPIO 8 (QMI CS1): a stock Pico 2 has none, and Duke
# halts with a message on screen without it. It does not have to be 8 MB: the
# heap top is clamped at boot to what SetupPsram() reports, and the tile cache
# shrinks to fit (see duke_psram.c / cmake/psram_linker.cmake).
#
# Install: copy the UF2 to the board with picotool or BOOTSEL drag-and-drop, and
# put DUKE3D.GRP at /roms/duke3d/ on the SD card.
#
# Artifact: build_olimexpicopc/src/pico/duke3d_game.uf2
set -e
TAG=olimexpicopc
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
    -DPICO_BOARD=pico2 \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DENABLE_PIO_USB=0 \
    -DDUKE_NESPAD=ON \
    -DDUKE_NO_STDIO_UART=1 \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2"
