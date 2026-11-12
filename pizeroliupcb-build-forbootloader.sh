#!/bin/sh
# Build pico-duke3D for Liu Szuping's PCB for the Waveshare RP2350-PiZero
# (HW_CONFIG 18), linked to run from the pico-bootLoader's app partition
# instead of owning flash.
#
# Differences from ./pizeroliupcb-build.sh:
#   * -DBUILD_FOR_BOOTLOADER=ON, which relinks FLASH to ORIGIN 0x10080000 and
#     defines BUILD_FOR_BOOTLOADER=1 for the C code.
#   * no DUKE_APP_SIZE / DUKE_FLASH_TOTAL overrides: the PiZero has 16 MB of
#     flash, like the Fruit Jam, so the defaults in cmake/BootPartition.cmake
#     apply. BoardConfigs.cmake reserves nothing at the top of flash for this
#     board.
#   * separate build dir, so the two variants never share stale objects.
#
# Install: copy the UF2 to SD:/emu/18/ and pick it in the bootloader's menu.
set -e
TAG=pizeroliupcb
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
    -DPICO_BOARD=waveshare_rp2350_pizero \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DDUKE_NESPAD=ON \
    -DBUILD_FOR_BOOTLOADER=ON \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2  -> SD:/emu/18/"
