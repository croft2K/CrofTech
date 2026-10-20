# Configures, builds and tests one preset: .\scripts\build.ps1 [-Preset windows-debug]
# Works from any shell: loads the Visual Studio x64 build environment if it is not already active.
param(
    [string]$Preset = 'windows-debug'
)
$ErrorActionPreference = 'Stop'

# vcvars64.bat points VCPKG_ROOT at Visual Studio's bundled vcpkg; keep the user's own clone.
$vcpkgRoot = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
if (-not $vcpkgRoot) { $vcpkgRoot = $env:VCPKG_ROOT }
if (-not $vcpkgRoot) { throw 'VCPKG_ROOT is not set. Clone and bootstrap vcpkg, then set VCPKG_ROOT.' }

# Shells opened before the Vulkan SDK was installed don't have VULKAN_SDK yet.
if (-not $env:VULKAN_SDK) {
    $env:VULKAN_SDK = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
}

if (-not $env:VSCMD_VER) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang -property installationPath
    if (-not $vsPath) { throw 'No Visual Studio install with the C++ Clang tools was found.' }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}
$env:VCPKG_ROOT = $vcpkgRoot

Push-Location (Join-Path $PSScriptRoot '..')
try {
    cmake --workflow --preset $Preset
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
