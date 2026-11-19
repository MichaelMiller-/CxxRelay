cmake_minimum_required(VERSION 3.20)

option(CXX_RELAY_BUILD_EXAMPLES "Build sample executables" ON)

project(CxxRelay VERSION 0.1.0 LANGUAGES CXX)

## Globals
##
set(CMAKE_CXX_STANDARD 20)

##
##
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

## Link to this 'library' in order to use the warnings specified in CompilerWarnings.cmake
##
include(CompilerWarnings)
add_library(cxx_relay_enable_compiler_warnings INTERFACE)
add_library(CxxRelay::enable_compiler_warnings ALIAS cxx_relay_enable_compiler_warnings)
enable_warnings(cxx_relay_enable_compiler_warnings)

## Dependencies
##
# add_subdirectory(dependencies)

## Scripts
##
# add_subdirectory(scripts)

## Tests
##
include(CTest)
if (BUILD_TESTING) # option is created by CTest
    enable_testing()
endif ()

## Sources
##
add_subdirectory(src)
