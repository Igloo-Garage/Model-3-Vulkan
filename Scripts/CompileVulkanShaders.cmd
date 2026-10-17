@echo off
setlocal
cd /d "%~dp0.."

set "GLSLANG="
if defined VULKAN_SDK if exist "%VULKAN_SDK%\Bin\glslangValidator.exe" set "GLSLANG=%VULKAN_SDK%\Bin\glslangValidator.exe"
if not defined GLSLANG if exist "Libraries\glslang\bin\glslang.exe" set "GLSLANG=Libraries\glslang\bin\glslang.exe"
if not defined GLSLANG if exist "Libraries\glslang\bin\glslangValidator.exe" set "GLSLANG=Libraries\glslang\bin\glslangValidator.exe"

if not defined GLSLANG (
    echo Downloading glslang...
    curl -L "https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip" -o "Libraries\glslang.zip"
    if errorlevel 1 exit /b 1
    powershell -NoLogo -Command "Expand-Archive -LiteralPath 'Libraries\glslang.zip' -DestinationPath 'Libraries\glslang' -Force"
    del "Libraries\glslang.zip"
    if exist "Libraries\glslang\bin\glslangValidator.exe" (
        set "GLSLANG=Libraries\glslang\bin\glslangValidator.exe"
    ) else (
        for /f "delims=" %%F in ('dir /s /b Libraries\glslang\glslang.exe Libraries\glslang\glslangValidator.exe 2^>nul') do set "GLSLANG=%%F"
    )
)

if not defined GLSLANG (
    echo ERROR: glslangValidator was not found.
    exit /b 1
)

call :build real3d.vert real3d_vert vert
if errorlevel 1 exit /b 1
call :build real3d.frag real3d_frag frag
if errorlevel 1 exit /b 1
call :build composite.vert composite_vert vert
if errorlevel 1 exit /b 1
call :build composite.frag composite_frag frag
if errorlevel 1 exit /b 1
call :build texdecode.comp texdecode_comp comp
if errorlevel 1 exit /b 1
call :build fog.vert fog_vert vert
if errorlevel 1 exit /b 1
call :build fog.frag fog_frag frag
if errorlevel 1 exit /b 1
call :build post.frag post_frag frag
if errorlevel 1 exit /b 1
echo Shader headers written to Src\Graphics\Vulkan3D\Shaders
exit /b 0

:build
set "SRC=Src\Graphics\Vulkan3D\Shaders\%~1"
set "NAME=%~2"
set "SPV=Src\Graphics\Vulkan3D\Shaders\%~2.spv"
rem Target SPIR-V 1.5, which is what --target-env vulkan1.2 produces.
rem
rem Deliberately not vulkan1.3: that emits SPIR-V 1.6, which requires Vulkan 1.3 or VK_KHR_maintenance4
rem to load. The backend runs on either, and 1.2 with the dynamic rendering and synchronization2
rem extensions is supported on purpose - see ClassifyDevice in VulkanContext.cpp - so requiring
rem maintenance4 for the shaders alone would shut those devices out at shader-module creation, with an
rem error that points at the shader rather than at the version. SPIR-V 1.5 loads on both.
"%GLSLANG%" -V --target-env vulkan1.2 -o "%SPV%" "%SRC%"
if errorlevel 1 exit /b 1
powershell -NoLogo -Command ^
  "$bytes = [System.IO.File]::ReadAllBytes('%SPV%');" ^
  "$sb = New-Object System.Text.StringBuilder;" ^
  "[void]$sb.AppendLine('#pragma once');" ^
  "[void]$sb.AppendLine('#include <cstddef>');" ^
  "[void]$sb.AppendLine('#include <cstdint>');" ^
  "[void]$sb.AppendLine('static const uint32_t %NAME%_spv[] = {');" ^
  "for ($i = 0; $i -lt $bytes.Length; $i += 4) {" ^
  "  $w = [BitConverter]::ToUInt32($bytes, $i);" ^
  "  [void]$sb.AppendFormat('0x{0:x8},', $w);" ^
  "  if (((($i/4)+1) %% 8) -eq 0) { [void]$sb.AppendLine() }" ^
  "}" ^
  "[void]$sb.AppendLine('};');" ^
  "[void]$sb.AppendLine('static const size_t %NAME%_spv_size = sizeof(%NAME%_spv);');" ^
  "[System.IO.File]::WriteAllText('Src\Graphics\Vulkan3D\Shaders\%NAME%.spv.h', $sb.ToString())"
exit /b %ERRORLEVEL%
