#!/bin/sh
# Build pico-duke3D for HW_CONFIG 2 (Adafruit DVI Breakout + MicroSD breakout on
# a Pimoroni Pico Plus 2), linked to run from the pico-bootLoader's app partition
# instead of owning flash.
#
# Differences from ./adafruitdvisd-build.sh:
#   * -DBUILD_FOR_BOOTLOADER=ON, which relinks FLASH to ORIGIN 0x10080000 and
#     defines BUILD_FOR_BOOTLOADER=1 for the C code.
#   * separate build dir, so the two variants never share stale objects.
#
# NO flash-map overrides, unlike ./murmulatorm2-build-forbootloader.sh: the Pico
# Plus 2 has the same 16 MB as the Fruit Jam, so cmake/BootPartition.cmake's
# defaults already describe this board —
#     0x10000000  bootloader          512 KB
#     0x10080000  app partition       15.5 MB  (DUKE_APP_SIZE=0xF80000)
#     0x11000000  end of 16 MB flash (also the PSRAM XIP window on QMI CS1)
# Duke keeps NOTHING in flash — DUKE3D.GRP streams from SD and savegames go next
# to it — so the whole partition is the app's.
#
# Install: copy the UF2 to the SD card under /emu/2/ (the bootloader's picker
# scans /emu/<HW_CONFIG>/), with DUKE3D.GRP at /roms/duke3d/ as usual. The picker
# flashes it into the app partition and jumps to it; any reset returns to the
# picker, as does quitting Duke (see _exit in src/pico/duke_boot.c).
#
# Artifact: build_bl_adafruitdvisd/src/pico/duke3d_game.uf2
set -e
TAG=adafruitdvisd
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
    -DPICO_BOARD=pimoroni_pico_plus2_rp2350 \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DENABLE_PIO_USB=0 \
    -DDUKE_NESPAD=ON \
    -DBUILD_FOR_BOOTLOADER=ON \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2  -> SD:/emu/2/"
