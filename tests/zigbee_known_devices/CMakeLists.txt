cmake_minimum_required(VERSION 3.16)
project(zigbee_known_devices_tests LANGUAGES CXX)

enable_testing()

add_executable(zigbee_known_devices_tests zigbee_known_devices_test.cpp)
target_include_directories(zigbee_known_devices_tests PRIVATE
    ../../firmware/p4/main
)
target_compile_features(zigbee_known_devices_tests PRIVATE cxx_std_17)

add_test(NAME zigbee_known_devices_tests COMMAND zigbee_known_devices_tests)