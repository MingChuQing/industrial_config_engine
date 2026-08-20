@echo off
rem One-click build + run: prints all paper conclusions in English.
rem Requirements: CMake 3.14+, Ninja, MSVC (Visual Studio 2022) or GCC/Clang.
chcp 65001 >nul
cd /d "%~dp0"

rem Locate cmake (PATH first, then VS 2022 bundled location)
where cmake >nul 2>nul
if errorlevel 1 (
    set "PATH=%PATH%;D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
)

echo === Configuring build (Ninja) ===
cmake -S . -B build\simcheck -G Ninja
if errorlevel 1 exit /b 1

echo === Building ===
cmake --build build\simcheck
if errorlevel 1 exit /b 1

echo.
echo === Running full paper summary (demo_summary) ===
build\simcheck\bin\demo_summary.exe
