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
# THE AUDIO PATH MUST NOT TOUCH THE QMI BUS (measured, see below). PSRAM at
# 0x11000000 is the *cached* XIP window for QMI CS1, and flash code at
# 0x10000000 is the cached window for CS0 — they share one 8 KB XIP cache. So
# while core0 streams texture tiles out of PSRAM in-level it (a) saturates QMI
# and (b) continuously evicts core1's flash-resident audio code from that cache.
# Every audio instruction fetch then becomes a cache miss that queues behind
# core0's tile reads.
#
# That is what starved the sink in-level: the HDMI audio queue sat at ~50/512
# with ~1500-2700 underruns/s during gameplay while the identical build held
# ~520 with zero underruns in the menu, where core0 leaves PSRAM alone. It also
# explains why halving the synth cost (reference renderer -> LINEAR+slot_render)
# barely moved the underrun rate: core1 was not short of cycles, it was stalled
# on the bus. Only ~48 KB total moves here, against ~252 KB of free SRAM.

# SRAM taken off the top for the two stack sections (core0's plus the SDK's
# unused core1 one, both sized by PICO_STACK_SIZE — see src/pico/CMakeLists.txt).
# Must be >= 2 * PICO_STACK_SIZE; ld errors out if the sections do not fit.
set(DUKE_STACK_REGION "16k" CACHE STRING "SRAM reserved at the top for stacks")

# audiolib translation units inside libduke.a (mixer, MIDI sequencer, FM driver).
set(DUKE_AUDIOLIB_OBJS
    multivoc.c.o mv_mix.c.o   pitch.c.o    midi.c.o   al_midi.c.o
    fx_man.c.o   mvreverb.c.o ll_man.c.o   usrhooks.c.o nodpmi.c.o
)
# Everything on core1's audio path, as linker file patterns.
set(DUKE_AUDIO_PATH_OBJS *duke_audio.c.o *duke_music.c.o *emu8950.c.o *slot_render.cpp.o)
foreach(_o IN LISTS DUKE_AUDIOLIB_OBJS)
    list(APPEND DUKE_AUDIO_PATH_OBJS "*libduke.a:${_o}")
endforeach()

function(duke_use_psram_linker_script target)
    set(_src "${PICO_SDK_PATH}/src/rp2_common/pico_crt0/rp2350/memmap_default.ld")
    set(_dst "${CMAKE_BINARY_DIR}/memmap_duke.ld")

    file(READ "${_src}" _ld)
    string(JOIN " " _audio ${DUKE_AUDIO_PATH_OBJS})

    # 0) Bootloader variant: move FLASH to the pico-bootLoader app partition.
    #    This lives here, and not in cmake/BootPartition.cmake where the map is
    #    documented, because pico_set_linker_script can only be honoured once per
    #    target — a second call replaces the first, silently dropping either the
    #    relocated FLASH or the PSRAM/.audio_bss/.psram_bss sections below.
    #    One generator, all patches.
    if (BUILD_FOR_BOOTLOADER)
        # Newer SDKs pull FLASH in from a generated pico_flash_region.ld; older
        # ones spell the region out inline. Handle both (the regex is idempotent
        # if the INCLUDE replacement already produced the explicit line).
        string(REPLACE
            "INCLUDE \"pico_flash_region.ld\""
            "FLASH(rx) : ORIGIN = ${DUKE_APP_BASE}, LENGTH = ${DUKE_APP_SIZE}"
            _ld "${_ld}")
        string(REGEX REPLACE
            "FLASH\\(rx\\)[ \t]*:[^\n]*"
            "FLASH(rx) : ORIGIN = ${DUKE_APP_BASE}, LENGTH = ${DUKE_APP_SIZE}"
            _ld "${_ld}")
        if (NOT _ld MATCHES "ORIGIN = ${DUKE_APP_BASE}")
            message(FATAL_ERROR
                "psram_linker: could not relocate FLASH in ${_src}. Neither the "
                "pico_flash_region.ld INCLUDE nor an inline FLASH(rx) line was "
                "found, so the image would silently link at 0x10000000 and the "
                "bootloader could not launch it.")
        endif()
        duke_bootloader_target_props(${target})
        message(STATUS "BootPartition: ${target} -> FLASH ORIGIN=${DUKE_APP_BASE} LENGTH=${DUKE_APP_SIZE}")
    endif()

    # 0b) Core0's stack -> main RAM, out of the 4 KB SCRATCH_Y bank.
    #
    #  The SDK parks core0's stack at the top of SCRATCH_Y, which caps it at
    #  4 KB. That is not enough for this engine: BUILD/Duke have ~1.9 KB frames
    #  (resetpspritevars, tics, enterlevel) and the SDK default is only 2 KB, so
    #  with the stock layout deep paths ran off the bottom and survived purely
    #  because the memory under it happened to be unused. Raising
    #  PICO_STACK_SIZE alone does not fix it either: it sizes BOTH stack
    #  sections, so 4096 exactly fills SCRATCH_Y *and* SCRATCH_X and then any
    #  __scratch_x / __scratch_y user fails to link — emu8950's slot_render.cpp
    #  puts 1 KB in __scratch_y and did exactly that.
    #
    #  So carve a dedicated STACK region off the TOP of RAM and put both stack
    #  sections there. That leaves both scratch banks entirely free for their
    #  intended users. crt0 sets SP from __stack == __StackTop, so redirecting
    #  the two symbol assignments below is what actually moves the stack.
    #
    #  A DEDICATED REGION, not just "> RAM": placing the stacks in RAM lets them
    #  land at the current cursor, i.e. immediately after .bss — which is exactly
    #  where the HEAP starts. sbrk grows from `end` (end of .bss) up to
    #  __StackLimit, so the heap then eats the stack after ~18 KB of malloc, and
    #  Duke allocates well past that during MV_Init. That HARDFAULTED on hardware
    #  with garbage return addresses (pc=0x00000100) right after
    #  "audio: playback started". Shrinking RAM by the stack size instead means
    #  the SDK's own `__StackLimit = ORIGIN(RAM) + LENGTH(RAM)` now lands exactly
    #  at the stack bottom, so sbrk stops below the stack with no further edits.
    #  When touching this, check BOTH invariants: the stack must be outside
    #  __bss_start__..__bss_end__ (or crt0 zeroes the stack it is running on) AND
    #  outside `end`..__StackLimit (or the heap grows into it).
    string(REPLACE
        "    .stack1_dummy (NOLOAD):\n    {\n        *(.stack1*)\n    } > SCRATCH_X"
        "    .stack1_dummy (NOLOAD):\n    {\n        *(.stack1*)\n    } > STACK"
        _ld "${_ld}")
    string(REPLACE
        "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > SCRATCH_Y"
        "    .stack_dummy (NOLOAD):\n    {\n        KEEP(*(.stack*))\n    } > STACK"
        _ld "${_ld}")
    string(REPLACE
        "__StackOneTop = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);"
        "__StackOneTop = ADDR(.stack1_dummy) + SIZEOF(.stack1_dummy);"
        _ld "${_ld}")
    string(REPLACE
        "__StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);"
        "__StackTop = ADDR(.stack_dummy) + SIZEOF(.stack_dummy);"
        _ld "${_ld}")
    if (NOT _ld MATCHES "ADDR\\(\\.stack_dummy\\)")
        message(FATAL_ERROR
            "psram_linker: could not relocate the stack out of SCRATCH_Y in "
            "${_src}. Leaving it there silently caps core0's stack at 4 KB, "
            "which this engine overruns.")
    endif()

    # 1) Rewrite the MEMORY block in ONE replacement: shorten RAM by the stack
    #    carve-out, add the STACK region at the top of SRAM, and add PSRAM.
    #    Doing these as separate string(REPLACE)s would be a trap — they all
    #    match the same "RAM(rwx) ... LENGTH = 512k" text, so the second would
    #    splice itself into the middle of the first's output.
    string(REPLACE
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k"
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k - ${DUKE_STACK_REGION}
    STACK(rw) : ORIGIN = 0x20080000 - ${DUKE_STACK_REGION}, LENGTH = ${DUKE_STACK_REGION}
    PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = 8192k"
        _ld "${_ld}")
    if (NOT _ld MATCHES "STACK\\(rw\\)")
        message(FATAL_ERROR "psram_linker: MEMORY rewrite failed in ${_src}")
    endif()

    # 2) Audio-path CODE and read-only tables -> SRAM. Adding these objects to
    #    the flash .text/.rodata EXCLUDE_FILE lists makes them fall through to
    #    the SDK's own "remaining .text and .rodata" catch-all inside .data,
    #    which is a RAM section that crt0 copies from flash at boot. No source
    #    annotation needed, so the vendored audiolib stays untouched.
    string(REPLACE
        "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)"
        "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a: ${_audio}) .text*)"
        _ld "${_ld}")
    string(REPLACE
        "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata*)"
        "*(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a: ${_audio}) .rodata*)"
        _ld "${_ld}")

    # 3) Audio-path DATA -> SRAM. The blanket *libduke.a:* in .psram_bss below
    #    was also sweeping audiolib's per-sample state into PSRAM (MV_PanTable,
    #    the voice list, MV_MixDestination and the MIDI channel state all landed
    #    at 0x111f_xxxx), so core1 was reading and writing PSRAM for every
    #    sample it mixed.
    #
    #    Claiming it in a RAM section placed *before* .psram_bss is what works:
    #    ld places each input section once, at the first spec that matches, so
    #    the blanket below then only sees the engine/game arrays it is meant
    #    for. (An EXCLUDE_FILE inside .psram_bss silently does nothing here --
    #    it does not honour the archive:member syntax that the outer file spec
    #    needs.) Placement also matters for correctness, not just speed: this
    #    lands between .tbss (which defines __bss_start__) and .bss (which ends
    #    with __bss_end__), so crt0 zeroes it on the way up, whereas .psram_bss
    #    sits outside that range and has to be cleared by hand in
    #    duke_psram_init once PSRAM is actually mapped.
    set(_audiolib_bss "")
    foreach(_o IN LISTS DUKE_AUDIOLIB_OBJS)
        string(APPEND _audiolib_bss
               "        *libduke.a:${_o}(.bss .bss.* .sbss .sbss.* COMMON)\n")
    endforeach()

    set(_psram_section
"    .audio_bss (NOLOAD) : {
        . = ALIGN(4);
        __audio_bss_start = .;
${_audiolib_bss}        . = ALIGN(4);
        __audio_bss_end = .;
    } > RAM

    .psram_bss (NOLOAD) : {
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
