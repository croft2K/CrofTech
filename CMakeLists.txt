cmake_minimum_required(VERSION 3.28)
project(CrofTech VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules; skipping the scan step keeps builds fast and avoids needing clang-scan-deps.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# Dynamic CRT everywhere; matches the x64-windows-static-md vcpkg triplet.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

option(CROFTECH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CROFTECH_SANITIZE "Build with AddressSanitizer (plus UBSan where supported)" OFF)
option(CROFTECH_PROFILE "Enable Tracy profiling zones" OFF)
option(CROFTECH_BUILD_TESTS "Build the test suites" ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompilerOptions)

if(CROFTECH_PROFILE)
    find_package(Tracy CONFIG REQUIRED)
endif()

add_subdirectory(engine)

if(CROFTECH_BUILD_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
