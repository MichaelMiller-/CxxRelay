cmake_minimum_required(VERSION 3.20)

option(CXXRELAY_BUILD_EXAMPLES "Build sample executables" ON)

project(CxxRelay VERSION 0.1.0 LANGUAGES CXX)

## Globals
##
set(CMAKE_CXX_STANDARD 20)

##
##
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(FetchContent)
include(CMakePrintHelpers)

## Link to this 'library' in order to use the warnings specified in CompilerWarnings.cmake
##
include(CompilerWarnings)
add_library(cxx_relay_enable_compiler_warnings INTERFACE)
add_library(CxxRelay::enable_compiler_warnings ALIAS cxx_relay_enable_compiler_warnings)
enable_warnings(cxx_relay_enable_compiler_warnings)

## CPM
##
file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.0/CPM.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
        EXPECTED_HASH SHA256=2020b4fc42dba44817983e06342e682ecfc3d2f484a581f11cc5731fbe4dce8a
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

## Tests
##
include(CTest)
if (BUILD_TESTING) # option is created by CTest
    enable_testing()
endif ()

## Sources
##
add_subdirectory(src)
