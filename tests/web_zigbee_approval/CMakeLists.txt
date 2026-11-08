cmake_minimum_required(VERSION 3.16)
project(web_zigbee_approval_tests LANGUAGES CXX)

enable_testing()

add_executable(web_zigbee_approval_tests
    ../../firmware/p4/main/web_zigbee_approval.cpp
    web_zigbee_approval_test.cpp
)
target_include_directories(web_zigbee_approval_tests BEFORE PRIVATE
    mocks
    ../../firmware/p4/main
)
target_compile_features(web_zigbee_approval_tests PRIVATE cxx_std_17)

add_test(NAME web_zigbee_approval_tests COMMAND web_zigbee_approval_tests)