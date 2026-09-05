<#
.SYNOPSIS
    Build calendae.exe and package it as a portable, self-contained zip.

.DESCRIPTION
    Configures + builds calendae against the given Qt (MSVC, dynamically
    linked -- there is no lean Qt for Windows, unlike Linux), then runs
    windeployqt to copy every DLL/plugin/translation calendae needs next to
    the binary, and zips the result. No installer, no registry writes: unzip
    and run, matching the AppImage/tarball experience on Linux.

    Needs a Visual Studio "Developer" environment on PATH (cl.exe, cmake,
    ninja) -- see .github/workflows/release.yml's use of
    ilammy/msvc-dev-cmd, or run this from a "Developer PowerShell for VS".

.PARAMETER Version
    Version string embedded in the build and the output filename.

.PARAMETER QtDir
    Path to the Qt kit's platform directory, e.g.
    C:\Qt\6.8.3\msvc2022_64 -- must contain bin\windeployqt.exe.

.PARAMETER OutDir
    Where to drop the .zip. Default: .\dist
#>
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$QtDir,
    [string]$OutDir = "dist"
)
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$BuildDir = Join-Path $Root "build-windows"
$WindeployQt = Join-Path $QtDir "bin\windeployqt.exe"

if (-not (Test-Path $WindeployQt)) {
    throw "windeployqt.exe not found under '$QtDir\bin' -- is -QtDir correct?"
}

Write-Host ">> configure"
cmake -S $Root -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH="$QtDir" `
    -DCALENDAE_VERSION="$Version" `
    -DTINY_GCAL_BUILD_TESTS=OFF
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host ">> build"
cmake --build $BuildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$StageDir = Join-Path $OutDir "calendae-$Version-windows-x64"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null

Copy-Item (Join-Path $BuildDir "calendae.exe") $StageDir
Copy-Item (Join-Path $Root "LICENSE") $StageDir

Write-Host ">> windeployqt"
# --compiler-runtime bundles the MSVC redistributable DLLs (vcruntime140.dll
# etc.) too, so the zip runs on a clean Windows install without the user
# separately installing the Visual C++ Redistributable.
& $WindeployQt --release --compiler-runtime (Join-Path $StageDir "calendae.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$ZipPath = Join-Path $OutDir "calendae-$Version-windows-x64.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }

Write-Host ">> zip"
Compress-Archive -Path (Join-Path $StageDir "*") -DestinationPath $ZipPath

Write-Host ">> $ZipPath"
