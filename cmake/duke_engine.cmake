# Chocolate Duke3D BUILD engine + Duke game logic as a static library for RP2350.
#
# M1: compile the portable pure-C core. SDL backends (display.c, dsl.c,
# sdl_midi.c), multiplayer (mmulti*, multi.c) and the DOS audiolib drivers are
# excluded; the platform layer + link step are added separately (src/pico/).

set(DUKE_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/Engine)
set(DUKE_GAME_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/src/Game)

add_library(duke STATIC
    # --- BUILD engine (pure C, kept as-is) ---
    ${DUKE_ENGINE_DIR}/engine.c
    ${DUKE_ENGINE_DIR}/draw.c
    ${DUKE_ENGINE_DIR}/cache.c
    ${DUKE_ENGINE_DIR}/tiles.c
    ${DUKE_ENGINE_DIR}/fixedPoint_math.c
    ${DUKE_ENGINE_DIR}/dummy_multi.c
    ${DUKE_ENGINE_DIR}/network.c        # thin dispatcher; mmulti stubbed at link
    ${DUKE_ENGINE_DIR}/filesystem.c     # GRP index kept; low-level I/O -> FatFs (M1 link step)
    # --- Duke game logic (pure C, kept as-is) ---
    ${DUKE_GAME_DIR}/actors.c
    ${DUKE_GAME_DIR}/animlib.c
    ${DUKE_GAME_DIR}/config.c
    ${DUKE_GAME_DIR}/console.c
    ${DUKE_GAME_DIR}/control.c
    ${DUKE_GAME_DIR}/cvar_defs.c
    ${DUKE_GAME_DIR}/cvars.c
    ${DUKE_GAME_DIR}/game.c
    ${DUKE_GAME_DIR}/gamedef.c
    ${DUKE_GAME_DIR}/global.c
    ${DUKE_GAME_DIR}/keyboard.c
    ${DUKE_GAME_DIR}/menues.c
    ${DUKE_GAME_DIR}/player.c
    ${DUKE_GAME_DIR}/premap.c
    ${DUKE_GAME_DIR}/rts.c
    ${DUKE_GAME_DIR}/scriplib.c
    ${DUKE_GAME_DIR}/sector.c
    ${DUKE_GAME_DIR}/sounds.c
    # --- audiolib software mixer (pure C; the SDL-build subset minus dsl.c,
    #     which src/pico/duke_audio.c replaces) ---
    ${DUKE_GAME_DIR}/audiolib/fx_man.c
    ${DUKE_GAME_DIR}/audiolib/multivoc.c
    ${DUKE_GAME_DIR}/audiolib/mv_mix.c
    ${DUKE_GAME_DIR}/audiolib/mvreverb.c
    ${DUKE_GAME_DIR}/audiolib/pitch.c
    ${DUKE_GAME_DIR}/audiolib/ll_man.c
    ${DUKE_GAME_DIR}/audiolib/user.c
    ${DUKE_GAME_DIR}/audiolib/usrhooks.c
    ${DUKE_GAME_DIR}/audiolib/nodpmi.c
    # --- MIDI music chain (M5): sequencer + AdLib FM driver. music.c (DOS
    #     sound-card matrix) is replaced by src/pico/duke_music.c; the OPL
    #     port I/O and task_man scheduling resolve to shims there too. ---
    ${DUKE_GAME_DIR}/audiolib/midi.c
    ${DUKE_GAME_DIR}/audiolib/al_midi.c
    ${DUKE_GAME_DIR}/audiolib/gmtimbre.c
)

# NOTE: audiolib/ and midi/ are deliberately NOT on the include path — duke3d.h
# refers to the sound headers with the "audiolib/" prefix, and putting audiolib/
# on -I would shadow the system <assert.h> with audiolib/assert.h.
target_include_directories(duke PUBLIC
    ${DUKE_ENGINE_DIR}
    ${DUKE_GAME_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/pico/shim
)

target_compile_definitions(duke PUBLIC PLATFORM_PICO=1)

# The 1996 source puns pointers to int32 heavily and is UB-sensitive; match the
# upstream configure.ac flags. -w silences the (very noisy) legacy warnings.
target_compile_options(duke PRIVATE
    -fno-strict-aliasing
    -fno-aggressive-loop-optimizations
    -w
    # The 1996 sources put tentative definitions (int _argc; char typebuf[41];)
    # in shared headers and rely on the pre-gcc-10 -fcommon default to merge
    # them. Modern gcc defaults to -fno-common (one strong def per TU) which
    # would multiply-define them at link. Restore -fcommon.
    -fcommon
    # Per-symbol sections so the linker script can relocate individual big
    # arrays (.bss.<name>) into PSRAM without touching the source.
    -ffunction-sections
    -fdata-sections
)

# premap.c must be built at -O0 (upstream comment: higher optimization crashes
# on new-game due to undefined behaviour).
set_source_files_properties(${DUKE_GAME_DIR}/premap.c PROPERTIES COMPILE_OPTIONS "-O0")

# No pico/SDK libraries are linked into this static lib: doing so would compile
# the SDK's INTERFACE sources (gpio.c, ...) into duke with duke's include paths.
# The engine/game core needs only newlib. The platform layer (src/pico) links
# the SDK and FatFs and is where duke's file/video/audio hooks are resolved.
