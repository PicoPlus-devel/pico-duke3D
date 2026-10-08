# Stereo PWM audio jack, fed with the same samples as HDMI. Vendored verbatim
# from the pico_shared root (pwm_audio.h / pwm_audio.c) — see the parent
# CMakeLists.txt for the resync procedure. No local patches.
#
# Only boards whose cflags header sets PWM_AUDIO_PIN_L >= 0 get a driver (the
# Olimex RP2040-PICO-PC: left GPIO 28, right GPIO 27). Everywhere else
# pwm_audio.h turns pwm_audio_init() / pwm_audio_push() into inline no-ops and
# pwm_audio.c compiles to nothing, so linking this costs those boards nothing.
add_library(pico_shared_pwm_audio INTERFACE)
target_sources(pico_shared_pwm_audio INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/pwm_audio.c
)
target_include_directories(pico_shared_pwm_audio INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
)
target_link_libraries(pico_shared_pwm_audio INTERFACE pico_stdlib hardware_pwm hardware_irq)
