# Generate a linker script that adds an 8 MB PSRAM region and relocates ALL of
# libduke.a's .bss into it, leaving SDK/driver/platform .bss (DMA buffers, HSTX
# and audio rings, stacks) in SRAM. Patterned on fruitjam-doom's BootPartition
# approach: copy the SDK default memmap and string-patch it.
#
# The .psram_bss section lives at the PSRAM XIP window (0x11000000), which is
# only mapped after SetupPsram() runs, and is NOT within __bss_start__..
# __bss_end__, so crt0 never touches it — the boot code zeroes it manually
# (duke_psram_init) after PSRAM is up. The region above it (__psram_heap_start
# .. __psram_heap_end) is a runtime bump heap for the cache1d tile cache.
function(duke_use_psram_linker_script target)
    set(_src "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld")
    set(_dst "${CMAKE_BINARY_DIR}/memmap_duke.ld")

    file(READ "${_src}" _ld)

    # 1) Add the PSRAM MEMORY region.
    string(REPLACE
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k"
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k\n    PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = 8192k"
        _ld "${_ld}")

    # 2) Insert the .psram_bss section immediately before the SRAM .bss so the
    #    libduke sections are claimed here first (each input section is placed
    #    once; the later *(.bss*) catch-all then only sees SDK/driver bss).
    set(_psram_section
"    .psram_bss (NOLOAD) : {
        . = ALIGN(4);
        __psram_bss_start = .;
        *libduke.a:*(.bss .bss.* .sbss .sbss.* COMMON)
        . = ALIGN(32);
        __psram_bss_end = .;
        __psram_heap_start = .;
    } > PSRAM
    __psram_heap_end = ORIGIN(PSRAM) + LENGTH(PSRAM);

    .bss (NOLOAD) : {")
    string(REPLACE "    .bss (NOLOAD) : {" "${_psram_section}" _ld "${_ld}")

    file(WRITE "${_dst}" "${_ld}")
    pico_set_linker_script(${target} "${_dst}")
endfunction()
