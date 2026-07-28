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

    # 1) Add the PSRAM MEMORY region.
    string(REPLACE
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k"
        "RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 512k\n    PSRAM(rwx) : ORIGIN = 0x11000000, LENGTH = 8192k"
        _ld "${_ld}")

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
