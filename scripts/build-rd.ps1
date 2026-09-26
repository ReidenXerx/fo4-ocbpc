<#
.SYNOPSIS
  Builds the Runtime Database cbp.dll (CommonLibF4RD): one DLL for Fallout 4 OG, NG and AE.

.DESCRIPTION
  Configures with the Visual Studio 2022 generator and vcpkg (manifest mode, x64-windows-static-md, pinned in
  CMakeLists.txt) into build-rd\, then builds. The classic OG build (OpenCBP_FO4.sln) is not touched.
  Run from PowerShell, not Git Bash (the fo4-silhouette port measured Git Bash's vcvars route doing nothing).
  CMake caches the toolchain on the FIRST configure only: a configure that ran without vcpkg keeps failing
  until build-rd\ is deleted (fo4-silhouette, 2026-09-26). ASCII only.
#>
[CmdletBinding()]
param(
    [string] $Config = 'Release',
    [string] $Vcpkg  = $(if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:USERPROFILE 'vcpkg' })
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build-rd'

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
if (-not (Test-Path $cmake)) { throw "No cmake: install the VS 2022 Build Tools (C++ workload)." }
if (-not (Test-Path (Join-Path $root 'extern\CommonLibF4RD\CommonLibF4\CMakeLists.txt'))) {
    throw "CommonLibF4RD is missing: git submodule update --init --recursive"
}
$toolchain = Join-Path $Vcpkg 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path $toolchain)) { throw "No vcpkg at $Vcpkg (set VCPKG_ROOT)." }

& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config $Config
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

$dll = Get-Item (Join-Path $build "$Config\cbp.dll")
Write-Host ("built {0}  {1} bytes  {2:yyyy-MM-dd HH:mm:ss}" -f $dll.FullName, $dll.Length, $dll.LastWriteTime)
