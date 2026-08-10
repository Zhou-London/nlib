cmake_minimum_required(VERSION 3.28)

project(nlib
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Header-only.
add_library(nlib INTERFACE)
add_library(nlib::nlib ALIAS nlib)

target_include_directories(nlib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(nlib INTERFACE cxx_std_23)

option(NLIB_BUILD_TESTS "Build the nlib tests" ${PROJECT_IS_TOP_LEVEL})
if(NLIB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
