cmake_minimum_required(VERSION 3.16)
project(zigbee_level_state_tests LANGUAGES CXX)

enable_testing()

add_executable(zigbee_level_state_tests zigbee_level_state_test.cpp)
target_include_directories(zigbee_level_state_tests PRIVATE
    ../../firmware/p4/main
)
target_compile_features(zigbee_level_state_tests PRIVATE cxx_std_17)

add_test(NAME zigbee_level_state_tests COMMAND zigbee_level_state_tests)