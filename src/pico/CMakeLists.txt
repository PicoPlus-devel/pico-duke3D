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
    ${CMAKE_SOURCE_DIR}/3rdparty/emu8950/slot_render.cpp
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

# emu8950 (whole file is gated on USE_EMU8950_OPL): doom's LINEAR C renderer
# (slot_render.cpp) — the config rp2350-doom shipped as "EMU8950_ASM=0". The
# unoptimized reference path proved far too slow on core0 (starved the frame
# loop: no music output, input polling dead). NOT EMU8950_ASM: that variant
# has a known RP2350 state-corruption bug (doom memory). NO_RATECONV per doom
# precedent: renders at the OPL's native 49716 Hz; at 48 kHz playback music is
# ~3.45% flat, inaudible.
target_compile_definitions(duke3d_game PRIVATE
    USE_EMU8950_OPL=1
    EMU8950_LINEAR=1
    EMU8950_LINEAR_SKIP=1
    EMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION
    # slot_render.cpp is the C implementation of the slot-render path (the
    # EMU8950_ASM .S variant is the one with the RP2350 corruption bug).
    EMU8950_SLOT_RENDER=1
    EMU8950_NO_PERCUSSION_MODE=1
    EMU8950_NO_WAVE_TABLE_MAP=1
    EMU8950_NO_TLL=1
    EMU8950_NO_FLOAT=1
    EMU8950_NO_RATECONV=1
    EMU8950_NO_TIMER=1
    EMU8950_NO_TEST_FLAG=1
)

# emu8950 embeds OPL_SLOT_RENDER as an unnamed struct member (Microsoft
# anonymous-tag extension) — same flag fruitjam-doom's opl CMake uses.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/3rdparty/emu8950/emu8950.c
    ${CMAKE_SOURCE_DIR}/3rdparty/emu8950/slot_render.cpp
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
    hardware_interp        # emu8950 slot_render.cpp drives the RP2 interpolator
)

pico_enable_stdio_uart(duke3d_game 1)
pico_enable_stdio_usb(duke3d_game 0)

# -fcommon: the sound stubs include duke3d.h, which carries tentative defs that
# must merge with libduke's (compiled -fcommon) rather than multiply-define.
target_compile_options(duke3d_game PRIVATE -fcommon)

# Video diagnostics (off by default; the signal-loss hunt is over). Reenable
# with -DDUKE_VIDEO_DIAG=1: turns on pico_hdmi's HSTX_DEBUG 1 Hz core1 dump
# and the core0 "vid:" status line in pico_display.c.
if (DUKE_VIDEO_DIAG)
    target_compile_definitions(duke3d_game PRIVATE HSTX_DEBUG=1 DUKE_VIDEO_DIAG=1)
endif()

# (The audio pump now runs as core1's video background task — the frame-paced
# and timer-IRQ pump variants are gone; see duke_audio.c.)

duke_use_psram_linker_script(duke3d_game)

pico_add_extra_outputs(duke3d_game)
