param([string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$taskProjectRoot = Split-Path -Parent $PSScriptRoot
$taskPortableRoot = Join-Path $taskProjectRoot 'tools\host_toolchain'
if (Test-Path -LiteralPath (Join-Path $taskPortableRoot 'w64devkit\bin\gcc.exe')) {
    $env:PATH = (Join-Path $taskPortableRoot 'w64devkit\bin') + ';' +
        (Join-Path $taskPortableRoot 'cmake\bin') + ';' + $env:PATH
}
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue) -or
    -not (Get-Command gcc.exe -ErrorAction SilentlyContinue)) {
    throw 'GCC/CMake unavailable. Provide tools/host_toolchain or place compatible tools on PATH; see docs/C_PORT_STATUS.md.'
}
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $taskProjectRoot 'results_c_validation\final\build'
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$taskAllowedRoot = [IO.Path]::GetFullPath((Join-Path $taskProjectRoot 'results_c_validation'))
if (-not $BuildDirectory.StartsWith($taskAllowedRoot + '\',[StringComparison]::OrdinalIgnoreCase)) {
    throw 'E34 host build must remain under results_c_validation.'
}
& cmake.exe -S $taskProjectRoot -B $BuildDirectory -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DWRJ_ENABLE_PROFILING=OFF
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
& cmake.exe --build $BuildDirectory --parallel
if ($LASTEXITCODE -ne 0) { throw 'C build failed' }
& ctest.exe --test-dir $BuildDirectory -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'CTest failed' }
