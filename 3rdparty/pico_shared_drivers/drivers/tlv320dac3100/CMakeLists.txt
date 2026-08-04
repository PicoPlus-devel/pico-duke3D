if (NOT TARGET tlv320dac3100)
    add_library(tlv320dac3100 INTERFACE)
    target_sources(tlv320dac3100 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/tlv320dac3100.c
    )
    target_include_directories(tlv320dac3100 INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )
    target_link_libraries(tlv320dac3100 INTERFACE
        pico_stdlib
        hardware_i2c
        hardware_gpio
        # i2c_bus_clear() before every init attempt: the codec shares SDA/SCL
        # with the Wii extension port, and an attached pad can leave the bus
        # wedged (see the driver's own comments).
        i2c_bus_recovery
    )
endif()
