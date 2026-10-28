@echo off
rem ============================================================================
rem  build-cmake.bat - Stage 2 CMake/Ninja build
rem
rem  Produces build-cmake\bin\Supermodel-LegacyOG.exe (SDL2 + OpenGL backend) and
rem  build-cmake\bin\Model3-Vulkan.exe (SDL3 + Vulkan backend).
rem  The historical build.bat / MSVC solution remains the release packager until
rem  the CMake build has passed a full regression pass.
rem
rem  Usage:
rem      build-cmake.bat
rem      build-cmake.bat -DUSE_SDL3=ON
rem ============================================================================

setlocal EnableExtensions

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

rem The splash artwork is compiled in and checked against the hash in
rem Src\OSD\Windows\SplashResource.h. Refresh it here too, so building this
rem target on its own cannot produce an executable that calls itself UNOFFICIAL.
if exist "%ROOT%\Scripts\UpdateSplashSignature.ps1" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\Scripts\UpdateSplashSignature.ps1"
)

rem Locate the newest Visual Studio installation that has the C++ toolchain.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSDIR=%%I"
    )
)
if not defined VSDIR if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
)
if not defined VSDIR (
    echo Could not find a Visual Studio C++ toolchain.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

cmake -S "%ROOT%" -B "%ROOT%\build-cmake" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DUSE_SDL2=ON ^
    -DUSE_SDL3=OFF ^
    -DSUPERMODEL_SDL3_PURE_VULKAN=ON ^
    %*
if errorlevel 1 exit /b 1

cmake --build "%ROOT%\build-cmake" --config Release --parallel
if errorlevel 1 exit /b 1

echo.
echo Built: %ROOT%\build-cmake\bin\Supermodel-LegacyOG.exe
if exist "%ROOT%\build-cmake\bin\Model3-Vulkan.exe" (
    echo Built: %ROOT%\build-cmake\bin\Model3-Vulkan.exe
)
