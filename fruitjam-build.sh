#!/bin/sh
# Build pico-duke3D for the Adafruit Fruit Jam (HW_CONFIG 8).
#
# Uses PICO_SDK_PATH from the environment. The board config header is
# force-included so the vendored pico_shared drivers see the HSTX/I2S/SD/PSRAM
# pin macros at compile time.
#
# Artifact: build_fruitjam/src/pico/duke3d_game.uf2
set -e
TAG=fruitjam
BUILD=build_${TAG}

if [ -z "${PICO_SDK_PATH}" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    exit 1
fi

# NOTE: the -include of ${TAG}_cflags.h and the board/platform/build-type
# defaults now live in CMakeLists.txt, so a plain `cmake -S . -B build`
# (e.g. VSCode / CMake Tools) configures identically. The -D flags below are
# kept for explicitness but are redundant with those defaults.
cmake -S . -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
    -DPICO_PLATFORM=rp2350-arm-s \
    -DPICO_BOARD=adafruit_fruit_jam \
    -DUSE_HSTX=1 \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/duke3d_game.uf2"
