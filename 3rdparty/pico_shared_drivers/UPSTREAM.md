A vendored subset of pico_shared (https://github.com/fhoedemakers/pico_shared). Local changes
are marked pico-duke3D:.

pico_hdmi/: video_output.c, hstx_data_island_queue.c and hstx_data_island_queue.h were re-copied
on 2026-11-11 from pico_shared 722f019, unchanged. This brings real DVI 1.0 output in DVI mode
(3e19ce0, PICO_HDMI_DVI_USE_DATA_ISLANDS 0), the HSTX block reset on start and resync (a252e47)
and the split back porch of DVI lines (722f019). CMakeLists.txt keeps its local queue size and
NOHSTXCLOCK and now links hardware_resets explicitly. hstx.c is not built.

pwm_audio/: local addition pwm_audio_get_fill() (the ring fill, for pacing the mixer by the
PWM jack alone in DVI display mode, when there is no HDMI audio).
