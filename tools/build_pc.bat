@echo off
setlocal

for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
if not defined PC_BUILD_DIR set "PC_BUILD_DIR=%PROJECT_ROOT%\build-pc\windows"
if not defined CMAKE_GENERATOR set "CMAKE_GENERATOR=MinGW Makefiles"
if not defined WRJ_ENABLE_PROFILING set "WRJ_ENABLE_PROFILING=OFF"

cmake -S "%PROJECT_ROOT%" -B "%PC_BUILD_DIR%" -G "%CMAKE_GENERATOR%" -DCMAKE_BUILD_TYPE=Release -DWRJ_ENABLE_PROFILING=%WRJ_ENABLE_PROFILING%
if errorlevel 1 exit /b %errorlevel%

cmake --build "%PC_BUILD_DIR%" --config Release --parallel
if errorlevel 1 exit /b %errorlevel%

if "%RUN_TESTS%"=="1" (
    ctest --test-dir "%PC_BUILD_DIR%" -C Release --output-on-failure
    if errorlevel 1 exit /b 1
)

echo PC Release build complete: %PC_BUILD_DIR%
endlocal
