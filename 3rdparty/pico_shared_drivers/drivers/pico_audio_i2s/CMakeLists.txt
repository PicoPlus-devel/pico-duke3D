if (NOT TARGET pico_audio_i2s_shared)
    add_library(pico_audio_i2s_shared INTERFACE)
    pico_generate_pio_header(pico_audio_i2s_shared ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)
    target_sources(pico_audio_i2s_shared INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.c
    )
    target_include_directories(pico_audio_i2s_shared INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )
    target_link_libraries(pico_audio_i2s_shared INTERFACE
        pico_stdlib
        hardware_pio
        hardware_dma
        tlv320dac3100
    )
endif()
