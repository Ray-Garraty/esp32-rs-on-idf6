idf_component_register(
    SRCS
        "src/command.cpp"
        "src/dispatch.cpp"
        "src/state_machine.cpp"
        "src/scheduler.cpp"
        "src/handlers/burette_ops.cpp"
        "src/handlers/burette_cal.cpp"
        "src/handlers/sensors.cpp"
        "src/handlers/system.cpp"
        "src/handlers/valve.cpp"
        "src/handlers/serial.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        domain
        infrastructure
        json
)
