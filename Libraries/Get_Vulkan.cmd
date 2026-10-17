@echo off
setlocal
cd /d "%~dp0.."

echo === Vulkan-Headers 1.3.296 + volk ===

if not exist "Libraries" mkdir Libraries
if not exist "Src\Pkgs\volk" mkdir "Src\Pkgs\volk"

where curl >nul 2>&1
if errorlevel 1 (
    echo ERROR: curl is required.
    exit /b 1
)

if not exist "Libraries\Vulkan-Headers\include\vulkan\vulkan.h" (
    echo Downloading Vulkan-Headers...
    curl -L "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.3.296.zip" -o "Libraries\vulkan-headers.zip"
    if errorlevel 1 exit /b 1
    powershell -NoLogo -Command "Expand-Archive -LiteralPath 'Libraries\vulkan-headers.zip' -DestinationPath 'Libraries' -Force"
    if exist "Libraries\Vulkan-Headers" rmdir /s /q "Libraries\Vulkan-Headers"
    ren "Libraries\Vulkan-Headers-1.3.296" Vulkan-Headers
    del "Libraries\vulkan-headers.zip"
)

if not exist "Src\Pkgs\volk\volk.h" (
    echo Downloading volk...
    curl -L "https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.3.296.0.zip" -o "Libraries\volk.zip"
    if errorlevel 1 exit /b 1
    powershell -NoLogo -Command "Expand-Archive -LiteralPath 'Libraries\volk.zip' -DestinationPath 'Libraries' -Force"
    if exist "Libraries\volk-vulkan-sdk-1.3.296.0" (
        copy /Y "Libraries\volk-vulkan-sdk-1.3.296.0\volk.h" "Src\Pkgs\volk\volk.h" >nul
        copy /Y "Libraries\volk-vulkan-sdk-1.3.296.0\volk.c" "Src\Pkgs\volk\volk.c" >nul
        rmdir /s /q "Libraries\volk-vulkan-sdk-1.3.296.0"
    ) else (
        copy /Y "Libraries\volk-1.3.296\volk.h" "Src\Pkgs\volk\volk.h" >nul
        copy /Y "Libraries\volk-1.3.296\volk.c" "Src\Pkgs\volk\volk.c" >nul
        rmdir /s /q "Libraries\volk-1.3.296"
    )
    del "Libraries\volk.zip"
)

echo Vulkan headers are in Libraries\Vulkan-Headers\include
echo volk is in Src\Pkgs\volk
endlocal
