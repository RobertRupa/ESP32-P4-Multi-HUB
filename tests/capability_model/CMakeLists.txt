cmake_minimum_required(VERSION 3.16)
project(capability_model_tests LANGUAGES CXX)

enable_testing()

add_library(capability_model
    ../../components/capability_model/src/capability.cpp
)
target_include_directories(capability_model PUBLIC
    ../../components/capability_model/include
)
target_compile_features(capability_model PUBLIC cxx_std_17)

add_executable(capability_model_tests capability_model_test.cpp)
target_link_libraries(capability_model_tests PRIVATE capability_model)

add_test(NAME capability_model_tests COMMAND capability_model_tests)