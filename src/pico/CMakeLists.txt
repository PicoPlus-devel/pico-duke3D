# M0 bring-up executable. The BUILD engine + Duke game and the full platform
# layer (baselayer/video/sound/input/grp loader) are added in later milestones.
#
# FRUIT JAM ONLY, on purpose. pico_main.c is a hardware bring-up harness written
# against that board specifically: it retasks PLL_USB as the HSTX source
# unconditionally (only valid without a native USB host) and its headline test
# is TLV320 headphone-jack detection, which no other supported board has. The
# shipping image (duke3d_game below) is the portable one.
if (DUKE_BOARD STREQUAL "fruitjam")
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

    # Console on UART0 (GPIO 44/45 per fruitjam_cflags.h); native USB is free
    # for the PIO-USB host wiring added in M3.
    pico_enable_stdio_uart(duke3d 1)
    pico_enable_stdio_usb(duke3d 0)

    pico_add_extra_outputs(duke3d)
endif()

# --- M1: the full Duke3D game image ----------------------------------------
# Links the BUILD engine + game (libduke) with the RP2350 platform layer. The
# custom linker script (memmap_duke.ld) relocates libduke's .bss into PSRAM.
include(${CMAKE_SOURCE_DIR}/cmake/psram_linker.cmake)

add_executable(duke3d_game
    duke_boot.c
    duke_psram.c
    duke_fatfs_io.c
    duke_dostext.cpp
    duke_fatal.c
    ${CMAKE_SOURCE_DIR}/3rdparty/pico_shared_drivers/fonts/FrensFonts.cpp
    pico_display.c
    duke_usb_input.cpp
    duke_wiipad.cpp
    duke_leds.c
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
    ${CMAKE_SOURCE_DIR}/3rdparty/pico_shared_drivers/fonts   # pico_shared 8x8 font
)

# WS2812 program for the Fruit Jam VU meter, a verbatim copy of
# pico_shared/ws2812.pio. duke_leds.c is its only consumer, and the boards
# without a strip set DUKE_VU_WS2812_PIN -1 and never include the generated
# header at all.
pico_generate_pio_header(duke3d_game ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio)

# emu8950 (whole file is gated on USE_EMU8950_OPL). Its hot functions are forced
# SRAM-resident (see the __not_in_flash_func patch in 3rdparty/emu8950/emu8950.c
# — the lookup tables are non-const and were already in RAM), because executing
# per-sample audio code from flash makes core1 fetch over XIP while core0 streams
# textures through the same QMI bus.
#
# NOT EMU8950_ASM: that variant has a known RP2350 state-corruption bug.
# (LINEAR without SLOT_RENDER does not build upstream — `hack_ch` debug cruft
# escapes its `#if DUMPO` guard in the non-slot-render linear code.)
#
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
    # Wii-extension pads on the codec's I2C bus, under the same
    # WII_PIN_SDA != -1 convention as the NES pins; i2c_bus_recovery is the
    # shared-bus clear that duke_wiipad.cpp (and now the codec) needs.
    pico_shared_wiipad
    i2c_bus_recovery
)

# USB host transport (see the ENABLE_PIO_USB block in the root CMakeLists).
# DUKE_PIO_USB carries the CMake choice into C so duke_usb_input.cpp can
# #error on a mismatch with the board header's HAS_USBPIO — the two disagreeing
# otherwise links fine and boots a board with no input at all.
target_compile_definitions(duke3d_game PRIVATE DUKE_PIO_USB=$<BOOL:${ENABLE_PIO_USB}>)
if (ENABLE_PIO_USB)
    target_link_libraries(duke3d_game PRIVATE tinyusb_pico_pio_usb)
endif()

# Legacy NES/SNES ports (Murmulator M2). The pin macros come from the board
# cflags header; this only puts the driver in the link.
if (DUKE_NESPAD)
    target_link_libraries(duke3d_game PRIVATE pico_shared_nespad)
    target_compile_definitions(duke3d_game PRIVATE DUKE_NESPAD=1)
endif()

# Console on the UART, unless the board needs those pins for something else
# (Murmulator M2: GPIO 0/1 are its Wii connector, so DUKE_NO_STDIO_UART=1 and
# printf reaches only the DOS startup screen on HDMI). Native USB is never a
# stdio device here — it is the host port on the non-PIO-USB boards.
if (DUKE_NO_STDIO_UART)
    pico_enable_stdio_uart(duke3d_game 0)
else()
    pico_enable_stdio_uart(duke3d_game 1)
endif()
pico_enable_stdio_usb(duke3d_game 0)

# Core0 stack. The SDK default is 2 KB, which this engine overruns: BUILD/Duke
# have deep frames (resetpspritevars ~1.9 KB, tics ~1.5 KB, enterlevel ~1.3 KB),
# and three functions had multi-kilobyte buffers as locals (loadtmb 8000 B,
# saveplayer/loadplayer 20 KB each — all now static, see their comments).
# 8 KB is ~4x the largest remaining frame.
#
# cmake/psram_linker.cmake moves both stack sections out of the 4 KB SCRATCH
# banks into a dedicated STACK region carved off the top of SRAM, which is what
# lets this exceed 4 KB while leaving the scratch banks free for their intended
# __scratch_x/__scratch_y users. This value sizes BOTH stack sections, so
# 2 * PICO_STACK_SIZE must fit DUKE_STACK_REGION (ld errors if it does not).
#
# Note __StackLimit in the map is the HEAP bound, not the stack bottom; the
# bottom is __StackTop - PICO_STACK_SIZE.
target_compile_definitions(duke3d_game PRIVATE PICO_STACK_SIZE=8192)

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

# Stamp the release version into the binary info, so `picotool info` on a UF2
# names the release it came from. "dev" unless -DDUKE_RELEASE_VERSION is passed,
# which is what buildAll.sh does under CI (see CMakeLists.txt).
pico_set_program_version(duke3d_game "${DUKE_RELEASE_VERSION}")

pico_add_extra_outputs(duke3d_game)
