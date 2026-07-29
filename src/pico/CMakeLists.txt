# M0 bring-up executable. The BUILD engine + Duke game and the full platform
# layer (baselayer/video/sound/input/grp loader) are added in later milestones.

add_executable(duke3d
    pico_main.c
)

target_link_libraries(duke3d PRIVATE
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_pll
    hardware_vreg
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_spi
    pico_hdmi
    pico_audio_i2s_shared
    tlv320dac3100
    pico_fatfs
    pico_psram
)

# Console on UART0 (GPIO 44/45 per fruitjam_cflags.h); native USB is free for
# the PIO-USB host wiring added in M3.
pico_enable_stdio_uart(duke3d 1)
pico_enable_stdio_usb(duke3d 0)

pico_add_extra_outputs(duke3d)

# --- M1: the full Duke3D game image ----------------------------------------
# Links the BUILD engine + game (libduke) with the RP2350 platform layer. The
# custom linker script (memmap_duke.ld) relocates libduke's .bss into PSRAM.
include(${CMAKE_SOURCE_DIR}/cmake/psram_linker.cmake)

add_executable(duke3d_game
    duke_boot.c
    duke_psram.c
    duke_fatfs_io.c
    pico_display.c
    duke_usb_input.cpp
    duke_audio.c
    duke_music.c
    ${CMAKE_SOURCE_DIR}/3rdparty/emu8950/emu8950.c
    duke_platform_stub.c
    duke_net_stub.c
)

# tusb_config.h lives here; tinyusb sources are INTERFACE-compiled into this
# target, so this include dir (plus the force-included board cflags for
# HAS_USBPIO) is how the host stack finds its configuration.
target_include_directories(duke3d_game PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_SOURCE_DIR}/3rdparty/emu8950
)

# emu8950 (whole file is gated on USE_EMU8950_OPL). See the per-define notes
# below for why this is the LINEAR/slot-render path rather than the plain
# reference C renderer. Its hot functions are also forced SRAM-resident (see the
# __not_in_flash_func patch in 3rdparty/emu8950/emu8950.c — the lookup tables are
# non-const and were already in RAM), because executing per-sample audio code
# from flash makes core1 fetch over XIP while core0 streams textures through the
# same QMI bus.
#
# NOT EMU8950_ASM: that variant has a known RP2350 state-corruption bug.
# (LINEAR without SLOT_RENDER does not build upstream — `hack_ch` debug cruft
# escapes its `#if DUMPO` guard in the non-slot-render linear code.)
# Renderer choice. "reference" is the plain C renderer: it is the one signed off
# as sounding right on hardware, and it is bit-identical to what the host
# harness renders, so its output can be checked offline against a WAV.
# "linear" is Graham Sanderson's RP2-tuned
# batch path (silent-slot skipping, end-of-note early-out, RP2350 interpolator) --
# roughly half the cost, but never quality-validated on device, and it is the one
# piece of the chain host forensics cannot check (host builds compile
# slot_render's !PICO_ON_DEVICE C fallbacks).
#
# Default is "reference" deliberately: the in-level audio starvation that tempted
# us to trade sound quality for cycles turned out NOT to be a cycle shortage at
# all -- core1's audio code and data were living in flash/PSRAM behind the QMI bus
# that core0 saturates while streaming textures. See cmake/psram_linker.cmake.
# Keep "linear" as the fallback for a genuine cycle shortage only.
set(DUKE_OPL_RENDERER "reference" CACHE STRING "OPL renderer: reference | linear")

target_compile_definitions(duke3d_game PRIVATE
    USE_EMU8950_OPL=1
    EMU8950_NO_RATECONV=1     # chip always emits native 49716 Hz; duke_music.c
                              # resamples to the HDMI-locked 48 kHz sink
    EMU8950_NO_TIMER=1
    EMU8950_NO_TEST_FLAG=1
    EMU8950_NO_TLL=1          # saves a 128 KB SRAM table
)

if (DUKE_OPL_RENDERER STREQUAL "linear")
    target_sources(duke3d_game PRIVATE ${CMAKE_SOURCE_DIR}/3rdparty/emu8950/slot_render.cpp)
    target_link_libraries(duke3d_game PRIVATE hardware_interp)
    target_compile_definitions(duke3d_game PRIVATE
        EMU8950_LINEAR=1 EMU8950_LINEAR_SKIP=1
        EMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION
        EMU8950_SLOT_RENDER=1
        EMU8950_NO_PERCUSSION_MODE=1 EMU8950_NO_WAVE_TABLE_MAP=1 EMU8950_NO_FLOAT=1
    )
    set_source_files_properties(${CMAKE_SOURCE_DIR}/3rdparty/emu8950/slot_render.cpp
        PROPERTIES COMPILE_OPTIONS "-fms-extensions")
endif()
message(STATUS "Duke3D OPL renderer: ${DUKE_OPL_RENDERER}")

# emu8950 embeds OPL_SLOT_RENDER as an unnamed struct member (Microsoft
# anonymous-tag extension).
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/3rdparty/emu8950/emu8950.c
    duke_music.c
    PROPERTIES COMPILE_OPTIONS "-fms-extensions"
)

target_link_libraries(duke3d_game PRIVATE
    duke
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_pll
    hardware_vreg
    hardware_dma
    hardware_pio
    hardware_i2c
    hardware_spi
    pico_hdmi
    pico_audio_i2s_shared
    tlv320dac3100
    pico_fatfs
    pico_psram
    pico_shared_usb_hid
    tinyusb_host
    tinyusb_pico_pio_usb
)

pico_enable_stdio_uart(duke3d_game 1)
pico_enable_stdio_usb(duke3d_game 0)

# -fcommon: the sound stubs include duke3d.h, which carries tentative defs that
# must merge with libduke's (compiled -fcommon) rather than multiply-define.
target_compile_options(duke3d_game PRIVATE -fcommon)

# Video diagnostics, off by default. -DDUKE_VIDEO_DIAG=1 enables ONLY the core0
# "vid:" one-liner in pico_display.c (gf/ic/vf/rs/di/du) — that is on core0, so
# it cannot disturb the audio pump.
if (DUKE_VIDEO_DIAG)
    target_compile_definitions(duke3d_game PRIVATE DUKE_VIDEO_DIAG=1)
endif()

# pico_hdmi's own 1 Hz core1 dump is now a SEPARATE, deliberately awkward opt-in,
# because it CORRUPTS THE THING WE USE IT TO MEASURE. It runs on core1, in the
# same loop that pumps audio, and costs ~33 ms of wall time per burst: three
# printfs (~360 chars, and SDK UART stdio BUSY-WAITS, so at 115200 baud that
# alone is ~31 ms) plus two blocking frequency_count_khz() calls. The HDMI data
# island ring only holds 64 ms of audio (43 ms pump target), so one burst drains
# three quarters of the buffer and the sink underruns while core1 catches up.
# That produced exactly the in-level "di=46..108 with du climbing" symptom that
# looked like a CPU shortage. Everything it reported that we still care about
# (di level, underrun count, resync count) is in the core0 line above.
# Only enable it to debug an actual HSTX wedge, and never to judge audio.
if (DUKE_HSTX_DEBUG)
    target_compile_definitions(duke3d_game PRIVATE HSTX_DEBUG=1)
endif()

# (The audio pump runs ONLY as core1's video background task. Pumping from
# core0's idle hook as well was measured to collapse the in-level frame rate
# from ~30 to 4-15 fps for no benefit — see duke_audio.c's duke_audio_pump.)

duke_use_psram_linker_script(duke3d_game)

pico_add_extra_outputs(duke3d_game)
