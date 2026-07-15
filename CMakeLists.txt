# pico-duke3D — Duke Nukem 3D (Chocolate Duke3D / BUILD engine) for RP2350.
#
# M0 (hardware bring-up skeleton): clocks, PSRAM, SD, HDMI test pattern and
# TLV320 headphone detection using Frank's vendored pico_shared drivers. The
# BUILD engine + Duke game are vendored in later milestones.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# PICO_SDK_PATH is expected in the environment or passed with -DPICO_SDK_PATH.
if (NOT PICO_SDK_PATH)
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    else()
        message(FATAL_ERROR "Set PICO_SDK_PATH (env var or -DPICO_SDK_PATH=...)")
    endif()
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(pico_duke3d C CXX ASM)
set(CMAKE_C_STANDARD 11)
# C++17: the vendored pico_shared usb_hid headers (used from M3) need
# inline static constexpr; harmless for M0.
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

if (PICO_ON_DEVICE AND NOT CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    message(WARNING "Prefer -DCMAKE_BUILD_TYPE=MinSizeRel on RP2350 (binary size is tight).")
endif()

# --- Vendored pico_shared drivers needed for the M0 bring-up ----------------
# Video (HSTX/HDMI), audio (I2S + TLV320 codec + headphone detect), PSRAM and
# SD/FatFs. usb_hid + nespad + wiipad are added in the input milestone (M3).
add_subdirectory(3rdparty/pico_shared_drivers/drivers/tlv320dac3100)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_audio_i2s)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_hdmi)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_fatfs)
add_subdirectory(3rdparty/pico_shared_drivers/drivers/pico_psram)

add_subdirectory(src/pico)
