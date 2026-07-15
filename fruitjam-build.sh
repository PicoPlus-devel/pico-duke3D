#!/bin/sh
# Build pico-duke3D for the Adafruit Fruit Jam (HW_CONFIG 8).
#
# Uses PICO_SDK_PATH from the environment. The board config header is
# force-included so the vendored pico_shared drivers see the HSTX/I2S/SD/PSRAM
# pin macros at compile time.
#
# Artifact: build_fruitjam/src/pico/duke3d.uf2
set -e
TAG=fruitjam
BUILD=build_${TAG}

if [ -z "${PICO_SDK_PATH}" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    exit 1
fi

export CFLAGS="-include $(pwd)/${TAG}_cflags.h"
export CXXFLAGS="-include $(pwd)/${TAG}_cflags.h"

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
echo "Artifact: $BUILD/src/pico/duke3d.uf2"
