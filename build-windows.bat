@echo off
rem ---------------------------------------------------------------------------
rem  OJU - Windows build (VST3 + Standalone, x64)
rem
rem  Needs: Visual Studio 2022 or newer with "Desktop development with C++",
rem         CMake 3.22+ (bundled with Visual Studio), Git (for fetching JUCE).
rem
rem  Usage:  build-windows.bat            build Release
rem          build-windows.bat install    build, then copy OJU.vst3 into
rem                                       C:\Program Files\Common Files\VST3
rem                                       (run from an Administrator prompt)
rem ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found. Open "x64 Native Tools Command Prompt for VS" or install CMake from cmake.org.
    exit /b 1
)

cmake -S . -B build-win -A x64
if errorlevel 1 goto :fail

cmake --build build-win --config Release --parallel
if errorlevel 1 goto :fail

set "VST3=%~dp0build-win\OJU_artefacts\Release\VST3\OJU.vst3"
set "APP=%~dp0build-win\OJU_artefacts\Release\Standalone\OJU.exe"

echo.
echo  Built OK
echo    VST3       %VST3%
echo    Standalone %APP%
echo.

if /i "%~1"=="install" (
    echo Installing to C:\Program Files\Common Files\VST3 ...
    if exist "C:\Program Files\Common Files\VST3\OJU.vst3" rmdir /s /q "C:\Program Files\Common Files\VST3\OJU.vst3"
    xcopy /e /i /y /q "%VST3%" "C:\Program Files\Common Files\VST3\OJU.vst3" >nul
    if errorlevel 1 (
        echo Copy failed - run this from an Administrator command prompt.
        exit /b 1
    )
    echo Installed. Rescan plugins in your DAW.
)
exit /b 0

:fail
echo.
echo  Build failed - see the messages above.
exit /b 1
