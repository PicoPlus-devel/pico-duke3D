# Legacy NES/SNES controller input via PIO (two ports, SNES auto-detected at
# runtime). Vendored from the pico_shared root (nespad.h/nespad.cpp/nespad.pio),
# see the parent CMakeLists.txt for the resync procedure. The PIO program is an
# inline instruction array in nespad.cpp — nespad.pio is reference documentation
# only, no pico_generate_pio_header needed. nespad.cpp reads the force-included
# HW_CONFIG macro to select the shared-CLK/LAT program variant (Murmulator M2).
#
# Copied from pico-quake2, which has pico_shared fb9ad91 by way of pico-lutionX:
# SNES Mouse with NESPAD_SNES_MOUSE (which a board header may define; 32-clock
# read then, 18-clock otherwise), nespad_padtype[], and nespad_read_start()
# doing nothing while a read is pending. nespad_padtype[] replaces the old local
# nespad_is_nes[]; see padIsNes() in src/pico/duke_usb_input.cpp.
#
# LOCAL PATCH (keep on resync / upstream to pico_shared): nespad_read_ready()
# in nespad.{h,cpp} — non-blocking "finish will not block" check, used by
# duke_usb_input.cpp so a controller port can never hang the game loop.
add_library(pico_shared_nespad INTERFACE)
target_sources(pico_shared_nespad INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/nespad.cpp
)
target_include_directories(pico_shared_nespad INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(pico_shared_nespad INTERFACE hardware_pio)
