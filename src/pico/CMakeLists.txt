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
