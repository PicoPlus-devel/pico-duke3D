# Vendored from pico-infonesPlus/pico_shared/drivers/pico_psram, trimmed for
# this port: only the low-level SetupPsram() init is compiled. Upstream's
# PicoPlusPsram.cpp/.h (an lwmem-backed C++ allocator over the PSRAM window)
# is intentionally NOT copied — doom_tiny_full loads a single WHD file at the
# base of the XIP CS1 window (0x11000000), so no heap manager is needed.
# SetupPsram.h is local to this copy (upstream declares it in PicoPlusPsram.h).
if (NOT TARGET pico_psram)

    add_library(pico_psram INTERFACE)

    target_sources(pico_psram INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/SetupPsram.c
    )

    target_include_directories(pico_psram INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_link_libraries(pico_psram INTERFACE
        pico_stdlib
        hardware_clocks
        hardware_sync
    )
endif()
