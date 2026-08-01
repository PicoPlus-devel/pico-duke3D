#!/bin/sh
# Build pico-duke3D for the Murmulator M2 (HW_CONFIG 13), linked to run from
# the pico-bootLoader's app partition instead of owning flash.
#
# Differences from ./murmulatorm2-build.sh:
#   * -DBUILD_FOR_BOOTLOADER=ON, which relinks FLASH to ORIGIN 0x10080000 and
#     defines BUILD_FOR_BOOTLOADER=1 for the C code.
#   * the flash map is capped to the 4 MB of a genuine Pico 2 rather than the
#     Fruit Jam's 16 MB:
#         0x10000000  bootloader          512 KB
#         0x10080000  app partition       3.5 MB   (DUKE_APP_SIZE=0x380000)
#         0x10400000  end of 4 MB flash
#     Duke keeps NOTHING in flash — DUKE3D.GRP streams from SD and savegames go
#     next to it — so the whole partition is the app's, exactly as on the Fruit
#     Jam. Only the totals differ. DUKE_FLASH_TOTAL must match the real chip:
#     it becomes PICO_FLASH_SIZE_BYTES, and flash_range_program hard_asserts
#     against it.
#   * separate build dir, so the two variants never share stale objects.
#
# Install: copy the UF2 to the SD card under /emu/13/ (the bootloader's picker
# scans /emu/<HW_CONFIG>/), with DUKE3D.GRP at /roms/duke3d/ as usual. The
# picker flashes it into the app partition and jumps to it; any reset returns
# to the picker, as does quitting Duke (see _exit in src/pico/duke_boot.c).
#
# Artifact: build_bl_murmulatorm2/src/pico/duke3d_game.uf2
set -e
TAG=murmulatorm2
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
    -DPICO_BOARD=pico2 \
    -DDUKE_BOARD=${TAG} \
    -DUSE_HSTX=1 \
    -DENABLE_PIO_USB=0 \
    -DDUKE_NESPAD=ON \
    -DDUKE_NO_STDIO_UART=1 \
    -DBUILD_FOR_BOOTLOADER=ON \
    -DDUKE_APP_SIZE=0x380000 \
    -DDUKE_FLASH_TOTAL=0x400000 \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2  -> SD:/emu/13/"
