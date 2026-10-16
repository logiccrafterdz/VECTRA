@echo off
rem VECTRA: configure, build and test on Windows (Visual Studio generator).
rem   scripts\build.bat [Release|Debug]
setlocal enabledelayedexpansion

set "BUILD_TYPE=%~1"
if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Release"

set "ROOT=%~dp0.."

cmake -S "%ROOT%" -B "%ROOT%\build" || exit /b 1
cmake --build "%ROOT%\build" --config %BUILD_TYPE% || exit /b 1

echo.
echo == running tests ==
ctest --test-dir "%ROOT%\build" -C %BUILD_TYPE% --output-on-failure || exit /b 1
exit /b 0
