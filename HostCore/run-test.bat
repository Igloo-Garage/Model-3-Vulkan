@echo off
cd /d "%~dp0.."
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /W3 /I. /Fe:HostCore\test_abi.exe HostCore\test_abi.cpp || exit /b 1
rem Model3VulkanHostCore.dll is self-contained: the zstd codec is managed code
rem compiled into the image, so there is no libzstd.dll to copy beside the test exe.
HostCore\test_abi.exe HostCore\publish\Model3VulkanHostCore.dll HostCore\thumb.png
