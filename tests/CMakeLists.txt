cmake_minimum_required(VERSION 3.16)
project(ecotiter_tests CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(unit_tests
    src/test_main.cpp
    src/test_types.cpp
    src/test_burette.cpp
    src/test_adc.cpp
    src/test_valve.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/domain/src/burette.cpp
)

target_include_directories(unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/catch2
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/domain/include
)

target_compile_definitions(unit_tests PRIVATE CATCH_CONFIG_MAIN=0)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
