idf_component_register(
    SRCS "src/burette.cpp"
         "src/log_buffer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ""   # NO ESP-IDF dependencies — pure logic only
)
