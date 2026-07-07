idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        diag
        domain
        infrastructure
        nvs_flash
        freertos
        esp_system
        esp_driver_gpio
        esp_driver_rmt
        esp_timer
)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")
