param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$BuildDirectory = '',
    [switch]$Shared
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$BuildDirectory) {
    $folder = if ($Shared) { 'build/c-bindings-shared' } else { 'build/c-bindings-clang' }
    $BuildDirectory = Join-Path $repo $folder
}
$sharedValue = if ($Shared) { 'ON' } else { 'OFF' }
& (Join-Path $PSScriptRoot 'Generate-Wrappers.ps1')
& cmake -S $PSScriptRoot -B $BuildDirectory -G 'Visual Studio 18 2026' -A x64 -T ClangCL "-DLWS_BUILD_SHARED=$sharedValue"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake --build $BuildDirectory --config $Configuration --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'C wrapper build failed' }
& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'C wrapper tests failed' }
Write-Host "Verified library: $BuildDirectory/$Configuration/LWSC.lib"
