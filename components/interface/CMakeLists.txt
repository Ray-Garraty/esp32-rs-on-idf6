idf_component_register(
    SRCS
        "src/serial.cpp"
        "src/broadcast.cpp"
        "src/rest_api.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        domain
        application
        json
        esp_http_server
        esp_driver_uart
)
