# BootPartition.cmake — flash map for the pico-bootLoader app partition.
#
# The pico-bootLoader (https://github.com/FrankHoedemakers/pico-bootLoader) owns
# the first 512 KB of flash and treats everything after it as one application
# partition. Because the bootloader sits at the very start of flash the bootrom
# always runs IT first, so every reset / power cycle lands back in the picker and
# an app only runs when the picker jumps to it. To be launchable that way,
# duke3d_game must link with FLASH ORIGIN at the partition base instead of the
# RP2350 default 0x10000000.
#
#   0x10000000  bootloader (menu/flasher)      512 KB
#   0x10080000  application partition          15.5 MB   <- APP_BASE_ADDR
#   0x11000000  end of 16 MB flash (also the PSRAM XIP window on QMI CS1)
#
# The whole partition goes to the app on purpose. fruitjam-doom caps its app at
# 3 MB because its WHX asset blob is flashed at 0x10400000, but Duke streams
# DUKE3D.GRP from SD and keeps nothing in flash, so there is nothing to leave
# room for and no in-flash save region either (saves go to SD next to the GRP).
# Sizes here mirror pico-bootLoader's src/boot_config.h — keep them in agreement.
#
# NOTE: this file deliberately does NOT call pico_set_linker_script. The FLASH
# rewrite is applied by cmake/psram_linker.cmake, which is the single owner of
# the generated linker script, because that script must ALSO carry the PSRAM
# region, .audio_bss and .psram_bss. Two independent pico_set_linker_script
# calls would silently drop one set of patches.

if(DEFINED _DUKE_BOOTPARTITION_INCLUDED)
    return()
endif()
set(_DUKE_BOOTPARTITION_INCLUDED 1)

# CACHE so a -D on the cmake line wins (other boards with smaller flash can
# override DUKE_APP_SIZE / DUKE_FLASH_TOTAL from their build script).
set(DUKE_XIP_BASE    "0x10000000" CACHE STRING "RP2350 XIP flash base")
set(DUKE_APP_BASE    "0x10080000" CACHE STRING "Bootloader app-partition base")
set(DUKE_APP_SIZE    "0xF80000"   CACHE STRING "Bytes reserved for the app (15.5 MB = 16 MB - 512 KB bootloader)")
set(DUKE_FLASH_TOTAL "0x1000000"  CACHE STRING "Total external flash on the board (Fruit Jam = 16 MB)")

# Target properties that go with a relinked image. Called from
# duke_use_psram_linker_script() so there is exactly one place to keep in sync.
function(duke_bootloader_target_props TARGET)
    # Force the real chip capacity: hard_assert in flash_range_erase /
    # flash_range_program panics if a board header understates it.
    target_compile_definitions(${TARGET} PRIVATE
        PICO_FLASH_SIZE_BYTES=${DUKE_FLASH_TOTAL})

    # picotool >= 2.2.0 re-detects the chip from the ELF rather than trusting
    # --family, and assumes RP2040 for images that do not start at 0x10000000 —
    # which is exactly what a relinked image looks like. Tell it explicitly.
    # The option does not exist before 2.2.0, so version-guard it.
    if(PICO_PLATFORM MATCHES "rp2350")
        find_program(_duke_picotool picotool)
        if(_duke_picotool)
            execute_process(COMMAND ${_duke_picotool} version --semantic
                            OUTPUT_VARIABLE _duke_pt_ver
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_duke_pt_ver VERSION_GREATER_EQUAL 2.2.0)
                set_target_properties(${TARGET} PROPERTIES
                    PICOTOOL_EXTRA_UF2_ARGS "--platform;rp2350")
            endif()
        endif()
    endif()
endfunction()
