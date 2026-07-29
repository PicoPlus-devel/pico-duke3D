#!/bin/sh
# Build pico-duke3D for the Adafruit Fruit Jam (HW_CONFIG 8), linked to run
# from the pico-bootLoader's app partition instead of owning flash.
#
# Differences from ./fruitjam-build.sh:
#   * -DBUILD_FOR_BOOTLOADER=ON, which relinks FLASH to ORIGIN 0x10080000
#     (see cmake/BootPartition.cmake for the map) and defines
#     BUILD_FOR_BOOTLOADER=1 for the C code.
#   * separate build dir, so the two variants never share stale objects.
#
# Install: copy the UF2 to the SD card under /emu/8/ (the bootloader's picker
# scans /emu/<HW_CONFIG>/), with DUKE3D.GRP at /roms/duke3d/ as usual. The
# picker flashes it into the app partition and jumps to it; any reset returns
# to the picker, as does quitting Duke (see _exit in src/pico/duke_boot.c).
#
# Artifact: build_bl_fruitjam/src/pico/duke3d_game.uf2
set -e
TAG=fruitjam
BUILD=build_bl_${TAG}

if [ -z "${PICO_SDK_PATH}" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    exit 1
fi

cmake -S . -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
    -DPICO_PLATFORM=rp2350-arm-s \
    -DPICO_BOARD=adafruit_fruit_jam \
    -DUSE_HSTX=1 \
    -DBUILD_FOR_BOOTLOADER=ON \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2  -> SD:/emu/8/"
