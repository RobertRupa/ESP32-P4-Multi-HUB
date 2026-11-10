cmake_minimum_required(VERSION 3.16)
project(zigbee_search_policy_tests LANGUAGES CXX)

enable_testing()

add_executable(zigbee_search_policy_tests zigbee_search_policy_test.cpp)
target_include_directories(zigbee_search_policy_tests PRIVATE
    ../../firmware/p4/main
)
target_compile_features(zigbee_search_policy_tests PRIVATE cxx_std_17)

add_test(NAME zigbee_search_policy_tests COMMAND zigbee_search_policy_tests)