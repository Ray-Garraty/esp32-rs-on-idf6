idf_component_register(
    SRCS "src/burette.cpp"
         "src/log_buffer.cpp"
    INCLUDE_DIRS "include"
    REQUIRES "diag"   # log_buffer uses diag/stack_monitor.hpp for periodic watermark logging
)
