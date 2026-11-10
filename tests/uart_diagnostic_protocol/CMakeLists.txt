cmake_minimum_required(VERSION 3.16)
project(uart_diagnostic_protocol_tests LANGUAGES CXX)

enable_testing()

add_executable(uart_diagnostic_protocol_tests uart_diagnostic_protocol_test.cpp)
target_include_directories(uart_diagnostic_protocol_tests PRIVATE
    ../../firmware/common
)
target_compile_features(uart_diagnostic_protocol_tests PRIVATE cxx_std_17)

add_test(NAME uart_diagnostic_protocol_tests COMMAND uart_diagnostic_protocol_tests)