# USB HID host input: keyboard, mouse and a wide range of USB gamepads
# (DualShock 4/5, PSClassic, Genesis Mini, Retro-bit MD, MantaPad NES/SNES
# clones, XInput/Xbox via tusb_xinput). Vendored from pico_shared, see the
# parent CMakeLists.txt for the resync procedure.
add_library(pico_shared_usb_hid INTERFACE)
target_sources(pico_shared_usb_hid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/hid_app.cpp
        ${CMAKE_CURRENT_LIST_DIR}/gamepad.cpp
)
target_include_directories(pico_shared_usb_hid INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(pico_shared_usb_hid INTERFACE xinput_host tinyusb_host)
