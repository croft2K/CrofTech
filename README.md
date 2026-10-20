# CrofTech

A netcode-first, idTech-style engine for fast competitive first-person shooters.

## Requirements

- Windows: Visual Studio Build Tools 2026 with "Desktop development with C++" and "C++ Clang tools"
- CMake 3.28+ and Ninja
- vcpkg, cloned and bootstrapped, with `VCPKG_ROOT` set
- Vulkan SDK 1.3+ (needed from milestone 1)

## Build

    .\scripts\build.ps1                       # configure, build and test (windows-debug)
    .\scripts\build.ps1 -Preset windows-asan

On Linux: `cmake --workflow --preset linux-debug`.

See `CLAUDE.md` for code rules and project layout.
