#!/bin/sh
# ====================================================================================
# PICO-DUKE3D build all script
#
# Builds every released configuration and collects the artifacts in releases/.
# This is the whole body of the release workflow: what CI runs is exactly
# `./buildAll.sh`, so running it locally is the way to test the pipeline.
#
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   export PICO_PIO_USB_PATH=/path/to/Pico-PIO-USB     # Fruit Jam only
#   ./buildAll.sh
#
# Optional: CMAKE_ARGS="-DDUKE_RELEASE_VERSION=v1.0" to stamp a version into the
# UF2 binary info (visible in `picotool info`).
#
# Released per board: the standalone build only. The pico-bootLoader variants are
# NOT released here -- they are built in the pico-bootLoader repo, which calls
# <tag>-build-forbootloader.sh directly.
#
# There is no data file to ship: Duke keeps nothing in flash, DUKE3D.GRP streams
# from the SD card. A release is one UF2 per board and nothing else.
# ====================================================================================
cd "$(dirname "$0")" || exit 1

. ./boards.sh

# --- toolchain ---------------------------------------------------------------
if [ -z "$PICO_SDK_PATH" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    echo "    export PICO_SDK_PATH=\$HOME/pico/pico-sdk" >&2
    exit 1
fi
if [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    echo "PICO_SDK_PATH=$PICO_SDK_PATH has no pico_sdk_init.cmake -- not a Pico SDK checkout" >&2
    exit 1
fi

for TAG in $RELEASE_BOARDS; do
    if board_needs_pio_usb "$TAG" && [ -z "$PICO_PIO_USB_PATH" ]; then
        echo "PICO_PIO_USB_PATH is not set, but $TAG drives USB host over PIO" >&2
        echo "    export PICO_PIO_USB_PATH=\$HOME/pico/Pico-PIO-USB" >&2
        exit 1
    fi
done

# Every <tag>-build.sh passes -G Ninja.
if ! command -v ninja > /dev/null 2>&1; then
    echo "ninja could not be found (the build scripts pass -G Ninja)" >&2
    exit 1
fi

# picotool builds the UF2s (pico_add_extra_outputs). Without it on PATH the SDK
# fetches and builds its own copy, which is slow and silent; and
# cmake/BootPartition.cmake probes `picotool version --semantic` for the >= 2.2.0
# that gives UF2s the --platform rp2350 flag.
if ! command -v picotool > /dev/null 2>&1; then
    echo "picotool could not be found" >&2
    echo "Please install picotool from https://github.com/raspberrypi/picotool.git" >&2
    exit 1
fi
PICOTOOL_VER=$(picotool version --semantic 2>/dev/null)
case "$PICOTOOL_VER" in
    ""|0.*|1.*|2.0*|2.1*)
        echo "picotool $PICOTOOL_VER is too old; 2.2.0 or newer is required" >&2
        exit 1 ;;
esac
echo "Using picotool $PICOTOOL_VER"

[ -d releases ] && rm -rf releases
mkdir releases || exit 1

# ---------------------------------------------------------------------------
# Firmware: one standalone build per board.
#
# Every board's build script writes src/pico/duke3d_game.uf2 under the same
# basename, so the copy into releases/ has to rename per board or they would
# overwrite each other. Copy that exact path rather than globbing *.uf2: the
# Fruit Jam build tree also holds duke3d.uf2, the M0 hardware bring-up harness
# that src/pico/CMakeLists.txt only builds for that board.
# ---------------------------------------------------------------------------
for TAG in $RELEASE_BOARDS; do
    echo ""
    echo "=================== $TAG ==================="
    ./${TAG}-build.sh || exit 1
    cp "build_${TAG}/src/pico/duke3d_game.uf2" "releases/duke3d_game_${TAG}.uf2" || exit 1
done

# ---------------------------------------------------------------------------
if [ -z "$(ls -A releases)" ]; then
    echo "No files found in releases folder" >&2
    exit 1
fi

echo ""
echo "=================== releases/ ==================="
for UF2 in releases/*.uf2; do
    ls -l "$UF2"
    picotool info "$UF2"
    echo " "
done
echo "$(ls releases | wc -l) files in releases/"
