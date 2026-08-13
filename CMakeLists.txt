cmake_minimum_required(VERSION 3.24)
project(ampneve LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(AMPNEVE_BUILD_VST "Build the VST3 shell" OFF)
option(AMPNEVE_BUILD_TOOLS "Build offline renderer" ON)

include(CTest)

function(ampneve_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

add_library(ampneve_core STATIC core/ampsim.c)
target_include_directories(ampneve_core PUBLIC core)
ampneve_warnings(ampneve_core)

if(AMPNEVE_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(AMPNEVE_BUILD_VST)
    add_subdirectory(vst)
endif()

if(BUILD_TESTING)
    add_executable(test_ampsim tests/test_ampsim.c)
    target_include_directories(test_ampsim PRIVATE core)
    target_link_libraries(test_ampsim PRIVATE ampneve_core)
    ampneve_warnings(test_ampsim)
    add_test(NAME test_ampsim COMMAND test_ampsim)
endif()
