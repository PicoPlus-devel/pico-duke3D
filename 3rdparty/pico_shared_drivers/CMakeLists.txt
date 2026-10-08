# Vendored copies of Frank's pico_shared driver library.
# Upstream: https://github.com/... /pico-infonesPlus/pico_shared/drivers
#
# Each subdirectory declares an INTERFACE library:
#   pico_hdmi                (HSTX video + HDMI data-island audio)
#   pico_audio_i2s_shared    (I2S PIO+DMA, renamed to avoid clash with pico-extras)
#   tlv320dac3100            (Fruit Jam DAC + headphone-detect)
#   pico_shared_usb_hid      (USB HID host: keyboard/mouse/gamepads + xinput)
#   pico_shared_nespad       (legacy NES/SNES pads on PIO, two ports)
#   pico_shared_wiipad       (Wii-extension pads over I2C: NES/SNES Classic Mini,
#                             Wii Classic Controller (Pro))
#   i2c_bus_recovery         (standard SCL-pulse bus clear; used by the DAC and
#                             the Wii pad, which share SDA/SCL)
#   pico_fatfs               (FatFs R0.15 + SD card over HW-SPI / PIO-SPI)
#   pico_psram               (RP2350 QMI CS1 PSRAM init; trimmed, see its CMakeLists)
#   pico_shared_pwm_audio    (stereo PWM audio jack fed with the HDMI samples;
#                             Olimex RP2040-PICO-PC)
#
# NOTE: this file is documentation, not build input. The root CMakeLists.txt
# adds each driver directory by full path and never adds this one, so a
# subdirectory listed here is only compiled if it is listed there too.
#
# Resync by re-copying from pico_shared/drivers — only pico_audio_i2s/CMakeLists.txt
# is patched (target renamed). usb_hid is copied from the pico_shared root
# (hid_app.cpp, gamepad.cpp, gamepad.h) and depends on the 3rdparty/tusb_xinput
# submodule for the XInput host class driver. nespad and wiipad are copied from
# the pico_shared root as well (nespad.h, nespad.cpp, nespad.pio; wiipad.h,
# wiipad.cpp — wiipad's CMakeLists.txt is ours, upstream builds it as part of
# the pico_shared library rather than a driver of its own), and so is pwm_audio
# (pwm_audio.h, pwm_audio.c; CMakeLists.txt ours for the same reason). pico_fatfs
# omits upstream's tests/, doc/ and fatfs_R0.15Patch1/ (not built there
# either); pico_psram compiles SetupPsram.c only.

add_subdirectory(drivers/i2c_bus_recovery)
add_subdirectory(drivers/tlv320dac3100)
add_subdirectory(drivers/pico_audio_i2s)
add_subdirectory(drivers/pico_hdmi)
if (PICO_ON_DEVICE)
    add_subdirectory(usb_hid)
    add_subdirectory(nespad)
    add_subdirectory(wiipad)
    add_subdirectory(pwm_audio)
    # SD + PSRAM (used by the doom_tiny_full WHD_LOAD_FROM_SD variant)
    add_subdirectory(drivers/pico_fatfs)
    add_subdirectory(drivers/pico_psram)
endif()
