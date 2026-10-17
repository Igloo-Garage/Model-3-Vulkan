@echo off
rem ============================================================================
rem  build-run.bat - compile RomAudit and run it.
rem
rem  Compiles the tool with the C# compiler that ships with the same Visual Studio
rem  installation the rest of the project builds with, so there is nothing extra to
rem  install. Builds to RomAudit.exe next to this script, then runs it.
rem
rem  Usage:
rem      build-run.bat                             build, then audit the default folder
rem      build-run.bat build                       build only
rem      build-run.bat <romdir> <games.xml>        build, then audit the given folder
rem      build-run.bat <romdir> <games.xml> --set skichamp
rem
rem  Environment overrides:
rem      ROMDIR     default ROM folder
rem      GAMESXML   default Games.xml
rem ============================================================================
setlocal EnableExtensions

set "HERE=%~dp0"
if "%HERE:~-1%"=="\" set "HERE=%HERE:~0,-1%"
set "PROJROOT=%HERE%\..\.."

if not defined ROMDIR   set "ROMDIR=E:\EMU\model-3-romset-2020"
if not defined GAMESXML set "GAMESXML=%PROJROOT%\Config\Games.xml"

rem ---- locate csc.exe (the Roslyn compiler is inside MSBuild) -----------------
rem The paths are tested one at a time rather than looped over, because a `for /d` whose set contains
rem "(x86)" is split on the parentheses and the whole block fails to parse - which reads as "no
rem compiler installed" rather than as a syntax problem in this script.
set "CSC="
set "VSROOT=%ProgramFiles%\Microsoft Visual Studio\2022"
if exist "%VSROOT%\Community\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Community\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\Professional\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Professional\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\Enterprise\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Enterprise\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\BuildTools\MSBuild\Current\Bin\Roslyn\csc.exe"
if not defined CSC if exist "%VSROOT%\Preview\MSBuild\Current\Bin\Roslyn\csc.exe" set "CSC=%VSROOT%\Preview\MSBuild\Current\Bin\Roslyn\csc.exe"

rem Fall back to whatever is on PATH, which covers a Developer Command Prompt.
if not defined CSC (
    for %%I in (csc.exe) do if not "%%~$PATH:I"=="" set "CSC=%%~$PATH:I"
)

if not defined CSC (
    echo [ERROR] csc.exe not found.
    echo         Install Visual Studio with the C# workload, or run this from a
    echo         Developer Command Prompt where csc is already on PATH.
    exit /b 1
)

echo Compiling RomAudit...
"%CSC%" /nologo /target:exe /optimize+ /platform:anycpu ^
    /out:"%HERE%\RomAudit.exe" ^
    /r:System.IO.Compression.dll ^
    /r:System.IO.Compression.FileSystem.dll ^
    "%HERE%\RomAudit.cs"
if errorlevel 1 (
    echo [ERROR] compile failed.
    exit /b 1
)
echo   built %HERE%\RomAudit.exe
echo.

if /i "%~1"=="build" exit /b 0

rem ---- run -------------------------------------------------------------------
rem No arguments of our own: use the defaults and pass through anything given.
if "%~1"=="" (
    "%HERE%\RomAudit.exe" "%ROMDIR%" "%GAMESXML%" %2 %3 %4 %5
) else (
    "%HERE%\RomAudit.exe" %*
)
exit /b %ERRORLEVEL%
