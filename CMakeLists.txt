cmake_minimum_required(VERSION 3.28)
project(CrofTech VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# No C++20 modules; skipping the scan step keeps builds fast and avoids needing clang-scan-deps.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# Dynamic CRT everywhere; matches the x64-windows-static-md vcpkg triplet.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# Every executable lands in one folder, next to the compiled shaders.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(WIN32)
    set(ct_client_default ON)
else()
    set(ct_client_default OFF)
endif()

option(CROFTECH_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CROFTECH_SANITIZE "Build with AddressSanitizer (plus UBSan where supported)" OFF)
option(CROFTECH_PROFILE "Enable Tracy profiling zones" OFF)
option(CROFTECH_BUILD_TESTS "Build the test suites" ON)
option(CROFTECH_FUZZ "Build only the fuzz targets, as libFuzzer executables (Linux clang)" OFF)
option(CROFTECH_BUILD_CLIENT "Build the client and renderer (needs SDL3 and the Vulkan SDK)"
       ${ct_client_default})

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompilerOptions)

find_package(recastnavigation CONFIG REQUIRED)

if(CROFTECH_PROFILE)
    find_package(Tracy CONFIG REQUIRED)
endif()

if(CROFTECH_BUILD_CLIENT)
    find_package(SDL3 CONFIG REQUIRED)
    # Vulkan headers come from vcpkg. Stop volk falling back to $VULKAN_SDK/include, which differs
    # between machines; the SDK is only used for glslc.
    set(VOLK_PULL_IN_VULKAN OFF)
    find_package(VulkanHeaders CONFIG REQUIRED)
    find_package(volk CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(vk-bootstrap CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_program(CT_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)
endif()

add_subdirectory(engine)

if(CROFTECH_BUILD_CLIENT)
    add_subdirectory(apps)
endif()

if(CROFTECH_FUZZ)
    add_subdirectory(tests/fuzz)
elseif(CROFTECH_BUILD_TESTS)
    enable_testing()
    find_package(doctest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
