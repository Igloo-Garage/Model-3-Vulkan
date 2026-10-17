@echo off
rem ============================================================================
rem  build-run.bat - compile SaveStateDump and run it on a save state.
rem
rem  Usage:
rem      build-run.bat build                        compile only
rem      build-run.bat <file.st> [outdir]           compile, then dump
rem
rem  With no arguments it uses STFILE / OUTDIR if set, otherwise prints usage.
rem ============================================================================
setlocal EnableExtensions

set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"

set "CSC="
set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022"
if exist "%VSROOT%\Community\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\Professional\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Professional\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\Enterprise\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Enterprise\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\Preview\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Preview\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC (
    for %%I in (csc.exe) do if not "%%~$PATH:I"=="" set "CSC=%%~$PATH:I"
)
if not defined CSC (
    echo [ERROR] csc.exe not found. Install Visual Studio with the C# workload,
    echo         or run this from a Developer Command Prompt.
    exit /b 1
)

echo Compiling SaveStateDump...
"%CSC%" /nologo /target:exe /optimize+ /platform:anycpu ^
    /out:"%HERE%\SaveStateDump.exe" ^
    /r:System.Drawing.dll ^
    "%HERE%\SaveStateDump.cs"
if errorlevel 1 (
    echo [ERROR] compile failed.
    exit /b 1
)
echo   built %HERE%\SaveStateDump.exe
echo.

if /i "%~1"=="build" exit /b 0

if "%~1"=="" (
    if not defined STFILE (
        echo usage: build-run.bat ^<file.st^> [outdir]
        exit /b 2
    )
    "%HERE%\SaveStateDump.exe" "%STFILE%" "%OUTDIR%"
) else (
    "%HERE%\SaveStateDump.exe" %*
)
exit /b %ERRORLEVEL%
