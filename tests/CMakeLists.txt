cmake_minimum_required(VERSION 3.16)
project(ecotiter_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- nlohmann_json via FetchContent ---
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# --- Catch2 via FetchContent ---
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.1
)
FetchContent_MakeAvailable(Catch2)

# --- Application library (host-compilable subset) ---
set(APPLICATION_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/command.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/dispatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/state_machine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/scheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/handlers/burette_ops.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/handlers/burette_cal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/handlers/sensors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/handlers/system.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/handlers/valve.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/src/handlers/serial.cpp
)

set(DOMAIN_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/domain/src/burette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/domain/src/motion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/infrastructure/src/drivers/tmc_uart.cpp
)

# --- Interface library (host-compilable subset) ---
set(INTERFACE_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/interface/src/broadcast.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/interface/src/rest_api.cpp
)

add_library(ecotiter_app STATIC
    ${APPLICATION_SRCS}
    ${DOMAIN_SRCS}
    ${INTERFACE_SRCS}
)

target_include_directories(ecotiter_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/interface/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/infrastructure/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_link_libraries(ecotiter_app PUBLIC nlohmann_json::nlohmann_json)

# --- Host stubs for components not under test ---
set(STUB_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stub_motor_task.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stub_nvs.cpp
)

# --- Unit test executable ---
add_executable(unit_tests
    ${STUB_SRCS}
    src/test_main.cpp
    src/test_types.cpp
    src/test_burette.cpp
    src/test_adc.cpp
    src/test_valve.cpp
    src/test_command.cpp
    src/test_dispatch.cpp
    src/test_handlers.cpp
    src/test_state_machine.cpp
    src/test_serial.cpp
    src/test_broadcast.cpp
    src/test_rest_api.cpp
    src/test_scheduler.cpp
    src/test_ble_init.cpp
    src/test_dns.cpp
    src/test_http_config.cpp
    src/test_json_utils.cpp
    src/test_mdns.cpp
    src/test_cal_math.cpp
    src/test_motion.cpp
    src/test_wifi_sta.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/domain/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/application/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/interface/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/infrastructure/include
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)

target_compile_definitions(unit_tests PRIVATE
    TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(unit_tests PRIVATE
    ecotiter_app
    Catch2::Catch2WithMain
)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
