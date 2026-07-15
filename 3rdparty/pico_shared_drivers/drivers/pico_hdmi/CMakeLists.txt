if (NOT TARGET pico_hdmi)
    add_library(pico_hdmi INTERFACE)
    if (PICO_RP2350)
        target_sources(pico_hdmi INTERFACE
                    ${CMAKE_CURRENT_LIST_DIR}/video_output.c
                    ${CMAKE_CURRENT_LIST_DIR}/hstx_packet.c
                    ${CMAKE_CURRENT_LIST_DIR}/hstx_data_island_queue.c
            )
        # NOTE: fruitjam-doom omits hstx.c on purpose. Upstream's hstx.c owns
        # a 154 KB static FRAMEBUFFER we would never use — Doom renders
        # directly from its own 8bpp frame_buffer[] via a custom scanline
        # callback (see src/pico/pico_hdmi_glue.c + i_video.c). The init,
        # vsync-callback, audio-push and framebuffer wrappers that live in
        # hstx.c are re-provided (or replaced) in pico_hdmi_glue.c.
        target_include_directories(pico_hdmi INTERFACE
                    ${CMAKE_CURRENT_LIST_DIR}
            )
        target_link_libraries(pico_hdmi INTERFACE
                    pico_stdlib
                    pico_multicore
                    hardware_pio
                    hardware_dma
                    hardware_clocks
            )
        # Doom drives clk_sys at 264 MHz. pico_hdmi's default clock config sets
        # clk_hstx = clk_sys / MODE_HSTX_CLK_DIV (=1), which would give 264 MHz
        # — twice the ~126 MHz the HSTX peripheral needs for a 25.2 MHz pixel
        # clock. NOHSTXCLOCK skips that config; pico_hdmi_glue.c installs a
        # fractional-divider clock_configure(clk_hstx, ..., 125_875_000) before
        # calling video_output_init(), matching the pre-existing sys/hstx
        # ratio the old Framebuffer_RP2350.c used.
        target_compile_definitions(pico_hdmi INTERFACE NOHSTXCLOCK=1)
        # Ring is 128 slots (~18 KB) — the value we've measured works
        # cleanly with Doom's 128-sample mix chunks. HSTX_DI_RING_ADDRESS in
        # fruitjam_cflags.h pins the buffer at 0x20076000, so this SRAM
        # doesn't come out of Doom's zone. Upstream default 256 caused a
        # regression to noisy audio on the observed hardware — kept low here
        # until we understand why.
        # Ring depth is load-bearing: UART diagnostics showed ~2400 DI-queue
        # underruns/s with a 96-packet (8 ms) cap — Doom's mixer is bursty
        # (long gaps during heavy render frames) and 8 ms doesn't bridge
        # them, so silence packets were injected mid-stream ~2400x/s
        # (= audible static) while real samples queued up behind them
        # (= ~20% tempo drag vs I2S). The I2S ring rides out the same bursts
        # with its 1024-sample (21 ms) depth. 224 packets = 896 samples
        # ≈ 18.7 ms gives HDMI the same margin. 256 slots × 144 B = 36 KB
        # sits at fixed 0x20076000 (see HSTX_DI_RING_ADDRESS), ending at
        # 0x2007F000 — still below __HeapLimit (0x20080000).
        # (Verified on hardware 2026-07-05: with 224-packet watermark the
        # underrun counter stays frozen through gameplay; at 96 it grew by
        # ~2400/s. Re-enable HSTX_DEBUG=1 here to get the 1 Hz UART dump of
        # frames/s, irqs/s, resyncs, DI level and underrun count.)
        target_compile_definitions(pico_hdmi INTERFACE
                    DI_RING_BUFFER_SIZE=256
                    HSTX_AUDIO_DI_HIGH_WATERMARK=224
            )
    else()
        target_include_directories(pico_hdmi INTERFACE
                    ${CMAKE_CURRENT_LIST_DIR}
            )
        target_compile_definitions(pico_hdmi INTERFACE PICO_HDMI_DUMMY=1 PICO_HDMI_DISABLED=1)
    endif()
endif()
