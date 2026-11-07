cmake_minimum_required(VERSION 3.16)
project(device_registry_tests LANGUAGES CXX)

enable_testing()

add_library(device_registry
    ../../components/device_registry/src/device_registry.cpp
)
target_include_directories(device_registry PUBLIC
    ../../components/device_registry/include
)
target_compile_features(device_registry PUBLIC cxx_std_17)

add_executable(device_registry_tests device_registry_test.cpp)
target_link_libraries(device_registry_tests PRIVATE device_registry)

add_test(NAME device_registry_tests COMMAND device_registry_tests)